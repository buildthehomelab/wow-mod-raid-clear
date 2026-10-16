/*
 * mod-raid-clear: Ruins of Ahn'Qiraj, Ossirian the Unscarred. See AqRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"

#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "LastMovementValue.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Playerbots.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <list>
#include <mutex>
#include <unordered_map>

using namespace RaidClear::RuinsOfAhnQiraj;

namespace
{
    Creature* FightingOssirian(Player* bot) { return RaidClear::FightingCreature(bot, { NPC_OSSIRIAN }); }

    // The weakness area round a crystal, from the spell data (30 yd on a stock server).
    float WeaknessReach()
    {
        static float const reach = []
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_FIRE_WEAKNESS);
            float const r = info ? info->Effects[EFFECT_0].CalcRadius() : 0.0f;
            return r > 0.0f ? r : 30.0f;
        }();
        return reach;
    }

    bool HasStrength(Unit const* boss) { return boss->HasAura(SPELL_STRENGTH_OF_OSSIRIAN); }

    // How long his longest weakness has left; 0 with none.
    uint32 WeaknessLeftMs(Unit const* boss)
    {
        int32 left = 0;
        for (uint32 spellId : OSSIRIAN_WEAKNESSES)
            if (Aura const* aura = boss->GetAura(spellId))
                left = std::max(left, aura->GetDuration());
        return uint32(std::max(left, 0));
    }

    bool Usable(GameObject const* crystal)
    {
        return crystal && crystal->IsInWorld() && crystal->isSpawned() && !crystal->HasGameObjectFlag(GO_FLAG_IN_USE);
    }

    // Whether his weakness would reach him from this crystal.
    bool InReach(Unit const* boss, GameObject const* crystal)
    {
        return boss->GetExactDist2d(crystal) <= WeaknessReach() - CRYSTAL_REACH_MARGIN;
    }

    // How long it takes to cover the ground between a unit and a crystal's reach.
    uint32 WalkMs(Unit const* unit, GameObject const* crystal, float reach)
    {
        float const yards = std::max(0.0f, unit->GetExactDist2d(crystal) - reach);
        return uint32(yards / LEAD_SPEED * 1000.0f);
    }

    // Time for the tank he is on to set off for the crystal: he has Strength, or his weakness runs
    // out before the walk would be over.
    bool LeadDue(Unit const* boss, GameObject const* crystal)
    {
        if (HasStrength(boss))
            return true;
        return WeaknessLeftMs(boss) < WalkMs(boss, crystal, WeaknessReach() - CRYSTAL_REACH_MARGIN) + LEAD_SLACK_MS;
    }

    // Time for the bot that uses it to be on its way: a little before the tank.
    bool ClickerDue(Player const* bot, Unit const* boss, GameObject const* crystal)
    {
        if (HasStrength(boss))
            return true;
        uint32 const walk = std::max(WalkMs(bot, crystal, CLICK_STAND),
                                     WalkMs(boss, crystal, WeaknessReach() - CRYSTAL_REACH_MARGIN));
        return WeaknessLeftMs(boss) < walk + LEAD_SLACK_MS + CLICK_SLACK_MS;
    }

    // The crystal the raid takes him to and the bot that uses it, kept per instance: every bot
    // has to agree on both, and the crystal mustn't change while the tank is on its way.
    struct CrystalPlan
    {
        ObjectGuid crystal;
        ObjectGuid clicker;
        std::unordered_map<ObjectGuid, uint32> blockedUntil;     // crystal -> getMSTime()
        std::unordered_map<ObjectGuid, uint32> skippedClickers;  // bot -> getMSTime()
        // The crystal just used, until its weakness has landed: the plan stays on it meanwhile.
        ObjectGuid usedCrystal;
        uint32 usedAtMs = 0;
        uint32 weaknessAtUse = 0;
        // No usable crystal was found: not looked for again before this.
        uint32 noCrystalUntilMs = 0;
        uint32 lastUsedMs = 0;
    };
    std::unordered_map<uint32, CrystalPlan> sPlans;  // instance id -> plan
    std::mutex sPlansLock;

    // An instance not asked about for this long is dropped (the fight is long over).
    constexpr uint32 PLAN_STALE_MS = 10 * MINUTE * IN_MILLISECONDS;
    // With no usable crystal, the room is searched again after this.
    constexpr uint32 NO_CRYSTAL_RETRY_MS = 1000;

    // Whether a getMSTime() stamp kept in the plan is still ahead.
    bool Ahead(uint32 until, uint32 now) { return until && int32(until - now) > 0; }

    void PruneUntil(std::unordered_map<ObjectGuid, uint32>& entries, uint32 now)
    {
        for (auto it = entries.begin(); it != entries.end();)
        {
            if (!Ahead(it->second, now))
                it = entries.erase(it);
            else
                ++it;
        }
    }

    bool CanUseCrystal(Player* member, Unit const* boss, uint32 instanceId, CrystalPlan const& plan)
    {
        if (!member || !member->IsInWorld() || !member->IsAlive() || member->GetInstanceId() != instanceId ||
            member->GetMapId() != MAP_ID || PlayerbotAI::IsTank(member) || boss->GetVictim() == member ||
            plan.skippedClickers.count(member->GetGUID()))
            return false;
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        return memberAI && memberAI->HasStrategy("rc aq20", BOT_STATE_COMBAT);
    }

    // The bot (not a tank, not his victim, not one that couldn't get there before) nearest to the
    // crystal; the bot asking if it has no group.
    // A damage dealer before a healer: the bot is away from the fight while it waits there.
    Player* PickClicker(Player* bot, Unit const* boss, GameObject const* crystal, CrystalPlan const& plan)
    {
        uint32 const instanceId = bot->GetInstanceId();
        Group* group = bot->GetGroup();
        if (!group)
            return CanUseCrystal(bot, boss, instanceId, plan) ? bot : nullptr;

        Player* best = nullptr;
        bool bestHeals = false;
        float bestDist = 0.0f;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!CanUseCrystal(member, boss, instanceId, plan))
                continue;
            bool const heals = PlayerbotAI::IsHeal(member);
            float const dist = member->GetExactDist2d(crystal);
            bool better = !best || (bestHeals && !heals);
            if (!better && heals == bestHeals)
                better = dist < bestDist || (dist == bestDist && member->GetGUID() < best->GetGUID());
            if (better)
            {
                best = member;
                bestHeals = heals;
                bestDist = dist;
            }
        }
        return best;
    }

    // The plan for the bot's instance, made again when its crystal is gone or used, or its bot
    // can't use it any more. False with no crystal to go to.
    // `pending`, when given, says whether the crystal is one just used whose weakness hasn't
    // landed yet: nothing to do at it but wait.
    bool CurrentPlan(Player* bot, Creature* boss, GameObject*& crystal, ObjectGuid& clicker, bool* pending = nullptr)
    {
        if (pending)
            *pending = false;
        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(sPlansLock);
        for (auto it = sPlans.begin(); it != sPlans.end();)
        {
            if (it->second.lastUsedMs && getMSTimeDiff(it->second.lastUsedMs, now) > PLAN_STALE_MS)
                it = sPlans.erase(it);
            else
                ++it;
        }

        CrystalPlan& plan = sPlans[bot->GetInstanceId()];
        plan.lastUsedMs = now ? now : 1;
        PruneUntil(plan.blockedUntil, now);
        PruneUntil(plan.skippedClickers, now);

        // A crystal was just used: its trigger is still casting the weakness, and he has to stay
        // in its reach until that lands. The plan stays on it until his weakness has jumped back
        // up, or for USE_GRACE_MS.
        if (plan.usedCrystal)
        {
            bool const landed = WeaknessLeftMs(boss) > plan.weaknessAtUse + USE_LANDED_MS;
            if (!landed && getMSTimeDiff(plan.usedAtMs, now) < USE_GRACE_MS)
            {
                crystal = ObjectAccessor::GetGameObject(*boss, plan.usedCrystal);
                clicker = plan.clicker;
                if (pending)
                    *pending = true;
                return crystal != nullptr;
            }
            if (plan.crystal == plan.usedCrystal)
                plan.crystal.Clear();
            plan.usedCrystal.Clear();
        }

        crystal = plan.crystal ? ObjectAccessor::GetGameObject(*boss, plan.crystal) : nullptr;
        if (!Usable(crystal) || plan.blockedUntil.count(plan.crystal))
        {
            crystal = nullptr;
            plan.crystal.Clear();
            plan.clicker.Clear();
            if (Ahead(plan.noCrystalUntilMs, now))
                return false;

            // The usable crystal nearest to him that nobody was stopped on the way to.
            std::list<GameObject*> found;
            boss->GetGameObjectListWithEntryInGrid(found, GO_OSSIRIAN_CRYSTAL, CRYSTAL_SEARCH);
            float bestDist = 0.0f;
            for (GameObject* go : found)
            {
                if (!Usable(go) || plan.blockedUntil.count(go->GetGUID()))
                    continue;
                float const dist = boss->GetExactDist2d(go);
                if (!crystal || dist < bestDist)
                {
                    crystal = go;
                    bestDist = dist;
                }
            }
            if (!crystal)
            {
                uint32 const retry = now + NO_CRYSTAL_RETRY_MS;
                plan.noCrystalUntilMs = retry ? retry : 1;
                return false;
            }
            plan.noCrystalUntilMs = 0;
            plan.crystal = crystal->GetGUID();
        }

        Player* current = plan.clicker ? ObjectAccessor::GetPlayer(*boss, plan.clicker) : nullptr;
        if (!CanUseCrystal(current, boss, bot->GetInstanceId(), plan))
        {
            current = PickClicker(bot, boss, crystal, plan);
            plan.clicker = current ? current->GetGUID() : ObjectGuid::Empty;
        }
        clicker = plan.clicker;
        return true;
    }

    // The leading tank couldn't get to this crystal: another one for a while.
    void BlockCrystal(Player* bot, ObjectGuid crystal)
    {
        uint32 const until = getMSTime() + CRYSTAL_BLOCKED_MS;
        std::lock_guard<std::mutex> guard(sPlansLock);
        CrystalPlan& plan = sPlans[bot->GetInstanceId()];
        plan.blockedUntil[crystal] = until ? until : 1;
        if (plan.crystal == crystal)
        {
            plan.crystal.Clear();
            plan.clicker.Clear();
        }
    }

    // The bot picked to use the crystal couldn't get to it: somebody else for a while.
    void SkipClicker(Player* bot)
    {
        uint32 const until = getMSTime() + CRYSTAL_BLOCKED_MS;
        std::lock_guard<std::mutex> guard(sPlansLock);
        CrystalPlan& plan = sPlans[bot->GetInstanceId()];
        plan.skippedClickers[bot->GetGUID()] = until ? until : 1;
        if (plan.clicker == bot->GetGUID())
            plan.clicker.Clear();
    }

    // The bot has just used the crystal. Once per crystal: a second use while the first one's
    // weakness is still on its way doesn't start the wait again.
    void MarkUsed(Player* bot, GameObject const* crystal, Unit const* boss)
    {
        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(sPlansLock);
        CrystalPlan& plan = sPlans[bot->GetInstanceId()];
        if (plan.usedCrystal == crystal->GetGUID())
            return;
        plan.usedCrystal = crystal->GetGUID();
        plan.usedAtMs = now ? now : 1;
        plan.weaknessAtUse = WeaknessLeftMs(boss);
    }

    // The next step of a walk to a crystal, judged by whether it gets anywhere: a bot standing
    // still a while after it was sent is sent again (a stun or a knock-up stopped it), and given
    // up on after two sends in a row that brought it no closer (the aggro leash stops the walk,
    // or there is no way there).
    enum class WalkStep
    {
        Walking,  // on its way: nothing else this tick
        Wait,     // just sent: give the walk a moment to start
        Send,
        GiveUp,
    };

    WalkStep NextStep(CrystalWalk& walk, Player* bot, GameObject const* crystal)
    {
        if (walk.to != crystal->GetGUID())
        {
            walk = CrystalWalk();
            walk.to = crystal->GetGUID();
            return WalkStep::Send;
        }
        if (bot->isMoving())
            return WalkStep::Walking;
        uint32 const now = getMSTime();
        if (walk.sentAtMs && getMSTimeDiff(walk.sentAtMs, now) < LEAD_STUCK_MS)
            return WalkStep::Wait;
        if (walk.sentAtMs)
        {
            if (bot->GetExactDist2d(crystal) > walk.sentDist - WALK_PROGRESS)
                ++walk.fruitless;
            else
                walk.fruitless = 0;
        }
        if (walk.fruitless >= WALK_TRIES)
        {
            walk = CrystalWalk();
            return WalkStep::GiveUp;
        }
        return WalkStep::Send;
    }

    void Sent(CrystalWalk& walk, Player* bot, GameObject const* crystal)
    {
        uint32 const now = getMSTime();
        walk.sentAtMs = now ? now : 1;
        walk.sentDist = bot->GetExactDist2d(crystal);
    }

    // The leading tank is close enough: on the crystal, or where he, following it to melee reach,
    // ends up in the crystal's reach whichever side he comes from.
    float TankArriveDistance(Player* bot, Unit const* boss)
    {
        return std::max(LEAD_ARRIVED, WeaknessReach() - CRYSTAL_REACH_MARGIN - boss->GetMeleeRange(bot) - LEAD_ARRIVED);
    }

    bool TankArrived(Player* bot, Unit const* boss, GameObject const* crystal)
    {
        return bot->GetExactDist2d(crystal) <= TankArriveDistance(bot, boss);
    }

    // The bot is the tank he is on.
    bool IsLeadingTank(Player* bot, Unit const* boss)
    {
        return boss->GetVictim() == bot && PlayerbotAI::IsTank(bot);
    }

    void UseCrystal(Player* bot, GameObject* crystal)
    {
        // Playerbots' own way: the use (the crystal's animation) and the report of it (which is
        // what the core's crystal script answers, with the weakness).
        WorldPacket use(CMSG_GAMEOBJ_USE);
        use << crystal->GetGUID();
        bot->GetSession()->HandleGameObjectUseOpcode(use);

        WorldPacket report(CMSG_GAMEOBJ_REPORT_USE);
        report << crystal->GetGUID();
        bot->GetSession()->HandleGameobjectReportUse(report);
    }
}

// --- The tank he is on ----------------------------------------------------

bool RcAq20LeadToCrystalTrigger::IsActive()
{
    if (!bot->IsAlive() || !botAI->CanMove())
        return false;
    Creature* boss = FightingOssirian(bot);
    if (!boss || !IsLeadingTank(bot, boss))
        return false;

    GameObject* crystal = nullptr;
    ObjectGuid clicker;
    if (!CurrentPlan(bot, boss, crystal, clicker))
        return false;
    // Close enough to stop: the crystal's bot does the rest, the multiplier keeps the tank there.
    if (TankArrived(bot, boss, crystal))
        return false;
    return LeadDue(boss, crystal);
}

bool RcAq20LeadToCrystalAction::Execute(Event /*event*/)
{
    Creature* boss = FightingOssirian(bot);
    if (!boss || !IsLeadingTank(bot, boss))
        return false;
    GameObject* crystal = nullptr;
    ObjectGuid clicker;
    if (!CurrentPlan(bot, boss, crystal, clicker))
        return false;
    // A queued action can run after its trigger has gone quiet: only while it is time.
    if (TankArrived(bot, boss, crystal) || !LeadDue(boss, crystal))
    {
        _walk = CrystalWalk();
        return false;
    }

    switch (NextStep(_walk, bot, crystal))
    {
        case WalkStep::Walking:
            return true;
        case WalkStep::Wait:
            return false;
        case WalkStep::GiveUp:
            BlockCrystal(bot, crystal->GetGUID());
            return false;
        case WalkStep::Send:
            break;
    }

    // To the spot on its own side of the crystal where it counts as there, not onto the crystal:
    // he follows it, and the bot at the crystal stays out of his War Stomp.
    float const dist = bot->GetExactDist2d(crystal);
    float const stopAt = std::max(LEAD_ARRIVED, TankArriveDistance(bot, boss) - LEAD_ARRIVED);
    float const x = crystal->GetPositionX() + (bot->GetPositionX() - crystal->GetPositionX()) / dist * stopAt;
    float const y = crystal->GetPositionY() + (bot->GetPositionY() - crystal->GetPositionY()) / dist * stopAt;

    // Whatever the bot was told last (a step to face him away from the raid) would make this walk
    // wait its turn.
    AI_VALUE(LastMovement&, "last movement").clear();
    if (!MoveTo(bot->GetMapId(), x, y, crystal->GetPositionZ(), false, false, false, true,
                MovementPriority::MOVEMENT_FORCED))
    {
        BlockCrystal(bot, crystal->GetGUID());
        _walk = CrystalWalk();
        return false;
    }
    Sent(_walk, bot, crystal);
    return true;
}

// --- The bot that uses the crystal ----------------------------------------

bool RcAq20CrystalTrigger::IsActive()
{
    if (!bot->IsAlive() || !botAI->CanMove())
        return false;
    Creature* boss = FightingOssirian(bot);
    if (!boss)
        return false;
    GameObject* crystal = nullptr;
    ObjectGuid clicker;
    if (!CurrentPlan(bot, boss, crystal, clicker) || clicker != bot->GetGUID())
        return false;
    return ClickerDue(bot, boss, crystal);
}

bool RcAq20UseCrystalAction::Execute(Event /*event*/)
{
    Creature* boss = FightingOssirian(bot);
    if (!boss)
        return false;
    GameObject* crystal = nullptr;
    ObjectGuid clicker;
    bool pending = false;
    if (!CurrentPlan(bot, boss, crystal, clicker, &pending) || clicker != bot->GetGUID())
        return false;
    // A queued action can run after its trigger has gone quiet: only while it is time.
    if (!pending && !ClickerDue(bot, boss, crystal))
        return false;

    if (bot->GetExactDist2d(crystal) > CLICK_STAND)
    {
        switch (NextStep(_walk, bot, crystal))
        {
            case WalkStep::Walking:
                return true;
            case WalkStep::Wait:
                return false;
            case WalkStep::GiveUp:
                SkipClicker(bot);
                return false;
            case WalkStep::Send:
                break;
        }
        AI_VALUE(LastMovement&, "last movement").clear();
        if (!MoveTo(bot->GetMapId(), crystal->GetPositionX(), crystal->GetPositionY(), crystal->GetPositionZ(),
                    false, false, false, true, MovementPriority::MOVEMENT_FORCED))
        {
            SkipClicker(bot);
            _walk = CrystalWalk();
            return false;
        }
        Sent(_walk, bot, crystal);
        return true;
    }
    _walk = CrystalWalk();

    // Used a moment ago: its weakness is on its way. Using it again would only stop the bot's
    // cast every tick (or, if that weakness missed him, spawn crystal after crystal).
    if (pending)
        return false;

    // There. Each use spawns the next crystal, so it is used as soon as he is in reach and his
    // weakness is getting short; until then the bot fights from here.
    if (!InReach(boss, crystal) || (!HasStrength(boss) && WeaknessLeftMs(boss) >= CLICK_AHEAD_MS))
        return false;
    if (!bot->IsWithinDistInMap(crystal, INTERACTION_DISTANCE))
        return false;

    bot->CastStop();
    UseCrystal(bot, crystal);
    MarkUsed(bot, crystal, boss);
    return true;
}

// --- Everyone -------------------------------------------------------------

float RcAq20CrystalMultiplier::GetValue(Action* action)
{
    // Playerbots' own crystal walk: every bot of the raid to the crystal, replaced by the two
    // roles above.
    if (action->getName() == "aq20 use crystal")
        return 0.0f;

    if (dynamic_cast<RcAq20LeadToCrystalAction*>(action) || dynamic_cast<RcAq20UseCrystalAction*>(action) ||
        dynamic_cast<RcAqExplodeMoveAwayAction*>(action))
        return 1.0f;
    // Picking a target isn't movement.
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action);
    // Stepping out of something on the floor, back from a pack that isn't in the fight, or off
    // a spot a knockback would throw the bot into one still goes.
    std::string const& name = action->getName();
    if (!walks || name == "avoid aoe" || name == "rc aggro leash" || name == "rc knockback footing")
        return 1.0f;

    Creature* boss = FightingOssirian(bot);
    if (!boss)
        return 1.0f;
    GameObject* crystal = nullptr;
    ObjectGuid clicker;
    if (!CurrentPlan(bot, boss, crystal, clicker))
        return 1.0f;

    // The tank on its way, or at the crystal waiting for him to come into reach: no other walk
    // (facing him away from the raid, back to the camp) takes it, and him, somewhere else.
    if (IsLeadingTank(bot, boss) && LeadDue(boss, crystal))
        return 0.0f;
    // The same for the bot that uses it.
    if (clicker == bot->GetGUID() && ClickerDue(bot, boss, crystal))
        return 0.0f;
    return 1.0f;
}
