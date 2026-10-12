/*
 * mod-raid-clear: Onyxia's Lair (map 249).
 *
 * Runs next to mod-playerbots' "onyxia" strategy, which keeps bots off her head and tail, away
 * from the whelp eggs, apart for Fireball and on the whelps while she is in the air.
 *
 * Replaced here (mechanics from the core script in scripts/Kalimdor/OnyxiasLair and from
 * mod-individual-progression's level 60 Onyxia):
 *   - Deep Breath. In the air she stops over one of eight points around the room, takes a deep
 *     breath and flies to the opposite point. The breath is a chain of 7 to 20 spells, each
 *     burning everyone within 18 yd of a fixed spot on the floor, the spots a few yards apart:
 *     a 36 yd wide strip of fire from wall to wall, for 4,000-7,000 a hit. Where each spell lands
 *     is a row in spell_target_position, not in the script.
 *     Playerbots sends its bots to two fixed "safe zones" per breath spell. Playerbot core
 *     2a2211c moved the strips (db_world 2026_09_28_00) and two of the diagonal spells now burn
 *     the other diagonal, so for those two breaths the safe zones are in the middle of the fire.
 *     Here nothing is fixed: the strip is read from the server's own spell data, a bot inside it
 *     walks to the nearest spot outside, a bot outside stays where it is, and nobody walks into
 *     or across the strip until the fire has passed. Playerbots' own safe zone walk is switched
 *     off. The way out never ends in the whelp caves or the egg trench between them, which the
 *     core's Onyxia sets on fire (Heated Ground) with every breath.
 *     A bot can't act in the middle of a cast, so the moment she starts the breath every bot in
 *     the strip is told to drop its spell.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_ONYXIAS_LAIR_H
#define MOD_RAID_CLEAR_ONYXIAS_LAIR_H

#include "Action.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "Position.h"
#include "Strategy.h"
#include "Trigger.h"

#include <array>

namespace RaidClear::OnyxiasLair
{
    constexpr uint32 MAP_ID = 249;

    enum Creatures : uint32
    {
        NPC_ONYXIA    = 10184,
        NPC_ONYXIA_40 = 301000,  // mod-individual-progression's level 60 Onyxia
    };

    // The first spell of each Deep Breath chain: the one she casts. Which line of the room a
    // chain burns comes from spell_target_position, so the names the scripts give them are left
    // out on purpose.
    constexpr std::array<uint32, 8> DEEP_BREATHS = { 17086, 18351, 18576, 18609, 18564, 18584, 18596, 18617 };

    // A bot closer than this to the edge of a blast leaves; it stops this far outside.
    constexpr float BREATH_KEEP_OUT = 4.0f;
    constexpr float BREATH_KEEP_OUT_TARGET = 7.0f;
    // A spot more than this above or below the bot is up on a ledge or down in the egg trench
    // (about 8 yd below the floor).
    constexpr float BREATH_SPOT_HEIGHT = 4.0f;
    // The two whelp caves (their mouths; playerbots keeps bots 5 yd away from the same points).
    constexpr float EGGS_KEEP_OUT = 10.0f;
    // Heated Ground: twelve patches of fire along the egg trench, a chain like the breath's.
    // A bot leaving the breath doesn't stop within this of the edge of one.
    constexpr uint32 SPELL_HEATED_GROUND = 22191;
    constexpr float ROOKERY_KEEP_OUT = 2.0f;

    void AddScripts();
}

class RaidClearOnyxiasLairStrategy : public Strategy
{
public:
    RaidClearOnyxiasLairStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc ony"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// --- Deep Breath ----------------------------------------------------------

// Onyxia is casting a Deep Breath, or its fire is still crossing the room, and the bot is in it.
class RcOnyDeepBreathTrigger : public Trigger
{
public:
    RcOnyDeepBreathTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc ony deep breath") {}
    bool IsActive() override;
};

class RcOnyLeaveDeepBreathAction : public MovementAction
{
public:
    RcOnyLeaveDeepBreathAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc ony leave deep breath") {}
    bool Execute(Event event) override;

private:
    // The spot picked for this breath, kept so the bot doesn't change its mind mid-run.
    Position _spot;
    uint32 _spotBreath = 0;
    uint64 _spotFoundMs = 0;
};

// Switches off playerbots' fixed safe zones, and for as long as a breath lasts keeps every other
// movement from taking a bot into or across the fire.
class RcOnyDeepBreathMultiplier : public Multiplier
{
public:
    RcOnyDeepBreathMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc ony deep breath") {}
    float GetValue(Action* action) override;
};

#endif
