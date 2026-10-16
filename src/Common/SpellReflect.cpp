/*
 * mod-raid-clear: enemies that send spells back at whoever casts them. See SpellReflect.h.
 *
 * Released under the MIT License.
 */

#include "SpellReflect.h"

#include "Common/SplashSpells.h"

#include "GenericSpellActions.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <utility>
#include <vector>

bool RaidClear::Reflect::CanBeReflected(SpellInfo const* info)
{
    // As Spell::Spell sets m_canReflect.
    return info && info->DmgClass == SPELL_DAMAGE_CLASS_MAGIC && !info->HasAttribute(SPELL_ATTR0_IS_ABILITY) &&
           !info->HasAttribute(SPELL_ATTR1_NO_REFLECTION) && !info->HasAttribute(SPELL_ATTR0_NO_IMMUNITIES) &&
           !info->IsPassive() && (!info->IsPositive() || info->HasEffect(SPELL_EFFECT_DISPEL));
}

bool RaidClear::Reflect::Reflects(Unit const* unit, SpellInfo const* info)
{
    // As Unit::SpellHitResult rolls it.
    return unit && unit->GetTotalAuraModifier(SPELL_AURA_REFLECT_SPELLS) +
                           unit->GetTotalAuraModifierByMiscMask(SPELL_AURA_REFLECT_SPELLS_SCHOOL,
                                                                info->GetSchoolMask()) >
                       0;
}

using RaidClear::Reflect::Reflects;

float RcSpellReflectMultiplier::GetValue(Action* action)
{
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || !bot->IsInCombat())
        return 1.0f;
    uint32 const spellId = AI_VALUE2(uint32, "spell id", cast->getSpell());
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    if (!RaidClear::Reflect::CanBeReflected(info))
        return 1.0f;

    Unit* target = action->GetTarget();
    if (target == bot || (target && !bot->IsValidAttackTarget(target)))
        target = nullptr;  // self casts (Arcane Explosion), heals
    if (target && Reflects(target, info))
        return 0.0f;

    RaidClear::Splash::Reach const reach = RaidClear::Splash::ReachOf(botAI, bot, cast->getSpell());
    if (!reach.Any())
        return 1.0f;

    // Whom else it lands on: the enemies in the fight around the bot, around the target, and a
    // chain's jumps on from the target.
    std::vector<Unit*> fighting;
    for (ObjectGuid const& guid : AI_VALUE(GuidVector, "attackers"))
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && unit != target && unit->IsAlive() && unit->IsInMap(bot))
            fighting.push_back(unit);
    }

    for (Unit* unit : fighting)
    {
        // An area spell reaches a mob's edge, not its middle.
        float const reachOf = unit->GetCombatReach();
        bool const hit = (reach.aroundCaster > 0.0f && bot->GetExactDist(unit) <= reach.aroundCaster + reachOf) ||
                         (target && reach.aroundTarget > 0.0f &&
                          target->GetExactDist(unit) <= reach.aroundTarget + reachOf);
        if (hit && Reflects(unit, info))
            return 0.0f;
    }

    if (!reach.chainHops || !target)
        return 1.0f;
    // The core measures a jump edge to edge.
    std::vector<Unit*> from = { target };
    std::vector<Unit*> left = fighting;
    for (uint32 hop = 0; hop < reach.chainHops && !from.empty(); ++hop)
    {
        std::vector<Unit*> next;
        for (auto it = left.begin(); it != left.end();)
        {
            bool reached = false;
            for (Unit* source : from)
                if (source->GetExactDist(*it) <= reach.chainJump + source->GetCombatReach() + (*it)->GetCombatReach())
                {
                    reached = true;
                    break;
                }
            if (!reached)
            {
                ++it;
                continue;
            }
            if (Reflects(*it, info))
                return 0.0f;
            next.push_back(*it);
            it = left.erase(it);
        }
        from = std::move(next);
    }
    return 1.0f;
}
