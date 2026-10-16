/*
 * mod-raid-clear: Temple of Ahn'Qiraj, the Bug Trio (Princess Yauj, Lord Kri, Vem). See
 * AqRaidClear.h. The kill order is in AqRaidClear.cpp.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"
#include "Common/Interrupts.h"

#include "Creature.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <list>

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    // --- Yauj -------------------------------------------------------------

    Creature* FightingYauj(Player* bot) { return RaidClear::FightingCreature(bot, { NPC_YAUJ }); }

    // Princess Yauj, in the middle of Great Heal.
    Creature* YaujHealing(Player* bot)
    {
        Creature* yauj = FightingYauj(bot);
        if (!yauj)
            return nullptr;
        Spell const* spell = yauj->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        return spell && spell->GetSpellInfo()->Id == SPELL_YAUJ_GREAT_HEAL ? yauj : nullptr;
    }

    bool NearYauj(Player* bot)
    {
        Creature* yauj = FightingYauj(bot);
        return yauj && bot->GetDistance(yauj) <= YAUJ_FEAR_RANGE;
    }

    // --- Poison Cloud -----------------------------------------------------

    float CloudRadius()
    {
        static float const radius = RaidClear::AhnQiraj::SpellRadius(SPELL_TOXIC_VAPORS, 8.0f);
        return radius;
    }

    struct Cloud
    {
        float x;
        float y;
    };

    // Kri's clouds in the bot's copy of the room, looked up again after CLOUDS_FRESH_MS: the
    // triggers and the hold multiplier of the whole raid ask many times a second. One instance's
    // bots update one after another on one thread. Only while a bug is still in the fight.
    constexpr uint32 CLOUDS_FRESH_MS = 200;
    std::vector<Cloud> const& Clouds(Player* bot)
    {
        thread_local uint32 tInstanceId = 0;
        thread_local uint32 tAtMs = 0;
        thread_local std::vector<Cloud> tClouds;
        uint32 const now = getMSTime();
        if (tInstanceId == bot->GetInstanceId() && tAtMs && getMSTimeDiff(tAtMs, now) < CLOUDS_FRESH_MS)
            return tClouds;

        tClouds.clear();
        tInstanceId = bot->GetInstanceId();
        tAtMs = now ? now : 1;
        Creature* bug = RaidClear::FightingCreature(bot, { NPC_YAUJ, NPC_VEM, NPC_KRI, NPC_YAUJ_BROOD });
        if (!bug)
            return tClouds;
        std::list<Creature*> found;
        bug->GetCreatureListWithEntryInGrid(found, NPC_POISON_CLOUD, POISON_CLOUD_SEARCH);
        for (Creature const* cloud : found)
            if (cloud->IsAlive())
                tClouds.push_back({ cloud->GetPositionX(), cloud->GetPositionY() });
        return tClouds;
    }

    // Yards from a spot to the nearest cloud, centre to centre.
    float CloudGap(std::vector<Cloud> const& clouds, float x, float y)
    {
        float gap = 1000.0f;
        for (Cloud const& cloud : clouds)
            gap = std::min(gap, std::hypot(cloud.x - x, cloud.y - y));
        return gap;
    }

    float CloudGap(std::vector<Cloud> const& clouds, Position const* pos)
    {
        return CloudGap(clouds, pos->GetPositionX(), pos->GetPositionY());
    }
}

// --- Great Heal -----------------------------------------------------------

bool RcAq40YaujHealTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    Creature* yauj = YaujHealing(bot);
    return yauj && RaidClear::ReadyInterrupt(botAI, bot, yauj);
}

bool RcAq40YaujInterruptAction::Execute(Event /*event*/)
{
    Creature* yauj = YaujHealing(bot);
    if (!yauj)
        return false;
    char const* interrupt = RaidClear::ReadyInterrupt(botAI, bot, yauj);
    return interrupt && botAI->CastSpell(interrupt, yauj);
}

// --- Fear -----------------------------------------------------------------

bool RcAq40YaujTremorTrigger::IsActive()
{
    if (bot->getClass() != CLASS_SHAMAN || !bot->IsAlive() || !bot->IsInCombat() || !NearYauj(bot))
        return false;
    return !AI_VALUE2(bool, "has totem", "tremor totem");
}

float RcAq40YaujTremorMultiplier::GetValue(Action* action)
{
    if (!action || bot->getClass() != CLASS_SHAMAN || !bot->IsInCombat())
        return 1.0f;

    // Playerbots' names for whatever puts a totem in the earth slot or takes this one away.
    static constexpr std::array<char const*, 6> EARTH_SLOT = {
        "stoneskin totem", "strength of earth totem", "earthbind totem", "stoneclaw totem",
        "call of the elements", "totemic recall",
    };
    std::string const name = action->getName();
    if (std::none_of(EARTH_SLOT.begin(), EARTH_SLOT.end(), [&name](char const* other) { return name == other; }))
        return 1.0f;

    // A shaman without Tremor Totem keeps what it has.
    return NearYauj(bot) && AI_VALUE2(uint32, "spell id", "tremor totem") ? 0.0f : 1.0f;
}

// --- Poison Cloud ---------------------------------------------------------

bool RcAq40PoisonCloudTrigger::IsActive()
{
    // A bot that is feared or rooted isn't going anywhere.
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    return CloudGap(Clouds(bot), bot) < CloudRadius() + POISON_CLOUD_KEEP_EXTRA;
}

bool RcAq40LeavePoisonCloudAction::Execute(Event /*event*/)
{
    std::vector<Cloud> const clouds = Clouds(bot);
    float const radius = CloudRadius();
    float const keepAway = radius + POISON_CLOUD_KEEP_EXTRA;
    if (CloudGap(clouds, bot) >= keepAway)
        return false;
    auto const gapAt = [&clouds](float x, float y, float /*z*/) { return CloudGap(clouds, x, y); };
    // Up to the whole cloud across from the middle of it.
    return _walk.Go(gapAt, keepAway, radius + POISON_CLOUD_SPOT_EXTRA, POISON_CLOUD_WORTH_IT,
                    radius + POISON_CLOUD_SPOT_EXTRA + 3.0f, clouds.size() == 1);
}

float RcAq40PoisonCloudHoldMultiplier::GetValue(Action* action)
{
    Unit* target = RaidClear::AhnQiraj::WalkTarget(action, bot);
    if (!target)
        return 1.0f;
    std::vector<Cloud> const& clouds = Clouds(bot);
    float const radius = CloudRadius();
    if (clouds.empty() || CloudGap(clouds, bot) >= radius + POISON_CLOUD_HOLD_EXTRA)
        return 1.0f;
    return CloudGap(clouds, target) < radius + POISON_CLOUD_KEEP_EXTRA ? 0.0f : 1.0f;
}
