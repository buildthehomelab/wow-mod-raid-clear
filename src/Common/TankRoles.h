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
 *     Buffet), or that two tanks take in turns (Fankriss), the off-tank attacks the boss whenever
 *     no add needs it, so it sits second on threat and catches the boss, and an off-tank the
 *     boss is on keeps it rather than turning to adds. Playerbots' "lose aggro" never makes a
 *     non-main tank taunt off another tank, and makes the main tank taunt back.
 *   - On bosses that keep taking their tank away (each raid lists them: Hakkar's mind control and
 *     threat wipes) the tanks share the boss's threat and the damage dealers stay under it, so
 *     whoever the boss turns to next is a tank:
 *       - a tank the boss isn't on taunts when its threat has dropped under 90% of the highest
 *         player's (a taunt lifts it to the top). Under half, it has lost its threat and taunts
 *         at once; between half and 90% it has only drifted behind, and does so at most every
 *         30 s, so the taunts aren't all on cooldown when they are needed;
 *       - any tank taunts the moment the boss is on a player who isn't a tank;
 *       - one tank at a time: nobody taunts while another tank's taunt is still running.
 *       - Playerbots' damage dealers don't watch their threat at all (its "threat" strategy is
 *         off by default). On these bosses they hold their damage while they are within 10% of
 *         the threat of the tank who would catch the boss next.
 *       - Where no taunt lands (Hakkar is immune on this core, so none of the taunting above
 *         happens there) the paladins stand in for it. Hand of Protection on a player the boss
 *         is on who isn't a tank: the core doesn't let a creature stay on someone its swings
 *         can't hurt while anyone else is on its threat list, so it turns to the next one.
 *         Only when that next one is a tank, which the damage limit sees to (it stays on while
 *         such a boss is loose); with a damage dealer next it would only pass the boss along.
 *         The player can't attack for those 10 s. A player in bear form, Defensive Stance or
 *         Frost Presence, or with Righteous Fury, counts as tanking whatever its talents say.
 *       - A player in the raid has nothing holding its damage back. One who is within 10% of
 *         that same tank's threat gets a paladin's Hand of Salvation (20% of its threat gone
 *         over 10 s), and the next paladin's when that one has run out.
 *   - No tank spends a global cooldown on a taunt that can't land: Taunt, Growl, Dark Command
 *     and Righteous Defense are not cast at a boss immune to taunts (Chromaggus, Nefarian,
 *     Broodlord and Hakkar on this core; one Chromaggus kill had 120 Righteous Defenses, all
 *     IMMUNE). Hand of Reckoning and Mocking Blow still go out for their damage.
 *   - Endless respawning adds a raid lists (BWL's Suppression Room whelps) are never picked up.
 *   - Among the loose mobs an off-tank picks from, one that evades every attack (it is running
 *     home, or can't find a way to its victim) is skipped: a taunt on it does nothing.
 *   - Adds a raid lists as needing their own tank (BWL's Death Talon Captain) go to an off-tank
 *     first: it taunts them off whoever has them, the main tank included, keeps them until they
 *     die and drags them by their own splash radius.
 *   - Packs where every tank should hold one (BWL's Wyrmguards, also listed by each raid): the
 *     main tank keeps one and each off-tank is handed another the moment the pack is pulled,
 *     taunts it off whoever has it and holds it.
 *   - On trash, an off-tank holding a mob drags it away from the main tank's mob, so cleaves
 *     and stomps don't hit both tanks, and from the mobs held by off-tanks ahead of it in group
 *     order (those don't move for it), so three or more mobs end up spread out instead of two
 *     off-tanks dragging theirs to the same spot. The distance is measured mob to mob: the
 *     configured separation, or more for mobs with a big AoE (each raid lists those with their
 *     radius).
 *     The spot is on dry ground (no lava or water), at about the same height and in line of
 *     sight of the main tank, and clear of every pack and boss that isn't in the fight (by the
 *     aggro leash's buffer, see Common/AggroLeash.h), as is the walk to it.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_TANK_ROLES_H
#define MOD_RAID_CLEAR_TANK_ROLES_H

#include "Action.h"
#include "AttackAction.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "ObjectGuid.h"
#include "Strategy.h"
#include "Trigger.h"

#include <map>

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
    // Adds no tank bothers with and that never stop coming (Blackwing Lair's Suppression Room whelps).
    bool IsEndlessAdd(Unit const* unit);
    bool IsOwnTankAdd(Unit const* unit);

    // The own-tank adds the main tank leaves to off-tanks: those an off-tank already holds, plus
    // as many of the rest as there are off-tanks not holding one yet. Any left over are the main
    // tank's own (a double pull with too few off-tanks).
    GuidSet OwnTankAddsForOffTanks(PlayerbotAI* botAI, Player* mainTank, GuidVector const& attackers);

    // A mob of a pack where every tank takes one (the raid lists them: BWL's Wyrmguards).
    bool IsSpreadAdd(Unit const* unit);

    // Which off-tank takes which spread add (those fighting the group within 60 yd of the main
    // tank, read from the world so every tank sees the same ones). Off-tanks keep the one they
    // hold; the main tank keeps the one it's on (else the first by GUID); the rest go one each to
    // the bot off-tanks near the main tank, in group order: idle ones first, then ones holding some
    // other mob; never one on an own-tank add. Any left over stay on the main tank.
    std::map<ObjectGuid, Player*> SpreadAddsForOffTanks(PlayerbotAI* botAI, Player* mainTank, GuidVector const& attackers);

    // Everything the main tank leaves to its off-tanks: own-tank adds and handed-out spread adds.
    GuidSet HandedOffAdds(PlayerbotAI* botAI, Player* mainTank, GuidVector const& attackers);

    // One of playerbots' taunts (by action name), single-target or area.
    bool IsTauntAction(Action* action);
    // Cast the first single-target taunt this bot has ready on the unit.
    bool CanTaunt(PlayerbotAI* botAI, Unit* unit);
    bool Taunt(PlayerbotAI* botAI, Unit* unit);

    // A living boss in the fight that cuts its tank's threat, which the off-tank co-tanks.
    Unit* CoTankBoss(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers);

    // The mob this off-tank should hold, or nullptr when there's nothing for it.
    Unit* PickOffTankTarget(PlayerbotAI* botAI, Player* bot, Player* mainTank, GuidVector const& attackers);

    // A boss is among the attackers (separation is trash-only unless configured).
    bool BossInFight(PlayerbotAI* botAI, GuidVector const& attackers);

    // A living boss in combat whose threat the tanks share (the raid lists them), and whether the
    // map has any at all.
    bool HasThreatShareBosses(uint32 mapId);
    bool IsThreatShareBoss(Unit const* unit);
    Unit* ThreatShareBoss(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers);

    // Why this tank should taunt a threat-share boss it isn't tanking.
    enum class ShareNeed
    {
        None,
        Drift,  // somewhat behind on threat, or the boss is on a pet: no hurry
        Lost,   // has lost its threat (wiped, revived): catch up now
        Catch,  // the boss is on a player who isn't a tank
    };
    ShareNeed NeedsThreatShare(Player* bot, Unit* boss);

    // The threat a damage dealer stays under on a threat-share boss; 0 = no limit right now.
    // While the boss is loose on someone who can't hold it there is none, unless it can't be
    // taunted: then it is the best tank's threat, so that tank stays next in line.
    float ThreatShareLimit(Player* bot, Unit* boss);

    // No taunt moves this boss (Hakkar: the core's NO_TAUNT immunity set).
    bool CantBeTaunted(Unit* boss);

    // Such a boss is hitting a player who isn't a tank, and the next on its threat is a tank: that
    // player, unless something already keeps the boss's swings off it.
    Player* BossVictimToProtect(Unit* boss);

    // A player (not a bot) who isn't a tank and is over the threat the bots stay under, without
    // Hand of Salvation on it and without this paladin's Hand of Protection: the one with the most.
    Player* PlayerOverThreatShare(Player* bot, Unit* boss);

    // The most threat a tank in the bot's group has on this boss, among the tanks that can hold
    // it (alive, not mind-controlled) and are within `range` of it; -1 when there is no such tank.
    float BestTankThreat(Player* bot, Unit* boss, float range);

    // Whether a threat limit holds this action back: anything that does damage or is cast for its
    // threat. Debuffs that do neither and Tranquilizing Shot still go out.
    bool HeldByThreatCap(PlayerbotAI* botAI, Action* action);
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
    bool isUseful() override;

private:
    uint32 lastFailMs = 0;
};

class RcTankShareThreatTrigger : public Trigger
{
public:
    RcTankShareThreatTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc tank share threat") {}
    bool IsActive() override;
};

class RcTankShareThreatAction : public Action
{
public:
    RcTankShareThreatAction(PlayerbotAI* botAI) : Action(botAI, "rc tank share threat") {}
    bool Execute(Event event) override;
    bool isUseful() override;

private:
    uint32 lastTauntMs = 0;
};

// A threat-share boss that can't be taunted is on a player who isn't a tank.
class RcBossOnNonTankTrigger : public Trigger
{
public:
    RcBossOnNonTankTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc boss on non tank") {}
    bool IsActive() override;
};

// A paladin's Hand of Protection on that player: the boss can't hurt it for 10 s and turns to
// whoever is next on its threat, which the threat cap makes a tank.
class RcProtectBossVictimAction : public Action
{
public:
    RcProtectBossVictimAction(PlayerbotAI* botAI) : Action(botAI, "rc protect boss victim") {}
    bool Execute(Event event) override;
};

// A player is over the threat the bots stay under on a threat-share boss.
class RcPlayerOverThreatTrigger : public Trigger
{
public:
    RcPlayerOverThreatTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc player over threat") {}
    bool IsActive() override;
};

class RcSalvationOnPlayerAction : public Action
{
public:
    RcSalvationOnPlayerAction(PlayerbotAI* botAI) : Action(botAI, "rc salvation on player") {}
    bool Execute(Event event) override;
};

// Keeps damage dealers under the tank who catches a threat-share boss next.
class RcTankThreatCapMultiplier : public Multiplier
{
public:
    RcTankThreatCapMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc tank threat cap") {}
    float GetValue(Action* action) override;
};

// No taunt that does nothing else at a boss immune to taunts.
class RcImmuneTauntMultiplier : public Multiplier
{
public:
    RcImmuneTauntMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc immune taunt") {}
    float GetValue(Action* action) override;
};

// Keeps off-tanks' own target switching (tank assist and friends) off the main tank's mob.
class RcOffTankMultiplier : public Multiplier
{
public:
    RcOffTankMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc offtank") {}
    float GetValue(Action* action) override;
};

#endif
