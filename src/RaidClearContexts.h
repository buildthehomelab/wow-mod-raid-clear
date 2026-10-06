/*
 * mod-raid-clear: the strategy, action and trigger names this module adds to mod-playerbots.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_CONTEXTS_H
#define MOD_RAID_CLEAR_CONTEXTS_H

#include "Common/KillOrder.h"
#include "Common/TankRoles.h"
#include "Raids/MoltenCore/McRaidClear.h"

#include "NamedObjectContext.h"

class RaidClearStrategyContext : public NamedObjectContext<Strategy>
{
public:
    RaidClearStrategyContext()
    {
        creators["rc raid tanks"] = &RaidClearStrategyContext::raid_tanks;
        creators["rc moltencore"] = &RaidClearStrategyContext::moltencore;
    }

private:
    static Strategy* raid_tanks(PlayerbotAI* botAI) { return new RaidClearTanksStrategy(botAI); }
    static Strategy* moltencore(PlayerbotAI* botAI) { return new RaidClearMoltenCoreStrategy(botAI); }
};

class RaidClearActionContext : public NamedObjectContext<Action>
{
public:
    RaidClearActionContext()
    {
        creators["rc mark kill order"] = &RaidClearActionContext::mark_kill_order;
        creators["rc offtank target"] = &RaidClearActionContext::offtank_target;
        creators["rc offtank separate"] = &RaidClearActionContext::offtank_separate;

        creators["rc mc tremor totem"] = &RaidClearActionContext::mc_tremor_totem;
        creators["rc mc stop melee"] = &RaidClearActionContext::mc_stop_melee;
        creators["rc mc ragnaros move out"] = &RaidClearActionContext::mc_ragnaros_move_out;
    }

private:
    static Action* mark_kill_order(PlayerbotAI* botAI) { return new RcMarkKillOrderAction(botAI); }
    static Action* offtank_target(PlayerbotAI* botAI) { return new RcOffTankTargetAction(botAI); }
    static Action* offtank_separate(PlayerbotAI* botAI) { return new RcOffTankSeparateAction(botAI); }

    static Action* mc_tremor_totem(PlayerbotAI* botAI) { return new RcMcTremorTotemAction(botAI); }
    static Action* mc_stop_melee(PlayerbotAI* botAI) { return new RcMcStopMeleeAction(botAI); }
    static Action* mc_ragnaros_move_out(PlayerbotAI* botAI) { return new RcMcRagnarosMoveOutAction(botAI); }
};

class RaidClearTriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidClearTriggerContext()
    {
        creators["rc kill order"] = &RaidClearTriggerContext::kill_order;
        creators["rc offtank target"] = &RaidClearTriggerContext::offtank_target;
        creators["rc offtank separate"] = &RaidClearTriggerContext::offtank_separate;

        creators["rc mc magmadar tremor"] = &RaidClearTriggerContext::mc_magmadar_tremor;
        creators["rc mc damage reflection"] = &RaidClearTriggerContext::mc_damage_reflection;
        creators["rc mc ragnaros ranged"] = &RaidClearTriggerContext::mc_ragnaros_ranged;
    }

private:
    static Trigger* kill_order(PlayerbotAI* botAI) { return new RcKillOrderTrigger(botAI); }
    static Trigger* offtank_target(PlayerbotAI* botAI) { return new RcOffTankTargetTrigger(botAI); }
    static Trigger* offtank_separate(PlayerbotAI* botAI) { return new RcOffTankSeparateTrigger(botAI); }

    static Trigger* mc_magmadar_tremor(PlayerbotAI* botAI) { return new RcMcMagmadarTremorTrigger(botAI); }
    static Trigger* mc_damage_reflection(PlayerbotAI* botAI) { return new RcMcDamageReflectionTrigger(botAI); }
    static Trigger* mc_ragnaros_ranged(PlayerbotAI* botAI) { return new RcMcRagnarosRangedTrigger(botAI); }
};

#endif
