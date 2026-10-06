/*
 * mod-raid-clear: raid-wide kill order through the skull raid icon.
 *
 * DPS bots attack the group's skull target before anything else (playerbots' DpsTargetValue
 * returns the RTI target first). One bot per group, the "marker", keeps skull on the most
 * important enemy in the fight: healers before the boss, adds that split or explode before
 * anything else. Each raid supplies a table of creature entries with a rank (lower = sooner).
 *
 * The marker leaves skull alone while it sits on a living enemy of the same or a better rank,
 * so it doesn't flip between equal adds, and so a skull the player put on an add stays.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_KILL_ORDER_H
#define MOD_RAID_CLEAR_KILL_ORDER_H

#include "Action.h"
#include "ObjectGuid.h"
#include "Trigger.h"

#include <vector>

class Group;
class Player;
class PlayerbotAI;
class Unit;

namespace RaidClear
{
    struct KillOrderEntry
    {
        uint32 entry;
        uint8 rank;  // 0 = kill first
    };

    // The kill-order table for a map; empty when the raid has none.
    std::vector<KillOrderEntry> const& KillOrderFor(uint32 mapId);

    // The bot that marks for this group: the first living bot that is the main tank, else the
    // first living bot tank, else the first living bot. Real players never mark.
    Player* KillOrderMarker(Group* group);

    // The enemy skull should move to, or nullptr when skull is fine where it is.
    Unit* PickKillOrderTarget(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers,
                              std::vector<KillOrderEntry> const& table);
}

class RcKillOrderTrigger : public Trigger
{
public:
    RcKillOrderTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc kill order") {}
    bool IsActive() override;
};

class RcMarkKillOrderAction : public Action
{
public:
    RcMarkKillOrderAction(PlayerbotAI* botAI) : Action(botAI, "rc mark kill order") {}
    bool Execute(Event event) override;
};

#endif
