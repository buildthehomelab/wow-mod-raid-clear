/*
 * mod-raid-clear: the strategy, action and trigger names this module adds to mod-playerbots.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_CONTEXTS_H
#define MOD_RAID_CLEAR_CONTEXTS_H

#include "Common/AggroLeash.h"
#include "Common/KillOrder.h"
#include "Common/TankRoles.h"
#include "Raids/BlackwingLair/BwlRaidClear.h"
#include "Raids/MoltenCore/McRaidClear.h"
#include "Raids/OnyxiasLair/OnyRaidClear.h"
#include "Raids/ZulGurub/ZgRaidClear.h"

#include "NamedObjectContext.h"

class RaidClearStrategyContext : public NamedObjectContext<Strategy>
{
public:
    RaidClearStrategyContext()
    {
        creators["rc raid tanks"] = &RaidClearStrategyContext::raid_tanks;
        creators["rc raid leash"] = &RaidClearStrategyContext::raid_leash;
        creators["rc moltencore"] = &RaidClearStrategyContext::moltencore;
        creators["rc bwl"] = &RaidClearStrategyContext::bwl;
        creators["rc zg"] = &RaidClearStrategyContext::zg;
        creators["rc ony"] = &RaidClearStrategyContext::ony;
    }

private:
    static Strategy* raid_tanks(PlayerbotAI* botAI) { return new RaidClearTanksStrategy(botAI); }
    static Strategy* raid_leash(PlayerbotAI* botAI) { return new RaidClearLeashStrategy(botAI); }
    static Strategy* moltencore(PlayerbotAI* botAI) { return new RaidClearMoltenCoreStrategy(botAI); }
    static Strategy* bwl(PlayerbotAI* botAI) { return new RaidClearBlackwingLairStrategy(botAI); }
    static Strategy* zg(PlayerbotAI* botAI) { return new RaidClearZulGurubStrategy(botAI); }
    static Strategy* ony(PlayerbotAI* botAI) { return new RaidClearOnyxiasLairStrategy(botAI); }
};

class RaidClearActionContext : public NamedObjectContext<Action>
{
public:
    RaidClearActionContext()
    {
        creators["rc mark kill order"] = &RaidClearActionContext::mark_kill_order;
        creators["rc offtank target"] = &RaidClearActionContext::offtank_target;
        creators["rc offtank separate"] = &RaidClearActionContext::offtank_separate;
        creators["rc tank share threat"] = &RaidClearActionContext::tank_share_threat;
        creators["rc aggro leash"] = &RaidClearActionContext::aggro_leash;

        creators["rc mc tremor totem"] = &RaidClearActionContext::mc_tremor_totem;
        creators["rc mc stop melee"] = &RaidClearActionContext::mc_stop_melee;
        creators["rc mc ragnaros move out"] = &RaidClearActionContext::mc_ragnaros_move_out;
        creators["rc mc garr gather"] = &RaidClearActionContext::mc_garr_gather;
        creators["rc mc leave firesworn"] = &RaidClearActionContext::mc_leave_firesworn;

        creators["rc bwl disarm suppression"] = &RaidClearActionContext::bwl_disarm_suppression;
        creators["rc bwl broodlord move out"] = &RaidClearActionContext::bwl_broodlord_move_out;
        creators["rc bwl ebonroc taunt"] = &RaidClearActionContext::bwl_ebonroc_taunt;
        creators["rc bwl technician spread"] = &RaidClearActionContext::bwl_technician_spread;
        creators["rc bwl firemaw hide"] = &RaidClearActionContext::bwl_firemaw_hide;
        creators["rc bwl chromaggus hide"] = &RaidClearActionContext::bwl_chromaggus_hide;
        creators["rc bwl nefarian move out"] = &RaidClearActionContext::bwl_nefarian_move_out;
        creators["rc bwl detonation keep away"] = &RaidClearActionContext::bwl_detonation_keep_away;
        creators["rc bwl seether tranq"] = &RaidClearActionContext::bwl_seether_tranq;
        creators["rc bwl remove black affliction"] = &RaidClearActionContext::bwl_remove_black_affliction;
        creators["rc bwl fire nova totem move away"] = &RaidClearActionContext::bwl_fire_nova_totem_move_away;
        creators["rc bwl captain hand off"] = &RaidClearActionContext::bwl_captain_hand_off;

        creators["rc zg holy wrath keep away"] = &RaidClearActionContext::zg_holy_wrath_keep_away;

        creators["rc ony leave deep breath"] = &RaidClearActionContext::ony_leave_deep_breath;
    }

private:
    static Action* mark_kill_order(PlayerbotAI* botAI) { return new RcMarkKillOrderAction(botAI); }
    static Action* offtank_target(PlayerbotAI* botAI) { return new RcOffTankTargetAction(botAI); }
    static Action* offtank_separate(PlayerbotAI* botAI) { return new RcOffTankSeparateAction(botAI); }
    static Action* tank_share_threat(PlayerbotAI* botAI) { return new RcTankShareThreatAction(botAI); }
    static Action* aggro_leash(PlayerbotAI* botAI) { return new RcAggroLeashAction(botAI); }

    static Action* mc_tremor_totem(PlayerbotAI* botAI) { return new RcMcTremorTotemAction(botAI); }
    static Action* mc_stop_melee(PlayerbotAI* botAI) { return new RcMcStopMeleeAction(botAI); }
    static Action* mc_ragnaros_move_out(PlayerbotAI* botAI) { return new RcMcRagnarosMoveOutAction(botAI); }
    static Action* mc_garr_gather(PlayerbotAI* botAI) { return new RcMcGarrGatherAction(botAI); }
    static Action* mc_leave_firesworn(PlayerbotAI* botAI) { return new RcMcLeaveFireswornAction(botAI); }

    static Action* bwl_disarm_suppression(PlayerbotAI* botAI) { return new RcBwlDisarmSuppressionAction(botAI); }
    static Action* bwl_broodlord_move_out(PlayerbotAI* botAI)
    {
        return new RcBwlKeepOutAction(botAI, "rc bwl broodlord move out", "broodlord lashlayer",
                                      RaidClear::BlackwingLair::BROODLORD_RANGED_TARGET);
    }
    static Action* bwl_ebonroc_taunt(PlayerbotAI* botAI) { return new RcBwlEbonrocTauntAction(botAI); }
    static Action* bwl_technician_spread(PlayerbotAI* botAI) { return new RcBwlTechnicianSpreadAction(botAI); }
    static Action* bwl_firemaw_hide(PlayerbotAI* botAI) { return new RcBwlFiremawHideAction(botAI); }
    static Action* bwl_chromaggus_hide(PlayerbotAI* botAI) { return new RcBwlChromaggusHideAction(botAI); }
    static Action* bwl_nefarian_move_out(PlayerbotAI* botAI)
    {
        return new RcBwlKeepOutAction(botAI, "rc bwl nefarian move out", "nefarian",
                                      RaidClear::BlackwingLair::NEFARIAN_RANGED_TARGET);
    }
    static Action* bwl_detonation_keep_away(PlayerbotAI* botAI) { return new RcBwlDetonationKeepAwayAction(botAI); }
    static Action* bwl_seether_tranq(PlayerbotAI* botAI) { return new RcBwlSeetherTranqAction(botAI); }
    static Action* bwl_remove_black_affliction(PlayerbotAI* botAI) { return new RcBwlBlackAfflictionAction(botAI); }
    static Action* bwl_fire_nova_totem_move_away(PlayerbotAI* botAI) { return new RcBwlFireNovaTotemMoveAwayAction(botAI); }
    static Action* bwl_captain_hand_off(PlayerbotAI* botAI) { return new RcBwlCaptainHandOffAction(botAI); }

    static Action* zg_holy_wrath_keep_away(PlayerbotAI* botAI) { return new RcZgHolyWrathKeepAwayAction(botAI); }

    static Action* ony_leave_deep_breath(PlayerbotAI* botAI) { return new RcOnyLeaveDeepBreathAction(botAI); }
};

class RaidClearTriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidClearTriggerContext()
    {
        creators["rc kill order"] = &RaidClearTriggerContext::kill_order;
        creators["rc offtank target"] = &RaidClearTriggerContext::offtank_target;
        creators["rc offtank separate"] = &RaidClearTriggerContext::offtank_separate;
        creators["rc tank share threat"] = &RaidClearTriggerContext::tank_share_threat;
        creators["rc aggro leash"] = &RaidClearTriggerContext::aggro_leash;

        creators["rc mc magmadar tremor"] = &RaidClearTriggerContext::mc_magmadar_tremor;
        creators["rc mc damage reflection"] = &RaidClearTriggerContext::mc_damage_reflection;
        creators["rc mc ragnaros ranged"] = &RaidClearTriggerContext::mc_ragnaros_ranged;
        creators["rc mc garr gather"] = &RaidClearTriggerContext::mc_garr_gather;
        creators["rc mc anxious firesworn"] = &RaidClearTriggerContext::mc_anxious_firesworn;

        creators["rc bwl suppression device"] = &RaidClearTriggerContext::bwl_suppression_device;
        creators["rc bwl broodlord ranged"] = &RaidClearTriggerContext::bwl_broodlord_ranged;
        creators["rc bwl ebonroc taunt"] = &RaidClearTriggerContext::bwl_ebonroc_taunt;
        creators["rc bwl technician spread"] = &RaidClearTriggerContext::bwl_technician_spread;
        creators["rc bwl firemaw hide"] = &RaidClearTriggerContext::bwl_firemaw_hide;
        creators["rc bwl chromaggus breath"] = &RaidClearTriggerContext::bwl_chromaggus_breath;
        creators["rc bwl nefarian ranged"] = &RaidClearTriggerContext::bwl_nefarian_ranged;
        creators["rc bwl detonation keep away"] = &RaidClearTriggerContext::bwl_detonation_keep_away;
        creators["rc bwl seether tranq"] = &RaidClearTriggerContext::bwl_seether_tranq;
        creators["rc bwl black affliction"] = &RaidClearTriggerContext::bwl_black_affliction;
        creators["rc bwl fire nova totem"] = &RaidClearTriggerContext::bwl_fire_nova_totem;
        creators["rc bwl captain hand off"] = &RaidClearTriggerContext::bwl_captain_hand_off;

        creators["rc zg holy wrath keep away"] = &RaidClearTriggerContext::zg_holy_wrath_keep_away;

        creators["rc ony deep breath"] = &RaidClearTriggerContext::ony_deep_breath;
    }

private:
    static Trigger* kill_order(PlayerbotAI* botAI) { return new RcKillOrderTrigger(botAI); }
    static Trigger* offtank_target(PlayerbotAI* botAI) { return new RcOffTankTargetTrigger(botAI); }
    static Trigger* offtank_separate(PlayerbotAI* botAI) { return new RcOffTankSeparateTrigger(botAI); }
    static Trigger* tank_share_threat(PlayerbotAI* botAI) { return new RcTankShareThreatTrigger(botAI); }
    static Trigger* aggro_leash(PlayerbotAI* botAI) { return new RcAggroLeashTrigger(botAI); }

    static Trigger* mc_magmadar_tremor(PlayerbotAI* botAI) { return new RcMcMagmadarTremorTrigger(botAI); }
    static Trigger* mc_damage_reflection(PlayerbotAI* botAI) { return new RcMcDamageReflectionTrigger(botAI); }
    static Trigger* mc_ragnaros_ranged(PlayerbotAI* botAI) { return new RcMcRagnarosRangedTrigger(botAI); }
    static Trigger* mc_garr_gather(PlayerbotAI* botAI) { return new RcMcGarrGatherTrigger(botAI); }
    static Trigger* mc_anxious_firesworn(PlayerbotAI* botAI) { return new RcMcAnxiousFireswornTrigger(botAI); }

    static Trigger* bwl_suppression_device(PlayerbotAI* botAI) { return new RcBwlSuppressionDeviceTrigger(botAI); }
    static Trigger* bwl_broodlord_ranged(PlayerbotAI* botAI)
    {
        return new RcBwlKeepOutTrigger(botAI, "rc bwl broodlord ranged", "broodlord lashlayer",
                                       RaidClear::BlackwingLair::BROODLORD_RANGED_MIN,
                                       RaidClear::BlackwingLair::BROODLORD_RANGED_TARGET);
    }
    static Trigger* bwl_ebonroc_taunt(PlayerbotAI* botAI) { return new RcBwlEbonrocTauntTrigger(botAI); }
    static Trigger* bwl_technician_spread(PlayerbotAI* botAI) { return new RcBwlTechnicianSpreadTrigger(botAI); }
    static Trigger* bwl_firemaw_hide(PlayerbotAI* botAI) { return new RcBwlFiremawHideTrigger(botAI); }
    static Trigger* bwl_chromaggus_breath(PlayerbotAI* botAI) { return new RcBwlChromaggusBreathTrigger(botAI); }
    static Trigger* bwl_nefarian_ranged(PlayerbotAI* botAI)
    {
        return new RcBwlKeepOutTrigger(botAI, "rc bwl nefarian ranged", "nefarian",
                                       RaidClear::BlackwingLair::NEFARIAN_RANGED_MIN,
                                       RaidClear::BlackwingLair::NEFARIAN_RANGED_TARGET);
    }
    static Trigger* bwl_detonation_keep_away(PlayerbotAI* botAI) { return new RcBwlDetonationKeepAwayTrigger(botAI); }
    static Trigger* bwl_seether_tranq(PlayerbotAI* botAI) { return new RcBwlSeetherTranqTrigger(botAI); }
    static Trigger* bwl_black_affliction(PlayerbotAI* botAI) { return new RcBwlBlackAfflictionTrigger(botAI); }
    static Trigger* bwl_fire_nova_totem(PlayerbotAI* botAI) { return new RcBwlFireNovaTotemTrigger(botAI); }
    static Trigger* bwl_captain_hand_off(PlayerbotAI* botAI) { return new RcBwlCaptainHandOffTrigger(botAI); }

    static Trigger* zg_holy_wrath_keep_away(PlayerbotAI* botAI) { return new RcZgHolyWrathKeepAwayTrigger(botAI); }

    static Trigger* ony_deep_breath(PlayerbotAI* botAI) { return new RcOnyDeepBreathTrigger(botAI); }
};

#endif
