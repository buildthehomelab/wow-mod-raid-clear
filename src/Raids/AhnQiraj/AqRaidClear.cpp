/*
 * mod-raid-clear: Ahn'Qiraj, both raids. See AqRaidClear.h. The strategies, the kill-order and
 * tank tables, and what the two raids share (Anubisath Explode); each boss is in a file of its
 * own (Aq20Ossirian.cpp, Aq20Kurinnaxx.cpp, Aq20Moam.cpp, Aq20Ayamiss.cpp, Aq40Skeram.cpp,
 * Aq40Viscidus.cpp, Aq40Sartura.cpp, Aq40Fankriss.cpp, Aq40BugTrio.cpp, Aq40TwinEmperors.cpp, Aq40Huhuran.cpp,
 * Aq40Defenders.cpp, Aq40NatureResistance.cpp).
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/Charmed.h"
#include "Common/SpellReflect.h"
#include "RaidClearConfig.h"

#include "AllSpellScript.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <list>

using namespace RaidClear::AhnQiraj;

void RaidClearAq20Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Under a reflect: the spell of the other school that playerbots ranks below the wand (a
    // frost mage's Fireball), just above it. "rc spell reflect" zeroes whichever is reflected.
    triggers.push_back(new TriggerNode("rc mage target reflects", { NextAction("fireball", 5.15f) }));
    triggers.push_back(new TriggerNode("rc warlock target reflects", { NextAction("immolate", 5.15f) }));

    triggers.push_back(new TriggerNode("rc kill order", { NextAction("rc mark kill order", ACTION_RAID + 2) }));

    // The Zul'Gurub Bat Rider's priority: nothing but the leash comes first.
    triggers.push_back(new TriggerNode("rc aq explode", { NextAction("rc aq explode move away", ACTION_EMERGENCY + 7) }));
    // Out of the trap before it goes off, like the Viscidus cloud.
    triggers.push_back(new TriggerNode("rc aq20 sand trap", { NextAction("rc aq20 leave sand trap", ACTION_EMERGENCY + 6) }));

    // A raid member dies when the larva arrives: over the damage dealers' own target picking and
    // the skull (ACTION_RAID + 2).
    triggers.push_back(new TriggerNode("rc aq20 larva", { NextAction("rc aq20 kill larva", ACTION_RAID + 3) }));
    // Over the damage dealers' own rotation.
    triggers.push_back(new TriggerNode("rc aq20 moam drain", { NextAction("rc aq20 moam drain", ACTION_RAID + 1) }));

    // Above playerbots' own crystal walk (ACTION_RAID), which the multiplier switches off, and
    // above the tanks' rules (ACTION_RAID - 4 and up).
    triggers.push_back(new TriggerNode("rc aq20 lead to crystal",
                                       { NextAction("rc aq20 lead to crystal", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc aq20 crystal", { NextAction("rc aq20 use crystal", ACTION_RAID + 3) }));
}

void RaidClearAq20Strategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcAqExplodeHoldMultiplier(botAI));
    multipliers.push_back(new RcAq20CrystalMultiplier(botAI));
    multipliers.push_back(new RcAq20SandTrapHoldMultiplier(botAI));
    multipliers.push_back(new RcAq20LarvaMultiplier(botAI));
    multipliers.push_back(new RcSpellReflectMultiplier(botAI));
}

void RaidClearAq40Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Under a reflect: the spell of the other school that playerbots ranks below the wand (a
    // frost mage's Fireball), just above it. "rc spell reflect" zeroes whichever is reflected.
    triggers.push_back(new TriggerNode("rc mage target reflects", { NextAction("fireball", 5.15f) }));
    triggers.push_back(new TriggerNode("rc warlock target reflects", { NextAction("immolate", 5.15f) }));

    triggers.push_back(new TriggerNode("rc kill order", { NextAction("rc mark kill order", ACTION_RAID + 2) }));

    triggers.push_back(new TriggerNode("rc aq explode", { NextAction("rc aq explode move away", ACTION_EMERGENCY + 7) }));

    // Out of the cloud (or the whirlwind) before anything else the fight asks for.
    triggers.push_back(new TriggerNode("rc aq40 toxin", { NextAction("rc aq40 leave toxin", ACTION_EMERGENCY + 6) }));
    triggers.push_back(new TriggerNode("rc aq40 whirlwind",
                                       { NextAction("rc aq40 whirlwind keep away", ACTION_EMERGENCY + 6) }));
    triggers.push_back(new TriggerNode("rc aq40 poison cloud",
                                       { NextAction("rc aq40 leave poison cloud", ACTION_EMERGENCY + 6) }));
    // Out of Vek'lor's Burst, Vek'nilash's Uppercut and the Blizzard: under the clouds and blasts,
    // over everything else.
    triggers.push_back(new TriggerNode("rc aq40 twins position",
                                       { NextAction("rc aq40 twins position", ACTION_EMERGENCY + 5) }));
    triggers.push_back(new TriggerNode("rc aq40 poison bolt",
                                       { NextAction("rc aq40 poison bolt keep away", ACTION_EMERGENCY + 5) }));
    triggers.push_back(new TriggerNode("rc aq40 plague", { NextAction("rc aq40 plague move away", ACTION_EMERGENCY + 5) }));
    triggers.push_back(new TriggerNode("rc aq40 defender aoe",
                                       { NextAction("rc aq40 defender aoe move", ACTION_EMERGENCY + 4) }));
    // A role's fixed spot (Skeram, the Bug Trio): under the dodges, over the rest, as in Blackwing
    // Lair. Skeram landing on an off-tank's platform: picked up before he Earth Shocks anyone.
    triggers.push_back(new TriggerNode("rc aq40 skeram pickup", { NextAction("rc aq40 skeram pickup", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc aq40 boss spot", { NextAction("rc aq40 boss spot", ACTION_RAID) }));

    // A heal that undoes the kill, and the stun that stops a whirlwind: over the rotation and the
    // skull, like Lor'Khan's heal in Zul'Gurub.
    triggers.push_back(new TriggerNode("rc aq40 yauj heal", { NextAction("rc aq40 yauj interrupt", ACTION_RAID + 4) }));
    triggers.push_back(new TriggerNode("rc aq40 skeram cast", { NextAction("rc aq40 skeram interrupt", ACTION_RAID + 4) }));
    // Skeram's mind control, as in Zul'Gurub: over the skull (ACTION_RAID + 2).
    triggers.push_back(new TriggerNode("rc charmed member", { NextAction("rc control charmed", ACTION_RAID + 4) }));
    triggers.push_back(new TriggerNode("rc charmed target", { NextAction("rc leave charmed target", ACTION_RAID + 3) }));
    // Like Ebonroc's swap.
    triggers.push_back(new TriggerNode("rc aq40 mortal wound", { NextAction("rc aq40 mortal wound taunt", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc aq40 sartura stun", { NextAction("rc aq40 sartura stun", ACTION_RAID + 3) }));
    // Over the skull and playerbots' own target picking, like the Ayamiss larva.
    triggers.push_back(new TriggerNode("rc aq40 twin target", { NextAction("rc aq40 attack twin", ACTION_RAID + 3) }));
    // Over the tank's rotation: every Heal Brother undoes a few seconds of the raid's damage.
    triggers.push_back(new TriggerNode("rc aq40 twins apart", { NextAction("rc aq40 twins apart", ACTION_RAID + 1) }));
    triggers.push_back(new TriggerNode("rc aq40 twins pet", { NextAction("rc aq40 twins pet", ACTION_RAID + 2) }));
    // Over the tank's rotation, which has nothing else that reaches him from where it stands.
    triggers.push_back(new TriggerNode("rc aq40 veklor tank threat",
                                       { NextAction("rc aq40 veklor tank threat", ACTION_RAID + 1) }));
    triggers.push_back(new TriggerNode("rc aq40 yauj tremor", { NextAction("rc mc tremor totem", ACTION_RAID + 1) }));
    triggers.push_back(new TriggerNode("rc aq40 nature resistance",
                                       { NextAction("rc aq40 nature resistance", ACTION_RAID + 1) }));
    // Before the totem's next pulse takes a sting off.
    triggers.push_back(new TriggerNode("rc aq40 cleansing totem",
                                       { NextAction("rc aq40 destroy cleansing totem", ACTION_EMERGENCY + 1) }));

    // Over the damage dealers' own rotation, under every walk out of something.
    triggers.push_back(new TriggerNode("rc aq40 viscidus frost",
                                       { NextAction("rc aq40 viscidus frost", ACTION_RAID + 1) }));
    // A glob that arrives heals him: over the skull, like the Ayamiss larva, and an area spell
    // that catches several over the single glob.
    triggers.push_back(new TriggerNode("rc aq40 glob", { NextAction("rc aq40 kill glob", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc aq40 glob aoe", { NextAction("rc aq40 glob aoe", ACTION_RAID + 4) }));
}

void RaidClearAq40Strategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcAqExplodeHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40ToxinHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40GlobMultiplier(botAI));
    multipliers.push_back(new RcAq40NatureResistanceMultiplier(botAI));
    multipliers.push_back(new RcAq40WyvernStingMultiplier(botAI));
    multipliers.push_back(new RcAq40PoisonBoltHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40DefenderHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40WhirlwindHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40PoisonCloudHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40YaujTremorMultiplier(botAI));
    multipliers.push_back(new RcAq40MortalWoundMultiplier(botAI));
    multipliers.push_back(new RcAq40SkeramInterruptMultiplier(botAI));
    multipliers.push_back(new RcAq40TwinTargetMultiplier(botAI));
    multipliers.push_back(new RcAq40TwinsHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40VeklorThreatMultiplier(botAI));
    multipliers.push_back(new RcAq40SpotHoldMultiplier(botAI));
    multipliers.push_back(new RcCharmedSplashMultiplier(botAI));
    multipliers.push_back(new RcCharmedSelfMultiplier(botAI));
    multipliers.push_back(new RcSpellReflectMultiplier(botAI));
}

// --- Tables ---------------------------------------------------------------

std::vector<RaidClear::KillOrderEntry> const& RaidClear::RuinsOfAhnQiraj::KillOrder()
{
    // Moam stays stone, gaining mana toward his Arcane Eruption, until his fiends are dead.
    static std::vector<KillOrderEntry> const table = {
        { NPC_MANA_FIEND, 0 },
    };
    return table;
}

std::vector<RaidClear::KillOrderEntry> const& RaidClear::TempleOfAhnQiraj::KillOrder()
{
    static std::vector<KillOrderEntry> const table = {
        // Skeram's images, then Skeram (KillOrderSkips holds him back while one is alive).
        { NPC_SKERAM, 0 },
        // Each glob that reaches the middle of the room heals Viscidus.
        { NPC_GLOB_OF_VISCIDUS, 0 },
        // Berserk 30 s after it appears.
        { NPC_SPAWN_OF_FANKRISS, 0 },
        { NPC_VEKNISS_HATCHLING, 1 },
        // The adds before their boss.
        { NPC_SARTURA_ROYAL_GUARD, 1 },
        // The Bug Trio, the raid guide's easiest order: Yauj's brood, then Kri (his Toxic Volley
        // keeps the healers curing), then Yauj, then Vem, whose death enrages whoever is left.
        { NPC_YAUJ_BROOD, 1 },
        { NPC_KRI, 2 },
        { NPC_YAUJ, 3 },
        { NPC_VEM, 4 },
        // One at a time: each enrages at 20%.
        { NPC_VEKNISS_GUARDIAN, 5 },
        // By their abilities (KillOrderRank): SENTINEL_RANK_BASE and up.
        { NPC_ANUBISATH_SENTINEL, SENTINEL_RANK_BASE },
    };
    return table;
}

bool RaidClear::TempleOfAhnQiraj::KillOrderSkips(Player* /*bot*/, Unit const* unit)
{
    if (SkeramWaitsForImages(unit))
        return true;

    // The script turns a bug passive while it walks over to eat a dying one, and the dying one
    // for as long as it waits to be eaten. The dying one takes no damage; the eater gets a Full
    // Heal when it arrives.
    if (!unit)
        return false;
    uint32 const entry = unit->GetEntry();
    if (entry != NPC_YAUJ && entry != NPC_KRI && entry != NPC_VEM)
        return false;
    Creature const* bug = unit->ToCreature();
    return bug && bug->HasReactState(REACT_PASSIVE);
}

std::vector<uint32> const& RaidClear::TempleOfAhnQiraj::TankSplitCoTankBosses()
{
    // Mortal Wound: two tanks take him in turns, so the second one is already on his threat.
    static std::vector<uint32> const bosses = { NPC_FANKRISS };
    return bosses;
}

std::vector<uint32> const& RaidClear::TempleOfAhnQiraj::TankSplitOwnTankAdds()
{
    // Each one hits like a boss once it is Berserk, and can't be taunted then.
    static std::vector<uint32> const adds = { NPC_SPAWN_OF_FANKRISS };
    return adds;
}

std::vector<RaidClear::Tanks::SplashRadius> const& RaidClear::TempleOfAhnQiraj::TankSplitSplashRadii()
{
    static std::vector<Tanks::SplashRadius> const radii = {
        { NPC_VEKNISS_GUARDIAN, AhnQiraj::SpellRadius(SPELL_GUARDIAN_IMPALE, 10.0f) },
    };
    return radii;
}

// --- Anubisath Explode ----------------------------------------------------

float RaidClear::AhnQiraj::ExplodeRadius()
{
    static float const radius = []
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_EXPLODE_BLAST);
        float const r = info ? info->Effects[EFFECT_0].CalcRadius() : 0.0f;
        return r > 0.0f ? r : 20.0f;
    }();
    return radius;
}

float RaidClear::AhnQiraj::ExplodeBugRadius()
{
    static float const radius = SpellRadius(SPELL_EXPLODE_BUG_BLAST, 15.0f);
    return radius;
}

std::vector<RaidClear::TempleOfAhnQiraj::ToxinCloud> RaidClear::TempleOfAhnQiraj::FindToxinClouds(
    WorldObject const* centre, uint32 dropper, float range)
{
    std::vector<ToxinCloud> clouds;

    std::list<WorldObject*> objects;
    Acore::AllWorldObjectsInRange check(centre, range);
    Acore::WorldObjectListSearcher<Acore::AllWorldObjectsInRange> searcher(centre, objects, check,
                                                                           GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
    Cell::VisitObjects(centre, searcher, range);
    for (WorldObject* object : objects)
    {
        DynamicObject const* dynObj = object->ToDynObject();
        if (dynObj && dynObj->GetSpellId() == SPELL_TOXIN_CLOUD)
            clouds.push_back({ dynObj->GetPositionX(), dynObj->GetPositionY(), dynObj->GetRadius() });
    }

    static float const radius = AhnQiraj::SpellRadius(SPELL_TOXIN_CLOUD, 5.0f);
    std::list<Creature*> droppers;
    centre->GetCreatureListWithEntryInGrid(droppers, dropper, range);
    for (Creature const* mob : droppers)
        if (mob->IsAlive())
            clouds.push_back({ mob->GetPositionX(), mob->GetPositionY(), radius });
    return clouds;
}

float RaidClear::TempleOfAhnQiraj::ToxinGap(std::vector<ToxinCloud> const& clouds, float x, float y)
{
    float gap = 1000.0f;
    for (ToxinCloud const& cloud : clouds)
        gap = std::min(gap, std::hypot(cloud.x - x, cloud.y - y) - cloud.radius);
    return gap;
}

float RaidClear::TempleOfAhnQiraj::ToxinGap(std::vector<ToxinCloud> const& clouds, Position const* pos)
{
    return ToxinGap(clouds, pos->GetPositionX(), pos->GetPositionY());
}

float RaidClear::AhnQiraj::SpellRadius(uint32 spellId, float fallback)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    float const r = info ? info->Effects[EFFECT_0].CalcRadius() : 0.0f;
    return r > 0.0f ? r : fallback;
}

Unit* RaidClear::AhnQiraj::WalkTarget(Action* action, Player* bot)
{
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action);
    if (!walks && !dynamic_cast<CastReachTargetSpellAction*>(action))
        return nullptr;
    if (!bot->IsInCombat())
        return nullptr;
    // Only walks toward an enemy: a healer still walks to whoever it has to heal.
    Unit* target = action->GetTarget();
    return target && target != bot && bot->IsValidAttackTarget(target) ? target : nullptr;
}

namespace
{
    // A mob fighting the raid that is about to blow up, and how far its blast reaches.
    struct LitMob
    {
        Unit* mob;
        float radius;
    };

    // Every mob fighting the raid that has its fuse lit: an Anubisath's Explode, or a bug Vek'lor
    // has set to explode.
    std::vector<LitMob> LitMobs(PlayerbotAI* botAI)
    {
        std::vector<LitMob> lit;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive())
                continue;
            if (unit->HasAura(SPELL_EXPLODE_FUSE))
                lit.push_back({ unit, ExplodeRadius() });
            else if (unit->HasAura(SPELL_EXPLODE_BUG))
                lit.push_back({ unit, ExplodeBugRadius() });
        }
        return lit;
    }

    // Yards from a spot to the edge of the nearest blast (less than 0 inside one), centre to
    // centre as the radius is.
    float ExplodeGap(std::vector<LitMob> const& lit, float x, float y, float z)
    {
        float gap = 1000.0f;
        for (LitMob const& one : lit)
            gap = std::min(gap, one.mob->GetExactDist(x, y, z) - one.radius);
        return gap;
    }

    float ExplodeGap(std::vector<LitMob> const& lit, Position const* pos)
    {
        return ExplodeGap(lit, pos->GetPositionX(), pos->GetPositionY(), pos->GetPositionZ());
    }

    // Vek'lor's keep-out as a gap from his edge (less than 0 inside it), or none.
    struct VeklorRule
    {
        bool any = false;
        float x = 0.0f;
        float y = 0.0f;
        float reach = 0.0f;
        float keep = 0.0f;
    };

    float VeklorGap(VeklorRule const& rule, float botReach, float x, float y)
    {
        if (!rule.any)
            return 1000.0f;
        return std::hypot(x - rule.x, y - rule.y) - rule.reach - botReach - rule.keep;
    }

    bool IsAhnQiraj(uint32 mapId) { return mapId == MAP_AQ20 || mapId == MAP_AQ40; }
}

bool RcAqExplodeTrigger::IsActive()
{
    // A bot that is feared or rooted isn't going anywhere.
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    return ExplodeGap(LitMobs(botAI), bot) < EXPLODE_KEEP_AWAY_EXTRA;
}

bool RcAqExplodeMoveAwayAction::Execute(Event /*event*/)
{
    std::vector<LitMob> const lit = LitMobs(botAI);
    if (ExplodeGap(lit, bot) >= EXPLODE_KEEP_AWAY_EXTRA)
        return false;
    float widest = 0.0f;
    for (LitMob const& one : lit)
        widest = std::max(widest, one.radius);
    // At the Twin Emperors, not out of a bug's blast into Vek'lor's Arcane Burst: a spot counts as
    // good enough only outside his keep-out as well.
    VeklorRule veklor;
    using namespace RaidClear::TempleOfAhnQiraj;
    veklor.any = VeklorKeepOut(bot, veklor.x, veklor.y, veklor.reach, veklor.keep);
    float const botReach = bot->GetCombatReach();
    auto const gapAt = [&lit, &veklor, botReach](float x, float y, float z)
    { return std::min(ExplodeGap(lit, x, y, z), VeklorGap(veklor, botReach, x, y) + EXPLODE_SPOT_EXTRA); };
    // Up to the whole blast across from the middle of it.
    return _walk.Go(gapAt, EXPLODE_KEEP_AWAY_EXTRA, EXPLODE_SPOT_EXTRA, EXPLODE_WORTH_IT,
                    widest + EXPLODE_SPOT_EXTRA + 3.0f, lit.size() == 1);
}

std::vector<RaidClear::AhnQiraj::LitBlast> RaidClear::AhnQiraj::LitBlasts(PlayerbotAI* botAI)
{
    std::vector<LitBlast> blasts;
    for (LitMob const& one : LitMobs(botAI))
        blasts.push_back({ one.mob->GetPositionX(), one.mob->GetPositionY(), one.mob->GetPositionZ(), one.radius });
    return blasts;
}

float RcAqExplodeHoldMultiplier::GetValue(Action* action)
{
    Unit* target = WalkTarget(action, bot);
    if (!target)
        return 1.0f;

    std::vector<LitMob> const lit = LitMobs(botAI);
    if (ExplodeGap(lit, bot) >= EXPLODE_HOLD_EXTRA)
        return 1.0f;
    return ExplodeGap(lit, target) < EXPLODE_KEEP_AWAY_EXTRA ? 0.0f : 1.0f;
}

// A bot in the middle of a cast doesn't run its engine until the cast ends: wake the ones in reach
// the moment something they have to get away from starts. A trap appears under the player its
// spell is cast on; a fuse, a whirlwind and Kri's cloud round the mob that casts them.
class RaidClearAqWakeSpellScript : public AllSpellScript
{
public:
    RaidClearAqWakeSpellScript() : AllSpellScript("RaidClearAqWakeSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || !IsAhnQiraj(caster->GetMapId()))
            return;

        using namespace RaidClear::RuinsOfAhnQiraj;
        using namespace RaidClear::TempleOfAhnQiraj;
        float reach = 0.0f;
        switch (spellInfo->Id)
        {
            case SPELL_EXPLODE_FUSE:
                reach = ExplodeRadius() + EXPLODE_KEEP_AWAY_EXTRA;
                break;
            case SPELL_SAND_TRAP_SUMMON:
                reach = SpellRadius(SPELL_SAND_TRAP, 8.0f) + SAND_TRAP_KEEP_EXTRA;
                break;
            case SPELL_SARTURA_WHIRLWIND:
                reach = SpellRadius(SPELL_SARTURA_WHIRLWIND_HIT, 10.0f) + WHIRLWIND_KEEP_EXTRA;
                break;
            case SPELL_GUARD_WHIRLWIND:
                reach = SpellRadius(SPELL_GUARD_WHIRLWIND_HIT, 10.0f) + WHIRLWIND_KEEP_EXTRA;
                break;
            case SPELL_POISON_CLOUD_SUMMON:
                reach = SpellRadius(SPELL_TOXIN_CLOUD, 5.0f) + TOXIN_SPOT;
                break;
            default:
                return;
        }
        if (!RaidClear::GetConfig().IsRaidEnabled(caster->GetMapId()))
            return;

        char const* strategy = caster->GetMapId() == MAP_AQ20 ? "rc aq20" : "rc aq40";
        Map::PlayerList const& players = caster->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive() || player->GetExactDist(caster) >= reach)
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->HasStrategy(strategy, BOT_STATE_COMBAT))
                continue;

            botAI->RequestSpellInterrupt();
            botAI->ResetActionDuration();
        }
    }
};

void RaidClear::AhnQiraj::AddScripts()
{
    new RaidClearAqWakeSpellScript();
    TempleOfAhnQiraj::AddSkeramScripts();
    TempleOfAhnQiraj::AddTwinEmperorsScripts();
    TempleOfAhnQiraj::AddDefenderScripts();
}
