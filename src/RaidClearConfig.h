/*
 * mod-raid-clear: config values, read on every config load.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_CONFIG_H
#define MOD_RAID_CLEAR_CONFIG_H

#include "Define.h"

#include <string>
#include <unordered_map>

namespace RaidClear
{
    struct Config
    {
        bool enable = true;
        bool killOrder = true;
        bool assignMainTank = true;
        bool tankSplit = true;
        float tankSeparation = 12.0f;
        bool separateOnBosses = false;
        bool deathLog = true;
        std::unordered_map<uint32, bool> raidEnabled;  // mapId -> RaidClear.<Raid>.Enable

        bool IsRaidEnabled(uint32 mapId) const
        {
            if (!enable)
                return false;
            auto it = raidEnabled.find(mapId);
            return it != raidEnabled.end() && it->second;
        }
    };

    Config const& GetConfig();
    void LoadConfig();
}

#endif
