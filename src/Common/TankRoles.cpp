/*
 * mod-raid-clear: main tank / off-tank split for bot raids. See TankRoles.h.
 *
 * Released under the MIT License.
 */

#include "TankRoles.h"

#include "RaidClearConfig.h"
#include "Raids/MoltenCore/McRaidClear.h"

#include "ChooseTargetActions.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"

#include <algorithm>
#include <cmath>

namespace RaidClear::Tanks
{
    namespace
    {
        // Off-tanks only look at mobs this close; anything further belongs to whoever is there.
        constexpr float PICKUP_RANGE = 40.0f;
        // Re-separate only once the off-tank has drifted this far inside the wanted distance.
        constexpr float SEPARATION_SLACK = 3.0f;
        // A spot more than this above or below the main tank is a ledge or a pit.
        constexpr float MAX_HEIGHT_DIFF = 5.0f;

        // Bosses whose own strategy (mod-playerbots or ours) places the tanks itself.
        std::vector<uint32> const& SkipBosses(uint32 mapId)
        {
            static std::vector<uint32> const none;
            switch (mapId)
            {
                case MoltenCore::MAP_ID: return MoltenCore::TankSplitSkipBosses();
                default:                 return none;
            }
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
            return false;
        }

        // How far each mob's own AoE reaches, so two of them are kept out of each other's.
        std::vector<SplashRadius> const& SplashRadii(uint32 mapId)
        {
            static std::vector<SplashRadius> const none;
            switch (mapId)
            {
                case MoltenCore::MAP_ID: return MoltenCore::TankSplitSplashRadii();
                default:                 return none;
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

    Unit* PickOffTankTarget(PlayerbotAI* botAI, Player* bot, Player* mainTank, GuidVector const& attackers)
    {
        Unit* mainTarget = mainTank->GetVictim();

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
            if (!unit || unit == mainTarget || !unit->IsAlive() || !bot->IsValidAttackTarget(unit))
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
    if (attackers.size() < 2 || SkipFight(botAI, bot, attackers))
        return false;

    Unit* want = PickOffTankTarget(botAI, bot, mainTank, attackers);
    return want && want != AI_VALUE(Unit*, "current target");
}

bool RcOffTankTargetAction::Execute(Event /*event*/)
{
    Player* mainTank = ActingMainTank(bot->GetGroup());
    if (!IsOffTank(bot, mainTank))
        return false;

    Unit* want = PickOffTankTarget(botAI, bot, mainTank, AI_VALUE(GuidVector, "attackers"));
    if (!want)
        return false;

    // The class tank strategies taunt a target that isn't attacking them ("lose aggro").
    return Attack(want);
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

    // Measured between the two mobs, not the two tanks: the mobs are what swing and stomp, and
    // big ones stand well in front of their tank.
    Unit* anchor = mainTank->GetVictim() ? mainTank->GetVictim() : mainTank;
    return target->GetExactDist2d(anchor) < WantedSeparation(target, anchor) - SEPARATION_SLACK;
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
    Map* map = bot->GetMap();

    // Stand far enough out that my mob, following me, ends up at the wanted distance from the
    // main tank's mob even if it stops between us.
    float const wanted = WantedSeparation(mine, anchor) + mine->GetCombatReach() + 1.0f;

    // Straight away from the main tank's mob first, then fan out to either side; a little
    // closer only if nothing at full distance works.
    float base = anchor->GetAngle(bot);
    if (bot->GetExactDist2d(anchor) < 1.0f)
        base = anchor->GetOrientation() + float(M_PI) / 2.0f;

    static float const offsets[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f };
    static float const scales[] = { 1.0f, 0.85f };
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

            if (MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
                return true;
        }
    }
    return false;
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
    // an off-tank may as well hit it.
    GuidVector const& attackers = AI_VALUE(GuidVector, "attackers");
    if (SkipFight(botAI, bot, attackers))
        return 1.0f;
    return PickOffTankTarget(botAI, bot, mainTank, attackers) ? 0.0f : 1.0f;
}
