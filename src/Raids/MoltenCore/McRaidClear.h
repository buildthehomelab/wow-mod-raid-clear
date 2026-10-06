/*
 * mod-raid-clear: Molten Core (map 409).
 *
 * Runs next to mod-playerbots' "moltencore" strategy, which already handles Geddon's Living
 * Bomb and Inferno, Shazzrah's Arcane Explosion range, the Golemagg / Core Rager tank split,
 * Core Hound skull rotation, lava escape and the resistance auras/totems. Fire patches (Lava
 * Bomb traps) and Rain of Fire are left to playerbots' generic "avoid aoe".
 *
 * Added here (mechanics from the core scripts in scripts/EasternKingdoms/BlackrockMountain/
 * MoltenCore):
 *   - Kill order: Lava Spawn (splits every 15s), the healing adds (Sulfuron's priests,
 *     Majordomo's healers), then the other boss adds and Ragnaros' Sons of Flame.
 *   - Magmadar: every shaman keeps Tremor Totem down. Panic is a 45 yd raid-wide fear.
 *   - Majordomo: nobody casts at an enemy with Magic Reflection, and melee (not tanks) stop
 *     swinging at one with Damage Reflection. Both shields cover all of his adds.
 *   - Ragnaros: ranged and healers stay out of Wrath of Ragnaros (25 yd knockback around him).
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_MOLTEN_CORE_H
#define MOD_RAID_CLEAR_MOLTEN_CORE_H

#include "Common/KillOrder.h"

#include "Action.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "Strategy.h"
#include "Trigger.h"

#include <vector>

namespace RaidClear::MoltenCore
{
    constexpr uint32 MAP_ID = 409;

    enum Creatures : uint32
    {
        NPC_MAGMADAR             = 11982,
        NPC_MAJORDOMO            = 12018,
        NPC_RAGNAROS             = 11502,

        NPC_FLAMEWALKER          = 11661,  // Gehennas
        NPC_FLAMEWALKER_PRIEST   = 11662,  // Sulfuron
        NPC_FLAMEWAKER_HEALER    = 11663,  // Majordomo
        NPC_FLAMEWAKER_ELITE     = 11664,  // Majordomo
        NPC_FIRESWORN            = 12099,  // Garr
        NPC_FLAMEWALKER_PROTECTOR = 12119, // Lucifron
        NPC_SON_OF_FLAME         = 12143,  // Ragnaros, submerge phase
        NPC_LAVA_SPAWN           = 12265,  // Firelord trash, splits every 15s
    };

    enum Spells : uint32
    {
        SPELL_MAGIC_REFLECTION   = 20619,
        SPELL_DAMAGE_REFLECTION  = 21075,
    };

    // Panic (19408) radius.
    constexpr float PANIC_RANGE = 45.0f;
    // Wrath of Ragnaros (20566) radius is 25 yd; leave a little room for his combat reach
    // and for movement jitter.
    constexpr float RAGNAROS_RANGED_MIN = 27.0f;
    constexpr float RAGNAROS_RANGED_TARGET = 30.0f;

    std::vector<KillOrderEntry> const& KillOrder();
}

class RaidClearMoltenCoreStrategy : public Strategy
{
public:
    RaidClearMoltenCoreStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc moltencore"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// --- Magmadar -------------------------------------------------------------

class RcMcMagmadarTremorTrigger : public Trigger
{
public:
    RcMcMagmadarTremorTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc magmadar tremor") {}
    bool IsActive() override;
};

class RcMcTremorTotemAction : public Action
{
public:
    RcMcTremorTotemAction(PlayerbotAI* botAI) : Action(botAI, "rc mc tremor totem") {}
    bool Execute(Event event) override;
};

// --- Majordomo Executus ---------------------------------------------------

class RcMcDamageReflectionTrigger : public Trigger
{
public:
    RcMcDamageReflectionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc damage reflection") {}
    bool IsActive() override;
};

class RcMcStopMeleeAction : public Action
{
public:
    RcMcStopMeleeAction(PlayerbotAI* botAI) : Action(botAI, "rc mc stop melee") {}
    bool Execute(Event event) override;
};

class RcMcReflectionMultiplier : public Multiplier
{
public:
    RcMcReflectionMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc mc reflection") {}
    float GetValue(Action* action) override;
};

// --- Ragnaros -------------------------------------------------------------

class RcMcRagnarosRangedTrigger : public Trigger
{
public:
    RcMcRagnarosRangedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc ragnaros ranged") {}
    bool IsActive() override;
};

class RcMcRagnarosMoveOutAction : public MovementAction
{
public:
    RcMcRagnarosMoveOutAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc mc ragnaros move out") {}
    bool Execute(Event event) override;
};

#endif
