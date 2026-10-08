/*
 * mod-raid-clear: Blackwing Lair (map 469).
 *
 * Runs next to mod-playerbots' "bwl" strategy, which already handles the Onyxia Scale Cloak,
 * Razorgore's cone and egg phase, Vaelastrasz' Burning Adrenaline, Chromaggus' Bronze affliction,
 * Nefarian's Wild Magic and Fear Ward, Wyrmguard spacing, the fire resistance auras/totems, and
 * disarms Suppression Devices within 15 yd.
 *
 * Added here (mechanics from the core scripts in scripts/EasternKingdoms/BlackrockMountain/
 * BlackwingLair and the trash SmartAI):
 *   - Kill order: Blackwing Warlocks first. Each casts Demon Portal every 30-45s and every portal
 *     summons an Enraged Felguard every 30s until its warlock dies (which despawns its portals),
 *     so the pack only shrinks once the warlocks are dead. Then the casters and elites that heal,
 *     polymorph or buff: Taskmasters, Spellbinders, Hatchers.
 *   - Suppression Devices: Suppression Aura reaches 20 yd, playerbots' disarm only 15, so a bot
 *     standing 15-20 yd from an armed device stays slowed. With the "raid" bot cheat on, bots in
 *     the Suppression Room turn off every armed device within 22 yd.
 *   - Broodlord, Firemaw, Ebonroc, Flamegor: Knock Away (-50%) and Wing Buffet (-75%) cut the
 *     main tank's threat, so the off-tank builds threat on the boss instead of standing idle and
 *     catches it (the tank strategies taunt it back). The Suppression Room whelps don't pull the
 *     off-tank away from Broodlord.
 *   - Broodlord: ranged and healers stay out of Blast Wave (20 yd around him).
 *   - Ebonroc: the off-tank taunts him off a tank with Shadow of Ebonroc (he heals on every hit on
 *     it), and that tank doesn't taunt him back while it lasts.
 *   - Vaelastrasz is taunt immune, so the off-tank keeps itself second on threat for when Burning
 *     Adrenaline kills the main tank. Razorgore's tanks are placed by playerbots and
 *     mod-dungeon-clear, so the tank split stays out of that fight.
 *   - Firemaw: Flame Buffet (23341) stacks on everyone in his line of sight every 5s. A non-tank
 *     at 5 stacks hides behind cover until it drops off.
 *   - Chromaggus: non-tanks duck behind cover while he casts a breath, except Time Lapse, which
 *     everyone should take because it halves the threat of everyone it hits, tank included.
 *     Brood Affliction: Black (a curse that makes its target take more fire damage, which
 *     Incinerate and Ignite Flesh deal) is removed before anything else the curse removers would
 *     do: mages, druids and Cleanse Spirit shamans split the cursed players between them, tanks
 *     first, then the lowest on health. Healers leave it while a tank is below half health.
 *   - Nefarian: ranged and healers stay out of Bellowing Roar (35 yd fear).
 *     Class calls: priests stop healing while Corrupted Healing is on them (every heal would put
 *     a shadow DoT on its target; shields still go out, and a tank below 35% still gets healed);
 *     the shaman call's corrupted Healing Stream totem dies first, then Stoneskin and Windfury,
 *     and non-tanks walk 13 yd away from the Fire Nova totem before it goes off; the warlock
 *     call's Infernals die with the drakonids.
 *   - A bot revived in Blackwing Lair gets the Onyxia Scale Cloak aura back at once (playerbots
 *     only re-adds it on a random check every few seconds, and Shadow Flame kills without it).
 *   - Razorgore's and Nefarian's adds die in order: dragonkin first, then casters, then melee;
 *     Drakonids and Bone Constructs before Nefarian.
 *   - Technician packs: Blackwing Technicians throw Bomb (22334, 5 yd splash) at random raiders
 *     within 30 yd, so ranged and healers keep 6 yd from each other while one is fighting nearby.
 *     Only bots in a clump move, one short step at a time.
 *   - Death Talon packs (Hall of the Dragonspawn: a Captain, two Seethers, two Wyrmkin and a
 *     Flamescale each). The Captain puts Mark of Detonation (22438, 30s, magic) on whoever he
 *     hits; every melee hit on that player then explodes (22439) for 657-844 fire on all of that
 *     player's allies within 30 yd. So the Captain gets an off-tank of his own: the main tank
 *     leaves him alone (and drops him if it had him), an off-tank taunts him off whoever has him
 *     and drags him 36 yd from the main tank's mob, and keeps him until he dies. Every other
 *     non-tank stays 32 yd away from anyone carrying the Mark, moving out toward the main tank,
 *     and doesn't walk back toward a target inside that circle. He dies last, mostly to ranged.
 *     With more Captains than free off-tanks, the main tank keeps the extra one. Wyrmkin die
 *     first (Fireball Volley hits the whole raid within 45 yd), then Flamescales and Seethers;
 *     the nearest hunter Tranquilizing Shots an enraged Seether (22428: +100% attack speed).
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_BLACKWING_LAIR_H
#define MOD_RAID_CLEAR_BLACKWING_LAIR_H

#include "Common/KillOrder.h"
#include "Common/TankRoles.h"

#include "Action.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "Strategy.h"
#include "Trigger.h"

#include <vector>

namespace RaidClear::BlackwingLair
{
    constexpr uint32 MAP_ID = 469;

    enum Creatures : uint32
    {
        NPC_BROODLORD             = 12017,
        NPC_FIREMAW               = 11983,
        NPC_EBONROC               = 14601,
        NPC_FLAMEGOR              = 11981,

        NPC_BLACKWING_SPELLBINDER = 12457,  // Greater Polymorph, Flamestrike
        NPC_BLACKWING_TASKMASTER  = 12458,  // Shadow Shock, Healing Circle
        NPC_BLACKWING_WARLOCK     = 12459,  // Demon Portal
        NPC_DEATH_TALON_CAPTAIN   = 12467,  // Commanding Shout, Mark of Detonation
        NPC_DEATH_TALON_HATCHER   = 12468,  // Suppression Room elites
        NPC_BLACKWING_TECHNICIAN  = 13996,  // Bomb
        NPC_DEATH_TALON_WYRMGUARD = 12460,  // War Stomp, 15 yd
        NPC_DEATH_TALON_WYRMKIN   = 12465,  // Fireball Volley, Blast Wave
        NPC_DEATH_TALON_SEETHER   = 12464,  // Enrage, Flame Buffet
        NPC_DEATH_TALON_FLAMESCALE = 12463, // Flame Shock, Berserker Charge

        NPC_RAZORGORE             = 12435,
        NPC_VAELASTRASZ           = 13020,
        NPC_CHROMAGGUS            = 14020,
        NPC_NEFARIAN              = 11583,

        NPC_BLACKWING_LEGIONNAIRE = 12416,  // Razorgore adds
        NPC_BLACKWING_MAGE        = 12420,
        NPC_DEATH_TALON_DRAGONSPAWN = 12422,

        NPC_BLUE_DRAKONID         = 14261,  // Nefarian adds
        NPC_GREEN_DRAKONID        = 14262,
        NPC_BRONZE_DRAKONID       = 14263,
        NPC_RED_DRAKONID          = 14264,
        NPC_BLACK_DRAKONID        = 14265,
        NPC_CHROMATIC_DRAKONID    = 14302,
        NPC_BONE_CONSTRUCT        = 14605,
        NPC_ENRAGED_FELGUARD      = 14101,  // from the warlocks' Demon Portals

        // Nefarian's class calls.
        NPC_CORRUPTED_FIRE_NOVA_TOTEM = 14662,
        NPC_CORRUPTED_STONESKIN_TOTEM = 14663,
        NPC_CORRUPTED_HEALING_TOTEM   = 14664,
        NPC_CORRUPTED_WINDFURY_TOTEM  = 14666,
        NPC_CORRUPTED_INFERNAL        = 14668,

        NPC_CORRUPTED_RED_WHELP    = 14022,
        NPC_CORRUPTED_GREEN_WHELP  = 14023,
        NPC_CORRUPTED_BLUE_WHELP   = 14024,
        NPC_CORRUPTED_BRONZE_WHELP = 14025,
    };

    enum Spells : uint32
    {
        SPELL_SHADOW_OF_EBONROC = 23340,
        SPELL_FLAME_BUFFET      = 23341,
        SPELL_MARK_OF_DETONATION = 22438,  // Death Talon Captain
        SPELL_SEETHER_ENRAGE    = 22428,
        SPELL_BROOD_AFFLICTION_BLACK = 23154,  // Chromaggus: curse, more fire damage taken
        SPELL_CORRUPTED_HEALING = 23401,       // Nefarian's priest call
        SPELL_ONYXIA_SCALE_CLOAK = 22683,      // the cloak's aura; Shadow Flame checks for it

        // Curse removal, for telling who in the group can take Black off.
        SPELL_MAGE_REMOVE_CURSE   = 475,
        SPELL_DRUID_REMOVE_CURSE  = 2782,
        SPELL_CLEANSE_SPIRIT      = 51886,

        // Chromaggus' breaths (two per instance, every 30s, 2s cast).
        SPELL_INCINERATE        = 23308,
        SPELL_TIME_LAPSE        = 23310,
        SPELL_CORROSIVE_ACID    = 23313,
        SPELL_IGNITE_FLESH      = 23315,
        SPELL_FROST_BURN        = 23187,
    };

    enum GameObjects : uint32
    {
        GO_SUPPRESSION_DEVICE = 179784,
    };

    // Suppression Aura (22247) radius, plus a little for bots on the edge.
    constexpr float SUPPRESSION_DISARM_RANGE = 22.0f;
    // Blast Wave (23331) radius is 20 yd.
    constexpr float BROODLORD_RANGED_MIN = 23.0f;
    constexpr float BROODLORD_RANGED_TARGET = 26.0f;

    // Bomb (22334) splashes 5 yd; a yard more so the edge of one doesn't reach the next bot.
    constexpr float TECHNICIAN_SPREAD = 6.0f;
    // Technicians fighting this close count; Bomb's own range is 30 yd.
    constexpr float TECHNICIAN_RANGE = 35.0f;
    // Each step out of a clump, and the pause before the next one. Re-deciding every tick makes
    // the whole camp zigzag as every bot reacts to every other bot's last move.
    constexpr float TECHNICIAN_STEP = 4.0f;
    constexpr uint32 TECHNICIAN_STEP_INTERVAL_MS = 1500;

    // Firemaw: hide at 5-7 Flame Buffet stacks (each one adds fire damage taken), the exact
    // number varying per bot so the raid doesn't leave at once. At most a third of the healers
    // hide at the same time.
    constexpr uint8 FLAME_BUFFET_HIDE_STACKS = 5;
    constexpr uint8 FLAME_BUFFET_HIDE_SPREAD = 3;
    constexpr uint32 FIREMAW_HEALER_HIDE_SHARE = 3;
    // Cover search radius and the longest walk accepted: anywhere around a corner in Firemaw's
    // room; Chromaggus' breath gives 2s, about 14 yd of running.
    constexpr float FIREMAW_COVER_RANGE = 30.0f;
    constexpr float FIREMAW_COVER_PATH = 45.0f;
    constexpr float CHROMAGGUS_COVER_RANGE = 12.0f;
    constexpr float CHROMAGGUS_COVER_PATH = 14.0f;
    // Bellowing Roar (22686) fears everyone within 35 yd. Healers still reach a tank at his feet.
    constexpr float NEFARIAN_RANGED_MIN = 36.0f;
    constexpr float NEFARIAN_RANGED_TARGET = 37.5f;

    // Mark of Detonation's explosion (22439) hits the marked player's allies within 30 yd.
    // Non-tanks keep a margin beyond it. Within DETONATION_HOLD (+4) of the marked player they
    // don't walk toward a target inside the circle.
    constexpr float DETONATION_RADIUS = 30.0f;
    constexpr float DETONATION_KEEP_AWAY = 32.0f;
    constexpr float DETONATION_KEEP_AWAY_TARGET = 35.0f;
    constexpr float DETONATION_HOLD = 36.0f;
    // The Captain's off-tank keeps him this far from the main tank's mob (and its melee).
    constexpr float CAPTAIN_SPLASH = 36.0f;
    // The corrupted Fire Nova totem's blast (Fire Nova 11307, about 10 yd), with a margin.
    constexpr float FIRE_NOVA_KEEP_AWAY = 13.0f;
    // Priests under Corrupted Healing still heal a tank below this.
    constexpr float CORRUPTED_HEALING_TANK_PCT = 35.0f;
    // Tranquilizing Shot's range.
    constexpr float TRANQUILIZING_SHOT_RANGE = 35.0f;

    std::vector<KillOrderEntry> const& KillOrder();

    // Bosses that cut their tank's threat: the off-tank builds threat on them too.
    std::vector<uint32> const& TankSplitCoTankBosses();

    // Adds the off-tank leaves alone (the Suppression Room's endless whelps).
    std::vector<uint32> const& TankSplitIgnore();

    // Bosses whose tanks playerbots / mod-dungeon-clear place themselves (Razorgore).
    std::vector<uint32> const& TankSplitSkipBosses();

    // Trash whose AoE needs more room than the default tank separation.
    std::vector<Tanks::SplashRadius> const& TankSplitSplashRadii();

    // Adds that get an off-tank of their own (the Death Talon Captain).
    std::vector<uint32> const& TankSplitOwnTankAdds();

    // Packs where every tank takes one (the Death Talon Wyrmguards).
    std::vector<uint32> const& TankSplitSpreadAdds();

    // Player hooks for Blackwing Lair (the Onyxia Scale Cloak aura on revived bots).
    void AddScripts();
}

class RaidClearBlackwingLairStrategy : public Strategy
{
public:
    RaidClearBlackwingLairStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc bwl"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
    bool HasTargetExclusions() const override { return true; }
    void AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type) override;
};

// --- Suppression Room -----------------------------------------------------

class RcBwlSuppressionDeviceTrigger : public Trigger
{
public:
    RcBwlSuppressionDeviceTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl suppression device") {}
    bool IsActive() override;
};

class RcBwlDisarmSuppressionAction : public Action
{
public:
    RcBwlDisarmSuppressionAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl disarm suppression") {}
    bool Execute(Event event) override;
};

// --- Technician packs -----------------------------------------------------

class RcBwlTechnicianSpreadTrigger : public Trigger
{
public:
    RcBwlTechnicianSpreadTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl technician spread") {}
    bool IsActive() override;

private:
    uint64 _nextStepMs = 0;  // game time (ms) before which this bot doesn't step again
};

class RcBwlTechnicianSpreadAction : public MovementAction
{
public:
    RcBwlTechnicianSpreadAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl technician spread") {}
    bool Execute(Event event) override;
};

// --- Line-of-sight cover (Firemaw, Chromaggus) ----------------------------

// Walks to the nearest reachable spot the boss can't see, and stays there while the trigger
// holds. The spot is remembered so the search runs once per hide, not every tick.
class RcBwlHideAction : public MovementAction
{
public:
    RcBwlHideAction(PlayerbotAI* botAI, std::string const& name, float range, float maxPath)
        : MovementAction(botAI, name), _range(range), _maxPath(maxPath) {}
    bool Execute(Event event) override;

protected:
    virtual Unit* HideFrom() = 0;

private:
    float _range;
    float _maxPath;
    ObjectGuid _coverFrom;
    Position _cover;
    uint64 _coverFoundMs = 0;
    uint64 _noCoverUntilMs = 0;  // a search just found nothing; don't repeat it every tick
};

// While a bot is hidden, nothing else walks it back into view (its spells still go out).
class RcBwlHoldCoverMultiplier : public Multiplier
{
public:
    RcBwlHoldCoverMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl hold cover") {}
    float GetValue(Action* action) override;
};

class RcBwlFiremawHideTrigger : public Trigger
{
public:
    RcBwlFiremawHideTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl firemaw hide") {}
    bool IsActive() override;
};

class RcBwlFiremawHideAction : public RcBwlHideAction
{
public:
    RcBwlFiremawHideAction(PlayerbotAI* botAI) : RcBwlHideAction(botAI, "rc bwl firemaw hide", RaidClear::BlackwingLair::FIREMAW_COVER_RANGE,
                          RaidClear::BlackwingLair::FIREMAW_COVER_PATH) {}

protected:
    Unit* HideFrom() override;
};

class RcBwlChromaggusBreathTrigger : public Trigger
{
public:
    RcBwlChromaggusBreathTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl chromaggus breath") {}
    bool IsActive() override;
};

class RcBwlChromaggusHideAction : public RcBwlHideAction
{
public:
    RcBwlChromaggusHideAction(PlayerbotAI* botAI)
        : RcBwlHideAction(botAI, "rc bwl chromaggus hide", RaidClear::BlackwingLair::CHROMAGGUS_COVER_RANGE,
                          RaidClear::BlackwingLair::CHROMAGGUS_COVER_PATH) {}

protected:
    Unit* HideFrom() override;
};

// --- Keep out of a boss's AoE (Broodlord, Nefarian) -----------------------

// Ranged and healers closer than `minDistance` (edge to edge) to the boss step straight out to
// `targetDistance`. A healer only does so if it can still reach the boss's target from there.
class RcBwlKeepOutTrigger : public Trigger
{
public:
    RcBwlKeepOutTrigger(PlayerbotAI* botAI, std::string const& name, char const* bossName, float minDistance,
                        float targetDistance)
        : Trigger(botAI, name), _bossName(bossName), _minDistance(minDistance), _targetDistance(targetDistance) {}
    bool IsActive() override;

private:
    char const* _bossName;
    float _minDistance;
    float _targetDistance;
};

class RcBwlKeepOutAction : public MovementAction
{
public:
    RcBwlKeepOutAction(PlayerbotAI* botAI, std::string const& name, char const* bossName, float targetDistance)
        : MovementAction(botAI, name), _bossName(bossName), _targetDistance(targetDistance) {}
    bool Execute(Event event) override;

private:
    char const* _bossName;
    float _targetDistance;
};

// --- Ebonroc --------------------------------------------------------------

class RcBwlEbonrocTauntTrigger : public Trigger
{
public:
    RcBwlEbonrocTauntTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl ebonroc taunt") {}
    bool IsActive() override;
};

class RcBwlEbonrocTauntAction : public Action
{
public:
    RcBwlEbonrocTauntAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl ebonroc taunt") {}
    bool Execute(Event event) override;
};

// The tank with Shadow of Ebonroc leaves him to the other tank until it wears off.
class RcBwlShadowOfEbonrocMultiplier : public Multiplier
{
public:
    RcBwlShadowOfEbonrocMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl shadow of ebonroc") {}
    float GetValue(Action* action) override;
};

// --- Death Talon packs ----------------------------------------------------

// Non-tanks within DETONATION_KEEP_AWAY of a group member carrying Mark of Detonation.
class RcBwlDetonationKeepAwayTrigger : public Trigger
{
public:
    RcBwlDetonationKeepAwayTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl detonation keep away") {}
    bool IsActive() override;
};

class RcBwlDetonationKeepAwayAction : public MovementAction
{
public:
    RcBwlDetonationKeepAwayAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl detonation keep away") {}
    bool Execute(Event event) override;
};

// A non-tank doesn't walk toward a target inside a marked player's circle (chasing a mob next
// to the Captain's tank), so it doesn't walk back into the explosion it just left.
class RcBwlDetonationHoldMultiplier : public Multiplier
{
public:
    RcBwlDetonationHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl detonation hold") {}
    float GetValue(Action* action) override;
};

// The main tank drops a Captain it is still targeting once he is an off-tank's.
class RcBwlCaptainHandOffTrigger : public Trigger
{
public:
    RcBwlCaptainHandOffTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl captain hand off") {}
    bool IsActive() override;
};

class RcBwlCaptainHandOffAction : public AttackAction
{
public:
    RcBwlCaptainHandOffAction(PlayerbotAI* botAI) : AttackAction(botAI, "rc bwl captain hand off") {}
    bool Execute(Event event) override;
};

// The main tank doesn't taunt a Captain back from the off-tank that came for him.
class RcBwlCaptainMainTankMultiplier : public Multiplier
{
public:
    RcBwlCaptainMainTankMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl captain main tank") {}
    float GetValue(Action* action) override;
};

// Priests under Nefarian's Corrupted Healing don't cast heals (shields are fine, and a tank
// below 35% still gets one).
class RcBwlCorruptedHealingMultiplier : public Multiplier
{
public:
    RcBwlCorruptedHealingMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl corrupted healing") {}
    float GetValue(Action* action) override;
};

class RcBwlFireNovaTotemTrigger : public Trigger
{
public:
    RcBwlFireNovaTotemTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl fire nova totem") {}
    bool IsActive() override;
};

class RcBwlFireNovaTotemMoveAwayAction : public MovementAction
{
public:
    RcBwlFireNovaTotemMoveAwayAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl fire nova totem move away") {}
    bool Execute(Event event) override;
};

class RcBwlBlackAfflictionTrigger : public Trigger
{
public:
    RcBwlBlackAfflictionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl black affliction") {}
    bool IsActive() override;
};

class RcBwlBlackAfflictionAction : public Action
{
public:
    RcBwlBlackAfflictionAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl remove black affliction") {}
    bool Execute(Event event) override;
};

class RcBwlSeetherTranqTrigger : public Trigger
{
public:
    RcBwlSeetherTranqTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl seether tranq") {}
    bool IsActive() override;
};

class RcBwlSeetherTranqAction : public Action
{
public:
    RcBwlSeetherTranqAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl seether tranq") {}
    bool Execute(Event event) override;
};

#endif
