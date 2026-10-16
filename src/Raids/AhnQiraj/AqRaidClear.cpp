/*
 * mod-raid-clear: Ahn'Qiraj, both raids. See AqRaidClear.h. The strategies, the kill-order and
 * tank tables, and what the two raids share (Anubisath Explode); each boss is in a file of its
 * own (Aq20Ossirian.cpp, Aq20Kurinnaxx.cpp, Aq20Moam.cpp, Aq20Ayamiss.cpp, Aq40Viscidus.cpp,
 * Aq40Sartura.cpp, Aq40BugTrio.cpp). Fankriss is only the tables.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "RaidClearConfig.h"

#include "AllSpellScript.h"
#include "Creature.h"
#include "Map.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>

using namespace RaidClear::AhnQiraj;

void RaidClearAq20Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
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
}

void RaidClearAq40Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("rc kill order", { NextAction("rc mark kill order", ACTION_RAID + 2) }));

    triggers.push_back(new TriggerNode("rc aq explode", { NextAction("rc aq explode move away", ACTION_EMERGENCY + 7) }));

    // Out of the cloud (or the whirlwind) before anything else the fight asks for.
    triggers.push_back(new TriggerNode("rc aq40 toxin", { NextAction("rc aq40 leave toxin", ACTION_EMERGENCY + 6) }));
    triggers.push_back(new TriggerNode("rc aq40 whirlwind",
                                       { NextAction("rc aq40 whirlwind keep away", ACTION_EMERGENCY + 6) }));
    triggers.push_back(new TriggerNode("rc aq40 poison cloud",
                                       { NextAction("rc aq40 leave poison cloud", ACTION_EMERGENCY + 6) }));

    // A heal that undoes the kill, and the stun that stops a whirlwind: over the rotation and the
    // skull, like Lor'Khan's heal in Zul'Gurub.
    triggers.push_back(new TriggerNode("rc aq40 yauj heal", { NextAction("rc aq40 yauj interrupt", ACTION_RAID + 4) }));
    triggers.push_back(new TriggerNode("rc aq40 sartura stun", { NextAction("rc aq40 sartura stun", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc aq40 yauj tremor", { NextAction("rc mc tremor totem", ACTION_RAID + 1) }));

    // Over the damage dealers' own rotation, under every walk out of something.
    triggers.push_back(new TriggerNode("rc aq40 viscidus frost",
                                       { NextAction("rc aq40 viscidus frost", ACTION_RAID + 1) }));
}

void RaidClearAq40Strategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcAqExplodeHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40ToxinHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40WhirlwindHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40PoisonCloudHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40YaujTremorMultiplier(botAI));
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
        // Each glob that reaches the middle of the room heals Viscidus.
        { NPC_GLOB_OF_VISCIDUS, 0 },
        // Berserk 30 s after it appears.
        { NPC_SPAWN_OF_FANKRISS, 0 },
        { NPC_VEKNISS_HATCHLING, 1 },
        // The adds before their boss.
        { NPC_SARTURA_ROYAL_GUARD, 1 },
        // The Bug Trio: Yauj's brood, then Yauj (heals, fears), then Kri, then Vem, whose death
        // enrages whoever is left.
        { NPC_YAUJ_BROOD, 1 },
        { NPC_YAUJ, 2 },
        { NPC_KRI, 3 },
        { NPC_VEM, 4 },
        // One at a time: each enrages at 20%.
        { NPC_VEKNISS_GUARDIAN, 5 },
    };
    return table;
}

bool RaidClear::TempleOfAhnQiraj::KillOrderSkips(Player* /*bot*/, Unit const* unit)
{
    // The script turns a bug passive while it walks over to eat a dying one, and the dying one
    // for as long as it waits to be eaten; neither takes damage meanwhile.
    if (!unit)
        return false;
    uint32 const entry = unit->GetEntry();
    if (entry != NPC_YAUJ && entry != NPC_KRI && entry != NPC_VEM)
        return false;
    Creature const* bug = unit->ToCreature();
    return bug && bug->HasReactState(REACT_PASSIVE);
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
    Unit* target = action->GetTarget();
    return target && target != bot ? target : nullptr;
}

namespace
{
    // Every mob fighting the raid that has its fuse lit.
    std::vector<Unit*> LitMobs(PlayerbotAI* botAI)
    {
        std::vector<Unit*> lit;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (unit && unit->IsAlive() && unit->HasAura(SPELL_EXPLODE_FUSE))
                lit.push_back(unit);
        }
        return lit;
    }

    // Yards from a spot to the nearest of them, centre to centre.
    float ExplodeGap(std::vector<Unit*> const& lit, float x, float y, float z)
    {
        float gap = 1000.0f;
        for (Unit const* mob : lit)
            gap = std::min(gap, mob->GetExactDist(x, y, z));
        return gap;
    }

    float ExplodeGap(std::vector<Unit*> const& lit, Position const* pos)
    {
        return ExplodeGap(lit, pos->GetPositionX(), pos->GetPositionY(), pos->GetPositionZ());
    }

    bool IsAhnQiraj(uint32 mapId) { return mapId == MAP_AQ20 || mapId == MAP_AQ40; }
}

bool RcAqExplodeTrigger::IsActive()
{
    // A bot that is feared or rooted isn't going anywhere.
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    return ExplodeGap(LitMobs(botAI), bot) < ExplodeRadius() + EXPLODE_KEEP_AWAY_EXTRA;
}

bool RcAqExplodeMoveAwayAction::Execute(Event /*event*/)
{
    std::vector<Unit*> const lit = LitMobs(botAI);
    float const radius = ExplodeRadius();
    float const keepAway = radius + EXPLODE_KEEP_AWAY_EXTRA;
    if (ExplodeGap(lit, bot) >= keepAway)
        return false;
    auto const gapAt = [&lit](float x, float y, float z) { return ExplodeGap(lit, x, y, z); };
    // Up to the whole blast across from the middle of it.
    return _walk.Go(gapAt, keepAway, radius + EXPLODE_SPOT_EXTRA, EXPLODE_WORTH_IT,
                    radius + EXPLODE_SPOT_EXTRA + 3.0f, lit.size() == 1);
}

float RcAqExplodeHoldMultiplier::GetValue(Action* action)
{
    Unit* target = WalkTarget(action, bot);
    if (!target)
        return 1.0f;

    std::vector<Unit*> const lit = LitMobs(botAI);
    float const radius = ExplodeRadius();
    if (ExplodeGap(lit, bot) >= radius + EXPLODE_HOLD_EXTRA)
        return 1.0f;
    return ExplodeGap(lit, target) < radius + EXPLODE_KEEP_AWAY_EXTRA ? 0.0f : 1.0f;
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
                reach = SpellRadius(SPELL_TOXIC_VAPORS, 8.0f) + POISON_CLOUD_KEEP_EXTRA;
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
}
