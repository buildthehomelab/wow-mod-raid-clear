/*
 * mod-raid-clear: Onyxia's Lair (map 249). See OnyRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "OnyRaidClear.h"

#include "RaidClearConfig.h"
#include "Common/BossLookup.h"
#include "Common/SpellReflect.h"
#include "Common/TankRoles.h"

#include "AllSpellScript.h"
#include "AttackAction.h"
#include "Creature.h"
#include "GameTime.h"
#include "LastMovementValue.h"
#include "Group.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "ThreatManager.h"
#include "Timer.h"
#include "PlayerbotAI.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

using namespace RaidClear::OnyxiasLair;

void RaidClearOnyxiasLairStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Under a reflect: the spell of the other school that playerbots ranks below the wand (a
    // frost mage's Fireball), just above it. "rc spell reflect" zeroes whichever is reflected.
    triggers.push_back(new TriggerNode("rc mage target reflects", { NextAction("fireball", 5.15f) }));
    triggers.push_back(new TriggerNode("rc warlock target reflects", { NextAction("immolate", 5.15f) }));

    // Above playerbots' own "ony move to safe zone" (ACTION_EMERGENCY + 5).
    triggers.push_back(new TriggerNode("rc ony deep breath",
                                       { NextAction("rc ony leave deep breath", ACTION_EMERGENCY + 6) }));

    // The same priority playerbots gives the melee's walk to her side.
    triggers.push_back(new TriggerNode("rc ony offtank position",
                                       { NextAction("rc ony offtank position", ACTION_RAID + 2) }));
    triggers.push_back(new TriggerNode("rc ony cleave", { NextAction("rc ony leave cleave", ACTION_RAID + 2) }));

    // Above both, so a bot is off an evading mob before it does anything else to it.
    triggers.push_back(new TriggerNode("rc ony evading target",
                                       { NextAction("rc ony leave evading target", ACTION_RAID + 3) }));

    // Playerbots' own trigger and priority; its action is switched off by the multiplier.
    triggers.push_back(new TriggerNode("ony whelps spawn",
                                       { NextAction("rc ony kill whelps", ACTION_RAID + 1) }));

    // Under the urgent heals, over everything a bot does when nobody is about to die.
    triggers.push_back(new TriggerNode("rc ony spread", { NextAction("rc ony spread out", ACTION_MOVE - 3.0f) }));
}

void RaidClearOnyxiasLairStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcOnyDeepBreathMultiplier(botAI));
    multipliers.push_back(new RcOnyTargetMultiplier(botAI));
    multipliers.push_back(new RcOnyTankThreatMultiplier(botAI));
    multipliers.push_back(new RcSpellReflectMultiplier(botAI));
}

// --- Deep Breath ----------------------------------------------------------

namespace
{
    // One spell of a chain: everyone within `radius` of this spot on the floor burns.
    struct Blast
    {
        float x;
        float y;
        float radius;
    };
    using Trail = std::vector<Blast>;

    struct BreathData
    {
        std::unordered_map<uint32, Trail> trails;    // first spell of a chain -> where the chain burns
        std::unordered_map<uint32, uint32> firstOf;  // any spell of a chain -> its first spell
        Trail rookery;                               // the Heated Ground patches
    };

    // The longest chain has 20 spells.
    constexpr uint8 MAX_CHAIN = 40;
    constexpr float DEFAULT_BLAST_RADIUS = 18.0f;

    // A chain, walked from the server's spell data: each spell lands around its row in
    // spell_target_position and its second effect casts the next one.
    Trail WalkChain(uint32 first, std::unordered_map<uint32, uint32>* firstOf = nullptr)
    {
        Trail trail;
        uint32 id = first;
        for (uint8 link = 0; id && link < MAX_CHAIN; ++link)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
            if (!info)
                break;
            if (firstOf)
                firstOf->emplace(id, first);

            SpellTargetPosition const* spot = sSpellMgr->GetSpellTargetPosition(id, EFFECT_0);
            if (spot && spot->target_mapId == MAP_ID)
            {
                float const radius = info->Effects[EFFECT_0].CalcRadius();
                trail.push_back({ spot->target_X, spot->target_Y, radius > 0.0f ? radius : DEFAULT_BLAST_RADIUS });
            }
            id = info->Effects[EFFECT_1].TriggerSpell;
        }
        return trail;
    }

    BreathData BuildBreathData()
    {
        BreathData data;
        for (uint32 const first : DEEP_BREATHS)
            data.trails[first] = WalkChain(first, &data.firstOf);
        data.rookery = WalkChain(SPELL_HEATED_GROUND);
        return data;
    }

    BreathData const& Breaths()
    {
        static BreathData const data = BuildBreathData();
        return data;
    }

    // Onyxia, alive and fighting. Not through playerbots' "find target": that only knows what the
    // bot itself has threat on, and she drops her threat list when she takes off.
    Creature* FightingOnyxia(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID)
            return nullptr;
        return RaidClear::FightingCreature(bot, { NPC_ONYXIA, NPC_ONYXIA_40 });
    }

    // The first spell of the Deep Breath she is casting, or whose fire is still crossing the
    // room (one of the chain's auras is on her), or 0.
    uint32 ActiveBreath(Unit* boss)
    {
        BreathData const& data = Breaths();
        if (Spell* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            if (SpellInfo const* info = spell->GetSpellInfo())
                if (data.trails.count(info->Id))
                    return info->Id;

        for (auto const& [id, aurApp] : boss->GetAppliedAuras())
        {
            auto it = data.firstOf.find(id);
            if (it != data.firstOf.end())
                return it->second;
        }
        return 0;
    }

    // Where a breath burns, or nullptr when the server has no positions for it.
    Trail const* TrailOf(uint32 breath)
    {
        BreathData const& data = Breaths();
        auto it = data.trails.find(breath);
        return it != data.trails.end() && !it->second.empty() ? &it->second : nullptr;
    }

    // The strip of the breath going on right now, or nullptr.
    Trail const* ActiveTrail(Player* bot, uint32* breath = nullptr)
    {
        Creature* boss = FightingOnyxia(bot);
        uint32 const id = boss ? ActiveBreath(boss) : 0;
        if (breath)
            *breath = id;
        return id ? TrailOf(id) : nullptr;
    }

    // Yards from a spot to the edge of the nearest blast; negative inside the fire. Flat
    // distance: the blasts sit on the floor and so does everyone else.
    float Clearance(Trail const& trail, float x, float y)
    {
        float clearance = 1000.0f;
        for (Blast const& blast : trail)
            clearance = std::min(clearance, std::hypot(x - blast.x, y - blast.y) - blast.radius);
        return clearance;
    }

    // Whether the straight walk from one spot to another comes within `margin` of the fire.
    bool CrossesFire(Trail const& trail, float fromX, float fromY, float toX, float toY, float margin)
    {
        float const length = std::hypot(toX - fromX, toY - fromY);
        int const steps = std::max(1, int(std::ceil(length / 3.0f)));
        for (int i = 0; i <= steps; ++i)
        {
            float const t = float(i) / float(steps);
            if (Clearance(trail, fromX + (toX - fromX) * t, fromY + (toY - fromY) * t) < margin)
                return true;
        }
        return false;
    }

    // The whelp caves and the egg trench between them: no place to wait out a breath. The core's
    // Onyxia sets the trench on fire with every Deep Breath (Heated Ground, a chain like the
    // breath's), so its patches mark the trench on any server.
    bool NearEggs(float x, float y)
    {
        static Position const caves[] = { { -35.0f, -165.0f, 0.0f }, { -35.0f, -260.0f, 0.0f } };
        for (Position const& cave : caves)
            if (cave.GetExactDist2d(x, y) < EGGS_KEEP_OUT)
                return true;

        Trail const& rookery = Breaths().rookery;
        return !rookery.empty() && Clearance(rookery, x, y) < ROOKERY_KEEP_OUT;
    }

    // The nearest spot outside the fire: rings around the bot, closest first. On the first ring
    // with anything usable, the spot deepest outside wins, which is the one straight out of the
    // strip. Level floor the bot can see, not at the whelp caves or in the egg trench.
    bool FindSafeSpot(Player* bot, Trail const& trail, Position& out)
    {
        Map* map = bot->GetMap();
        float const botX = bot->GetPositionX();
        float const botY = bot->GetPositionY();
        float const botZ = bot->GetPositionZ();

        constexpr float RING_STEP = 2.0f;
        constexpr float RING_MAX = 46.0f;  // from the middle of a strip the edge is 25 yd away
        constexpr int STEPS = 24;
        for (float ring = RING_STEP; ring <= RING_MAX; ring += RING_STEP)
        {
            bool found = false;
            float bestClearance = 0.0f;
            for (int i = 0; i < STEPS; ++i)
            {
                float const angle = float(i) * 2.0f * float(M_PI) / float(STEPS);
                float const x = botX + std::cos(angle) * ring;
                float const y = botY + std::sin(angle) * ring;
                float const clearance = Clearance(trail, x, y);
                if (clearance < BREATH_KEEP_OUT_TARGET || (found && clearance <= bestClearance) || NearEggs(x, y))
                    continue;

                float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + BREATH_SPOT_HEIGHT);
                if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > BREATH_SPOT_HEIGHT)
                    continue;
                if (!bot->IsWithinLOS(x, y, z + 2.0f))
                    continue;

                out.Relocate(x, y, z);
                bestClearance = clearance;
                found = true;
            }
            if (found)
                return true;
        }
        return false;
    }
}

bool RcOnyDeepBreathTrigger::IsActive()
{
    if (!bot->IsAlive())
        return false;
    Trail const* trail = ActiveTrail(bot);
    return trail && Clearance(*trail, bot->GetPositionX(), bot->GetPositionY()) < BREATH_KEEP_OUT;
}

bool RcOnyLeaveDeepBreathAction::Execute(Event /*event*/)
{
    uint32 breath = 0;
    Trail const* trail = ActiveTrail(bot, &breath);
    if (!trail || Clearance(*trail, bot->GetPositionX(), bot->GetPositionY()) >= BREATH_KEEP_OUT)
        return false;

    // One spot per breath: a new pick every tick would have the bot zigzag along the strip.
    constexpr uint64 SPOT_REUSE_MS = 20000;
    uint64 const now = GameTime::GetGameTimeMS().count();
    bool const cached = _spotBreath == breath && now - _spotFoundMs < SPOT_REUSE_MS &&
                        Clearance(*trail, _spot.GetPositionX(), _spot.GetPositionY()) >= BREATH_KEEP_OUT;
    if (!cached)
    {
        if (!FindSafeSpot(bot, *trail, _spot))
            return false;  // nowhere to go from here: fight on
        _spotBreath = breath;
        _spotFoundMs = now;
    }

    // Already running there: nothing else this tick, so no cast stops the run.
    LastMovement& last = AI_VALUE(LastMovement&, "last movement");
    if (bot->isMoving() && last.lastMoveShort.GetExactDist2d(_spot.GetPositionX(), _spot.GetPositionY()) < 1.0f)
        return true;

    // Whatever the bot was told last (a walk somewhere else, or to stand still for a cast) would
    // make this one wait its turn.
    last.clear();
    bot->CastStop();
    return MoveTo(bot->GetMapId(), _spot.GetPositionX(), _spot.GetPositionY(), _spot.GetPositionZ(), false, false,
                  false, false, MovementPriority::MOVEMENT_FORCED);
}

float RcOnyDeepBreathMultiplier::GetValue(Action* action)
{
    if (dynamic_cast<RcOnyLeaveDeepBreathAction*>(action))
        return 1.0f;

    // Nothing to work from (no breath going on, or a server without positions for it): playerbots
    // is on its own.
    Trail const* trail = ActiveTrail(bot);
    if (!trail)
        return 1.0f;

    // Playerbots' two fixed spots per breath spell: in the fire for two of the eight since the
    // strips moved.
    if (action->getName() == "ony move to safe zone")
        return 0.0f;

    // Picking a target isn't movement; only walking somewhere is held.
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action))
        return 1.0f;

    // In the fire: the way out is the only walk.
    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    if (Clearance(*trail, botX, botY) < BREATH_KEEP_OUT)
        return 0.0f;

    // Out of it: stepping out of something else on the floor is fine, and so is a walk toward
    // something on this side (a whelp, someone to heal). Anything else waits until the fire has
    // passed.
    if (action->getName() == "avoid aoe")
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 0.0f;
    return CrossesFire(*trail, botX, botY, target->GetPositionX(), target->GetPositionY(), BREATH_KEEP_OUT) ? 0.0f
                                                                                                             : 1.0f;
}

// --- Fireball -------------------------------------------------------------

namespace
{
    constexpr uint32 SPREAD_CHECK_MS = 500;
    // A bot that was sent to a spot isn't sent again sooner than this, one that found no spot
    // doesn't look again sooner than that, and a walk to a spot is given this long.
    constexpr uint32 SPREAD_RESEND_MS = 1500;
    constexpr uint32 SPREAD_RETRY_MS = 3000;
    constexpr uint32 SPREAD_WALK_MS = 5000;

    // How many living raid members other than the bot are, or are about to be, within `range` of
    // a spot. Someone on the move counts where it is going: two bots leaving the same crowd would
    // otherwise pick the same empty spot.
    int OthersWithin(Player* bot, float x, float y, float range)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return 0;

        int count = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || !member->IsInMap(bot))
                continue;
            float memberX = member->GetPositionX();
            float memberY = member->GetPositionY();
            if (member->isMoving() && !member->movespline->Finalized())
            {
                G3D::Vector3 const dest = member->movespline->FinalDestination();
                memberX = dest.x;
                memberY = dest.y;
            }
            if (std::hypot(memberX - x, memberY - y) < range)
                ++count;
        }
        return count;
    }

    // Onyxia in the air with no Deep Breath on: the time to stand apart.
    bool FireballTime(Player* bot)
    {
        Creature* boss = FightingOnyxia(bot);
        return boss && boss->IsFlying() && !ActiveBreath(boss);
    }
}

bool RcOnySpreadTrigger::IsActive()
{
    // Tanks and melee are on the whelps, which come to them in one pile.
    if (!bot->IsAlive() || !bot->IsInCombat() || PlayerbotAI::IsTank(bot) ||
        !(PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot)) || !botAI->CanMove())
    {
        _lastResult = false;
        return false;
    }

    uint32 const now = getMSTime();
    if (_lastCheckMs && getMSTimeDiff(_lastCheckMs, now) < SPREAD_CHECK_MS)
        return _lastResult;
    _lastCheckMs = now ? now : 1;

    _lastResult = FireballTime(bot) &&
                  OthersWithin(bot, bot->GetPositionX(), bot->GetPositionY(), FIREBALL_SPREAD) >= SPREAD_CROWD;
    return _lastResult;
}

bool RcOnySpreadOutAction::Execute(Event /*event*/)
{
    if (!FireballTime(bot))
        return false;

    uint32 const now = getMSTime();
    if (_sentAtMs)
    {
        // Still on its way there: nothing else this tick, so no cast stops the walk.
        uint32 const since = getMSTimeDiff(_sentAtMs, now);
        if (since < SPREAD_WALK_MS && bot->isMoving() && !bot->movespline->Finalized())
        {
            G3D::Vector3 const dest = bot->movespline->FinalDestination();
            if (_spot.GetExactDist2d(dest.x, dest.y) < 1.0f)
                return true;
        }
        if (since < SPREAD_RESEND_MS)
            return false;
    }
    if (_noSpotAtMs && getMSTimeDiff(_noSpotAtMs, now) < SPREAD_RETRY_MS)
        return false;

    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    float const botZ = bot->GetPositionZ();
    if (OthersWithin(bot, botX, botY, FIREBALL_SPREAD) < SPREAD_CROWD)
        return false;
    // Told to wait for an earlier walk: no spot would do. Look again in a moment.
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_COMBAT))
    {
        _sentAtMs = now ? now : 1;
        return false;
    }

    // The nearest spot with nobody in reach of a Fireball thrown at it: rings around the bot,
    // closest first. Each bot starts its way round a ring somewhere else, so neighbours leaving
    // the same pile don't all look the same way first.
    Map* map = bot->GetMap();
    constexpr float RING_STEP = 4.0f;
    constexpr int STEPS = 12;
    int const first = int(bot->GetGUID().GetCounter() % STEPS);
    int refused = 0;
    for (float ring = RING_STEP; ring <= SPREAD_SEARCH; ring += RING_STEP)
    {
        for (int i = 0; i < STEPS; ++i)
        {
            float const angle = float((first + i) % STEPS) * 2.0f * float(M_PI) / float(STEPS);
            float const x = botX + std::cos(angle) * ring;
            float const y = botY + std::sin(angle) * ring;
            if (NearEggs(x, y) || OthersWithin(bot, x, y, FIREBALL_SPREAD) > 0)
                continue;

            float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + BREATH_SPOT_HEIGHT);
            if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > BREATH_SPOT_HEIGHT)
                continue;
            if (!bot->IsWithinLOS(x, y, z + 2.0f))
                continue;

            // No way to this one (off the floor the bot can walk on): the next may do. Not all 72
            // of them, each with its own look round the raid.
            if (!MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
            {
                if (++refused >= 3)
                {
                    _noSpotAtMs = now ? now : 1;
                    return false;
                }
                continue;
            }
            _spot.Relocate(x, y, z);
            _sentAtMs = now ? now : 1;
            _noSpotAtMs = 0;
            return true;
        }
    }
    _noSpotAtMs = now ? now : 1;
    return false;
}

// --- Threat ---------------------------------------------------------------

float RcOnyTankThreatMultiplier::GetValue(Action* action)
{
    // Melee swings and pets carry on; spells and special attacks are what's held.
    if (!action || action->getThreatType() == Action::ActionThreatType::None)
        return 1.0f;
    if (!bot->IsInCombat() || PlayerbotAI::IsTank(bot))
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || (target->GetEntry() != NPC_ONYXIA && target->GetEntry() != NPC_ONYXIA_40))
        return 1.0f;
    // Where a taunt lands, playerbots' tanks take her back with one.
    Creature* boss = FightingOnyxia(bot);
    if (boss != target || !RaidClear::Tanks::CantBeTaunted(boss) || !RaidClear::Tanks::HeldByThreatCap(botAI, action))
        return 1.0f;
    // In the air nobody tanks her and what she is hit with is forgotten when she comes down: she
    // wipes her threat as the descent starts, two seconds before she lands and turns on whoever
    // has the most. So from 40% down (she only goes below that on her way to land, or landed) the
    // limit is on in the air as well. Above it, in the air or walking off to take wing, it isn't.
    if (boss->GetHealthPct() > LANDING_HEALTH_PCT && (boss->IsFlying() || boss->HasReactState(REACT_PASSIVE)))
        return 1.0f;

    // No tank at her to wait for: the fight goes on without one. While she is coming down any
    // living tank counts, wherever it stands: they are on the whelps, and she lands in two seconds.
    float const tank =
        RaidClear::Tanks::BestTankThreat(bot, boss, boss->IsFlying() ? LANDING_TANK_RANGE : TANK_THREAT_RANGE);
    if (tank < 0.0f)
        return 1.0f;
    return boss->GetThreatMgr().GetThreat(bot) >= tank * TANK_THREAT_SHARE ? 0.0f : 1.0f;
}

// --- Off-tanks ------------------------------------------------------------

namespace
{
    // How far round from her nose the bot stands: 0 dead ahead, pi at the tip of her tail.
    float AngleOffNose(Unit const* boss, Player const* bot)
    {
        float const diff = std::fabs(boss->GetAngle(bot) - boss->GetOrientation());
        return diff > float(M_PI) ? 2.0f * float(M_PI) - diff : diff;
    }

    bool AtHeadOrTail(Unit const* boss, Player const* bot)
    {
        if (bot->GetDistance(boss) > SIDE_RANGE)
            return false;
        float const angle = AngleOffNose(boss, bot);
        return angle < SIDE_CONE || angle > float(M_PI) - SIDE_CONE;
    }

    // Cleave: whoever she is on, and up to nine more within 10 yd of that one anywhere in the half
    // of the room she is facing. Whether a spot is one of those.
    bool InCleaveAt(Unit const* boss, Unit const* victim, float x, float y)
    {
        float diff = std::fabs(boss->GetAngle(x, y) - boss->GetOrientation());
        if (diff > float(M_PI))
            diff = 2.0f * float(M_PI) - diff;
        return diff < CLEAVE_ARC && victim->GetExactDist2d(x, y) < CLEAVE_KEEP_AWAY;
    }

    // She is on the ground with someone to cleave, and this bot isn't that one but stands in it.
    Unit* CleavedWith(Player* bot)
    {
        Creature* boss = FightingOnyxia(bot);
        if (!boss || boss->IsFlying())
            return nullptr;
        Unit* victim = boss->GetVictim();
        if (!victim || victim == bot || !InCleaveAt(boss, victim, bot->GetPositionX(), bot->GetPositionY()))
            return nullptr;
        return victim;
    }

    // The main tank an off-tank Onyxia has turned to stands with, or nullptr. Only a main tank
    // who is at her: one busy with whelps, far off or running in fear would have the off-tank drag
    // her through the raid.
    Player* MainTankAtBoss(Player* bot, Unit const* boss)
    {
        Player* mainTank = RaidClear::Tanks::ActingMainTank(bot->GetGroup());
        if (!RaidClear::Tanks::IsOffTank(bot, mainTank) || !mainTank->IsWithinMeleeRange(boss) ||
            mainTank->HasUnitState(UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED))
            return nullptr;
        return mainTank;
    }

    // Whether that off-tank still has to walk over: not there yet, and the main tank's spot is one
    // it can see, within reach and away from the whelp caves.
    bool ShouldJoin(Player* bot, Player* mainTank)
    {
        float const dist = bot->GetDistance(mainTank);
        return dist > JOIN_NEAR && dist <= JOIN_RANGE && bot->IsWithinLOSInMap(mainTank) &&
               !NearEggs(mainTank->GetPositionX(), mainTank->GetPositionY());
    }
}

bool RcOnyOffTankPositionTrigger::IsActive()
{
    if (!bot->IsAlive() || !PlayerbotAI::IsTank(bot))
        return false;

    Creature* boss = FightingOnyxia(bot);
    if (!boss || boss->IsFlying())
        return false;
    Unit* victim = boss->GetVictim();
    if (!victim)
        return false;

    // The main tank stays in front: it is the one who takes her back when she turns away.
    Player* mainTank = RaidClear::Tanks::ActingMainTank(bot->GetGroup());
    if (!RaidClear::Tanks::IsOffTank(bot, mainTank))
        return false;

    // She is on me: at her side that turns her head to the bots standing there.
    if (victim == bot)
    {
        Player* join = MainTankAtBoss(bot, boss);
        return join && ShouldJoin(bot, join);
    }

    // Holding a whelp is done where the whelp is, in front of her or not.
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current && current != boss)
        return false;

    // Her head and tail, and the rest of her Cleave: next to whoever she is on.
    return AtHeadOrTail(boss, bot) || InCleaveAt(boss, victim, bot->GetPositionX(), bot->GetPositionY());
}

bool RcOnyOffTankPositionAction::Execute(Event /*event*/)
{
    Creature* boss = FightingOnyxia(bot);
    if (!boss)
        return false;

    if (boss->GetVictim() == bot)
    {
        Player* join = MainTankAtBoss(bot, boss);
        return join && ShouldJoin(bot, join) &&
               MoveTo(bot->GetMapId(), join->GetPositionX(), join->GetPositionY(), join->GetPositionZ(), false, false,
                      false, false, MovementPriority::MOVEMENT_COMBAT);
    }

    // Her two sides, the nearer one first. Level floor the bot can see, not at the whelp caves.
    Map* map = bot->GetMap();
    float const botZ = bot->GetPositionZ();
    float const facing = boss->GetOrientation();
    Position sides[2];
    for (int i = 0; i < 2; ++i)
    {
        float const angle = facing + (i == 0 ? float(M_PI_2) : -float(M_PI_2));
        sides[i].Relocate(boss->GetPositionX() + SIDE_DISTANCE * std::cos(angle),
                          boss->GetPositionY() + SIDE_DISTANCE * std::sin(angle), botZ);
    }
    if (bot->GetExactDist2d(&sides[1]) < bot->GetExactDist2d(&sides[0]))
        std::swap(sides[0], sides[1]);

    for (Position const& side : sides)
    {
        float const x = side.GetPositionX();
        float const y = side.GetPositionY();
        if (NearEggs(x, y))
            continue;

        float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + BREATH_SPOT_HEIGHT);
        if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > BREATH_SPOT_HEIGHT)
            continue;
        if (!bot->IsWithinLOS(x, y, z + 2.0f))
            continue;

        return MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT);
    }
    return false;
}

// --- Cleave ---------------------------------------------------------------

bool RcOnyCleaveTrigger::IsActive()
{
    // Tanks have their own rule above, and playerbots walks the melee to her side.
    if (!bot->IsAlive() || !bot->IsInCombat() || PlayerbotAI::IsTank(bot) ||
        !(PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot)) || !botAI->CanMove())
        return false;
    return CleavedWith(bot) != nullptr;
}

bool RcOnyLeaveCleaveAction::Execute(Event /*event*/)
{
    Unit* victim = CleavedWith(bot);
    if (!victim)
        return false;
    Creature* boss = FightingOnyxia(bot);
    if (!boss)
        return false;

    // Away from whoever she is on, or round to her side. Not in line with her head or her tail
    // (Flame Breath and Tail Sweep, and playerbots walks a bot out of both), and not at the whelp
    // caves.
    Position const bossAt = boss->GetPosition();
    Position const victimAt = victim->GetPosition();
    float const facing = boss->GetOrientation();
    // SIDE_RANGE is from her edge to the bot's, as playerbots measures it.
    float const coneRange = SIDE_RANGE + boss->GetCombatReach() + bot->GetCombatReach();
    auto const gapAt = [&bossAt, &victimAt, facing, coneRange](float x, float y, float /*z*/)
    {
        if (NearEggs(x, y))
            return 0.0f;
        float diff = std::fabs(bossAt.GetAngle(x, y) - facing);
        if (diff > float(M_PI))
            diff = 2.0f * float(M_PI) - diff;
        if ((diff < SIDE_CONE || diff > float(M_PI) - SIDE_CONE) && bossAt.GetExactDist2d(x, y) <= coneRange)
            return 0.0f;
        return diff >= CLEAVE_ARC ? CLEAVE_SPOT : victimAt.GetExactDist2d(x, y);
    };
    return _walk.Go(gapAt, CLEAVE_KEEP_AWAY, CLEAVE_SPOT, CLEAVE_WORTH_IT);
}

// --- Evading mobs ---------------------------------------------------------

namespace
{
    bool IsWhelp(Unit const* unit)
    {
        return unit->GetEntry() == NPC_ONYXIAN_WHELP || unit->GetEntry() == NPC_ONYXIAN_WHELP_40;
    }

    // A living mob no attack can land on: it is running home, or it can't find a way to its
    // victim (the core then lets it evade everything, though it goes on hitting whoever it can
    // reach). Onyxia herself is left out: she is what the bots fall back to.
    bool IsEvadingMob(Unit const* unit)
    {
        Creature const* creature = unit ? unit->ToCreature() : nullptr;
        return creature && creature->IsAlive() && creature->GetEntry() != NPC_ONYXIA &&
               creature->GetEntry() != NPC_ONYXIA_40 && creature->IsEvadingAttacks();
    }

    // The nearest enemy in the list the bot can hurt and see (playerbots' Attack refuses a target
    // out of sight); with `whelpsOnly`, the nearest of the whelps playerbots' action knows (the
    // core's). Never Onyxia.
    Unit* NearestHittable(PlayerbotAI* botAI, Player* bot, GuidVector const& units, bool whelpsOnly)
    {
        Unit* best = nullptr;
        float bestDist = 0.0f;
        for (ObjectGuid const& guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || !unit->IsInWorld())
                continue;
            if (whelpsOnly ? unit->GetEntry() != NPC_ONYXIAN_WHELP
                           : (unit->GetEntry() == NPC_ONYXIA || unit->GetEntry() == NPC_ONYXIA_40))
                continue;
            if (IsEvadingMob(unit) || !bot->IsValidAttackTarget(unit))
                continue;

            float const dist = bot->GetDistance(unit);
            if ((best && dist >= bestDist) || !bot->IsWithinLOSInMap(unit))
                continue;
            best = unit;
            bestDist = dist;
        }
        return best;
    }
}

// Playerbots asks every combat strategy what its target picking should skip ("dps target", "tank
// target", the debuff targets), in whichever engine the bot is running: a bot whose target has
// just died picks its next one outside combat, where this strategy's multipliers don't reach.
// "dps target" and "tank target" skip a skull on an evading mob, but "attack rti target" can
// still pick one, and the trigger below leaves it again.
void RaidClearOnyxiasLairStrategy::AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType /*type*/)
{
    GuidVector const& attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const& guid : attackers)
        if (IsEvadingMob(botAI->GetUnit(guid)))
            exclusions.insert(guid);
}

bool RcOnyEvadingTargetTrigger::IsActive()
{
    // A bot without a living target is handed one by playerbots, which skips the evading ones.
    return bot->IsAlive() && bot->IsInCombat() && IsEvadingMob(AI_VALUE(Unit*, "current target"));
}

bool RcOnyLeaveEvadingTargetAction::Execute(Event /*event*/)
{
    // What playerbots would pick now, if the bot can attack it from here.
    Unit* next = AI_VALUE(Unit*, PlayerbotAI::IsTank(bot) ? "tank target" : "dps target");
    if (next && next->IsAlive() && !IsEvadingMob(next) && Attack(next))
        return true;

    next = NearestHittable(botAI, bot, AI_VALUE(GuidVector, "attackers"), false);
    if (next && Attack(next))
        return true;

    Creature* boss = FightingOnyxia(bot);
    return boss && bot->IsValidAttackTarget(boss) && Attack(boss);
}

bool RcOnyKillWhelpsAction::Execute(Event /*event*/)
{
    // On a whelp that can be hurt: stay on it, as playerbots does.
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current && current->IsAlive() && current->GetEntry() == NPC_ONYXIAN_WHELP && !IsEvadingMob(current))
        return false;

    Unit* whelp = NearestHittable(botAI, bot, AI_VALUE(GuidVector, "possible targets"), true);
    return whelp && Attack(whelp);
}

float RcOnyTargetMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    std::string const name = action->getName();
    // Replaced by "rc ony kill whelps".
    if (name == "ony kill whelps")
        return 0.0f;

    // The wand never asks how far its target is: with Onyxia out of reach (off to her Deep
    // Breath, which nobody walks after) casters kept starting it over, a few times a second.
    if (name == "shoot")
    {
        Unit* target = action->GetTarget();
        return target && !bot->IsWithinCombatRange(target, WAND_RANGE) ? 0.0f : 1.0f;
    }

    if (name == "hunter's mark")
    {
        Unit* target = action->GetTarget();
        return target && IsWhelp(target) ? 0.0f : 1.0f;
    }

    // An off-tank she has turned to stands with the main tank (see the off-tank trigger).
    // Playerbots would have it turn her away from the raid by itself and walk off that spot again.
    if (name == "tank face")
    {
        Creature* boss = FightingOnyxia(bot);
        return boss && boss->GetVictim() == bot && MainTankAtBoss(bot, boss) ? 0.0f : 1.0f;
    }

    // Target picking that doesn't go through the exclusions (the area damage target, for one).
    if (!dynamic_cast<AttackAction*>(action) || dynamic_cast<RcOnyLeaveEvadingTargetAction*>(action) ||
        dynamic_cast<RcOnyKillWhelpsAction*>(action))
        return 1.0f;
    return IsEvadingMob(action->GetTarget()) ? 0.0f : 1.0f;
}

// A bot's AI waits for the spell it is casting, so a mage two seconds into a Frostbolt would
// only look up after it lands. The moment a Deep Breath starts, every bot standing in its strip
// is told to drop its cast and think again on its next update.
class RaidClearOnyBreathSpellScript : public AllSpellScript
{
public:
    RaidClearOnyBreathSpellScript() : AllSpellScript("RaidClearOnyBreathSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || caster->GetMapId() != MAP_ID ||
            std::find(DEEP_BREATHS.begin(), DEEP_BREATHS.end(), spellInfo->Id) == DEEP_BREATHS.end())
            return;
        if (!RaidClear::GetConfig().IsRaidEnabled(MAP_ID))
            return;
        Trail const* trail = TrailOf(spellInfo->Id);
        if (!trail)
            return;

        Map::PlayerList const& players = caster->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive())
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->HasStrategy("rc ony", BOT_STATE_COMBAT))
                continue;
            if (Clearance(*trail, player->GetPositionX(), player->GetPositionY()) >= BREATH_KEEP_OUT)
                continue;

            botAI->RequestSpellInterrupt();
            botAI->ResetActionDuration();
        }
    }
};

void RaidClear::OnyxiasLair::AddScripts()
{
    new RaidClearOnyBreathSpellScript();
}
