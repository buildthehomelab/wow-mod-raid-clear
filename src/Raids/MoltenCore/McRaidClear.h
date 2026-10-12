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
 *   - Garr: a Firesworn more than 40 yd from him gets Separation Anxiety (23492, +300% damage),
 *     and a Firesworn erupts when it dies (Eruption, 19497: about 2,000 fire to everyone within
 *     15 yd). With the two together that is 8,000, which kills everyone around it. So the fight
 *     stays with Garr: a bot more than 30 yd from him walks back to 22 (a Firesworn follows
 *     whoever it is on; a tank only comes when a Firesworn is on it, so trash pulled with the
 *     boss stays where its tank holds it), nobody but the tanks hits a Firesworn that has
 *     Separation Anxiety or is about to get it (bots already on one stop, pets too), and the
 *     kill order skips it.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_MOLTEN_CORE_H
#define MOD_RAID_CLEAR_MOLTEN_CORE_H

#include "Common/KillOrder.h"
#include "Common/TankRoles.h"

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
        NPC_GARR                 = 12057,
        NPC_GOLEMAGG             = 11988,

        NPC_MOLTEN_GIANT         = 11658,  // Smash: 10 yd AoE, Knock Away
        NPC_MOLTEN_DESTROYER     = 11659,  // Massive Tremor: 13 yd AoE, Knockdown
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
        SPELL_SEPARATION_ANXIETY = 23492,  // on a Firesworn more than 40 yd from Garr
    };

    // Panic (19408) radius.
    constexpr float PANIC_RANGE = 45.0f;
    // Wrath of Ragnaros (20566) radius is 25 yd; leave a little room for his combat reach
    // and for movement jitter.
    constexpr float RAGNAROS_RANGED_MIN = 27.0f;
    constexpr float RAGNAROS_RANGED_TARGET = 30.0f;

    // Garr checks his Firesworn once a second: more than 40 yd from him (edge to edge) and it
    // gets Separation Anxiety. A Firesworn this far out counts as having it already.
    constexpr float GARR_ANXIOUS_RANGE = 36.0f;
    // A bot further than this from Garr walks back to the target distance.
    constexpr float GARR_GATHER = 30.0f;
    constexpr float GARR_GATHER_TARGET = 22.0f;

    std::vector<KillOrderEntry> const& KillOrder();
    // Adds the kill order leaves alone for now (a Firesworn with Separation Anxiety).
    bool KillOrderSkips(Player* bot, Unit const* unit);

    // Bosses where mod-playerbots places the tanks itself (Golemagg: main tank on him, assist
    // tank on the Core Ragers at fixed spots), so the generic off-tank split stays out.
    std::vector<uint32> const& TankSplitSkipBosses();

    // Trash whose AoE needs more room than the default tank separation.
    std::vector<Tanks::SplashRadius> const& TankSplitSplashRadii();
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

// --- Garr -----------------------------------------------------------------

class RcMcGarrGatherTrigger : public Trigger
{
public:
    RcMcGarrGatherTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc garr gather") {}
    bool IsActive() override;
};

class RcMcGarrGatherAction : public MovementAction
{
public:
    RcMcGarrGatherAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc mc garr gather") {}
    bool Execute(Event event) override;
};

// A bot that isn't a tank is attacking a Firesworn that has Separation Anxiety.
class RcMcAnxiousFireswornTrigger : public Trigger
{
public:
    RcMcAnxiousFireswornTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc anxious firesworn") {}
    bool IsActive() override;
};

// Stops the swing, the Auto Shot or the wand, and the pet.
class RcMcLeaveFireswornAction : public Action
{
public:
    RcMcLeaveFireswornAction(PlayerbotAI* botAI) : Action(botAI, "rc mc leave firesworn") {}
    bool Execute(Event event) override;
};

// Nobody but the tanks hits a Firesworn that would erupt for four times the damage.
class RcMcAnxiousFireswornMultiplier : public Multiplier
{
public:
    RcMcAnxiousFireswornMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc mc anxious firesworn") {}
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
