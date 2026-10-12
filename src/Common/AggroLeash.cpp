/*
 * mod-raid-clear: bots don't back into packs that aren't in the fight. See AggroLeash.h.
 *
 * Released under the MIT License.
 */

#include "AggroLeash.h"

#include "RaidClearConfig.h"
#include "Common/TankRoles.h"

#include "Creature.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace RaidClear;

void RaidClearLeashStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Above every "move out" there is (the raid mechanics sit at ACTION_RAID, the life-savers
    // at ACTION_EMERGENCY + 5 or 6): a second pack costs more than whatever the bot was dodging.
    triggers.push_back(new TriggerNode("rc aggro leash", { NextAction("rc aggro leash", ACTION_EMERGENCY + 8) }));
}

namespace
{
    // Mobs further away than this can't matter to where the bot is or is going.
    constexpr float SCAN_RANGE = 75.0f;
    // A mob more than this above or below doesn't aggro (the core allows 3 yd).
    constexpr float AGGRO_HEIGHT = 6.0f;
    // A walk is sampled this often along the way.
    constexpr float PATH_STEP = 2.0f;
    // After a walk is stopped the bot stands still this long, so the action that sent it doesn't
    // send it again on the next update.
    constexpr float HOLD_MS = 1500.0f;
    // A spot more than this above or below the bot is a ledge or a pit.
    constexpr float SPOT_HEIGHT = 5.0f;
    constexpr uint32 IDLE_CHECK_MS = 300;

    // A mob that would come if the bot got close enough.
    struct IdleMob
    {
        Creature* creature;
        float x;
        float y;
        float z;
        float aggro;  // its aggro radius for this bot
    };

    std::vector<IdleMob> IdleMobs(PlayerbotAI* botAI, Player* bot)
    {
        std::vector<IdleMob> mobs;
        GuidVector const units = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets no los")->Get();
        for (ObjectGuid const& guid : units)
        {
            if (!guid.IsCreature())
                continue;  // pets, totems, vehicles and players never pull
            Unit* unit = botAI->GetUnit(guid);
            Creature* creature = unit ? unit->ToCreature() : nullptr;
            if (!creature || !creature->IsAlive() || creature->IsInCombat() || creature->IsInEvadeMode())
                continue;
            if (!creature->HasReactState(REACT_AGGRESSIVE) || creature->IsCivilian() || creature->IsNeutralToAll() ||
                creature->GetCharmerOrOwnerGUID().IsPlayer() || !creature->IsHostileTo(bot))
                continue;
            if (bot->GetExactDist2d(creature) > SCAN_RANGE)
                continue;

            mobs.push_back({ creature, creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(),
                             creature->GetAggroRange(bot) });
        }
        return mobs;
    }

    // Yards between a spot and the edge of the nearest mob's aggro radius; negative inside it.
    // Mobs on another level don't count.
    float Clearance(std::vector<IdleMob> const& mobs, float x, float y, float z, IdleMob const** nearest = nullptr)
    {
        float clearance = 1000.0f;
        for (IdleMob const& mob : mobs)
        {
            if (std::fabs(mob.z - z) > AGGRO_HEIGHT)
                continue;
            float const gap = std::hypot(x - mob.x, y - mob.y) - mob.aggro;
            if (gap < clearance)
            {
                clearance = gap;
                if (nearest)
                    *nearest = &mob;
            }
        }
        return clearance;
    }

    // What the bot is attacking or about to attack.
    Unit* TargetOf(PlayerbotAI* botAI, Player* bot)
    {
        if (Unit* victim = bot->GetVictim())
            return victim;
        return botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    }

    enum class Verdict
    {
        Fine,
        Stop,      // the walk would end or pass too close to a mob
        StepBack,  // the bot is standing too close to one
    };

    Verdict Judge(PlayerbotAI* botAI, Player* bot, std::vector<IdleMob> const& mobs)
    {
        if (mobs.empty())
            return Verdict::Fine;

        float const buffer = GetConfig().aggroLeashBuffer;
        float const botX = bot->GetPositionX();
        float const botY = bot->GetPositionY();
        float const botZ = bot->GetPositionZ();
        Unit* target = TargetOf(botAI, bot);
        if (target && !target->IsAlive())
            target = nullptr;

        // Attacking a mob that isn't in combat yet is a pull: the bot is meant to go there.
        if (target)
            for (IdleMob const& mob : mobs)
                if (mob.creature == target)
                    return Verdict::Fine;

        // A walk toward its own target is going to the fight.
        bool const moving = !bot->movespline->Finalized();
        G3D::Vector3 const dest = moving ? bot->movespline->FinalDestination() : G3D::Vector3(botX, botY, botZ);
        if (moving && target && target->GetExactDist2d(dest.x, dest.y) < target->GetExactDist2d(botX, botY) - 1.0f)
            return Verdict::Fine;

        // Standing at the edge of a mob's aggro radius: half the buffer, so a bot that was stopped
        // at the full buffer isn't sent back and forth. Not a bot in melee with its target, and
        // not one that is already on its way out.
        IdleMob const* nearest = nullptr;
        if (Clearance(mobs, botX, botY, botZ, &nearest) < buffer * 0.5f && nearest &&
            !(target && bot->IsWithinMeleeRange(target)) &&
            !(moving && Clearance(mobs, dest.x, dest.y, dest.z) >= buffer) &&
            nearest->creature->IsWithinLOSInMap(bot))
            return Verdict::StepBack;

        if (!moving)
            return Verdict::Fine;

        float const length = std::hypot(dest.x - botX, dest.y - botY);
        int const steps = std::max(1, int(std::ceil(length / PATH_STEP)));
        for (int i = 1; i <= steps; ++i)
        {
            float const t = float(i) / float(steps);
            float const x = botX + (dest.x - botX) * t;
            float const y = botY + (dest.y - botY) * t;
            float const z = botZ + (dest.z - botZ) * t;
            for (IdleMob const& mob : mobs)
            {
                if (std::fabs(mob.z - z) > AGGRO_HEIGHT)
                    continue;
                float const there = std::hypot(x - mob.x, y - mob.y);
                // Only a walk that takes the bot closer to the mob than it already is.
                if (there < mob.aggro + buffer && there < std::hypot(botX - mob.x, botY - mob.y) - 0.5f &&
                    mob.creature->IsWithinLOS(x, y, z + 2.0f))
                    return Verdict::Stop;
            }
        }
        return Verdict::Fine;
    }
}

bool RcAggroLeashTrigger::IsActive()
{
    // A bot that is feared or rooted isn't choosing where it goes.
    if (!GetConfig().aggroLeash || !bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;

    // A bot standing still can only be walked up to; that doesn't need looking at every update.
    bool const moving = !bot->movespline->Finalized();
    uint32 const now = getMSTime();
    if (!moving && getMSTimeDiff(_lastIdleCheckMs, now) < IDLE_CHECK_MS)
        return _lastIdleResult;

    bool const active = Judge(botAI, bot, IdleMobs(botAI, bot)) != Verdict::Fine;
    if (!moving)
    {
        _lastIdleCheckMs = now;
        _lastIdleResult = active;
    }
    return active;
}

bool RcAggroLeashAction::Execute(Event /*event*/)
{
    std::vector<IdleMob> const mobs = IdleMobs(botAI, bot);
    Verdict const verdict = Judge(botAI, bot, mobs);
    if (verdict == Verdict::Fine)
        return false;

    LastMovement& last = AI_VALUE(LastMovement&, "last movement");
    if (verdict == Verdict::Stop)
    {
        bot->StopMoving();
        last.clear();
        SetNextMovementDelay(HOLD_MS);
        return true;
    }

    // Step back: the nearest spot that is clear of every idle mob by the full buffer, on level
    // floor the bot can see. Among equally near ones, the one closest to the main tank.
    float const buffer = GetConfig().aggroLeashBuffer;
    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    float const botZ = bot->GetPositionZ();
    Player* mainTank = Tanks::ActingMainTank(bot->GetGroup());
    Map* map = bot->GetMap();

    constexpr int STEPS = 16;
    for (float ring = 3.0f; ring <= 21.0f; ring += 3.0f)
    {
        bool found = false;
        float bestScore = 0.0f;
        float bestX = 0.0f, bestY = 0.0f, bestZ = 0.0f;
        for (int i = 0; i < STEPS; ++i)
        {
            float const angle = float(i) * 2.0f * float(M_PI) / float(STEPS);
            float const x = botX + std::cos(angle) * ring;
            float const y = botY + std::sin(angle) * ring;
            float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + SPOT_HEIGHT);
            if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > SPOT_HEIGHT)
                continue;
            if (Clearance(mobs, x, y, z) < buffer + 1.0f)
                continue;

            float const score = mainTank && mainTank != bot ? mainTank->GetExactDist2d(x, y) : 0.0f;
            if (found && score >= bestScore)
                continue;
            if (!bot->IsWithinLOS(x, y, z + 2.0f))
                continue;

            found = true;
            bestScore = score;
            bestX = x;
            bestY = y;
            bestZ = z;
        }
        if (found)
        {
            last.clear();
            return MoveTo(bot->GetMapId(), bestX, bestY, bestZ, false, false, false, false,
                          MovementPriority::MOVEMENT_FORCED);
        }
    }
    return false;
}
