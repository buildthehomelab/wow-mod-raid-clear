/*
 * mod-raid-clear: finding a boss in the bot's instance. See BossLookup.h.
 *
 * Released under the MIT License.
 */

#include "BossLookup.h"

#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"

#include <algorithm>
#include <array>

namespace RaidClear
{
    namespace
    {
        constexpr uint32 CACHE_MS = 1000;
        // A boss that isn't on the map at all (killed in an earlier visit) is looked for less often.
        // One that was there a moment ago may be being respawned (a new object, up to 5 s later), so
        // it keeps the short interval for a while.
        constexpr uint32 RESPAWN_WATCH_MS = 15000;
        constexpr uint32 MISSING_CACHE_MS = 3000;
        constexpr float SEARCH_RANGE = 250.0f;

        // One slot per creature asked for on this map thread; Blackwing Lair asks for seven, the
        // Temple of Ahn'Qiraj about a dozen (two of its instances on one thread share them).
        struct Slot
        {
            bool valid = false;
            uint32 instanceId = 0;
            uint32 entry = 0;  // the first entry asked for
            uint32 ms = 0;
            ObjectGuid guid;
            uint32 seenMs = 0;  // when it was last found here; 0 = never
        };
        thread_local std::array<Slot, 32> tSlots;
        thread_local uint8 tNextSlot = 0;

        bool Matches(Creature const* creature, std::initializer_list<uint32> entries)
        {
            return creature && std::find(entries.begin(), entries.end(), creature->GetEntry()) != entries.end();
        }
    }

    Creature* FightingCreature(Player* bot, std::initializer_list<uint32> entries)
    {
        Creature* creature = InstanceCreature(bot, entries);
        return creature && creature->IsAlive() && creature->IsInCombat() ? creature : nullptr;
    }

    Creature* InstanceCreature(Player* bot, std::initializer_list<uint32> entries)
    {
        Map* map = bot->GetMap();
        if (!map || entries.size() == 0)
            return nullptr;

        uint32 const key = *entries.begin();
        uint32 const now = getMSTime();
        Slot* slot = nullptr;
        for (Slot& candidate : tSlots)
            if (candidate.valid && candidate.instanceId == map->GetInstanceId() && candidate.entry == key)
                slot = &candidate;

        Creature* creature = nullptr;
        bool const longWait = slot && slot->guid.IsEmpty() &&
                              (!slot->seenMs || getMSTimeDiff(slot->seenMs, now) > RESPAWN_WATCH_MS);
        if (slot && getMSTimeDiff(slot->ms, now) <= (longWait ? MISSING_CACHE_MS : CACHE_MS))
        {
            creature = slot->guid.IsEmpty() ? nullptr : map->GetCreature(slot->guid);
        }
        else
        {
            for (auto const& [spawnId, spawned] : map->GetCreatureBySpawnIdStore())
            {
                if (Matches(spawned, entries))
                {
                    creature = spawned;
                    break;
                }
            }
            // Not a database spawn (a module that summons the boss): look around instead.
            for (auto it = entries.begin(); !creature && it != entries.end(); ++it)
                creature = bot->FindNearestCreature(*it, SEARCH_RANGE);

            if (!slot)
            {
                slot = &tSlots[tNextSlot];
                tNextSlot = (tNextSlot + 1) % tSlots.size();
                slot->seenMs = 0;
            }
            slot->valid = true;
            slot->instanceId = map->GetInstanceId();
            slot->entry = key;
            slot->ms = now;
            slot->guid = creature ? creature->GetGUID() : ObjectGuid::Empty;
            if (creature)
                slot->seenMs = now ? now : 1;
        }
        return creature;
    }
}
