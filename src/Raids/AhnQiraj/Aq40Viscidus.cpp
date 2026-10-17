/*
 * mod-raid-clear: Temple of Ahn'Qiraj, Viscidus. See AqRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"
#include "Common/SplashSpells.h"

#include "Creature.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <list>

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    Creature* FightingViscidus(Player* bot) { return RaidClear::FightingCreature(bot, { NPC_VISCIDUS }); }

    // Neither frozen nor shattered: frost hits count. He stuns himself the moment he shatters and
    // stays stunned until the last glob is gone (he only turns invisible 3 s in, and visible again
    // when the first glob reaches him): until then the globs come first.
    bool CanBeFrozen(Unit const* boss)
    {
        return !boss->HasAura(SPELL_VISCIDUS_FREEZE) && !boss->HasAura(SPELL_VISCIDUS_INVIS) &&
               !boss->HasAura(SPELL_VISCIDUS_STUN_SELF);
    }

    // --- Frost ------------------------------------------------------------

    // The frost spells a damage dealer of this class has, in the order it tries them. A spell id
    // when a lower rank is the point (rank 1 Frostbolt casts in 1.5 s), else a name for
    // playerbots to find the bot's best rank of.
    struct FrostSpell
    {
        uint32 id;
        char const* name;
    };

    std::vector<FrostSpell> const& FrostSpellsFor(Player const* bot)
    {
        static std::vector<FrostSpell> const none;
        static std::vector<FrostSpell> const mage = { { SPELL_FROSTBOLT_RANK_1, nullptr }, { 0, "frostbolt" } };
        static std::vector<FrostSpell> const shaman = { { 0, "frost shock" } };
        static std::vector<FrostSpell> const deathKnight = { { 0, "howling blast" }, { 0, "icy touch" } };
        switch (bot->getClass())
        {
            case CLASS_MAGE:         return mage;
            case CLASS_SHAMAN:       return shaman;
            case CLASS_DEATH_KNIGHT: return deathKnight;
            default:                 return none;
        }
    }

    // Whether the bot can cast it at him from where it stands. Playerbots' own check lets a spell
    // through out of range (the walk there is another action's job), so the range is checked
    // here, edge to edge as the core measures it, with the bot's range talents.
    bool CanCastFrost(PlayerbotAI* botAI, Player* bot, FrostSpell const& spell, Unit* boss)
    {
        uint32 const spellId =
            spell.id ? spell.id : botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell.name)->Get();
        if (!spellId || !bot->HasSpell(spellId))
            return false;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || !bot->IsWithinCombatRange(boss, info->GetMaxRange(false, bot)))
            return false;
        return spell.id ? botAI->CanCastSpell(spell.id, boss) : botAI->CanCastSpell(spell.name, boss);
    }

    // A damage dealer that can cast one of its frost spells at him now.
    FrostSpell const* ReadyFrostSpell(PlayerbotAI* botAI, Player* bot, Unit* boss)
    {
        std::vector<FrostSpell> const& spells = FrostSpellsFor(bot);
        if (spells.empty() || PlayerbotAI::IsHeal(bot) || PlayerbotAI::IsTank(bot))
            return nullptr;
        for (FrostSpell const& spell : spells)
            if (CanCastFrost(botAI, bot, spell, boss))
                return bot->IsWithinLOSInMap(boss) ? &spell : nullptr;
        return nullptr;
    }

    // --- Toxin ------------------------------------------------------------

    // The clouds in the bot's copy of the room, looked up again after CLOUDS_FRESH_MS: the
    // triggers and the hold multiplier of the whole raid ask many times a second. One instance's
    // bots update one after another on one thread.
    constexpr uint32 CLOUDS_FRESH_MS = 200;
    std::vector<ToxinCloud> const& CloudsNear(Player* bot, Creature const* boss)
    {
        thread_local uint32 tInstanceId = 0;
        thread_local ObjectGuid tBoss;
        thread_local uint32 tAtMs = 0;
        thread_local std::vector<ToxinCloud> tClouds;
        uint32 const now = getMSTime();
        if (tInstanceId != bot->GetInstanceId() || tBoss != boss->GetGUID() || !tAtMs ||
            getMSTimeDiff(tAtMs, now) >= CLOUDS_FRESH_MS)
        {
            tClouds = FindToxinClouds(boss, NPC_TOXIC_SLIME, TOXIN_ROOM_SEARCH);
            tInstanceId = bot->GetInstanceId();
            tBoss = boss->GetGUID();
            tAtMs = now ? now : 1;
        }
        return tClouds;
    }

    // --- Globs -------------------------------------------------------------

    bool IsGlob(Unit const* unit) { return unit && unit->GetEntry() == NPC_GLOB_OF_VISCIDUS; }

    // Him while he lives, in combat or not: shattered, he drops his threat list.
    Creature* ShatteredViscidus(Player* bot)
    {
        Creature* boss = RaidClear::InstanceCreature(bot, { NPC_VISCIDUS });
        return boss && boss->IsAlive() ? boss : nullptr;
    }

    // A bot that goes for the globs: neither a tank nor a healer.
    bool Hunts(Player* bot) { return !PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsHeal(bot); }

    // The living globs round him (he is in the middle of the room while they crawl), looked up
    // again after GLOBS_FRESH_MS like the clouds. GUIDs, not pointers: a glob that arrives is gone
    // before the next look.
    constexpr uint32 GLOBS_FRESH_MS = 250;
    std::vector<ObjectGuid> const& Globs(Player* bot, Creature const* boss)
    {
        thread_local uint32 tInstanceId = 0;
        thread_local uint32 tAtMs = 0;
        thread_local std::vector<ObjectGuid> tGlobs;
        uint32 const now = getMSTime();
        if (tInstanceId != bot->GetInstanceId() || !tAtMs || getMSTimeDiff(tAtMs, now) >= GLOBS_FRESH_MS)
        {
            std::list<Creature*> found;
            boss->GetCreatureListWithEntryInGrid(found, NPC_GLOB_OF_VISCIDUS, TOXIN_ROOM_SEARCH);
            tGlobs.clear();
            for (Creature const* glob : found)
                if (glob->IsAlive())
                    tGlobs.push_back(glob->GetGUID());
            tInstanceId = bot->GetInstanceId();
            tAtMs = now ? now : 1;
        }
        return tGlobs;
    }

    std::vector<Creature*> LivingGlobs(Player* bot, Creature const* boss)
    {
        std::vector<Creature*> globs;
        for (ObjectGuid const& guid : Globs(bot, boss))
            if (Creature* glob = ObjectAccessor::GetCreature(*bot, guid))
                if (glob->IsAlive() && bot->IsValidAttackTarget(glob))
                    globs.push_back(glob);
        return globs;
    }

    // How many raid members other than the bot have this glob selected.
    uint32 Crowd(Player* bot, Unit const* glob)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return 0;
        uint32 crowd = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot && member->IsAlive() && member->GetTarget() == glob->GetGUID())
                ++crowd;
        }
        return crowd;
    }

    // The glob this damage dealer goes for. One skull put the whole raid on one glob at a time,
    // and most of them reached him: each bot takes the glob that costs it least (see
    // GLOB_CROWD_YARDS), so they spread over the globs and every glob near someone dies.
    Creature* GlobFor(PlayerbotAI* botAI, Player* bot, Creature const* boss)
    {
        bool const ranged = PlayerbotAI::IsRanged(bot);
        float const range = botAI->GetRange("spell");
        Creature* best = nullptr;
        float bestCost = 0.0f;
        for (Creature* glob : LivingGlobs(bot, boss))
        {
            float cost = bot->GetExactDist(glob);
            if (ranged)
            {
                cost = std::hypot(glob->GetPositionX() - VISCIDUS_ROOM_X, glob->GetPositionY() - VISCIDUS_ROOM_Y);
                if (!bot->IsWithinCombatRange(glob, range) || !bot->IsWithinLOSInMap(glob))
                    cost += GLOB_OUT_OF_RANGE_YARDS;
            }
            cost += GLOB_CROWD_YARDS * float(Crowd(bot, glob));
            if (!best || cost < bestCost)
            {
                best = glob;
                bestCost = cost;
            }
        }
        return best;
    }

    // Area spells worth casting at globs, by playerbots name: what hits at once, as a glob crawls
    // through a Blizzard or a Consecration in a few seconds and dies the moment it arrives.
    constexpr std::array<char const*, 11> GLOB_AOE_SPELLS = {
        "arcane explosion", "chain lightning", "multi-shot", "whirlwind", "cleave", "thunder clap",
        "blood boil", "holy nova", "consecration", "divine storm", "swipe (cat)",
    };

    struct GlobAoe
    {
        char const* spell = nullptr;
        Unit* at = nullptr;
        uint32 hits = 0;
    };

    uint32 GlobsWithin(std::vector<Creature*> const& globs, Unit const* from, float radius)
    {
        uint32 hits = 0;
        for (Creature const* glob : globs)
            if (from->GetExactDist(glob) <= radius + glob->GetCombatReach())
                ++hits;
        return hits;
    }

    // The bot's area spell that hits the most globs from where it stands (at least GLOB_AOE_MIN),
    // and where to cast it.
    GlobAoe BestGlobAoe(PlayerbotAI* botAI, Player* bot, std::vector<Creature*> const& globs)
    {
        GlobAoe best;
        if (globs.size() < GLOB_AOE_MIN)
            return best;
        for (char const* name : GLOB_AOE_SPELLS)
        {
            uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get();
            SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
            if (!info || !bot->HasSpell(spellId))
                continue;
            RaidClear::Splash::Reach const reach = RaidClear::Splash::ReachOf(botAI, bot, name);
            if (!reach.Any())
                continue;

            GlobAoe here;
            here.spell = name;
            if (reach.aroundCaster > 0.0f && reach.aroundTarget <= 0.0f && !reach.chainHops)
            {
                // Round the bot (Arcane Explosion, Whirlwind, Consecration).
                here.at = bot;
                here.hits = GlobsWithin(globs, bot, reach.aroundCaster);
            }
            else
            {
                // At a glob it can reach, edge to edge as the core measures a cast's range.
                float const maxRange = info->GetMaxRange(false, bot);
                for (Creature* glob : globs)
                {
                    bool const inReach = maxRange <= NOMINAL_MELEE_RANGE ? bot->IsWithinMeleeRange(glob)
                                                                         : bot->IsWithinCombatRange(glob, maxRange);
                    if (!inReach || !bot->IsWithinLOSInMap(glob))
                        continue;
                    uint32 hits = 1;
                    if (reach.chainHops)
                        hits += std::min<uint32>(reach.chainHops, GlobsWithin(globs, glob, reach.chainJump +
                                                                                               glob->GetCombatReach()) - 1);
                    else
                        hits = GlobsWithin(globs, glob, reach.aroundTarget);
                    if (hits > here.hits)
                    {
                        here.hits = hits;
                        here.at = glob;
                    }
                }
            }
            if (here.at && here.hits >= GLOB_AOE_MIN && here.hits > best.hits && botAI->CanCastSpell(name, here.at))
                best = here;
        }
        return best;
    }

    // He can't follow anyone: frozen, shattered, stunned or rooted.
    bool StuckInPlace(Unit const* boss)
    {
        return boss->HasAura(SPELL_VISCIDUS_FREEZE) || boss->HasAura(SPELL_VISCIDUS_INVIS) ||
               boss->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT);
    }

    // The tank he is on, with him standing in a cloud and able to follow it out.
    bool MustWalkHimOut(Player* bot, Unit const* boss, std::vector<ToxinCloud> const& clouds)
    {
        return boss->GetVictim() == bot && PlayerbotAI::IsTank(bot) && !StuckInPlace(boss) &&
               ToxinGap(clouds, boss) < TOXIN_BOSS_KEEP;
    }
}

// --- Frost ----------------------------------------------------------------

bool RcAq40ViscidusFrostTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    Creature* boss = FightingViscidus(bot);
    if (!boss || !CanBeFrozen(boss))
        return false;
    return ReadyFrostSpell(botAI, bot, boss) != nullptr;
}

bool RcAq40ViscidusFrostAction::Execute(Event /*event*/)
{
    Creature* boss = FightingViscidus(bot);
    if (!boss || !CanBeFrozen(boss))
        return false;
    FrostSpell const* spell = ReadyFrostSpell(botAI, bot, boss);
    if (!spell)
        return false;
    return spell->id ? botAI->CastSpell(spell->id, boss) : botAI->CastSpell(spell->name, boss);
}

// --- Toxin ----------------------------------------------------------------

bool RcAq40ToxinTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    Creature* boss = FightingViscidus(bot);
    if (!boss)
        return false;
    std::vector<ToxinCloud> const clouds = CloudsNear(bot, boss);
    return ToxinGap(clouds, bot) < TOXIN_KEEP || MustWalkHimOut(bot, boss, clouds);
}

bool RcAq40LeaveToxinAction::Execute(Event /*event*/)
{
    Creature* boss = FightingViscidus(bot);
    if (!boss)
        return false;
    std::vector<ToxinCloud> const clouds = CloudsNear(bot, boss);

    if (MustWalkHimOut(bot, boss, clouds))
    {
        // He follows his victim and stops a melee reach short of it, on the side he comes from:
        // a spot is as good as the worse of the two, the tank's and his. Never toward the
        // entrance, where he evades.
        float const reach = boss->GetCombatReach() + bot->GetCombatReach();
        float const bossX = boss->GetPositionX();
        float const bossY = boss->GetPositionY();
        auto const gapAt = [&clouds, reach, bossX, bossY](float x, float y, float /*z*/)
        {
            if (std::hypot(x - RESET_X, y - RESET_Y) < RESET_KEEP_AWAY)
                return -100.0f;
            float const toBoss = std::hypot(bossX - x, bossY - y);
            float himX = bossX;
            float himY = bossY;
            if (toBoss > reach)
            {
                himX = x + (bossX - x) / toBoss * reach;
                himY = y + (bossY - y) / toBoss * reach;
            }
            return std::min(ToxinGap(clouds, x, y), ToxinGap(clouds, himX, himY));
        };
        // Where the tank stands already does: he is on his way to it.
        if (gapAt(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= TOXIN_SPOT)
            return false;
        return _walk.Go(gapAt, TOXIN_BOSS_KEEP, TOXIN_SPOT, TOXIN_WORTH_IT, TOXIN_SEARCH + reach);
    }

    if (ToxinGap(clouds, bot) >= TOXIN_KEEP)
        return false;

    // Melee on him while he is frozen: only a spot they can still hit him from, else they stay.
    // The hits are what shatter him, and a spot out of reach would only send them back in.
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target == boss && boss->HasAura(SPELL_VISCIDUS_FREEZE) && !PlayerbotAI::IsRanged(bot))
    {
        float const reach = boss->GetMeleeRange(bot) - MELEE_SLACK;
        float const bossX = boss->GetPositionX();
        float const bossY = boss->GetPositionY();
        auto const gapAt = [&clouds, reach, bossX, bossY](float x, float y, float /*z*/)
        {
            if (std::hypot(x - bossX, y - bossY) > reach)
                return -100.0f;
            return ToxinGap(clouds, x, y);
        };
        return _walk.Go(gapAt, TOXIN_KEEP, TOXIN_SPOT, TOXIN_WORTH_IT, TOXIN_SEARCH);
    }

    auto const gapAt = [&clouds](float x, float y, float /*z*/) { return ToxinGap(clouds, x, y); };
    return _walk.Go(gapAt, TOXIN_KEEP, TOXIN_SPOT, TOXIN_WORTH_IT, TOXIN_SEARCH);
}

float RcAq40ToxinHoldMultiplier::GetValue(Action* action)
{
    Unit* target = RaidClear::AhnQiraj::WalkTarget(action, bot);
    if (!target)
        return 1.0f;
    Creature* boss = FightingViscidus(bot);
    if (!boss)
        return 1.0f;
    // Frozen, he can't be walked out, and the melee hits are what shatter him.
    if (target == boss && boss->HasAura(SPELL_VISCIDUS_FREEZE))
        return 1.0f;
    // Only what stands right in a cloud: the tank he is on walks him out of one.
    return ToxinGap(CloudsNear(bot, boss), target) < 0.0f ? 0.0f : 1.0f;
}

// --- Globs ----------------------------------------------------------------

bool RcAq40GlobTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !Hunts(bot))
        return false;
    Creature* boss = ShatteredViscidus(bot);
    if (!boss || Globs(bot, boss).empty())
        return false;
    // Already on one: playerbots keeps hitting it.
    Unit* current = AI_VALUE(Unit*, "current target");
    if (IsGlob(current) && current->IsAlive())
        return false;
    return GlobFor(botAI, bot, boss) != nullptr;
}

bool RcAq40KillGlobAction::Execute(Event /*event*/)
{
    Creature* boss = ShatteredViscidus(bot);
    if (!boss)
        return false;
    Creature* glob = GlobFor(botAI, bot, boss);
    return glob && Attack(glob);
}

float RcAq40GlobMultiplier::GetValue(Action* action)
{
    if (!dynamic_cast<AttackAction*>(action) || dynamic_cast<RcAq40KillGlobAction*>(action))
        return 1.0f;
    if (!bot->IsInCombat() || !Hunts(bot))
        return 1.0f;
    Creature* boss = ShatteredViscidus(bot);
    if (!boss || Globs(bot, boss).empty())
        return 1.0f;
    // On a glob: stays on it (the skull would pull everyone onto the same one). On none: only a
    // glob.
    Unit* current = AI_VALUE(Unit*, "current target");
    Unit* target = action->GetTarget();
    if (IsGlob(current) && current->IsAlive())
        return target == current ? 1.0f : 0.0f;
    return IsGlob(target) ? 1.0f : 0.0f;
}

bool RcAq40GlobAoeTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !Hunts(bot))
        return false;
    Creature* boss = ShatteredViscidus(bot);
    if (!boss || Globs(bot, boss).size() < GLOB_AOE_MIN)
        return false;
    return BestGlobAoe(botAI, bot, LivingGlobs(bot, boss)).spell != nullptr;
}

bool RcAq40GlobAoeAction::Execute(Event /*event*/)
{
    Creature* boss = ShatteredViscidus(bot);
    if (!boss)
        return false;
    GlobAoe const aoe = BestGlobAoe(botAI, bot, LivingGlobs(bot, boss));
    return aoe.spell && botAI->CastSpell(aoe.spell, aoe.at);
}
