/*
 * mod-raid-clear: raid members an enemy has mind-controlled (Hakkar's Cause Insanity, the Prophet
 * Skeram's True Fulfillment and the like). A raid adds these to its strategy:
 *
 *   - The member itself. The core gives a player an enemy controls an AI of its own (melee, and
 *     now and then one of its class's single-target spells), but playerbots goes on running the
 *     bot as well: with its faction now the enemy's, the raid is what it fights, and its area
 *     spells (Blizzard, Consecration, Whirlwind) land on everyone around it, with whatever
 *     damage the mind control adds (True Fulfillment: +300%). So a bot an enemy controls does
 *     nothing of its own ("rc charmed self"); the core's AI is all that is left of it, as for a
 *     player.
 *   - It is stopped: a mage's Polymorph or a paladin's Hammer of Justice, one bot at a time
 *     (Polymorph lasts 10 s on a player, Hammer of Justice 6 s). When the mind control ends
 *     first, the bot that cast it takes its sheep or stun off again, so nobody comes back as a
 *     sheep. Not Blind: nothing takes that off.
 *   - Nobody hurts it: a bot or pet on it lets go ("rc charmed target", where the raid has no
 *     wider rule of its own), and no spell that hits more than one enemy is cast within 12 yd of
 *     it.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_CHARMED_H
#define MOD_RAID_CLEAR_CHARMED_H

#include "Action.h"
#include "AttackAction.h"
#include "Multiplier.h"
#include "Trigger.h"

class Player;
class Unit;

namespace RaidClear::Charmed
{
    // No spell that hits more than one enemy is cast this close to a mind-controlled raid member.
    constexpr float SPLASH_RANGE = 12.0f;

    // A member of the bot's raid (not the bot) that an enemy controls right now.
    bool IsCharmedRaidMember(Player* bot, Unit const* unit);

    // The bot itself is controlled by a creature.
    bool IsCharmedByEnemy(Player const* bot);
}

// An enemy has taken over a raid member this bot can stop (Polymorph, Hammer of Justice), or a
// member it stopped is itself again.
class RcCharmedMemberTrigger : public Trigger
{
public:
    RcCharmedMemberTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc charmed member") {}
    bool IsActive() override;
};

class RcControlCharmedAction : public Action
{
public:
    RcControlCharmedAction(PlayerbotAI* botAI) : Action(botAI, "rc control charmed") {}
    bool Execute(Event event) override;
};

// Zeroes spells that hit more than one enemy while a mind-controlled raid member stands where
// they would land.
class RcCharmedSplashMultiplier : public Multiplier
{
public:
    RcCharmedSplashMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc charmed splash") {}
    float GetValue(Action* action) override;
};

// Zeroes everything a bot would do of its own while an enemy controls it.
class RcCharmedSelfMultiplier : public Multiplier
{
public:
    RcCharmedSelfMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc charmed self") {}
    float GetValue(Action* action) override;
};

// Takes the bot, and its pet, off a target nobody should be hitting: the bot to what playerbots
// would pick next, else the nearest enemy it can hurt, else nothing. Which targets those are is
// the raid's (IsBadTarget for the bot, IsBadPetTarget for its pet).
class RcLeaveTargetAction : public AttackAction
{
public:
    RcLeaveTargetAction(PlayerbotAI* botAI, std::string const name) : AttackAction(botAI, name) {}
    bool Execute(Event event) override;

protected:
    virtual bool IsBadTarget(Unit const* unit) = 0;
    virtual bool IsBadPetTarget(Unit const* unit) = 0;
};

// The bot's target, or its pet's, is a raid member an enemy controls.
class RcCharmedTargetTrigger : public Trigger
{
public:
    RcCharmedTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc charmed target") {}
    bool IsActive() override;
};

class RcLeaveCharmedTargetAction : public RcLeaveTargetAction
{
public:
    RcLeaveCharmedTargetAction(PlayerbotAI* botAI) : RcLeaveTargetAction(botAI, "rc leave charmed target") {}

protected:
    bool IsBadTarget(Unit const* unit) override;
    bool IsBadPetTarget(Unit const* unit) override;
};

#endif
