/*
 * mod-raid-clear: Temple of Ahn'Qiraj, the Prophet Skeram. See AqRaidClear.h. The kill order is
 * in AqRaidClear.cpp; mind-controlled raid members are Common/Charmed.h's.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/Interrupts.h"
#include "RaidClearConfig.h"

#include "AllSpellScript.h"
#include "Creature.h"
#include "Map.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Timer.h"

#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <utility>

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    bool IsSkeramCast(uint32 spellId)
    {
        return spellId == SPELL_SKERAM_ARCANE_EXPLOSION || spellId == SPELL_TRUE_FULFILLMENT;
    }

    // What this Skeram (he or an image) is casting that has to be stopped, or 0.
    uint32 CastToStop(Unit const* skeram)
    {
        Spell const* spell = skeram->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        uint32 const id = spell ? spell->GetSpellInfo()->Id : 0;
        return IsSkeramCast(id) ? id : 0;
    }

    // When a bot last sent an interrupt at each Skeram (getMSTime()), by instance and caster. Map
    // updates move between threads from one tick to the next, so it is shared and locked.
    using InterruptKey = std::pair<uint32, ObjectGuid>;
    std::map<InterruptKey, uint32> sInterruptsSent;
    std::mutex sInterruptsLock;

    bool InterruptJustSent(Unit const* skeram)
    {
        std::lock_guard<std::mutex> lock(sInterruptsLock);
        auto it = sInterruptsSent.find({ skeram->GetInstanceId(), skeram->GetGUID() });
        return it != sInterruptsSent.end() && getMSTimeDiff(it->second, getMSTime()) < SKERAM_INTERRUPT_SPREAD_MS;
    }

    void InterruptSent(Unit const* skeram)
    {
        std::lock_guard<std::mutex> lock(sInterruptsLock);
        uint32 const now = getMSTime();
        // Stamps that have run out (images gone, other raids) go.
        for (auto it = sInterruptsSent.begin(); it != sInterruptsSent.end();)
            it = getMSTimeDiff(it->second, now) >= SKERAM_INTERRUPT_SPREAD_MS ? sInterruptsSent.erase(it) : std::next(it);
        sInterruptsSent[{ skeram->GetInstanceId(), skeram->GetGUID() }] = now ? now : 1;
    }

    // The Skeram fighting the raid that this bot should interrupt now, and with what: True
    // Fulfillment before Arcane Explosion (a mind-controlled member does more harm than one
    // blast), then the nearest.
    Unit* SkeramToInterrupt(PlayerbotAI* botAI, Player* bot, char const*& with)
    {
        Unit* best = nullptr;
        bool bestCharm = false;
        float bestDist = 0.0f;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || unit->GetEntry() != NPC_SKERAM || !unit->IsAlive())
                continue;
            uint32 const cast = CastToStop(unit);
            if (!cast || InterruptJustSent(unit))
                continue;
            char const* interrupt = RaidClear::ReadyInterrupt(botAI, bot, unit);
            if (!interrupt)
                continue;

            bool const charm = cast == SPELL_TRUE_FULFILLMENT;
            float const dist = bot->GetDistance(unit);
            if (!best || (charm && !bestCharm) || (charm == bestCharm && dist < bestDist))
            {
                best = unit;
                bestCharm = charm;
                bestDist = dist;
                with = interrupt;
            }
        }
        return best;
    }
}

bool RaidClear::TempleOfAhnQiraj::SkeramWaitsForImages(Unit const* unit)
{
    // Only Skeram himself: his images are summons of his.
    Creature const* skeram = unit ? unit->ToCreature() : nullptr;
    if (!skeram || skeram->GetEntry() != NPC_SKERAM || skeram->IsSummon())
        return false;

    std::list<Creature*> images;
    skeram->GetCreatureListWithEntryInGrid(images, NPC_SKERAM, SKERAM_IMAGE_SEARCH);
    for (Creature const* image : images)
        if (image != skeram && image->IsSummon() && image->IsAlive() && !image->IsImmuneToPC())
            return true;
    return false;
}

bool RcAq40SkeramCastTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    char const* with = nullptr;
    return SkeramToInterrupt(botAI, bot, with) != nullptr;
}

bool RcAq40SkeramInterruptAction::Execute(Event /*event*/)
{
    char const* with = nullptr;
    Unit* skeram = SkeramToInterrupt(botAI, bot, with);
    if (!skeram)
        return false;
    InterruptSent(skeram);
    return botAI->CastSpell(with, skeram);
}

// A bot doesn't run its engine while it casts, and Arcane Explosion is cast in a second: the
// moment he or an image starts one of the two, the bots that could stop it look at once. Those
// that aren't casting anything just think now; if none of them can, the nearest one that is
// casting drops its own cast for it.
class RaidClearSkeramWakeSpellScript : public AllSpellScript
{
public:
    RaidClearSkeramWakeSpellScript() : AllSpellScript("RaidClearSkeramWakeSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || !IsSkeramCast(spellInfo->Id) || caster->GetEntry() != NPC_SKERAM ||
            caster->GetMapId() != MAP_ID || !RaidClear::GetConfig().IsRaidEnabled(MAP_ID))
            return;

        PlayerbotAI* nearestCasting = nullptr;
        float nearestDist = 0.0f;
        bool woken = false;
        Map::PlayerList const& players = caster->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive() || player->GetExactDist(caster) > SKERAM_WAKE_RANGE)
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->HasStrategy("rc aq40", BOT_STATE_COMBAT) ||
                !RaidClear::ReadyInterrupt(botAI, player, caster))
                continue;

            if (!player->GetCurrentSpell(CURRENT_GENERIC_SPELL) && !player->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            {
                botAI->ResetActionDuration();
                woken = true;
                continue;
            }
            float const dist = player->GetExactDist(caster);
            if (!nearestCasting || dist < nearestDist)
            {
                nearestCasting = botAI;
                nearestDist = dist;
            }
        }

        if (!woken && nearestCasting)
        {
            nearestCasting->RequestSpellInterrupt();
            nearestCasting->ResetActionDuration();
        }
    }
};

void RaidClear::TempleOfAhnQiraj::AddSkeramScripts()
{
    new RaidClearSkeramWakeSpellScript();
}
