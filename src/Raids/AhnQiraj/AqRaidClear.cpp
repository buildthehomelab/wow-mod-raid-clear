/*
 * mod-raid-clear: Ahn'Qiraj, both raids. See AqRaidClear.h. The strategies and what the two
 * raids share (Anubisath Explode); Ossirian is in Aq20Ossirian.cpp, Viscidus in Aq40Viscidus.cpp.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "RaidClearConfig.h"

#include "AllSpellScript.h"
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
    // The Zul'Gurub Bat Rider's priority: nothing but the leash comes first.
    triggers.push_back(new TriggerNode("rc aq explode", { NextAction("rc aq explode move away", ACTION_EMERGENCY + 7) }));

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
}

void RaidClearAq40Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("rc aq explode", { NextAction("rc aq explode move away", ACTION_EMERGENCY + 7) }));

    // Out of the cloud before anything else the fight asks for.
    triggers.push_back(new TriggerNode("rc aq40 toxin", { NextAction("rc aq40 leave toxin", ACTION_EMERGENCY + 6) }));

    // Over the damage dealers' own rotation, under every walk out of something.
    triggers.push_back(new TriggerNode("rc aq40 viscidus frost",
                                       { NextAction("rc aq40 viscidus frost", ACTION_RAID + 1) }));
}

void RaidClearAq40Strategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcAqExplodeHoldMultiplier(botAI));
    multipliers.push_back(new RcAq40ToxinHoldMultiplier(botAI));
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
    // Picking a target isn't movement; only getting there is held: on foot, or with Charge,
    // Intercept or Feral Charge, which would undo the walk out in one cast.
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action);
    if (!walks && !dynamic_cast<CastReachTargetSpellAction*>(action))
        return 1.0f;
    if (!bot->IsInCombat())
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    std::vector<Unit*> const lit = LitMobs(botAI);
    float const radius = ExplodeRadius();
    if (ExplodeGap(lit, bot) >= radius + EXPLODE_HOLD_EXTRA)
        return 1.0f;
    return ExplodeGap(lit, target) < radius + EXPLODE_KEEP_AWAY_EXTRA ? 0.0f : 1.0f;
}

// A bot in the middle of a cast doesn't run its engine until the cast ends: wake the ones in reach
// the moment a fuse is lit.
class RaidClearAqExplodeSpellScript : public AllSpellScript
{
public:
    RaidClearAqExplodeSpellScript() : AllSpellScript("RaidClearAqExplodeSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || spellInfo->Id != SPELL_EXPLODE_FUSE || !IsAhnQiraj(caster->GetMapId()))
            return;
        if (!RaidClear::GetConfig().IsRaidEnabled(caster->GetMapId()))
            return;

        char const* strategy = caster->GetMapId() == MAP_AQ20 ? "rc aq20" : "rc aq40";
        float const keepAway = ExplodeRadius() + EXPLODE_KEEP_AWAY_EXTRA;
        Map::PlayerList const& players = caster->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive() || player->GetExactDist(caster) >= keepAway)
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
    new RaidClearAqExplodeSpellScript();
}
