/*
 * mod-raid-clear: which of a bot's spells reach more than their target. See SplashSpells.h.
 *
 * Released under the MIT License.
 */

#include "SplashSpells.h"

#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <array>

namespace RaidClear::Splash
{
    namespace
    {
        // Spells the server's data doesn't show as hitting more than one enemy, by their
        // playerbots names, with how far they reach: the damage sits in a spell they trigger or
        // in something they summon, or they are a buff that copies the bot's swings.
        struct Hidden
        {
            char const* name;
            float aroundCaster;
            float aroundTarget;
        };
        constexpr std::array<Hidden, 11> HIDDEN = {{
            { "blade flurry", 8.0f, 0.0f },
            { "sweeping strikes", 8.0f, 0.0f },
            { "fire nova", 10.0f, 0.0f },
            { "magma totem", 8.0f, 0.0f },
            { "explosive trap", 10.0f, 0.0f },
            { "bladestorm", 8.0f, 0.0f },
            { "starfall", 30.0f, 0.0f },
            { "typhoon", 30.0f, 0.0f },
            { "living bomb", 0.0f, 10.0f },
            { "immolation aura", 8.0f, 0.0f },
            { "seed of corruption", 0.0f, 15.0f },
        }};

        Hidden const* HiddenSplash(std::string const& name)
        {
            for (Hidden const& hidden : HIDDEN)
                if (name == hidden.name)
                    return &hidden;
            return nullptr;
        }

        SpellInfo const* InfoOf(PlayerbotAI* botAI, std::string const& name)
        {
            uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get();
            return spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        }

        bool HitsArea(SpellEffectInfo const& effect)
        {
            return effect.IsTargetingArea() || effect.IsEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA);
        }

        bool HitsMoreThanOneEnemy(SpellInfo const* info)
        {
            if (info->IsPositive())
                return false;
            for (SpellEffectInfo const& effect : info->Effects)
                if (effect.IsEffect() && (HitsArea(effect) || effect.ChainTarget > 1))
                    return true;
            return false;
        }

        // The core's Spell::SelectImplicitChainTargets: how far one jump goes, and whether the
        // chain goes on from each enemy it hits or only takes enemies next to its target.
        float ChainJump(SpellInfo const* info)
        {
            if (info->JumpDistance > 0.0f)
                return info->JumpDistance;
            return info->DmgClass == SPELL_DAMAGE_CLASS_RANGED ? 7.5f : 10.0f;
        }

        bool ChainBouncesOn(SpellInfo const* info)
        {
            return info->HasAttribute(SPELL_ATTR4_BOUNCY_CHAIN_MISSILES) ||
                   info->DmgClass == SPELL_DAMAGE_CLASS_NONE || info->DmgClass == SPELL_DAMAGE_CLASS_MAGIC;
        }
    }

    bool IsSplashSpell(PlayerbotAI* botAI, std::string const& name)
    {
        // Picks a spot on the ground, but only to summon the treants there.
        if (name == "force of nature")
            return false;
        if (HiddenSplash(name))
            return true;

        SpellInfo const* info = InfoOf(botAI, name);
        return info && HitsMoreThanOneEnemy(info);
    }

    Reach ReachOf(PlayerbotAI* botAI, Player* bot, std::string const& name)
    {
        Reach reach;
        if (name == "force of nature")
            return reach;
        if (Hidden const* hidden = HiddenSplash(name))
        {
            reach.aroundCaster = hidden->aroundCaster;
            reach.aroundTarget = hidden->aroundTarget;
            return reach;
        }

        SpellInfo const* info = InfoOf(botAI, name);
        if (!info || info->IsPositive())
            return reach;
        for (SpellEffectInfo const& effect : info->Effects)
        {
            if (!effect.IsEffect())
                continue;
            if (effect.ChainTarget > 1)
            {
                // ChainTarget counts the target itself; a glyph or talent may add to it.
                if (ChainBouncesOn(info))
                {
                    uint32 targets = effect.ChainTarget;
                    bot->ApplySpellMod(info->Id, SPELLMOD_JUMP_TARGETS, targets);
                    reach.chainJump = std::max(reach.chainJump, ChainJump(info));
                    reach.chainHops = std::max(reach.chainHops, targets > 1 ? targets - 1 : 0);
                }
                else
                    reach.aroundTarget = std::max(reach.aroundTarget, ChainJump(info));
            }
            if (!HitsArea(effect))
                continue;

            // Around the caster (Arcane Explosion, Consecration, a cone), or around whatever it
            // was aimed at (Blizzard, Howling Blast).
            float const radius = effect.CalcRadius(bot);
            SpellTargetReferenceTypes const from = effect.TargetA.GetReferenceType();
            if (from == TARGET_REFERENCE_TYPE_CASTER || from == TARGET_REFERENCE_TYPE_SRC)
                reach.aroundCaster = std::max(reach.aroundCaster, radius);
            else
                reach.aroundTarget = std::max(reach.aroundTarget, radius);
        }
        return reach;
    }
}
