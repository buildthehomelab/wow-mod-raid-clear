/*
 * mod-raid-clear: who starts a fight, and who stays out of melee.
 *
 * Installed on every raid map, on both the combat and the non-combat engine (a pull starts from
 * the second).
 *
 * Pull discipline. Playerbots counts the group's skull as a target for every bot, in combat or
 * not, so the moment a pack is marked the whole raid opens on it: a mage's Flamestrike lands
 * before a tank has moved, the pack runs through the casters, and the next pack is pulled by
 * whoever backs away from it. So:
 *   - A bot that isn't a tank leaves a pack alone until something in the raid has started the
 *     fight with it (a tank's pull, a patrol walking in, a real player's opener). It doesn't
 *     attack it, cast on it or walk up to it, and playerbots' target pickers don't offer it.
 *   - No bot, tank or not, opens on a boss that isn't in the fight while the raid is still
 *     fighting something else.
 *   - A bot whose target turns out to be one of those takes the nearest enemy that is in the
 *     fight instead, or lets go.
 * Only database spawns count (a pack waiting where it stands): anything summoned mid-fight is
 * fair game the moment it appears. Off when the raid has no tank to do the pulling. A player's
 * "attack my target" on such a pack waits like the rest until the player (or a tank) has hit it.
 *
 * Healers stay back. A healer paladin's idle action is Judgement of Light, 10 yd, and playerbots
 * walks the bot into range for it: nine holy paladins standing in every Cleave, War Stomp and
 * breath. A healer paladin now only judges what is already in range from where it stands.
 *
 * Tranquilizing Shot. mod-individual-progression moves it (a book in Molten Core originally) to
 * level 61 on the trainers, so a level 60 hunter bot never has it and every Frenzy stays up.
 * Hunter bots on a raid map are taught it.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_RAID_DISCIPLINE_H
#define MOD_RAID_CLEAR_RAID_DISCIPLINE_H

#include "Action.h"
#include "AttackAction.h"
#include "Multiplier.h"
#include "Strategy.h"
#include "Trigger.h"

#include <vector>

class Player;
class PlayerbotAI;
class Unit;

namespace RaidClear::Discipline
{
    // A creature standing where it spawned that nothing has started a fight with.
    bool IsUnpulled(Unit const* unit);

    // Whether this bot leaves `unit` alone for now (see the rules above).
    bool MustWaitFor(PlayerbotAI* botAI, Player* bot, Unit* unit);

    // What a bot needs to know in a raid that its trainer won't teach it. World thread only.
    void TeachRaidSpells(Player* bot);
}

class RaidClearDisciplineStrategy : public Strategy
{
public:
    RaidClearDisciplineStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc raid discipline"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
    bool HasTargetExclusions() const override { return true; }
    void AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type) override;
};

// The bot (or its pet) is on a target it should be waiting for.
class RcUnpulledTargetTrigger : public Trigger
{
public:
    RcUnpulledTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc unpulled target") {}
    bool IsActive() override;
};

class RcLeaveUnpulledTargetAction : public AttackAction
{
public:
    RcLeaveUnpulledTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "rc leave unpulled target") {}
    bool Execute(Event event) override;
};

// Nothing aimed at a target the bot should be waiting for.
class RcPullDisciplineMultiplier : public Multiplier
{
public:
    RcPullDisciplineMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc pull discipline") {}
    float GetValue(Action* action) override;
};

// A healer paladin doesn't walk into melee to judge.
class RcHealerStaysBackMultiplier : public Multiplier
{
public:
    RcHealerStaysBackMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc healer stays back") {}
    float GetValue(Action* action) override;
};

#endif
