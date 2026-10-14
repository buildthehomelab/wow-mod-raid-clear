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
 *     stopped before the bot has gone a yard. The bot then starts no walk for 0.7 s, so
 *     whatever sent it doesn't send it again at once.
 *   - Only mobs that would actually come count: hostile, aggressive, alive, not in combat, not
 *     evading, on the bot's level (they don't aggro more than a few yards up or down) and with
 *     line of sight.
 *   - A walk toward the bot's target (going to the fight, or pulling) is judged more gently. The
 *     target itself and any mob whose aggro radius plus the buffer already covers the target
 *     don't count, because there is no getting at the target without them. Every other mob only
 *     has to be passed by half the buffer: round the corner of a pack is fine, through it is not.
 *   - Any other walk (backing off, spreading out, dodging) is judged against every idle mob: the
 *     full buffer, or half of it for a mob the target stands next to. A boss tanked beside an
 *     idle pack doesn't make that pack safe to back into.
 *   - A bot standing a yard closer than a walk toward a target may pass (a patrol walking up, or
 *     a bot that crept there) steps back out, toward the main tank where it can. Not a bot in
 *     melee with its target, and not for mobs its target stands next to.
 *   - A spell that hits more than its target isn't cast where it would reach one of those idle
 *     mobs: an area spell (Blizzard, Volley, Death and Decay, Hurricane, Starfall) hits everything
 *     hostile in its radius, in the fight or not, and a chain (Chain Lightning, Multi-Shot,
 *     Avenger's Shield) jumps to whatever stands near enough. How far each one reaches is read
 *     from the server's spell data. Not judged on a target that isn't in combat itself: that is
 *     the pull, and its pack comes along whatever it is hit with.
 *   - A mob that is walking (a patrol, or one wandering round its spot) counts where it is and
 *     where it will be over the next yards of its way, for every walk and every spot a "move
 *     out" action looks at: a bot doesn't walk to where a patrol is about to arrive. Not for the
 *     step back out: a bot doesn't leave its spot for a patrol that may yet turn off.
 *   - A boss that isn't in the fight is given a wider berth than a pack: extra yards on top of
 *     its aggro radius, for everything above.
 *   - A bot whose walks keep being stopped (whatever sends it keeps sending it, and each try
 *     gets it a step further before it is caught) is held longer each time: 0.7 s, then 1.4,
 *     then 2.8, until it has gone a few seconds without being stopped.
 *   - A mob that throws players about (Common/Knockback.h) is not fought from a spot where the
 *     throw ends in one of those idle packs.
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

#include <memory>
#include <vector>

namespace G3D
{
    class Vector3;
}

namespace RaidClear::Leash
{
    void AddScripts();

    // For a "move out" action that looks for its own spot: what the leash would say to a walk
    // from the bot to that spot, straight or along a path (from the bot's feet to the spot, as
    // PathGenerator gives it). The idle mobs around the bot are listed once, when this is made,
    // so make one per search.
    class WalkCheck
    {
    public:
        WalkCheck(PlayerbotAI* botAI, Player* bot);
        ~WalkCheck();

        bool Pulls(float x, float y, float z) const;
        bool Pulls(std::vector<G3D::Vector3> const& path) const;

        // Yards between a spot and the edge of the nearest idle mob's aggro radius; negative
        // inside it. Mobs on another level, and mobs that are close but can't see the spot,
        // don't count. NO_MOBS with none around (or with the leash off).
        // Without `sight`, every mob on the level counts, seen or not.
        static constexpr float NO_MOBS = 1000.0f;
        float Gap(float x, float y, float z, bool sight = true) const;

    private:
        struct Mobs;
        Player* _bot;
        std::unique_ptr<Mobs> _mobs;  // none: the leash is off for this bot
    };
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

// No spell that hits more than its target where it would reach a mob that isn't in the fight.
class RcLeashSplashMultiplier : public Multiplier
{
public:
    RcLeashSplashMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc leash splash") {}
    float GetValue(Action* action) override;

private:
    struct Idle
    {
        ObjectGuid guid;
        float x;
        float y;
        float z;
        float reach;
    };
    // The idle mobs near the bot, looked up a few times a second, not for every spell it weighs.
    std::vector<Idle> _idle;
    uint32 _idleAtMs = 0;
    uint32 _idleInstance = 0;
};

#endif
