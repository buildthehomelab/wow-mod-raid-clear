/*
 * mod-raid-clear: Temple of Ahn'Qiraj, Viscidus. See AqRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"

#include "CellImpl.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <list>

using namespace RaidClear::TempleOfAhnQiraj;

std::vector<RaidClear::KillOrderEntry> const& RaidClear::TempleOfAhnQiraj::KillOrder()
{
    // Each glob that reaches the middle of the room heals him.
    static std::vector<KillOrderEntry> const table = {
        { NPC_GLOB_OF_VISCIDUS, 0 },
    };
    return table;
}

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

    // A Toxin cloud on the floor, or one to come: every slime leaves a new one round itself every
    // 10 s.
    struct Cloud
    {
        float x;
        float y;
        float radius;
    };

    float CloudRadius()
    {
        static float const radius = []
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_TOXIN_CLOUD);
            float const r = info ? info->Effects[EFFECT_0].CalcRadius() : 0.0f;
            return r > 0.0f ? r : 5.0f;
        }();
        return radius;
    }

    // Every cloud in the room, and every slime.
    std::vector<Cloud> FindClouds(Creature const* boss)
    {
        std::vector<Cloud> clouds;

        std::list<WorldObject*> objects;
        Acore::AllWorldObjectsInRange check(boss, TOXIN_ROOM_SEARCH);
        Acore::WorldObjectListSearcher<Acore::AllWorldObjectsInRange> searcher(boss, objects, check,
                                                                               GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
        Cell::VisitObjects(boss, searcher, TOXIN_ROOM_SEARCH);
        for (WorldObject* object : objects)
        {
            DynamicObject const* dynObj = object->ToDynObject();
            if (dynObj && dynObj->GetSpellId() == SPELL_TOXIN_CLOUD)
                clouds.push_back({ dynObj->GetPositionX(), dynObj->GetPositionY(), dynObj->GetRadius() });
        }

        std::list<Creature*> slimes;
        boss->GetCreatureListWithEntryInGrid(slimes, NPC_TOXIC_SLIME, TOXIN_ROOM_SEARCH);
        for (Creature const* slime : slimes)
            if (slime->IsAlive())
                clouds.push_back({ slime->GetPositionX(), slime->GetPositionY(), CloudRadius() });
        return clouds;
    }

    // The clouds in the bot's copy of the room, looked up again after CLOUDS_FRESH_MS: the
    // triggers and the hold multiplier of the whole raid ask many times a second. One instance's
    // bots update one after another on one thread.
    constexpr uint32 CLOUDS_FRESH_MS = 200;
    std::vector<Cloud> const& CloudsNear(Player* bot, Creature const* boss)
    {
        thread_local uint32 tInstanceId = 0;
        thread_local ObjectGuid tBoss;
        thread_local uint32 tAtMs = 0;
        thread_local std::vector<Cloud> tClouds;
        uint32 const now = getMSTime();
        if (tInstanceId != bot->GetInstanceId() || tBoss != boss->GetGUID() || !tAtMs ||
            getMSTimeDiff(tAtMs, now) >= CLOUDS_FRESH_MS)
        {
            tClouds = FindClouds(boss);
            tInstanceId = bot->GetInstanceId();
            tBoss = boss->GetGUID();
            tAtMs = now ? now : 1;
        }
        return tClouds;
    }

    // Yards from a spot to the edge of the nearest cloud (less than 0 inside one).
    float ToxinGap(std::vector<Cloud> const& clouds, float x, float y)
    {
        float gap = 1000.0f;
        for (Cloud const& cloud : clouds)
            gap = std::min(gap, std::hypot(cloud.x - x, cloud.y - y) - cloud.radius);
        return gap;
    }

    float ToxinGap(std::vector<Cloud> const& clouds, Position const* pos)
    {
        return ToxinGap(clouds, pos->GetPositionX(), pos->GetPositionY());
    }

    // He can't follow anyone: frozen, shattered, stunned or rooted.
    bool StuckInPlace(Unit const* boss)
    {
        return boss->HasAura(SPELL_VISCIDUS_FREEZE) || boss->HasAura(SPELL_VISCIDUS_INVIS) ||
               boss->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT);
    }

    // The tank he is on, with him standing in a cloud and able to follow it out.
    bool MustWalkHimOut(Player* bot, Unit const* boss, std::vector<Cloud> const& clouds)
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
    std::vector<Cloud> const clouds = CloudsNear(bot, boss);
    return ToxinGap(clouds, bot) < TOXIN_KEEP || MustWalkHimOut(bot, boss, clouds);
}

bool RcAq40LeaveToxinAction::Execute(Event /*event*/)
{
    Creature* boss = FightingViscidus(bot);
    if (!boss)
        return false;
    std::vector<Cloud> const clouds = CloudsNear(bot, boss);

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
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action);
    if (!walks && !dynamic_cast<CastReachTargetSpellAction*>(action))
        return 1.0f;
    if (!bot->IsInCombat())
        return 1.0f;

    // Only walks toward an enemy: a healer still walks to whoever it has to heal.
    Unit* target = action->GetTarget();
    if (!target || target == bot || !bot->IsValidAttackTarget(target))
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
