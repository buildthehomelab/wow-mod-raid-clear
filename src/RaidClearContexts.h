/*
 * mod-raid-clear: the strategy, action and trigger names this module adds to mod-playerbots.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_CONTEXTS_H
#define MOD_RAID_CLEAR_CONTEXTS_H

#include "Common/AggroLeash.h"
#include "Common/KillOrder.h"
#include "Common/Knockback.h"
#include "Common/RaidDiscipline.h"
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
        creators["rc raid discipline"] = &RaidClearStrategyContext::raid_discipline;
        creators["rc moltencore"] = &RaidClearStrategyContext::moltencore;
        creators["rc bwl"] = &RaidClearStrategyContext::bwl;
        creators["rc bwl runners"] = &RaidClearStrategyContext::bwl_runners;
        creators["rc zg"] = &RaidClearStrategyContext::zg;
        creators["rc ony"] = &RaidClearStrategyContext::ony;
    }

private:
    static Strategy* raid_tanks(PlayerbotAI* botAI) { return new RaidClearTanksStrategy(botAI); }
    static Strategy* raid_leash(PlayerbotAI* botAI) { return new RaidClearLeashStrategy(botAI); }
    static Strategy* raid_discipline(PlayerbotAI* botAI) { return new RaidClearDisciplineStrategy(botAI); }
    static Strategy* moltencore(PlayerbotAI* botAI) { return new RaidClearMoltenCoreStrategy(botAI); }
    static Strategy* bwl(PlayerbotAI* botAI) { return new RaidClearBlackwingLairStrategy(botAI); }
    static Strategy* bwl_runners(PlayerbotAI* botAI) { return new RaidClearBwlRunnersStrategy(botAI); }
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
        creators["rc protect boss victim"] = &RaidClearActionContext::protect_boss_victim;
        creators["rc salvation on player"] = &RaidClearActionContext::salvation_on_player;
        creators["rc aggro leash"] = &RaidClearActionContext::aggro_leash;
        creators["rc knockback footing"] = &RaidClearActionContext::knockback_footing;
        creators["rc leave unpulled target"] = &RaidClearActionContext::leave_unpulled_target;

        creators["rc mc tremor totem"] = &RaidClearActionContext::mc_tremor_totem;
        creators["rc mc stop melee"] = &RaidClearActionContext::mc_stop_melee;
        creators["rc mc ragnaros move out"] = &RaidClearActionContext::mc_ragnaros_move_out;
        creators["rc mc garr gather"] = &RaidClearActionContext::mc_garr_gather;
        creators["rc mc leave firesworn"] = &RaidClearActionContext::mc_leave_firesworn;
        creators["rc mc leave inferno"] = &RaidClearActionContext::mc_leave_inferno;
        creators["rc mc leave hot coal"] = &RaidClearActionContext::mc_leave_hot_coal;
        creators["rc mc carry living bomb"] = &RaidClearActionContext::mc_carry_living_bomb;

        creators["rc bwl disarm suppression"] = &RaidClearActionContext::bwl_disarm_suppression;
        creators["rc bwl broodlord move out"] = &RaidClearActionContext::bwl_broodlord_move_out;
        creators["rc bwl ebonroc taunt"] = &RaidClearActionContext::bwl_ebonroc_taunt;
        creators["rc bwl technician spread"] = &RaidClearActionContext::bwl_technician_spread;
        creators["rc bwl catch technician"] = &RaidClearActionContext::bwl_catch_technician;
        creators["rc bwl firemaw hide"] = &RaidClearActionContext::bwl_firemaw_hide;
        creators["rc bwl chromaggus hide"] = &RaidClearActionContext::bwl_chromaggus_hide;
        creators["rc bwl dragon move"] = &RaidClearActionContext::bwl_dragon_move;
        creators["rc bwl nefarian fear ward"] = &RaidClearActionContext::bwl_nefarian_fear_ward;
        creators["rc bwl wyrmguard keep away"] = &RaidClearActionContext::bwl_wyrmguard_keep_away;
        creators["rc bwl drake front"] = &RaidClearActionContext::bwl_drake_front;
        creators["rc bwl nefarian turn"] = &RaidClearActionContext::bwl_nefarian_turn;
        creators["rc bwl construct gather"] = &RaidClearActionContext::bwl_construct_gather;
        creators["rc bwl detonation keep away"] = &RaidClearActionContext::bwl_detonation_keep_away;
        creators["rc bwl tranq"] = &RaidClearActionContext::bwl_tranq;
        creators["rc bwl remove black affliction"] = &RaidClearActionContext::bwl_remove_black_affliction;
        creators["rc bwl fire nova totem move away"] = &RaidClearActionContext::bwl_fire_nova_totem_move_away;
        creators["rc bwl captain hand off"] = &RaidClearActionContext::bwl_captain_hand_off;

        creators["rc zg holy wrath keep away"] = &RaidClearActionContext::zg_holy_wrath_keep_away;
        creators["rc zg thekal even kill"] = &RaidClearActionContext::zg_thekal_even_kill;
        creators["rc zg lorkhan interrupt"] = &RaidClearActionContext::zg_lorkhan_interrupt;
        creators["rc zg leave bad target"] = &RaidClearActionContext::zg_leave_bad_target;
        creators["rc zg concoction move away"] = &RaidClearActionContext::zg_concoction_move_away;
        creators["rc zg leave liquid fire"] = &RaidClearActionContext::zg_leave_liquid_fire;
        creators["rc zg control charmed"] = &RaidClearActionContext::zg_control_charmed;

        creators["rc ony leave deep breath"] = &RaidClearActionContext::ony_leave_deep_breath;
        creators["rc ony offtank position"] = &RaidClearActionContext::ony_offtank_position;
        creators["rc ony leave evading target"] = &RaidClearActionContext::ony_leave_evading_target;
        creators["rc ony kill whelps"] = &RaidClearActionContext::ony_kill_whelps;
        creators["rc ony spread out"] = &RaidClearActionContext::ony_spread_out;
        creators["rc ony leave cleave"] = &RaidClearActionContext::ony_leave_cleave;
    }

private:
    static Action* mark_kill_order(PlayerbotAI* botAI) { return new RcMarkKillOrderAction(botAI); }
    static Action* offtank_target(PlayerbotAI* botAI) { return new RcOffTankTargetAction(botAI); }
    static Action* offtank_separate(PlayerbotAI* botAI) { return new RcOffTankSeparateAction(botAI); }
    static Action* tank_share_threat(PlayerbotAI* botAI) { return new RcTankShareThreatAction(botAI); }
    static Action* protect_boss_victim(PlayerbotAI* botAI) { return new RcProtectBossVictimAction(botAI); }
    static Action* salvation_on_player(PlayerbotAI* botAI) { return new RcSalvationOnPlayerAction(botAI); }
    static Action* aggro_leash(PlayerbotAI* botAI) { return new RcAggroLeashAction(botAI); }
    static Action* knockback_footing(PlayerbotAI* botAI) { return new RcKnockbackFootingAction(botAI); }
    static Action* leave_unpulled_target(PlayerbotAI* botAI) { return new RcLeaveUnpulledTargetAction(botAI); }

    static Action* mc_tremor_totem(PlayerbotAI* botAI) { return new RcMcTremorTotemAction(botAI); }
    static Action* mc_stop_melee(PlayerbotAI* botAI) { return new RcMcStopMeleeAction(botAI); }
    static Action* mc_ragnaros_move_out(PlayerbotAI* botAI) { return new RcMcRagnarosMoveOutAction(botAI); }
    static Action* mc_garr_gather(PlayerbotAI* botAI) { return new RcMcGarrGatherAction(botAI); }
    static Action* mc_leave_firesworn(PlayerbotAI* botAI) { return new RcMcLeaveFireswornAction(botAI); }
    static Action* mc_leave_inferno(PlayerbotAI* botAI) { return new RcMcLeaveInfernoAction(botAI); }
    static Action* mc_leave_hot_coal(PlayerbotAI* botAI) { return new RcMcLeaveHotCoalAction(botAI); }
    static Action* mc_carry_living_bomb(PlayerbotAI* botAI) { return new RcMcCarryLivingBombAction(botAI); }

    static Action* bwl_disarm_suppression(PlayerbotAI* botAI) { return new RcBwlDisarmSuppressionAction(botAI); }
    static Action* bwl_broodlord_move_out(PlayerbotAI* botAI)
    {
        return new RcBwlKeepOutAction(botAI, "rc bwl broodlord move out", RaidClear::BlackwingLair::NPC_BROODLORD,
                                      RaidClear::BlackwingLair::BROODLORD_RANGED_TARGET);
    }
    static Action* bwl_ebonroc_taunt(PlayerbotAI* botAI) { return new RcBwlEbonrocTauntAction(botAI); }
    static Action* bwl_technician_spread(PlayerbotAI* botAI) { return new RcBwlTechnicianSpreadAction(botAI); }
    static Action* bwl_catch_technician(PlayerbotAI* botAI) { return new RcBwlCatchTechnicianAction(botAI); }
    static Action* bwl_firemaw_hide(PlayerbotAI* botAI) { return new RcBwlFiremawHideAction(botAI); }
    static Action* bwl_chromaggus_hide(PlayerbotAI* botAI) { return new RcBwlChromaggusHideAction(botAI); }
    static Action* bwl_dragon_move(PlayerbotAI* botAI) { return new RcBwlDragonMoveAction(botAI); }
    static Action* bwl_nefarian_fear_ward(PlayerbotAI* botAI) { return new RcBwlFearWardAction(botAI); }
    static Action* bwl_wyrmguard_keep_away(PlayerbotAI* botAI) { return new RcBwlWyrmguardKeepAwayAction(botAI); }
    static Action* bwl_drake_front(PlayerbotAI* botAI) { return new RcBwlDrakeFrontAction(botAI); }
    static Action* bwl_nefarian_turn(PlayerbotAI* botAI) { return new RcBwlNefarianTurnAction(botAI); }
    static Action* bwl_construct_gather(PlayerbotAI* botAI) { return new RcBwlConstructGatherAction(botAI); }
    static Action* bwl_detonation_keep_away(PlayerbotAI* botAI) { return new RcBwlDetonationKeepAwayAction(botAI); }
    static Action* bwl_tranq(PlayerbotAI* botAI) { return new RcBwlTranqAction(botAI); }
    static Action* bwl_remove_black_affliction(PlayerbotAI* botAI) { return new RcBwlBlackAfflictionAction(botAI); }
    static Action* bwl_fire_nova_totem_move_away(PlayerbotAI* botAI) { return new RcBwlFireNovaTotemMoveAwayAction(botAI); }
    static Action* bwl_captain_hand_off(PlayerbotAI* botAI) { return new RcBwlCaptainHandOffAction(botAI); }

    static Action* zg_holy_wrath_keep_away(PlayerbotAI* botAI) { return new RcZgHolyWrathKeepAwayAction(botAI); }
    static Action* zg_thekal_even_kill(PlayerbotAI* botAI) { return new RcZgThekalEvenKillAction(botAI); }
    static Action* zg_lorkhan_interrupt(PlayerbotAI* botAI) { return new RcZgLorkhanInterruptAction(botAI); }
    static Action* zg_leave_bad_target(PlayerbotAI* botAI) { return new RcZgLeaveBadTargetAction(botAI); }
    static Action* zg_concoction_move_away(PlayerbotAI* botAI) { return new RcZgConcoctionMoveAwayAction(botAI); }
    static Action* zg_leave_liquid_fire(PlayerbotAI* botAI) { return new RcZgLeaveLiquidFireAction(botAI); }
    static Action* zg_control_charmed(PlayerbotAI* botAI) { return new RcZgControlCharmedAction(botAI); }

    static Action* ony_leave_deep_breath(PlayerbotAI* botAI) { return new RcOnyLeaveDeepBreathAction(botAI); }
    static Action* ony_offtank_position(PlayerbotAI* botAI) { return new RcOnyOffTankPositionAction(botAI); }
    static Action* ony_leave_evading_target(PlayerbotAI* botAI) { return new RcOnyLeaveEvadingTargetAction(botAI); }
    static Action* ony_kill_whelps(PlayerbotAI* botAI) { return new RcOnyKillWhelpsAction(botAI); }
    static Action* ony_spread_out(PlayerbotAI* botAI) { return new RcOnySpreadOutAction(botAI); }
    static Action* ony_leave_cleave(PlayerbotAI* botAI) { return new RcOnyLeaveCleaveAction(botAI); }
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
        creators["rc boss on non tank"] = &RaidClearTriggerContext::boss_on_non_tank;
        creators["rc player over threat"] = &RaidClearTriggerContext::player_over_threat;
        creators["rc aggro leash"] = &RaidClearTriggerContext::aggro_leash;
        creators["rc knockback footing"] = &RaidClearTriggerContext::knockback_footing;
        creators["rc knockback footing tank"] = &RaidClearTriggerContext::knockback_footing_tank;
        creators["rc unpulled target"] = &RaidClearTriggerContext::unpulled_target;

        creators["rc mc magmadar tremor"] = &RaidClearTriggerContext::mc_magmadar_tremor;
        creators["rc mc damage reflection"] = &RaidClearTriggerContext::mc_damage_reflection;
        creators["rc mc ragnaros ranged"] = &RaidClearTriggerContext::mc_ragnaros_ranged;
        creators["rc mc garr gather"] = &RaidClearTriggerContext::mc_garr_gather;
        creators["rc mc anxious firesworn"] = &RaidClearTriggerContext::mc_anxious_firesworn;
        creators["rc mc inferno"] = &RaidClearTriggerContext::mc_inferno;
        creators["rc mc hot coal"] = &RaidClearTriggerContext::mc_hot_coal;
        creators["rc mc living bomb"] = &RaidClearTriggerContext::mc_living_bomb;

        creators["rc bwl suppression device"] = &RaidClearTriggerContext::bwl_suppression_device;
        creators["rc bwl broodlord ranged"] = &RaidClearTriggerContext::bwl_broodlord_ranged;
        creators["rc bwl ebonroc taunt"] = &RaidClearTriggerContext::bwl_ebonroc_taunt;
        creators["rc bwl technician spread"] = &RaidClearTriggerContext::bwl_technician_spread;
        creators["rc bwl fleeing technician"] = &RaidClearTriggerContext::bwl_fleeing_technician;
        creators["rc bwl firemaw hide"] = &RaidClearTriggerContext::bwl_firemaw_hide;
        creators["rc bwl chromaggus breath"] = &RaidClearTriggerContext::bwl_chromaggus_breath;
        creators["rc bwl dragon position"] = &RaidClearTriggerContext::bwl_dragon_position;
        creators["rc bwl dragon breath"] = &RaidClearTriggerContext::bwl_dragon_breath;
        creators["rc bwl nefarian tremor"] = &RaidClearTriggerContext::bwl_nefarian_tremor;
        creators["rc bwl nefarian fear ward"] = &RaidClearTriggerContext::bwl_nefarian_fear_ward;
        creators["rc bwl wyrmguard keep away"] = &RaidClearTriggerContext::bwl_wyrmguard_keep_away;
        creators["rc bwl drake front"] = &RaidClearTriggerContext::bwl_drake_front;
        creators["rc bwl nefarian rogue call"] = &RaidClearTriggerContext::bwl_nefarian_rogue_call;
        creators["rc bwl construct gather"] = &RaidClearTriggerContext::bwl_construct_gather;
        creators["rc bwl detonation keep away"] = &RaidClearTriggerContext::bwl_detonation_keep_away;
        creators["rc bwl tranq"] = &RaidClearTriggerContext::bwl_tranq;
        creators["rc bwl black affliction"] = &RaidClearTriggerContext::bwl_black_affliction;
        creators["rc bwl fire nova totem"] = &RaidClearTriggerContext::bwl_fire_nova_totem;
        creators["rc bwl captain hand off"] = &RaidClearTriggerContext::bwl_captain_hand_off;

        creators["rc zg holy wrath keep away"] = &RaidClearTriggerContext::zg_holy_wrath_keep_away;
        creators["rc zg thekal even kill"] = &RaidClearTriggerContext::zg_thekal_even_kill;
        creators["rc zg lorkhan heal"] = &RaidClearTriggerContext::zg_lorkhan_heal;
        creators["rc zg bad target"] = &RaidClearTriggerContext::zg_bad_target;
        creators["rc zg concoction"] = &RaidClearTriggerContext::zg_concoction;
        creators["rc zg liquid fire"] = &RaidClearTriggerContext::zg_liquid_fire;
        creators["rc zg charmed member"] = &RaidClearTriggerContext::zg_charmed_member;

        creators["rc ony deep breath"] = &RaidClearTriggerContext::ony_deep_breath;
        creators["rc ony offtank position"] = &RaidClearTriggerContext::ony_offtank_position;
        creators["rc ony evading target"] = &RaidClearTriggerContext::ony_evading_target;
        creators["rc ony spread"] = &RaidClearTriggerContext::ony_spread;
        creators["rc ony cleave"] = &RaidClearTriggerContext::ony_cleave;
    }

private:
    static Trigger* kill_order(PlayerbotAI* botAI) { return new RcKillOrderTrigger(botAI); }
    static Trigger* offtank_target(PlayerbotAI* botAI) { return new RcOffTankTargetTrigger(botAI); }
    static Trigger* offtank_separate(PlayerbotAI* botAI) { return new RcOffTankSeparateTrigger(botAI); }
    static Trigger* tank_share_threat(PlayerbotAI* botAI) { return new RcTankShareThreatTrigger(botAI); }
    static Trigger* boss_on_non_tank(PlayerbotAI* botAI) { return new RcBossOnNonTankTrigger(botAI); }
    static Trigger* player_over_threat(PlayerbotAI* botAI) { return new RcPlayerOverThreatTrigger(botAI); }
    static Trigger* aggro_leash(PlayerbotAI* botAI) { return new RcAggroLeashTrigger(botAI); }
    static Trigger* knockback_footing(PlayerbotAI* botAI)
    {
        return new RcKnockbackFootingTrigger(botAI, "rc knockback footing", false);
    }
    static Trigger* knockback_footing_tank(PlayerbotAI* botAI)
    {
        return new RcKnockbackFootingTrigger(botAI, "rc knockback footing tank", true);
    }
    static Trigger* unpulled_target(PlayerbotAI* botAI) { return new RcUnpulledTargetTrigger(botAI); }

    static Trigger* mc_magmadar_tremor(PlayerbotAI* botAI) { return new RcMcMagmadarTremorTrigger(botAI); }
    static Trigger* mc_damage_reflection(PlayerbotAI* botAI) { return new RcMcDamageReflectionTrigger(botAI); }
    static Trigger* mc_ragnaros_ranged(PlayerbotAI* botAI) { return new RcMcRagnarosRangedTrigger(botAI); }
    static Trigger* mc_garr_gather(PlayerbotAI* botAI) { return new RcMcGarrGatherTrigger(botAI); }
    static Trigger* mc_anxious_firesworn(PlayerbotAI* botAI) { return new RcMcAnxiousFireswornTrigger(botAI); }
    static Trigger* mc_inferno(PlayerbotAI* botAI) { return new RcMcInfernoTrigger(botAI); }
    static Trigger* mc_hot_coal(PlayerbotAI* botAI) { return new RcMcHotCoalTrigger(botAI); }
    static Trigger* mc_living_bomb(PlayerbotAI* botAI) { return new RcMcLivingBombTrigger(botAI); }

    static Trigger* bwl_suppression_device(PlayerbotAI* botAI) { return new RcBwlSuppressionDeviceTrigger(botAI); }
    static Trigger* bwl_broodlord_ranged(PlayerbotAI* botAI)
    {
        return new RcBwlKeepOutTrigger(botAI, "rc bwl broodlord ranged", RaidClear::BlackwingLair::NPC_BROODLORD,
                                       RaidClear::BlackwingLair::BROODLORD_RANGED_MIN,
                                       RaidClear::BlackwingLair::BROODLORD_RANGED_TARGET);
    }
    static Trigger* bwl_ebonroc_taunt(PlayerbotAI* botAI) { return new RcBwlEbonrocTauntTrigger(botAI); }
    static Trigger* bwl_technician_spread(PlayerbotAI* botAI) { return new RcBwlTechnicianSpreadTrigger(botAI); }
    static Trigger* bwl_fleeing_technician(PlayerbotAI* botAI) { return new RcBwlFleeingTechnicianTrigger(botAI); }
    static Trigger* bwl_firemaw_hide(PlayerbotAI* botAI) { return new RcBwlFiremawHideTrigger(botAI); }
    static Trigger* bwl_chromaggus_breath(PlayerbotAI* botAI) { return new RcBwlChromaggusBreathTrigger(botAI); }
    static Trigger* bwl_dragon_position(PlayerbotAI* botAI) { return new RcBwlDragonPositionTrigger(botAI); }
    static Trigger* bwl_dragon_breath(PlayerbotAI* botAI) { return new RcBwlDragonBreathTrigger(botAI); }
    static Trigger* bwl_nefarian_tremor(PlayerbotAI* botAI) { return new RcBwlNefarianTremorTrigger(botAI); }
    static Trigger* bwl_nefarian_fear_ward(PlayerbotAI* botAI) { return new RcBwlFearWardTrigger(botAI); }
    static Trigger* bwl_wyrmguard_keep_away(PlayerbotAI* botAI) { return new RcBwlWyrmguardKeepAwayTrigger(botAI); }
    static Trigger* bwl_drake_front(PlayerbotAI* botAI) { return new RcBwlDrakeFrontTrigger(botAI); }
    static Trigger* bwl_nefarian_rogue_call(PlayerbotAI* botAI) { return new RcBwlNefarianRogueCallTrigger(botAI); }
    static Trigger* bwl_construct_gather(PlayerbotAI* botAI) { return new RcBwlConstructGatherTrigger(botAI); }
    static Trigger* bwl_detonation_keep_away(PlayerbotAI* botAI) { return new RcBwlDetonationKeepAwayTrigger(botAI); }
    static Trigger* bwl_tranq(PlayerbotAI* botAI) { return new RcBwlTranqTrigger(botAI); }
    static Trigger* bwl_black_affliction(PlayerbotAI* botAI) { return new RcBwlBlackAfflictionTrigger(botAI); }
    static Trigger* bwl_fire_nova_totem(PlayerbotAI* botAI) { return new RcBwlFireNovaTotemTrigger(botAI); }
    static Trigger* bwl_captain_hand_off(PlayerbotAI* botAI) { return new RcBwlCaptainHandOffTrigger(botAI); }

    static Trigger* zg_holy_wrath_keep_away(PlayerbotAI* botAI) { return new RcZgHolyWrathKeepAwayTrigger(botAI); }
    static Trigger* zg_thekal_even_kill(PlayerbotAI* botAI) { return new RcZgThekalEvenKillTrigger(botAI); }
    static Trigger* zg_lorkhan_heal(PlayerbotAI* botAI) { return new RcZgLorkhanHealTrigger(botAI); }
    static Trigger* zg_bad_target(PlayerbotAI* botAI) { return new RcZgBadTargetTrigger(botAI); }
    static Trigger* zg_concoction(PlayerbotAI* botAI) { return new RcZgConcoctionTrigger(botAI); }
    static Trigger* zg_liquid_fire(PlayerbotAI* botAI) { return new RcZgLiquidFireTrigger(botAI); }
    static Trigger* zg_charmed_member(PlayerbotAI* botAI) { return new RcZgCharmedMemberTrigger(botAI); }

    static Trigger* ony_deep_breath(PlayerbotAI* botAI) { return new RcOnyDeepBreathTrigger(botAI); }
    static Trigger* ony_offtank_position(PlayerbotAI* botAI) { return new RcOnyOffTankPositionTrigger(botAI); }
    static Trigger* ony_evading_target(PlayerbotAI* botAI) { return new RcOnyEvadingTargetTrigger(botAI); }
    static Trigger* ony_spread(PlayerbotAI* botAI) { return new RcOnySpreadTrigger(botAI); }
    static Trigger* ony_cleave(PlayerbotAI* botAI) { return new RcOnyCleaveTrigger(botAI); }
};

#endif
