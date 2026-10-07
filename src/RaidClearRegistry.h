/*
 * mod-raid-clear: which raid strategy belongs to which map.
 *
 * Every supported raid has one combat strategy, named "rc <raid>". The gate installs it on
 * every bot that is on the raid's map and strips it everywhere else. It runs alongside
 * mod-playerbots' own raid strategy for that map (e.g. "moltencore"), never instead of it:
 * mod-raid-clear only adds the mechanics playerbots doesn't handle.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_REGISTRY_H
#define MOD_RAID_CLEAR_REGISTRY_H

#include "Define.h"

#include <array>

namespace RaidClear
{
    struct RaidEntry
    {
        uint32 mapId;
        char const* strategy;  // combat-engine strategy name
        char const* confKey;   // RaidClear.<confKey>.Enable
    };

    // Main tank / off-tank split, installed on every raid map (see Common/TankRoles.h).
    inline constexpr char const* TANKS_STRATEGY = "rc raid tanks";

    inline constexpr std::array<RaidEntry, 2> Raids = {{
        { 409, "rc moltencore", "MoltenCore" },
        { 469, "rc bwl", "BlackwingLair" },
    }};

    // The raid entry for a map, or nullptr when mod-raid-clear has nothing for it.
    inline RaidEntry const* FindRaid(uint32 mapId)
    {
        for (RaidEntry const& raid : Raids)
            if (raid.mapId == mapId)
                return &raid;
        return nullptr;
    }
}

#endif
