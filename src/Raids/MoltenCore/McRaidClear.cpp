/*
 * mod-raid-clear: Molten Core (map 409). See McRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "McRaidClear.h"

#include "AttackAction.h"
#include "GenericSpellActions.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"

using namespace RaidClear::MoltenCore;

std::vector<RaidClear::KillOrderEntry> const& RaidClear::MoltenCore::KillOrder()
{
    static std::vector<KillOrderEntry> const table = {
        // Splits in two every 15s while it lives (up to 16), so it dies before anything else.
        { NPC_LAVA_SPAWN, 0 },

        // Healers: Sulfuron's priests cast Dark Mending, Majordomo's healers keep his elites up.
        { NPC_FLAMEWALKER_PRIEST, 1 },
        { NPC_FLAMEWAKER_HEALER, 1 },

        // The remaining boss adds, before their boss. Majordomo himself can't die (the fight
        // ends when his adds do) and Ragnaros can't be attacked while his Sons are up.
        { NPC_FLAMEWALKER_PROTECTOR, 2 },
        { NPC_FLAMEWALKER, 2 },
        { NPC_FLAMEWAKER_ELITE, 2 },
        { NPC_FIRESWORN, 2 },
        { NPC_SON_OF_FLAME, 2 },
    };
    return table;
}

std::vector<uint32> const& RaidClear::MoltenCore::TankSplitSkipBosses()
{
    static std::vector<uint32> const bosses = { NPC_GOLEMAGG };
    return bosses;
}

void RaidClearMoltenCoreStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("rc kill order", { NextAction("rc mark kill order", ACTION_RAID + 2) }));

    triggers.push_back(
        new TriggerNode("rc mc magmadar tremor", { NextAction("rc mc tremor totem", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc mc damage reflection", { NextAction("rc mc stop melee", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc mc ragnaros ranged", { NextAction("rc mc ragnaros move out", ACTION_RAID + 1) }));
}

void RaidClearMoltenCoreStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcMcReflectionMultiplier(botAI));
}

// --- Magmadar -------------------------------------------------------------

bool RcMcMagmadarTremorTrigger::IsActive()
{
    if (bot->getClass() != CLASS_SHAMAN || !bot->IsInCombat())
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "magmadar");
    if (!boss || !boss->IsAlive() || bot->GetDistance(boss) > PANIC_RANGE)
        return false;

    return !AI_VALUE2(bool, "has totem", "tremor totem");
}

bool RcMcTremorTotemAction::Execute(Event /*event*/)
{
    return botAI->CanCastSpell("tremor totem", bot) && botAI->CastSpell("tremor totem", bot);
}

// --- Majordomo Executus ---------------------------------------------------

namespace
{
    // Melee that isn't tanking: these take Damage Reflection's damage shield on every hit.
    bool IsReflectableMelee(Player* bot)
    {
        return PlayerbotAI::IsMelee(bot) && !PlayerbotAI::IsTank(bot);
    }
}

bool RcMcDamageReflectionTrigger::IsActive()
{
    if (!IsReflectableMelee(bot))
        return false;

    Unit* victim = bot->GetVictim();
    return victim && victim->HasAura(SPELL_DAMAGE_REFLECTION);
}

bool RcMcStopMeleeAction::Execute(Event /*event*/)
{
    // The multiplier keeps the attack actions from starting the swing again until the shield
    // (10s) is gone.
    return bot->AttackStop();
}

float RcMcReflectionMultiplier::GetValue(Action* action)
{
    if (!action || !bot->IsInCombat())
        return 1.0f;

    // Both shields are Majordomo's, cast on himself and every add within 100 yd.
    if (!AI_VALUE2(Unit*, "find target", "majordomo executus"))
        return 1.0f;

    bool const isSpell = dynamic_cast<CastSpellAction*>(action) != nullptr;
    bool const isSwing = dynamic_cast<AttackAction*>(action) != nullptr;
    if (!isSpell && !isSwing)
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || !bot->IsValidAttackTarget(target))
        return 1.0f;  // heals, buffs, self casts

    // Magic Reflection sends single-target spells back at the caster.
    if (isSpell && target->HasAura(SPELL_MAGIC_REFLECTION))
        return 0.0f;

    // Damage Reflection hurts whoever hits it in melee, abilities included.
    if (IsReflectableMelee(bot) && target->HasAura(SPELL_DAMAGE_REFLECTION))
        return 0.0f;

    return 1.0f;
}

// --- Ragnaros -------------------------------------------------------------

bool RcMcRagnarosRangedTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot) || !(PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot)))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "ragnaros");
    if (!boss || !boss->IsAlive() || !boss->IsInCombat())
        return false;

    return bot->GetDistance2d(boss) < RAGNAROS_RANGED_MIN;
}

bool RcMcRagnarosMoveOutAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ragnaros");
    if (!boss)
        return false;

    float const distance = RAGNAROS_RANGED_TARGET - bot->GetDistance2d(boss);
    if (distance <= 0.0f)
        return false;

    // He sits in the lava in the middle of the room; away from him is the raid's ledge.
    return MoveAway(boss, distance);
}
