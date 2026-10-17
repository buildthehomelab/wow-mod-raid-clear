/*
 * mod-raid-clear: Temple of Ahn'Qiraj, the Anubisath Defenders. See AqRaidClear.h for the
 * constants; their Explode is the rule both raids share (AqRaidClear.cpp), their reflects
 * Common/SpellReflect.h's.
 *
 *   - Thunderclap or Shadow Storm. In one bot raid a Defender's Thunderclap (25 yd) hit 37 of the
 *     40 every few seconds: the ranged bots and healers stood in it. Once a Defender has shown
 *     Thunderclap, ranged bots and healers stand outside its reach, which is still in reach of
 *     their spells and heals (30 yd from its edge). A Defender with Shadow Storm instead hits
 *     only those 25 yd or more from it: once it has shown that, they come in closer than that.
 *     Its first cast, 5-8 s into the pull, shows which; nothing does before.
 *   - Plague. Whoever has it walks clear of every other raid member and doesn't walk back toward
 *     an enemy while it lasts (40 s): every 3 s it hits everyone within 5 yd of them.
 *   - Not done: Meteor, split among everyone within 8 yd of its second on threat, and the
 *     Anubisath Warriors and Swarmguards it calls, which the tanks pick up like any add.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "RaidClearConfig.h"

#include "AllSpellScript.h"
#include "Creature.h"
#include "Group.h"
#include "Playerbots.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    // Which of the two area spells each Defender has shown, by instance and Defender. Map updates
    // move between threads from one tick to the next, so it is shared and locked. A handful of
    // entries per instance; cleared now and then.
    using DefenderKey = std::pair<uint32, ObjectGuid>;
    std::mutex sAoeLock;
    std::map<DefenderKey, uint32> sAoe;
    constexpr size_t AOE_MEMORY_MAX = 256;

    void NoteAoe(Unit const* defender, uint32 spellId)
    {
        std::lock_guard<std::mutex> guard(sAoeLock);
        if (sAoe.size() >= AOE_MEMORY_MAX)
            sAoe.clear();
        sAoe[{ defender->GetInstanceId(), defender->GetGUID() }] = spellId;
    }

    uint32 AoeOf(Unit const* defender)
    {
        std::lock_guard<std::mutex> guard(sAoeLock);
        auto it = sAoe.find({ defender->GetInstanceId(), defender->GetGUID() });
        return it == sAoe.end() ? 0 : it->second;
    }

    struct DefenderZone
    {
        float x;
        float y;
        float z;
        uint32 aoe;
    };

    // The Defenders fighting the raid near the bot that have shown their area spell.
    std::vector<DefenderZone> Zones(PlayerbotAI* botAI, Player* bot)
    {
        std::vector<DefenderZone> zones;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || unit->GetEntry() != NPC_ANUBISATH_DEFENDER || !unit->IsAlive() ||
                bot->GetExactDist(unit) > DEFENDER_SEARCH_RANGE)
                continue;
            // About to Explode: everyone gets out of its 20 yd, Shadow Storm or not.
            if (unit->HasAura(RaidClear::AhnQiraj::SPELL_EXPLODE_FUSE))
                continue;
            if (uint32 const aoe = AoeOf(unit))
                zones.push_back({ unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), aoe });
        }
        return zones;
    }

    // Who stands by the Defenders' area spells: ranged bots and healers. Melee and tanks are in
    // Thunderclap anyway, and never in Shadow Storm.
    bool MindsDefenderAoe(Player* bot) { return PlayerbotAI::IsRanged(bot) && !PlayerbotAI::IsTank(bot); }

    // Yards a spot has to spare (less than 0: it is hit).
    float AoeGap(std::vector<DefenderZone> const& zones, float botReach, float x, float y, float z)
    {
        static float const thunderclap = RaidClear::AhnQiraj::SpellRadius(SPELL_DEFENDER_THUNDERCLAP, 25.0f);
        float gap = 1000.0f;
        for (DefenderZone const& zone : zones)
        {
            if (zone.aoe == SPELL_DEFENDER_THUNDERCLAP)
            {
                // The core takes in everyone whose edge is within its reach of the caster.
                float const dist = std::sqrt((x - zone.x) * (x - zone.x) + (y - zone.y) * (y - zone.y) +
                                             (z - zone.z) * (z - zone.z));
                gap = std::min(gap, dist - thunderclap - botReach - THUNDERCLAP_KEEP);
            }
            else
                gap = std::min(gap, SHADOW_STORM_SAFE - SHADOW_STORM_KEEP - std::hypot(x - zone.x, y - zone.y));
        }
        return gap;
    }

    // --- Plague -----------------------------------------------------------

    struct Member
    {
        float x;
        float y;
        float z;
        float reach;
    };

    // The other living raid members near the bot.
    std::vector<Member> OthersNear(Player* bot)
    {
        std::vector<Member> others;
        Group* group = bot->GetGroup();
        if (!group)
            return others;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot && member->IsAlive() && member->IsInMap(bot) && bot->GetExactDist(member) < 40.0f)
                others.push_back({ member->GetPositionX(), member->GetPositionY(), member->GetPositionZ(),
                                   member->GetObjectSize() });
        }
        return others;
    }

    float PlagueGap(std::vector<Member> const& others, float x, float y, float z)
    {
        static float const radius = RaidClear::AhnQiraj::SpellRadius(SPELL_DEFENDER_PLAGUE_HIT, 5.0f);
        float gap = 1000.0f;
        for (Member const& other : others)
        {
            float const dist = std::sqrt((x - other.x) * (x - other.x) + (y - other.y) * (y - other.y) +
                                         (z - other.z) * (z - other.z));
            gap = std::min(gap, dist - radius - other.reach - PLAGUE_KEEP);
        }
        return gap;
    }
}

// --- Thunderclap and Shadow Storm -------------------------------------------

bool RcAq40DefenderAoeTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove() || !MindsDefenderAoe(bot))
        return false;
    std::vector<DefenderZone> const zones = Zones(botAI, bot);
    return !zones.empty() &&
           AoeGap(zones, bot->GetObjectSize(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) < 0.0f;
}

bool RcAq40DefenderAoeMoveAction::Execute(Event /*event*/)
{
    std::vector<DefenderZone> const zones = Zones(botAI, bot);
    float const botReach = bot->GetObjectSize();
    if (zones.empty() || AoeGap(zones, botReach, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= 0.0f)
        return false;
    auto const gapAt = [&zones, botReach](float x, float y, float z) { return AoeGap(zones, botReach, x, y, z); };
    return _walk.Go(gapAt, 0.0f, DEFENDER_SPOT, DEFENDER_WORTH_IT, DEFENDER_SEARCH);
}

// --- Plague -----------------------------------------------------------------

bool RcAq40PlagueTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove() || !bot->HasAura(SPELL_DEFENDER_PLAGUE))
        return false;
    return PlagueGap(OthersNear(bot), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) < 0.0f;
}

bool RcAq40PlagueMoveAwayAction::Execute(Event /*event*/)
{
    if (!bot->HasAura(SPELL_DEFENDER_PLAGUE))
        return false;
    std::vector<Member> const others = OthersNear(bot);
    if (PlagueGap(others, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= 0.0f)
        return false;
    auto const gapAt = [&others](float x, float y, float z) { return PlagueGap(others, x, y, z); };
    return _walk.Go(gapAt, 0.0f, DEFENDER_SPOT, DEFENDER_WORTH_IT, DEFENDER_SEARCH);
}

float RcAq40DefenderHoldMultiplier::GetValue(Action* action)
{
    // Pure walks toward an enemy: a ranged bot's spells still go out from where it stands.
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action) ||
        !bot->HasAura(SPELL_DEFENDER_PLAGUE))
        return 1.0f;
    return RaidClear::AhnQiraj::WalkTarget(action, bot) ? 0.0f : 1.0f;
}

// The first Thunderclap or Shadow Storm of each Defender tells which it has. Both are triggered
// instants: by the time this hook runs the cast has landed, which is all it needs.
class RaidClearDefenderSpellScript : public AllSpellScript
{
public:
    RaidClearDefenderSpellScript() : AllSpellScript("RaidClearDefenderSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || caster->GetEntry() != NPC_ANUBISATH_DEFENDER || caster->GetMapId() != MAP_ID)
            return;
        if (spellInfo->Id == SPELL_DEFENDER_THUNDERCLAP || spellInfo->Id == SPELL_DEFENDER_SHADOW_STORM)
            NoteAoe(caster, spellInfo->Id);
    }
};

void RaidClear::TempleOfAhnQiraj::AddDefenderScripts()
{
    new RaidClearDefenderSpellScript();
}
