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
#include "AttackAction.h"
#include "Creature.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "ReachTargetActions.h"
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

void RaidClearLeashStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcAggroLeashMultiplier(botAI));
}

namespace
{
    // Mobs further away than this can't matter to where the bot is or is going.
    constexpr float SCAN_RANGE = 75.0f;
    // A mob more than this above or below doesn't aggro (the core allows 3 yd).
    constexpr float AGGRO_HEIGHT = 6.0f;
    // A walk is sampled this often along the way.
    constexpr float PATH_STEP = 2.0f;
    // A walk toward the bot's target only has to stay this far out of other mobs' aggro radius.
    constexpr float APPROACH_BUFFER = 1.0f;
    // How long the list of an instance's idle mobs is kept.
    constexpr uint32 IDLE_SCAN_MS = 250;
    // After a walk is stopped, the bot only walks toward a target for this long.
    constexpr uint32 LEASH_MS = 2000;
    // A spot more than this above or below the bot is a ledge or a pit.
    constexpr float SPOT_HEIGHT = 5.0f;
    constexpr uint32 STEP_BACK_CHECK_MS = 300;

    struct BotLeash
    {
        uint32 splineId = 0;     // the last walk that was looked at
        bool leashed = false;
        uint32 leashedAtMs = 0;
    };

    // Per instance. Only ever touched from that instance's map update (the hook below and the
    // bots' own AI, which runs inside Player::Update), so it needs no lock of its own.
    struct InstanceState
    {
        bool scanned = false;
        uint32 scanMs = 0;
        GuidVector idle;  // creatures that would aggro, whoever walks up
        std::unordered_map<ObjectGuid, BotLeash> bots;
    };

    std::mutex sStatesMutex;
    std::unordered_map<uint32, std::shared_ptr<InstanceState>> sStates;

    std::shared_ptr<InstanceState> StateOf(Map* map, bool create = true)
    {
        std::lock_guard<std::mutex> lock(sStatesMutex);
        if (!create)
        {
            auto it = sStates.find(map->GetInstanceId());
            return it != sStates.end() ? it->second : nullptr;
        }
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
    };

    // What the bot is attacking or about to attack.
    Unit* TargetOf(PlayerbotAI* botAI, Player* bot)
    {
        Unit* target = bot->GetVictim();
        if (!target)
            target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        return target && target->IsAlive() ? target : nullptr;
    }

    // The idle mobs this bot has to keep away from. Left out: the bot's target (a pull) and any
    // mob whose aggro radius plus the buffer already covers the target, because there is no
    // getting at the target without it.
    std::vector<IdleMob> IdleMobsNear(Player* bot, InstanceState& state, Unit* target, float buffer)
    {
        Map* map = bot->GetMap();
        RefreshIdle(map, state);

        std::vector<IdleMob> mobs;
        for (ObjectGuid const& guid : state.idle)
        {
            Creature* creature = map->GetCreature(guid);
            if (!creature || creature == target || !WouldAggro(creature))
                continue;
            if (bot->GetExactDist2d(creature) > SCAN_RANGE || !creature->IsHostileTo(bot))
                continue;

            float const aggro = creature->GetAggroRange(bot);
            if (target && target->GetExactDist2d(creature) < aggro + buffer)
                continue;
            mobs.push_back({ creature, creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(),
                             aggro });
        }
        return mobs;
    }

    // Yards between a spot and the edge of the nearest mob's aggro radius; negative inside it.
    // Mobs on another level don't count.
    float Clearance(std::vector<IdleMob> const& mobs, float x, float y, float z)
    {
        float clearance = 1000.0f;
        for (IdleMob const& mob : mobs)
            if (std::fabs(mob.z - z) <= AGGRO_HEIGHT)
                clearance = std::min(clearance, std::hypot(x - mob.x, y - mob.y) - mob.aggro);
        return clearance;
    }

    // Whether the walk the bot is on passes too close to one of the mobs.
    bool WalkPulls(Player* bot, std::vector<IdleMob> const& mobs, Unit* target, float buffer)
    {
        Movement::MoveSpline const* walk = bot->movespline;
        if (mobs.empty() || walk->Finalized())
            return false;

        float const botX = bot->GetPositionX();
        float const botY = bot->GetPositionY();
        G3D::Vector3 const dest = walk->FinalDestination();

        // Going for its target: round the corner of another pack is fine, through it is not.
        bool const approach =
            target && target->GetExactDist2d(dest.x, dest.y) < target->GetExactDist2d(botX, botY) - 1.0f;
        float const margin = approach ? std::min(buffer, APPROACH_BUFFER) : buffer;

        // From where the bot is, along what is left of the path.
        G3D::Vector3 from(botX, botY, bot->GetPositionZ());
        Movement::MoveSpline::MySpline const& spline = walk->_Spline();
        for (int32 i = walk->_currentSplineIdx() + 1; i <= spline.last(); ++i)
        {
            G3D::Vector3 const& to = spline.getPoint(i);
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
                    if (std::fabs(mob.z - z) > AGGRO_HEIGHT)
                        continue;
                    float const there = std::hypot(x - mob.x, y - mob.y);
                    // Only a walk that takes the bot closer to the mob than it already is.
                    if (there < mob.aggro + margin && there < std::hypot(botX - mob.x, botY - mob.y) - 0.5f &&
                        mob.creature->IsWithinLOS(x, y, z + 2.0f))
                        return true;
                }
            }
            from = to;
        }
        return false;
    }

    // Whether the bot is standing at the edge of a mob's aggro radius: half the buffer, so a bot
    // stopped at the full buffer isn't sent back and forth.
    bool StandsTooClose(Player* bot, std::vector<IdleMob> const& mobs, Unit* target, float buffer)
    {
        if (mobs.empty() || (target && bot->IsWithinMeleeRange(target)))
            return false;

        // Already on its way out.
        Movement::MoveSpline const* walk = bot->movespline;
        if (!walk->Finalized())
        {
            G3D::Vector3 const dest = walk->FinalDestination();
            if (Clearance(mobs, dest.x, dest.y, dest.z) >= buffer)
                return false;
        }

        for (IdleMob const& mob : mobs)
        {
            if (std::fabs(mob.z - bot->GetPositionZ()) > AGGRO_HEIGHT)
                continue;
            if (bot->GetExactDist2d(mob.x, mob.y) - mob.aggro < buffer * 0.5f && mob.creature->IsWithinLOSInMap(bot))
                return true;
        }
        return false;
    }

    bool IsLeashed(Player* bot)
    {
        std::shared_ptr<InstanceState> state = StateOf(bot->GetMap(), false);
        if (!state)
            return false;
        auto it = state->bots.find(bot->GetGUID());
        return it != state->bots.end() && it->second.leashed &&
               getMSTimeDiff(it->second.leashedAtMs, getMSTime()) < LEASH_MS;
    }
}

bool RcAggroLeashTrigger::IsActive()
{
    // A bot that is feared or rooted isn't choosing where it stands.
    if (!GetConfig().aggroLeash || !bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;

    uint32 const now = getMSTime();
    if (_lastCheckMs && getMSTimeDiff(_lastCheckMs, now) < STEP_BACK_CHECK_MS)
        return _lastResult;
    _lastCheckMs = now;

    float const buffer = GetConfig().aggroLeashBuffer;
    Unit* target = TargetOf(botAI, bot);
    std::shared_ptr<InstanceState> state = StateOf(bot->GetMap());
    _lastResult = StandsTooClose(bot, IdleMobsNear(bot, *state, target, buffer), target, buffer);
    return _lastResult;
}

bool RcAggroLeashAction::Execute(Event /*event*/)
{
    float const buffer = GetConfig().aggroLeashBuffer;
    Unit* target = TargetOf(botAI, bot);
    std::shared_ptr<InstanceState> state = StateOf(bot->GetMap());
    std::vector<IdleMob> const mobs = IdleMobsNear(bot, *state, target, buffer);
    if (!StandsTooClose(bot, mobs, target, buffer))
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
            float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + SPOT_HEIGHT);
            if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > SPOT_HEIGHT)
                continue;
            if (Clearance(mobs, x, y, z) < buffer + 1.0f)
                continue;

            float const score = mainTank && mainTank != bot ? mainTank->GetExactDist2d(x, y) : 0.0f;
            if (found && score >= bestScore)
                continue;
            if (!bot->IsWithinLOS(x, y, z + 2.0f))
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
            return MoveTo(bot->GetMapId(), bestX, bestY, bestZ, false, false, false, false,
                          MovementPriority::MOVEMENT_FORCED);
        }
    }
    return false;
}

float RcAggroLeashMultiplier::GetValue(Action* action)
{
    // Picking a target isn't a walk, and a walk toward a target is always allowed (the map hook
    // still stops it if it goes through a pack).
    if (!action || !dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action) ||
        dynamic_cast<ReachTargetAction*>(action) || dynamic_cast<RcAggroLeashAction*>(action))
        return 1.0f;
    return IsLeashed(bot) ? 0.0f : 1.0f;
}

// A bot in combat thinks twice a second (playerbots' react delay), and a bot running at 7 yd a
// second is 3-4 yd further on by then. So a walk is judged here, on the map's own update, the
// first time it is seen: before the bot has gone a yard.
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
            BotLeash& leash = state->bots[player->GetGUID()];
            uint32 const splineId = player->movespline->GetId();
            if (leash.splineId == splineId)
                continue;  // looked at this walk already
            leash.splineId = splineId;

            // Not a walk the bot chose: a knockback, a jump, a charge, a fear.
            if (player->movespline->isFalling() || player->HasUnitState(UNIT_STATE_JUMPING | UNIT_STATE_CHARGING) ||
                !botAI->CanMove() || !botAI->HasStrategy(LEASH_STRATEGY, BOT_STATE_COMBAT))
                continue;

            Unit* target = TargetOf(botAI, player);
            if (!WalkPulls(player, IdleMobsNear(player, *state, target, config.aggroLeashBuffer), target,
                           config.aggroLeashBuffer))
                continue;

            player->StopMoving();
            // Playerbots would otherwise wait for the walk it thinks is still going on.
            botAI->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get().clear();
            leash.leashed = true;
            leash.leashedAtMs = getMSTime();
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
