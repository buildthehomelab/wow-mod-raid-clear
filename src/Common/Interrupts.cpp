/*
 * mod-raid-clear: interrupting an enemy's cast, whoever's target it is. See Interrupts.h.
 *
 * Released under the MIT License.
 */

#include "Interrupts.h"

#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

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
    }

    bool CanPayFor(PlayerbotAI* botAI, Player* bot, char const* spell)
    {
        uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
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
            if (inReach && botAI->CanCastSpell(interrupt.spell, target) && CanPayFor(botAI, bot, interrupt.spell))
            {
                ready = interrupt.spell;
                break;
            }
        }
        return ready && bot->IsWithinLOSInMap(target) ? ready : nullptr;
    }
}
