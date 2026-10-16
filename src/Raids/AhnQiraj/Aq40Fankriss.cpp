/*
 * mod-raid-clear: Temple of Ahn'Qiraj, Fankriss the Unyielding's Mortal Wound. See AqRaidClear.h.
 * His worms are the kill-order and tank tables in AqRaidClear.cpp.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"

#include "Creature.h"
#include "Group.h"
#include "Playerbots.h"
#include "SpellAuras.h"

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    Creature* FightingFankriss(Player* bot) { return RaidClear::FightingCreature(bot, { NPC_FANKRISS }); }

    uint8 MortalWounds(Unit const* unit)
    {
        Aura const* aura = unit ? unit->GetAura(SPELL_MORTAL_WOUND) : nullptr;
        return aura ? aura->GetStackAmount() : 0;
    }

    // A mob other than Fankriss is on this tank: an add it holds (a worm, a hatchling).
    bool HoldsAnAdd(Player* tank, Unit const* fankriss)
    {
        for (Unit* attacker : tank->getAttackers())
            if (attacker != fankriss && attacker->IsAlive() && attacker->GetVictim() == tank)
                return true;
        return false;
    }

    // His two tanks: the main tank, and the first living bot tank after it in group order that
    // isn't holding an add (worms and hatchlings stay with whoever has them).
    bool HoldsFankriss(Player* tank, Creature* fankriss)
    {
        if (!tank || !PlayerbotAI::IsTank(tank))
            return false;
        if (fankriss->GetVictim() == tank)
            return true;
        Group* group = tank->GetGroup();
        if (!group)
            return false;
        Player* mainTank = RaidClear::Tanks::ActingMainTank(group);
        if (tank == mainTank)
            return true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == mainTank || !member->IsAlive() || !member->IsInMap(fankriss) ||
                !PlayerbotAI::IsTank(member) || !GET_PLAYERBOT_AI(member) || HoldsAnAdd(member, fankriss))
                continue;
            return member == tank;
        }
        return false;
    }

    // The tank he is on, when it is a tank: the swap is between tanks.
    Player* TankOnFankriss(Creature* fankriss)
    {
        Unit* victim = fankriss->GetVictim();
        Player* tank = victim ? victim->ToPlayer() : nullptr;
        return tank && PlayerbotAI::IsTank(tank) ? tank : nullptr;
    }
}

bool RcAq40MortalWoundTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !PlayerbotAI::IsTank(bot) || bot->HasAura(SPELL_MORTAL_WOUND))
        return false;

    Creature* fankriss = FightingFankriss(bot);
    if (!fankriss || RaidClear::Tanks::CantBeTaunted(fankriss) || !HoldsFankriss(bot, fankriss))
        return false;

    Player* tank = TankOnFankriss(fankriss);
    return tank && tank != bot && MortalWounds(tank) >= MORTAL_WOUND_SWAP_STACKS &&
           RaidClear::Tanks::CanTaunt(botAI, fankriss);
}

bool RcAq40MortalWoundTauntAction::Execute(Event /*event*/)
{
    Creature* fankriss = FightingFankriss(bot);
    return fankriss && RaidClear::Tanks::Taunt(botAI, fankriss);
}

float RcAq40MortalWoundMultiplier::GetValue(Action* action)
{
    // Playerbots' main tank taunts whatever it is fighting back the moment it is on someone else,
    // the other tank included: only the swap above takes him off a tank.
    if (!action || !PlayerbotAI::IsTank(bot) || !RaidClear::Tanks::IsTauntAction(action))
        return 1.0f;
    Creature* fankriss = FightingFankriss(bot);
    if (!fankriss || action->GetTarget() != fankriss)
        return 1.0f;
    Player* tank = TankOnFankriss(fankriss);
    return tank && tank != bot ? 0.0f : 1.0f;
}
