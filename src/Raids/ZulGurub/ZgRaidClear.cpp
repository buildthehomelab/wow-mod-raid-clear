/*
 * mod-raid-clear: Zul'Gurub (map 309). See ZgRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "ZgRaidClear.h"

#include "Common/AggroLeash.h"
#include "Common/BossLookup.h"
#include "Common/Interrupts.h"
#include "Common/SplashSpells.h"
#include "Common/WayOut.h"
#include "RaidClearConfig.h"

#include "AttackAction.h"
#include "Creature.h"
#include "GameObject.h"
#include "GenericSpellActions.h"
#include "Group.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "MoveSpline.h"
#include "PathGenerator.h"
#include "Pet.h"
#include "AllSpellScript.h"
#include "ObjectAccessor.h"
#include "ThreatManager.h"
#include "Timer.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "ReachTargetActions.h"
#include "RtiTargetValue.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace RaidClear::ZulGurub;
using RaidClear::FindWayOut;
using RaidClear::Splash::IsSplashSpell;
using RaidClear::OnItsWayOut;
using RaidClear::WAY_OUT_RESEND_MS;
using RaidClear::WAY_OUT_RETRY_MS;

std::vector<RaidClear::KillOrderEntry> const& RaidClear::ZulGurub::KillOrder()
{
    static std::vector<KillOrderEntry> const table = {
        // Trash: whoever heals the pack, then the ones that heal themselves off the raid.
        { NPC_HAKKARI_BLOOD_PRIEST, 0 },
        { NPC_HAKKARI_PRIEST, 1 },
        { NPC_BLOOD_DRINKER, 2 },
        // Venoxis' four guards, before him.
        { NPC_RAZZASHI_COBRA, 1 },
    };
    return table;
}

std::vector<uint32> const& RaidClear::ZulGurub::TankSplitCoTankBosses()
{
    // Hakkar: Cause Insanity takes his tank away, the Aspects of Arlokk and Mar'li wipe its threat.
    static std::vector<uint32> const bosses = { NPC_HAKKAR };
    return bosses;
}

std::vector<uint32> const& RaidClear::ZulGurub::TankSplitThreatShareBosses()
{
    // Hakkar, for the same reasons: once Frenzy is up, whoever he turns to next has to be a tank.
    static std::vector<uint32> const bosses = { NPC_HAKKAR };
    return bosses;
}

void RaidClearZulGurubStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("rc kill order", { NextAction("rc mark kill order", ACTION_RAID + 2) }));

    triggers.push_back(new TriggerNode("rc zg holy wrath keep away",
                                       { NextAction("rc zg holy wrath keep away", ACTION_RAID + 3) }));

    // Above the life-savers (ACTION_EMERGENCY + 5 or 6): a healer that stays for one more cast is
    // dead three seconds later. Under the aggro leash's own step back.
    triggers.push_back(new TriggerNode("rc zg concoction",
                                       { NextAction("rc zg concoction move away", ACTION_EMERGENCY + 7) }));
    triggers.push_back(new TriggerNode("rc zg bad target",
                                       { NextAction("rc zg leave bad target", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc zg liquid fire",
                                       { NextAction("rc zg leave liquid fire", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc zg charmed member",
                                       { NextAction("rc zg control charmed", ACTION_RAID + 4) }));

    triggers.push_back(new TriggerNode("rc zg thekal even kill",
                                       { NextAction("rc zg thekal even kill", ACTION_RAID + 2) }));
    triggers.push_back(new TriggerNode("rc zg lorkhan heal",
                                       { NextAction("rc zg lorkhan interrupt", ACTION_RAID + 4) }));
}

void RaidClearZulGurubStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcZgHolyWrathHoldMultiplier(botAI));
    multipliers.push_back(new RcZgThekalSingleTargetMultiplier(botAI));
    multipliers.push_back(new RcZgBadTargetMultiplier(botAI));
    multipliers.push_back(new RcZgConcoctionHoldMultiplier(botAI));
    multipliers.push_back(new RcZgLiquidFireHoldMultiplier(botAI));
    multipliers.push_back(new RcZgCharmedSplashMultiplier(botAI));
}

// --- Trash ----------------------------------------------------------------

namespace
{
    // A mob the core lets evade every attack (it can't reach its victim), though it may go on
    // hitting whoever it can. The core sets and clears that with every chase path it works out, so
    // a mob a tank is dragging round a corner flickers: it only counts after a full second.
    constexpr uint32 EVADING_FOR_MS = 1000;
    // Asked about less often than this, the run is broken: it starts again.
    constexpr uint32 EVADING_STALE_MS = 700;
    struct EvadeSeen
    {
        uint32 instanceId = 0;  // creature GUIDs repeat between two copies of the raid
        uint32 first = 0;
        uint32 last = 0;
    };
    thread_local std::unordered_map<ObjectGuid, EvadeSeen> tEvading;

    bool IsEvadingMob(Unit const* unit)
    {
        Creature const* creature = unit ? unit->ToCreature() : nullptr;
        if (!creature)
            return false;
        if (!creature->IsAlive() || !creature->IsEvadingAttacks())
        {
            auto it = tEvading.find(creature->GetGUID());
            if (it != tEvading.end() && it->second.instanceId == creature->GetInstanceId())
                tEvading.erase(it);
            return false;
        }

        uint32 const now = getMSTime();
        if (tEvading.size() > 64)
            tEvading.clear();
        EvadeSeen& seen = tEvading[creature->GetGUID()];
        if (seen.instanceId != creature->GetInstanceId() || !seen.first ||
            getMSTimeDiff(seen.last, now) > EVADING_STALE_MS)
        {
            seen.instanceId = creature->GetInstanceId();
            seen.first = now ? now : 1;
        }
        seen.last = now;
        return getMSTimeDiff(seen.first, now) >= EVADING_FOR_MS;
    }

    // A member of the bot's raid that an enemy controls right now.
    bool IsCharmedRaidMember(Player* bot, Unit const* unit)
    {
        Player const* player = unit ? unit->ToPlayer() : nullptr;
        Group const* group = bot->GetGroup();
        return player && player != bot && player->IsCharmed() && group && group->IsMember(player->GetGUID());
    }

    // A Hakkari Priest no spell can touch for the moment.
    bool IsSpellShielded(Unit const* unit)
    {
        return unit && unit->GetEntry() == NPC_HAKKARI_PRIEST && unit->HasAura(SPELL_ANTI_MAGIC_SHIELD);
    }

    // Nobody's target: not the bot's, not its pet's.
    bool IsOffLimits(Player* bot, Unit const* unit)
    {
        return IsEvadingMob(unit) || IsCharmedRaidMember(bot, unit);
    }

    bool IsBadTargetFor(Player* bot, Unit const* unit)
    {
        return IsOffLimits(bot, unit) || (IsSpellShielded(unit) && PlayerbotAI::IsCaster(bot));
    }

    // Every Gurubashi Bat Rider in the fight that is casting Unstable Concoction.
    std::vector<Unit*> CastingBatRiders(PlayerbotAI* botAI)
    {
        std::vector<Unit*> riders;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || unit->GetEntry() != NPC_BAT_RIDER || !unit->IsAlive())
                continue;
            Spell const* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (spell && spell->GetSpellInfo()->Id == SPELL_UNSTABLE_CONCOCTION)
                riders.push_back(unit);
        }
        return riders;
    }

    // Yards from a spot to the nearest of them, centre to centre (how the blast is measured).
    float ConcoctionGap(std::vector<Unit*> const& riders, float x, float y, float z)
    {
        float gap = 1000.0f;
        for (Unit const* rider : riders)
            gap = std::min(gap, rider->GetExactDist(x, y, z));
        return gap;
    }

    float ConcoctionGap(std::vector<Unit*> const& riders, Position const* pos)
    {
        return ConcoctionGap(riders, pos->GetPositionX(), pos->GetPositionY(), pos->GetPositionZ());
    }

    // Whether one of them is casting with its centre less than `range` from `pos`.
    bool BatRiderCastingNear(PlayerbotAI* botAI, Position const* pos, float range)
    {
        return ConcoctionGap(CastingBatRiders(botAI), pos) < range;
    }

    // Liquid Fire burning near the bot, while Jeklik is being fought.
    std::vector<GameObject*> LiquidFiresNear(Player* bot)
    {
        std::vector<GameObject*> fires;
        if (!RaidClear::FightingCreature(bot, { NPC_JEKLIK }))
            return fires;
        std::list<GameObject*> found;
        bot->GetGameObjectListWithEntryInGrid(found, GO_LIQUID_FIRE, LIQUID_FIRE_SCAN);
        for (GameObject* fire : found)
            if (fire && fire->isSpawned())
                fires.push_back(fire);
        return fires;
    }

    float LiquidFireGap(std::vector<GameObject*> const& fires, float x, float y, float z)
    {
        float gap = 1000.0f;
        for (GameObject const* fire : fires)
            gap = std::min(gap, fire->GetExactDist(x, y, z));
        return gap;
    }

    float LiquidFireGap(std::vector<GameObject*> const& fires, Position const* pos)
    {
        return LiquidFireGap(fires, pos->GetPositionX(), pos->GetPositionY(), pos->GetPositionZ());
    }

    // How far from every Liquid Fire the bot should get; 0 when it is fine where it stands.
    float LiquidFireWant(Player* bot, std::vector<GameObject*> const& fires)
    {
        if (fires.empty())
            return 0.0f;
        float const gap = LiquidFireGap(fires, bot);
        // Whoever Jeklik is on takes her along when either of them stands in one, far enough that
        // she ends up outside too: the melee wait for that.
        Creature* jeklik = RaidClear::FightingCreature(bot, { NPC_JEKLIK });
        if (jeklik && jeklik->GetVictim() == bot && gap < LIQUID_FIRE_TANK_SPOT &&
            (gap < LIQUID_FIRE_KEEP_AWAY || LiquidFireGap(fires, jeklik) < LIQUID_FIRE_KEEP_AWAY))
            return LIQUID_FIRE_TANK_SPOT;
        return gap < LIQUID_FIRE_KEEP_AWAY ? LIQUID_FIRE_SPOT : 0.0f;
    }
}

bool RaidClear::ZulGurub::KillOrderSkips(Player* /*bot*/, Unit const* unit)
{
    return IsSpellShielded(unit) || IsEvadingMob(unit);
}

// Playerbots asks every combat strategy what its target picking should skip ("dps target", "tank
// target", the debuff targets). The skull target is taken before this is asked, which is why the
// kill order skips the same enemies. A mind-controlled raid member is never in the attackers (a
// player has no threat list); "rc zg bad target" is what takes a bot off one.
void RaidClearZulGurubStrategy::AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType /*type*/)
{
    Player* bot = botAI->GetBot();
    for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        if (IsBadTargetFor(bot, botAI->GetUnit(guid)))
            exclusions.insert(guid);
}

bool RcZgBadTargetTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    if (IsBadTargetFor(bot, AI_VALUE(Unit*, "current target")))
        return true;
    Pet* pet = bot->GetPet();
    return pet && IsOffLimits(bot, pet->GetVictim());
}

bool RcZgLeaveBadTargetAction::Execute(Event /*event*/)
{
    if (Pet* pet = bot->GetPet())
    {
        if (IsOffLimits(bot, pet->GetVictim()))
        {
            pet->AttackStop();
            pet->InterruptNonMeleeSpells(false);
        }
    }

    // Stopping the pet alone isn't the bot's action for this tick: a pet that is being hit goes
    // straight back, and its owner would do nothing else.
    Unit* current = AI_VALUE(Unit*, "current target");
    if (!IsBadTargetFor(bot, current))
        return false;

    // What playerbots would pick now, if the bot can attack it from here.
    Unit* next = AI_VALUE(Unit*, PlayerbotAI::IsTank(bot) ? "tank target" : "dps target");
    if (next && next != current && next->IsAlive() && !IsBadTargetFor(bot, next) && Attack(next))
        return true;

    // That can be the bad target itself (skull is picked before anything is skipped): then the
    // nearest enemy the bot can hurt.
    Unit* nearest = nullptr;
    float nearestDist = 0.0f;
    for (ObjectGuid const& guid : AI_VALUE(GuidVector, "attackers"))
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || unit == current || !unit->IsAlive() || IsBadTargetFor(bot, unit) || !bot->IsValidAttackTarget(unit))
            continue;
        float const dist = bot->GetDistance(unit);
        if ((nearest && dist >= nearestDist) || !bot->IsWithinLOSInMap(unit))
            continue;
        nearest = unit;
        nearestDist = dist;
    }
    if (nearest && Attack(nearest))
        return true;

    // Nothing else to hit: let go, and playerbots picks again once there is.
    context->GetValue<Unit*>("current target")->Set(nullptr);
    bot->SetTarget(ObjectGuid::Empty);
    bot->SetSelection(ObjectGuid::Empty);
    bot->AttackStop();
    bot->InterruptNonMeleeSpells(false);
    return true;
}

float RcZgBadTargetMultiplier::GetValue(Action* action)
{
    // Skull is picked before anything is skipped, so "dps assist" can ask for one of these.
    if (!dynamic_cast<AttackAction*>(action) || dynamic_cast<RcZgLeaveBadTargetAction*>(action))
        return 1.0f;
    return IsBadTargetFor(bot, action->GetTarget()) ? 0.0f : 1.0f;
}

bool RcZgConcoctionTrigger::IsActive()
{
    // A bot that is feared or rooted isn't going anywhere.
    return bot->IsAlive() && bot->IsInCombat() && botAI->CanMove() &&
           BatRiderCastingNear(botAI, bot, CONCOCTION_KEEP_AWAY);
}

bool RcZgConcoctionMoveAwayAction::Execute(Event /*event*/)
{
    std::vector<Unit*> const riders = CastingBatRiders(botAI);
    auto const gapAt = [&riders](float x, float y, float z) { return ConcoctionGap(riders, x, y, z); };
    if (ConcoctionGap(riders, bot) >= CONCOCTION_KEEP_AWAY)
        return false;
    // Already running somewhere clear of all of them: nothing else this tick, so nothing (a
    // potion, a cast) stops the run.
    if (OnItsWayOut(bot, gapAt, CONCOCTION_KEEP_AWAY))
        return true;

    // Sent off a moment ago and not running: that walk was stopped (the aggro leash's own look
    // at it) or ended short. Found nowhere to go a moment ago: nothing has changed yet.
    uint32 const now = getMSTime();
    if ((_sentAtMs && getMSTimeDiff(_sentAtMs, now) < WAY_OUT_RESEND_MS) ||
        (_noSpotAtMs && getMSTimeDiff(_noSpotAtMs, now) < WAY_OUT_RETRY_MS))
        return false;

    Position spot;
    if (!FindWayOut(botAI, bot, gapAt, CONCOCTION_SPOT, CONCOCTION_WORTH_IT, spot))
    {
        _noSpotAtMs = now ? now : 1;
        return false;
    }

    // Whatever the bot was told last (a walk to its target, an earlier way out that a second Bat
    // Rider has just made useless) would make this walk wait its turn. Straight to the spot the
    // path was checked for: playerbots' own planning could settle for another.
    AI_VALUE(LastMovement&, "last movement").clear();
    bot->CastStop();
    if (!MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false, false,
                true, MovementPriority::MOVEMENT_FORCED))
    {
        _noSpotAtMs = now ? now : 1;
        return false;
    }
    _sentAtMs = now ? now : 1;
    _noSpotAtMs = 0;
    return true;
}

float RcZgConcoctionHoldMultiplier::GetValue(Action* action)
{
    // Picking a target isn't movement; only getting there is held: on foot, or with Charge,
    // Intercept or Feral Charge, which would undo the walk out in one cast.
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action) &&
                       !dynamic_cast<RcZgConcoctionMoveAwayAction*>(action);
    if (!walks && !dynamic_cast<CastReachTargetSpellAction*>(action))
        return 1.0f;
    if (!bot->IsInCombat())
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    std::vector<Unit*> const riders = CastingBatRiders(botAI);
    if (ConcoctionGap(riders, bot) >= CONCOCTION_HOLD)
        return 1.0f;
    return ConcoctionGap(riders, target) < CONCOCTION_KEEP_AWAY ? 0.0f : 1.0f;
}

bool RcZgLiquidFireTrigger::IsActive()
{
    return bot->IsAlive() && bot->IsInCombat() && botAI->CanMove() &&
           LiquidFireWant(bot, LiquidFiresNear(bot)) > 0.0f;
}

bool RcZgLeaveLiquidFireAction::Execute(Event /*event*/)
{
    std::vector<GameObject*> const fires = LiquidFiresNear(bot);
    auto const gapAt = [&fires](float x, float y, float z) { return LiquidFireGap(fires, x, y, z); };
    float const want = LiquidFireWant(bot, fires);
    if (want <= 0.0f)
        return false;
    // The tank taking Jeklik out of one has further to go than the edge of it.
    if (OnItsWayOut(bot, gapAt, want >= LIQUID_FIRE_TANK_SPOT ? LIQUID_FIRE_TANK_SPOT : LIQUID_FIRE_KEEP_AWAY))
        return true;

    uint32 const now = getMSTime();
    if ((_sentAtMs && getMSTimeDiff(_sentAtMs, now) < WAY_OUT_RESEND_MS) ||
        (_noSpotAtMs && getMSTimeDiff(_noSpotAtMs, now) < WAY_OUT_RETRY_MS))
        return false;

    Position spot;
    if (!FindWayOut(botAI, bot, gapAt, want, LIQUID_FIRE_WORTH_IT, spot))
    {
        _noSpotAtMs = now ? now : 1;
        return false;
    }

    AI_VALUE(LastMovement&, "last movement").clear();
    bot->CastStop();
    if (!MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false, false,
                true, MovementPriority::MOVEMENT_FORCED))
    {
        _noSpotAtMs = now ? now : 1;
        return false;
    }
    _sentAtMs = now ? now : 1;
    _noSpotAtMs = 0;
    return true;
}

float RcZgLiquidFireHoldMultiplier::GetValue(Action* action)
{
    // Melee would end up standing in it; ranged and healers stop at spell range.
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action) &&
                       !dynamic_cast<RcZgLeaveLiquidFireAction*>(action) &&
                       !dynamic_cast<RcZgConcoctionMoveAwayAction*>(action);
    if (!walks && !dynamic_cast<CastReachTargetSpellAction*>(action))
        return 1.0f;
    if (!bot->IsInCombat() || PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot))
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;
    return LiquidFireGap(LiquidFiresNear(bot), target) < LIQUID_FIRE_KEEP_AWAY ? 0.0f : 1.0f;
}

// A bot in the middle of a cast doesn't run its engine until the cast ends, and a 2.5 s spell
// outlasts the 3 s it has to get out: wake it the moment the Bat Rider starts.
class RaidClearZgConcoctionSpellScript : public AllSpellScript
{
public:
    RaidClearZgConcoctionSpellScript() : AllSpellScript("RaidClearZgConcoctionSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || spellInfo->Id != SPELL_UNSTABLE_CONCOCTION || caster->GetMapId() != MAP_ID ||
            caster->GetEntry() != NPC_BAT_RIDER)
            return;
        if (!RaidClear::GetConfig().IsRaidEnabled(MAP_ID))
            return;

        Map::PlayerList const& players = caster->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive() || player->GetExactDist(caster) >= CONCOCTION_KEEP_AWAY)
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->HasStrategy("rc zg", BOT_STATE_COMBAT))
                continue;

            botAI->RequestSpellInterrupt();
            botAI->ResetActionDuration();
        }
    }
};

void RaidClear::ZulGurub::AddScripts()
{
    new RaidClearZgConcoctionSpellScript();
}

// --- High Priest Venoxis --------------------------------------------------

namespace
{
    // Venoxis while he can cast Holy Wrath: alive, fighting, still a troll.
    Unit* VenoxisInTrollForm(PlayerbotAI* botAI)
    {
        Unit* boss = botAI->GetAiObjectContext()->GetValue<Unit*>("find target", "high priest venoxis")->Get();
        return boss && boss->IsAlive() && boss->IsInCombat() && !boss->HasAura(SPELL_VENOXIS_TRANSFORM) ? boss : nullptr;
    }

    // Edge-to-edge distance, the way the core measures a chain jump.
    float Gap(Unit const* a, Unit const* b)
    {
        return a->GetExactDist(b) - a->GetObjectSize() - b->GetObjectSize();
    }

    // Who Holy Wrath reaches before it needs a non-tank to carry on. It starts on the top of
    // Venoxis' threat list (DoCastMaxThreat), which is his victim unless a damage dealer has passed
    // the tank without pulling him yet, so both count. Then every tank and every pet standing
    // within a jump of someone already in the chain (an off-tank on the cobras, a hunter's pet on
    // the boss): tanks can take the first few jumps and pets are expendable, but each is a
    // stepping stone, so nobody else may stand near any of them.
    std::vector<Unit*> BuildHolyWrathChain(Player* bot, Unit* boss)
    {
        std::vector<Unit*> chain;
        if (Unit* victim = boss->GetVictim())
            chain.push_back(victim);
        for (ThreatReference const* ref : boss->GetThreatMgr().GetSortedThreatList())
        {
            if (ref->IsOffline())
                continue;
            Unit* top = ref->GetVictim();
            if (top && std::find(chain.begin(), chain.end(), top) == chain.end())
                chain.push_back(top);
            break;
        }

        Group* group = bot->GetGroup();
        if (chain.empty() || !group)
            return chain;

        std::vector<Unit*> stones;
        auto consider = [&](Unit* unit)
        {
            if (unit && unit->IsAlive() && std::find(chain.begin(), chain.end(), unit) == chain.end())
                stones.push_back(unit);
        };
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInMap(bot))
                continue;
            if (PlayerbotAI::IsTank(member))
                consider(member);
            consider(member->GetPet());
        }

        bool added = true;
        while (added && !stones.empty())
        {
            added = false;
            for (auto it = stones.begin(); it != stones.end();)
            {
                Unit* stone = *it;
                bool const linked = std::any_of(chain.begin(), chain.end(), [&](Unit* link)
                {
                    return Gap(stone, link) <= HOLY_WRATH_CHAIN_LINK;
                });
                if (linked)
                {
                    chain.push_back(stone);
                    it = stones.erase(it);
                    added = true;
                }
                else
                    ++it;
            }
        }
        return chain;
    }

    // The chain is the same for every bot in the fight and is asked for by the trigger, the action
    // and the multiplier (once per movement action): build it once per tick per fight.
    constexpr uint32 CHAIN_CACHE_MS = 100;
    struct ChainCache
    {
        ObjectGuid boss;
        uint32 instanceId = 0;
        uint32 ms = 0;
        GuidVector members;
    };
    thread_local ChainCache tChainCache;

    std::vector<Unit*> HolyWrathChain(Player* bot, Unit* boss)
    {
        uint32 const now = getMSTime();
        if (tChainCache.boss == boss->GetGUID() && tChainCache.instanceId == boss->GetInstanceId() &&
            getMSTimeDiff(tChainCache.ms, now) <= CHAIN_CACHE_MS)
        {
            std::vector<Unit*> chain;
            for (ObjectGuid const& guid : tChainCache.members)
                if (Unit* unit = ObjectAccessor::GetUnit(*bot, guid))
                    if (unit->IsAlive())
                        chain.push_back(unit);
            return chain;
        }

        std::vector<Unit*> chain = BuildHolyWrathChain(bot, boss);
        tChainCache.boss = boss->GetGUID();
        tChainCache.instanceId = boss->GetInstanceId();
        tChainCache.ms = now;
        tChainCache.members.clear();
        for (Unit* unit : chain)
            tChainCache.members.push_back(unit->GetGUID());
        return chain;
    }

    bool InChain(std::vector<Unit*> const& chain, Unit const* unit)
    {
        return std::find(chain.begin(), chain.end(), unit) != chain.end();
    }

    // The closest chain member with less than `range` between its edge and a unit of size
    // `size` standing at `pos`.
    Unit* ChainMemberNear(std::vector<Unit*> const& chain, Position const* pos, float size, float range)
    {
        Unit* nearest = nullptr;
        float best = range;
        for (Unit* link : chain)
        {
            float const gap = pos->GetExactDist(link) - size - link->GetObjectSize();
            if (gap < best)
            {
                nearest = link;
                best = gap;
            }
        }
        return nearest;
    }
}

bool RcZgHolyWrathKeepAwayTrigger::IsActive()
{
    if (!bot->IsInCombat())
        return false;
    Unit* boss = VenoxisInTrollForm(botAI);
    if (!boss)
        return false;

    // Tanks go where their mobs are; one that walks in becomes part of the chain.
    if (PlayerbotAI::IsTank(bot))
        return false;

    std::vector<Unit*> const chain = HolyWrathChain(bot, boss);
    return !InChain(chain, bot) && ChainMemberNear(chain, bot, bot->GetObjectSize(), HOLY_WRATH_KEEP_AWAY);
}

bool RcZgHolyWrathKeepAwayAction::Execute(Event /*event*/)
{
    Unit* boss = VenoxisInTrollForm(botAI);
    if (!boss)
        return false;

    std::vector<Unit*> const chain = HolyWrathChain(bot, boss);
    float const size = bot->GetObjectSize();
    Unit* closest = InChain(chain, bot) ? nullptr : ChainMemberNear(chain, bot, size, HOLY_WRATH_KEEP_AWAY);
    if (!closest)
        return false;

    // Rings around the chain member the bot is too close to, all the way round: the nearest spot
    // that is clear of the whole chain, level with the bot (no stepping off the temple's edge or
    // down its stairs), and from which that chain member can still be seen (it's who the healers
    // are healing).
    Map* map = bot->GetMap();
    float const base = closest->GetAngle(bot);
    float const reach = HOLY_WRATH_KEEP_AWAY_TARGET + closest->GetObjectSize() + size;

    static float const offsets[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f, 2.0f, -2.0f, 2.5f, -2.5f, float(M_PI) };
    static float const extras[] = { 0.0f, 5.0f };
    bool found = false;
    float bestX = 0.0f, bestY = 0.0f, bestZ = 0.0f, bestDist = 0.0f;
    for (float const extra : extras)
    {
        for (float const offset : offsets)
        {
            float const angle = base + offset;
            float const x = closest->GetPositionX() + std::cos(angle) * (reach + extra);
            float const y = closest->GetPositionY() + std::sin(angle) * (reach + extra);
            float const z = map->GetHeight(bot->GetPhaseMask(), x, y, bot->GetPositionZ() + HOLY_WRATH_SPOT_HEIGHT);
            if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > HOLY_WRATH_SPOT_HEIGHT)
                continue;
            if (!bot->IsWithinLOS(x, y, z + 2.0f) || !closest->IsWithinLOS(x, y, z + 2.0f))
                continue;

            Position const spot(x, y, z);
            if (ChainMemberNear(chain, &spot, size, HOLY_WRATH_KEEP_AWAY))
                continue;

            float const dist = bot->GetExactDist(&spot);
            if (!found || dist < bestDist)
            {
                found = true;
                bestX = x;
                bestY = y;
                bestZ = z;
                bestDist = dist;
            }
        }
        if (found)
            break;
    }

    if (found)
        return MoveTo(bot->GetMapId(), bestX, bestY, bestZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT);

    // Nothing clean nearby: any step away from it is better than standing in the chain.
    return MoveAway(closest, HOLY_WRATH_KEEP_AWAY_TARGET - Gap(bot, closest));
}

float RcZgHolyWrathHoldMultiplier::GetValue(Action* action)
{
    // Picking a target isn't movement; only walking somewhere is held.
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action) ||
        dynamic_cast<RcZgHolyWrathKeepAwayAction*>(action))
        return 1.0f;

    if (!bot->IsInCombat() || PlayerbotAI::IsTank(bot))
        return 1.0f;
    Unit* boss = VenoxisInTrollForm(botAI);
    if (!boss)
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    std::vector<Unit*> const chain = HolyWrathChain(bot, boss);
    // Whoever Venoxis is on stays put. Walking toward a chain member itself only happens to heal
    // it (into range or line of sight), and that stops well outside.
    if (InChain(chain, bot) || InChain(chain, target))
        return 1.0f;

    // Something inside the chain's reach (Venoxis, a cobra on a tank).
    Unit* closest = ChainMemberNear(chain, target, target->GetObjectSize(), HOLY_WRATH_KEEP_AWAY);
    if (!closest)
        return 1.0f;

    // Melee would have to walk all the way in, so they don't start, however far out they are: one
    // "reach melee" from 30 yd carries a bot to the tank before keep-away can turn it round.
    // Ranged and healers stop at spell range anyway; they're only held once they're close.
    if (!PlayerbotAI::IsRanged(bot) && !PlayerbotAI::IsHeal(bot))
        return 0.0f;
    return Gap(bot, closest) > HOLY_WRATH_HOLD ? 1.0f : 0.0f;
}

// --- High Priest Thekal ---------------------------------------------------

namespace
{
    bool IsThekalOrZealot(Unit const* unit)
    {
        uint32 const entry = unit->GetEntry();
        return entry == NPC_THEKAL || entry == NPC_ZEALOT_LORKHAN || entry == NPC_ZEALOT_ZATH;
    }

    // Still to be brought down: a zealot that is alive, or Thekal while he isn't lying on the floor
    // (he never dies in this phase: he stops at 1 health and can't be selected until he gets up).
    bool IsStanding(Unit const* unit)
    {
        return unit->IsAlive() && !unit->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
    }

    // Thekal while none of the three stays dead: fighting and not a tiger yet. He stays alive and
    // in combat while he lies on the floor, so this holds between attempts.
    Creature* ThekalInTrollForm(Player* bot)
    {
        Creature* thekal = RaidClear::FightingCreature(bot, { NPC_THEKAL });
        return thekal && !thekal->HasAura(SPELL_THEKAL_TIGER_FORM) ? thekal : nullptr;
    }

    // One of the three is down and the 10 s are running: everything that is left has to go.
    bool OneIsDown(Player* bot, Creature const* thekal)
    {
        if (!IsStanding(thekal))
            return true;
        for (uint32 const entry : { NPC_ZEALOT_LORKHAN, NPC_ZEALOT_ZATH })
        {
            Creature const* zealot = RaidClear::InstanceCreature(bot, { entry });
            if (zealot && !zealot->IsAlive())
                return true;
        }
        return false;
    }

    // Where skull belongs while Thekal is a troll. `move` is false when it is already there.
    // Returns nullptr with `move` set when skull sits on one of the three that is down and nobody
    // is left standing (all three are down, he is about to turn into a tiger): it comes off, and
    // from then on the fight is an ordinary one.
    Unit* PickEvenKillTarget(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers, bool& move)
    {
        move = false;
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        Unit* highest = nullptr;
        Unit* tiger = nullptr;
        for (ObjectGuid const& guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            // An evading one can't be hurt: skull on it would have every damage dealer pick it,
            // let go of it and pick it again.
            if (!unit || !unit->IsAlive() || !bot->IsValidAttackTarget(unit) || IsEvadingMob(unit))
                continue;
            if (unit->GetEntry() == NPC_ZULIAN_TIGER)
            {
                if (!tiger || unit->GetHealth() < tiger->GetHealth())
                    tiger = unit;
            }
            else if (IsThekalOrZealot(unit) && IsStanding(unit))
            {
                if (!highest || unit->GetHealth() > highest->GetHealth())
                    highest = unit;
            }
        }

        Unit* skull = botAI->GetUnit(group->GetTargetIcon(RtiTargetValue::skullIndex));

        // His two tigers are small and nothing else would ever kill them: skull never leaves the
        // three otherwise.
        if (tiger)
        {
            if (skull && skull->IsAlive() && skull->GetEntry() == NPC_ZULIAN_TIGER && !IsEvadingMob(skull))
                return nullptr;
            move = true;
            return tiger;
        }

        bool const skullOnThem = skull && IsThekalOrZealot(skull);
        // Skull left on a tiger that started evading comes off as well.
        bool const skullOnEvader = skull && skull->GetEntry() == NPC_ZULIAN_TIGER && IsEvadingMob(skull);
        if (!highest)
        {
            move = (skullOnThem && !IsStanding(skull)) || skullOnEvader;
            return nullptr;
        }

        if (skull != highest && skullOnThem && IsStanding(skull) && !IsEvadingMob(skull))
        {
            float const margin = std::max(float(highest->GetHealth()) * EVEN_KILL_MARGIN, EVEN_KILL_MARGIN_MIN);
            if (float(skull->GetHealth()) + margin >= float(highest->GetHealth()))
                return nullptr;
        }

        move = skull != highest;
        return highest;
    }
}

bool RaidClear::ZulGurub::KillOrderSuspended(Player* bot)
{
    return ThekalInTrollForm(bot) != nullptr;
}

bool RcZgThekalEvenKillTrigger::IsActive()
{
    if (!RaidClear::GetConfig().killOrder || !bot->IsInCombat() || !ThekalInTrollForm(bot))
        return false;
    if (RaidClear::KillOrderMarker(bot->GetGroup()) != bot)
        return false;

    bool move = false;
    PickEvenKillTarget(botAI, bot, AI_VALUE(GuidVector, "attackers"), move);
    return move;
}

bool RcZgThekalEvenKillAction::Execute(Event /*event*/)
{
    Group* group = bot->GetGroup();
    if (!group || !ThekalInTrollForm(bot))
        return false;

    bool move = false;
    Unit* target = PickEvenKillTarget(botAI, bot, AI_VALUE(GuidVector, "attackers"), move);
    if (!move)
        return false;

    group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), target ? target->GetGUID() : ObjectGuid::Empty);
    return true;
}

float RcZgThekalSingleTargetMultiplier::GetValue(Action* action)
{
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || !RaidClear::GetConfig().killOrder || !bot->IsInCombat() || PlayerbotAI::IsTank(bot))
        return 1.0f;

    Creature* thekal = ThekalInTrollForm(bot);
    if (!thekal || !IsSplashSpell(botAI, cast->getSpell()))
        return 1.0f;

    // With one of them down, hitting the other two at once is exactly what's wanted.
    return OneIsDown(bot, thekal) ? 1.0f : 0.0f;
}

namespace
{
    // Zealot Lor'Khan, in the middle of Great Heal.
    Creature* LorkhanHealing(Player* bot)
    {
        Creature* lorkhan = RaidClear::FightingCreature(bot, { NPC_ZEALOT_LORKHAN });
        if (!lorkhan)
            return nullptr;
        Spell const* spell = lorkhan->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        return spell && spell->GetSpellInfo()->Id == SPELL_LORKHAN_GREAT_HEAL ? lorkhan : nullptr;
    }
}

bool RcZgLorkhanHealTrigger::IsActive()
{
    if (!bot->IsInCombat())
        return false;
    Creature* lorkhan = LorkhanHealing(bot);
    return lorkhan && RaidClear::ReadyInterrupt(botAI, bot, lorkhan);
}

bool RcZgLorkhanInterruptAction::Execute(Event /*event*/)
{
    Creature* lorkhan = LorkhanHealing(bot);
    if (!lorkhan)
        return false;
    char const* interrupt = RaidClear::ReadyInterrupt(botAI, bot, lorkhan);
    return interrupt && botAI->CastSpell(interrupt, lorkhan);
}

// --- Mind-controlled raid members -------------------------------------------

float RcZgCharmedSplashMultiplier::GetValue(Action* action)
{
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || !bot->IsInCombat() || !IsSplashSpell(botAI, cast->getSpell()))
        return 1.0f;
    Group* group = bot->GetGroup();
    if (!group)
        return 1.0f;

    // Where the spell lands: on its target, or around the bot itself (Arcane Explosion).
    Unit* target = action->GetTarget();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || !member->IsCharmed() || !member->IsInMap(bot))
            continue;
        if (member->GetExactDist(bot) <= CHARMED_SPLASH_RANGE ||
            (target && member->GetExactDist(target) <= CHARMED_SPLASH_RANGE))
            return 0.0f;
    }
    return 1.0f;
}

// --- Mind-controlled raid members -----------------------------------------

namespace
{
    struct Control
    {
        char const* spell;  // playerbots name
        float range;
        int32 needMs;       // how much longer the mind control has to run for this to be worth it
    };
    // What stops a raid member an enemy has taken over without hurting it. On a player Polymorph
    // lasts 10 s (after a 1.5 s cast) and Hammer of Justice 6 s; whichever is still on the member
    // when the mind control ends is taken off again by the bot that cast it. Not Blind: nothing
    // takes that off.
    constexpr std::array<Control, 2> CONTROLS = {{
        { "polymorph", 30.0f, 6000 },
        { "hammer of justice", 10.0f, 3000 },
    }};
    // A hold with no end of its own (Brain Wash lasts as long as its totem stands, and the totem
    // dies within seconds) counts as a short one: only the stun is used on it.
    constexpr int32 CHARM_NO_END_MS = 5000;
    // One bot at a time per member: whoever starts first has this long to land it.
    constexpr uint32 CONTROL_CLAIM_MS = 2500;

    struct ControlClaim
    {
        ObjectGuid by;
        uint32 atMs = 0;
    };
    // By mind-controlled member. Bots of one raid think on that raid's map update, but which
    // thread runs that update changes from one update to the next.
    std::mutex sControlClaimsMutex;
    std::unordered_map<ObjectGuid, ControlClaim> sControlClaims;

    bool ControlClaimedByOther(Player const* bot, Player const* member)
    {
        std::lock_guard<std::mutex> lock(sControlClaimsMutex);
        auto it = sControlClaims.find(member->GetGUID());
        return it != sControlClaims.end() && it->second.by != bot->GetGUID() &&
               getMSTimeDiff(it->second.atMs, getMSTime()) < CONTROL_CLAIM_MS;
    }

    void ClaimControl(Player const* bot, Player const* member)
    {
        std::lock_guard<std::mutex> lock(sControlClaimsMutex);
        if (sControlClaims.size() > 64)
            sControlClaims.clear();
        sControlClaims[member->GetGUID()] = { bot->GetGUID(), getMSTime() };
    }

    void DropControlClaim(Player const* member)
    {
        std::lock_guard<std::mutex> lock(sControlClaimsMutex);
        sControlClaims.erase(member->GetGUID());
    }

    // How much longer an enemy's hold on this member runs, in ms.
    int32 CharmLeftMs(Player const* member)
    {
        int32 left = 0;
        for (AuraType const type : { SPELL_AURA_MOD_CHARM, SPELL_AURA_MOD_POSSESS, SPELL_AURA_AOE_CHARM })
            for (AuraEffect const* effect : member->GetAuraEffectsByType(type))
            {
                int32 const duration = effect->GetBase()->GetDuration();
                left = std::max(left, duration < 0 ? CHARM_NO_END_MS : duration);
            }
        return left;
    }

    // A mind-controlled member of the raid this bot can stop right now, and with what. Nobody
    // who is stopped already, and nobody another bot has just started on.
    Player* MemberToControl(PlayerbotAI* botAI, Player* bot, char const*& with)
    {
        Group* group = bot->GetGroup();
        if (!group || !bot->IsAlive() || !bot->IsInCombat() || bot->IsCharmed())
            return nullptr;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || !member->IsCharmed() || !member->IsInMap(bot))
                continue;
            if (member->HasUnitState(UNIT_STATE_CONTROLLED) || ControlClaimedByOther(bot, member))
                continue;

            int32 const left = CharmLeftMs(member);
            float const dist = bot->GetDistance(member);
            for (Control const& control : CONTROLS)
            {
                if (left < control.needMs || dist > control.range)
                    continue;
                if (!bot->IsWithinLOSInMap(member) || !botAI->CanCastSpell(control.spell, member) ||
                    !RaidClear::CanPayFor(botAI, bot, control.spell))
                    continue;
                with = control.spell;
                return member;
            }
        }
        return nullptr;
    }
}

namespace
{
    // The bot's own stuns and Polymorphs on members of its raid who are themselves again (the mind
    // control ran out first, or whatever cast it died).
    std::vector<std::pair<Player*, uint32>> ControlsToLetGo(Player* bot)
    {
        std::vector<std::pair<Player*, uint32>> spells;
        Group* group = bot->GetGroup();
        if (!group)
            return spells;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || member->IsCharmed() || !member->IsInMap(bot) ||
                !member->HasUnitState(UNIT_STATE_CONTROLLED))
                continue;
            for (AuraType const type : { SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_STUN })
                for (AuraEffect const* effect : member->GetAuraEffectsByType(type))
                    if (effect->GetCasterGUID() == bot->GetGUID())
                        spells.emplace_back(member, effect->GetId());
        }
        return spells;
    }

    bool ControlsCharmed(Player const* bot)
    {
        return bot->getClass() == CLASS_MAGE || bot->getClass() == CLASS_PALADIN;
    }
}

bool RcZgCharmedMemberTrigger::IsActive()
{
    if (!ControlsCharmed(bot) || !bot->IsAlive())
        return false;
    char const* with = nullptr;
    return !ControlsToLetGo(bot).empty() || MemberToControl(botAI, bot, with) != nullptr;
}

bool RcZgControlCharmedAction::Execute(Event /*event*/)
{
    if (!ControlsCharmed(bot))
        return false;

    // A member who is back before the sheep or the stun has worn off: let go of it. It isn't this
    // bot's whole turn.
    for (auto const& [freed, spellId] : ControlsToLetGo(bot))
        freed->RemoveAurasDueToSpell(spellId, bot->GetGUID());

    char const* with = nullptr;
    Player* member = MemberToControl(botAI, bot, with);
    if (!member)
        return false;

    ClaimControl(bot, member);
    if (botAI->CastSpell(with, member))
        return true;
    DropControlClaim(member);
    return false;
}
