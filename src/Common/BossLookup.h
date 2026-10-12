/*
 * mod-raid-clear: finding a boss in the bot's instance.
 *
 * Playerbots' "find target" value only looks through what the bot itself has threat on. That
 * misses a boss that wiped its threat list (Onyxia taking off), and a bot that was revived
 * mid-fight or has only been hitting the adds. This looks the creature up on the map instead.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_BOSS_LOOKUP_H
#define MOD_RAID_CLEAR_BOSS_LOOKUP_H

#include "Define.h"

#include <initializer_list>

class Creature;
class Player;

namespace RaidClear
{
    // The first creature with one of these entries in the bot's instance, if it is alive and in
    // combat; nullptr otherwise. The lookup itself is cached for a second per instance, so the
    // triggers, actions and multipliers of a whole raid can ask every tick.
    Creature* FightingCreature(Player* bot, std::initializer_list<uint32> entries);
}

#endif
