/*
 * mod-raid-clear: enemies that send spells back at whoever casts them.
 *
 * Playerbots casts its rotation whatever the target has on: an arcane mage goes on with Arcane
 * Missiles into an Anubisath's Fire and Arcane Reflect and hits itself with every one. Several
 * raid enemies carry a reflect: the Anubisath Guardians of Ruins (AQ20), the Anubisath Sentinels
 * and Defenders of the Temple (AQ40), each with either Fire and Arcane Reflect (13022) or Shadow
 * and Frost Reflect (19595), and Majordomo's adds under Magic Reflection (20619).
 *
 * So a spell the core would reflect off its target is not cast at it, nor a channel whose
 * missiles would be (Arcane Missiles: the channel can't be reflected, each missile it fires can).
 * The bot goes down its list to the next spell: an arcane or fire mage to Frostbolt, a warlock
 * under Fire and Arcane Reflect to Shadow Bolt. Where playerbots' list has the wand before a spell
 * of the other school (a frost mage's Fireball, an affliction warlock's Immolate), "rc target
 * reflects" puts that spell just above the wand. Not Searing Pain: its threat would pull the mob
 * off the tank. Nor is a chain or area spell cast where it would land on a
 * reflecting enemy in the fight (Chain Lightning under Magic Reflection, Seed of Corruption);
 * most area spells (Arcane Explosion, Blizzard, Consecration) can't be reflected at all, and a
 * spell cast on the bot itself (Starfall) never is.
 * Pets cast through the core's pet AI and still hit the reflect.
 *
 * Read from the auras themselves, as the core does (Unit::SpellHitResult), so it covers every
 * reflect in every raid with no list to keep.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_SPELL_REFLECT_H
#define MOD_RAID_CLEAR_SPELL_REFLECT_H

#include "Multiplier.h"
#include "Trigger.h"

class SpellInfo;
class Unit;

namespace RaidClear::Reflect
{
    // The core would try to reflect this spell (Spell::m_canReflect): a magic spell, not an
    // ability, that harms its target.
    bool CanBeReflected(SpellInfo const* info);

    // What of this spell a reflect would send back: the spell itself, else the first spell it
    // triggers on its target that can be reflected (Arcane Missiles' missiles), else nullptr.
    SpellInfo const* ReflectedPart(SpellInfo const* info);

    // The unit carries a reflect of any school.
    bool HasReflect(Unit const* unit);

    // The unit has a chance to reflect this spell back.
    bool Reflects(Unit const* unit, SpellInfo const* info);
}

// Zeroes spells a reflecting enemy would send back at the bot.
class RcSpellReflectMultiplier : public Multiplier
{
public:
    RcSpellReflectMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc spell reflect") {}
    float GetValue(Action* action) override;
};

// The bot's target reflects something, and the bot (its class) has a spell of another school that
// playerbots ranks under its wand.
class RcTargetReflectsTrigger : public Trigger
{
public:
    RcTargetReflectsTrigger(PlayerbotAI* botAI, std::string const name, uint8 cls)
        : Trigger(botAI, name), _class(cls)
    {
    }
    bool IsActive() override;

private:
    uint8 _class;
};

#endif
