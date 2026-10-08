/*
 * mod-raid-clear: main tank / off-tank split for bot raids. See TankRoles.h.
 *
 * Released under the MIT License.
 */

#include "TankRoles.h"

#include "RaidClearConfig.h"
#include "Raids/BlackwingLair/BwlRaidClear.h"
#include "Raids/MoltenCore/McRaidClear.h"

#include "ChooseTargetActions.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <list>
#include <map>

namespace RaidClear::Tanks
{
    namespace
    {
        // Off-tanks only look at mobs this close; anything further belongs to whoever is there.
        constexpr float PICKUP_RANGE = 40.0f;
        // Re-separate only once the off-tank has drifted this far inside the wanted distance.
        constexpr float SEPARATION_SLACK = 3.0f;
        // After the separation search finds nowhere better, wait this long before searching again.
        constexpr uint32 SEPARATION_RETRY_MS = 2000;
        // A spot more than this above or below the main tank is a ledge or a pit.
        constexpr float MAX_HEIGHT_DIFF = 5.0f;
        // A skip-listed boss this close and in combat owns the fight even when it isn't attacking.
        constexpr float SKIP_BOSS_RANGE = 80.0f;

        // Bosses whose own strategy (mod-playerbots or ours) places the tanks itself.
        std::vector<uint32> const& SkipBosses(uint32 mapId)
        {
            static std::vector<uint32> const none;
            switch (mapId)
            {
                case MoltenCore::MAP_ID:    return MoltenCore::TankSplitSkipBosses();
                case BlackwingLair::MAP_ID: return BlackwingLair::TankSplitSkipBosses();
                default:                    return none;
            }
        }

        // Bosses that cut their tank's threat (Knock Away, Wing Buffet): the off-tank builds threat
        // on them as well, so the boss lands on a tank instead of the top damage dealer.
        std::vector<uint32> const& CoTankBosses(uint32 mapId)
        {
            static std::vector<uint32> const none;
            switch (mapId)
            {
                case BlackwingLair::MAP_ID: return BlackwingLair::TankSplitCoTankBosses();
                default:                    return none;
            }
        }

        // Adds an off-tank never picks up (endless respawning whelps and the like).
        std::vector<uint32> const& IgnoredAdds(uint32 mapId)
        {
            static std::vector<uint32> const none;
            switch (mapId)
            {
                case BlackwingLair::MAP_ID: return BlackwingLair::TankSplitIgnore();
                default:                    return none;
            }
        }

        // Adds that get an off-tank of their own, taken even off the main tank (the Death Talon
        // Captain, whose Mark of Detonation has to be tanked away from the raid).
        std::vector<uint32> const& OwnTankAdds(uint32 mapId)
        {
            static std::vector<uint32> const none;
            switch (mapId)
            {
                case BlackwingLair::MAP_ID: return BlackwingLair::TankSplitOwnTankAdds();
                default:                    return none;
            }
        }

        // Mobs every tank takes one of (BWL's Wyrmguards): the main tank keeps one and each
        // off-tank taunts another off it as soon as the pack is pulled.
        std::vector<uint32> const& SpreadAdds(uint32 mapId)
        {
            static std::vector<uint32> const none;
            switch (mapId)
            {
                case BlackwingLair::MAP_ID: return BlackwingLair::TankSplitSpreadAdds();
                default:                    return none;
            }
        }

        bool IsIgnoredAdd(Unit const* unit)
        {
            std::vector<uint32> const& ignore = IgnoredAdds(unit->GetMapId());
            return std::find(ignore.begin(), ignore.end(), unit->GetEntry()) != ignore.end();
        }

        bool SkipFight(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers)
        {
            std::vector<uint32> const& skip = SkipBosses(bot->GetMapId());
            if (skip.empty())
                return false;

            for (ObjectGuid const& guid : attackers)
                if (Unit* unit = botAI->GetUnit(guid))
                    if (unit->IsAlive() && std::find(skip.begin(), skip.end(), unit->GetEntry()) != skip.end())
                        return true;

            // A boss can be in the fight without attacking anyone: Razorgore spends his whole
            // first phase mind-controlled by the raid while his adds are what attack.
            std::list<Creature*> nearby;
            bot->GetCreatureListWithEntryInGrid(nearby, skip, SKIP_BOSS_RANGE);
            for (Creature* boss : nearby)
                if (boss && boss->IsAlive() && boss->IsInCombat())
                    return true;
            return false;
        }

        // How far each mob's own AoE reaches, so two of them are kept out of each other's.
        std::vector<SplashRadius> const& SplashRadii(uint32 mapId)
        {
            static std::vector<SplashRadius> const none;
            switch (mapId)
            {
                case MoltenCore::MAP_ID:    return MoltenCore::TankSplitSplashRadii();
                case BlackwingLair::MAP_ID: return BlackwingLair::TankSplitSplashRadii();
                default:                    return none;
            }
        }

        float SplashRadiusOf(Unit const* unit)
        {
            if (!unit)
                return 0.0f;
            for (SplashRadius const& s : SplashRadii(unit->GetMapId()))
                if (s.entry == unit->GetEntry())
                    return s.radius;
            return 0.0f;
        }

        // The group's living off-tanks in group order. Every tank walks the same list, so anything
        // handed out in this order comes out the same for all of them.
        std::vector<Player*> OffTanksInOrder(Group* group, Player* mainTank)
        {
            std::vector<Player*> offTanks;
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (member->IsInWorld() && IsOffTank(member, mainTank))
                        offTanks.push_back(member);
            return offTanks;
        }

        // Only a bot off-tank close enough to come for a new mob is free to take one: a player tank
        // or one running back from a rez wouldn't.
        bool CanTakeOne(Player* offTank, Player* mainTank)
        {
            return GET_PLAYERBOT_AI(offTank) && offTank->GetDistance(mainTank) <= PICKUP_RANGE;
        }

        // The units this off-tank's mob keeps clear of: the main tank's mob (or the main tank,
        // between pulls) and every mob held by the main tank or by an off-tank ahead of this one in
        // group order. Those tanks don't move for this one, so a pack of three Wyrmguards spreads
        // out one tank at a time instead of two off-tanks dragging theirs to the same spot.
        std::vector<Unit*> MobsToClear(PlayerbotAI* botAI, Player* bot, Player* mainTank, Unit* mine,
                                       GuidVector const& attackers)
        {
            std::vector<Unit*> result;
            Unit* anchor = mainTank->GetVictim() ? mainTank->GetVictim() : mainTank;
            result.push_back(anchor);

            std::vector<Player*> ahead = { mainTank };
            for (Player* offTank : OffTanksInOrder(bot->GetGroup(), mainTank))
            {
                if (offTank == bot)
                    break;
                ahead.push_back(offTank);
            }

            for (ObjectGuid const& guid : attackers)
            {
                Unit* unit = botAI->GetUnit(guid);
                if (!unit || unit == mine || unit == anchor || !unit->IsAlive())
                    continue;
                Player* victim = unit->GetVictim() ? unit->GetVictim()->ToPlayer() : nullptr;
                if (victim && std::find(ahead.begin(), ahead.end(), victim) != ahead.end())
                    result.push_back(unit);
            }
            return result;
        }

        // How many yards short of its wanted separation from the closest offender a mob at (x, y)
        // would be; zero or less when it clears them all.
        float Shortfall(Unit const* mine, std::vector<Unit*> const& others, float x, float y, float extra)
        {
            float worst = -1000.0f;
            for (Unit const* other : others)
                worst = std::max(worst, WantedSeparation(mine, other) + extra - other->GetExactDist2d(x, y));
            return worst;
        }

        bool IsDry(Map* map, uint32 phaseMask, float x, float y, float z, float collisionHeight)
        {
            LiquidData const liquid = map->GetLiquidData(phaseMask, x, y, z, collisionHeight, {});
            return !(liquid.Status & MAP_LIQUID_STATUS_IN_CONTACT);
        }
    }

    void AssignMainTank(Group* group)
    {
        if (!group || !group->isRaidGroup())
            return;

        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.flags & MEMBER_FLAG_MAINTANK)
                return;  // the player (or an earlier pass) already chose

        // Only worth doing with a second tank to split from.
        uint32 tanks = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (PlayerbotAI::IsTank(member))
                    ++tanks;
        if (tanks < 2)
            return;

        ObjectGuid const mainTank = PlayerbotAI::GetMainTankGuid(group);
        if (!mainTank.IsEmpty())
            group->SetGroupMemberFlag(mainTank, true, MEMBER_FLAG_MAINTANK);
    }

    Player* ActingMainTank(Group* group)
    {
        if (!group)
            return nullptr;

        Player* firstTank = nullptr;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;

            if (PlayerbotAI::IsExplicitMainTank(member))
            {
                if (member->IsAlive())
                    return member;
                continue;  // flagged but dead: fall back to the first living tank
            }
            if (!firstTank && member->IsAlive() && PlayerbotAI::IsTank(member))
                firstTank = member;
        }
        return firstTank;
    }

    bool IsOffTank(Player* bot, Player* mainTank)
    {
        return mainTank && mainTank != bot && bot->IsAlive() && PlayerbotAI::IsTank(bot) &&
               mainTank->GetMapId() == bot->GetMapId();
    }

    Unit* CoTankBoss(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers)
    {
        std::vector<uint32> const& bosses = CoTankBosses(bot->GetMapId());
        if (bosses.empty())
            return nullptr;

        for (ObjectGuid const& guid : attackers)
            if (Unit* unit = botAI->GetUnit(guid))
                if (unit->IsAlive() && std::find(bosses.begin(), bosses.end(), unit->GetEntry()) != bosses.end() &&
                    bot->IsValidAttackTarget(unit) && bot->GetDistance(unit) <= PICKUP_RANGE)
                    return unit;
        return nullptr;
    }

    bool IsOwnTankAdd(Unit const* unit)
    {
        if (!unit)
            return false;
        std::vector<uint32> const& own = OwnTankAdds(unit->GetMapId());
        return std::find(own.begin(), own.end(), unit->GetEntry()) != own.end();
    }

    GuidSet OwnTankAddsForOffTanks(PlayerbotAI* botAI, Player* mainTank, GuidVector const& attackers)
    {
        GuidSet result;
        Group* group = mainTank ? mainTank->GetGroup() : nullptr;
        if (!group)
            return result;

        // One that already holds one is meant to be far from the main tank, so that one counts at
        // any distance; only a new one needs CanTakeOne.
        std::vector<Player*> const offTanks = OffTanksInOrder(group, mainTank);
        if (offTanks.empty())
            return result;

        std::vector<Player*> holding;
        std::vector<Unit*> loose;
        for (ObjectGuid const& guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || !IsOwnTankAdd(unit))
                continue;

            Player* victim = unit->GetVictim() ? unit->GetVictim()->ToPlayer() : nullptr;
            if (victim && std::find(offTanks.begin(), offTanks.end(), victim) != offTanks.end())
            {
                result.insert(guid);
                if (std::find(holding.begin(), holding.end(), victim) == holding.end())
                    holding.push_back(victim);
            }
            else
                loose.push_back(unit);
        }

        size_t free = 0;
        for (Player* offTank : offTanks)
            if (std::find(holding.begin(), holding.end(), offTank) == holding.end() && CanTakeOne(offTank, mainTank))
                ++free;
        for (size_t i = 0; i < loose.size() && i < free; ++i)
            result.insert(loose[i]->GetGUID());
        return result;
    }


    bool IsSpreadAdd(Unit const* unit)
    {
        if (!unit)
            return false;
        std::vector<uint32> const& spread = SpreadAdds(unit->GetMapId());
        return std::find(spread.begin(), spread.end(), unit->GetEntry()) != spread.end();
    }

    namespace
    {
        // Spread adds this close to the main tank and fighting its group. Read straight from the
        // world, not from a bot's own range-filtered and separately cached "attackers", so every
        // tank sees the same mobs.
        constexpr float SPREAD_SEARCH_RANGE = 60.0f;

        bool FightsGroup(Unit const* unit, Group const* group)
        {
            Unit const* victim = unit->GetVictim();
            if (!victim)
                return false;
            Player const* player = victim->GetCharmerOrOwnerPlayerOrPlayerItself();
            return player && player->GetGroup() == group;
        }

        std::map<ObjectGuid, Player*> AssignSpreadAdds(Player* mainTank, GuidSet const& ownTank)
        {
            std::map<ObjectGuid, Player*> result;
            Group* group = mainTank ? mainTank->GetGroup() : nullptr;
            if (!group)
                return result;

            std::vector<uint32> const& entries = SpreadAdds(mainTank->GetMapId());
            if (entries.empty())
                return result;

            std::list<Creature*> found;
            mainTank->GetCreatureListWithEntryInGrid(found, entries, SPREAD_SEARCH_RANGE);
            std::vector<Unit*> mobs;
            for (Creature* creature : found)
                if (creature && creature->IsAlive() && creature->IsInCombat() && FightsGroup(creature, group))
                    mobs.push_back(creature);
            if (mobs.empty())
                return result;
            // Sorted, so every tank hands out the same mob to the same off-tank.
            std::sort(mobs.begin(), mobs.end(), [](Unit* a, Unit* b) { return a->GetGUID() < b->GetGUID(); });

            std::vector<Player*> const offTanks = OffTanksInOrder(group, mainTank);
            if (offTanks.empty())
                return result;

            // Held by an off-tank: it keeps it (one each; a second one goes back in the pool).
            std::vector<Player*> holding;
            std::vector<Unit*> loose;
            for (Unit* unit : mobs)
            {
                Player* victim = unit->GetVictim()->ToPlayer();
                if (victim && std::find(offTanks.begin(), offTanks.end(), victim) != offTanks.end() &&
                    std::find(holding.begin(), holding.end(), victim) == holding.end())
                {
                    result[unit->GetGUID()] = victim;
                    holding.push_back(victim);
                }
                else
                    loose.push_back(unit);
            }

            // The main tank keeps the one it's on, else the first in GUID order: a fixed choice, so
            // the hand-out doesn't change while the pack is still running in.
            Unit* keep = nullptr;
            if (Unit* victim = mainTank->GetVictim())
                if (std::find(loose.begin(), loose.end(), victim) != loose.end())
                    keep = victim;
            if (!keep && !loose.empty())
                keep = loose.front();
            loose.erase(std::remove(loose.begin(), loose.end(), keep), loose.end());

            // An off-tank on a Captain stays on it. One holding some other mob only gets a spread
            // add once the idle ones have one each, so it doesn't drop its mob onto the raid.
            auto holdsAny = [&](Player* offTank, bool ownTankOnly)
            {
                for (Unit* attacker : offTank->getAttackers())
                    if (attacker->IsAlive() && attacker->GetVictim() == offTank &&
                        (!ownTankOnly || ownTank.count(attacker->GetGUID())))
                        return true;
                return false;
            };

            size_t next = 0;
            for (bool const busyToo : { false, true })
                for (Player* offTank : offTanks)
                {
                    if (next >= loose.size())
                        return result;
                    if (std::find(holding.begin(), holding.end(), offTank) != holding.end() ||
                        !CanTakeOne(offTank, mainTank) || holdsAny(offTank, true))
                        continue;
                    bool const busy = holdsAny(offTank, false);
                    if (busy != busyToo)
                        continue;
                    result[loose[next++]->GetGUID()] = offTank;
                }
            return result;
        }
    }

    namespace
    {
        // Only search the world for spread adds when this bot is fighting one: almost every fight
        // has none, and the hand-out is asked for several times a tick.
        bool FightingSpreadAdd(PlayerbotAI* botAI, GuidVector const& attackers)
        {
            return std::any_of(attackers.begin(), attackers.end(),
                               [&](ObjectGuid const& guid) { return IsSpreadAdd(botAI->GetUnit(guid)); });
        }
    }

    std::map<ObjectGuid, Player*> SpreadAddsForOffTanks(PlayerbotAI* botAI, Player* mainTank, GuidVector const& attackers)
    {
        if (!FightingSpreadAdd(botAI, attackers))
            return {};
        return AssignSpreadAdds(mainTank, OwnTankAddsForOffTanks(botAI, mainTank, attackers));
    }

    GuidSet HandedOffAdds(PlayerbotAI* botAI, Player* mainTank, GuidVector const& attackers)
    {
        GuidSet result = OwnTankAddsForOffTanks(botAI, mainTank, attackers);
        if (FightingSpreadAdd(botAI, attackers))
            for (auto const& [guid, offTank] : AssignSpreadAdds(mainTank, result))
                result.insert(guid);
        return result;
    }

    // Single-target taunts, by their playerbots spell names; all reach 30 yd.
    constexpr std::array<char const*, 4> SINGLE_TAUNTS = { "taunt", "growl", "hand of reckoning", "dark command" };
    constexpr float TAUNT_RANGE = 30.0f;

    bool CanTaunt(PlayerbotAI* botAI, Unit* unit)
    {
        // CanCastSpell lets an out-of-range target through, and the cast then fails every tick.
        if (!unit || botAI->GetBot()->GetDistance(unit) > TAUNT_RANGE)
            return false;
        for (char const* taunt : SINGLE_TAUNTS)
            if (botAI->CanCastSpell(taunt, unit))
                return true;
        return false;
    }

    bool Taunt(PlayerbotAI* botAI, Unit* unit)
    {
        if (!unit || botAI->GetBot()->GetDistance(unit) > TAUNT_RANGE)
            return false;
        for (char const* taunt : SINGLE_TAUNTS)
            if (botAI->CanCastSpell(taunt, unit) && botAI->CastSpell(taunt, unit))
                return true;
        return false;
    }

    Unit* PickOffTankTarget(PlayerbotAI* botAI, Player* bot, Player* mainTank, GuidVector const& attackers)
    {
        // An add that wants a tank of its own comes first, wherever it is, unless another off-tank
        // already holds it. The main tank leaves it alone (see the raid's target exclusions).
        Unit* ownTankAdd = nullptr;
        float ownTankDist = 0.0f;
        for (ObjectGuid const& guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || !IsOwnTankAdd(unit) || !bot->IsValidAttackTarget(unit))
                continue;

            Unit* victim = unit->GetVictim();
            if (victim == bot)
                return unit;  // keep holding it

            Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
            if (victimPlayer && victimPlayer != mainTank && PlayerbotAI::IsTank(victimPlayer))
                continue;  // another off-tank has it

            float const dist = bot->GetDistance(unit);
            if (dist <= PICKUP_RANGE && (!ownTankAdd || dist < ownTankDist))
            {
                ownTankAdd = unit;
                ownTankDist = dist;
            }
        }
        if (ownTankAdd)
            return ownTankAdd;

        // The one of a spread pack (Wyrmguards) this off-tank was handed.
        for (auto const& [guid, offTank] : SpreadAddsForOffTanks(botAI, mainTank, attackers))
            if (offTank == bot)
                if (Unit* unit = botAI->GetUnit(guid))
                    if (bot->IsValidAttackTarget(unit) && bot->GetDistance(unit) <= PICKUP_RANGE)
                        return unit;

        Unit* mainTarget = mainTank->GetVictim();
        Unit* coBoss = CoTankBoss(botAI, bot, attackers);

        // 0 = already on me, 1 = loose on a non-tank, 2 = extra mob on the main tank,
        // 3 = attacking nobody. Mobs held by another off-tank are left alone.
        auto rankOf = [&](Unit* unit) -> int
        {
            Unit* victim = unit->GetVictim();
            if (victim == bot)
                return 0;
            if (!victim)
                return 3;
            Player* victimPlayer = victim->ToPlayer();
            if (!victimPlayer)
                return 1;  // on a pet or a guardian
            if (victimPlayer == mainTank)
                return 2;
            if (PlayerbotAI::IsTank(victimPlayer))
                return -1;
            return 1;
        };

        Unit* current = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        Unit* best = nullptr;
        int bestRank = 99;
        float bestDist = 0.0f;
        for (ObjectGuid const& guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            // Spread adds are handed out above; one not handed to me belongs to another tank.
            if (!unit || unit == mainTarget || unit == coBoss || !unit->IsAlive() || !bot->IsValidAttackTarget(unit) ||
                IsIgnoredAdd(unit) || IsSpreadAdd(unit))
                continue;

            float const dist = bot->GetDistance(unit);
            if (dist > PICKUP_RANGE)
                continue;

            int const rank = rankOf(unit);
            if (rank < 0)
                continue;

            // Keep what I'm holding unless something is loose on a non-tank.
            if (unit == current && rank == 0)
                return current;

            if (!best || rank < bestRank || (rank == bestRank && dist < bestDist))
            {
                best = unit;
                bestRank = rank;
                bestDist = dist;
            }
        }

        // Adds that hit somebody come first; otherwise build threat on a boss that sheds it.
        if (coBoss && (!best || bestRank >= 3))
            return coBoss;
        return best;
    }

    float WantedSeparation(Unit const* mine, Unit const* theirs)
    {
        return std::max(GetConfig().tankSeparation, SplashRadiusOf(mine) + SplashRadiusOf(theirs));
    }

    bool BossInFight(PlayerbotAI* botAI, GuidVector const& attackers)
    {
        for (ObjectGuid const& guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            Creature* creature = unit ? unit->ToCreature() : nullptr;
            if (creature && creature->IsAlive() && (creature->IsDungeonBoss() || creature->isWorldBoss()))
                return true;
        }
        return false;
    }
}

using namespace RaidClear;
using namespace RaidClear::Tanks;

namespace
{
    // Mobs an off-tank taunts off whoever has them, the main tank included: Playerbots' "lose
    // aggro" never taunts off another tank.
    bool TauntsOff(Unit const* unit)
    {
        return IsOwnTankAdd(unit) || IsSpreadAdd(unit);
    }
}

void RaidClearTanksStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Above playerbots' own tank target picking ("tank assist", 50), below the raid mechanics
    // (ACTION_RAID): dodging a Living Bomb still comes first.
    triggers.push_back(
        new TriggerNode("rc offtank target", { NextAction("rc offtank target", ACTION_RAID - 4) }));
    triggers.push_back(
        new TriggerNode("rc offtank separate", { NextAction("rc offtank separate", ACTION_RAID - 5) }));
}

void RaidClearTanksStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcOffTankMultiplier(botAI));
}

bool RcOffTankTargetTrigger::IsActive()
{
    if (!GetConfig().tankSplit || !bot->IsInCombat())
        return false;

    Player* mainTank = ActingMainTank(bot->GetGroup());
    if (!IsOffTank(bot, mainTank))
        return false;

    GuidVector const& attackers = AI_VALUE(GuidVector, "attackers");
    // Alone with the main tank's mob there's nothing to split, unless it's a boss to co-tank or an
    // add that needs its own tank (the main tank hands those off even when they're the last one).
    bool const ownTankAdd = std::any_of(attackers.begin(), attackers.end(),
        [&](ObjectGuid const& guid) { return TauntsOff(botAI->GetUnit(guid)); });
    if ((attackers.size() < 2 && !ownTankAdd && !CoTankBoss(botAI, bot, attackers)) ||
        SkipFight(botAI, bot, attackers))
        return false;

    Unit* want = PickOffTankTarget(botAI, bot, mainTank, attackers);
    if (!want)
        return false;
    if (want != AI_VALUE(Unit*, "current target"))
        return true;

    // Playerbots' "lose aggro" never taunts a mob off another tank, so an own-tank add sitting
    // on the main tank has to be taunted from here.
    return TauntsOff(want) && want->GetVictim() != bot && CanTaunt(botAI, want);
}

bool RcOffTankTargetAction::Execute(Event /*event*/)
{
    Player* mainTank = ActingMainTank(bot->GetGroup());
    if (!IsOffTank(bot, mainTank))
        return false;

    Unit* want = PickOffTankTarget(botAI, bot, mainTank, AI_VALUE(GuidVector, "attackers"));
    if (!want)
        return false;

    // The class tank strategies taunt a target that isn't attacking them ("lose aggro"), but not
    // off another tank: an own-tank add on the main tank is taunted here.
    bool const taunt = TauntsOff(want) && want->GetVictim() != bot;

    // Already on it: only the taunt is left to do. Returning true without casting would starve
    // the rotation and "reach melee" every tick.
    if (want == AI_VALUE(Unit*, "current target"))
        return taunt && Taunt(botAI, want);

    bool const attacked = Attack(want);
    return (taunt && Taunt(botAI, want)) || attacked;
}

bool RcOffTankSeparateTrigger::IsActive()
{
    if (GetConfig().tankSeparation <= 0.0f || !GetConfig().tankSplit || !bot->IsInCombat())
        return false;

    Player* mainTank = ActingMainTank(bot->GetGroup());
    if (!IsOffTank(bot, mainTank) || !mainTank->IsInCombat())
        return false;

    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !target->IsAlive() || target->GetVictim() != bot || target == mainTank->GetVictim())
        return false;

    GuidVector const& attackers = AI_VALUE(GuidVector, "attackers");
    if (SkipFight(botAI, bot, attackers))
        return false;
    if (!GetConfig().separateOnBosses && BossInFight(botAI, attackers))
        return false;

    // Measured between the mobs, not the tanks: the mobs are what swing and stomp, and big ones
    // stand well in front of their tank.
    for (Unit* other : MobsToClear(botAI, bot, mainTank, target, attackers))
        if (target->GetExactDist2d(other) < WantedSeparation(target, other) - SEPARATION_SLACK)
            return true;
    return false;
}

bool RcOffTankSeparateAction::Execute(Event /*event*/)
{
    Player* mainTank = ActingMainTank(bot->GetGroup());
    if (!IsOffTank(bot, mainTank))
        return false;

    Unit* mine = AI_VALUE(Unit*, "current target");
    if (!mine)
        return false;

    Unit* anchor = mainTank->GetVictim() ? mainTank->GetVictim() : mainTank;
    std::vector<Unit*> const others = MobsToClear(botAI, bot, mainTank, mine, AI_VALUE(GuidVector, "attackers"));
    Map* map = bot->GetMap();

    // Stand far enough out that my mob, following me, ends up at the wanted distance from the
    // others even if it stops between us.
    float const extra = mine->GetCombatReach() + 1.0f;
    float const wanted = WantedSeparation(mine, anchor) + extra;

    // On a ring around the main tank's mob: straight away from it first, then fanning out to
    // either side and round to the far side; a little closer only if nothing at full distance
    // works. A spot also has to clear the mobs of the tanks ahead of this one. If none does, the
    // spot that comes closest, as long as it's a real improvement on staying put.
    float base = anchor->GetAngle(bot);
    if (bot->GetExactDist2d(anchor) < 1.0f)
        base = anchor->GetOrientation() + float(M_PI) / 2.0f;

    static float const offsets[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f, 2.0f, -2.0f, 2.5f, -2.5f, float(M_PI) };
    static float const scales[] = { 1.0f, 0.85f };

    // The ring itself keeps the main tank's mob far enough away (or as far as 0.85 of it allows);
    // a spot is accepted on the other mobs alone, and fallbacks are ranked on all of them.
    std::vector<Unit*> rest;
    for (Unit* other : others)
        if (other != anchor)
            rest.push_back(other);

    struct Spot { float x, y, z, shortfall; };
    std::vector<Spot> spots;
    for (float const scale : scales)
    {
        float const radius = wanted * scale;
        for (float const offset : offsets)
        {
            float const angle = base + offset;
            float const x = anchor->GetPositionX() + std::cos(angle) * radius;
            float const y = anchor->GetPositionY() + std::sin(angle) * radius;
            float const z = map->GetHeight(bot->GetPhaseMask(), x, y, anchor->GetPositionZ() + MAX_HEIGHT_DIFF);

            if (z <= INVALID_HEIGHT || std::fabs(z - anchor->GetPositionZ()) > MAX_HEIGHT_DIFF)
                continue;
            if (!IsDry(map, bot->GetPhaseMask(), x, y, z, bot->GetCollisionHeight()))
                continue;
            // Healers stay near the main tank; keep the off-tank where they can see it.
            if (!mainTank->IsWithinLOS(x, y, z + 2.0f))
                continue;

            if ((rest.empty() || Shortfall(mine, rest, x, y, extra) <= 0.0f) &&
                MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
                return true;
            spots.push_back({ x, y, z, Shortfall(mine, others, x, y, extra) });
        }
    }

    std::sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b) { return a.shortfall < b.shortfall; });
    float const now = Shortfall(mine, others, bot->GetPositionX(), bot->GetPositionY(), extra);
    for (Spot const& spot : spots)
    {
        if (spot.shortfall > now - SEPARATION_SLACK)
            break;
        if (MoveTo(bot->GetMapId(), spot.x, spot.y, spot.z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
            return true;
    }

    // Nowhere better to go: don't search the whole ring again on every tick.
    lastFailMs = getMSTime();
    return false;
}

bool RcOffTankSeparateAction::isUseful()
{
    return (!lastFailMs || getMSTimeDiff(lastFailMs, getMSTime()) >= SEPARATION_RETRY_MS) && MovementAction::isUseful();
}

float RcOffTankMultiplier::GetValue(Action* action)
{
    if (!GetConfig().tankSplit || !bot->IsInCombat() || !dynamic_cast<AttackAction*>(action) ||
        dynamic_cast<RcOffTankTargetAction*>(action))
        return 1.0f;

    Player* mainTank = ActingMainTank(bot->GetGroup());
    if (!IsOffTank(bot, mainTank))
        return 1.0f;

    Unit* mainTarget = mainTank->GetVictim();
    if (!mainTarget || action->GetTarget() != mainTarget)
        return 1.0f;

    // Only hold back while there is something else to pick up; alone with the main tank's mob,
    // an off-tank may as well hit it. A boss that sheds threat is itself what the off-tank wants.
    GuidVector const& attackers = AI_VALUE(GuidVector, "attackers");
    if (SkipFight(botAI, bot, attackers))
        return 1.0f;
    Unit* want = PickOffTankTarget(botAI, bot, mainTank, attackers);
    return want && want != mainTarget ? 0.0f : 1.0f;
}
