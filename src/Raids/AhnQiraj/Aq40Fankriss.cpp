/*
 * mod-raid-clear: Temple of Ahn'Qiraj, Fankriss the Unyielding's Mortal Wound. See AqRaidClear.h.
 * His worms are the kill-order and tank tables in AqRaidClear.cpp.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"

#include "Creature.h"
#include "GenericSpellActions.h"
#include "Group.h"
#include "Playerbots.h"
#include "SpellAuras.h"
#include "ThreatManager.h"

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

    // A Spawn of Fankriss is on this tank. Hatchlings don't count: twelve come every 25-55 s and
    // any tank with an area attack ends up with a few.
    bool HoldsAWorm(Player* tank)
    {
        for (Unit* attacker : tank->getAttackers())
            if (attacker->GetEntry() == NPC_SPAWN_OF_FANKRISS && attacker->IsAlive() && attacker->GetVictim() == tank)
                return true;
        return false;
    }

    // His two tanks: the main tank, and the off-tank he is on (unless it has a worm to hold), else
    // the first living bot tank after the main tank in group order that isn't holding a worm.
    std::pair<Player*, Player*> FankrissTanks(Player* bot, Creature* fankriss)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return { nullptr, nullptr };
        Player* mainTank = RaidClear::Tanks::ActingMainTank(group);
        if (mainTank && (!mainTank->IsAlive() || !mainTank->IsInMap(fankriss)))
            mainTank = nullptr;
        Unit* victim = fankriss->GetVictim();
        Player* onHim = victim ? victim->ToPlayer() : nullptr;
        if (onHim && onHim != mainTank && PlayerbotAI::IsTank(onHim) && GET_PLAYERBOT_AI(onHim) && !HoldsAWorm(onHim))
            return { mainTank, onHim };
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == mainTank || !member->IsAlive() || !member->IsInMap(fankriss) ||
                !PlayerbotAI::IsTank(member) || !GET_PLAYERBOT_AI(member) || HoldsAWorm(member))
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
    // tank died and a worm's tank caught him, or the tank he is on went for a worm) at once, by
    // whichever of the two has fewer stacks.
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
    if (!action || !PlayerbotAI::IsTank(bot))
        return 1.0f;
    bool const taunt = RaidClear::Tanks::IsTauntAction(action);
    if (!taunt && !dynamic_cast<CastSpellAction*>(action))
        return 1.0f;
    Creature* fankriss = FightingFankriss(bot);
    if (!fankriss)
        return 1.0f;
    Player* onHim = TankOnFankriss(fankriss);
    if (!onHim || onHim == bot)
        return 1.0f;
    auto const [mainTank, second] = FankrissTanks(bot, fankriss);
    if (onHim != mainTank && onHim != second)
        return 1.0f;

    // Playerbots' main tank taunts whatever it is fighting back the moment it is on someone else,
    // the other tank included: only the swap above takes him off one of his two tanks.
    if (taunt)
        return TauntsFankriss(bot, action, fankriss) ? 0.0f : 1.0f;

    // And the one waiting its turn keeps hitting him (playerbots' main tank stays on its target):
    // its spells stop short of the threat that would take him back with its stacks. Swings carry
    // on. Area threat (Consecration, Thunder Clap, Swipe) counts once it is at his feet.
    if (bot != mainTank && bot != second)
        return 1.0f;
    if (action->getThreatType() == Action::ActionThreatType::Aoe)
    {
        if (!bot->IsWithinCombatRange(fankriss, CHALLENGING_TAUNT_REACH))
            return 1.0f;
    }
    else if (action->GetTarget() != fankriss || !RaidClear::Tanks::HeldByThreatCap(botAI, action))
        return 1.0f;
    ThreatManager const& threat = fankriss->GetThreatMgr();
    return threat.GetThreat(bot) >= threat.GetThreat(onHim) * MORTAL_WOUND_WAITING_SHARE ? 0.0f : 1.0f;
}
