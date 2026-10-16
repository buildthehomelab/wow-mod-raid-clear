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

#include <string>
#include <utility>

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
    std::pair<Player*, Player*> FankrissTanks(Player* bot, Creature* fankriss)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return { nullptr, nullptr };
        Player* mainTank = RaidClear::Tanks::ActingMainTank(group);
        if (mainTank && (!mainTank->IsAlive() || !mainTank->IsInMap(fankriss)))
            mainTank = nullptr;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == mainTank || !member->IsAlive() || !member->IsInMap(fankriss) ||
                !PlayerbotAI::IsTank(member) || !GET_PLAYERBOT_AI(member) || HoldsAnAdd(member, fankriss))
                continue;
            return { mainTank, member };
        }
        return { mainTank, nullptr };
    }

    // The tank he is on, when it is a tank: the swap is between tanks.
    Player* TankOnFankriss(Creature* fankriss)
    {
        Unit* victim = fankriss->GetVictim();
        Player* tank = victim ? victim->ToPlayer() : nullptr;
        return tank && PlayerbotAI::IsTank(tank) ? tank : nullptr;
    }

    // The one of his two tanks that should taunt him now, or nullptr. Off the other one once that
    // has MORTAL_WOUND_SWAP_STACKS and the taker has none left; off any other tank (his main
    // tank died and a worm's tank caught him) at once, by whichever of the two has fewer stacks.
    Player* SwapTaker(Player* bot, Creature* fankriss)
    {
        Player* onHim = TankOnFankriss(fankriss);
        if (!onHim)
            return nullptr;  // on a damage dealer: playerbots' own taunts go out
        auto const [mainTank, second] = FankrissTanks(bot, fankriss);
        if (onHim == mainTank || onHim == second)
        {
            Player* other = onHim == mainTank ? second : mainTank;
            return other && MortalWounds(onHim) >= MORTAL_WOUND_SWAP_STACKS && !other->HasAura(SPELL_MORTAL_WOUND)
                       ? other
                       : nullptr;
        }
        if (!mainTank || !second)
            return mainTank ? mainTank : second;
        return MortalWounds(second) < MortalWounds(mainTank) ? second : mainTank;
    }

    // One of playerbots' taunts that would take him: at him, Righteous Defense on the tank he is
    // on, or a shout that takes everything round the bot (10 yd).
    bool TauntsFankriss(Player* bot, Action* action, Creature* fankriss)
    {
        Unit* target = action->GetTarget();
        if (target == fankriss || (target && target == fankriss->GetVictim()))
            return true;
        std::string const name = action->getName();
        return (name == "challenging shout" || name == "challenging roar") &&
               bot->IsWithinDistInMap(fankriss, CHALLENGING_TAUNT_REACH);
    }
}

bool RcAq40MortalWoundTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !PlayerbotAI::IsTank(bot))
        return false;

    Creature* fankriss = FightingFankriss(bot);
    if (!fankriss || fankriss->GetVictim() == bot || RaidClear::Tanks::CantBeTaunted(fankriss))
        return false;
    return SwapTaker(bot, fankriss) == bot && RaidClear::Tanks::CanTaunt(botAI, fankriss);
}

bool RcAq40MortalWoundTauntAction::Execute(Event /*event*/)
{
    Creature* fankriss = FightingFankriss(bot);
    return fankriss && RaidClear::Tanks::Taunt(botAI, fankriss);
}

float RcAq40MortalWoundMultiplier::GetValue(Action* action)
{
    // Playerbots' main tank taunts whatever it is fighting back the moment it is on someone else,
    // the other tank included: only the swap above takes him off one of his two tanks.
    if (!action || !PlayerbotAI::IsTank(bot) || !RaidClear::Tanks::IsTauntAction(action))
        return 1.0f;
    Creature* fankriss = FightingFankriss(bot);
    if (!fankriss || !TauntsFankriss(bot, action, fankriss))
        return 1.0f;
    Player* onHim = TankOnFankriss(fankriss);
    if (!onHim || onHim == bot)
        return 1.0f;
    auto const [mainTank, second] = FankrissTanks(bot, fankriss);
    return onHim == mainTank || onHim == second ? 0.0f : 1.0f;
}
