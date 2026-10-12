/*
 * mod-raid-clear: Onyxia's Lair (map 249). See OnyRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "OnyRaidClear.h"

#include "RaidClearConfig.h"
#include "Common/BossLookup.h"

#include "AllSpellScript.h"
#include "AttackAction.h"
#include "Creature.h"
#include "GameTime.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "PlayerbotAI.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

using namespace RaidClear::OnyxiasLair;

void RaidClearOnyxiasLairStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Above playerbots' own "ony move to safe zone" (ACTION_EMERGENCY + 5).
    triggers.push_back(new TriggerNode("rc ony deep breath",
                                       { NextAction("rc ony leave deep breath", ACTION_EMERGENCY + 6) }));
}

void RaidClearOnyxiasLairStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcOnyDeepBreathMultiplier(botAI));
}

// --- Deep Breath ----------------------------------------------------------

namespace
{
    // One spell of a chain: everyone within `radius` of this spot on the floor burns.
    struct Blast
    {
        float x;
        float y;
        float radius;
    };
    using Trail = std::vector<Blast>;

    struct BreathData
    {
        std::unordered_map<uint32, Trail> trails;    // first spell of a chain -> where the chain burns
        std::unordered_map<uint32, uint32> firstOf;  // any spell of a chain -> its first spell
        Trail rookery;                               // the Heated Ground patches
    };

    // The longest chain has 20 spells.
    constexpr uint8 MAX_CHAIN = 40;
    constexpr float DEFAULT_BLAST_RADIUS = 18.0f;

    // A chain, walked from the server's spell data: each spell lands around its row in
    // spell_target_position and its second effect casts the next one.
    Trail WalkChain(uint32 first, std::unordered_map<uint32, uint32>* firstOf = nullptr)
    {
        Trail trail;
        uint32 id = first;
        for (uint8 link = 0; id && link < MAX_CHAIN; ++link)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
            if (!info)
                break;
            if (firstOf)
                firstOf->emplace(id, first);

            SpellTargetPosition const* spot = sSpellMgr->GetSpellTargetPosition(id, EFFECT_0);
            if (spot && spot->target_mapId == MAP_ID)
            {
                float const radius = info->Effects[EFFECT_0].CalcRadius();
                trail.push_back({ spot->target_X, spot->target_Y, radius > 0.0f ? radius : DEFAULT_BLAST_RADIUS });
            }
            id = info->Effects[EFFECT_1].TriggerSpell;
        }
        return trail;
    }

    BreathData BuildBreathData()
    {
        BreathData data;
        for (uint32 const first : DEEP_BREATHS)
            data.trails[first] = WalkChain(first, &data.firstOf);
        data.rookery = WalkChain(SPELL_HEATED_GROUND);
        return data;
    }

    BreathData const& Breaths()
    {
        static BreathData const data = BuildBreathData();
        return data;
    }

    // Onyxia, alive and fighting. Not through playerbots' "find target": that only knows what the
    // bot itself has threat on, and she drops her threat list when she takes off.
    Creature* FightingOnyxia(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID)
            return nullptr;
        return RaidClear::FightingCreature(bot, { NPC_ONYXIA, NPC_ONYXIA_40 });
    }

    // The first spell of the Deep Breath she is casting, or whose fire is still crossing the
    // room (one of the chain's auras is on her), or 0.
    uint32 ActiveBreath(Unit* boss)
    {
        BreathData const& data = Breaths();
        if (Spell* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            if (SpellInfo const* info = spell->GetSpellInfo())
                if (data.trails.count(info->Id))
                    return info->Id;

        for (auto const& [id, aurApp] : boss->GetAppliedAuras())
        {
            auto it = data.firstOf.find(id);
            if (it != data.firstOf.end())
                return it->second;
        }
        return 0;
    }

    // Where a breath burns, or nullptr when the server has no positions for it.
    Trail const* TrailOf(uint32 breath)
    {
        BreathData const& data = Breaths();
        auto it = data.trails.find(breath);
        return it != data.trails.end() && !it->second.empty() ? &it->second : nullptr;
    }

    // The strip of the breath going on right now, or nullptr.
    Trail const* ActiveTrail(Player* bot, uint32* breath = nullptr)
    {
        Creature* boss = FightingOnyxia(bot);
        uint32 const id = boss ? ActiveBreath(boss) : 0;
        if (breath)
            *breath = id;
        return id ? TrailOf(id) : nullptr;
    }

    // Yards from a spot to the edge of the nearest blast; negative inside the fire. Flat
    // distance: the blasts sit on the floor and so does everyone else.
    float Clearance(Trail const& trail, float x, float y)
    {
        float clearance = 1000.0f;
        for (Blast const& blast : trail)
            clearance = std::min(clearance, std::hypot(x - blast.x, y - blast.y) - blast.radius);
        return clearance;
    }

    // Whether the straight walk from one spot to another comes within `margin` of the fire.
    bool CrossesFire(Trail const& trail, float fromX, float fromY, float toX, float toY, float margin)
    {
        float const length = std::hypot(toX - fromX, toY - fromY);
        int const steps = std::max(1, int(std::ceil(length / 3.0f)));
        for (int i = 0; i <= steps; ++i)
        {
            float const t = float(i) / float(steps);
            if (Clearance(trail, fromX + (toX - fromX) * t, fromY + (toY - fromY) * t) < margin)
                return true;
        }
        return false;
    }

    // The whelp caves and the egg trench between them: no place to wait out a breath. The core's
    // Onyxia sets the trench on fire with every Deep Breath (Heated Ground, a chain like the
    // breath's), so its patches mark the trench on any server.
    bool NearEggs(float x, float y)
    {
        static Position const caves[] = { { -35.0f, -165.0f, 0.0f }, { -35.0f, -260.0f, 0.0f } };
        for (Position const& cave : caves)
            if (cave.GetExactDist2d(x, y) < EGGS_KEEP_OUT)
                return true;

        Trail const& rookery = Breaths().rookery;
        return !rookery.empty() && Clearance(rookery, x, y) < ROOKERY_KEEP_OUT;
    }

    // The nearest spot outside the fire: rings around the bot, closest first. On the first ring
    // with anything usable, the spot deepest outside wins, which is the one straight out of the
    // strip. Level floor the bot can see, not at the whelp caves or in the egg trench.
    bool FindSafeSpot(Player* bot, Trail const& trail, Position& out)
    {
        Map* map = bot->GetMap();
        float const botX = bot->GetPositionX();
        float const botY = bot->GetPositionY();
        float const botZ = bot->GetPositionZ();

        constexpr float RING_STEP = 2.0f;
        constexpr float RING_MAX = 46.0f;  // from the middle of a strip the edge is 25 yd away
        constexpr int STEPS = 24;
        for (float ring = RING_STEP; ring <= RING_MAX; ring += RING_STEP)
        {
            bool found = false;
            float bestClearance = 0.0f;
            for (int i = 0; i < STEPS; ++i)
            {
                float const angle = float(i) * 2.0f * float(M_PI) / float(STEPS);
                float const x = botX + std::cos(angle) * ring;
                float const y = botY + std::sin(angle) * ring;
                float const clearance = Clearance(trail, x, y);
                if (clearance < BREATH_KEEP_OUT_TARGET || (found && clearance <= bestClearance) || NearEggs(x, y))
                    continue;

                float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + BREATH_SPOT_HEIGHT);
                if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > BREATH_SPOT_HEIGHT)
                    continue;
                if (!bot->IsWithinLOS(x, y, z + 2.0f))
                    continue;

                out.Relocate(x, y, z);
                bestClearance = clearance;
                found = true;
            }
            if (found)
                return true;
        }
        return false;
    }
}

bool RcOnyDeepBreathTrigger::IsActive()
{
    if (!bot->IsAlive())
        return false;
    Trail const* trail = ActiveTrail(bot);
    return trail && Clearance(*trail, bot->GetPositionX(), bot->GetPositionY()) < BREATH_KEEP_OUT;
}

bool RcOnyLeaveDeepBreathAction::Execute(Event /*event*/)
{
    uint32 breath = 0;
    Trail const* trail = ActiveTrail(bot, &breath);
    if (!trail || Clearance(*trail, bot->GetPositionX(), bot->GetPositionY()) >= BREATH_KEEP_OUT)
        return false;

    // One spot per breath: a new pick every tick would have the bot zigzag along the strip.
    constexpr uint64 SPOT_REUSE_MS = 20000;
    uint64 const now = GameTime::GetGameTimeMS().count();
    bool const cached = _spotBreath == breath && now - _spotFoundMs < SPOT_REUSE_MS &&
                        Clearance(*trail, _spot.GetPositionX(), _spot.GetPositionY()) >= BREATH_KEEP_OUT;
    if (!cached)
    {
        if (!FindSafeSpot(bot, *trail, _spot))
            return false;  // nowhere to go from here: fight on
        _spotBreath = breath;
        _spotFoundMs = now;
    }

    // Already running there: nothing else this tick, so no cast stops the run.
    LastMovement& last = AI_VALUE(LastMovement&, "last movement");
    if (bot->isMoving() && last.lastMoveShort.GetExactDist2d(_spot.GetPositionX(), _spot.GetPositionY()) < 1.0f)
        return true;

    // Whatever the bot was told last (a walk somewhere else, or to stand still for a cast) would
    // make this one wait its turn.
    last.clear();
    bot->CastStop();
    return MoveTo(bot->GetMapId(), _spot.GetPositionX(), _spot.GetPositionY(), _spot.GetPositionZ(), false, false,
                  false, false, MovementPriority::MOVEMENT_FORCED);
}

float RcOnyDeepBreathMultiplier::GetValue(Action* action)
{
    if (dynamic_cast<RcOnyLeaveDeepBreathAction*>(action))
        return 1.0f;

    // Nothing to work from (no breath going on, or a server without positions for it): playerbots
    // is on its own.
    Trail const* trail = ActiveTrail(bot);
    if (!trail)
        return 1.0f;

    // Playerbots' two fixed spots per breath spell: in the fire for two of the eight since the
    // strips moved.
    if (action->getName() == "ony move to safe zone")
        return 0.0f;

    // Picking a target isn't movement; only walking somewhere is held.
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action))
        return 1.0f;

    // In the fire: the way out is the only walk.
    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    if (Clearance(*trail, botX, botY) < BREATH_KEEP_OUT)
        return 0.0f;

    // Out of it: stepping out of something else on the floor is fine, and so is a walk toward
    // something on this side (a whelp, someone to heal). Anything else waits until the fire has
    // passed.
    if (action->getName() == "avoid aoe")
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 0.0f;
    return CrossesFire(*trail, botX, botY, target->GetPositionX(), target->GetPositionY(), BREATH_KEEP_OUT) ? 0.0f
                                                                                                             : 1.0f;
}

// A bot's AI waits for the spell it is casting, so a mage two seconds into a Frostbolt would
// only look up after it lands. The moment a Deep Breath starts, every bot standing in its strip
// is told to drop its cast and think again on its next update.
class RaidClearOnyBreathSpellScript : public AllSpellScript
{
public:
    RaidClearOnyBreathSpellScript() : AllSpellScript("RaidClearOnyBreathSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || caster->GetMapId() != MAP_ID ||
            std::find(DEEP_BREATHS.begin(), DEEP_BREATHS.end(), spellInfo->Id) == DEEP_BREATHS.end())
            return;
        if (!RaidClear::GetConfig().IsRaidEnabled(MAP_ID))
            return;
        Trail const* trail = TrailOf(spellInfo->Id);
        if (!trail)
            return;

        Map::PlayerList const& players = caster->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive())
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->HasStrategy("rc ony", BOT_STATE_COMBAT))
                continue;
            if (Clearance(*trail, player->GetPositionX(), player->GetPositionY()) >= BREATH_KEEP_OUT)
                continue;

            botAI->RequestSpellInterrupt();
            botAI->ResetActionDuration();
        }
    }
};

void RaidClear::OnyxiasLair::AddScripts()
{
    new RaidClearOnyBreathSpellScript();
}
