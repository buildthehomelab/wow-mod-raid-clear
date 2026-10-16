/*
 * mod-raid-clear: Ruins of Ahn'Qiraj, Kurinnaxx. See AqRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"

#include "Creature.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <list>

using namespace RaidClear::RuinsOfAhnQiraj;

namespace
{
    Creature* FightingKurinnaxx(Player* bot) { return RaidClear::FightingCreature(bot, { NPC_KURINNAXX }); }

    // What goes off reaches this far round the trap, centre to centre (8 yd on a stock server).
    float TrapRadius()
    {
        static float const radius = RaidClear::AhnQiraj::SpellRadius(SPELL_SAND_TRAP, 8.0f);
        return radius;
    }

    // Whether the trap has yet to go off. The summon gives it its whole life as its respawn time,
    // so it appeared that long before the respawn time; it goes off SAND_TRAP_FUSE_S later.
    bool Live(GameObject const* trap)
    {
        if (!trap->IsInWorld() || !trap->isSpawned())
            return false;
        // The trap deletes itself when it goes off, which also clears its respawn time; it stays
        // in the world until the end of that update.
        time_t const ends = trap->GetRespawnTime();
        if (!ends)
            return false;

        static int64 const lifeS = []
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_SAND_TRAP_SUMMON);
            int32 const ms = info ? info->GetDuration() : 0;
            return int64(ms > 0 ? ms / IN_MILLISECONDS : 60);
        }();
        int64 const ageS = int64(GameTime::GetGameTime().count()) - (int64(ends) - lifeS);
        return ageS <= int64(SAND_TRAP_FUSE_S + SAND_TRAP_SLACK_S);
    }

    struct Trap
    {
        float x;
        float y;
    };

    // The live traps in the bot's copy of the room, looked up again after TRAPS_FRESH_MS: the
    // triggers and the hold multiplier of the whole raid ask many times a second. One instance's
    // bots update one after another on one thread.
    constexpr uint32 TRAPS_FRESH_MS = 200;
    std::vector<Trap> const& LiveTraps(Player* bot, Creature const* boss)
    {
        thread_local uint32 tInstanceId = 0;
        thread_local uint32 tAtMs = 0;
        thread_local std::vector<Trap> tTraps;
        uint32 const now = getMSTime();
        if (tInstanceId != bot->GetInstanceId() || !tAtMs || getMSTimeDiff(tAtMs, now) >= TRAPS_FRESH_MS)
        {
            tTraps.clear();
            std::list<GameObject*> traps;
            boss->GetGameObjectListWithEntryInGrid(traps, GO_SAND_TRAP, SAND_TRAP_SEARCH);
            for (GameObject const* trap : traps)
                if (Live(trap))
                    tTraps.push_back({ trap->GetPositionX(), trap->GetPositionY() });
            tInstanceId = bot->GetInstanceId();
            tAtMs = now ? now : 1;
        }
        return tTraps;
    }

    // Yards from a spot to the nearest live trap, centre to centre.
    float TrapGap(std::vector<Trap> const& traps, float x, float y)
    {
        float gap = 1000.0f;
        for (Trap const& trap : traps)
            gap = std::min(gap, std::hypot(trap.x - x, trap.y - y));
        return gap;
    }

    float TrapGap(std::vector<Trap> const& traps, Position const* pos)
    {
        return TrapGap(traps, pos->GetPositionX(), pos->GetPositionY());
    }
}

bool RcAq20SandTrapTrigger::IsActive()
{
    // A bot that is feared or rooted isn't going anywhere.
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    Creature* boss = FightingKurinnaxx(bot);
    if (!boss)
        return false;
    return TrapGap(LiveTraps(bot, boss), bot) < TrapRadius() + SAND_TRAP_KEEP_EXTRA;
}

bool RcAq20LeaveSandTrapAction::Execute(Event /*event*/)
{
    Creature* boss = FightingKurinnaxx(bot);
    if (!boss)
        return false;
    std::vector<Trap> const traps = LiveTraps(bot, boss);
    float const radius = TrapRadius();
    float const keepAway = radius + SAND_TRAP_KEEP_EXTRA;
    if (TrapGap(traps, bot) >= keepAway)
        return false;
    auto const gapAt = [&traps](float x, float y, float /*z*/) { return TrapGap(traps, x, y); };
    // Up to the whole trap across from the middle of it.
    return _walk.Go(gapAt, keepAway, radius + SAND_TRAP_SPOT_EXTRA, SAND_TRAP_WORTH_IT,
                    radius + SAND_TRAP_SPOT_EXTRA + 3.0f, traps.size() == 1);
}

float RcAq20SandTrapHoldMultiplier::GetValue(Action* action)
{
    Unit* target = RaidClear::AhnQiraj::WalkTarget(action, bot);
    if (!target)
        return 1.0f;
    Creature* boss = FightingKurinnaxx(bot);
    if (!boss)
        return 1.0f;

    std::vector<Trap> const& traps = LiveTraps(bot, boss);
    float const radius = TrapRadius();
    if (TrapGap(traps, bot) >= radius + SAND_TRAP_HOLD_EXTRA)
        return 1.0f;
    return TrapGap(traps, target) < radius + SAND_TRAP_KEEP_EXTRA ? 0.0f : 1.0f;
}
