/*
 * mod-raid-clear: Ruins of Ahn'Qiraj, Moam. See AqRaidClear.h.
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
#include "SpellMgr.h"

using namespace RaidClear::RuinsOfAhnQiraj;

namespace
{
    Creature* FightingMoam(Player* bot) { return RaidClear::FightingCreature(bot, { NPC_MOAM }); }

    // The spell this bot takes his mana away with (playerbots' name), or nullptr. A priest that
    // heals keeps healing.
    char const* DrainSpellFor(Player* bot)
    {
        switch (bot->getClass())
        {
            case CLASS_HUNTER:  return "viper sting";
            case CLASS_WARLOCK: return "drain mana";
            case CLASS_PRIEST:  return PlayerbotAI::IsHeal(bot) ? nullptr : "mana burn";
            default:            return nullptr;
        }
    }

    // The bot's drain, when it can cast it at him from where it stands and it isn't on him
    // already. Playerbots' own check lets a spell through out of range (the walk there is another
    // action's job), so the range is checked here, edge to edge as the core measures it, with the
    // bot's range talents.
    char const* ReadyDrain(PlayerbotAI* botAI, Player* bot, Creature* moam)
    {
        char const* spell = DrainSpellFor(bot);
        if (!spell || moam->GetPowerPct(POWER_MANA) < MOAM_DRAIN_MIN_PCT)
            return nullptr;
        uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        if (!info || !bot->IsWithinCombatRange(moam, info->GetMaxRange(false, bot)) || !bot->IsWithinLOSInMap(moam))
            return nullptr;

        // Viper Sting drains for as long as it lasts; a Drain Mana already channelled at him
        // isn't started over.
        if (bot->getClass() == CLASS_HUNTER && botAI->HasAura(spell, moam, false, true))
            return nullptr;
        if (Spell const* channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            if (channel->GetSpellInfo()->Id == spellId)
                return nullptr;

        return botAI->CanCastSpell(spell, moam) && RaidClear::CanPayFor(botAI, bot, spell) ? spell : nullptr;
    }
}

bool RcAq20MoamDrainTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !DrainSpellFor(bot))
        return false;
    Creature* moam = FightingMoam(bot);
    return moam && ReadyDrain(botAI, bot, moam);
}

bool RcAq20MoamDrainAction::Execute(Event /*event*/)
{
    Creature* moam = FightingMoam(bot);
    if (!moam)
        return false;
    char const* spell = ReadyDrain(botAI, bot, moam);
    return spell && botAI->CastSpell(spell, moam);
}
