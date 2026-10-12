/*
 * mod-raid-clear: bots don't back into packs that aren't in the fight. See AggroLeash.h.
 *
 * Released under the MIT License.
 */

#include "AggroLeash.h"

#include "RaidClearConfig.h"
#include "RaidClearRegistry.h"
#include "Common/TankRoles.h"

#include "AllMapScript.h"
#include "Creature.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace RaidClear;

void RaidClearLeashStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Above every "move out" there is (the raid mechanics sit at ACTION_RAID, the life-savers
    // at ACTION_EMERGENCY + 5 or 6): a second pack costs more than whatever the bot was dodging.
    triggers.push_back(new TriggerNode("rc aggro leash", { NextAction("rc aggro leash", ACTION_EMERGENCY + 8) }));
}

namespace
{
    // Mobs further away than this can't matter to where the bot is or is going.
    constexpr float SCAN_RANGE = 75.0f;
    // A mob more than this above or below doesn't aggro (the core allows 3 yd).
    constexpr float AGGRO_HEIGHT = 6.0f;
    // A walk is sampled this often along the way.
    constexpr float PATH_STEP = 2.0f;
    // How long the list of an instance's idle mobs is kept.
    constexpr uint32 IDLE_SCAN_MS = 250;
    // After a walk is stopped the bot doesn't start another for this long, so the action that
    // sent it doesn't send it again on its next update (ten times a second for a bot led by a
    // player). Short enough that a dodge in a safe direction is only late, not missed.
    constexpr float HOLD_MS = 700.0f;
    // A spot more than this above or below the bot is a ledge or a pit.
    constexpr float SPOT_HEIGHT = 5.0f;
    constexpr uint32 STEP_BACK_CHECK_MS = 300;
    // After the step-back search finds nowhere to go, wait this long before searching again.
    constexpr uint32 STEP_BACK_RETRY_MS = 2000;

    // Per instance. Only ever touched from that instance's map update (the hook below and the
    // bots' own AI, which runs inside Player::Update), so it needs no lock of its own.
    struct InstanceState
    {
        bool scanned = false;
        uint32 scanMs = 0;
        GuidVector idle;  // creatures that would aggro, whoever walks up
        std::unordered_map<ObjectGuid, uint32> lastWalk;  // per bot: the last spline that was looked at
    };

    std::mutex sStatesMutex;
    std::unordered_map<uint32, std::shared_ptr<InstanceState>> sStates;

    std::shared_ptr<InstanceState> StateOf(Map* map)
    {
        std::lock_guard<std::mutex> lock(sStatesMutex);
        std::shared_ptr<InstanceState>& state = sStates[map->GetInstanceId()];
        if (!state)
            state = std::make_shared<InstanceState>();
        return state;
    }

    // A creature that would come at someone who walks into its aggro radius.
    bool WouldAggro(Creature const* creature)
    {
        return creature->IsInWorld() && creature->IsAlive() && !creature->IsInCombat() &&
               !creature->IsInEvadeMode() && creature->HasReactState(REACT_AGGRESSIVE) && creature->IsVisible() &&
               !creature->IsCivilian() && !creature->IsTrigger() && !creature->IsNeutralToAll() &&
               !creature->IsPet() && !creature->IsTotem() && !creature->GetCharmerOrOwnerGUID().IsPlayer() &&
               creature->GetCreatureType() != CREATURE_TYPE_CRITTER && !creature->IsImmuneToPC() &&
               !creature->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) && !creature->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
    }

    // The instance's spawns, walked a few times a second for the whole raid. (Playerbots'
    // "possible targets" would do a grid search per bot on every call.)
    void RefreshIdle(Map* map, InstanceState& state)
    {
        uint32 const now = getMSTime();
        if (state.scanned && getMSTimeDiff(state.scanMs, now) < IDLE_SCAN_MS)
            return;
        state.scanned = true;
        state.scanMs = now;
        state.idle.clear();
        for (auto const& [spawnId, creature] : map->GetCreatureBySpawnIdStore())
            if (creature && WouldAggro(creature))
                state.idle.push_back(creature->GetGUID());
    }

    struct IdleMob
    {
        Creature* creature;
        float x;
        float y;
        float z;
        float aggro;  // its aggro radius for this bot, centre to centre
        // Its aggro radius plus the buffer already covers one of the bot's targets: there is no
        // getting at that target without coming this close.
        bool tied;
    };

    // What the bot is attacking and what it is about to attack. They differ during a pull:
    // playerbots sets "current target" first and the swing follows.
    struct BotTargets
    {
        Unit* victim = nullptr;
        Unit* current = nullptr;

        bool Has(Unit const* unit) const { return unit && (unit == victim || unit == current); }
        bool Any() const { return victim || current; }
    };

    BotTargets TargetsOf(PlayerbotAI* botAI, Player* bot)
    {
        BotTargets targets;
        targets.victim = bot->GetVictim();
        targets.current = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (targets.victim && !targets.victim->IsAlive())
            targets.victim = nullptr;
        if (targets.current && !targets.current->IsAlive())
            targets.current = nullptr;
        return targets;
    }

    // The idle mobs around this bot. Its own targets are left out: attacking a mob that isn't in
    // combat yet is a pull.
    std::vector<IdleMob> IdleMobsNear(Player* bot, InstanceState& state, BotTargets const& targets, float buffer)
    {
        Map* map = bot->GetMap();
        RefreshIdle(map, state);

        std::vector<IdleMob> mobs;
        for (ObjectGuid const& guid : state.idle)
        {
            Creature* creature = map->GetCreature(guid);
            if (!creature || targets.Has(creature) || !WouldAggro(creature))
                continue;
            if (bot->GetExactDist2d(creature) > SCAN_RANGE || !creature->IsHostileTo(bot))
                continue;

            float const aggro = creature->GetAggroRange(bot);
            bool const tied = (targets.victim && targets.victim->GetExactDist2d(creature) < aggro + buffer) ||
                              (targets.current && targets.current->GetExactDist2d(creature) < aggro + buffer);
            mobs.push_back({ creature, creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(),
                             aggro, tied });
        }
        return mobs;
    }

    // Yards between a spot and the edge of the nearest mob's aggro radius; negative inside it.
    // Mobs on another level don't count. A mob tied to the bot's target gets `tiedSlack` yards
    // for free: the fight is next to it, so a spot only has to be outside its radius, not
    // outside the buffer as well.
    float Clearance(std::vector<IdleMob> const& mobs, float x, float y, float z, float tiedSlack)
    {
        float clearance = 1000.0f;
        for (IdleMob const& mob : mobs)
            if (std::fabs(mob.z - z) <= AGGRO_HEIGHT)
                clearance = std::min(clearance,
                                     std::hypot(x - mob.x, y - mob.y) - mob.aggro + (mob.tied ? tiedSlack : 0.0f));
        return clearance;
    }

    // Whether a walk from the bot to a spot takes it closer to one of its targets.
    bool Approaches(Player* bot, BotTargets const& targets, float x, float y)
    {
        for (Unit* target : { targets.victim, targets.current })
            if (target && target->GetExactDist2d(x, y) < target->GetExactDist2d(bot) - 1.0f)
                return true;
        return false;
    }

    // Whether a stretch of a walk passes too close to one of the mobs.
    //   - A walk toward a target (going to the fight, or pulling): mobs tied to the target don't
    //     count, and the others only have to be passed by half the buffer. Round the corner of a
    //     pack is fine, through it is not.
    //   - Any other walk (backing off, spreading out, dodging): every mob counts. A boss tanked
    //     next to an idle pack doesn't make that pack safe to back into. The full buffer for
    //     most; half of it for a mob tied to the target, so a melee bot shifting a few yards
    //     around its target isn't stopped for nothing.
    bool StretchPulls(Player* bot, std::vector<IdleMob> const& mobs, bool approach, float buffer,
                      G3D::Vector3 const& from, G3D::Vector3 const& to)
    {
        float const botX = bot->GetPositionX();
        float const botY = bot->GetPositionY();
        float const length = std::hypot(to.x - from.x, to.y - from.y);
        int const steps = std::max(1, int(std::ceil(length / PATH_STEP)));
        for (int step = 1; step <= steps; ++step)
        {
            float const t = float(step) / float(steps);
            float const x = from.x + (to.x - from.x) * t;
            float const y = from.y + (to.y - from.y) * t;
            float const z = from.z + (to.z - from.z) * t;
            for (IdleMob const& mob : mobs)
            {
                if ((approach && mob.tied) || std::fabs(mob.z - z) > AGGRO_HEIGHT)
                    continue;
                float const margin = approach || mob.tied ? buffer * 0.5f : buffer;
                float const there = std::hypot(x - mob.x, y - mob.y);
                // Only a walk that takes the bot closer to the mob than it already is.
                if (there < mob.aggro + margin && there < std::hypot(botX - mob.x, botY - mob.y) - 0.5f &&
                    mob.creature->IsWithinLOS(x, y, z + 2.0f))
                    return true;
            }
        }
        return false;
    }

    // Whether the walk the bot is on passes too close to one of the mobs.
    bool WalkPulls(Player* bot, std::vector<IdleMob> const& mobs, BotTargets const& targets, float buffer)
    {
        Movement::MoveSpline const* walk = bot->movespline;
        if (mobs.empty() || walk->Finalized())
            return false;

        G3D::Vector3 const dest = walk->FinalDestination();
        bool const approach = Approaches(bot, targets, dest.x, dest.y);

        // From where the bot is, along what is left of the path.
        G3D::Vector3 from(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        Movement::MoveSpline::MySpline const& spline = walk->_Spline();
        for (int32 i = walk->_currentSplineIdx() + 1; i <= spline.last(); ++i)
        {
            G3D::Vector3 const& to = spline.getPoint(i);
            if (StretchPulls(bot, mobs, approach, buffer, from, to))
                return true;
            from = to;
        }
        return false;
    }

    // Whether the bot is standing at the edge of a mob's aggro radius. The line is a yard inside
    // what a walk toward a target may pass at, so a bot that just got there isn't sent back out.
    // Mobs tied to a target don't count: that is where the fight is.
    bool StandsTooClose(Player* bot, std::vector<IdleMob> const& mobs, BotTargets const& targets, float buffer)
    {
        if (mobs.empty())
            return false;
        for (Unit* target : { targets.victim, targets.current })
            if (target && bot->IsWithinMeleeRange(target))
                return false;

        // Already on its way out.
        Movement::MoveSpline const* walk = bot->movespline;
        if (!walk->Finalized())
        {
            G3D::Vector3 const dest = walk->FinalDestination();
            if (Clearance(mobs, dest.x, dest.y, dest.z, buffer) >= buffer)
                return false;
        }

        float const line = std::max(0.0f, buffer * 0.5f - 1.0f);
        for (IdleMob const& mob : mobs)
        {
            if (mob.tied || std::fabs(mob.z - bot->GetPositionZ()) > AGGRO_HEIGHT)
                continue;
            if (bot->GetExactDist2d(mob.x, mob.y) - mob.aggro < line && mob.creature->IsWithinLOSInMap(bot))
                return true;
        }
        return false;
    }
}

struct RaidClear::Leash::WalkCheck::Mobs
{
    std::vector<IdleMob> list;
    BotTargets targets;
    float buffer = 0.0f;
};

RaidClear::Leash::WalkCheck::WalkCheck(PlayerbotAI* botAI, Player* bot) : _bot(bot)
{
    Config const& config = GetConfig();
    if (!config.enable || !config.aggroLeash || !bot->GetMap()->IsRaid() ||
        !botAI->HasStrategy(LEASH_STRATEGY, BOT_STATE_COMBAT))
        return;

    _mobs = std::make_unique<Mobs>();
    _mobs->buffer = config.aggroLeashBuffer;
    _mobs->targets = TargetsOf(botAI, bot);
    _mobs->list = IdleMobsNear(bot, *StateOf(bot->GetMap()), _mobs->targets, _mobs->buffer);
}

RaidClear::Leash::WalkCheck::~WalkCheck() = default;

bool RaidClear::Leash::WalkCheck::Pulls(float x, float y, float z) const
{
    if (!_mobs || _mobs->list.empty())
        return false;
    G3D::Vector3 const from(_bot->GetPositionX(), _bot->GetPositionY(), _bot->GetPositionZ());
    return StretchPulls(_bot, _mobs->list, Approaches(_bot, _mobs->targets, x, y), _mobs->buffer, from,
                        G3D::Vector3(x, y, z));
}

bool RaidClear::Leash::WalkCheck::Pulls(std::vector<G3D::Vector3> const& path) const
{
    if (!_mobs || _mobs->list.empty() || path.empty())
        return false;
    bool const approach = Approaches(_bot, _mobs->targets, path.back().x, path.back().y);
    G3D::Vector3 from(_bot->GetPositionX(), _bot->GetPositionY(), _bot->GetPositionZ());
    for (G3D::Vector3 const& to : path)
    {
        if (StretchPulls(_bot, _mobs->list, approach, _mobs->buffer, from, to))
            return true;
        from = to;
    }
    return false;
}

bool RcAggroLeashTrigger::IsActive()
{
    // A bot that is feared or rooted isn't choosing where it stands.
    if (!GetConfig().aggroLeash || !bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
    {
        _lastTooClose = false;
        _lastResult = false;
        return false;
    }

    uint32 const now = getMSTime();
    if (_lastCheckMs && getMSTimeDiff(_lastCheckMs, now) < STEP_BACK_CHECK_MS)
        return _lastResult;
    _lastCheckMs = now;

    // Two looks in a row: between one target dying and the next being picked a bot has no target
    // for an update, and the pack its old target stood next to would count for that long.
    float const buffer = GetConfig().aggroLeashBuffer;
    BotTargets const targets = TargetsOf(botAI, bot);
    std::shared_ptr<InstanceState> state = StateOf(bot->GetMap());
    bool const tooClose = StandsTooClose(bot, IdleMobsNear(bot, *state, targets, buffer), targets, buffer);
    _lastResult = tooClose && _lastTooClose;
    _lastTooClose = tooClose;
    return _lastResult;
}

bool RcAggroLeashAction::Execute(Event /*event*/)
{
    uint32 const now = getMSTime();
    if (_noSpotAtMs && getMSTimeDiff(_noSpotAtMs, now) < STEP_BACK_RETRY_MS)
        return false;

    float const buffer = GetConfig().aggroLeashBuffer;
    BotTargets const targets = TargetsOf(botAI, bot);
    std::shared_ptr<InstanceState> state = StateOf(bot->GetMap());
    std::vector<IdleMob> const mobs = IdleMobsNear(bot, *state, targets, buffer);
    if (!StandsTooClose(bot, mobs, targets, buffer))
        return false;

    // The nearest spot that is clear of every idle mob by the full buffer, on level floor the bot
    // can see. Among equally near ones, the one closest to the main tank.
    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    float const botZ = bot->GetPositionZ();
    Player* mainTank = Tanks::ActingMainTank(bot->GetGroup());
    Map* map = bot->GetMap();

    constexpr int STEPS = 16;
    for (float ring = 3.0f; ring <= 21.0f; ring += 3.0f)
    {
        bool found = false;
        float bestScore = 0.0f;
        float bestX = 0.0f, bestY = 0.0f, bestZ = 0.0f;
        for (int i = 0; i < STEPS; ++i)
        {
            float const angle = float(i) * 2.0f * float(M_PI) / float(STEPS);
            float const x = botX + std::cos(angle) * ring;
            float const y = botY + std::sin(angle) * ring;
            // The cheap tests first: most spots near a pack fail on distance alone.
            if (Clearance(mobs, x, y, botZ, buffer) < buffer + 1.0f)
                continue;
            float const score = mainTank && mainTank != bot ? mainTank->GetExactDist2d(x, y) : 0.0f;
            if (found && score >= bestScore)
                continue;

            float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + SPOT_HEIGHT);
            if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > SPOT_HEIGHT || !bot->IsWithinLOS(x, y, z + 2.0f))
                continue;

            found = true;
            bestScore = score;
            bestX = x;
            bestY = y;
            bestZ = z;
        }
        if (found)
        {
            // Whatever the bot was told last would make this walk wait its turn.
            AI_VALUE(LastMovement&, "last movement").clear();
            if (!MoveTo(bot->GetMapId(), bestX, bestY, bestZ, false, false, false, false,
                        MovementPriority::MOVEMENT_FORCED))
                return false;
            // The way out was picked with the mobs in mind already: the hook doesn't judge it.
            state->lastWalk[bot->GetGUID()] = bot->movespline->GetId();
            return true;
        }
    }
    _noSpotAtMs = now;
    return false;
}

// A bot led by a player thinks ten times a second and one on its own twice a second (playerbots'
// react delay), and either way it is on the move in between. So a walk is judged here, on the
// map's own update, the first time its spline is seen: before the bot has gone a yard.
class RaidClearLeashMapScript : public AllMapScript
{
public:
    RaidClearLeashMapScript()
        : AllMapScript("RaidClearLeashMapScript", { ALLMAPHOOK_ON_MAP_UPDATE, ALLMAPHOOK_ON_DESTROY_INSTANCE }) {}

    void OnMapUpdate(Map* map, uint32 /*diff*/) override
    {
        Config const& config = GetConfig();
        if (!config.enable || !config.aggroLeash || !map->IsRaid())
            return;

        std::shared_ptr<InstanceState> state;
        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsInWorld() || player->movespline->Finalized() || !player->IsAlive() ||
                !player->IsInCombat())
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI)
                continue;  // a real player goes where it likes

            if (!state)
                state = StateOf(map);
            uint32& lastWalk = state->lastWalk[player->GetGUID()];
            uint32 const splineId = player->movespline->GetId();
            if (lastWalk == splineId)
                continue;  // looked at this walk already
            lastWalk = splineId;

            // Not a walk the bot's combat AI chose: a knockback, a jump, a charge, a fear, or a
            // bot still following its leader with the combat flag lingering.
            if (player->movespline->isFalling() || player->HasUnitState(UNIT_STATE_JUMPING | UNIT_STATE_CHARGING) ||
                !botAI->CanMove() || botAI->GetState() != BOT_STATE_COMBAT ||
                !botAI->HasStrategy(LEASH_STRATEGY, BOT_STATE_COMBAT))
                continue;

            BotTargets const targets = TargetsOf(botAI, player);
            if (!WalkPulls(player, IdleMobsNear(player, *state, targets, config.aggroLeashBuffer), targets,
                           config.aggroLeashBuffer))
                continue;

            player->StopMoving();
            // Playerbots now believes the bot is standing here for a moment: the action that sent
            // it would otherwise send it again on its next update, ten times a second.
            botAI->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get().Set(
                player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
                player->GetOrientation(), HOLD_MS, MovementPriority::MOVEMENT_FORCED);
        }
    }

    void OnDestroyInstance(MapInstanced* /*mapInstanced*/, Map* map) override
    {
        std::lock_guard<std::mutex> lock(sStatesMutex);
        sStates.erase(map->GetInstanceId());
    }
};

void RaidClear::Leash::AddScripts()
{
    new RaidClearLeashMapScript();
}
