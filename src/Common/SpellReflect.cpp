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

SpellInfo const* RaidClear::Reflect::ReflectedPart(SpellInfo const* info)
{
    if (!info || CanBeReflected(info))
        return info;
    // A channel or a spell that hands its damage on: Arcane Missiles' channel can't be reflected,
    // the missile it fires at the target each second can.
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (!effect.TriggerSpell ||
            !(effect.IsAura(SPELL_AURA_PERIODIC_TRIGGER_SPELL) || effect.IsEffect(SPELL_EFFECT_TRIGGER_SPELL) ||
              effect.IsEffect(SPELL_EFFECT_TRIGGER_MISSILE)))
            continue;
        SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell);
        if (CanBeReflected(triggered))
            return triggered;
    }
    return nullptr;
}

bool RaidClear::Reflect::HasReflect(Unit const* unit)
{
    return unit && (unit->HasAuraType(SPELL_AURA_REFLECT_SPELLS) || unit->HasAuraType(SPELL_AURA_REFLECT_SPELLS_SCHOOL));
}

using RaidClear::Reflect::Reflects;

float RcSpellReflectMultiplier::GetValue(Action* action)
{
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || !bot->IsInCombat())
        return 1.0f;

    Unit* target = action->GetTarget();
    if (target == bot || (target && !bot->IsValidAttackTarget(target)))
        target = nullptr;  // self casts (Arcane Explosion), heals

    // Whom else a chain or area spell lands on: the enemies in the fight. Nothing to do where
    // none of them, nor the target, carries a reflect.
    std::vector<Unit*> fighting;
    bool anyReflect = RaidClear::Reflect::HasReflect(target);
    for (ObjectGuid const& guid : AI_VALUE(GuidVector, "attackers"))
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || unit == target || !unit->IsAlive() || !unit->IsInMap(bot))
            continue;
        fighting.push_back(unit);
        anyReflect = anyReflect || RaidClear::Reflect::HasReflect(unit);
    }
    if (!anyReflect)
        return 1.0f;

    uint32 const spellId = AI_VALUE2(uint32, "spell id", cast->getSpell());
    SpellInfo const* spell = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    // Only a spell cast at an enemy is reflected; one cast on the bot itself (Starfall) is not,
    // whatever it then does.
    if (!spell || !spell->NeedsExplicitUnitTarget())
        return 1.0f;
    SpellInfo const* info = RaidClear::Reflect::ReflectedPart(spell);
    if (!info)
        return 1.0f;
    if (target && Reflects(target, info))
        return 0.0f;

    RaidClear::Splash::Reach const reach = RaidClear::Splash::ReachOf(botAI, bot, cast->getSpell());
    if (!reach.Any())
        return 1.0f;
    return RaidClear::Splash::LandsOn(bot, reach, target, fighting, [info](Unit* unit) { return Reflects(unit, info); })
               ? 0.0f
               : 1.0f;
}

// --- Spells of the other school ---------------------------------------------

bool RcTargetReflectsTrigger::IsActive()
{
    if (bot->getClass() != _class || !bot->IsInCombat())
        return false;
    Unit* target = AI_VALUE(Unit*, "current target");
    return target && target->IsAlive() && RaidClear::Reflect::HasReflect(target);
}
