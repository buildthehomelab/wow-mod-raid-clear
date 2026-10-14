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
 *   - Vaelastrasz's room (BwlRunners.cpp): the six Blackwing Technicians round him run for the
 *     far door the moment a player walks in, and are gone some 25 seconds later, their loot
 *     (Elementium Ore, Hourglass Sand) with them. They are passive while they run, so no bot
 *     ever picked one up: playerbots only offers what is fighting the raid. One hit stops a
 *     technician for five minutes. Every bot that isn't a healer hits one that is still
 *     running, each starting with a different one, and the damage dealers go after those out of
 *     reach; tanks stay where they are. Not once Vaelastrasz himself is in the fight.
 *   - Firemaw: Flame Buffet (23341) stacks on everyone in his line of sight every 5s. A non-tank
 *     at 5 stacks hides behind cover until it drops off.
 *   - Firemaw, Ebonroc, Flamegor, Nefarian (BwlPositions.cpp): Shadow Flame is a 120-degree
 *     cone 45 yd long that kills a non-tank outright; Wing Buffet and Cleave go the same way.
 *     Everyone but the dragon's victim and the tank that takes it next stays out of the front,
 *     and a bot standing in it when a cast starts drops everything and runs. Playerbots' own
 *     rule takes every non-victim to the flank at melee range, casters and healers included;
 *     for those it is switched off.
 *   - Chromaggus: he breathes every 30s from the pull, his two breaths taking turns, and a 2s
 *     cast is too short to reach cover in. Non-tanks go behind cover a few seconds before a
 *     breath is due (the clock is kept from his casts), except for Time Lapse, which
 *     everyone should take because it halves the threat of everyone it hits, tank included.
 *     Brood Affliction: Black (a curse that makes its target take more fire damage, which
 *     Incinerate and Ignite Flesh deal) is removed before anything else the curse removers would
 *     do: mages, druids and Cleanse Spirit shamans split the cursed players between them, tanks
 *     first, then the lowest on health. Healers leave it while a tank is below half health.
 *   - Nefarian: no taunt lands on him, and the druid call (cat form) kills a bear that has
 *     him. So a paladin tank holds him when there is one: the other tanks stay under 80% of its
 *     threat, the damage dealers under the tanks' (the threat cap in Common/TankRoles.h).
 *     Ranged and healers stay outside Bellowing Roar (35 yd from his middle; he is 17 yd deep,
 *     so spells reach him from 48), healers where they still reach whoever he is on, and
 *     nobody stands behind him (Tail Lash). Shamans keep Tremor Totem down; a priest walks over
 *     to put Fear Ward on his tank whenever it is ready. His rogue call teleports every rogue
 *     to 5 yd in front of his middle and stuns them there for 30s, two or three Shadow Flames:
 *     the tank he is on walks round him so he turns his back on them. At 20% he raises the
 *     dead drakonids as Bone Constructs: a spare tank stands still at his side, the casters
 *     and healers stand on it, and the pile dies to everyone's area damage.
 *   - Death Talon Wyrmguards: War Stomp hits everything within 15 yd. Non-tanks stay 17.5 yd
 *     from every Wyrmguard they aren't in melee with.
 *   - Hunters Tranquilizing Shot Flamegor's and Chromaggus' Frenzy, like the Seethers' Enrage.
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
#include "Common/Knockback.h"
#include "Common/TankRoles.h"
#include "Common/WayOut.h"

#include "Action.h"
#include "AttackAction.h"
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
        NPC_VICTOR_NEFARIUS       = 10162,  // phase one

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
        SPELL_FLAMEGOR_FRENZY   = 23342,   // "Enrage" in the client: Fire Nova on the whole raid while it lasts
        SPELL_CHROMAGGUS_FRENZY = 23128,   // "Enrage" in the client; the one below 20% can't be removed
        SPELL_SHADOW_FLAME      = 22539,   // Firemaw, Ebonroc, Flamegor, Nefarian: 120 degrees, 45 yd
        SPELL_WING_BUFFET       = 23339,   // the three drakes: 120 degrees, 20 yd; throws 39 yd
        SPELL_BROODLORD_KNOCK_AWAY = 18670,  // whoever he is on, 17 yd
        SPELL_BROODLORD_BLAST_WAVE = 23331,  // everyone within 20 yd of him, 18 yd
        SPELL_FEAR_WARD         = 6346,
        SPELL_ROGUE_PARALYZE    = 23414,   // Nefarian's rogue call
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

    // The technicians that run from Vaelastrasz's room. A bot hits one that is this close
    // (edge to edge) and in sight, melee only one on their own floor, and stays on it out to
    // RUNNER_KEEP. A damage dealer with none that close goes after the nearest
    // within RUNNER_CHASE: melee only after one on their own floor (within RUNNER_FLOOR of
    // their height; on foot nobody catches one up a ramp), a caster to RUNNER_STAND_OFF short
    // of it on the ground (they run along a gallery 15 yd up, and there is no seeing one from
    // right underneath). The walk is aimed again every RUNNER_WALK_INTERVAL_MS.
    constexpr float RUNNER_REACH = 30.0f;
    constexpr float RUNNER_KEEP = 36.0f;
    constexpr float RUNNER_CHASE = 60.0f;
    constexpr float RUNNER_FLOOR = 5.0f;
    constexpr float RUNNER_STAND_OFF = 18.0f;
    constexpr uint32 RUNNER_WALK_INTERVAL_MS = 500;
    // A bot lets go of a runner that has been out of reach or sight this long.
    constexpr uint32 RUNNER_LET_GO_MS = 500;

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
    constexpr float CHROMAGGUS_COVER_RANGE = 25.0f;
    constexpr float CHROMAGGUS_COVER_PATH = 40.0f;
    // Chromaggus breathes every 30s from the pull, his two breaths taking turns. Non-tanks go
    // for cover this long before one is due (healers much later, so the tank isn't left without
    // heals for the whole walk: a healer whose cover is further than a 3s run takes the breath),
    // and give up on a breath that is this late.
    constexpr uint32 CHROMAGGUS_BREATH_INTERVAL_MS = 30000;
    constexpr uint32 CHROMAGGUS_HIDE_LEAD_MS = 7000;
    constexpr uint32 CHROMAGGUS_HEALER_HIDE_LEAD_MS = 3000;
    constexpr uint32 CHROMAGGUS_BREATH_LATE_MS = 4000;

    // Shadow Flame and Wing Buffet are 120-degree cones (spell_cone) from the middle of the
    // dragon, Shadow Flame 45 yd long plus the size of whoever it hits. Bots keep a wider angle
    // than the cone itself, so a small turn of the dragon doesn't put them in it.
    constexpr float BREATH_HALF_ANGLE = 1.0472f;       // 60 degrees
    constexpr float BREATH_KEEP_HALF_ANGLE = 1.2566f;  // 72 degrees
    constexpr float BREATH_RANGE = 48.0f;
    // Nefarian's Tail Lash: 120 degrees behind him, 30 yd.
    constexpr float TAIL_HALF_ANGLE = 1.0472f;
    constexpr float TAIL_RANGE = 32.0f;
    // A spot counts as out with this much room to spare; a spot that isn't out is still worth
    // the walk when it is this much better; how far round the bot spots are looked for (a bot
    // dead ahead of him at 30 yd has 26 yd to the side of the cone).
    constexpr float DRAGON_SPOT_MIN = 0.5f;
    constexpr float DRAGON_SPOT = 3.0f;
    constexpr float DRAGON_WORTH_IT = 2.0f;
    constexpr float DRAGON_SEARCH = 39.0f;
    // Bellowing Roar (22686) fears everyone within 35 yd of Nefarian's middle (plus their own
    // size). He is 17 yd deep, so spells reach him from 48: ranged and healers stay outside the
    // roar, healers where whoever he is on is still within this of them. Closer than ROAR_HOLD
    // nothing walks them toward a target that is under the roar.
    // (A heal reaches 40 yd edge to edge, 43 between two players' middles. With the tank 18 yd
    // in front of him that leaves a healer an arc of about 12 by 5 yd on either side.)
    constexpr float ROAR_KEEP_AWAY = 37.5f;
    constexpr float ROAR_HEALER_REACH = 41.5f;
    constexpr float ROAR_HOLD = 46.0f;
    // No taunt lands on Nefarian. The other tanks stay under this share of the threat of the tank
    // that should have him (he changes target at 110% in melee).
    constexpr float NEFARIAN_OFFTANK_SHARE = 0.8f;
    // ...as long as that tank is in melee with him, and until they have NEFARIAN_HANDOVER_THREAT
    // themselves: past that the tank he is on is never held back, and another only while the
    // tank that should have him has at least this share of its threat. A tank that was just
    // revived can't catch up with minutes of threat, and nobody waits for it.
    constexpr float NEFARIAN_HANDOVER_THREAT = 30000.0f;
    constexpr float NEFARIAN_CATCH_UP_SHARE = 0.5f;
    // His rogue call puts every rogue 5 yd in front of his middle and stuns them there for 30s.
    // The tank he is on walks round to where they are this many radians clear of the breath.
    constexpr float NEFARIAN_TURN_CLEAR = 0.15f;
    // At 20% he raises every drakonid that died in phase one as a Bone Construct: some 45 of
    // them, about 6,000 health each, and every heal puts all of them on the healers. They only
    // die to area damage, and that only works on a pile. So while this many are up (he is under
    // CONSTRUCT_PHASE_PCT), one spare tank stands still at his side and the casters and healers
    // stand on it: further than GATHER_FAR they walk to within GATHER_SPOT. That is inside the
    // roar; a fear every half minute is the price. The tank the pile is made on stands in the
    // middle of his flank (85 to 105 degrees off his facing, so the pile round it is clear of
    // his front and tail), within ANCHOR_REACH of whoever he is on, and only steps toward
    // something within GATHER_NEAR. A bot on his other side walks round behind him in steps
    // of RING_STEP radians.
    constexpr float CONSTRUCT_PHASE_PCT = 21.0f;
    constexpr uint32 CONSTRUCTS_TO_GATHER = 4;
    constexpr float CONSTRUCT_GATHER_SPOT = 3.0f;
    constexpr float CONSTRUCT_GATHER_FAR = 7.0f;
    constexpr float CONSTRUCT_GATHER_NEAR = 9.0f;
    constexpr float CONSTRUCT_ANCHOR_FRONT = 1.4835f;  // 85 degrees
    constexpr float CONSTRUCT_ANCHOR_TAIL = 1.3090f;   // 75 degrees
    constexpr float CONSTRUCT_ANCHOR_REACH = 30.0f;
    constexpr float CONSTRUCT_RING_STEP = 0.35f;
    // Fear Ward reaches 30 yd; a priest this far from the tank or nearer walks over to cast it.
    constexpr float FEAR_WARD_RANGE = 30.0f;
    constexpr float FEAR_WARD_WALK = 70.0f;
    constexpr uint32 FEAR_WARD_RETRY_MS = 3000;

    // War Stomp (24375) hits everyone within 15 yd of the Wyrmguard's middle, plus their own
    // size. Non-tanks stay this far from every Wyrmguard they aren't in melee with, and go to a
    // spot a little further. Two Wyrmguards closer together than WYRMGUARD_APART can't be told
    // apart by a melee bot: it stays on its own. Within WYRMGUARD_HOLD of one, casters and
    // healers don't walk toward anything in its stomp.
    constexpr float WYRMGUARD_KEEP_AWAY = 17.5f;
    constexpr float WYRMGUARD_SPOT = 20.0f;
    constexpr float WYRMGUARD_WORTH_IT = 2.0f;
    constexpr float WYRMGUARD_APART = 20.0f;
    constexpr float WYRMGUARD_HOLD = 24.0f;

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

    // The mobs that throw players about, with the spell they do it with (Common/Knockback.h).
    std::vector<Knockback::Thrower> const& Throwers();

    // Adds that get an off-tank of their own (the Death Talon Captain).
    std::vector<uint32> const& TankSplitOwnTankAdds();

    // Packs where every tank takes one (the Death Talon Wyrmguards).
    std::vector<uint32> const& TankSplitSpreadAdds();

    // Nefarian: no taunt lands on him, so the tanks share threat and the damage dealers stay
    // under them (see Common/TankRoles.h).
    std::vector<uint32> const& TankSplitThreatShareBosses();

    // Chromaggus has started casting something: kept for the breath clock.
    void NoteChromaggusBreath(Creature* boss, uint32 spellId);

    // Player hooks for Blackwing Lair (the Onyxia Scale Cloak aura on revived bots).
    void AddScripts();
    // Spell hooks: bots woken out of a cast when a dragon starts its breath, the breath clock.
    void AddPositionScripts();
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

// --- Vaelastrasz's room: the technicians that run (BwlRunners.cpp) --------

// On both engines: nothing is fighting anyone when they set off.
class RaidClearBwlRunnersStrategy : public Strategy
{
public:
    RaidClearBwlRunnersStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc bwl runners"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// A technician is running and this bot isn't on one yet, or has none in reach to be on.
class RcBwlFleeingTechnicianTrigger : public Trigger
{
public:
    RcBwlFleeingTechnicianTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl fleeing technician") {}
    bool IsActive() override;

private:
    uint32 _lostAtMs = 0;  // since when the runner the bot is on has been out of reach; 0 = it isn't
};

// Attacks one in reach; lets go of one that has got out of it, and goes after one that isn't.
class RcBwlCatchTechnicianAction : public AttackAction
{
public:
    RcBwlCatchTechnicianAction(PlayerbotAI* botAI) : AttackAction(botAI, "rc bwl catch technician") {}
    bool Execute(Event event) override;

private:
    bool GoAfter(Creature* runner);
    bool OwnWalk();

    uint32 _walkAtMs = 0;  // when the bot was last sent after one; 0 = never, or that walk was called off
    Position _walkTo;      // and where to
};

// While a bot has a runner to hit, nothing else picks its target: the first technician that
// stops is fighting the raid, and playerbots would put every bot on that one. And a tank
// doesn't walk after a runner.
class RcBwlCatchTechnicianMultiplier : public Multiplier
{
public:
    RcBwlCatchTechnicianMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl catch technician") {}
    float GetValue(Action* action) override;
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
    RcBwlKeepOutTrigger(PlayerbotAI* botAI, std::string const& name, uint32 bossEntry, float minDistance,
                        float targetDistance)
        : Trigger(botAI, name), _bossEntry(bossEntry), _minDistance(minDistance), _targetDistance(targetDistance) {}
    bool IsActive() override;

private:
    uint32 _bossEntry;
    float _minDistance;
    float _targetDistance;
};

class RcBwlKeepOutAction : public MovementAction
{
public:
    RcBwlKeepOutAction(PlayerbotAI* botAI, std::string const& name, uint32 bossEntry, float targetDistance)
        : MovementAction(botAI, name), _bossEntry(bossEntry), _targetDistance(targetDistance) {}
    bool Execute(Event event) override;

private:
    uint32 _bossEntry;
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

// --- Tranquilizing Shot: Seethers, Flamegor, Chromaggus -------------------

class RcBwlTranqTrigger : public Trigger
{
public:
    RcBwlTranqTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl tranq") {}
    bool IsActive() override;
};

class RcBwlTranqAction : public Action
{
public:
    RcBwlTranqAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl tranq") {}
    bool Execute(Event event) override;
};

// --- The dragons: breath, tail, roar (BwlPositions.cpp) -------------------

// The bot stands somewhere it shouldn't around Firemaw, Ebonroc, Flamegor or Nefarian: in front
// (Shadow Flame, Wing Buffet, Cleave), behind Nefarian (Tail Lash), or, for a caster or healer,
// under his Bellowing Roar or (a healer) out of reach of whoever he is on. Not the bot the
// dragon is on, nor the tank that takes it next.
class RcBwlDragonPositionTrigger : public Trigger
{
public:
    RcBwlDragonPositionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl dragon position") {}
    bool IsActive() override;
};

// The dragon is casting Shadow Flame or Wing Buffet and the bot is in it.
class RcBwlDragonBreathTrigger : public Trigger
{
public:
    RcBwlDragonBreathTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl dragon breath") {}
    bool IsActive() override;
};

// Not a MovementAction: see RaidClear::WayOutWalk.
class RcBwlDragonMoveAction : public Action
{
public:
    RcBwlDragonMoveAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl dragon move"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Casters and healers aren't taken to the dragon's flank at melee range by playerbots' own
// positioning, and nothing walks them in under Nefarian's roar.
class RcBwlDragonHoldMultiplier : public Multiplier
{
public:
    RcBwlDragonHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl dragon hold") {}
    float GetValue(Action* action) override;
};

// --- Nefarian: who holds him, fear -----------------------------------------

// The tanks that shouldn't have Nefarian stay under the threat of the one that should.
class RcBwlNefarianTankMultiplier : public Multiplier
{
public:
    RcBwlNefarianTankMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl nefarian tank") {}
    float GetValue(Action* action) override;
};

// A shaman in the Nefarian fight without a Tremor Totem down ("rc mc tremor totem" drops it).
class RcBwlNefarianTremorTrigger : public Trigger
{
public:
    RcBwlNefarianTremorTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl nefarian tremor") {}
    bool IsActive() override;
};

// ...and nothing else goes in the earth slot meanwhile.
class RcBwlNefarianTremorMultiplier : public Multiplier
{
public:
    RcBwlNefarianTremorMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl nefarian tremor") {}
    float GetValue(Action* action) override;
};

// A priest's Fear Ward is ready and Nefarian's tank doesn't have one.
class RcBwlFearWardTrigger : public Trigger
{
public:
    RcBwlFearWardTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl nefarian fear ward") {}
    bool IsActive() override;
};

// Walks into range if it has to, and casts it.
class RcBwlFearWardAction : public MovementAction
{
public:
    RcBwlFearWardAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl nefarian fear ward") {}
    bool Execute(Event event) override;
    Unit* GetTarget() override;

private:
    uint32 _failedAtMs = 0;
};

// Nefarian's rogue call: the bot he is on has stunned raid members in front of him.
class RcBwlNefarianRogueCallTrigger : public Trigger
{
public:
    RcBwlNefarianRogueCallTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl nefarian rogue call") {}
    bool IsActive() override;
};

// Walks to the other side of him, so he turns his back on them.
class RcBwlNefarianTurnAction : public MovementAction
{
public:
    RcBwlNefarianTurnAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl nefarian turn") {}
    bool Execute(Event event) override;
};

// Nefarian's Bone Constructs are up, the tank that collects them is in place, and this caster
// or healer isn't with it.
class RcBwlConstructGatherTrigger : public Trigger
{
public:
    RcBwlConstructGatherTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl construct gather") {}
    bool IsActive() override;
};

class RcBwlConstructGatherAction : public MovementAction
{
public:
    RcBwlConstructGatherAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl construct gather") {}
    bool Execute(Event event) override;
};

// That tank stays where it is: it takes what comes to it and doesn't chase the rest.
class RcBwlConstructAnchorMultiplier : public Multiplier
{
public:
    RcBwlConstructAnchorMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl construct anchor") {}
    float GetValue(Action* action) override;
};

// --- Death Talon Wyrmguards: War Stomp -------------------------------------

// A bot that isn't a tank stands in the War Stomp of a Wyrmguard it isn't in melee with.
class RcBwlWyrmguardKeepAwayTrigger : public Trigger
{
public:
    RcBwlWyrmguardKeepAwayTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl wyrmguard keep away") {}
    bool IsActive() override;
};

class RcBwlWyrmguardKeepAwayAction : public Action
{
public:
    RcBwlWyrmguardKeepAwayAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl wyrmguard keep away"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

class RcBwlWyrmguardHoldMultiplier : public Multiplier
{
public:
    RcBwlWyrmguardHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl wyrmguard hold") {}
    float GetValue(Action* action) override;
};

#endif
