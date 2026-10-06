/*
 * mod-raid-clear: main tank / off-tank split for bot raids.
 *
 * Out of the box every playerbots tank does the same thing: grab whatever isn't attacking it.
 * In a raid with two or more tanks that means the off-tanks taunt the main tank's mob and every
 * mob ends up stacked in one spot. Here:
 *
 *   - Each raid group on a raid map gets an explicit main tank (the group's Main Tank flag), if
 *     it doesn't have one yet: the tank playerbots already treats as main (first living tank in
 *     group order, which `.botraid sort` puts first). With the flag set, playerbots' main tank
 *     sticks to its own target, and mod-dungeon-clear elects the same tank as its leader.
 *   - Every other tank is an off-tank. It never takes the main tank's target. It picks up mobs
 *     that are hitting someone who isn't a tank first, then extra mobs on the main tank, and
 *     leaves mobs another off-tank already holds.
 *   - On trash, an off-tank holding a mob drags it a little way from the main tank, so cleaves
 *     and frontal attacks don't hit both tanks. The spot is on dry ground (no lava or water),
 *     at about the same height and in line of sight of the main tank.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_TANK_ROLES_H
#define MOD_RAID_CLEAR_TANK_ROLES_H

#include "AttackAction.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "ObjectGuid.h"
#include "Strategy.h"
#include "Trigger.h"

class Group;
class Player;
class PlayerbotAI;
class Unit;

namespace RaidClear::Tanks
{
    // Flag the group's main tank if nobody has the flag. World thread only.
    void AssignMainTank(Group* group);

    // The flagged main tank while alive, else the first living tank in group order.
    Player* ActingMainTank(Group* group);

    // A tank bot that isn't the acting main tank, in a group that has one.
    bool IsOffTank(Player* bot, Player* mainTank);

    // The mob this off-tank should hold, or nullptr when there's nothing for it.
    Unit* PickOffTankTarget(PlayerbotAI* botAI, Player* bot, Player* mainTank, GuidVector const& attackers);

    // A boss is among the attackers (separation is trash-only unless configured).
    bool BossInFight(PlayerbotAI* botAI, GuidVector const& attackers);
}

class RaidClearTanksStrategy : public Strategy
{
public:
    RaidClearTanksStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc raid tanks"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

class RcOffTankTargetTrigger : public Trigger
{
public:
    RcOffTankTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc offtank target") {}
    bool IsActive() override;
};

class RcOffTankTargetAction : public AttackAction
{
public:
    RcOffTankTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "rc offtank target") {}
    bool Execute(Event event) override;
};

class RcOffTankSeparateTrigger : public Trigger
{
public:
    RcOffTankSeparateTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc offtank separate") {}
    bool IsActive() override;
};

class RcOffTankSeparateAction : public MovementAction
{
public:
    RcOffTankSeparateAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc offtank separate") {}
    bool Execute(Event event) override;
};

// Keeps off-tanks' own target switching (tank assist and friends) off the main tank's mob.
class RcOffTankMultiplier : public Multiplier
{
public:
    RcOffTankMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc offtank") {}
    float GetValue(Action* action) override;
};

#endif
