/*
 * mod-raid-clear: raid-wide kill order through the skull raid icon. See KillOrder.h.
 *
 * Released under the MIT License.
 */

#include "KillOrder.h"

#include "RaidClearConfig.h"
#include "Raids/AhnQiraj/AqRaidClear.h"
#include "Raids/BlackwingLair/BwlRaidClear.h"
#include "Raids/MoltenCore/McRaidClear.h"
#include "Raids/ZulGurub/ZgRaidClear.h"

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
            case MoltenCore::MAP_ID:       return MoltenCore::KillOrder();
            case BlackwingLair::MAP_ID:    return BlackwingLair::KillOrder();
            case ZulGurub::MAP_ID:         return ZulGurub::KillOrder();
            case RuinsOfAhnQiraj::MAP_ID:  return RuinsOfAhnQiraj::KillOrder();
            case TempleOfAhnQiraj::MAP_ID: return TempleOfAhnQiraj::KillOrder();
            default:                       return none;
        }
    }

    bool KillOrderSkips(Player* bot, Unit const* unit)
    {
        switch (bot->GetMapId())
        {
            case MoltenCore::MAP_ID:       return MoltenCore::KillOrderSkips(bot, unit);
            case ZulGurub::MAP_ID:         return ZulGurub::KillOrderSkips(bot, unit);
            case TempleOfAhnQiraj::MAP_ID: return TempleOfAhnQiraj::KillOrderSkips(bot, unit);
            default:                       return false;
        }
    }

    bool KillOrderSuspended(Player* bot)
    {
        switch (bot->GetMapId())
        {
            case ZulGurub::MAP_ID: return ZulGurub::KillOrderSuspended(bot);
            default:               return false;
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
                              std::vector<KillOrderEntry> const& table, bool* clearSkull)
    {
        if (clearSkull)
            *clearSkull = false;
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        Unit* best = nullptr;
        uint8 bestRank = RANK_NONE;
        for (ObjectGuid const& guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            // Playerbots lists the skull among the attackers whether it is fighting or not: a
            // mark on a pack nobody has pulled is not a reason to mark it again.
            if (!unit || !unit->IsAlive() || !unit->IsInCombat() || !bot->IsValidAttackTarget(unit))
                continue;

            uint8 const rank = RankOf(unit, table);
            if (rank == RANK_NONE || KillOrderSkips(bot, unit))
                continue;

            // Same rank: finish the one closest to dying.
            if (rank < bestRank || (rank == bestRank && unit->GetHealthPct() < best->GetHealthPct()))
            {
                best = unit;
                bestRank = rank;
            }
        }

        Unit* skull = botAI->GetUnit(group->GetTargetIcon(RtiTargetValue::skullIndex));
        bool const skullSkipped = skull && skull->IsAlive() && KillOrderSkips(bot, skull);
        if (!best)
        {
            if (clearSkull)
                *clearSkull = skullSkipped;
            return nullptr;
        }

        if (skull && skull->IsAlive() && skull->IsInCombat() && !skullSkipped && RankOf(skull, table) <= bestRank)
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
    if (table.empty() || KillOrderMarker(bot->GetGroup()) != bot || KillOrderSuspended(bot))
        return false;

    bool clearSkull = false;
    return PickKillOrderTarget(botAI, bot, AI_VALUE(GuidVector, "attackers"), table, &clearSkull) || clearSkull;
}

bool RcMarkKillOrderAction::Execute(Event /*event*/)
{
    Group* group = bot->GetGroup();
    if (!group || KillOrderSuspended(bot))
        return false;

    bool clearSkull = false;
    Unit* target = PickKillOrderTarget(botAI, bot, AI_VALUE(GuidVector, "attackers"),
                                       KillOrderFor(bot->GetMapId()), &clearSkull);
    if (!target && !clearSkull)
        return false;

    group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), target ? target->GetGUID() : ObjectGuid::Empty);
    return true;
}
