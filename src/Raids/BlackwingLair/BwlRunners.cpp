/*
 * mod-raid-clear: Blackwing Lair, the technicians that run from Vaelastrasz's room. See
 * BwlRaidClear.h.
 *
 * Six Blackwing Technicians stand round Vaelastrasz, immune to everything, until a player walks
 * through the door (areatrigger 3626; a client reports it, so the bots' own arrival doesn't set
 * them off). Then they turn passive and run: two ways round the room, up a ramp each, along the
 * gallery and out through the door to the Halls of Strife, where they despawn for the week.
 * That takes them about 24 seconds at 8 yd a second, a yard faster than a player runs. Being hit
 * stops one for five minutes (SmartAI: pause the path, attack), and it stands and throws Bombs
 * like any other technician.
 *
 * Released under the MIT License.
 */

#include "BwlRaidClear.h"

#include "Common/BossLookup.h"

#include "Creature.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RaidClear::BlackwingLair;

namespace
{
    // The six by spawn id (creature.guid), not by entry: sixty more of the same entry stand in
    // the packs on the floors above, some of them straight overhead.
    constexpr std::array<uint32, 6> RUNNER_SPAWNS = { 84603, 84605, 84606, 84614, 84615, 84616 };

    // His room with its gallery and the way in from Razorgore. The Suppression Room overlaps it
    // on the map and lies above 440.
    bool InVaelastraszRoom(Player const* bot)
    {
        float const x = bot->GetPositionX();
        float const y = bot->GetPositionY();
        float const z = bot->GetPositionZ();
        return x > -7600.0f && x < -7400.0f && y > -1110.0f && y < -930.0f && z > 395.0f && z < 438.0f;
    }

    // On its way out and not stopped by anyone yet. One that was stopped and then got away from
    // whoever stopped it runs again. Passive is what its path makes it: one that is standing
    // about for any other reason is a pull like any other.
    bool IsRunning(Creature const* technician)
    {
        return technician && technician->IsInWorld() && technician->IsAlive() && !technician->IsInCombat() &&
               !technician->IsInEvadeMode() && !technician->IsImmuneToPC() &&
               technician->HasReactState(REACT_PASSIVE) && !technician->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) &&
               !technician->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
    }

    // One of the six, still running.
    bool IsRunner(Unit const* unit)
    {
        Creature const* creature = unit ? unit->ToCreature() : nullptr;
        return creature &&
               std::find(RUNNER_SPAWNS.begin(), RUNNER_SPAWNS.end(), creature->GetSpawnId()) != RUNNER_SPAWNS.end() &&
               IsRunning(creature);
    }

    bool SameFloor(Player const* bot, Creature const* runner)
    {
        return std::fabs(runner->GetPositionZ() - bot->GetPositionZ()) <= RUNNER_FLOOR;
    }

    Creature* RunnerAt(Map* map, size_t slot)
    {
        Map::CreatureBySpawnIdContainer const& spawns = map->GetCreatureBySpawnIdStore();
        auto const it = spawns.find(RUNNER_SPAWNS[slot]);
        return it != spawns.end() ? it->second : nullptr;
    }

    struct Pick
    {
        Creature* hit = nullptr;   // running, in reach and in sight (and, for melee, on the bot's floor)
        Creature* walk = nullptr;  // none is: the nearest one still running
    };

    // Every bot starts looking at a different technician, so the first volley stops several of
    // them and not the nearest one six times over. A bot stays on the one it has while that one
    // is still running. Melee leave one on the gallery overhead to the casters: attacking it
    // would send them the long way round after it.
    Pick Choose(Player* bot, Unit* current)
    {
        Pick pick;
        Map* map = bot->GetMap();
        if (!map)
            return pick;

        bool const ranged = PlayerbotAI::IsRanged(bot);
        float walkDist = 0.0f;
        size_t const first = bot->GetGUID().GetCounter() % RUNNER_SPAWNS.size();
        for (size_t i = 0; i < RUNNER_SPAWNS.size(); ++i)
        {
            Creature* runner = RunnerAt(map, (first + i) % RUNNER_SPAWNS.size());
            if (!IsRunning(runner) || !bot->IsValidAttackTarget(runner))
                continue;

            float const dist = bot->GetDistance(runner);
            // The one the bot is on gets a few yards more, or a bot at the edge of its reach
            // would take it and drop it by turns.
            float const reach = runner == current ? RUNNER_KEEP : RUNNER_REACH;
            if (dist <= reach && (ranged || SameFloor(bot, runner)) && bot->IsWithinLOSInMap(runner))
            {
                if (!pick.hit || runner == current)
                    pick.hit = runner;
                if (runner == current)
                    break;
                continue;
            }
            if (!pick.walk || dist < walkDist)
            {
                pick.walk = runner;
                walkDist = dist;
            }
        }
        return pick;
    }

    // Healers heal. And nobody leaves Vaelastrasz for them: with him awake every second counts,
    // and six technicians throwing Bombs into that fight are worse than six that got away.
    bool Catches(Player* bot)
    {
        return bot->IsAlive() && bot->GetMapId() == MAP_ID && InVaelastraszRoom(bot) && !PlayerbotAI::IsHeal(bot);
    }

    bool VaelastraszFighting(Player* bot)
    {
        return RaidClear::FightingCreature(bot, { NPC_VAELASTRASZ }) != nullptr;
    }

    // Whether this bot goes after a runner it can't hit from where it stands. The tanks don't:
    // the main tank is who mod-dungeon-clear walks the raid with.
    bool GoesAfter(Player* bot, Creature const* runner)
    {
        if (!runner || PlayerbotAI::IsTank(bot) || bot->GetDistance(runner) > RUNNER_CHASE)
            return false;
        return PlayerbotAI::IsRanged(bot) || SameFloor(bot, runner);
    }
}

void RaidClearBwlRunnersStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Above the rest of the raid rules and mod-dungeon-clear's own (65 at most outside chat
    // commands and loot rolls), below the ones that keep a bot alive.
    triggers.push_back(
        new TriggerNode("rc bwl fleeing technician", { NextAction("rc bwl catch technician", ACTION_RAID + 6) }));
}

void RaidClearBwlRunnersStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcBwlCatchTechnicianMultiplier(botAI));
}

bool RcBwlFleeingTechnicianTrigger::IsActive()
{
    if (!Catches(bot))
        return false;

    Unit* current = AI_VALUE(Unit*, "current target");
    bool const onRunner = IsRunner(current);
    Pick const pick = Choose(bot, current);
    if (!pick.hit && !pick.walk && !onRunner)
        return false;
    if (VaelastraszFighting(bot))
        return false;

    if (pick.hit)
        return pick.hit != current || botAI->GetState() != BOT_STATE_COMBAT;
    // Still on one that has got out of reach or sight: the bot lets go of it, or playerbots
    // walks it after its target all the way to the far door.
    return onRunner || GoesAfter(bot, pick.walk);
}

bool RcBwlCatchTechnicianAction::Execute(Event /*event*/)
{
    if (!Catches(bot) || VaelastraszFighting(bot))
        return false;

    Unit* current = AI_VALUE(Unit*, "current target");
    Pick const pick = Choose(bot, current);
    if (pick.hit)
    {
        // Still on its way to where a runner was: playerbots' attack doesn't stop a walk of
        // this priority, and a bot that is walking starts no cast and no shot.
        if (OwnWalk())
        {
            AI_VALUE(LastMovement&, "last movement").clear();
            bot->StopMoving();
            _walkAtMs = 0;
        }
        return Attack(pick.hit);
    }

    bool letGo = false;
    if (IsRunner(current))
    {
        context->GetValue<Unit*>("current target")->Set(nullptr);
        bot->SetTarget(ObjectGuid::Empty);
        bot->SetSelection(ObjectGuid::Empty);
        bot->AttackStop();
        bot->InterruptNonMeleeSpells(false);
        // And the walk after it, which would make the next one wait its turn.
        AI_VALUE(LastMovement&, "last movement").clear();
        bot->StopMoving();
        _walkAtMs = 0;
        letGo = true;
    }
    return (GoesAfter(bot, pick.walk) && GoAfter(pick.walk)) || letGo;
}

// Whether the walk playerbots has on record for this bot is the one this action sent it on.
bool RcBwlCatchTechnicianAction::OwnWalk()
{
    if (!_walkAtMs)
        return false;
    LastMovement& last = AI_VALUE(LastMovement&, "last movement");
    return last.lastMoveShort.GetExactDist(_walkTo.GetPositionX(), _walkTo.GetPositionY(), _walkTo.GetPositionZ()) <
           0.5f;
}

bool RcBwlCatchTechnicianAction::GoAfter(Creature* runner)
{
    // Sent a moment ago and still on that walk: nothing else this tick, so the bot doesn't stop
    // to cast at a technician that has already been stopped. Not on it any more: the walk ended,
    // was stopped (the aggro leash's own look at it) or something else moved the bot, and the
    // bot does what it would otherwise.
    uint32 const now = getMSTime();
    if (_walkAtMs && getMSTimeDiff(_walkAtMs, now) < RUNNER_WALK_INTERVAL_MS)
        return OwnWalk() && bot->isMoving();

    float x = runner->GetPositionX();
    float y = runner->GetPositionY();
    float z = runner->GetPositionZ();
    if (PlayerbotAI::IsRanged(bot))
    {
        // A caster only has to see it: toward it on the bot's own floor, and not underneath it.
        float const dx = bot->GetPositionX() - x;
        float const dy = bot->GetPositionY() - y;
        float const dist = std::hypot(dx, dy);
        bool const sameFloor = SameFloor(bot, runner);
        bool onGround = false;
        if (dist > RUNNER_STAND_OFF)
        {
            float const standX = x + dx / dist * RUNNER_STAND_OFF;
            float const standY = y + dy / dist * RUNNER_STAND_OFF;
            float const standZ =
                bot->GetMap()->GetHeight(bot->GetPhaseMask(), standX, standY, bot->GetPositionZ() + 3.0f);
            if (standZ > INVALID_HEIGHT && std::fabs(standZ - bot->GetPositionZ()) <= RUNNER_FLOOR)
            {
                x = standX;
                y = standY;
                z = standZ;
                onGround = true;
            }
        }
        // No such spot, or close enough on the map already. One on the gallery overhead comes
        // into view by itself; one on this floor is behind something, and the way to it is the
        // way round.
        if (!onGround && !sameFloor)
            return false;
    }

    // The walk this bot was sent on last would make the new one wait its turn. Anything else
    // in its place (the leash holding the bot where it stopped it) is left to playerbots.
    LastMovement& last = AI_VALUE(LastMovement&, "last movement");
    if (OwnWalk())
        last.clear();
    if (!MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
        return false;
    // Where playerbots put the end of it, which isn't always the height asked for.
    _walkAtMs = now ? now : 1;
    _walkTo.Relocate(last.lastMoveShort.GetPositionX(), last.lastMoveShort.GetPositionY(),
                     last.lastMoveShort.GetPositionZ());
    return true;
}

float RcBwlCatchTechnicianMultiplier::GetValue(Action* action)
{
    // A tank hits a runner with whatever reaches it from where the tank stands and doesn't
    // walk after it: the main tank is who mod-dungeon-clear walks the raid with, and the raid
    // follows it. (Charge still goes.)
    if (action && dynamic_cast<ReachTargetAction*>(action))
        return PlayerbotAI::IsTank(bot) && Catches(bot) && IsRunner(action->GetTarget()) &&
                       !VaelastraszFighting(bot)
                   ? 0.0f
                   : 1.0f;

    // Whatever picks a target: "dps assist", "tank assist", the off-tank's own picker.
    if (!action || !dynamic_cast<AttackAction*>(action) || dynamic_cast<RcBwlCatchTechnicianAction*>(action))
        return 1.0f;
    // A player's own order goes through; the bot stays on it if it is a runner.
    if (!Catches(bot) || action->getName() == "attack my target")
        return 1.0f;

    Pick const pick = Choose(bot, AI_VALUE(Unit*, "current target"));
    if (!pick.hit || VaelastraszFighting(bot))
        return 1.0f;
    // "melee" on the runner itself is the bot doing its job.
    return action->GetTarget() == pick.hit ? 1.0f : 0.0f;
}
