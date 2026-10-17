/*
 * mod-raid-clear: Temple of Ahn'Qiraj, the Anubisath Defenders and Sentinels. See AqRaidClear.h
 * for the constants; the Defenders' Explode is the rule both raids share (AqRaidClear.cpp), the
 * reflects Common/SpellReflect.h's.
 *
 *   - Thunderclap or Shadow Storm. In one bot raid a Defender's Thunderclap (25 yd) hit 37 of the
 *     40 every few seconds: the ranged bots and healers stood in it. Once a Defender has shown
 *     Thunderclap, ranged bots and healers stand outside its reach, which is still in reach of
 *     their spells and heals (30 yd from its edge). A Defender with Shadow Storm instead hits
 *     only those 25 yd or more from it: once it has shown that, they come in closer than that.
 *     Its first cast, 5-8 s into the pull, shows which; nothing does before.
 *   - Plague. Whoever has it walks clear of every other raid member and doesn't walk back toward
 *     an enemy while it lasts (40 s): every 3 s it hits everyone within 5 yd of them. A plagued
 *     ranged bot or healer walks by both rules at once, so the two don't send it back and forth.
 *     A Defender pulled again rolls again: what it showed is forgotten as its reflect goes up
 *     (it casts that as it is pulled).
 *   - Not done: Meteor, split among everyone within 8 yd of its second on threat, and the
 *     Anubisath Warriors and Swarmguards it calls, which the tanks pick up like any add.
 *   - Sentinels cast the same Thunderclap and Shadow Storm, and show which from the pull (an
 *     aura): the same rules hold for them at once. Their skull order goes by the same auras
 *     (KillOrderRank).
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
#include <iterator>
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

    // Pulled again (after a wipe or an evade): it rolls its abilities anew.
    void ForgetAoe(Unit const* defender)
    {
        std::lock_guard<std::mutex> guard(sAoeLock);
        sAoe.erase({ defender->GetInstanceId(), defender->GetGUID() });
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
        uint32 aoe;    // SPELL_DEFENDER_THUNDERCLAP or SPELL_DEFENDER_SHADOW_STORM: which rule
        float radius;  // Thunderclap's reach
    };

    // A Sentinel's area spell, from its auras: 0 for none, and for both (a death passed one on):
    // no spot is out of both, so it is left alone.
    uint32 SentinelAoe(Unit const* sentinel)
    {
        bool const thunderclap = sentinel->HasAura(SPELL_SENTINEL_THUNDERCLAP_AURA);
        bool const storm = sentinel->HasAura(SPELL_SENTINEL_SHADOW_STORM_AURA);
        if (thunderclap == storm)
            return 0;
        return thunderclap ? SPELL_DEFENDER_THUNDERCLAP : SPELL_DEFENDER_SHADOW_STORM;
    }

    // The Defenders and Sentinels fighting the raid near the bot whose area spell is known.
    std::vector<DefenderZone> Zones(PlayerbotAI* botAI, Player* bot)
    {
        static float const defenderClap = RaidClear::AhnQiraj::SpellRadius(SPELL_DEFENDER_THUNDERCLAP, 25.0f);
        static float const sentinelClap = RaidClear::AhnQiraj::SpellRadius(SPELL_SENTINEL_THUNDERCLAP, 25.0f);
        std::vector<DefenderZone> zones;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || bot->GetExactDist(unit) > DEFENDER_SEARCH_RANGE)
                continue;
            uint32 aoe = 0;
            float radius = 0.0f;
            if (unit->GetEntry() == NPC_ANUBISATH_DEFENDER)
            {
                // About to Explode: everyone gets out of its 20 yd, Shadow Storm or not.
                if (unit->HasAura(RaidClear::AhnQiraj::SPELL_EXPLODE_FUSE))
                    continue;
                aoe = AoeOf(unit);
                radius = defenderClap;
            }
            else if (unit->GetEntry() == NPC_ANUBISATH_SENTINEL)
            {
                aoe = SentinelAoe(unit);
                radius = sentinelClap;
            }
            if (aoe)
                zones.push_back({ unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), aoe, radius });
        }
        return zones;
    }

    // Who stands by the Defenders' and Sentinels' area spells: ranged bots and healers. Melee and
    // tanks are in Thunderclap anyway, and never in Shadow Storm.
    bool MindsDefenderAoe(Player* bot) { return PlayerbotAI::IsRanged(bot) && !PlayerbotAI::IsTank(bot); }

    // Yards a spot has to spare (less than 0: it is hit).
    float AoeGap(std::vector<DefenderZone> const& zones, float botReach, float x, float y, float z)
    {
        float gap = 1000.0f;
        for (DefenderZone const& zone : zones)
        {
            if (zone.aoe == SPELL_DEFENDER_THUNDERCLAP)
            {
                // The core takes in everyone whose edge is within its reach of the caster.
                float const dist = std::sqrt((x - zone.x) * (x - zone.x) + (y - zone.y) * (y - zone.y) +
                                             (z - zone.z) * (z - zone.z));
                gap = std::min(gap, dist - zone.radius - botReach - THUNDERCLAP_KEEP);
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

// --- Where to stand ---------------------------------------------------------

namespace
{
    // Both rules a bot has at once, read once per look: a plagued ranged bot next to a Shadow
    // Storm Defender has to be inside its dead zone and clear of everyone, and walking for one
    // rule alone would take it straight out of the other.
    struct DefenderRules
    {
        std::vector<DefenderZone> zones;  // empty: no area rule
        std::vector<Member> others;       // empty: not plagued
        bool plagued = false;
        float botReach = 0.0f;
    };

    DefenderRules ReadDefenderRules(PlayerbotAI* botAI, Player* bot)
    {
        DefenderRules rules;
        rules.botReach = bot->GetObjectSize();
        if (MindsDefenderAoe(bot))
            rules.zones = Zones(botAI, bot);
        rules.plagued = bot->HasAura(SPELL_DEFENDER_PLAGUE);
        if (rules.plagued)
            rules.others = OthersNear(bot);
        return rules;
    }

    float RulesGap(DefenderRules const& rules, float x, float y, float z)
    {
        float gap = 1000.0f;
        if (!rules.zones.empty())
            gap = AoeGap(rules.zones, rules.botReach, x, y, z);
        if (rules.plagued)
            gap = std::min(gap, PlagueGap(rules.others, x, y, z));
        return gap;
    }

    bool BreaksRules(DefenderRules const& rules, Player* bot)
    {
        return RulesGap(rules, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) < 0.0f;
    }

    bool WalkByRules(RaidClear::WayOutWalk& walk, DefenderRules const& rules)
    {
        auto const gapAt = [&rules](float x, float y, float z) { return RulesGap(rules, x, y, z); };
        return walk.Go(gapAt, 0.0f, DEFENDER_SPOT, DEFENDER_WORTH_IT, DEFENDER_SEARCH);
    }
}

// --- Thunderclap and Shadow Storm -------------------------------------------

bool RcAq40DefenderAoeTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove() || !MindsDefenderAoe(bot))
        return false;
    DefenderRules const rules = ReadDefenderRules(botAI, bot);
    return !rules.zones.empty() && BreaksRules(rules, bot);
}

bool RcAq40DefenderAoeMoveAction::Execute(Event /*event*/)
{
    DefenderRules const rules = ReadDefenderRules(botAI, bot);
    return !rules.zones.empty() && BreaksRules(rules, bot) && WalkByRules(_walk, rules);
}

// --- Plague -----------------------------------------------------------------

bool RcAq40PlagueTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove() || !bot->HasAura(SPELL_DEFENDER_PLAGUE))
        return false;
    return BreaksRules(ReadDefenderRules(botAI, bot), bot);
}

bool RcAq40PlagueMoveAwayAction::Execute(Event /*event*/)
{
    DefenderRules const rules = ReadDefenderRules(botAI, bot);
    return rules.plagued && BreaksRules(rules, bot) && WalkByRules(_walk, rules);
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
        // Its reflect goes up as it is pulled, when it rolls its abilities: whatever it showed in
        // an earlier pull no longer holds.
        else if (spellInfo->Id == SPELL_DEFENDER_SHADOW_FROST_REFLECT || spellInfo->Id == SPELL_DEFENDER_FIRE_ARCANE_REFLECT)
            ForgetAoe(caster);
    }
};

// --- Sentinels' kill order --------------------------------------------------

uint8 RaidClear::TempleOfAhnQiraj::KillOrderRank(Unit const* unit, uint8 rank)
{
    if (!unit || unit->GetEntry() != NPC_ANUBISATH_SENTINEL)
        return rank;

    // The raid guide's order: the abilities that do least harm passed on to the others go first.
    static constexpr uint32 ORDER[] = {
        SPELL_SENTINEL_FIRE_ARCANE_REFLECT, SPELL_SENTINEL_SHADOW_FROST_REFLECT, SPELL_SENTINEL_MENDING,
        SPELL_SENTINEL_KNOCK_AWAY,          SPELL_SENTINEL_THORNS,               SPELL_SENTINEL_MORTAL_STRIKE,
        SPELL_SENTINEL_MANA_BURN,           SPELL_SENTINEL_THUNDERCLAP_AURA,     SPELL_SENTINEL_SHADOW_STORM_AURA,
    };
    // By its worst ability: one that got Shadow Storm from a dead brother passes it on as it dies.
    uint8 worst = 0;
    for (uint8 i = 0; i < std::size(ORDER); ++i)
        if (unit->HasAura(ORDER[i]))
            worst = i;
    return rank + worst;
}

void RaidClear::TempleOfAhnQiraj::AddDefenderScripts()
{
    new RaidClearDefenderSpellScript();
}
