/*
 * mod-raid-clear: bots don't back into packs that aren't in the fight.
 *
 * Playerbots moves a bot away from something (a mob in its face, a boss's AoE, the rest of the
 * group for a Living Bomb, a patch on the floor) by picking a point straight behind it. Nothing
 * checks what stands there, so in a raid the bot backs into the next pack or the next boss and
 * the raid is fighting both. This module's own "move out" actions do the same.
 *
 * In every raid, while a bot is in combat:
 *   - Every walk a bot starts is looked at from the map's own update, not from the bot's AI,
 *     which only runs every 100 to 500 ms while the bot keeps moving. A walk that passes within
 *     a mob's aggro radius plus a buffer, and takes the bot closer to that mob than it is, is
 *     stopped before the bot has gone a yard. The bot then starts no ordinary walk for 0.7 s, so
 *     whatever sent it doesn't send it again at once.
 *   - Only mobs that would actually come count: hostile, aggressive, alive, not in combat, not
 *     evading, on the bot's level (they don't aggro more than a few yards up or down) and with
 *     line of sight.
 *   - A walk toward the bot's target (going to the fight, or pulling) is judged more gently. The
 *     target itself and any mob whose aggro radius plus the buffer already covers the target
 *     don't count, because there is no getting at the target without them. Every other mob only
 *     has to be passed by half the buffer: round the corner of a pack is fine, through it is not.
 *   - Any other walk (backing off, spreading out, dodging) is judged against every idle mob with
 *     the full buffer. A boss tanked next to an idle pack doesn't make that pack safe to back
 *     into.
 *   - A bot standing a yard closer than a walk toward a target may pass (a patrol walking up, or
 *     a bot that crept there) steps back out, toward the main tank where it can. Not a bot in
 *     melee with its target, and not for mobs its target stands next to.
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
    bool _lastTooClose = false;
    bool _lastResult = false;
};

class RcAggroLeashAction : public MovementAction
{
public:
    RcAggroLeashAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc aggro leash") {}
    bool Execute(Event event) override;

private:
    uint32 _noSpotAtMs = 0;
};

#endif
