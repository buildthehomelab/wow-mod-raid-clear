/*
 * mod-raid-clear: enemies that send spells back at whoever casts them.
 *
 * Playerbots casts its rotation whatever the target has on: an arcane mage goes on with Arcane
 * Missiles into an Anubisath's Fire and Arcane Reflect and hits itself with every one. Several
 * raid enemies carry a reflect: the Anubisath Guardians of Ruins (AQ20), the Anubisath Sentinels
 * and Defenders of the Temple (AQ40), each with either Fire and Arcane Reflect (13022) or Shadow
 * and Frost Reflect (19595), and Majordomo's adds under Magic Reflection (20619).
 *
 * So a spell the core would reflect off its target is not cast at it: the bot goes down its list
 * to the next one (an arcane or fire mage to Frostbolt, a frost mage to Fireball, else its wand,
 * which is never reflected). Nor is an area spell or a chain cast where it would land on a
 * reflecting enemy in the fight (Arcane Explosion in a pack of Sentinels).
 *
 * Read from the auras themselves, as the core does (Unit::SpellHitResult), so it covers every
 * reflect in every raid with no list to keep.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_SPELL_REFLECT_H
#define MOD_RAID_CLEAR_SPELL_REFLECT_H

#include "Multiplier.h"

class SpellInfo;
class Unit;

namespace RaidClear::Reflect
{
    // The core would try to reflect this spell (Spell::m_canReflect): a magic spell, not an
    // ability, that harms its target.
    bool CanBeReflected(SpellInfo const* info);

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

#endif
