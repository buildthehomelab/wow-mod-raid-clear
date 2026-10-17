/*
 * mod-raid-clear: Temple of Ahn'Qiraj, the Twin Emperors. See AqRaidClear.h for the mechanics; the
 * exploding bugs are the Anubisath Explode rule's (AqRaidClear.cpp).
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"
#include "Common/Charmed.h"
#include "Common/FixedSpots.h"
#include "Common/TankRoles.h"
#include "RaidClearConfig.h"

#include "AllSpellScript.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GenericSpellActions.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <list>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    enum class TwinRole
    {
        None,
        VeklorTank,
        VeknilashTank,
    };

    struct Blizzard
    {
        float x;
        float y;
        float radius;
    };

    // Who holds which twin, and where the Blizzards are, in the bot's copy of the fight.
    struct TwinsView
    {
        ObjectGuid veklor;
        ObjectGuid veknilash;
        ObjectGuid veklorTank;
        ObjectGuid veknilashTank;
        bool landing = false;
        std::vector<Blizzard> blizzards;
    };

    uint32 MsSinceTeleport(Unit const* veklor);

    // The two are still landing after a teleport (TWINS_LANDING_MS).
    bool Landing(Creature const* veklor, Creature const* veknilash)
    {
        for (Creature const* twin : { veklor, veknilash })
            if (twin->HasAura(SPELL_TWIN_TELEPORT_STUN) || twin->HasUnitState(UNIT_STATE_ROOT) ||
                twin->HasReactState(REACT_PASSIVE))
                return true;
        return MsSinceTeleport(veklor) < TWINS_LANDING_MS;
    }

    // The two tanks that hold the twins: the first two living tank bots of the raid in group order,
    // paladins first. Holy is the only school both twins take, and the one of the two tanks that
    // holds Vek'lor has nothing else to hurt him with (a warrior can't touch him). Vek'nilash's is
    // the one he is on, else the one nearer him; Vek'lor's is the other. A teleport swaps them:
    // Vek'nilash lands where Vek'lor's tank stands and takes the nearest. While they land, their
    // victims are still the old ones (a passive creature keeps its victim), so it goes by distance
    // alone then.
    void PickTwinTanks(Player* bot, Creature* veknilash, TwinsView& view)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return;

        std::vector<Player*> paladins;
        std::vector<Player*> others;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || !member->IsInMap(bot) || !GET_PLAYERBOT_AI(member) ||
                !PlayerbotAI::IsTank(member) || RaidClear::Charmed::IsCharmedByEnemy(member))
                continue;
            (member->getClass() == CLASS_PALADIN ? paladins : others).push_back(member);
        }
        paladins.insert(paladins.end(), others.begin(), others.end());
        if (paladins.empty())
            return;

        Player* first = paladins[0];
        Player* second = paladins.size() > 1 ? paladins[1] : nullptr;
        Unit* onNilash = view.landing ? nullptr : veknilash->GetVictim();
        bool firstHasNilash = onNilash == first;
        if (second && onNilash != first && onNilash != second)
            firstHasNilash = first->GetExactDist(veknilash) <= second->GetExactDist(veknilash);
        else if (!second)
            firstHasNilash = true;  // one tank: Vek'nilash's, and Vek'lor goes untanked

        view.veknilashTank = firstHasNilash ? first->GetGUID() : second->GetGUID();
        if (second)
            view.veklorTank = firstHasNilash ? second->GetGUID() : first->GetGUID();
    }

    void FindBlizzards(Creature* veklor, TwinsView& view)
    {
        // His Blizzard goes on a random player within 45 yd of him.
        constexpr float SEARCH = 70.0f;
        std::list<WorldObject*> objects;
        Acore::AllWorldObjectsInRange check(veklor, SEARCH);
        Acore::WorldObjectListSearcher<Acore::AllWorldObjectsInRange> searcher(veklor, objects, check,
                                                                               GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
        Cell::VisitObjects(veklor, searcher, SEARCH);
        for (WorldObject* object : objects)
        {
            DynamicObject const* dynObj = object->ToDynObject();
            if (dynObj && dynObj->GetSpellId() == SPELL_TWINS_BLIZZARD)
                view.blizzards.push_back({ dynObj->GetPositionX(), dynObj->GetPositionY(), dynObj->GetRadius() });
        }
    }

    // The fight in the bot's instance, looked up again after TWINS_FRESH_MS: the triggers and
    // multipliers of the whole raid ask many times a second. One instance's bots update one after
    // another on one thread. nullptr when the two aren't both alive and fighting.
    constexpr uint32 TWINS_FRESH_MS = 200;
    TwinsView const* Twins(Player* bot)
    {
        thread_local uint32 tInstanceId = 0;
        thread_local uint32 tAtMs = 0;
        thread_local bool tFound = false;
        thread_local TwinsView tView;
        uint32 const now = getMSTime();
        if (tInstanceId == bot->GetInstanceId() && tAtMs && getMSTimeDiff(tAtMs, now) < TWINS_FRESH_MS)
            return tFound ? &tView : nullptr;

        tInstanceId = bot->GetInstanceId();
        tAtMs = now ? now : 1;
        tView = TwinsView();
        Creature* veklor = RaidClear::FightingCreature(bot, { NPC_VEKLOR });
        Creature* veknilash = RaidClear::FightingCreature(bot, { NPC_VEKNILASH });
        tFound = veklor && veknilash;
        if (tFound)
        {
            tView.veklor = veklor->GetGUID();
            tView.veknilash = veknilash->GetGUID();
            tView.landing = Landing(veklor, veknilash);
            PickTwinTanks(bot, veknilash, tView);
            FindBlizzards(veklor, tView);
        }
        return tFound ? &tView : nullptr;
    }

    TwinRole RoleOf(Player* bot, TwinsView const& view)
    {
        if (bot->GetGUID() == view.veklorTank)
            return TwinRole::VeklorTank;
        if (bot->GetGUID() == view.veknilashTank)
            return TwinRole::VeknilashTank;
        return TwinRole::None;
    }

    bool IsTwin(Unit const* unit, TwinsView const& view)
    {
        return unit && (unit->GetGUID() == view.veklor || unit->GetGUID() == view.veknilash);
    }

    // Whether the bot is to hit this twin. Each tank its own. Every other tank and every melee
    // bot and hunter: Vek'nilash only, as Vek'lor takes no physical damage and Bursts whoever
    // stands next to him. Casters and healers: Vek'lor only, as Vek'nilash takes no spell but holy
    // and Uppercuts whoever is in his reach. (Damage on either is shared with the other.)
    bool HitsTwin(Player* bot, TwinRole role, uint32 entry)
    {
        if (role == TwinRole::VeklorTank)
            return entry == NPC_VEKLOR;
        if (role == TwinRole::VeknilashTank)
            return entry == NPC_VEKNILASH;
        bool const physical = PlayerbotAI::IsTank(bot) || !PlayerbotAI::IsCaster(bot);
        return entry == (physical ? NPC_VEKNILASH : NPC_VEKLOR);
    }

    // The twin this bot goes for, or nullptr (a healer picks no target here).
    Creature* TwinFor(Player* bot, TwinRole role, TwinsView const& view)
    {
        if (role == TwinRole::None && PlayerbotAI::IsHeal(bot))
            return nullptr;
        ObjectGuid const guid = HitsTwin(bot, role, NPC_VEKLOR) ? view.veklor : view.veknilash;
        return ObjectAccessor::GetCreature(*bot, guid);
    }

    // An Imp's Firebolt or a Water Elemental's Waterbolt can hurt Vek'lor; every other pet hits
    // physical, which only Vek'nilash takes.
    bool IsCasterPet(Unit const* pet)
    {
        constexpr uint32 NPC_IMP = 416;
        constexpr uint32 NPC_WATER_ELEMENTAL = 510;
        return pet && (pet->GetEntry() == NPC_IMP || pet->GetEntry() == NPC_WATER_ELEMENTAL);
    }

    bool PetHitsTwin(Unit const* pet, uint32 entry)
    {
        return entry == (IsCasterPet(pet) ? NPC_VEKLOR : NPC_VEKNILASH);
    }

    // --- The teleport clock ------------------------------------------------

    // When the fight was first seen and when Vek'lor last cast Twin Teleport, by instance. Map
    // updates move between threads from one tick to the next, so it is shared and locked.
    struct TeleportClock
    {
        ObjectGuid veklor;
        uint32 engagedMs = 0;
        uint32 seenMs = 0;
        uint32 lastTeleportMs = 0;  // 0: none yet in this fight
        int8 veknilashSpot = -1;    // torch spot Vek'nilash is held at: 0 A, 1 B, -1 not picked yet
    };
    // A fight nobody has looked at for this long is over: the next look starts a new one.
    constexpr uint32 TELEPORT_CLOCK_STALE_MS = 10000;
    std::mutex sClockLock;
    std::unordered_map<uint32, TeleportClock> sClocks;

    // Call with the lock held.
    TeleportClock& ClockOf(Unit const* veklor, uint32 now)
    {
        TeleportClock& clock = sClocks[veklor->GetInstanceId()];
        if (clock.veklor != veklor->GetGUID() || !clock.seenMs || getMSTimeDiff(clock.seenMs, now) > TELEPORT_CLOCK_STALE_MS)
        {
            clock = TeleportClock();
            clock.veklor = veklor->GetGUID();
            clock.engagedMs = now ? now : 1;
        }
        clock.seenMs = now ? now : 1;
        return clock;
    }

    void NoteTeleport(Unit const* veklor)
    {
        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(sClockLock);
        TeleportClock& clock = ClockOf(veklor, now);
        clock.lastTeleportMs = now ? now : 1;
        // Vek'nilash lands where Vek'lor stood, by the other torch.
        if (clock.veknilashSpot >= 0)
            clock.veknilashSpot = 1 - clock.veknilashSpot;
    }

    uint32 MsSinceTeleport(Unit const* veklor)
    {
        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(sClockLock);
        TeleportClock const& clock = ClockOf(veklor, now);
        return clock.lastTeleportMs ? getMSTimeDiff(clock.lastTeleportMs, now) : 0xFFFFFFFF;
    }

    // From TWINS_TELEPORT_LEAD_MS before the earliest the next teleport can come until it comes,
    // or until TWINS_TELEPORT_LATE_MS after the latest (one this clock missed): melee wait at
    // most the 10 s the script leaves open, plus a second each side.
    bool TeleportDue(Unit const* veklor)
    {
        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(sClockLock);
        TeleportClock const& clock = ClockOf(veklor, now);
        uint32 const since = getMSTimeDiff(clock.lastTeleportMs ? clock.lastTeleportMs : clock.engagedMs, now);
        return since + TWINS_TELEPORT_LEAD_MS >= TWINS_TELEPORT_MS &&
               since <= TWINS_TELEPORT_MAX_MS + TWINS_TELEPORT_LATE_MS;
    }

    // --- Where a bot may stand ---------------------------------------------

    // Everything the twins ask of where one bot stands, read once per look.
    struct Rules
    {
        TwinRole role = TwinRole::None;
        float botReach = 0.0f;
        float veklorX = 0.0f;
        float veklorY = 0.0f;
        float veklorReach = 0.0f;
        float veknilashX = 0.0f;
        float veknilashY = 0.0f;
        float veknilashReach = 0.0f;
        float veknilashKeep = 0.0f;  // 0: no rule
        bool hold = false;           // a tank standing still while the twins land
        std::vector<Blizzard> const* blizzards = nullptr;
        std::vector<RaidClear::AhnQiraj::LitBlast> blasts;
    };

    bool ReadRules(PlayerbotAI* botAI, Player* bot, TwinsView const& view, Rules& rules)
    {
        Creature* veklor = ObjectAccessor::GetCreature(*bot, view.veklor);
        Creature* veknilash = ObjectAccessor::GetCreature(*bot, view.veknilash);
        if (!veklor || !veknilash)
            return false;
        rules.role = RoleOf(bot, view);
        rules.botReach = bot->GetCombatReach();
        rules.veklorX = veklor->GetPositionX();
        rules.veklorY = veklor->GetPositionY();
        rules.veklorReach = veklor->GetCombatReach();
        rules.veknilashX = veknilash->GetPositionX();
        rules.veknilashY = veknilash->GetPositionY();
        rules.veknilashReach = veknilash->GetCombatReach();
        rules.blizzards = &view.blizzards;
        // Bugs about to blow: the explode walk goes first, but this one mustn't walk back in.
        rules.blasts = RaidClear::AhnQiraj::LitBlasts(botAI);
        // Each tank is where its new twin will look for the nearest: it stays there.
        rules.hold = view.landing && rules.role != TwinRole::None;
        if (rules.role == TwinRole::None)
        {
            // Ranged bots and healers out of his Uppercut; while a teleport is due, everyone else
            // but his tank out of the Burst Vek'lor will cast where Vek'nilash stands now (and
            // whoever he is on stays: he would only follow it).
            if (PlayerbotAI::IsRanged(bot))
                rules.veknilashKeep = VEKNILASH_KEEP;
            else if (veknilash->GetVictim() != bot && TeleportDue(veklor))
                rules.veknilashKeep = VEKNILASH_DUE_KEEP;
        }
        return true;
    }

    // Yards a spot has to spare on the bot's every rule (less than 0: it breaks one), edge to edge.
    float RulesGap(Rules const& rules, float x, float y)
    {
        if (rules.hold)
            return 1000.0f;
        float const toVeklor = std::hypot(x - rules.veklorX, y - rules.veklorY) - rules.veklorReach - rules.botReach;
        float gap = rules.role == TwinRole::VeklorTank
                        ? std::min(toVeklor - VEKLOR_TANK_NEAR, VEKLOR_TANK_FAR - toVeklor)
                        : toVeklor - VEKLOR_KEEP;
        if (rules.veknilashKeep > 0.0f)
        {
            float const toVeknilash = std::hypot(x - rules.veknilashX, y - rules.veknilashY) - rules.veknilashReach -
                                      rules.botReach;
            gap = std::min(gap, toVeknilash - rules.veknilashKeep);
        }
        for (Blizzard const& blizzard : *rules.blizzards)
            gap = std::min(gap, std::hypot(x - blizzard.x, y - blizzard.y) - blizzard.radius - rules.botReach -
                                    BLIZZARD_KEEP);
        for (RaidClear::AhnQiraj::LitBlast const& blast : rules.blasts)
            gap = std::min(gap, std::hypot(x - blast.x, y - blast.y) - blast.radius -
                                    RaidClear::AhnQiraj::EXPLODE_KEEP_AWAY_EXTRA);
        return gap;
    }

    // --- Keeping them apart ------------------------------------------------

    // Centre to centre, how far apart the two have to be for Heal Brother not to reach.
    float ApartKeep(Unit const* veklor, Unit const* veknilash)
    {
        return TWINS_HEAL_RANGE + veklor->GetCombatReach() + veknilash->GetCombatReach() + TWINS_APART_MARGIN;
    }

    // With both torch spots set (the raid guide's): the one Vek'nilash's tank holds him at. Picked
    // once per fight, the one farther from Vek'lor, and swapped at every teleport (Vek'lor lands
    // where Vek'nilash stood, and Vek'nilash by the other torch). Not picked again in between, as
    // the farther one would turn as Vek'lor follows his victim about; only when Vek'lor has come
    // within heal range of it and not of the other.
    Position const* VeknilashSpot(Creature const* veklor, Creature const* veknilash)
    {
        Position const* a = SpotOf(Spot::TwinsA);
        Position const* b = SpotOf(Spot::TwinsB);
        if (!a || !b)
            return nullptr;
        float const toA = veklor->GetExactDist2d(a->GetPositionX(), a->GetPositionY());
        float const toB = veklor->GetExactDist2d(b->GetPositionX(), b->GetPositionY());
        float const keep = ApartKeep(veklor, veknilash);
        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(sClockLock);
        TeleportClock& clock = ClockOf(veklor, now);
        if (clock.veknilashSpot < 0)
            clock.veknilashSpot = toA >= toB ? 0 : 1;
        else if (clock.veknilashSpot == 0 && toA < keep && toB >= keep)
            clock.veknilashSpot = 1;
        else if (clock.veknilashSpot == 1 && toB < keep && toA >= keep)
            clock.veknilashSpot = 0;
        return clock.veknilashSpot == 0 ? a : b;
    }

    // Vek'nilash's torch spot, when there is one and it can be stood on now: not in a Blizzard or
    // a bug's blast ("rc aq40 twins position" would only walk the bot out again). Else the tank
    // keeps him out of heal range as without spots.
    Position const* UsableVeknilashSpot(PlayerbotAI* botAI, Player* bot, TwinsView const& view, Creature const* veklor,
                                        Creature const* veknilash)
    {
        Position const* spot = VeknilashSpot(veklor, veknilash);
        if (!spot)
            return nullptr;
        Rules rules;
        if (!ReadRules(botAI, bot, view, rules) || RulesGap(rules, spot->GetPositionX(), spot->GetPositionY()) < 0.0f)
            return nullptr;
        return spot;
    }

    // The bot is the tank Vek'nilash is on, he can follow it, and he is in Vek'lor's heal range;
    // with the torch spots set, it isn't at his spot.
    bool MustWalkApart(PlayerbotAI* botAI, Player* bot, TwinsView const& view, Creature*& veklor,
                       Creature*& veknilash)
    {
        if (view.landing || RoleOf(bot, view) != TwinRole::VeknilashTank)
            return false;
        veklor = ObjectAccessor::GetCreature(*bot, view.veklor);
        veknilash = ObjectAccessor::GetCreature(*bot, view.veknilash);
        if (!veklor || !veknilash || veknilash->GetVictim() != bot)
            return false;
        // Landing after a teleport he is rooted and stunned for a moment: nothing follows then.
        if (veknilash->HasAura(SPELL_TWIN_TELEPORT_STUN) || veknilash->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED))
            return false;
        if (Position const* spot = UsableVeknilashSpot(botAI, bot, view, veklor, veknilash))
            return bot->GetExactDist2d(spot->GetPositionX(), spot->GetPositionY()) > RaidClear::Spots::TANK_NEAR;
        return veknilash->GetExactDist2d(veklor) < ApartKeep(veklor, veknilash);
    }

    // The bot is the tank Vek'nilash is on and stands at his torch spot: it stays.
    bool HoldsVeknilashSpot(Player* bot, TwinsView const& view)
    {
        if (view.landing || RoleOf(bot, view) != TwinRole::VeknilashTank)
            return false;
        Creature* veklor = ObjectAccessor::GetCreature(*bot, view.veklor);
        Creature* veknilash = ObjectAccessor::GetCreature(*bot, view.veknilash);
        if (!veklor || !veknilash || veknilash->GetVictim() != bot)
            return false;
        Position const* spot = VeknilashSpot(veklor, veknilash);
        return spot && bot->GetExactDist2d(spot->GetPositionX(), spot->GetPositionY()) <= RaidClear::Spots::HOLD;
    }
}

// --- Targets --------------------------------------------------------------

bool RcAq40TwinTargetTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    TwinsView const* view = Twins(bot);
    if (!view)
        return false;
    TwinRole const role = RoleOf(bot, *view);

    if (Pet* pet = bot->GetPet())
    {
        Unit* petVictim = pet->GetVictim();
        if (IsTwin(petVictim, *view) && !PetHitsTwin(pet, petVictim->GetEntry()))
            return true;
    }

    Creature* want = TwinFor(bot, role, *view);
    Unit* current = AI_VALUE(Unit*, "current target");
    if (role != TwinRole::None)
        return want && current != want;
    if (current && current->IsAlive() && IsTwin(current, *view))
        return !HitsTwin(bot, role, current->GetEntry());
    // Nothing to hit (skull on the other twin, which "rc aq40 twin target" keeps it from).
    return want && (!current || !current->IsAlive());
}

bool RcAq40AttackTwinAction::Execute(Event event)
{
    TwinsView const* view = Twins(bot);
    if (!view)
        return false;

    if (Pet* pet = bot->GetPet())
    {
        if (IsBadPetTarget(pet->GetVictim()))
        {
            pet->AttackStop();
            pet->InterruptNonMeleeSpells(false);
        }
    }

    Creature* want = TwinFor(bot, RoleOf(bot, *view), *view);
    // (Playerbots doesn't send the pet along: its own AI follows the owner's target, which
    // "rc aq40 twins pet" stops where it shouldn't.)
    if (want && want != AI_VALUE(Unit*, "current target") && bot->IsValidAttackTarget(want) && Attack(want))
        return true;
    // No twin to go for (a healer), or it can't be attacked from here: off the bad one at least.
    return RcLeaveTargetAction::Execute(event);
}

bool RcAq40AttackTwinAction::IsBadTarget(Unit const* unit)
{
    TwinsView const* view = Twins(bot);
    return view && IsTwin(unit, *view) && !HitsTwin(bot, RoleOf(bot, *view), unit->GetEntry());
}

bool RcAq40AttackTwinAction::IsBadPetTarget(Unit const* unit)
{
    TwinsView const* view = Twins(bot);
    return view && IsTwin(unit, *view) && !PetHitsTwin(bot->GetPet(), unit->GetEntry());
}

float RcAq40TwinTargetMultiplier::GetValue(Action* action)
{
    if (!action || dynamic_cast<RcAq40AttackTwinAction*>(action) || !bot->IsInCombat())
        return 1.0f;
    bool const attack = dynamic_cast<AttackAction*>(action) != nullptr;
    if (!attack && !dynamic_cast<CastSpellAction*>(action) && !dynamic_cast<MovementAction*>(action))
        return 1.0f;
    TwinsView const* view = Twins(bot);
    if (!view)
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    TwinRole const role = RoleOf(bot, *view);
    if (IsTwin(target, *view))
        return HitsTwin(bot, role, target->GetEntry()) ? 1.0f : 0.0f;
    // A twin's tank picks nothing else: the bugs are the other tanks'.
    if (role != TwinRole::None && attack && bot->IsValidAttackTarget(target))
        return 0.0f;
    return 1.0f;
}

// --- Where to stand -------------------------------------------------------

bool RcAq40TwinsPositionTrigger::IsActive()
{
    // A bot that is feared or rooted isn't going anywhere.
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    TwinsView const* view = Twins(bot);
    Rules rules;
    if (!view || !ReadRules(botAI, bot, *view, rules))
        return false;
    return RulesGap(rules, bot->GetPositionX(), bot->GetPositionY()) < 0.0f;
}

bool RcAq40TwinsPositionAction::Execute(Event /*event*/)
{
    TwinsView const* view = Twins(bot);
    Rules rules;
    if (!view || !ReadRules(botAI, bot, *view, rules))
        return false;
    // A copy: the view is looked up again while the walk is planned.
    std::vector<Blizzard> const blizzards = view->blizzards;
    rules.blizzards = &blizzards;
    if (RulesGap(rules, bot->GetPositionX(), bot->GetPositionY()) >= 0.0f)
        return false;
    auto const gapAt = [&rules](float x, float y, float /*z*/) { return RulesGap(rules, x, y); };
    return _walk.Go(gapAt, 0.0f, TWINS_SPOT, TWINS_WORTH_IT, TWINS_SEARCH);
}

float RcAq40TwinsHoldMultiplier::GetValue(Action* action)
{
    // Pure walks: a spell with a reach of its own (Judgement) stops at its range.
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action))
        return 1.0f;
    TwinsView const* view = Twins(bot);
    if (!view || !bot->IsInCombat())
        return 1.0f;
    TwinRole const role = RoleOf(bot, *view);
    // While they land, each tank stands where its new twin will find it nearest.
    if (view->landing && role != TwinRole::None)
        return 0.0f;
    // Vek'nilash's tank at his torch spot: only the fight's own walks (out of a Blizzard or a bug)
    // take it off.
    if (HoldsVeknilashSpot(bot, *view) && !RaidClear::Spots::IsDodge(action))
        return 0.0f;
    Unit* target = RaidClear::AhnQiraj::WalkTarget(action, bot);
    if (!target || !IsTwin(target, *view))
        return 1.0f;

    if (target->GetGUID() == view->veklor)
    {
        // His tank holds its band by "rc aq40 twins position"; a ranged bot's walk into casting
        // range stops far outside his Burst. Anyone else has no business walking up to him.
        return role != TwinRole::VeklorTank && PlayerbotAI::IsRanged(bot) ? 1.0f : 0.0f;
    }
    // Melee wait out a teleport that is due a few yards back from Vek'nilash.
    if (role == TwinRole::None && !PlayerbotAI::IsRanged(bot) && target->GetVictim() != bot)
    {
        Creature* veklor = ObjectAccessor::GetCreature(*bot, view->veklor);
        if (veklor && TeleportDue(veklor))
            return 0.0f;
    }
    return 1.0f;
}

// --- Keeping them apart ---------------------------------------------------

bool RcAq40TwinsApartTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    TwinsView const* view = Twins(bot);
    Creature* veklor = nullptr;
    Creature* veknilash = nullptr;
    return view && MustWalkApart(botAI, bot, *view, veklor, veknilash);
}

bool RcAq40TwinsApartAction::Execute(Event /*event*/)
{
    TwinsView const* view = Twins(bot);
    Creature* veklor = nullptr;
    Creature* veknilash = nullptr;
    if (!view || !MustWalkApart(botAI, bot, *view, veklor, veknilash))
        return false;

    if (Position const* spot = UsableVeknilashSpot(botAI, bot, *view, veklor, veknilash))
    {
        // He follows it there; a walk already on its way isn't sent again.
        if (RaidClear::Spots::OnTheWay(bot, *spot))
            return true;
        AI_VALUE(LastMovement&, "last movement").clear();
        return MoveTo(bot->GetMapId(), spot->GetPositionX(), spot->GetPositionY(), spot->GetPositionZ(), false, false,
                      false, false, MovementPriority::MOVEMENT_FORCED);
    }

    // He follows his victim and stops a melee reach short of it, in the worst case on Vek'lor's
    // side: a spot is as good as that much short of it.
    float const keep = ApartKeep(veklor, veknilash);
    float const shortBy = veknilash->GetCombatReach() + bot->GetCombatReach();
    float const veklorX = veklor->GetPositionX();
    float const veklorY = veklor->GetPositionY();
    auto const gapAt = [keep, shortBy, veklorX, veklorY](float x, float y, float /*z*/)
    { return std::hypot(x - veklorX, y - veklorY) - shortBy - keep; };
    // Several walks of up to TWINS_SEARCH + 6 yd each, when one is not enough.
    return _walk.Go(gapAt, 0.0f, TWINS_APART_EXTRA, TWINS_WORTH_IT, TWINS_SEARCH + 6.0f);
}

// --- Pets -----------------------------------------------------------------

namespace
{
    // Pets kept by their owner's side, with the react state each had, by pet. Shared and locked
    // like the teleport clock.
    struct ParkedPet
    {
        ObjectGuid owner;
        ReactStates react;
    };
    std::mutex sParkLock;
    std::map<ObjectGuid, ParkedPet> sParked;

    bool IsParked(Unit const* pet)
    {
        std::lock_guard<std::mutex> guard(sParkLock);
        return sParked.count(pet->GetGUID()) != 0;
    }

    // Each twin attacks the nearest unit, pets too, when it lands after a teleport. A pet still on
    // Vek'nilash when Vek'lor takes his place would be the one Vek'lor follows back across the
    // room after him, and a pet on Vek'lor hurts nothing there. Such a pet stays by its owner.
    bool PetStaysBack(Player* bot, Unit const* pet)
    {
        TwinsView const* view = Twins(bot);
        if (!view)
            return false;
        TwinRole const role = RoleOf(bot, *view);
        bool const ownerOnVeklor = HitsTwin(bot, role, NPC_VEKLOR);
        if (ownerOnVeklor && !IsCasterPet(pet))
            return true;
        // Through the landing too: a pet let go at the teleport is a unit the twin can find nearer
        // than its tank.
        Creature* veklor = ObjectAccessor::GetCreature(*bot, view->veklor);
        return !ownerOnVeklor && veklor && (view->landing || TeleportDue(veklor));
    }
}

bool RcAq40TwinsPetTrigger::IsActive()
{
    Pet* pet = bot->GetPet();
    if (!pet || !pet->IsAlive() || !pet->GetCharmInfo())
        return false;
    return PetStaysBack(bot, pet) != IsParked(pet);
}

bool RcAq40TwinsPetAction::Execute(Event /*event*/)
{
    Pet* pet = bot->GetPet();
    if (!pet || !pet->IsAlive() || !pet->GetCharmInfo())
        return false;
    bool const stay = PetStaysBack(bot, pet);
    std::lock_guard<std::mutex> guard(sParkLock);
    auto it = sParked.find(pet->GetGUID());
    if (stay == (it != sParked.end()))
        return false;

    if (!stay)
    {
        // Back to what it was: a defensive pet goes for its owner's target by itself.
        pet->SetReactState(it->second.react);
        sParked.erase(it);
        return true;
    }

    // As the pet bar's Follow, and passive so that it doesn't pick its owner's target again.
    sParked[pet->GetGUID()] = { bot->GetGUID(), pet->GetReactState() };
    pet->SetReactState(REACT_PASSIVE);
    pet->AttackStop();
    pet->InterruptNonMeleeSpells(false);
    pet->GetMotionMaster()->MoveFollow(bot, PET_FOLLOW_DIST, pet->GetFollowAngle());
    pet->GetCharmInfo()->SetCommandState(COMMAND_FOLLOW);
    pet->GetCharmInfo()->SetIsCommandAttack(false);
    return true;
}

void RaidClear::TempleOfAhnQiraj::SweepParkedPets()
{
    // The trigger above only runs in combat: a pet parked when the twins died, the raid wiped or
    // its hunter feigned death would stay passive, and playerbots never sends a passive pet in.
    std::lock_guard<std::mutex> guard(sParkLock);
    for (auto it = sParked.begin(); it != sParked.end();)
    {
        Player* owner = ObjectAccessor::FindPlayer(it->second.owner);
        Pet* pet = owner ? owner->GetPet() : nullptr;
        if (!pet || pet->GetGUID() != it->first)
        {
            it = sParked.erase(it);
            continue;
        }
        if (owner->IsAlive() && owner->IsInCombat() && PetStaysBack(owner, pet))
        {
            ++it;
            continue;
        }
        pet->SetReactState(it->second.react);
        it = sParked.erase(it);
    }
}

bool RaidClear::TempleOfAhnQiraj::VeklorKeepOut(Player* bot, float& x, float& y, float& reach, float& keep)
{
    TwinsView const* view = Twins(bot);
    Creature* veklor = view ? ObjectAccessor::GetCreature(*bot, view->veklor) : nullptr;
    if (!veklor)
        return false;
    x = veklor->GetPositionX();
    y = veklor->GetPositionY();
    reach = veklor->GetCombatReach();
    keep = RoleOf(bot, *view) == TwinRole::VeklorTank ? VEKLOR_TANK_NEAR : VEKLOR_KEEP;
    return true;
}

// --- Vek'lor's threat -----------------------------------------------------

namespace
{
    // Holy spells that reach Vek'lor from his tank's band, by playerbots name: playerbots' tank
    // rotation has Judgement for him and nothing else out of melee.
    constexpr std::array<char const*, 2> VEKLOR_TANK_SPELLS = { "avenger's shield", "exorcism" };

    char const* VeklorTankSpell(PlayerbotAI* botAI, Player* bot, Unit* veklor)
    {
        for (char const* name : VEKLOR_TANK_SPELLS)
        {
            uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get();
            SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
            if (!info || !bot->HasSpell(spellId) || !bot->IsWithinCombatRange(veklor, info->GetMaxRange(false, bot)))
                continue;
            if (botAI->CanCastSpell(name, veklor) && bot->IsWithinLOSInMap(veklor))
                return name;
        }
        return nullptr;
    }
}

bool RcAq40VeklorTankThreatTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    TwinsView const* view = Twins(bot);
    if (!view || RoleOf(bot, *view) != TwinRole::VeklorTank)
        return false;
    Creature* veklor = ObjectAccessor::GetCreature(*bot, view->veklor);
    return veklor && VeklorTankSpell(botAI, bot, veklor) != nullptr;
}

bool RcAq40VeklorTankThreatAction::Execute(Event /*event*/)
{
    TwinsView const* view = Twins(bot);
    if (!view || RoleOf(bot, *view) != TwinRole::VeklorTank)
        return false;
    Creature* veklor = ObjectAccessor::GetCreature(*bot, view->veklor);
    char const* spell = veklor ? VeklorTankSpell(botAI, bot, veklor) : nullptr;
    return spell && botAI->CastSpell(spell, veklor);
}

float RcAq40VeklorThreatMultiplier::GetValue(Action* action)
{
    if (!action || action->getThreatType() == Action::ActionThreatType::None || !bot->IsInCombat())
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || target->GetEntry() != NPC_VEKLOR)
        return 1.0f;
    TwinsView const* view = Twins(bot);
    if (!view || target->GetGUID() != view->veklor || bot->GetGUID() == view->veklorTank)
        return 1.0f;
    if (!RaidClear::Tanks::HeldByThreatCap(botAI, action))
        return 1.0f;
    // Under whoever he is on, his tank or not: what matters is that he doesn't turn. Not while
    // that is the bot itself, or nobody has built threat to speak of yet.
    Unit* victim = target->GetVictim();
    if (!victim || victim == bot)
        return 1.0f;
    float const victimThreat = target->GetThreatMgr().GetThreat(victim);
    if (victimThreat < VEKLOR_THREAT_FLOOR)
        return 1.0f;
    return target->GetThreatMgr().GetThreat(bot) >= victimThreat * VEKLOR_THREAT_SHARE ? 0.0f : 1.0f;
}

// --- The server side --------------------------------------------------------

// The teleport clock is set by Vek'lor's cast itself. A bot in the middle of a cast doesn't run its
// engine until the cast ends: at a teleport the bots round both twins think at once, and when he
// sets a bug to explode the bots round that bug drop their casts.
class RaidClearTwinsSpellScript : public AllSpellScript
{
public:
    RaidClearTwinsSpellScript() : AllSpellScript("RaidClearTwinsSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || caster->GetEntry() != NPC_VEKLOR || caster->GetMapId() != MAP_ID)
            return;
        bool const teleport = spellInfo->Id == SPELL_TWIN_TELEPORT;
        if (!teleport && spellInfo->Id != RaidClear::AhnQiraj::SPELL_EXPLODE_BUG)
            return;
        if (teleport)
            NoteTeleport(caster);
        if (!RaidClear::GetConfig().IsRaidEnabled(MAP_ID))
            return;

        // What the bots have to get away from or think about: both twins after a teleport, the
        // bugs that are about to blow (the instant cast has already landed its aura).
        std::vector<Unit*> around;
        float reach = 0.0f;
        if (teleport)
        {
            around.push_back(caster);
            if (Creature* veknilash = caster->FindNearestCreature(NPC_VEKNILASH, 250.0f))
                around.push_back(veknilash);
            reach = 25.0f;
        }
        else
        {
            for (uint32 const entry : { uint32(NPC_QIRAJI_SCARAB), uint32(NPC_QIRAJI_SCORPION) })
            {
                std::list<Creature*> bugs;
                caster->GetCreatureListWithEntryInGrid(bugs, entry, 100.0f);
                for (Creature* bug : bugs)
                    if (bug->IsAlive() && bug->HasAura(RaidClear::AhnQiraj::SPELL_EXPLODE_BUG))
                        around.push_back(bug);
            }
            reach = RaidClear::AhnQiraj::ExplodeBugRadius() + RaidClear::AhnQiraj::EXPLODE_KEEP_AWAY_EXTRA;
        }
        if (around.empty())
            return;

        Map::PlayerList const& players = caster->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive())
                continue;
            if (std::none_of(around.begin(), around.end(),
                             [player, reach](Unit const* unit) { return player->GetExactDist(unit) < reach; }))
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->HasStrategy("rc aq40", BOT_STATE_COMBAT))
                continue;
            if (!teleport)
                botAI->RequestSpellInterrupt();
            botAI->ResetActionDuration();
        }
    }
};

void RaidClear::TempleOfAhnQiraj::AddTwinEmperorsScripts()
{
    new RaidClearTwinsSpellScript();
}
