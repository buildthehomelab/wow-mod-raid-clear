/*
 * mod-raid-clear: interrupting an enemy's cast, whoever's target it is. See Interrupts.h.
 *
 * Released under the MIT License.
 */

#include "Interrupts.h"

#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "SpellAuraDefines.h"
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <array>

namespace RaidClear
{
    namespace
    {
        struct Interrupt
        {
            char const* spell;  // playerbots name
            float range;        // 0 = melee reach
        };
        // Counterspell, Wind Shear and Silence already go to an enemy healer that isn't the bot's
        // target (playerbots' "on enemy healer" triggers); the melee ones only ever hit the target.
        constexpr std::array<Interrupt, 8> INTERRUPTS = {{
            { "kick", 0.0f }, { "pummel", 0.0f }, { "shield bash", 0.0f }, { "mind freeze", 0.0f },
            { "wind shear", 25.0f }, { "counterspell", 30.0f }, { "silence", 30.0f }, { "silencing shot", 35.0f },
        }};

        // Whether this interrupt or silence gets through to the target: the spell as a whole (a
        // Counterspell carries its mechanic on the spell) and the effect that does the stopping
        // (Kick, Pummel and Wind Shear carry it on that effect, next to ones that still land).
        bool GetsThrough(Player* bot, Unit* target, SpellInfo const* info)
        {
            if (target->IsImmunedToSpell(info, bot))
                return false;
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                SpellEffectInfo const& effect = info->Effects[i];
                bool const stops = effect.Effect == SPELL_EFFECT_INTERRUPT_CAST ||
                                   (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_MOD_SILENCE);
                if (stops && !target->IsImmunedToSpellEffect(info, i, bot))
                    return true;
            }
            return false;
        }

        SpellInfo const* InfoOf(PlayerbotAI* botAI, char const* spell)
        {
            uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
            return spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        }
    }

    bool CanPayFor(PlayerbotAI* botAI, Player* bot, char const* spell)
    {
        SpellInfo const* info = InfoOf(botAI, spell);
        if (!info)
            return false;
        int32 const cost = info->CalcPowerCost(bot, info->GetSchoolMask());
        return cost <= 0 || bot->GetPower(Powers(info->PowerType)) >= uint32(cost);
    }

    char const* ReadyInterrupt(PlayerbotAI* botAI, Player* bot, Unit* target)
    {
        if (PlayerbotAI::IsTank(bot) && target->GetVictim() != bot)
            return nullptr;

        // CanCastSpell lets an out-of-range target through, so reach is checked here.
        char const* ready = nullptr;
        for (Interrupt const& interrupt : INTERRUPTS)
        {
            bool const inReach =
                interrupt.range > 0.0f ? bot->GetDistance(target) <= interrupt.range : bot->IsWithinMeleeRange(target);
            SpellInfo const* info = inReach ? InfoOf(botAI, interrupt.spell) : nullptr;
            if (info && botAI->CanCastSpell(interrupt.spell, target) && CanPayFor(botAI, bot, interrupt.spell) &&
                GetsThrough(bot, target, info))
            {
                ready = interrupt.spell;
                break;
            }
        }
        return ready && bot->IsWithinLOSInMap(target) ? ready : nullptr;
    }

    bool Interruptible(SpellInfo const* cast)
    {
        // What the core's interrupt effect asks of the cast it stops (Spell::EffectInterruptCast).
        if (!cast || cast->PreventionType != SPELL_PREVENTION_TYPE_SILENCE)
            return false;
        return cast->IsChanneled() ? (cast->ChannelInterruptFlags & CHANNEL_INTERRUPT_FLAG_INTERRUPT) != 0
                                   : (cast->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT) != 0;
    }

    bool IsInterrupt(std::string const& name)
    {
        return std::any_of(INTERRUPTS.begin(), INTERRUPTS.end(),
                           [&name](Interrupt const& interrupt) { return name == interrupt.spell; });
    }
}
