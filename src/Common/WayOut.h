/*
 * mod-raid-clear: the way out of something on the floor.
 *
 * Playerbots gets a bot away from something by sending it to the point straight behind it. In a
 * cave that point is as often as not inside a wall, in lava or in the next pack. The "move out"
 * actions here look all the way round the bot instead, for the nearest spot it can walk to: a
 * whole path over the floor that ends at the spot, isn't a detour, and that the aggro leash
 * wouldn't stop for heading into a pack that isn't in the fight.
 *
 * Each action says what it is getting away from with a `gapAt(x, y, z)`: the yards between a
 * spot and that thing (the edge of a blast, the nearest fire).
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_WAY_OUT_H
#define MOD_RAID_CLEAR_WAY_OUT_H

#include "Common/AggroLeash.h"

#include "LastMovementValue.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MovementActions.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Playerbots.h"
#include "Position.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>

namespace RaidClear
{
    // How far round a bot a way out is looked for unless the action asks for more, how far above
    // or below it a spot may be (more is a ledge or a pit), how far from the spot the walk there
    // may end, and how long a bot that found nowhere to go waits before it looks again.
    constexpr float WAY_OUT_SEARCH = 21.0f;
    constexpr float WAY_OUT_UP = 2.5f;
    constexpr float WAY_OUT_DOWN = 5.0f;
    constexpr float WAY_OUT_END_SLACK = 1.5f;
    constexpr uint32 WAY_OUT_RETRY_MS = 400;
    // A bot that was sent off and isn't running (the aggro leash stopped the walk, or it ended
    // short) isn't sent again sooner than this: the leash holds a bot it stopped for as long.
    constexpr uint32 WAY_OUT_RESEND_MS = 700;

    // Whether the bot is already running to somewhere `gapAt` is at least `want`.
    template <typename GapAt>
    bool OnItsWayOut(Player* bot, GapAt const& gapAt, float want)
    {
        if (!bot->isMoving() || bot->movespline->Finalized())
            return false;
        G3D::Vector3 const dest = bot->movespline->FinalDestination();
        return gapAt(dest.x, dest.y, dest.z) >= want;
    }

    // The nearest spot, all the way round the bot and from `nearest` to `search` yards from it,
    // where `gapAt` is at least `want`. With nowhere that good, the spot with the widest gap, if
    // that is at least `worthIt` yards more than the bot has where it stands.
    template <typename GapAt>
    bool FindWayOut(PlayerbotAI* botAI, Player* bot, GapAt const& gapAt, float want, float worthIt, Position& spot,
                    float search = WAY_OUT_SEARCH, float nearest = 3.0f)
    {
        Map* map = bot->GetMap();
        float const botX = bot->GetPositionX();
        float const botY = bot->GetPositionY();
        float const botZ = bot->GetPositionZ();
        Leash::WalkCheck const leash(botAI, bot);

        constexpr int STEPS = 16;
        bool found = false;
        // What a spot has to beat: good enough, or that much better than here.
        float bestGap = std::min(gapAt(botX, botY, botZ) + worthIt, want) - 0.01f;
        for (float ring = nearest; ring <= search && (!found || bestGap < want); ring += 3.0f)
        {
            for (int i = 0; i < STEPS; ++i)
            {
                float const angle = float(i) * 2.0f * float(M_PI) / float(STEPS);
                float const x = botX + std::cos(angle) * ring;
                float const y = botY + std::sin(angle) * ring;
                // The cheap test first: most of the circle is no better than where the bot is.
                if (gapAt(x, y, botZ) <= bestGap)
                    continue;

                // The floor at the bot's own level: asked from higher up, a ledge over it answers.
                float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + WAY_OUT_UP);
                if (z <= INVALID_HEIGHT || z < botZ - WAY_OUT_DOWN)
                    continue;

                PathGenerator path(bot);
                // A whole path over the floor: not one that stops short, and not the straight line
                // the core hands back when it finds no floor under either end.
                constexpr uint32 NOT_A_WALK = PATHFIND_INCOMPLETE | PATHFIND_NOPATH | PATHFIND_SHORTCUT |
                                              PATHFIND_NOT_USING_PATH | PATHFIND_FARFROMPOLY;
                if (!path.CalculatePath(x, y, z) || !(path.GetPathType() & PATHFIND_NORMAL) ||
                    (path.GetPathType() & NOT_A_WALK))
                    continue;
                G3D::Vector3 const& end = path.GetActualEndPosition();
                if (std::hypot(end.x - x, end.y - y) > WAY_OUT_END_SLACK || path.getPathLength() > ring * 1.5f + 2.0f)
                    continue;
                float const gap = gapAt(end.x, end.y, end.z);
                if (gap <= bestGap || leash.Pulls(path.GetPath()))
                    continue;

                found = true;
                bestGap = gap;
                spot.Relocate(end.x, end.y, end.z);
            }
        }
        return found;
    }

    // The walk itself, for an action to keep as a member: finds the way out and sends the bot
    // along it, and remembers when it last did. Not the action: playerbots' own strategies switch
    // off every movement action but theirs while some mechanics run (Baron Geddon's Inferno), and
    // an action that only owns one of these isn't a movement action to them.
    class WayOutWalk : public MovementAction
    {
    public:
        WayOutWalk(PlayerbotAI* botAI) : MovementAction(botAI, "rc way out") {}
        bool Execute(Event /*event*/) override { return false; }

        // For a bot closer than `keepAway` to whatever `gapAt` measures. True when the bot is on
        // its way to a spot at least that far, or has just been sent to one at least `want` away
        // (or the best there is, see FindWayOut): nothing else should run this tick, so no cast
        // or potion stops the run.
        // `onePoint`: what the bot is getting away from is a single point, so a spot r yards from
        // the bot is at most r yards further from it than the bot is. The rings too close to
        // reach `want` are then left for last: each spot looked at costs a path.
        template <typename GapAt>
        bool Go(GapAt const& gapAt, float keepAway, float want, float worthIt, float search = WAY_OUT_SEARCH,
                bool onePoint = false)
        {
            if (OnItsWayOut(bot, gapAt, keepAway))
                return true;

            // Sent off a moment ago and not running: that walk was stopped (the aggro leash's own
            // look at it) or ended short. Found nowhere to go a moment ago: nothing has changed.
            uint32 const now = getMSTime();
            if ((_sentAtMs && getMSTimeDiff(_sentAtMs, now) < WAY_OUT_RESEND_MS) ||
                (_noSpotAtMs && getMSTimeDiff(_noSpotAtMs, now) < WAY_OUT_RETRY_MS))
                return false;

            Position spot;
            float nearest = 3.0f;
            if (onePoint)
            {
                float const shortBy = want - gapAt(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
                nearest = std::max(3.0f, std::floor(shortBy / 3.0f) * 3.0f);
            }
            if (!FindWayOut(botAI, bot, gapAt, want, worthIt, spot, search, nearest) &&
                (nearest <= 3.0f || !FindWayOut(botAI, bot, gapAt, want, worthIt, spot, nearest - 3.0f)))
            {
                _noSpotAtMs = now ? now : 1;
                return false;
            }

            // Whatever the bot was told last (a walk to its target, a cast to stand still for)
            // would make this walk wait its turn. Straight to the spot the path was checked for:
            // playerbots' own planning could settle for another.
            AI_VALUE(LastMovement&, "last movement").clear();
            bot->CastStop();
            if (!MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false,
                        false, true, MovementPriority::MOVEMENT_FORCED))
            {
                _noSpotAtMs = now ? now : 1;
                return false;
            }
            _sentAtMs = now ? now : 1;
            _noSpotAtMs = 0;
            return true;
        }

    private:
        uint32 _sentAtMs = 0;
        uint32 _noSpotAtMs = 0;
    };
}

#endif
