/*
 * mod-raid-clear: bots don't back into packs that aren't in the fight.
 *
 * Playerbots moves a bot away from something (a mob in its face, a boss's AoE, the rest of the
 * group for a Living Bomb, a patch on the floor) by picking a point straight behind it. Nothing
 * checks what stands there, so in a raid the bot backs into the next pack or the next boss and
 * the raid is fighting both. This module's own "move out" actions do the same.
 *
 * In every raid, while a bot is in combat:
 *   - Every walk a bot starts is looked at from the map's own update, not from the bot's AI: a
 *     bot in combat only thinks twice a second and would be 3-4 yd further on by then. A walk
 *     that passes within a mob's aggro radius plus a buffer, and takes the bot closer to that
 *     mob than it is, is stopped before the bot has gone a yard. For the next two seconds the
 *     bot may only walk toward a target (reach melee, reach spell, reach someone to heal), so
 *     whatever sent it doesn't send it again at once.
 *   - Only mobs that would actually come count: hostile, aggressive, alive, not in combat, not
 *     evading, on the bot's level (they don't aggro more than a few yards up or down) and with
 *     line of sight.
 *   - A bot standing within half the buffer of such a mob (a patrol walking up) steps back out,
 *     toward the main tank where it can.
 *   - A mob the bot can't avoid if it wants its target doesn't count: the target itself (a
 *     pull), and any mob whose aggro radius plus the buffer already covers the target. A walk
 *     toward the target is still checked against every other mob on the way, with a 1 yd buffer:
 *     going round the corner of a pack is fine, through it is not.
 *   - A bot in melee with its target is never moved off it.
 *
 * The aggro radius is the core's own (Creature::GetAggroRange: the mob's detection range, 20 yd
 * by default, plus a yard per level the mob is above the bot).
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_AGGRO_LEASH_H
#define MOD_RAID_CLEAR_AGGRO_LEASH_H

#include "Action.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "Strategy.h"
#include "Trigger.h"

namespace RaidClear::Leash
{
    void AddScripts();
}

class RaidClearLeashStrategy : public Strategy
{
public:
    RaidClearLeashStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc raid leash"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// The bot is standing at the edge of an idle mob's aggro radius.
class RcAggroLeashTrigger : public Trigger
{
public:
    RcAggroLeashTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aggro leash") {}
    bool IsActive() override;

private:
    // Looked at a few times a second, not on every update.
    uint32 _lastCheckMs = 0;
    bool _lastResult = false;
};

class RcAggroLeashAction : public MovementAction
{
public:
    RcAggroLeashAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc aggro leash") {}
    bool Execute(Event event) override;
};

// For two seconds after one of its walks was stopped, a bot only walks toward a target.
class RcAggroLeashMultiplier : public Multiplier
{
public:
    RcAggroLeashMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aggro leash") {}
    float GetValue(Action* action) override;
};

#endif
