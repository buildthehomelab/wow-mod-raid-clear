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
 *   - On bosses that cut their tank's threat (Broodlord's Knock Away, the BWL drakes' Wing
 *     Buffet) the off-tank attacks the boss whenever no add needs it, so it sits second on
 *     threat and catches the boss. Playerbots' "lose aggro" never makes a non-main tank taunt
 *     off another tank, and makes the main tank taunt back.
 *   - Endless respawning adds a raid lists (BWL's Suppression Room whelps) are never picked up.
 *   - Adds a raid lists as needing their own tank (BWL's Death Talon Captain) go to an off-tank
 *     first: it taunts them off whoever has them, the main tank included, keeps them until they
 *     die and drags them by their own splash radius.
 *   - On trash, an off-tank holding a mob drags it away from the main tank's mob, so cleaves
 *     and stomps don't hit both tanks. The distance is measured mob to mob: the configured
 *     separation, or more for mobs with a big AoE (each raid lists those with their radius).
 *     The spot is on dry ground (no lava or water), at about the same height and in line of
 *     sight of the main tank.
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
    // A mob whose own AoE (stomp, cleave, knockback) reaches this far around it. Two tanked mobs
    // are kept at least the sum of their radii apart, or the configured separation if larger.
    struct SplashRadius
    {
        uint32 entry;
        float radius;
    };

    // Yards the off-tank's mob should be kept from the main tank's mob.
    float WantedSeparation(Unit const* mine, Unit const* theirs);

    // Flag the group's main tank if nobody has the flag. World thread only.
    void AssignMainTank(Group* group);

    // The flagged main tank while alive, else the first living tank in group order.
    Player* ActingMainTank(Group* group);

    // A tank bot that isn't the acting main tank, in a group that has one.
    bool IsOffTank(Player* bot, Player* mainTank);

    // An add that gets an off-tank of its own (the raid lists them), even off the main tank.
    bool IsOwnTankAdd(Unit const* unit);

    // The own-tank adds the main tank leaves to off-tanks: those an off-tank already holds, plus
    // as many of the rest as there are off-tanks not holding one yet. Any left over are the main
    // tank's own (a double pull with too few off-tanks).
    GuidSet OwnTankAddsForOffTanks(PlayerbotAI* botAI, Player* mainTank, GuidVector const& attackers);

    // Cast the first single-target taunt this bot has ready on the unit.
    bool CanTaunt(PlayerbotAI* botAI, Unit* unit);
    bool Taunt(PlayerbotAI* botAI, Unit* unit);

    // A living boss in the fight that cuts its tank's threat, which the off-tank co-tanks.
    Unit* CoTankBoss(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers);

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
