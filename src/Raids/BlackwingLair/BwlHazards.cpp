/*
 * mod-raid-clear: Blackwing Lair, fire on the ground and cleaves in the trash fights. See
 * BwlRaidClear.h.
 *
 * Playerbots' own "avoid aoe" only takes a bot out of fire it is already burning in, by a step
 * straight back (into a wall or the next pack as often as not), and doesn't know about a mob's
 * cleave at all. Here the fire is read from the casters themselves (the Warlocks' Rain of Fire,
 * the Spellbinders' and Hatchers' Flamestrike) while it burns, and the way out is the one the
 * aggro leash allows (Common/WayOut.h).
 *
 * Released under the MIT License.
 */

#include "BwlRaidClear.h"

#include "Creature.h"
#include "DynamicObject.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

using namespace RaidClear::BlackwingLair;

namespace
{
    struct Fire
    {
        Position pos;
        float radius;
    };

    struct Cleave
    {
        Position pos;
        float facing;
        float reach;
    };

    struct Hazards
    {
        uint32 ms = 0;
        std::vector<Fire> fires;
        std::vector<Cleave> cleaves;
    };

    // The casters whose fire stays on the ground, and the mobs that cleave.
    struct Burner
    {
        uint32 entry;
        uint32 spell;
    };
    constexpr std::array<Burner, 3> BURNERS = {{
        { NPC_BLACKWING_WARLOCK, SPELL_BWL_RAIN_OF_FIRE },
        { NPC_BLACKWING_SPELLBINDER, SPELL_BWL_FLAMESTRIKE },
        { NPC_DEATH_TALON_HATCHER, SPELL_BWL_FLAMESTRIKE },
    }};
    constexpr std::array<uint32, 2> CLEAVERS = { NPC_DEATH_TALON_CAPTAIN, NPC_DEATH_TALON_OVERSEER };

    // Everything this bot keeps out of, from the mobs fighting the raid, per bot for
    // HAZARD_FRESH_MS: the trigger asks every tick. One map thread updates a bot at a time.
    Hazards const& HazardsFor(PlayerbotAI* botAI, Player* bot)
    {
        thread_local std::unordered_map<ObjectGuid, Hazards> tHazards;
        uint32 const now = getMSTime();
        Hazards& hazards = tHazards[bot->GetGUID()];
        if (hazards.ms && getMSTimeDiff(hazards.ms, now) < HAZARD_FRESH_MS)
            return hazards;

        hazards.fires.clear();
        hazards.cleaves.clear();
        hazards.ms = now ? now : 1;
        if (bot->GetMapId() != MAP_ID)
            return hazards;

        // Tanks stand in front of what they hold, and take its cleave.
        bool const keepsOutOfCleaves = !PlayerbotAI::IsTank(bot);
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsCreature() || !unit->IsInMap(bot))
                continue;
            uint32 const entry = unit->GetEntry();
            // The caster's fire while it burns: Rain of Fire for its channel, Flamestrike for
            // eight seconds.
            for (Burner const& burner : BURNERS)
                if (burner.entry == entry)
                    if (DynamicObject* fire = unit->GetDynObject(burner.spell))
                        hazards.fires.push_back({ fire->GetPosition(), fire->GetRadius() });

            if (!keepsOutOfCleaves || !unit->IsAlive() ||
                std::find(CLEAVERS.begin(), CLEAVERS.end(), entry) == CLEAVERS.end())
                continue;
            Unit* victim = unit->GetVictim();
            if (!victim || victim == bot)
                continue;
            float const facing = unit->GetExactDist2d(victim) > 1.0f ? unit->GetAngle(victim) : unit->GetOrientation();
            hazards.cleaves.push_back({ unit->GetPosition(), facing, unit->GetCombatReach() + CLEAVE_REACH });
        }
        return hazards;
    }

    // Yards from a spot to the nearest way out of everything above; negative inside.
    float HazardGap(Hazards const& hazards, float x, float y)
    {
        float gap = 1000.0f;
        for (Fire const& fire : hazards.fires)
            gap = std::min(gap, fire.pos.GetExactDist2d(x, y) - fire.radius - GROUND_FIRE_MARGIN);
        for (Cleave const& cleave : hazards.cleaves)
        {
            float const r = cleave.pos.GetExactDist2d(x, y);
            if (r >= cleave.reach)
            {
                gap = std::min(gap, r - cleave.reach);
                continue;
            }
            float off = Position::NormalizeOrientation(std::atan2(y - cleave.pos.GetPositionY(), x - cleave.pos.GetPositionX()) -
                                                       cleave.facing);
            if (off > float(M_PI))
                off = 2.0f * float(M_PI) - off;
            // Out over its side, or back past its reach.
            float const side = off >= CLEAVE_HALF_ANGLE ? r * std::sin(std::min(off - CLEAVE_HALF_ANGLE, float(M_PI) / 2.0f))
                                                        : -std::min(r * std::sin(CLEAVE_HALF_ANGLE - off), cleave.reach - r);
            gap = std::min(gap, side);
        }
        return gap;
    }

    bool Empty(Hazards const& hazards)
    {
        return hazards.fires.empty() && hazards.cleaves.empty();
    }
}

bool RcBwlTrashHazardTrigger::IsActive()
{
    // Not only in combat: a Flamestrike lands on whoever is about, the camp included.
    if (!bot->IsAlive() || bot->GetMapId() != MAP_ID || !botAI->CanMove())
        return false;
    Hazards const& hazards = HazardsFor(botAI, bot);
    return !Empty(hazards) && HazardGap(hazards, bot->GetPositionX(), bot->GetPositionY()) < 0.0f;
}

bool RcBwlTrashHazardAction::Execute(Event /*event*/)
{
    // A copy: the walk asks for paths, and the cache is the bot's own anyway.
    Hazards const hazards = HazardsFor(botAI, bot);
    if (Empty(hazards))
        return false;
    auto const gapAt = [&hazards](float x, float y, float /*z*/) { return HazardGap(hazards, x, y); };
    if (gapAt(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= 0.0f)
        return false;
    return _walk.Go(gapAt, 0.0f, HAZARD_SPOT, 1.0f);
}
