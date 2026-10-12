/*
 * mod-raid-clear: bots don't back into packs that aren't in the fight.
 *
 * Playerbots moves a bot away from something (a mob in its face, a boss's AoE, the rest of the
 * group for a Living Bomb, a patch on the floor) by picking a point straight behind it. Nothing
 * checks what stands there, so in a raid the bot backs into the next pack or the next boss and
 * the raid is fighting both. This module's own "move out" actions do the same.
 *
 * In every raid, while a bot is in combat:
 *   - A walk that would end, or pass, within a mob's aggro radius plus a buffer is stopped on
 *     the bot's next update, and the bot stands still for a moment. Only mobs that would
 *     actually come count: hostile, aggressive, alive, not in combat, not evading, on the bot's
 *     level (they don't aggro more than a few yards up or down) and with line of sight.
 *   - A bot standing that close to such a mob (a patrol walking up, or the bot stopped late)
 *     steps back out, toward the main tank where it can.
 *   - A walk toward the bot's own target is never stopped: going to the fight, or pulling, is
 *     the point. Neither is a bot in melee with its target moved off it.
 *
 * The aggro radius is the core's own (Creature::GetAggroRange: about 20 yd at the same level,
 * a yard more per level the mob is above the bot).
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_AGGRO_LEASH_H
#define MOD_RAID_CLEAR_AGGRO_LEASH_H

#include "Action.h"
#include "MovementActions.h"
#include "Strategy.h"
#include "Trigger.h"

class RaidClearLeashStrategy : public Strategy
{
public:
    RaidClearLeashStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc raid leash"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

class RcAggroLeashTrigger : public Trigger
{
public:
    RcAggroLeashTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aggro leash") {}
    bool IsActive() override;

private:
    // A bot standing still is only looked at a few times a second.
    uint32 _lastIdleCheckMs = 0;
    bool _lastIdleResult = false;
};

class RcAggroLeashAction : public MovementAction
{
public:
    RcAggroLeashAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc aggro leash") {}
    bool Execute(Event event) override;
};

#endif
