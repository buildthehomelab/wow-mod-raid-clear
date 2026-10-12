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
        constexpr float SEARCH_RANGE = 250.0f;

        // One slot per boss asked for on this map thread; a raid asks for one or two.
        struct Slot
        {
            bool valid = false;
            uint32 instanceId = 0;
            uint32 entry = 0;  // the first entry asked for
            uint32 ms = 0;
            ObjectGuid guid;
        };
        thread_local std::array<Slot, 4> tSlots;
        thread_local uint8 tNextSlot = 0;

        bool Matches(Creature const* creature, std::initializer_list<uint32> entries)
        {
            return creature && std::find(entries.begin(), entries.end(), creature->GetEntry()) != entries.end();
        }
    }

    Creature* FightingCreature(Player* bot, std::initializer_list<uint32> entries)
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
        if (slot && getMSTimeDiff(slot->ms, now) <= CACHE_MS)
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
            }
            slot->valid = true;
            slot->instanceId = map->GetInstanceId();
            slot->entry = key;
            slot->ms = now;
            slot->guid = creature ? creature->GetGUID() : ObjectGuid::Empty;
        }
        return creature && creature->IsAlive() && creature->IsInCombat() ? creature : nullptr;
    }
}
