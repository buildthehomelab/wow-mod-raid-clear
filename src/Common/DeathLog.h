/*
 * mod-raid-clear: raid death log. Every player death on a raid map gets a line in the server log
 * with what killed it, the last 6 seconds of damage, its debuffs and the boss it was fighting;
 * a boss kill or reset gets a tally of the causes. See DeathLog.cpp.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_DEATH_LOG_H
#define MOD_RAID_CLEAR_DEATH_LOG_H

namespace RaidClear::DeathLog
{
    void AddScripts();
}

#endif
