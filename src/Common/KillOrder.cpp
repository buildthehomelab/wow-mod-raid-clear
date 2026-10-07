/*
 * mod-raid-clear: raid-wide kill order through the skull raid icon. See KillOrder.h.
 *
 * Released under the MIT License.
 */

#include "KillOrder.h"

#include "RaidClearConfig.h"
#include "Raids/BlackwingLair/BwlRaidClear.h"
#include "Raids/MoltenCore/McRaidClear.h"

#include "Group.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "RtiTargetValue.h"

namespace RaidClear
{
    namespace
    {
        constexpr uint8 RANK_NONE = 255;

        uint8 RankOf(Unit const* unit, std::vector<KillOrderEntry> const& table)
        {
            if (!unit)
                return RANK_NONE;
            for (KillOrderEntry const& e : table)
                if (e.entry == unit->GetEntry())
                    return e.rank;
            return RANK_NONE;
        }
    }

    std::vector<KillOrderEntry> const& KillOrderFor(uint32 mapId)
    {
        static std::vector<KillOrderEntry> const none;
        switch (mapId)
        {
            case MoltenCore::MAP_ID:    return MoltenCore::KillOrder();
            case BlackwingLair::MAP_ID: return BlackwingLair::KillOrder();
            default:                    return none;
        }
    }

    Player* KillOrderMarker(Group* group)
    {
        if (!group)
            return nullptr;

        Player* tank = nullptr;
        Player* any = nullptr;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld() || !member->IsAlive() || !GET_PLAYERBOT_AI(member))
                continue;

            if (PlayerbotAI::IsMainTank(member))
                return member;
            if (!tank && PlayerbotAI::IsTank(member))
                tank = member;
            if (!any)
                any = member;
        }
        return tank ? tank : any;
    }

    Unit* PickKillOrderTarget(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers,
                              std::vector<KillOrderEntry> const& table)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        Unit* best = nullptr;
        uint8 bestRank = RANK_NONE;
        for (ObjectGuid const& guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || !bot->IsValidAttackTarget(unit))
                continue;

            uint8 const rank = RankOf(unit, table);
            if (rank == RANK_NONE)
                continue;

            // Same rank: finish the one closest to dying.
            if (rank < bestRank || (rank == bestRank && unit->GetHealthPct() < best->GetHealthPct()))
            {
                best = unit;
                bestRank = rank;
            }
        }

        if (!best)
            return nullptr;

        Unit* skull = botAI->GetUnit(group->GetTargetIcon(RtiTargetValue::skullIndex));
        if (skull && skull->IsAlive() && skull->IsInCombat() && RankOf(skull, table) <= bestRank)
            return nullptr;  // already on something at least as important

        return best;
    }
}

using namespace RaidClear;

bool RcKillOrderTrigger::IsActive()
{
    if (!GetConfig().killOrder || !bot->IsInCombat())
        return false;

    std::vector<KillOrderEntry> const& table = KillOrderFor(bot->GetMapId());
    if (table.empty() || KillOrderMarker(bot->GetGroup()) != bot)
        return false;

    return PickKillOrderTarget(botAI, bot, AI_VALUE(GuidVector, "attackers"), table) != nullptr;
}

bool RcMarkKillOrderAction::Execute(Event /*event*/)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    Unit* target = PickKillOrderTarget(botAI, bot, AI_VALUE(GuidVector, "attackers"),
                                       KillOrderFor(bot->GetMapId()));
    if (!target)
        return false;

    group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), target->GetGUID());
    return true;
}
