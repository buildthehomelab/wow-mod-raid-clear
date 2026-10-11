/*
 * mod-raid-clear: Zul'Gurub (map 309). See ZgRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "ZgRaidClear.h"

#include "AttackAction.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ThreatManager.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"

#include <algorithm>
#include <cmath>

using namespace RaidClear::ZulGurub;

std::vector<RaidClear::KillOrderEntry> const& RaidClear::ZulGurub::KillOrder()
{
    static std::vector<KillOrderEntry> const table = {
        // Venoxis' four guards, before him.
        { NPC_RAZZASHI_COBRA, 1 },
    };
    return table;
}

void RaidClearZulGurubStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("rc kill order", { NextAction("rc mark kill order", ACTION_RAID + 2) }));

    triggers.push_back(new TriggerNode("rc zg holy wrath keep away",
                                       { NextAction("rc zg holy wrath keep away", ACTION_RAID + 3) }));
}

void RaidClearZulGurubStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcZgHolyWrathHoldMultiplier(botAI));
}

// --- High Priest Venoxis --------------------------------------------------

namespace
{
    // Venoxis while he can cast Holy Wrath: alive, fighting, still a troll.
    Unit* VenoxisInTrollForm(PlayerbotAI* botAI)
    {
        Unit* boss = botAI->GetAiObjectContext()->GetValue<Unit*>("find target", "high priest venoxis")->Get();
        return boss && boss->IsAlive() && boss->IsInCombat() && !boss->HasAura(SPELL_VENOXIS_TRANSFORM) ? boss : nullptr;
    }

    // Edge-to-edge distance, the way the core measures a chain jump.
    float Gap(Unit const* a, Unit const* b)
    {
        return a->GetExactDist(b) - a->GetObjectSize() - b->GetObjectSize();
    }

    // Who Holy Wrath reaches before it needs a non-tank to carry on. It starts on the top of
    // Venoxis' threat list (DoCastMaxThreat), which is his victim unless a damage dealer has passed
    // the tank without pulling him yet, so both count. Then every tank and every pet standing
    // within a jump of someone already in the chain (an off-tank on the cobras, a hunter's pet on
    // the boss): tanks can take the first few jumps and pets are expendable, but each is a
    // stepping stone, so nobody else may stand near any of them.
    std::vector<Unit*> BuildHolyWrathChain(Player* bot, Unit* boss)
    {
        std::vector<Unit*> chain;
        if (Unit* victim = boss->GetVictim())
            chain.push_back(victim);
        for (ThreatReference const* ref : boss->GetThreatMgr().GetSortedThreatList())
        {
            if (ref->IsOffline())
                continue;
            Unit* top = ref->GetVictim();
            if (top && std::find(chain.begin(), chain.end(), top) == chain.end())
                chain.push_back(top);
            break;
        }

        Group* group = bot->GetGroup();
        if (chain.empty() || !group)
            return chain;

        std::vector<Unit*> stones;
        auto consider = [&](Unit* unit)
        {
            if (unit && unit->IsAlive() && std::find(chain.begin(), chain.end(), unit) == chain.end())
                stones.push_back(unit);
        };
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInMap(bot))
                continue;
            if (PlayerbotAI::IsTank(member))
                consider(member);
            consider(member->GetPet());
        }

        bool added = true;
        while (added && !stones.empty())
        {
            added = false;
            for (auto it = stones.begin(); it != stones.end();)
            {
                Unit* stone = *it;
                bool const linked = std::any_of(chain.begin(), chain.end(), [&](Unit* link)
                {
                    return Gap(stone, link) <= HOLY_WRATH_CHAIN_LINK;
                });
                if (linked)
                {
                    chain.push_back(stone);
                    it = stones.erase(it);
                    added = true;
                }
                else
                    ++it;
            }
        }
        return chain;
    }

    // The chain is the same for every bot in the fight and is asked for by the trigger, the action
    // and the multiplier (once per movement action): build it once per tick per fight.
    constexpr uint32 CHAIN_CACHE_MS = 100;
    struct ChainCache
    {
        ObjectGuid boss;
        uint32 instanceId = 0;
        uint32 ms = 0;
        GuidVector members;
    };
    thread_local ChainCache tChainCache;

    std::vector<Unit*> HolyWrathChain(Player* bot, Unit* boss)
    {
        uint32 const now = getMSTime();
        if (tChainCache.boss == boss->GetGUID() && tChainCache.instanceId == boss->GetInstanceId() &&
            getMSTimeDiff(tChainCache.ms, now) <= CHAIN_CACHE_MS)
        {
            std::vector<Unit*> chain;
            for (ObjectGuid const& guid : tChainCache.members)
                if (Unit* unit = ObjectAccessor::GetUnit(*bot, guid))
                    if (unit->IsAlive())
                        chain.push_back(unit);
            return chain;
        }

        std::vector<Unit*> chain = BuildHolyWrathChain(bot, boss);
        tChainCache.boss = boss->GetGUID();
        tChainCache.instanceId = boss->GetInstanceId();
        tChainCache.ms = now;
        tChainCache.members.clear();
        for (Unit* unit : chain)
            tChainCache.members.push_back(unit->GetGUID());
        return chain;
    }

    bool InChain(std::vector<Unit*> const& chain, Unit const* unit)
    {
        return std::find(chain.begin(), chain.end(), unit) != chain.end();
    }

    // The closest chain member with less than `range` between its edge and a unit of size
    // `size` standing at `pos`.
    Unit* ChainMemberNear(std::vector<Unit*> const& chain, Position const* pos, float size, float range)
    {
        Unit* nearest = nullptr;
        float best = range;
        for (Unit* link : chain)
        {
            float const gap = pos->GetExactDist(link) - size - link->GetObjectSize();
            if (gap < best)
            {
                nearest = link;
                best = gap;
            }
        }
        return nearest;
    }
}

bool RcZgHolyWrathKeepAwayTrigger::IsActive()
{
    if (!bot->IsInCombat())
        return false;
    Unit* boss = VenoxisInTrollForm(botAI);
    if (!boss)
        return false;

    // Tanks go where their mobs are; one that walks in becomes part of the chain.
    if (PlayerbotAI::IsTank(bot))
        return false;

    std::vector<Unit*> const chain = HolyWrathChain(bot, boss);
    return !InChain(chain, bot) && ChainMemberNear(chain, bot, bot->GetObjectSize(), HOLY_WRATH_KEEP_AWAY);
}

bool RcZgHolyWrathKeepAwayAction::Execute(Event /*event*/)
{
    Unit* boss = VenoxisInTrollForm(botAI);
    if (!boss)
        return false;

    std::vector<Unit*> const chain = HolyWrathChain(bot, boss);
    float const size = bot->GetObjectSize();
    Unit* closest = InChain(chain, bot) ? nullptr : ChainMemberNear(chain, bot, size, HOLY_WRATH_KEEP_AWAY);
    if (!closest)
        return false;

    // Rings around the chain member the bot is too close to, all the way round: the nearest spot
    // that is clear of the whole chain, level with the bot (no stepping off the temple's edge or
    // down its stairs), and from which that chain member can still be seen (it's who the healers
    // are healing).
    Map* map = bot->GetMap();
    float const base = closest->GetAngle(bot);
    float const reach = HOLY_WRATH_KEEP_AWAY_TARGET + closest->GetObjectSize() + size;

    static float const offsets[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f, 2.0f, -2.0f, 2.5f, -2.5f, float(M_PI) };
    static float const extras[] = { 0.0f, 5.0f };
    bool found = false;
    float bestX = 0.0f, bestY = 0.0f, bestZ = 0.0f, bestDist = 0.0f;
    for (float const extra : extras)
    {
        for (float const offset : offsets)
        {
            float const angle = base + offset;
            float const x = closest->GetPositionX() + std::cos(angle) * (reach + extra);
            float const y = closest->GetPositionY() + std::sin(angle) * (reach + extra);
            float const z = map->GetHeight(bot->GetPhaseMask(), x, y, bot->GetPositionZ() + HOLY_WRATH_SPOT_HEIGHT);
            if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > HOLY_WRATH_SPOT_HEIGHT)
                continue;
            if (!bot->IsWithinLOS(x, y, z + 2.0f) || !closest->IsWithinLOS(x, y, z + 2.0f))
                continue;

            Position const spot(x, y, z);
            if (ChainMemberNear(chain, &spot, size, HOLY_WRATH_KEEP_AWAY))
                continue;

            float const dist = bot->GetExactDist(&spot);
            if (!found || dist < bestDist)
            {
                found = true;
                bestX = x;
                bestY = y;
                bestZ = z;
                bestDist = dist;
            }
        }
        if (found)
            break;
    }

    if (found)
        return MoveTo(bot->GetMapId(), bestX, bestY, bestZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT);

    // Nothing clean nearby: any step away from it is better than standing in the chain.
    return MoveAway(closest, HOLY_WRATH_KEEP_AWAY_TARGET - Gap(bot, closest));
}

float RcZgHolyWrathHoldMultiplier::GetValue(Action* action)
{
    // Picking a target isn't movement; only walking somewhere is held.
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action) ||
        dynamic_cast<RcZgHolyWrathKeepAwayAction*>(action))
        return 1.0f;

    if (!bot->IsInCombat() || PlayerbotAI::IsTank(bot))
        return 1.0f;
    Unit* boss = VenoxisInTrollForm(botAI);
    if (!boss)
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    std::vector<Unit*> const chain = HolyWrathChain(bot, boss);
    // Whoever Venoxis is on stays put. Walking toward a chain member itself only happens to heal
    // it (into range or line of sight), and that stops well outside.
    if (InChain(chain, bot) || InChain(chain, target))
        return 1.0f;

    // Something inside the chain's reach (Venoxis, a cobra on a tank).
    Unit* closest = ChainMemberNear(chain, target, target->GetObjectSize(), HOLY_WRATH_KEEP_AWAY);
    if (!closest)
        return 1.0f;

    // Melee would have to walk all the way in, so they don't start, however far out they are: one
    // "reach melee" from 30 yd carries a bot to the tank before keep-away can turn it round.
    // Ranged and healers stop at spell range anyway; they're only held once they're close.
    if (!PlayerbotAI::IsRanged(bot) && !PlayerbotAI::IsHeal(bot))
        return 0.0f;
    return Gap(bot, closest) > HOLY_WRATH_HOLD ? 1.0f : 0.0f;
}
