/*
 * mod-raid-clear: interrupting an enemy's cast, whoever's target it is.
 *
 * Playerbots only interrupts the bot's own target (Kick, Pummel) or, with Counterspell, Wind Shear
 * and Silence, an enemy healer it happens to see. A raid's must-stop casts (a zealot's or a
 * princess's Great Heal) are interrupted by any bot that has one ready and in reach.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_INTERRUPTS_H
#define MOD_RAID_CLEAR_INTERRUPTS_H

class Player;
class PlayerbotAI;
class Unit;

namespace RaidClear
{
    // Whether the bot has the power for this spell (its playerbots name) right now. CanCastSpell
    // tests a spell with its cost ignored: a rogue out of energy would pass and then fail the
    // cast on every tick.
    bool CanPayFor(PlayerbotAI* botAI, Player* bot, char const* spell);

    // The first interrupt the bot has ready, paid for and in reach of the target (playerbots'
    // name), or nullptr. A tank only interrupts what it holds: it would turn its back on that.
    char const* ReadyInterrupt(PlayerbotAI* botAI, Player* bot, Unit* target);
}

#endif
