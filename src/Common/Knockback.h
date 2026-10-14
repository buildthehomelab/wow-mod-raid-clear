/*
 * mod-raid-clear: nobody is thrown into the next pack.
 *
 * Some mobs throw players about. A Molten Giant's Knock Away sends its tank 9 yd, Broodlord's
 * sends his 17, his Blast Wave everyone within 20 yd of him 18, and the Wing Buffet of Blackwing
 * Lair's drakes sends everyone in front of them 39 yd. Nothing looks at where that ends.
 * Playerbots' "tank face" even walks a tank round its mob to turn it away from the raid, so the
 * tank stands on the far side with its back to whatever hasn't been pulled yet, and that is
 * where it lands: in the next pack, or at the next boss.
 *
 * In every raid, as part of the aggro leash (Common/AggroLeash.h):
 *   - Which mob throws, whom and how far is read from the server's spell data: the spell's
 *     knockback effect gives the speeds, and the distance follows the way the core works it out
 *     for a bot (twice the upward speed over gravity, times the speed along the ground). Each
 *     raid lists the throwers it knows (BWL's drakes and Broodlord, Molten Core's giants, Lava
 *     Surgers and Sulfuron). Any other is picked up when it is seen to throw someone on a raid
 *     map, and known from then until the server restarts.
 *   - Who it catches comes from the spell too: only whoever the mob is on, everyone within the
 *     spell's radius of it, or only those of them in front of it (a cone) or behind it (a tail).
 *     A single-target throw is only picked up once it has gone to whoever the mob was on three
 *     times, and dropped for good the first time it goes to anyone else: one aimed at a random
 *     player (a Death Talon Flamescale's Berserker Charge) can't be stood out of the way of.
 *   - A bot standing where such a throw would catch it and carry it into an idle mob's aggro
 *     radius (the flight there included, and stopping at walls the way the core stops a thrown
 *     bot) moves. One in melee with the thrower goes round it, to the nearest side from which
 *     the throw ends in clear ground or against a wall: whoever it is on anywhere round it (not
 *     while it is casting, which would sweep a breath across the raid), anyone else within the
 *     half behind it. Anyone not in melee with it walks to the nearest spot out of the throw's
 *     reach, or with a clear landing.
 *   - While such a mob is on a tank and there is an idle pack near enough to be thrown into,
 *     "tank face" is off for that tank: which side it stands on is decided by where it lands.
 *   - With no idle mob anywhere near a thrower none of this does anything.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_KNOCKBACK_H
#define MOD_RAID_CLEAR_KNOCKBACK_H

#include "Common/WayOut.h"

#include "Action.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "Trigger.h"

#include <string>
#include <vector>

namespace RaidClear::Knockback
{
    // A mob a raid knows to throw players, and the spell it does it with. How far and whom is
    // read from the spell.
    struct Thrower
    {
        uint32 entry;
        uint32 spell;
    };

    void AddScripts();
}

// The bot stands where a throw would carry it into a pack that isn't in the fight. Two of them,
// so tanks and the rest can be given their own place among a bot's other moves.
class RcKnockbackFootingTrigger : public Trigger
{
public:
    RcKnockbackFootingTrigger(PlayerbotAI* botAI, std::string const& name, bool tanks)
        : Trigger(botAI, name), _tanks(tanks) {}
    bool IsActive() override;

private:
    bool _tanks;
    // Looked at a couple of times a second, not on every update.
    uint32 _lastCheckMs = 0;
    bool _lastResult = false;
};

class RcKnockbackFootingAction : public MovementAction
{
public:
    RcKnockbackFootingAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc knockback footing"), _walk(botAI) {}
    bool Execute(Event event) override;
    bool isUseful() override;

private:
    RaidClear::WayOutWalk _walk;
    uint32 _sentAtMs = 0;
    uint32 _noSpotAtMs = 0;
};

// No "tank face" for a tank whose mob could throw it into an idle pack.
class RcKnockbackFootingMultiplier : public Multiplier
{
public:
    RcKnockbackFootingMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc knockback footing") {}
    float GetValue(Action* action) override;

private:
    uint32 _lastCheckMs = 0;
    bool _lastResult = false;
};

#endif
