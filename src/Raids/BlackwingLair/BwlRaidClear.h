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
 *     so the pack only shrinks once the warlocks are dead. Technicians die with them: each
 *     throws a Bomb at someone every few seconds. Then the casters and elites that heal,
 *     polymorph or buff: Taskmasters, Spellbinders, Hatchers.
 *   - Interrupts (BwlInterrupts.cpp): any bot with a kick, pummel, shield bash, wind shear,
 *     counterspell or silence in reach stops a trash cast, its target or not, one bot per cast:
 *     Fireball Volley, then Healing Circle, Shadow Bolt, Flamestrike, Fireball. In one 40-bot
 *     evening only 17 casts were interrupted (playerbots only kicks the bot's own target).
 *     Only the Warlocks' Shadow Bolt and the Mages' Fireball can be stopped on this core: the
 *     Wyrmkin, Hatchers and Spellbinders are immune to interrupts, and Healing Circle can't be
 *     silenced (Common/Interrupts.h). The others stay listed for a core that changes that.
 *   - Suppression Devices: Suppression Aura reaches 20 yd, playerbots' disarm only 15, so a bot
 *     standing 15-20 yd from an armed device stays slowed. With the "raid" bot cheat on, bots in
 *     the Suppression Room turn off every armed device within 22 yd.
 *   - Broodlord, Firemaw, Ebonroc, Flamegor: Knock Away (-50%) and Wing Buffet (-75%) cut the
 *     main tank's threat, so the off-tank builds threat on the boss instead of standing idle and
 *     catches it (the tank strategies taunt it back). The Suppression Room whelps don't pull the
 *     off-tank away from Broodlord.
 *   - Firemaw, Ebonroc, Flamegor: two tanks hold the drake, the main tank and the first bot
 *     off-tank in group order that isn't holding an add, and both stand in front of it. Only
 *     those two taunt it, only from in front (or next to whoever it is on) and never while
 *     Shadow Flame is being cast; the other tanks stay under 80% of their threat. A taunt from
 *     a tank at its flank turned the drake, and the breath with it, onto the raid: five tanks
 *     taunted Flamegor within 0.3 s after a Wing Buffet and the next Shadow Flame killed 18.
 *   - Chromaggus: no taunt lands on him, and in one kill he spent most of the fight on rogues,
 *     warlocks and a death knight. The off-tanks build threat on him and the damage dealers stay
 *     under the tanks (the threat cap in Common/TankRoles.h), as on Nefarian.
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
 *   - Firemaw: Flame Buffet (23341) stacks on everyone in his line of sight every 5s. A damage
 *     dealer or spare tank at 5-7 stacks hides until it drops off; healers take turns, the most
 *     stacks first, at most a third of them at once (any at 9 or more go anyway). His two tanks
 *     trade him: the one he is on hands him to the other once it has 10 stacks and 7 more than
 *     the other, and a holder he isn't on hides at 10 while a spare tank can stand in for it.
 *     A bot that hides is "hiding" for everyone (BwlCover.cpp) until its last stack is gone.
 *   - Firemaw and Chromaggus can have fixed spots, from the config (BwlCover.cpp): where the
 *     tank he is on holds him, where Firemaw's second tank stands, where ranged and healers
 *     fight from, and where each role hides. Without them, cover is searched for round the bot.
 *     Sight is judged the way the core's spells judge it (small props don't block).
 *   - Firemaw, Ebonroc, Flamegor, Nefarian (BwlPositions.cpp): Shadow Flame is a 120-degree
 *     cone 45 yd long that kills a non-tank outright; Wing Buffet and Cleave go the same way.
 *     Everyone but the dragon's victim and the tank that takes it next stays out of the front,
 *     and a bot standing in it when a cast starts drops everything and runs. Playerbots' own
 *     rule takes every non-victim to the flank at melee range, casters and healers included;
 *     for those it is switched off. Follow and formation don't walk a bot back into the front,
 *     and Charge, Intercept and Feral Charge onto the dragon aren't used from in front of it
 *     (they land on the near side).
 *   - Chromaggus: he breathes every 30s from the pull, his two breaths taking turns, and a 2s
 *     cast is too short to reach cover in. Everyone but the bot he is on goes behind cover a few
 *     seconds before a breath is due (the clock is kept from his casts), Time Lapse included: it
 *     only stuns (its threat cut was taken out in 2.0.1), and his tank is the one that takes it.
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
 *     dead drakonids as Bone Constructs: a spare tank stands still at his side, outside the
 *     roar where the healers stand, the casters and healers stand on it, and the pile dies to
 *     everyone's area damage. (Inside the roar a fear scattered the pile into his front: one
 *     Shadow Flame killed nine of it.)
 *   - Nefarian's Bellowing Roar: every roar of one evening feared all four tanks for its full
 *     length, he turned with them, and each Shadow Flame after it hit four to eight others. 84
 *     Tremor Totems did nothing: Tremor only frees its shaman's own party within 30 yd of the
 *     totem, and they stood with the shamans out on the ranged ring (and were put down again
 *     every time a shaman moved, since playerbots only counts a totem within 30 yd of its shaman).
 *     So each tank gets the Tremor Totem of the first shaman in its own party: that shaman walks
 *     in under the roar to within 22 yd of its tank, outside his front and tail, puts it down
 *     there and goes back; nothing replaces it in its earth slot, and it is put down again only
 *     once it is gone or more than 28 yd from the tank. A party without a tank or a shaman does
 *     without. Warriors within the roar use Berserker Rage the moment it starts. Taunts don't land
 *     on him; a non-tank he is on (as when he lands) walks to his tank, so his breath goes where
 *     the tank stands and not through the ranged ring, until the threat or a paladin's Hand of
 *     Protection (Common/TankRoles.h) hands him to it. His fly-in Shadow Flame (22992) isn't a
 *     breath: it is fire dropped on fixed spots as he comes in, before he lands.
 *   - Death Talon Wyrmguards: War Stomp hits everything within 15 yd, and all three of a pack
 *     stomp exactly 8 s after the pull (their SmartAI), by which time the tank has brought them
 *     to the raid: two or three stomps at once killed 6 to 16 bots in one second, four times
 *     in one evening. Non-tanks stay 17.5 yd from every Wyrmguard and from whoever a Wyrmguard
 *     is still running to (and from the main tank, where the pack ends up), from the moment the
 *     pack is pulled, and look up to 36 yd round them for a way out. Melee only stand by the one
 *     they hit, and only once no other Wyrmguard is within 20 yd of it. These rules run on both
 *     engines ("rc bwl runners"): a bot waiting at the camp isn't in combat yet. Their tanks
 *     hold them where they took them, not 30 yd apart: three stomps spread down a corridor left
 *     the raid nowhere to stand, two stomps on one spot leave everywhere else.
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
 *     within 30 yd, so ranged and healers keep 7 yd from each other while one is fighting nearby.
 *     Only a bot with someone that close moves, to the nearest spot with nobody that close (a
 *     search all the way round it; pushes away from each neighbour cancel out in a ring of
 *     them), and its follow, formation or camp hold doesn't walk it straight back.
 *   - Trash ground fire and cleaves (BwlHazards.cpp): bots walk out of the Warlocks' Rain of
 *     Fire and the Spellbinders' and Hatchers' Flamestrike while they burn (the fire on the
 *     ground, not the cast), and everyone but the tanks out of the front of a Death Talon
 *     Captain or Overseer it isn't on (Cleave). The way out is the one the leash allows.
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

class Creature;
class Player;

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
        NPC_DEATH_TALON_OVERSEER  = 12461,  // Cleave
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
        SPELL_BROODLORD_KNOCK_AWAY = 25778,  // whoever he is on (boss_broodlord_lashlayer)
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

        // Trash casts worth an interrupt (BwlInterrupts.cpp).
        SPELL_FIREBALL_VOLLEY   = 22425,   // Death Talon Wyrmkin: everyone within 45 yd
        SPELL_HEALING_CIRCLE    = 22458,   // Blackwing Taskmaster
        SPELL_BWL_SHADOW_BOLT   = 22336,   // Blackwing Warlock
        SPELL_BWL_FLAMESTRIKE   = 22275,   // Blackwing Spellbinder, Death Talon Hatcher
        SPELL_BWL_FIREBALL      = 17290,   // Blackwing Mage (Razorgore)

        SPELL_BELLOWING_ROAR    = 22686,   // Nefarian: fears everyone within 35 yd of his middle
        SPELL_TREMOR_TOTEM      = 8143,
        SPELL_BERSERKER_RAGE    = 18499,   // fear immunity for 10 s

        // Fire left on the ground (BwlHazards.cpp).
        SPELL_BWL_RAIN_OF_FIRE  = 19717,   // Blackwing Warlock, 10 yd
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

    // Bomb (22334) splashes 5 yd round whoever it is thrown at, plus the size of whoever else it
    // reaches; two yards more so the edge of one doesn't reach the next bot.
    constexpr float TECHNICIAN_SPREAD = 7.0f;
    // Technicians fighting this close count (looked up per bot every TECHNICIAN_FRESH_MS);
    // Bomb's own range is 30 yd.
    constexpr float TECHNICIAN_RANGE = 35.0f;
    constexpr uint32 TECHNICIAN_FRESH_MS = 100;
    // How long after a bot spread out a camp or follow walk with no known end waits.
    constexpr uint32 TECHNICIAN_SPREAD_HOLD_MS = 4000;
    // How far round the bot a spot is looked for, how far from its target it may be, and the
    // pause before the next look. Re-deciding every tick makes the whole camp zigzag as every
    // bot reacts to every other bot's last move.
    constexpr float TECHNICIAN_SEARCH = 12.0f;
    constexpr float TECHNICIAN_TARGET_REACH = 30.0f;
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
    // hide at the same time, those with the most stacks first; one with HEALER_MUST_HIDE_STACKS
    // goes whatever the others do. (Last night's log, 2026-10-17: the same five healers took
    // every turn and seven never dropped a stack, ending on 15 to 22.)
    constexpr uint8 FLAME_BUFFET_HIDE_STACKS = 5;
    constexpr uint8 FLAME_BUFFET_HIDE_SPREAD = 3;
    constexpr uint32 FIREMAW_HEALER_HIDE_SHARE = 3;
    constexpr uint8 HEALER_MUST_HIDE_STACKS = 9;
    // His two tanks: the one he is on hands him to the other once it has TANK_SWAP_STACKS and at
    // least TANK_SWAP_GAP more than that one; a holder he isn't on hides at TANK_REST_STACKS
    // while another tank with at most TANK_SPARE_STACKS can stand in for it.
    constexpr uint8 TANK_SWAP_STACKS = 10;
    constexpr uint8 TANK_SWAP_GAP = 7;
    constexpr uint8 TANK_REST_STACKS = 10;
    constexpr uint8 TANK_SPARE_STACKS = 5;
    // A bot that set off to hide and is still in his sight this long after gives up (nowhere to
    // go: it fights on), and doesn't try again for HIDE_RETRY_MS. It would otherwise keep a
    // healer's turn to itself.
    constexpr uint32 HIDE_GIVE_UP_MS = 8000;
    constexpr uint32 HIDE_RETRY_MS = 10000;
    // Cover search radius and the longest walk accepted, when no spot is set: anywhere around a
    // corner in Firemaw's room; Chromaggus' 7 s lead is some 45 yd of running.
    constexpr float FIREMAW_COVER_RANGE = 30.0f;
    constexpr float FIREMAW_COVER_PATH = 45.0f;
    constexpr float CHROMAGGUS_COVER_RANGE = 34.0f;
    constexpr float CHROMAGGUS_COVER_PATH = 45.0f;
    // Fixed spots (BwlCover.cpp). A tank holds within SPOT_TANK_NEAR of its spot, a caster or
    // healer within SPOT_NEAR of its own place round the spot (bots spread up to SPOT_SCATTER
    // round it). Within SPOT_HOLD nothing but the raid mechanics walks it away.
    constexpr float SPOT_TANK_NEAR = 2.0f;
    constexpr float SPOT_NEAR = 3.5f;
    constexpr float SPOT_SCATTER = 3.0f;
    constexpr float SPOT_HOLD = 8.0f;
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
    // stand on it: further than GATHER_FAR they walk to within GATHER_SPOT. The pile is where
    // the healers stand anyway: outside the roar (a feared pile ran into his front, and one
    // Shadow Flame killed nine of it), at least 75 degrees off his facing and 105 off his tail,
    // and within ANCHOR_REACH (a heal) of whoever he is on: about 38 to 42 yd from his middle at
    // 75 degrees off his facing. The tank the pile is made on only steps toward something
    // within GATHER_NEAR. A bot on his other side walks round behind him, outside the roar, in
    // steps of RING_STEP radians.
    constexpr float CONSTRUCT_PHASE_PCT = 21.0f;
    constexpr uint32 CONSTRUCTS_TO_GATHER = 4;
    constexpr float CONSTRUCT_GATHER_SPOT = 3.0f;
    constexpr float CONSTRUCT_GATHER_FAR = 7.0f;
    constexpr float CONSTRUCT_GATHER_NEAR = 9.0f;
    constexpr float CONSTRUCT_ANCHOR_FRONT = 1.3090f;  // 75 degrees
    constexpr float CONSTRUCT_ANCHOR_TAIL = 1.3090f;   // 75 degrees
    constexpr float CONSTRUCT_ANCHOR_REACH = ROAR_HEALER_REACH;
    constexpr float CONSTRUCT_RING_STEP = 0.35f;
    // Fear Ward reaches 30 yd; a priest this far from the tank or nearer walks over to cast it.
    constexpr float FEAR_WARD_RANGE = 30.0f;
    constexpr float FEAR_WARD_WALK = 70.0f;
    constexpr uint32 FEAR_WARD_RETRY_MS = 3000;
    // Tremor Totem frees its shaman's party within 30 yd of the totem. A tank's shaman puts it
    // down within TREMOR_TANK_NEAR of the tank (spots on rings TREMOR_RING apart round it), and
    // again only once it is gone or further than TREMOR_TANK_REACH from the tank.
    constexpr float TREMOR_TANK_NEAR = 22.0f;
    constexpr float TREMOR_TANK_REACH = 28.0f;
    constexpr float TREMOR_RING = 4.0f;
    // Path checks per walk to such a spot, nearest first.
    constexpr uint32 TREMOR_SPOT_TRIES = 8;
    // A non-tank Nefarian is on walks to within this of the tank that should have him.
    constexpr float NEFARIAN_LOOSE_NEAR = 4.0f;

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
    // How far round the bot the way out is looked for: from the middle of a pack that has just
    // arrived, 21 yd (the usual) is barely past the stomps, and in a corridor often nowhere.
    constexpr float WYRMGUARD_WAY_OUT_SEARCH = 36.0f;
    // Melee stand by the Wyrmguard they hit only while a tank holds it and they have this much
    // health. The stomps are looked up per bot every WYRMGUARD_STOMPS_FRESH_MS.
    constexpr float WYRMGUARD_MELEE_HEALTH_PCT = 75.0f;
    constexpr uint32 WYRMGUARD_STOMPS_FRESH_MS = 100;

    // Ground fire (BwlHazards.cpp): a bot keeps this far past its edge. A Captain's or Overseer's
    // Cleave reaches this far past its own size, this far either side of its facing. The hazards
    // are looked up per bot every HAZARD_FRESH_MS.
    constexpr float GROUND_FIRE_MARGIN = 1.5f;
    constexpr float CLEAVE_REACH = 6.0f;
    constexpr float CLEAVE_HALF_ANGLE = 1.0472f;  // 60 degrees
    constexpr float HAZARD_SPOT = 2.0f;
    constexpr uint32 HAZARD_FRESH_MS = 100;

    // Interrupts (BwlInterrupts.cpp). After a bot sends one at a caster the rest leave that
    // caster alone this long; bots this close to a caster when it starts a cast are woken to
    // look (Silencing Shot, the farthest, reaches 35 yd).
    constexpr uint32 INTERRUPT_SPREAD_MS = 400;
    constexpr float INTERRUPT_WAKE_RANGE = 40.0f;

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
    // Wyrmguards fighting the group count within this of the bot (which ones they are is looked
    // up once per instance every WYRMGUARD_CACHE_MS). One further than WYRMGUARD_RUNNING from
    // whoever it is on is still running there, and that spot counts too.
    constexpr float WYRMGUARD_SEARCH = 60.0f;
    constexpr uint32 WYRMGUARD_CACHE_MS = 250;
    constexpr float WYRMGUARD_RUNNING = 6.0f;

    // The drakes' two tanks. The one the drake isn't on walks to within DRAKE_SECOND_SPOT of
    // the one it is on when further than DRAKE_SECOND_NEAR from it, or more than
    // DRAKE_FRONT_ARC off the drake's facing. A taunt is only cast from within DRAKE_TAUNT_ARC
    // of the facing or DRAKE_TAUNT_NEAR of the tank it is on: from there it doesn't turn him.
    // Every other tank stays under DRAKE_OFFTANK_SHARE of the threat of the two.
    constexpr float DRAKE_SECOND_SPOT = 2.5f;
    constexpr float DRAKE_SECOND_NEAR = 6.0f;
    constexpr float DRAKE_FRONT_ARC = 0.7854f;  // 45 degrees
    constexpr float DRAKE_TAUNT_ARC = 0.8727f;  // 50 degrees
    constexpr float DRAKE_TAUNT_NEAR = 8.0f;
    constexpr float DRAKE_OFFTANK_SHARE = 0.8f;
    // A drake on someone who isn't a tank for this long may be taunted by any tank, holder or
    // not (2026-10-17: Firemaw spent 40 s on non-tanks because both holders "could" taunt).
    constexpr uint32 DRAKE_LOOSE_TAUNT_MS = 1500;
    // The second tank is kept for the fight; one that has held an add this long is replaced.
    constexpr uint32 DRAKE_SECOND_ADD_MS = 5000;

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

    // Nefarian and Chromaggus: no taunt lands on them, so the tanks share threat and the
    // damage dealers stay under them (see Common/TankRoles.h).
    std::vector<uint32> const& TankSplitThreatShareBosses();

    // Firemaw, Ebonroc and Flamegor: whether this tank is one of the two that hold the drake (the
    // main tank, the first spare bot off-tank in group order, or whoever it is on now).
    bool HoldsDrake(Player* tank, Creature* drake);

    // Chromaggus has started casting something: kept for the breath clock.
    void NoteChromaggusBreath(Creature* boss, uint32 spellId);

    // Whether the boss sees this unit, or a spot a player could stand on, the way the core's
    // spells judge it: small props (M2 models) don't block, and a spot is looked at from the
    // boss's own eyes to a player's height above it.
    bool SeenBy(Unit const* boss, WorldObject const* who);
    // `height`: the eyes of whoever would stand there (0: a player's usual 2 yd).
    bool SeesSpot(Unit const* boss, float x, float y, float z, float height = 0.0f);

    // Firemaw, Ebonroc, Flamegor, Nefarian (BwlPositions.cpp): whether a spot is somewhere this
    // bot keeps out of round the dragon (his front, Nefarian's tail and roar), and whether he is
    // casting Shadow Flame or Wing Buffet right now.
    bool InDragonsWay(Player* bot, Creature* boss, float x, float y);
    bool DragonBreathing(Creature* boss);

    // Whether this action walks the bot back to its place rather than to a target: playerbots'
    // follow (to its place in the formation) and "stay", mod-dungeon-clear's camp hold, follow,
    // assist, regroup and transit walks. `to` is where it ends when that is known.
    bool WalksBack(PlayerbotAI* botAI, Action* action, Position& to, bool& known);

    // The bot has set off to hide from Firemaw's Flame Buffet and holds there until the last
    // stack is gone. One answer for every bot: healers' turns, his tanks and the front rule all
    // go by it.
    bool HidingFromFiremaw(Player* bot);

    // Fixed spots for Firemaw and Chromaggus, from the config. A role's spot, or nullptr when it
    // isn't set.
    enum class Spot : uint8
    {
        Tank,        // where the tank he is on holds him
        OffTank,     // Firemaw: where his second tank stands
        TankHide,    // Firemaw: where a tank drops its stacks (else MeleeHide)
        MeleeHide,
        Ranged,      // where ranged damage dealers fight from
        RangedHide,
        Healer,      // where healers heal from (else Ranged)
        HealerHide,  // (else RangedHide)
        Count
    };
    Position const* BossSpot(uint32 bossEntry, Spot spot);
    void LoadSpots();

    // Player hooks for Blackwing Lair (the Onyxia Scale Cloak aura on revived bots).
    void AddScripts();
    // Spell hooks: bots woken out of a cast when a dragon starts its breath, the breath clock.
    void AddPositionScripts();
    // Spell hooks: bots woken to interrupt a trash cast.
    void AddInterruptScripts();
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

// --- Interrupts (BwlInterrupts.cpp) ---------------------------------------

// A trash mob fighting the raid is casting something on the list that this bot can stop now,
// and no other bot has just sent an interrupt at it.
class RcBwlInterruptTrigger : public Trigger
{
public:
    RcBwlInterruptTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl interrupt") {}
    bool IsActive() override;
};

// Interrupts from where the bot stands, and is named for it: playerbots' "passive" (which
// mod-dungeon-clear puts on the bots held at the camp) lets through only names with "stay",
// "follow" or "move from group" in them. Not a cast action, so mod-dungeon-clear's hold on
// damage at the pull doesn't take it either. On both engines, in "rc bwl runners".
class RcBwlInterruptAction : public Action
{
public:
    RcBwlInterruptAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl stay and interrupt") {}
    bool Execute(Event event) override;
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

// Named with "stay" on purpose, like the Wyrmguard one: it still runs while mod-dungeon-clear
// keeps the bot passive. Not a MovementAction: see RaidClear::WayOutWalk.
class RcBwlTechnicianSpreadAction : public Action
{
public:
    RcBwlTechnicianSpreadAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl technician stay apart"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Follow, formation and the camp hold don't walk a bot that spread out back into the clump.
class RcBwlTechnicianHoldMultiplier : public Multiplier
{
public:
    RcBwlTechnicianHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl technician hold") {}
    float GetValue(Action* action) override;
};

// --- Trash ground fire and cleaves (BwlHazards.cpp) ------------------------

// The bot stands in a Rain of Fire or Flamestrike that is burning, or (not a tank) in front of a
// Death Talon Captain or Overseer that isn't on it. On both engines, in "rc bwl runners".
class RcBwlTrashHazardTrigger : public Trigger
{
public:
    RcBwlTrashHazardTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl trash hazard") {}
    bool IsActive() override;
};

// With "stay" in the name for the passive camp hold (see the Wyrmguard one).
class RcBwlTrashHazardAction : public Action
{
public:
    RcBwlTrashHazardAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl hazard stay clear"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// --- Vaelastrasz's room: the technicians that run (BwlRunners.cpp) --------

// On both engines: nothing is fighting anyone when they set off. The rules a bot waiting at the
// camp needs before the pack reaches it (War Stomp, ground fire, interrupts) are here too.
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

// --- Line-of-sight cover and fixed spots (Firemaw, Chromaggus; BwlCover.cpp) --

// Walks to the role's hide spot from the config, or without one (or with the boss seeing it)
// to the nearest reachable spot the boss can't see, and stays there while the trigger holds.
// A searched spot is remembered so the search runs once per hide, not every tick.
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
    Position _sentTo;            // the last walk sent, and when: not sent again for a moment
    uint64 _sentAtMs = 0;
};

// While a bot is hidden, nothing else walks it back into view (its spells still go out). The
// raid mechanics' own steps (out of a breath, a bomb, a fire) and playerbots' "avoid aoe" still
// do.
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

// The bot has a fixed spot for its role in this fight (the tank the boss is on, Firemaw's second
// tank, ranged, healers) and isn't on it.
class RcBwlBossSpotTrigger : public Trigger
{
public:
    RcBwlBossSpotTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl boss spot") {}
    bool IsActive() override;
};

class RcBwlBossSpotAction : public MovementAction
{
public:
    RcBwlBossSpotAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl boss spot") {}
    bool Execute(Event event) override;
};

// On its spot, nothing but the raid mechanics walks the bot off it: not playerbots' positioning
// (tank face, rear flank, reach), not the knockback footing (a tank with its back to the wall
// isn't thrown anywhere).
class RcBwlSpotHoldMultiplier : public Multiplier
{
public:
    RcBwlSpotHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl spot hold") {}
    float GetValue(Action* action) override;

private:
    uint32 _checkedAtMs = 0;  // the answer below holds for a few ticks: every action asks
    bool _holds = false;
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

// "stay" in the name: it still runs while mod-dungeon-clear keeps the bot passive.
class RcBwlDetonationKeepAwayAction : public MovementAction
{
public:
    RcBwlDetonationKeepAwayAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl detonation stay clear") {}
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
// positioning, and nothing walks them in under Nefarian's roar. Follow and formation don't
// walk anyone who keeps out of the front back into it, and nobody charges onto the dragon from
// in front of it.
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

// A shaman whose party has one of Nefarian's tanks, and no Tremor Totem of its own within
// TREMOR_TANK_REACH of that tank.
class RcBwlNefarianTremorTrigger : public Trigger
{
public:
    RcBwlNefarianTremorTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl nefarian tremor") {}
    bool IsActive() override;
};

// Walks to within TREMOR_TANK_NEAR of the tank, outside his front and tail (in under the roar),
// and puts it down there.
class RcBwlTremorTankAction : public MovementAction
{
public:
    RcBwlTremorTankAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl tremor for tank") {}
    bool Execute(Event event) override;
};

// ...and nothing else goes in that shaman's earth slot while it keeps the tank's Tremor.
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

// Nefarian is casting Bellowing Roar, and this warrior is in its reach with Berserker Rage ready.
class RcBwlBellowingRoarTrigger : public Trigger
{
public:
    RcBwlBellowingRoarTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl bellowing roar") {}
    bool IsActive() override;
};

class RcBwlBerserkerRageAction : public Action
{
public:
    RcBwlBerserkerRageAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl berserker rage") {}
    bool Execute(Event event) override;
};

// Nefarian is on this bot, which isn't a tank (he picks whoever comes first when he lands).
class RcBwlNefarianLooseTrigger : public Trigger
{
public:
    RcBwlNefarianLooseTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl nefarian loose") {}
    bool IsActive() override;
};

// Walks to the tank that should have him, so he faces where that tank stands.
class RcBwlNefarianToTankAction : public MovementAction
{
public:
    RcBwlNefarianToTankAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl nefarian to tank") {}
    bool Execute(Event event) override;
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

// --- The drakes: who holds them (BwlPositions.cpp) -------------------------

// The drake's second tank is away from the one it is on, or not in front of the drake.
class RcBwlDrakeFrontTrigger : public Trigger
{
public:
    RcBwlDrakeFrontTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl drake front") {}
    bool IsActive() override;
};

// Walks next to the tank the drake is on.
class RcBwlDrakeFrontAction : public MovementAction
{
public:
    RcBwlDrakeFrontAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl drake front") {}
    bool Execute(Event event) override;
};

// Firemaw: the tank he is on has piled up Flame Buffet and this one, his other tank, hasn't.
class RcBwlDrakeSwapTrigger : public Trigger
{
public:
    RcBwlDrakeSwapTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl drake swap") {}
    bool IsActive() override;
};

class RcBwlDrakeSwapAction : public Action
{
public:
    RcBwlDrakeSwapAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl drake swap") {}
    bool Execute(Event event) override;
};

// Only the drake's two tanks taunt it, from in front, not during Shadow Flame; every other tank
// stays under their threat. On Firemaw one of them only takes him off the other when it has far
// fewer stacks (the swap), so the two don't hand him back and forth.
class RcBwlDrakeTauntMultiplier : public Multiplier
{
public:
    RcBwlDrakeTauntMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc bwl drake taunt") {}
    float GetValue(Action* action) override;
};

// --- Death Talon Wyrmguards: War Stomp -------------------------------------

// A bot that isn't a tank stands in the War Stomp of a Wyrmguard (or where one is running to)
// that it has no business next to. On both engines, in "rc bwl runners".
class RcBwlWyrmguardKeepAwayTrigger : public Trigger
{
public:
    RcBwlWyrmguardKeepAwayTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl wyrmguard keep away") {}
    bool IsActive() override;
};

// Named with "stay" on purpose: playerbots' "passive" strategy (mod-dungeon-clear puts it on the
// bots waiting at the camp during a pull) lets through only actions whose names contain "stay",
// "follow" or "move from group".
class RcBwlWyrmguardKeepAwayAction : public Action
{
public:
    RcBwlWyrmguardKeepAwayAction(PlayerbotAI* botAI) : Action(botAI, "rc bwl wyrmguard stay clear"), _walk(botAI) {}
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
