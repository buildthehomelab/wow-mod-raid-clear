/*
 * mod-raid-clear: who starts a fight, and who stays out of melee. See RaidDiscipline.h.
 *
 * Released under the MIT License.
 */

#include "RaidDiscipline.h"

#include "RaidClearConfig.h"
#include "Common/TankRoles.h"

#include "Creature.h"
#include "GenericSpellActions.h"
#include "Group.h"
#include "Map.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <string>

namespace RaidClear::Discipline
{
    namespace
    {
        constexpr uint32 SPELL_TRANQUILIZING_SHOT = 19801;
        constexpr uint8 TRANQUILIZING_SHOT_LEVEL = 60;
        // Judgement's range when the server's spell data can't be asked.
        constexpr float JUDGEMENT_RANGE = 10.0f;

        // Somebody in the raid, here and alive, whose job the pull is.
        bool RaidHasTank(Player* bot)
        {
            Group* group = bot->GetGroup();
            if (!group)
                return false;
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && member != bot && member->IsAlive() && member->IsInMap(bot) && PlayerbotAI::IsTank(member))
                    return true;
            }
            return false;
        }

        // An enemy the raid is fighting right now, other than `except`. Not one that is only
        // evading (a mob stuck out of reach would keep a boss from ever being pulled), and not
        // the adds that never stop coming: Broodlord is pulled with the Suppression Room's
        // whelps still hatching.
        bool FightingSomethingElse(PlayerbotAI* botAI, Unit const* except)
        {
            for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
            {
                Unit* unit = botAI->GetUnit(guid);
                if (!unit || unit == except || !unit->IsAlive() || !unit->IsInCombat() || Tanks::IsEndlessAdd(unit))
                    continue;
                Creature const* creature = unit->ToCreature();
                if (creature && creature->IsEvadingAttacks())
                    continue;
                return true;
            }
            return false;
        }

        // The nearest enemy in sight that the raid is fighting and this bot may hit.
        Unit* NearestInFight(PlayerbotAI* botAI, Player* bot, Unit const* except)
        {
            Unit* nearest = nullptr;
            float nearestDist = 0.0f;
            for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
            {
                Unit* unit = botAI->GetUnit(guid);
                if (!unit || unit == except || !unit->IsAlive() || !unit->IsInCombat() ||
                    !bot->IsValidAttackTarget(unit) || MustWaitFor(botAI, bot, unit))
                    continue;
                Creature const* creature = unit->ToCreature();
                if (creature && creature->IsEvadingAttacks())
                    continue;
                // Not an add another tank has been told to take (a Death Talon Captain).
                if (Tanks::IsOwnTankAdd(unit) && PlayerbotAI::IsTank(bot))
                    continue;
                float const dist = bot->GetDistance(unit);
                if ((nearest && dist >= nearestDist) || !bot->IsWithinLOSInMap(unit))
                    continue;
                nearest = unit;
                nearestDist = dist;
            }
            return nearest;
        }

        // What playerbots would hand this bot next.
        Unit* NextTarget(PlayerbotAI* botAI, Player* bot)
        {
            return botAI->GetAiObjectContext()
                ->GetValue<Unit*>(PlayerbotAI::IsTank(bot) ? "tank target" : "dps target")
                ->Get();
        }
    }

    bool IsUnpulled(Unit const* unit)
    {
        Creature const* creature = unit ? unit->ToCreature() : nullptr;
        // A database spawn: what the fight itself summons (adds, totems, a boss a module puts
        // there) is never "not pulled yet".
        return creature && creature->IsInWorld() && creature->IsAlive() && creature->GetSpawnId() &&
               !creature->IsSummon() && !creature->IsInCombat() && !creature->GetVictim() &&
               !creature->HasReactState(REACT_PASSIVE) && !creature->IsPet() && !creature->IsTotem() &&
               !creature->IsTrigger() && !creature->IsCivilian() && !creature->GetCharmerOrOwnerGUID().IsPlayer() &&
               creature->GetCreatureType() != CREATURE_TYPE_CRITTER && !creature->IsImmuneToPC() &&
               !creature->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) && !creature->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
    }

    bool MustWaitFor(PlayerbotAI* botAI, Player* bot, Unit* unit)
    {
        Config const& config = GetConfig();
        if (!config.enable || !config.pullDiscipline || !unit || unit == bot || !IsUnpulled(unit))
            return false;
        Map* map = bot->GetMap();
        if (!map || !map->IsRaid() || !bot->IsValidAttackTarget(unit))
            return false;

        if (PlayerbotAI::IsTank(bot))
        {
            // The pull is the tank's, but not a boss on top of the fight it is already in.
            Creature const* creature = unit->ToCreature();
            if (!creature->IsDungeonBoss() && !creature->isWorldBoss())
                return false;
            return FightingSomethingElse(botAI, unit);
        }
        return RaidHasTank(bot);
    }

    void TeachRaidSpells(Player* bot)
    {
        if (!GetConfig().teachTranquilizingShot || !bot->IsClass(CLASS_HUNTER) ||
            bot->GetLevel() < TRANQUILIZING_SHOT_LEVEL || bot->HasSpell(SPELL_TRANQUILIZING_SHOT))
            return;
        bot->learnSpell(SPELL_TRANQUILIZING_SHOT);
    }
}

using namespace RaidClear;
using namespace RaidClear::Discipline;

void RaidClearDisciplineStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode("rc unpulled target", { NextAction("rc leave unpulled target", ACTION_RAID + 3) }));
}

void RaidClearDisciplineStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcPullDisciplineMultiplier(botAI));
    multipliers.push_back(new RcHealerStaysBackMultiplier(botAI));
}

// Playerbots puts the group's skull among every bot's "attackers" whether it is fighting or not.
// The skull itself is still handed out before anything is skipped ("dps assist" asks for it by
// name), which is what RcPullDisciplineMultiplier is for.
void RaidClearDisciplineStrategy::AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type)
{
    // A tank picks what it pulls.
    if (type == TargetValueExclusionType::Tank)
        return;

    Player* bot = botAI->GetBot();
    if (PlayerbotAI::IsTank(bot))
        return;
    for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        if (MustWaitFor(botAI, bot, botAI->GetUnit(guid)))
            exclusions.insert(guid);
}

bool RcUnpulledTargetTrigger::IsActive()
{
    if (!bot->IsAlive())
        return false;
    Unit* current = AI_VALUE(Unit*, "current target");
    if (MustWaitFor(botAI, bot, current))
        return true;
    Pet* pet = bot->GetPet();
    if (pet && MustWaitFor(botAI, bot, pet->GetVictim()))
        return true;

    // Nothing to hit, and all playerbots would offer is a target to wait for (the skull is
    // handed out before anything is skipped, so "dps assist" keeps asking for it and keeps
    // being refused): the bot would stand idle next to a fight. It is given something from it.
    if (current && current->IsAlive())
        return false;
    return MustWaitFor(botAI, bot, NextTarget(botAI, bot)) && NearestInFight(botAI, bot, nullptr);
}

bool RcLeaveUnpulledTargetAction::Execute(Event /*event*/)
{
    if (Pet* pet = bot->GetPet())
    {
        if (MustWaitFor(botAI, bot, pet->GetVictim()))
        {
            pet->AttackStop();
            pet->InterruptNonMeleeSpells(false);
        }
    }

    Unit* current = AI_VALUE(Unit*, "current target");
    bool const waiting = MustWaitFor(botAI, bot, current);
    // On something it may hit: nothing to change.
    if (!waiting && current && current->IsAlive())
        return false;

    // What playerbots would pick now, when that is something in the fight.
    Unit* next = NextTarget(botAI, bot);
    if (next && next != current && next->IsAlive() && !MustWaitFor(botAI, bot, next) && Attack(next))
        return true;

    // That can be the target to wait for again (the skull is picked before anything is skipped):
    // then the nearest enemy the raid is fighting.
    Unit* nearest = NearestInFight(botAI, bot, current);
    if (nearest && Attack(nearest))
        return true;
    if (!waiting)
        return false;

    // Nothing to hit yet: let go, and playerbots picks again once the pull has happened.
    context->GetValue<Unit*>("current target")->Set(nullptr);
    bot->SetTarget(ObjectGuid::Empty);
    bot->SetSelection(ObjectGuid::Empty);
    bot->AttackStop();
    bot->InterruptNonMeleeSpells(false);
    return true;
}

float RcPullDisciplineMultiplier::GetValue(Action* action)
{
    if (!action || !GetConfig().pullDiscipline)
        return 1.0f;
    // Attacking it, casting on it, walking or charging up to it.
    if (!dynamic_cast<AttackAction*>(action) && !dynamic_cast<CastSpellAction*>(action) &&
        !dynamic_cast<ReachTargetAction*>(action))
        return 1.0f;
    // The way off such a target.
    if (dynamic_cast<RcLeaveUnpulledTargetAction*>(action))
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;
    return MustWaitFor(botAI, bot, target) ? 0.0f : 1.0f;
}

float RcHealerStaysBackMultiplier::GetValue(Action* action)
{
    if (!action || !GetConfig().healersStayBack || !bot->IsClass(CLASS_PALADIN) || !PlayerbotAI::IsHeal(bot))
        return 1.0f;
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast)
        return 1.0f;
    std::string const spell = cast->getSpell();
    if (spell.rfind("judgement", 0) != 0)
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    // In range from here: nothing to walk for. A big boss's own reach counts, as it does for the
    // cast itself.
    uint32 const spellId = AI_VALUE2(uint32, "spell id", spell);
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    float const range = info ? info->GetMaxRange(false, bot) : Discipline::JUDGEMENT_RANGE;
    return bot->IsWithinCombatRange(target, range) ? 1.0f : 0.0f;
}
