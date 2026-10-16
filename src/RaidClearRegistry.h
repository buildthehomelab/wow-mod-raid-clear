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
        // A second strategy for both engines, or nullptr: what has to happen before anything
        // is fighting the raid (Blackwing Lair's technicians running from Vaelastrasz's room).
        char const* earlyStrategy = nullptr;
    };

    // Main tank / off-tank split, installed on every raid map (see Common/TankRoles.h).
    inline constexpr char const* TANKS_STRATEGY = "rc raid tanks";
    // Keeps bots from backing into packs that aren't in the fight (see Common/AggroLeash.h).
    inline constexpr char const* LEASH_STRATEGY = "rc raid leash";
    // Who starts a fight and who stays out of melee (see Common/RaidDiscipline.h). On the
    // non-combat engine too: a pull starts from there.
    inline constexpr char const* DISCIPLINE_STRATEGY = "rc raid discipline";

    inline constexpr std::array<RaidEntry, 6> Raids = {{
        { 409, "rc moltencore", "MoltenCore" },
        { 469, "rc bwl", "BlackwingLair", "rc bwl runners" },
        { 309, "rc zg", "ZulGurub" },
        { 249, "rc ony", "OnyxiasLair" },
        { 509, "rc aq20", "RuinsOfAhnQiraj" },
        { 531, "rc aq40", "TempleOfAhnQiraj" },
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
