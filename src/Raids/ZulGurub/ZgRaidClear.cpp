/*
 * mod-raid-clear: Zul'Gurub (map 309). See ZgRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "ZgRaidClear.h"

#include "Common/BossLookup.h"
#include "RaidClearConfig.h"

#include "AttackAction.h"
#include "Creature.h"
#include "GenericSpellActions.h"
#include "Group.h"
#include "Map.h"
#include "Pet.h"
#include "AllSpellScript.h"
#include "ObjectAccessor.h"
#include "ThreatManager.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "ReachTargetActions.h"
#include "RtiTargetValue.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>

using namespace RaidClear::ZulGurub;

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

    triggers.push_back(new TriggerNode("rc zg concoction",
                                       { NextAction("rc zg concoction move away", ACTION_RAID + 5) }));
    triggers.push_back(new TriggerNode("rc zg bad target",
                                       { NextAction("rc zg leave bad target", ACTION_RAID + 3) }));

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

    // The nearest Gurubashi Bat Rider casting Unstable Concoction with its centre less than
    // `range` from `pos`.
    Unit* CastingBatRider(PlayerbotAI* botAI, Position const* pos, float range)
    {
        Unit* nearest = nullptr;
        float best = range;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || unit->GetEntry() != NPC_BAT_RIDER || !unit->IsAlive())
                continue;
            Spell const* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!spell || spell->GetSpellInfo()->Id != SPELL_UNSTABLE_CONCOCTION)
                continue;
            float const dist = unit->GetExactDist(pos);
            if (dist < best)
            {
                nearest = unit;
                best = dist;
            }
        }
        return nearest;
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
    return bot->IsAlive() && bot->IsInCombat() && CastingBatRider(botAI, bot, CONCOCTION_KEEP_AWAY);
}

bool RcZgConcoctionMoveAwayAction::Execute(Event /*event*/)
{
    Unit* rider = CastingBatRider(botAI, bot, CONCOCTION_KEEP_AWAY);
    return rider && MoveAway(rider, CONCOCTION_TARGET - bot->GetExactDist(rider));
}

float RcZgConcoctionHoldMultiplier::GetValue(Action* action)
{
    // Picking a target isn't movement; only getting there is held: on foot, or with Charge,
    // Intercept or Feral Charge, which would undo the walk out in one cast.
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action) &&
                       !dynamic_cast<RcZgConcoctionMoveAwayAction*>(action);
    if (!walks && !dynamic_cast<CastReachTargetSpellAction*>(action))
        return 1.0f;
    if (!bot->IsInCombat() || !CastingBatRider(botAI, bot, CONCOCTION_HOLD))
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;
    return CastingBatRider(botAI, target, CONCOCTION_KEEP_AWAY) ? 0.0f : 1.0f;
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

namespace
{
    // A harmful spell that reaches more than the enemy it is cast at: area and cone spells, ground
    // effects (Blizzard, Rain of Fire, Volley, Consecration) and chains (Multi-Shot, Cleave, Chain
    // Lightning).
    bool HitsMoreThanOneEnemy(SpellInfo const* info)
    {
        if (info->IsPositive())
            return false;
        for (SpellEffectInfo const& effect : info->Effects)
        {
            if (!effect.IsEffect())
                continue;
            if (effect.IsTargetingArea() || effect.IsEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA) || effect.ChainTarget > 1)
                return true;
        }
        return false;
    }

    // Spells the check above can't see, by their playerbots names: the damage sits in a spell they
    // trigger or in something they summon, or they are a buff that copies the bot's swings.
    constexpr std::array<char const*, 10> HIDDEN_SPLASH = {
        "blade flurry", "sweeping strikes", "fire nova", "magma totem", "explosive trap",
        "bladestorm", "starfall", "typhoon", "living bomb", "immolation aura",
    };

    bool IsSplashSpell(PlayerbotAI* botAI, std::string const& name)
    {
        // Picks a spot on the ground, but only to summon the treants there.
        if (name == "force of nature")
            return false;
        for (char const* hidden : HIDDEN_SPLASH)
            if (name == hidden)
                return true;

        uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get();
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        return info && HitsMoreThanOneEnemy(info);
    }
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
    struct Interrupt
    {
        char const* spell;  // playerbots name
        float range;        // 0 = melee reach
    };
    // Counterspell, Wind Shear and Silence already go to an enemy healer that isn't the bot's
    // target (playerbots' "on enemy healer" triggers); the melee ones only ever hit the target.
    constexpr std::array<Interrupt, 8> INTERRUPTS = {{
        { "kick", 0.0f }, { "pummel", 0.0f }, { "shield bash", 0.0f }, { "mind freeze", 0.0f },
        { "wind shear", 25.0f }, { "counterspell", 30.0f }, { "silence", 30.0f }, { "silencing shot", 35.0f },
    }};

    // Zealot Lor'Khan, in the middle of Great Heal.
    Creature* LorkhanHealing(Player* bot)
    {
        Creature* lorkhan = RaidClear::FightingCreature(bot, { NPC_ZEALOT_LORKHAN });
        if (!lorkhan)
            return nullptr;
        Spell const* spell = lorkhan->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        return spell && spell->GetSpellInfo()->Id == SPELL_LORKHAN_GREAT_HEAL ? lorkhan : nullptr;
    }

    // CanCastSpell tests the spell with its cost ignored: a rogue out of energy would pass and
    // then fail the cast on every tick of the heal.
    bool CanPayFor(PlayerbotAI* botAI, Player* bot, char const* spell)
    {
        uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        if (!info)
            return false;
        int32 const cost = info->CalcPowerCost(bot, info->GetSchoolMask());
        return cost <= 0 || bot->GetPower(Powers(info->PowerType)) >= uint32(cost);
    }

    // The first interrupt the bot has ready and in reach of him. CanCastSpell lets an out-of-range
    // target through, so reach is checked here.
    char const* ReadyInterrupt(PlayerbotAI* botAI, Player* bot, Unit* target)
    {
        // A tank holding something else would turn its back on it to reach him.
        if (PlayerbotAI::IsTank(bot) && target->GetVictim() != bot)
            return nullptr;

        char const* ready = nullptr;
        for (Interrupt const& interrupt : INTERRUPTS)
        {
            bool const inReach =
                interrupt.range > 0.0f ? bot->GetDistance(target) <= interrupt.range : bot->IsWithinMeleeRange(target);
            if (inReach && botAI->CanCastSpell(interrupt.spell, target) && CanPayFor(botAI, bot, interrupt.spell))
            {
                ready = interrupt.spell;
                break;
            }
        }
        return ready && bot->IsWithinLOSInMap(target) ? ready : nullptr;
    }
}

bool RcZgLorkhanHealTrigger::IsActive()
{
    if (!bot->IsInCombat())
        return false;
    Creature* lorkhan = LorkhanHealing(bot);
    return lorkhan && ReadyInterrupt(botAI, bot, lorkhan);
}

bool RcZgLorkhanInterruptAction::Execute(Event /*event*/)
{
    Creature* lorkhan = LorkhanHealing(bot);
    if (!lorkhan)
        return false;
    char const* interrupt = ReadyInterrupt(botAI, bot, lorkhan);
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
