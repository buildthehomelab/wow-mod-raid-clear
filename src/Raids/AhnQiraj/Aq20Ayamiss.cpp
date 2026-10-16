/*
 * mod-raid-clear: Ruins of Ahn'Qiraj, Ayamiss the Hunter. See AqRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"

#include "Creature.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <list>

using namespace RaidClear::RuinsOfAhnQiraj;

namespace
{
    Creature* FightingAyamiss(Player* bot) { return RaidClear::FightingCreature(bot, { NPC_AYAMISS }); }

    bool IsLarva(Unit const* unit) { return unit && unit->GetEntry() == NPC_HIVEZARA_LARVA; }

    // A bot that goes for the larvae: neither a tank nor a healer.
    bool Hunts(Player* bot) { return !PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsHeal(bot); }

    float AltarGap(Unit const* unit) { return std::hypot(unit->GetPositionX() - ALTAR_X, unit->GetPositionY() - ALTAR_Y); }

    // The living larvae round her, nearest the altar first, looked up again after
    // LARVAE_FRESH_MS: the trigger and the multiplier of the whole raid ask many times a second.
    // One instance's bots update one after another on one thread. GUIDs, not pointers: a larva
    // that eats its player is gone before the next look.
    constexpr uint32 LARVAE_FRESH_MS = 250;
    std::vector<ObjectGuid> const& Larvae(Player* bot, Creature const* boss)
    {
        thread_local uint32 tInstanceId = 0;
        thread_local uint32 tAtMs = 0;
        thread_local std::vector<ObjectGuid> tLarvae;
        uint32 const now = getMSTime();
        if (tInstanceId != bot->GetInstanceId() || !tAtMs || getMSTimeDiff(tAtMs, now) >= LARVAE_FRESH_MS)
        {
            std::list<Creature*> found;
            boss->GetCreatureListWithEntryInGrid(found, NPC_HIVEZARA_LARVA, LARVA_SEARCH);
            std::vector<Creature*> alive;
            for (Creature* larva : found)
                if (larva->IsAlive())
                    alive.push_back(larva);
            std::sort(alive.begin(), alive.end(),
                      [](Creature const* a, Creature const* b) { return AltarGap(a) < AltarGap(b); });
            tLarvae.clear();
            for (Creature const* larva : alive)
                tLarvae.push_back(larva->GetGUID());
            tInstanceId = bot->GetInstanceId();
            tAtMs = now ? now : 1;
        }
        return tLarvae;
    }

    // The larva this bot goes for: the one nearest the altar that it is close enough to, can see
    // and can attack.
    Creature* LarvaFor(Player* bot, Creature const* boss)
    {
        for (ObjectGuid const& guid : Larvae(bot, boss))
        {
            Creature* larva = ObjectAccessor::GetCreature(*bot, guid);
            if (!larva || !larva->IsAlive() || bot->GetExactDist(larva) > LARVA_JOIN ||
                !bot->IsValidAttackTarget(larva) || !bot->IsWithinLOSInMap(larva))
                continue;
            return larva;
        }
        return nullptr;
    }
}

bool RcAq20LarvaTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !Hunts(bot))
        return false;
    Creature* boss = FightingAyamiss(bot);
    if (!boss)
        return false;
    // Already on one: playerbots keeps hitting it.
    Unit* current = AI_VALUE(Unit*, "current target");
    if (IsLarva(current) && current->IsAlive())
        return false;
    return LarvaFor(bot, boss) != nullptr;
}

bool RcAq20KillLarvaAction::Execute(Event /*event*/)
{
    Creature* boss = FightingAyamiss(bot);
    if (!boss)
        return false;
    Creature* larva = LarvaFor(bot, boss);
    return larva && Attack(larva);
}

float RcAq20LarvaMultiplier::GetValue(Action* action)
{
    if (!dynamic_cast<AttackAction*>(action) || dynamic_cast<RcAq20KillLarvaAction*>(action))
        return 1.0f;
    if (!bot->IsInCombat() || !Hunts(bot))
        return 1.0f;
    Creature* boss = FightingAyamiss(bot);
    if (!boss || !LarvaFor(bot, boss))
        return 1.0f;
    return IsLarva(action->GetTarget()) ? 1.0f : 0.0f;
}
