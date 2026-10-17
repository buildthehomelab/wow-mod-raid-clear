/*
 * mod-raid-clear: fixed spots from the config, for fights a raid guide lays out by place (which
 * wall the boss is tanked against, where the ranged stand).
 *
 * A spot is "x y z", or the line `.gps` prints in game (its first three numbers). An empty value
 * is no spot: the fight goes as it would without one.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_FIXED_SPOTS_H
#define MOD_RAID_CLEAR_FIXED_SPOTS_H

#include "Position.h"

#include <optional>
#include <string>

class Action;
class Player;
class Unit;

namespace RaidClear::Spots
{
    // A tank holds within TANK_NEAR of its spot, a caster or healer within NEAR of its own place
    // round the spot (bots spread up to SCATTER round it). Within HOLD nothing but the raid
    // mechanics walks it away.
    constexpr float TANK_NEAR = 2.0f;
    constexpr float NEAR = 3.5f;
    constexpr float SCATTER = 3.0f;
    constexpr float HOLD = 8.0f;

    // `text` as a spot; nullopt when it is empty, or isn't one (logged, naming `key`).
    std::optional<Position> Parse(std::string const& key, std::string const& text);
    // The config option `key` as a spot.
    std::optional<Position> Load(std::string const& key);

    // The bot's own place round a spot, so a group doesn't stand in one heap: on the floor, at
    // about the spot's height and in sight of it; else the spot itself.
    Position Scatter(Player* bot, Position const& spot);

    // Already walking toward there: a walk sent again every tick plans its path again every tick.
    bool OnTheWay(Player* bot, Position const& to);

    // Walking somewhere, or getting there with a charge. Picking a target isn't movement.
    bool Walks(Action* action);
    // Steps a held bot still takes: the raid mechanics' own (all named "rc ...") and playerbots'
    // step out of a fire on the ground.
    bool IsDodge(Action* action);
}

#endif
