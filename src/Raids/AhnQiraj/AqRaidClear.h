/*
 * mod-raid-clear: Ruins of Ahn'Qiraj (map 509) and Temple of Ahn'Qiraj (map 531).
 *
 * Mechanics from the core scripts in scripts/Kalimdor/RuinsOfAhnQiraj and TempleOfAhnQiraj, and
 * the spell data.
 *
 * Both raids:
 *   - Anubisath Explode. An Anubisath Guardian (Ruins) or Defender (Temple), or any other mob
 *     that does it, can go off at 10% health: Explode (25698) is a 6 s fuse on the mob (it stuns
 *     it), then 25699 hits everyone within 20 yd for about 4,700. Every bot in reach, tanks too, walks out and stays out until it has
 *     gone off, the ZG Bat Rider rule. A bot in the middle of a cast is told to drop it the moment
 *     the fuse is lit. The Twin Emperors' bugs that Vek'lor sets to explode (Explode Bug, 804:
 *     3 s, then 26059 hits everyone within 15 yd) are kept away from the same way, by their own
 *     blast's reach.
 *
 * Ruins of Ahn'Qiraj, Ossirian the Unscarred:
 *   - He fights with Strength of Ossirian (25176): +300% damage, so his War Stomp (25188, 20 yd)
 *     hits for 6,000-8,000 and his swings kill a cloth wearer in two. A crystal (GO 180619) takes
 *     it off: using one while he is within its reach (the weakness spells' 30 yd area round the
 *     crystal) puts a weakness on him for the spell's duration, and the moment no weakness is
 *     left the script gives him Strength again. Each use spawns the next crystal somewhere else
 *     in the room.
 *     Playerbots' "aq20" strategy sends every bot of the raid to the crystal nearest him and has
 *     each of them use it. Replaced here by two roles:
 *       - The tank he is on leads him to the next crystal in time: when he has Strength, or when
 *         his weakness has less left than the walk takes plus a margin. It walks to the crystal
 *         and waits there; he follows his victim. A crystal the aggro leash keeps stopping the
 *         walk to is passed over for a while.
 *       - One bot uses the crystal: the damage dealer nearest to it (a healer only when no
 *         damage dealer can), picked once for the crystal. The tank stops short of it, so the
 *         two aren't in one spot. It walks there ahead of him, waits, and uses it as soon as he is in reach and
 *         has Strength or less than CLICK_AHEAD of weakness left. A bot that can't get there is
 *         replaced for a while. After a use the plan stays on that crystal until the weakness has
 *         landed (the crystal's trigger casts it), so the tank doesn't take him out of its reach
 *         first.
 *     Everyone else fights on; playerbots' own crystal walk is switched off.
 *
 * Ruins of Ahn'Qiraj, Kurinnaxx:
 *   - Sand Trap. Every 5-15 s he puts a Sand Trap (GO 180647) under a random raid member; 5 s
 *     later it goes off: 25656 hits everyone within 8 yd of it for about 2,000 nature, with 75%
 *     less chance to hit and a silence for 20 s. Every bot within its reach, tanks too, walks out
 *     while the fuse burns and walks toward nothing inside it. The trap is gone once it has
 *     gone off.
 *
 * Ruins of Ahn'Qiraj, Moam:
 *   - Arcane Eruption. Every 2-6 s he drains up to six mana users (anywhere within 100 yd) of
 *     500 mana and gains twice that; in his stone phase he gains 500 a second on top. The moment
 *     his mana is full he blows it all on Arcane Eruption: 3,000-4,000 arcane to everyone within
 *     45 yd and a knockback. Hunters keep their Viper Sting on him, priests that aren't healing
 *     Mana Burn him and warlocks Drain Mana, so he never fills up.
 *   - Mana Fiends. His stone phase lasts until the three Mana Fiends are dead (or 90 s); each
 *     fiend casts Arcane Explosion round itself every 6 s. Skull goes on them first.
 *
 * Ruins of Ahn'Qiraj, Ayamiss the Hunter:
 *   - Paralyze. Every 15-28 s she paralyzes a random raid member and takes it to the altar, and
 *     a Hive'Zara Larva crawls there; the moment it arrives it eats that player (Feed: dead).
 *     The larva is passive and not in the fight, so playerbots never looks at it. Every damage
 *     dealer in reach goes for the larva nearest the altar and stays on it until it is dead.
 *
 * Temple of Ahn'Qiraj, the Prophet Skeram:
 *   - True Fulfillment (785). Every 20-30 s he mind-controls the raid member second nearest to
 *     him for 20 s, and it does four times its damage meanwhile (+300%). In one bot raid the
 *     bots he took killed four others with their own area spells: playerbots went on running
 *     them against their new enemies, the raid. Such a member does nothing of its own, is
 *     stopped with a Polymorph or a Hammer of Justice and kept out of the raid's area spells
 *     (Common/Charmed.h), and a bot or pet on it lets go.
 *   - Interrupts. His Arcane Explosion (26192: a 1 s cast, 30 yd round him) and True Fulfillment
 *     (a 0.5 s cast) can both be interrupted, and he isn't immune to it. Any bot with an interrupt
 *     in reach stops either, his target or not (a tank only when he is on that tank), True
 *     Fulfillment first; one bot per cast, the next only if the cast is still going a moment
 *     later. The last interrupt ready in reach of him is kept for True Fulfillment: in one bot
 *     raid all five went through, every interrupt spent on an Arcane Explosion just before (and
 *     playerbots' own Kick at its target waits the same way). A bot that is casting doesn't think until its cast is done, so the moment he starts
 *     one the bots that could stop it look at once, and if none of those is free, the nearest
 *     one in the middle of a cast drops it. (A bot channelling a spell can't be asked: playerbots
 *     reports none of its spells as ready until the channel ends.)
 *   - Images. At 75%, 50% and 25% he splits into himself and two images, with 10%, 20% and 50% of
 *     his health (at his health share), that cast all he casts. Skull goes on the images before
 *     him: each has a fraction of what he has left. Who is who is the server's knowledge (an
 *     image is his summon), the same as a player reading the health bars.
 *
 * Temple of Ahn'Qiraj, Battleguard Sartura and her Royal Guards:
 *   - Whirlwind. Sartura whirlwinds for 15 s (26083: 10 yd round her, every second, immune to
 *     stuns meanwhile), each guard for 8 s (26038, 10 yd); all the while they turn to a random
 *     raid member every 2-7 s. She runs at 15 yd a second, so there is no outrunning her. Every
 *     bot she or a guard isn't on, tanks too, walks out of a whirlwind's reach and toward nothing
 *     inside it.
 *   - Stuns. Outside her whirlwind she can be stunned, and a stun when one is due makes the
 *     whirlwind fail. The guards can be stunned in theirs. A bot with a stun in reach (Hammer of
 *     Justice, Concussion Blow, Kidney Shot, Bash, Maim) puts it on a whirlwinding guard, else on
 *     Sartura, else on a guard, whenever none is on it; one bot at a time per target, and none
 *     on a target that diminishing returns would make immune or shorten to a quarter. (A guard
 *     in its whirlwind is mostly stunned by the bot it is on: everyone else keeps out of reach.)
 *   - The guards die before her: they go in the kill order.
 *
 * Temple of Ahn'Qiraj, Fankriss the Unyielding:
 *   - Spawn of Fankriss. Every 22-70 s he sends one to three worms; 30 s after it appears a worm
 *     goes Berserk (500% damage, immune to taunts). Each one gets an off-tank of its own (Common/
 *     TankRoles.h) and skull before anything else; the Vekniss Hatchlings come next.
 *   - Mortal Wound (25646). Every 7-14 s he puts a stack on his victim: 10% less healing taken
 *     each, for 15 s, up to 10. The hits come faster than a stack wears off, so on one tank they
 *     only ever add up. Two tanks take him in turns, the main tank and the first bot off-tank
 *     that isn't holding a worm (it builds threat on him meanwhile and keeps him once he is on
 *     it, Common/TankRoles.h): when the one he is on has MORTAL_WOUND_SWAP_STACKS, the other
 *     taunts him as soon as its own stacks are gone. No other taunt takes him off one of the two
 *     (playerbots' main tank would taunt him straight back, a paladin with Righteous Defense on
 *     the other tank), and the one waiting its turn keeps its spells under 90% of the other's
 *     threat. If he ends up on some other tank (his main tank died, and a worm's tank caught him)
 *     or the off-tank he is on goes for a worm (with only two tanks, nobody else takes it),
 *     whichever of the two has fewer stacks taunts him at once. Three tanks or more keep the
 *     swap clean.
 *
 * Temple of Ahn'Qiraj, the Bug Trio (Princess Yauj, Lord Kri, Vem):
 *   - Kill order. A bug that drops while another is alive is eaten by the others, who get their
 *     health back; Yauj leaves ten Yauj Broods, Kri a Poison Cloud, and Vem's death enrages the
 *     other two (Vengeance). Skull: Broods, then Kri (his Toxic Volley needs the most cures),
 *     then Yauj, then Vem: the raid guide's easiest order. A bug that is being eaten can't be hurt
 *     and one on its way to eat gets its health back when it arrives: skull leaves both meanwhile.
 *   - Fixed spots (config RaidClear.TempleOfAhnQiraj.BugTrio.*): the tank a bug is on holds it
 *     at that bug's spot, so the three are tanked far apart and Vem sits in an alcove where only
 *     his tank is in his sight (his Charge goes to a random player out of his melee reach that he
 *     can see). Ranged bots and healers stand at their spots when those are set. No spot: as
 *     before.
 *   - Great Heal. Yauj's heal (25807, 2 s) is interrupted by any bot that can reach her.
 *   - Fear. Every 20 s Yauj fears everyone within 45 yd and wipes her threat. Shamans keep
 *     Tremor Totem down while she is alive.
 *   - Poison Cloud. Kri dying leaves a Poison Cloud (creature 15933) that drops a Toxin cloud
 *     (25989, 5 yd, about 1,300 nature every 2 s and a slow) round itself every 10 s, the same
 *     cloud as Viscidus' slimes. Bots walk out of them, and toward nothing standing in one, as
 *     at Viscidus.
 *   - Vekniss Guardians. Their Impale throws everyone within 10 yd: off-tanks keep them 10 yd
 *     from the other tanks' mobs, and skull takes them one at a time.
 *
 * Temple of Ahn'Qiraj, Viscidus:
 *   - Frost. 200 hits of frost damage freeze him for 15 s (100 slow him, 150 slow him more); in
 *     that time 150 hits shatter him into globs. Playerbots' damage dealers cast whatever their
 *     spec does, so an arcane mage never adds a single frost hit. While he is neither frozen nor
 *     in globs, every damage dealer that has one casts its frost spell at him: a mage Frostbolt
 *     (rank 1 when it has it: 1.5 s instead of 3), a shaman Frost Shock, a death knight Howling
 *     Blast or Icy Touch. Healers heal.
 *   - Toxic Slime. Every 15-20 s a slime appears under a random raid member and, every 10 s,
 *     leaves a Toxin cloud (25989) around itself: 1,300 nature a tick and a slow, 5 yd across.
 *     Playerbots only steps out once the bot has the debuff, and straight back, into the next
 *     cloud. Here a bot standing at a slime's cloud walks to the nearest spot clear of all of
 *     them, and walks toward nothing inside one (but him while he is frozen: the melee hits are
 *     what shatter him). The tank he is on walks him out of a cloud when he can follow, but
 *     never toward the room's entrance, where the script makes him evade.
 *   - Globs. When he shatters (at 1% health, with a glob for every 5% he had), the Globs of
 *     Viscidus crawl back to the middle of the room, faster every second, and each one that gets
 *     there gives him 5% back. Skull on one glob at a time let 12 of 16 through. Now each damage
 *     dealer takes a glob of its own: melee the nearest, ranged bots the one nearest the middle
 *     that they can reach, each counting every other raid member on a glob against it
 *     (GLOB_CROWD_YARDS), so they spread over all of them; nothing else is targeted until they are
 *     gone. A bot with an instant area spell that would catch three globs or more (Arcane
 *     Explosion, Chain Lightning, Multi-Shot, Whirlwind, Cleave, Thunder Clap, Blood Boil, Holy
 *     Nova, Consecration, Divine Storm, Swipe) casts that first. Skull still marks them.
 *
 * Temple of Ahn'Qiraj, nature damage (Viscidus, Princess Huhuran, the Bug Trio):
 *   - One shaman keeps Nature Resistance Totem down and one hunter keeps Aspect of the Wild up
 *     (raid auras, 30 yd, that don't stack with each other); that shaman places nothing else in
 *     the air slot and that hunter switches to no other aspect but Viper, below 20% mana (and
 *     back to the Wild above 60%).
 *
 * Temple of Ahn'Qiraj, Princess Huhuran: see Aq40Huhuran.cpp (no cures on Wyvern Sting; out of
 * Poison Bolt's 15 nearest from Berserk).
 *
 * Temple of Ahn'Qiraj, Anubisath Defenders: see Aq40Defenders.cpp (out of Thunderclap, into
 * Shadow Storm's dead zone, a plagued bot walks clear of everyone).
 *
 * Temple of Ahn'Qiraj, the Prophet Skeram, by place (Aq40Spots.cpp):
 *   - He blinks between three platforms (4801, 8195, 20449: their spots are in the core's
 *     spell_target_position) and wipes his threat each time; whoever he is on and doesn't hit
 *     him back takes an Earth Shock every 1.2 s. The raid guide has a tank on every platform.
 *     The tank he is on holds him on his own platform (the one nearest his spawn, or
 *     RaidClear.TempleOfAhnQiraj.Skeram.TankSpot), so after a blink he walks back to it. The
 *     first two off-tanks in group order each stand on one of the other two platforms while
 *     nothing is hitting them; the images that land there go to them as any loose mob does.
 *     When Skeram himself lands on an off-tank's platform and isn't on a tank, that off-tank
 *     taunts him and brings him back to the middle. Ranged bots and healers stand at
 *     Skeram.RangedSpot / Skeram.HealerSpot when those are set.
 *
 * Temple of Ahn'Qiraj, the Twin Emperors (Vek'lor, Vek'nilash):
 *   - Each takes the other's damage as a share of health (Twin Empathy), and within 60 yd of each
 *     other each heals the other (Heal Brother, every 3.6 s). Every 30-40 s Vek'lor teleports the
 *     two into each other's place (Twin Teleport, 799), wipes both threat lists and each attacks
 *     the nearest unit with 2,000 threat. Neither can be taunted. Vek'lor takes no physical
 *     damage, keeps up to 45 yd from whoever he is on and Shadow Bolts it (3,600), Blizzards a
 *     random player near him (26607, 10 yd), Arcane Bursts (568, 10 yd, about 4,000) whenever a
 *     player is within 5 yd of him and sets bugs to explode. Vek'nilash takes nothing but
 *     physical and holy damage and Uppercuts a random player in his reach.
 *   - In one bot raid the twins stood together the whole fight (126 Heal Brothers each), four
 *     tanks and the melee stood on Vek'lor (Arcane Burst killed 21) and his Shadow Bolts went to
 *     hunters and moonkins. Now:
 *       - Two tanks hold the twins: the first two tank bots in group order, paladins first (holy
 *         is all a tank has for Vek'lor). Vek'nilash's is the one he is on, else the nearer; the
 *         other holds Vek'lor from 6.5-9.5 yd edge to edge (Judgement's reach, out of the Burst)
 *         and casts Avenger's Shield and Exorcism at him besides. Neither picks another target.
 *         For 2.5 s after a teleport (or while either twin is still rooted or passive, keeping
 *         its old victim) the two are told apart by who is nearer which twin, and both stand
 *         still: each is then the nearest unit its new twin finds.
 *       - The tank Vek'nilash is on walks him away from Vek'lor whenever they are within his
 *         heal range. Vek'lor himself only moves when his victim is 45 yd away, and a teleport
 *         swaps the two without bringing them closer: held where they land, they stay apart.
 *       - Melee bots, the other tanks and hunters hit Vek'nilash only; casters and healers Vek'lor
 *         only (a skull on the wrong one is passed over). Casters keep their threat on Vek'lor
 *         under 115% of his victim's (his tank or not): away from him he only turns to someone
 *         with 130% of it. Not while his victim has under 1,500 (the pull).
 *       - Nobody but his tank comes within 12 yd of Vek'lor (edge to edge); ranged bots and
 *         healers stay 8 yd from Vek'nilash; everyone walks out of a Blizzard.
 *       - The teleport clock (from Vek'lor's cast, the first 30 s in): from 1 s before the
 *         earliest one can come until it comes (at most 1 s past the latest), melee bots and
 *         other tanks but the one Vek'nilash is on step 6 yd back from him and stay, and pets
 *         are called to their owners (passive until the twins have landed; the module's sweep
 *         gives a pet back its react state once its owner is out of the fight). Vek'lor then lands on Vek'nilash's tank, the
 *         nearest, which becomes his tank; Vek'nilash lands next to Vek'lor's tank, nearer than
 *         anyone else, which becomes his. A pet that could only hit Vek'lor (all but an Imp or a
 *         Water Elemental) stays by its owner all fight.
 *       - Bugs Vek'lor sets to explode are kept away from as an Anubisath's Explode; that walk
 *         keeps out of Vek'lor's 12 yd and the twins' walks keep out of the blast.
 *       - Fixed spots (RaidClear.TempleOfAhnQiraj.TwinEmperors.TankSpotA / TankSpotB, by the two
 *         torches as in the raid guide, more than 60 yd apart): with both set, the tank
 *         Vek'nilash is on walks him to his spot instead of just away from Vek'lor, and holds
 *         him there: the one farther from Vek'lor when first looked at, the other one after each
 *         teleport (picked again, it would turn as Vek'lor follows his victim about). Not while
 *         a Blizzard or a bug's blast is on it. Vek'lor lands where Vek'nilash stood, so after the first
 *         teleport both twins sit on the two spots.
 *     Not solved: the pull itself (both run to the puller; the first teleport sorts it out), a
 *     twin landing on a pet or totem that is nearer than its tank, and Vek'nilash's mutated
 *     bugs, left to the off-tanks.
 *
 * Temple of Ahn'Qiraj, the Anubisath Sentinels (the four before Skeram, and the rest):
 *   - Each one rolls one of nine abilities as the pack is pulled (an aura on it, so it shows at
 *     once), and as one dies the others get its ability and half their health back. The raid
 *     guide kills them in an order that passes the dangerous abilities on last: Fire/Arcane
 *     reflect, Shadow/Frost reflect, Mending, Knock Away, Thorns, Mortal Strike, Mana Burn,
 *     Thunderclap, Shadow Storm. Skull goes by each one's own ability (the one it passes on;
 *     those it got from a dead one all the others got too): the earliest in that list dies first.
 *   - Thunderclap (8732, 25 yd) and Shadow Storm (26546: everyone within 45 yd but the ones
 *     within 25) are the Defenders' spells: ranged bots and healers stand outside a Thunderclap
 *     Sentinel's reach and inside a Shadow Storm one's dead zone, as at the Defenders, from the
 *     pull on. One that has both (a death passed one on) is left alone: no spot is out of both.
 *     A Thunderclap one tanked nearly on a Shadow Storm one (no room keeps to both) gives way to
 *     the Storm.
 *   - Not done: Mana Burn (40 yd round it, nowhere to stand), Thorns and Knock Away (melee and
 *     tanks fight them as they are).
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_AHN_QIRAJ_H
#define MOD_RAID_CLEAR_AHN_QIRAJ_H

#include "Common/Charmed.h"
#include "Common/KillOrder.h"
#include "Common/TankRoles.h"
#include "Common/WayOut.h"

#include "Action.h"
#include "AttackAction.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "Strategy.h"
#include "Trigger.h"

#include <vector>

class Creature;
class GameObject;
class Player;
class Unit;

namespace RaidClear::AhnQiraj
{
    constexpr uint32 MAP_AQ20 = 509;
    constexpr uint32 MAP_AQ40 = 531;

    // --- Anubisath Explode (both raids) -------------------------------------
    constexpr uint32 SPELL_EXPLODE_FUSE = 25698;   // 6 s on the mob, then
    constexpr uint32 SPELL_EXPLODE_BLAST = 25699;  // 20 yd around it
    // A bot closer than blast radius + KEEP_AWAY_EXTRA to a lit mob leaves, to blast radius +
    // SPOT_EXTRA. Bots within blast radius + HOLD_EXTRA walk toward nothing inside the blast.
    constexpr float EXPLODE_KEEP_AWAY_EXTRA = 4.0f;
    constexpr float EXPLODE_SPOT_EXTRA = 5.0f;
    constexpr float EXPLODE_HOLD_EXTRA = 14.0f;
    constexpr float EXPLODE_WORTH_IT = 2.0f;
    // Vek'lor's Explode Bug (Twin Emperors): a Qiraji Scarab or Scorpion with this aura goes off 3 s
    // later (its periodic trigger), 26059: about 2,800 to everyone within 15 yd, and dies.
    constexpr uint32 SPELL_EXPLODE_BUG = 804;
    constexpr uint32 SPELL_EXPLODE_BUG_BLAST = 26059;

    // The blast's reach, centre to centre, from the spell data (20 yd on a stock server).
    float ExplodeRadius();
    // An exploding bug's blast, the same way (15 yd).
    float ExplodeBugRadius();

    // A blast about to go off among the mobs fighting the raid (an Explode fuse, an exploding
    // bug): where, and how far it reaches, centre to centre.
    struct LitBlast
    {
        float x;
        float y;
        float z;
        float radius;
    };
    std::vector<LitBlast> LitBlasts(PlayerbotAI* botAI);

    // An area spell's reach from its effect 0, or `fallback` when the spell data has none.
    float SpellRadius(uint32 spellId, float fallback);

    // The enemy an action would walk the bot toward, on foot or with Charge, Intercept or Feral
    // Charge (which would undo a walk out in one cast), or nullptr: picking a target isn't
    // movement, and a healer still walks to whoever it has to heal. What the hold multipliers
    // look at.
    Unit* WalkTarget(Action* action, Player* bot);

    void AddScripts();
}

namespace RaidClear::RuinsOfAhnQiraj
{
    constexpr uint32 MAP_ID = AhnQiraj::MAP_AQ20;

    enum : uint32
    {
        NPC_OSSIRIAN = 15339,
        GO_OSSIRIAN_CRYSTAL = 180619,
        SPELL_STRENGTH_OF_OSSIRIAN = 25176,
        SPELL_FIRE_WEAKNESS = 25177,  // all five: same reach and length
    };
    constexpr uint32 OSSIRIAN_WEAKNESSES[] = { 25177, 25178, 25180, 25181, 25183 };

    // How far round him crystals are looked for (the room is about 300 yd across).
    constexpr float CRYSTAL_SEARCH = 250.0f;
    // He counts as in a crystal's reach with his centre this much inside the weakness area.
    constexpr float CRYSTAL_REACH_MARGIN = 3.0f;
    // The leading tank stops this close to the crystal; the bot that uses it stands this close.
    constexpr float LEAD_ARRIVED = 3.0f;
    constexpr float CLICK_STAND = 3.0f;
    // The tank sets off when his weakness has less left than the walk plus this.
    constexpr uint32 LEAD_SLACK_MS = 8000;
    // How fast the walk is taken to be (a running player).
    constexpr float LEAD_SPEED = 7.0f;
    // The bot that uses the crystal sets off this much earlier than the tank.
    constexpr uint32 CLICK_SLACK_MS = 6000;
    // It uses the crystal once he is in reach and has less than this of weakness left.
    constexpr uint32 CLICK_AHEAD_MS = 15000;
    // A walk to a crystal standing still LEAD_STUCK_MS after it was sent is sent again; after
    // WALK_TRIES sends in a row that each brought the bot less than WALK_PROGRESS closer, the
    // crystal (for the tank) or the bot (for the use) is passed over for CRYSTAL_BLOCKED_MS.
    constexpr uint32 CRYSTAL_BLOCKED_MS = 15000;
    constexpr uint32 LEAD_STUCK_MS = 2000;
    constexpr float WALK_PROGRESS = 1.0f;
    constexpr uint8 WALK_TRIES = 2;
    // After a use the plan stays on that crystal while its trigger casts the weakness: until his
    // weakness has gone up by USE_LANDED_MS, for at most USE_GRACE_MS.
    constexpr uint32 USE_GRACE_MS = 6000;
    constexpr uint32 USE_LANDED_MS = 5000;

    // --- Kurinnaxx ------------------------------------------------------
    enum : uint32
    {
        NPC_KURINNAXX = 15348,
        GO_SAND_TRAP = 180647,
        SPELL_SAND_TRAP_SUMMON = 25648,  // the trap's life (60 s)
        SPELL_SAND_TRAP = 25656,         // what goes off, 8 yd round the trap
    };
    // The trap goes off this long after it appears; a bot treats it as live for a second more.
    constexpr uint32 SAND_TRAP_FUSE_S = 5;
    constexpr uint32 SAND_TRAP_SLACK_S = 1;
    // A bot closer than blast radius + KEEP_EXTRA to a live trap leaves, to blast radius +
    // SPOT_EXTRA; within blast radius + HOLD_EXTRA it walks toward nothing inside one.
    constexpr float SAND_TRAP_KEEP_EXTRA = 2.5f;
    constexpr float SAND_TRAP_SPOT_EXTRA = 5.0f;
    constexpr float SAND_TRAP_HOLD_EXTRA = 12.0f;
    constexpr float SAND_TRAP_WORTH_IT = 2.0f;
    // Traps are looked for this far round him.
    constexpr float SAND_TRAP_SEARCH = 100.0f;

    // --- Moam -----------------------------------------------------------
    enum : uint32
    {
        NPC_MOAM = 15340,
        NPC_MANA_FIEND = 15527,
    };
    // Below this much mana there is nothing worth draining.
    constexpr float MOAM_DRAIN_MIN_PCT = 5.0f;

    // --- Ayamiss --------------------------------------------------------
    enum : uint32
    {
        NPC_AYAMISS = 15369,
        NPC_HIVEZARA_LARVA = 15555,
    };
    // Where the larvae crawl to, and the paralyzed player is taken (the script's AltarPos).
    constexpr float ALTAR_X = -9717.18f;
    constexpr float ALTAR_Y = 1517.72f;
    // Larvae are looked for this far round her; a damage dealer this close to one goes for it.
    constexpr float LARVA_SEARCH = 120.0f;
    constexpr float LARVA_JOIN = 60.0f;

    std::vector<KillOrderEntry> const& KillOrder();

    // A bot's walk to a crystal, kept by the action that sends it.
    struct CrystalWalk
    {
        ObjectGuid to;
        uint32 sentAtMs = 0;
        float sentDist = 0.0f;
        uint8 fruitless = 0;
    };

}

namespace RaidClear::TempleOfAhnQiraj
{
    constexpr uint32 MAP_ID = AhnQiraj::MAP_AQ40;

    enum : uint32
    {
        NPC_VISCIDUS = 15299,
        NPC_GLOB_OF_VISCIDUS = 15667,
        NPC_TOXIC_SLIME = 15925,
        SPELL_VISCIDUS_FREEZE = 25937,
        SPELL_VISCIDUS_INVIS = 25905,  // shattered into globs (from 3 s in)
        SPELL_VISCIDUS_STUN_SELF = 25900,  // shattered, until the last glob is gone
        SPELL_TOXIN_CLOUD = 25989,
        SPELL_FROSTBOLT_RANK_1 = 116,
    };

    // The script makes him evade within 10 yd of this spot, the top of the entrance ramp. A tank
    // walking him out of a cloud keeps him this far from it.
    constexpr float RESET_X = -7992.0f;
    constexpr float RESET_Y = 1041.0f;
    constexpr float RESET_KEEP_AWAY = 30.0f;

    // Clear of a Toxin cloud: the gap from the bot's centre to the cloud's edge. A bot with less
    // than KEEP walks to a spot with SPOT; one with less than HOLD walks toward nothing inside a
    // cloud.
    constexpr float TOXIN_KEEP = 2.0f;
    constexpr float TOXIN_SPOT = 4.0f;
    constexpr float TOXIN_WORTH_IT = 1.5f;
    constexpr float TOXIN_SEARCH = 15.0f;
    // Clouds and slimes are looked for this far round him (his room).
    constexpr float TOXIN_ROOM_SEARCH = 100.0f;
    // The tank he is on walks him out when his centre is this close to a cloud's edge.
    constexpr float TOXIN_BOSS_KEEP = 1.0f;
    // A melee bot stepping out of a cloud next to him while he is frozen stays this much inside
    // its melee reach.
    constexpr float MELEE_SLACK = 0.5f;

    // Globs: the middle of the room, where the script sends them (and him).
    constexpr float VISCIDUS_ROOM_X = -7992.36f;
    constexpr float VISCIDUS_ROOM_Y = 908.19f;
    // A damage dealer picks the glob that costs it least: for melee the yards to it, for ranged
    // bots its yards from the middle (the nearest arrives first); plus GLOB_CROWD_YARDS for every
    // other raid member already on it.
    constexpr float GLOB_CROWD_YARDS = 8.0f;
    // Ranged bots: a glob out of casting range costs this much more.
    constexpr float GLOB_OUT_OF_RANGE_YARDS = 40.0f;
    // An area spell that hits at least this many globs goes before the bot's single-target one.
    constexpr uint32 GLOB_AOE_MIN = 3;

    // A Toxin cloud on the floor (25989), or one to come: Viscidus' Toxic Slimes and Lord Kri's
    // Poison Cloud each leave a new one round themselves every 10 s.
    struct ToxinCloud
    {
        float x;
        float y;
        float radius;
    };

    // Every Toxin cloud within `range` of `centre`, and one round every living `dropper`.
    std::vector<ToxinCloud> FindToxinClouds(WorldObject const* centre, uint32 dropper, float range);

    // Yards from a spot to the edge of the nearest cloud (less than 0 inside one).
    float ToxinGap(std::vector<ToxinCloud> const& clouds, float x, float y);
    float ToxinGap(std::vector<ToxinCloud> const& clouds, Position const* pos);

    // --- Sartura ---------------------------------------------------------
    enum : uint32
    {
        NPC_SARTURA = 15516,
        NPC_SARTURA_ROYAL_GUARD = 15984,
        SPELL_SARTURA_WHIRLWIND = 26083,      // 15 s, stun immune
        SPELL_SARTURA_WHIRLWIND_HIT = 26084,  // every second, 10 yd
        SPELL_GUARD_WHIRLWIND = 26038,        // 8 s
        SPELL_GUARD_WHIRLWIND_HIT = 26686,    // every second, 10 yd
    };
    // A bot closer than a whirlwind's reach + KEEP_EXTRA (centre to centre) leaves, to reach +
    // SPOT_EXTRA; within reach + HOLD_EXTRA it walks toward nothing inside one.
    constexpr float WHIRLWIND_KEEP_EXTRA = 4.0f;
    constexpr float WHIRLWIND_SPOT_EXTRA = 8.0f;
    constexpr float WHIRLWIND_HOLD_EXTRA = 20.0f;
    constexpr float WHIRLWIND_WORTH_IT = 2.0f;
    // Whirlwinders are looked for this far round the bot (her room).
    constexpr float WHIRLWIND_SEARCH = 80.0f;
    // One stun per target at a time: after a bot sends one, the rest leave that target alone
    // this long, so the stuns go out one after another instead of all at once.
    constexpr uint32 STUN_SPREAD_MS = 1500;

    // --- Skeram -----------------------------------------------------------
    enum : uint32
    {
        NPC_SKERAM = 15263,  // his images too
        SPELL_SKERAM_ARCANE_EXPLOSION = 26192,
        SPELL_TRUE_FULFILLMENT = 785,
        // His Blink, one per platform (spell_target_position).
        SPELL_SKERAM_BLINK_1 = 4801,
        SPELL_SKERAM_BLINK_2 = 8195,
        SPELL_SKERAM_BLINK_3 = 20449,
    };
    // An off-tank counts as on its platform within POST_NEAR of the spot, and takes Skeram himself
    // there when he is within PICKUP_RANGE of it and on someone who isn't a tank.
    constexpr float SKERAM_POST_NEAR = 6.0f;
    constexpr float SKERAM_PICKUP_RANGE = 15.0f;
    // One interrupt per cast: after a bot sends one, the rest leave that Skeram alone this long.
    constexpr uint32 SKERAM_INTERRUPT_SPREAD_MS = 400;
    // Bots this close to him when he starts a cast are woken to interrupt it (the farthest
    // interrupt, Silencing Shot, reaches 35 yd).
    constexpr float SKERAM_WAKE_RANGE = 40.0f;
    // His images are looked for this far round him (his room).
    constexpr float SKERAM_IMAGE_SEARCH = 100.0f;

    // Skeram himself while one of his images is alive and can be hit: skull waits.
    bool SkeramWaitsForImages(Unit const* unit);
    void AddSkeramScripts();

    // --- Fankriss --------------------------------------------------------
    enum : uint32
    {
        NPC_FANKRISS = 15510,
        NPC_SPAWN_OF_FANKRISS = 15630,
        NPC_VEKNISS_HATCHLING = 15962,
        SPELL_MORTAL_WOUND = 25646,
    };
    // The tank he is on hands him over at this many stacks (of 10): 40% less healing taken.
    constexpr uint8 MORTAL_WOUND_SWAP_STACKS = 4;
    // Challenging Shout and Challenging Roar take everything this close to the tank; area threat
    // (Consecration, Thunder Clap) reaches about as far.
    constexpr float CHALLENGING_TAUNT_REACH = 10.0f;
    // The one of his two tanks he isn't on stays under this share of the other's threat, so it
    // doesn't take him back before the swap is due.
    constexpr float MORTAL_WOUND_WAITING_SHARE = 0.9f;

    // --- Bug Trio --------------------------------------------------------
    enum : uint32
    {
        NPC_YAUJ = 15543,
        NPC_KRI = 15511,
        NPC_VEM = 15544,
        NPC_YAUJ_BROOD = 15621,
        NPC_POISON_CLOUD = 15933,  // drops Toxin clouds like a Toxic Slime
        NPC_VEKNISS_GUARDIAN = 15233,
        SPELL_YAUJ_GREAT_HEAL = 25807,
        SPELL_POISON_CLOUD_SUMMON = 26590,  // Kri, as he dies
        SPELL_GUARDIAN_IMPALE = 26025,      // 10 yd round the guardian
    };
    // Kri's clouds are looked for this far round the bugs (their room); bots keep out of them as
    // out of Viscidus' (TOXIN_KEEP and the rest).
    constexpr float POISON_CLOUD_SEARCH = 100.0f;
    // Tremor Totem goes down while Yauj is alive and this close.
    constexpr float YAUJ_FEAR_RANGE = 50.0f;

    // Kri's Poison Clouds in the bot's copy of the room (a fresh look at most every 200 ms).
    std::vector<ToxinCloud> const& BugTrioClouds(Player* bot);

    // --- Twin Emperors ----------------------------------------------------
    enum : uint32
    {
        NPC_VEKLOR = 15276,     // the caster: immune to physical damage
        NPC_VEKNILASH = 15275,  // the fighter: immune to every school but physical and holy
        NPC_QIRAJI_SCARAB = 15316,
        NPC_QIRAJI_SCORPION = 15317,
        SPELL_TWIN_TELEPORT = 799,       // Vek'lor's: the two swap places
        SPELL_TWIN_TELEPORT_STUN = 800,  // on both, as they land
        SPELL_HEAL_BROTHER = 7393,
        SPELL_ARCANE_BURST = 568,
        SPELL_TWINS_BLIZZARD = 26607,
    };
    // Heal Brother goes out every 3.6 s while the two are within 60 yd, which the script measures
    // edge to edge (both combat reaches added on). The tank Vek'nilash is on walks him away from
    // Vek'lor when they are closer than APART_KEEP centre to centre, to APART_WANT.
    constexpr float TWINS_HEAL_RANGE = 60.0f;
    constexpr float TWINS_APART_MARGIN = 3.0f;
    constexpr float TWINS_APART_EXTRA = 8.0f;
    // Vek'lor casts Arcane Burst (10 yd round him, about 4,000) whenever a player is within 5 yd of
    // him, edge to edge. Nobody comes that close: his tank holds him from VEKLOR_TANK_NEAR to
    // VEKLOR_TANK_FAR (edge to edge: in reach of Judgement, out of the Burst), everyone else stays
    // VEKLOR_KEEP away. The tank being the nearest also makes it the one Vek'nilash picks when he
    // lands there after a teleport.
    constexpr float VEKLOR_TANK_NEAR = 6.5f;
    constexpr float VEKLOR_TANK_FAR = 9.5f;
    constexpr float VEKLOR_KEEP = 12.0f;
    // Uppercut goes to a random player within Vek'nilash's melee reach (4.5 yd, edge to edge):
    // ranged bots and healers stay VEKNILASH_KEEP away.
    constexpr float VEKNILASH_KEEP = 8.0f;
    // He teleports 30 s into the fight and every 30-40 s after; Vek'lor lands where Vek'nilash
    // stood and attacks the nearest unit. From TELEPORT_LEAD_MS before the earliest one until it
    // comes (or TELEPORT_LATE_MS after the latest), melee bots that aren't tanks stand back
    // VEKNILASH_DUE_KEEP (edge to edge, out of the Burst's reach), so his tank is the nearest.
    constexpr uint32 TWINS_TELEPORT_MS = 30000;
    constexpr uint32 TWINS_TELEPORT_MAX_MS = 40000;
    constexpr uint32 TWINS_TELEPORT_LEAD_MS = 1000;
    constexpr uint32 TWINS_TELEPORT_LATE_MS = 1000;
    // For this long after a teleport (and while either is still rooted or passive) the two are
    // landing: they keep their old victims until they turn to the nearest unit. The two tanks
    // are then told apart by who is nearer which twin, and stand still.
    constexpr uint32 TWINS_LANDING_MS = 2500;
    constexpr float VEKNILASH_DUE_KEEP = 6.0f;
    // Out of a Blizzard: the gap from the bot's edge to the cloud's.
    constexpr float BLIZZARD_KEEP = 1.0f;
    // A bot walks to the nearest spot with this much to spare on every rule above.
    constexpr float TWINS_SPOT = 1.0f;
    constexpr float TWINS_WORTH_IT = 1.0f;
    constexpr float TWINS_SEARCH = 24.0f;
    // A bot's damage on Vek'lor stays under this share of his victim's threat: out of melee range
    // he only turns to someone with 130% of it. Not while his victim has less than
    // VEKLOR_THREAT_FLOOR (the pull, nobody has built any): under the 2,000 the script gives
    // whoever he lands on, so that grant is still kept.
    constexpr float VEKLOR_THREAT_SHARE = 1.15f;
    constexpr float VEKLOR_THREAT_FLOOR = 1500.0f;

    void AddTwinEmperorsScripts();

    // While the Twin Emperors fight: where Vek'lor stands and how close to him (edge to edge) this
    // bot may come, for other rules' walks to respect. False outside the fight.
    bool VeklorKeepOut(Player* bot, float& x, float& y, float& reach, float& keep);

    // Gives pets parked for a teleport back their react state once their owner is out of the
    // fight, and forgets pets that are gone. World thread (the module's sweep).
    void SweepParkedPets();

    // --- Princess Huhuran -------------------------------------------------
    enum : uint32
    {
        NPC_HUHURAN = 15509,
        SPELL_WYVERN_STING = 26180,  // a poison: dispelled, it hits for 3,000 (26233)
        SPELL_HUHURAN_BERSERK = 26068,
        SPELL_HUHURAN_POISON_BOLT = 26052,
    };
    // From Berserk (at 30%, or after 5 min) Poison Bolt hits the 15 raid members nearest to her
    // every 2 s for about 1,500 nature. Bots that aren't tanks or in plate stand POISON_BOLT_KEEP
    // from her (centre to centre: in range of their spells still), from HUHURAN_BERSERK_PCT on.
    constexpr float HUHURAN_BERSERK_PCT = 32.0f;
    constexpr float POISON_BOLT_KEEP = 32.0f;
    constexpr float POISON_BOLT_SPOT = 1.0f;
    constexpr float POISON_BOLT_WORTH_IT = 1.5f;
    constexpr float POISON_BOLT_SEARCH = 30.0f;
    // Bots within this much more than POISON_BOLT_KEEP walk toward her no further.
    constexpr float POISON_BOLT_HOLD_EXTRA = 10.0f;
    // Who counts as standing round her, centre to centre, for the melee to stand off.
    constexpr float POISON_BOLT_NEAR = 15.0f;

    // --- Anubisath Defender ----------------------------------------------
    // Each Defender rolls its abilities as it is pulled, and nothing on it shows which until it
    // uses them: Thunderclap (26554, everyone within 25 yd, about 1,150) or Shadow Storm (26555,
    // everyone within 45 yd but not within 25: the script drops those), Plague (26556, on the
    // highest-threat player who isn't its tank: every 3 s 26557 hits the raid within 5 yd of them)
    // or Meteor. The first cast tells.
    enum : uint32
    {
        NPC_ANUBISATH_DEFENDER = 15277,
        SPELL_DEFENDER_THUNDERCLAP = 26554,
        SPELL_DEFENDER_SHADOW_STORM = 26555,
        SPELL_DEFENDER_PLAGUE = 26556,
        SPELL_DEFENDER_PLAGUE_HIT = 26557,
        SPELL_DEFENDER_SHADOW_FROST_REFLECT = 19595,  // one of the two, cast as it is pulled
        SPELL_DEFENDER_FIRE_ARCANE_REFLECT = 13022,
    };
    // Ranged bots and healers stand outside Thunderclap by this much (the gap from their edge to
    // its reach), or inside Shadow Storm's dead zone by this much (centre to centre, flat, as the
    // script measures it).
    constexpr float THUNDERCLAP_KEEP = 2.0f;
    constexpr float SHADOW_STORM_SAFE = 25.0f;
    constexpr float SHADOW_STORM_KEEP = 3.0f;
    // A Thunderclap mob that leaves less than this many yards between its reach and a Shadow Storm
    // mob's dead zone (they are tanked nearly together) gives way to the Storm.
    constexpr float STORM_CLAP_BAND = 4.0f;
    // A plagued bot keeps this much more than the plague's reach (and the others' edge) from
    // every other raid member.
    constexpr float PLAGUE_KEEP = 1.5f;
    constexpr float DEFENDER_SPOT = 1.0f;
    constexpr float DEFENDER_WORTH_IT = 1.5f;
    constexpr float DEFENDER_SEARCH = 24.0f;
    // Defenders are looked at this far round the bot.
    constexpr float DEFENDER_SEARCH_RANGE = 60.0f;

    // --- Anubisath Sentinel ----------------------------------------------
    // Its ability is an aura it has from the pull (mob_anubisath_sentinel.cpp), passed on to the
    // others as it dies.
    enum : uint32
    {
        NPC_ANUBISATH_SENTINEL = 15264,
        SPELL_SENTINEL_FIRE_ARCANE_REFLECT = 13022,
        SPELL_SENTINEL_SHADOW_FROST_REFLECT = 19595,
        SPELL_SENTINEL_MENDING = 2147,
        SPELL_SENTINEL_KNOCK_AWAY = 21737,
        SPELL_SENTINEL_THORNS = 25777,
        SPELL_SENTINEL_MORTAL_STRIKE = 9347,
        SPELL_SENTINEL_MANA_BURN = 812,
        SPELL_SENTINEL_THUNDERCLAP_AURA = 2834,  // casts
        SPELL_SENTINEL_THUNDERCLAP = 8732,       // 25 yd round it
        SPELL_SENTINEL_SHADOW_STORM_AURA = 2148, // casts
        SPELL_SENTINEL_SHADOW_STORM = 26546,     // the Defenders' 25 yd dead zone (spell_aq_shadow_storm)
    };
    // Skull rank of a Sentinel: this plus the place of its own ability in the guide's list. Its
    // pack (it calls the Sentinels within 100 yd) is looked for this far round it.
    constexpr uint8 SENTINEL_RANK_BASE = 6;
    constexpr float SENTINEL_PACK_RANGE = 100.0f;

    // A unit's rank in the kill order, from its rank in the table (Sentinels go by their
    // abilities).
    uint8 KillOrderRank(Unit const* unit, uint8 rank);

    void AddDefenderScripts();

    // --- Nature resistance -------------------------------------------------
    // Viscidus (Poison Bolt Volley, Toxin), Princess Huhuran (Poison Bolt, Noxious Poison) and the
    // Bug Trio (Kri's Toxic Volley and cloud) deal nature damage to everyone. Nature Resistance
    // Totem and Aspect of the Wild are raid auras (30 yd round the totem or the hunter) that don't
    // stack with each other: one shaman keeps the totem down and one hunter keeps the aspect up,
    // the first of each in group order who knows it and is within NATURE_HELPER_RANGE of the boss.
    constexpr uint32 SPELL_NATURE_RESISTANCE_TOTEM_RANK_1 = 10595;
    constexpr uint32 ASPECT_OF_THE_WILD_RANKS[] = { 20043, 20190, 27045, 49071 };
    constexpr float NATURE_HELPER_RANGE = 50.0f;
    // That hunter may take Aspect of the Viper below VIPER_MANA_PCT mana, and only goes back to
    // the Wild above WILD_MANA_PCT.
    constexpr float VIPER_MANA_PCT = 20.0f;
    constexpr float WILD_MANA_PCT = 60.0f;

    // --- Fixed spots (Aq40Spots.cpp) ----------------------------------------
    // From the config, RaidClear.TempleOfAhnQiraj.<Boss>.<Role>Spot. nullptr when not set.
    enum class Spot : uint8
    {
        SkeramTank,    // else his own platform
        SkeramRanged,
        SkeramHealer,  // else SkeramRanged
        VemTank,
        YaujTank,
        KriTank,
        BugRanged,
        BugHealer,     // else BugRanged
        TwinsA,
        TwinsB,
        Count
    };
    Position const* SpotOf(Spot spot);
    void LoadSpots();

    std::vector<KillOrderEntry> const& KillOrder();

    // A bug that is eating or being eaten: no damage lands on it, so skull leaves it.
    bool KillOrderSkips(Player* bot, Unit const* unit);

    // Fankriss: two tanks take him in turns (Mortal Wound).
    std::vector<uint32> const& TankSplitCoTankBosses();
    // Spawn of Fankriss: an off-tank each. Vekniss Guardian: Impale, 10 yd round it.
    std::vector<uint32> const& TankSplitOwnTankAdds();
    std::vector<Tanks::SplashRadius> const& TankSplitSplashRadii();
}

// --- Strategies -----------------------------------------------------------

class RaidClearAq20Strategy : public Strategy
{
public:
    RaidClearAq20Strategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc aq20"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

class RaidClearAq40Strategy : public Strategy
{
public:
    RaidClearAq40Strategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc aq40"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// --- Anubisath Explode ----------------------------------------------------

// A mob fighting the raid has its Explode fuse lit with the bot in reach.
class RcAqExplodeTrigger : public Trigger
{
public:
    RcAqExplodeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq explode") {}
    bool IsActive() override;
};

class RcAqExplodeMoveAwayAction : public Action
{
public:
    RcAqExplodeMoveAwayAction(PlayerbotAI* botAI) : Action(botAI, "rc aq explode move away"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Keeps bots from walking back to a mob that is about to go off.
class RcAqExplodeHoldMultiplier : public Multiplier
{
public:
    RcAqExplodeHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq explode hold") {}
    float GetValue(Action* action) override;
};

// --- Ossirian -------------------------------------------------------------

// The bot is the tank Ossirian is on and it is time to take him to the next crystal.
class RcAq20LeadToCrystalTrigger : public Trigger
{
public:
    RcAq20LeadToCrystalTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq20 lead to crystal") {}
    bool IsActive() override;
};

class RcAq20LeadToCrystalAction : public MovementAction
{
public:
    RcAq20LeadToCrystalAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc aq20 lead to crystal") {}
    bool Execute(Event event) override;

private:
    RaidClear::RuinsOfAhnQiraj::CrystalWalk _walk;
};

// The bot is the one picked to use the next crystal and it is time to go there or use it.
class RcAq20CrystalTrigger : public Trigger
{
public:
    RcAq20CrystalTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq20 crystal") {}
    bool IsActive() override;
};

class RcAq20UseCrystalAction : public MovementAction
{
public:
    RcAq20UseCrystalAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc aq20 use crystal") {}
    bool Execute(Event event) override;

private:
    RaidClear::RuinsOfAhnQiraj::CrystalWalk _walk;
};

// Switches off playerbots' crystal walk for everyone, and while the leading tank and the bot at
// the crystal are on their way, every other walk of theirs.
class RcAq20CrystalMultiplier : public Multiplier
{
public:
    RcAq20CrystalMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq20 crystal") {}
    float GetValue(Action* action) override;
};

// --- Viscidus -------------------------------------------------------------

// Viscidus can be frozen and this damage dealer has a frost spell to cast at him.
class RcAq40ViscidusFrostTrigger : public Trigger
{
public:
    RcAq40ViscidusFrostTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 viscidus frost") {}
    bool IsActive() override;
};

class RcAq40ViscidusFrostAction : public Action
{
public:
    RcAq40ViscidusFrostAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 viscidus frost") {}
    bool Execute(Event event) override;
};

// The bot stands at a Toxin cloud, or is the tank Viscidus is on and he stands in one.
class RcAq40ToxinTrigger : public Trigger
{
public:
    RcAq40ToxinTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 toxin") {}
    bool IsActive() override;
};

class RcAq40LeaveToxinAction : public Action
{
public:
    RcAq40LeaveToxinAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 leave toxin"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Viscidus has shattered and this damage dealer isn't on a glob.
class RcAq40GlobTrigger : public Trigger
{
public:
    RcAq40GlobTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 glob") {}
    bool IsActive() override;
};

class RcAq40KillGlobAction : public AttackAction
{
public:
    RcAq40KillGlobAction(PlayerbotAI* botAI) : AttackAction(botAI, "rc aq40 kill glob") {}
    bool Execute(Event event) override;
};

// While globs crawl, a damage dealer's target picking (skull, assist) keeps it on its own glob,
// or on globs at all.
class RcAq40GlobMultiplier : public Multiplier
{
public:
    RcAq40GlobMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 glob") {}
    float GetValue(Action* action) override;
};

// The bot has an area spell ready that would hit GLOB_AOE_MIN globs or more.
class RcAq40GlobAoeTrigger : public Trigger
{
public:
    RcAq40GlobAoeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 glob aoe") {}
    bool IsActive() override;
};

class RcAq40GlobAoeAction : public Action
{
public:
    RcAq40GlobAoeAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 glob aoe") {}
    bool Execute(Event event) override;
};

// Keeps bots from walking toward something that stands in a cloud.
class RcAq40ToxinHoldMultiplier : public Multiplier
{
public:
    RcAq40ToxinHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 toxin hold") {}
    float GetValue(Action* action) override;
};

// --- Kurinnaxx -----------------------------------------------------------

// The bot is within reach of a Sand Trap that is about to go off.
class RcAq20SandTrapTrigger : public Trigger
{
public:
    RcAq20SandTrapTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq20 sand trap") {}
    bool IsActive() override;
};

class RcAq20LeaveSandTrapAction : public Action
{
public:
    RcAq20LeaveSandTrapAction(PlayerbotAI* botAI) : Action(botAI, "rc aq20 leave sand trap"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Keeps bots from walking toward something that stands on a live trap.
class RcAq20SandTrapHoldMultiplier : public Multiplier
{
public:
    RcAq20SandTrapHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq20 sand trap hold") {}
    float GetValue(Action* action) override;
};

// --- Moam -----------------------------------------------------------------

// A hunter, a priest that isn't healing or a warlock with its mana drain ready for Moam.
class RcAq20MoamDrainTrigger : public Trigger
{
public:
    RcAq20MoamDrainTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq20 moam drain") {}
    bool IsActive() override;
};

class RcAq20MoamDrainAction : public Action
{
public:
    RcAq20MoamDrainAction(PlayerbotAI* botAI) : Action(botAI, "rc aq20 moam drain") {}
    bool Execute(Event event) override;
};

// --- Ayamiss --------------------------------------------------------------

// A Hive'Zara Larva is crawling to the altar and this damage dealer isn't on one.
class RcAq20LarvaTrigger : public Trigger
{
public:
    RcAq20LarvaTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq20 larva") {}
    bool IsActive() override;
};

class RcAq20KillLarvaAction : public AttackAction
{
public:
    RcAq20KillLarvaAction(PlayerbotAI* botAI) : AttackAction(botAI, "rc aq20 kill larva") {}
    bool Execute(Event event) override;
};

// Keeps damage dealers' target picking (skull, assist) off everything but the larvae while one
// crawls.
class RcAq20LarvaMultiplier : public Multiplier
{
public:
    RcAq20LarvaMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq20 larva") {}
    float GetValue(Action* action) override;
};

// --- Sartura --------------------------------------------------------------

// The bot is within reach of Sartura or a guard in a whirlwind that isn't on it.
class RcAq40WhirlwindTrigger : public Trigger
{
public:
    RcAq40WhirlwindTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 whirlwind") {}
    bool IsActive() override;
};

class RcAq40WhirlwindKeepAwayAction : public Action
{
public:
    RcAq40WhirlwindKeepAwayAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 whirlwind keep away"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Keeps bots from walking toward something within a whirlwind's reach.
class RcAq40WhirlwindHoldMultiplier : public Multiplier
{
public:
    RcAq40WhirlwindHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 whirlwind hold") {}
    float GetValue(Action* action) override;
};

// The bot has a stun ready and in reach of Sartura or a guard that should be stunned now.
class RcAq40SarturaStunTrigger : public Trigger
{
public:
    RcAq40SarturaStunTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 sartura stun") {}
    bool IsActive() override;
};

class RcAq40SarturaStunAction : public Action
{
public:
    RcAq40SarturaStunAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 sartura stun") {}
    bool Execute(Event event) override;
};

// --- Skeram ---------------------------------------------------------------

// Skeram or an image is casting Arcane Explosion or True Fulfillment and the bot has an interrupt
// in reach.
class RcAq40SkeramCastTrigger : public Trigger
{
public:
    RcAq40SkeramCastTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 skeram cast") {}
    bool IsActive() override;
};

class RcAq40SkeramInterruptAction : public Action
{
public:
    RcAq40SkeramInterruptAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 skeram interrupt") {}
    bool Execute(Event event) override;
};

// The last interrupt in reach of Skeram waits for True Fulfillment.
class RcAq40SkeramInterruptMultiplier : public Multiplier
{
public:
    RcAq40SkeramInterruptMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 skeram interrupt") {}
    float GetValue(Action* action) override;
};

// --- Fankriss -------------------------------------------------------------

// The bot is one of Fankriss' two tanks, has no Mortal Wound, and the tank he is on has enough.
class RcAq40MortalWoundTrigger : public Trigger
{
public:
    RcAq40MortalWoundTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 mortal wound") {}
    bool IsActive() override;
};

class RcAq40MortalWoundTauntAction : public Action
{
public:
    RcAq40MortalWoundTauntAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 mortal wound taunt") {}
    bool Execute(Event event) override;
};

// No other taunt takes Fankriss off a tank.
class RcAq40MortalWoundMultiplier : public Multiplier
{
public:
    RcAq40MortalWoundMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 mortal wound") {}
    float GetValue(Action* action) override;
};

// --- Bug Trio -------------------------------------------------------------

// Princess Yauj is casting Great Heal and the bot has an interrupt in reach.
class RcAq40YaujHealTrigger : public Trigger
{
public:
    RcAq40YaujHealTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 yauj heal") {}
    bool IsActive() override;
};

class RcAq40YaujInterruptAction : public Action
{
public:
    RcAq40YaujInterruptAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 yauj interrupt") {}
    bool Execute(Event event) override;
};

// A shaman near Princess Yauj without a Tremor Totem down ("rc mc tremor totem" drops it).
class RcAq40YaujTremorTrigger : public Trigger
{
public:
    RcAq40YaujTremorTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 yauj tremor") {}
    bool IsActive() override;
};

// Near Princess Yauj the earth slot is Tremor Totem's: nothing else is placed in it.
class RcAq40YaujTremorMultiplier : public Multiplier
{
public:
    RcAq40YaujTremorMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 yauj tremor") {}
    float GetValue(Action* action) override;
};

// The bot stands at Lord Kri's Poison Cloud.
class RcAq40PoisonCloudTrigger : public Trigger
{
public:
    RcAq40PoisonCloudTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 poison cloud") {}
    bool IsActive() override;
};

class RcAq40LeavePoisonCloudAction : public Action
{
public:
    RcAq40LeavePoisonCloudAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 leave poison cloud"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Keeps bots from walking toward something that stands in the cloud.
class RcAq40PoisonCloudHoldMultiplier : public Multiplier
{
public:
    RcAq40PoisonCloudHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 poison cloud hold") {}
    float GetValue(Action* action) override;
};

// --- Fixed spots (Skeram, Bug Trio) ---------------------------------------

// The bot has a fixed spot for its role in this fight (the tank a boss is on, a Skeram platform
// off-tank, ranged, healers) and isn't on it.
class RcAq40BossSpotTrigger : public Trigger
{
public:
    RcAq40BossSpotTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 boss spot") {}
    bool IsActive() override;

private:
    uint32 _checkedAtMs = 0;  // the answer holds for a few ticks: the look is several lookups
    bool _active = false;
};

class RcAq40BossSpotAction : public MovementAction
{
public:
    RcAq40BossSpotAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc aq40 boss spot") {}
    bool Execute(Event event) override;

private:
    uint32 _arriveMs = 0;  // the last walk that started: when it should arrive, its spot, its path's end
    Position _sentSpot;
    Position _sentEnd;
};

// On its spot, nothing but the raid mechanics walks the bot off it.
class RcAq40SpotHoldMultiplier : public Multiplier
{
public:
    RcAq40SpotHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 spot hold") {}
    float GetValue(Action* action) override;

private:
    uint32 _checkedAtMs = 0;  // the answer below holds for a few ticks: every action asks
    bool _holds = false;
};

// Skeram has landed on this off-tank's platform and is on someone who isn't a tank.
class RcAq40SkeramPickupTrigger : public Trigger
{
public:
    RcAq40SkeramPickupTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 skeram pickup") {}
    bool IsActive() override;
};

class RcAq40SkeramPickupAction : public Action
{
public:
    RcAq40SkeramPickupAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 skeram pickup") {}
    bool Execute(Event event) override;
};

// --- Princess Huhuran ------------------------------------------------------

// No poison cure goes to a raid member under Wyvern Sting, no Abolish Poison or Cleansing Totem
// while she is fighting (each would take it off later).
class RcAq40WyvernStingMultiplier : public Multiplier
{
public:
    RcAq40WyvernStingMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 wyvern sting") {}
    float GetValue(Action* action) override;
};

// A shaman has a Cleansing Totem down while she is fighting.
class RcAq40CleansingTotemTrigger : public Trigger
{
public:
    RcAq40CleansingTotemTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 cleansing totem") {}
    bool IsActive() override;
};

class RcAq40DestroyCleansingTotemAction : public Action
{
public:
    RcAq40DestroyCleansingTotemAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 destroy cleansing totem") {}
    bool Execute(Event event) override;
};

// She has Berserk (or is about to), and the bot is one Poison Bolt should miss and stands too close.
class RcAq40PoisonBoltTrigger : public Trigger
{
public:
    RcAq40PoisonBoltTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 poison bolt") {}
    bool IsActive() override;
};

class RcAq40PoisonBoltKeepAwayAction : public Action
{
public:
    RcAq40PoisonBoltKeepAwayAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 poison bolt keep away"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Keeps those bots from walking back up to her.
class RcAq40PoisonBoltHoldMultiplier : public Multiplier
{
public:
    RcAq40PoisonBoltHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 poison bolt hold") {}
    float GetValue(Action* action) override;
};

// --- Anubisath Defender ----------------------------------------------------

// A ranged bot or healer stands inside a Defender's Thunderclap or outside its Shadow Storm's dead
// zone.
class RcAq40DefenderAoeTrigger : public Trigger
{
public:
    RcAq40DefenderAoeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 defender aoe") {}
    bool IsActive() override;
};

class RcAq40DefenderAoeMoveAction : public Action
{
public:
    RcAq40DefenderAoeMoveAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 defender aoe move"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// The bot has a Defender's Plague and another raid member stands in its reach.
class RcAq40PlagueTrigger : public Trigger
{
public:
    RcAq40PlagueTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 plague") {}
    bool IsActive() override;
};

class RcAq40PlagueMoveAwayAction : public Action
{
public:
    RcAq40PlagueMoveAwayAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 plague move away"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// A plagued bot walks toward nothing with raid members round it; a ranged bot or healer toward
// nothing that would take it into a Thunderclap.
class RcAq40DefenderHoldMultiplier : public Multiplier
{
public:
    RcAq40DefenderHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 defender hold") {}
    float GetValue(Action* action) override;
};

// --- Nature resistance -----------------------------------------------------

// A nature fight, and the bot is the raid's shaman for Nature Resistance Totem (none down) or its
// hunter for Aspect of the Wild (not up).
class RcAq40NatureResistanceTrigger : public Trigger
{
public:
    RcAq40NatureResistanceTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 nature resistance") {}
    bool IsActive() override;
};

class RcAq40NatureResistanceAction : public Action
{
public:
    RcAq40NatureResistanceAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 nature resistance") {}
    bool Execute(Event event) override;
};

// That shaman's air slot is the totem's, and that hunter casts no other aspect.
class RcAq40NatureResistanceMultiplier : public Multiplier
{
public:
    RcAq40NatureResistanceMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 nature resistance") {}
    float GetValue(Action* action) override;
};

// --- Twin Emperors ----------------------------------------------------------

// The bot is on a twin it can't hurt (or nothing), or is a twin's tank and isn't on its twin.
class RcAq40TwinTargetTrigger : public Trigger
{
public:
    RcAq40TwinTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 twin target") {}
    bool IsActive() override;
};

// Onto the twin it should be hitting; with none, off the one it can't hurt.
class RcAq40AttackTwinAction : public RcLeaveTargetAction
{
public:
    RcAq40AttackTwinAction(PlayerbotAI* botAI) : RcLeaveTargetAction(botAI, "rc aq40 attack twin") {}
    bool Execute(Event event) override;

protected:
    bool IsBadTarget(Unit const* unit) override;
    bool IsBadPetTarget(Unit const* unit) override;
};

// Nothing the bot does is aimed at a twin it can't hurt; a twin's tank picks no other target.
class RcAq40TwinTargetMultiplier : public Multiplier
{
public:
    RcAq40TwinTargetMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 twin target") {}
    float GetValue(Action* action) override;
};

// The bot stands where the twins don't want it: too close to Vek'lor (or, for his tank, not in
// its band), too close to Vek'nilash, or in a Blizzard.
class RcAq40TwinsPositionTrigger : public Trigger
{
public:
    RcAq40TwinsPositionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 twins position") {}
    bool IsActive() override;
};

class RcAq40TwinsPositionAction : public Action
{
public:
    RcAq40TwinsPositionAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 twins position"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// Keeps Vek'lor's tank from walking into his Burst, and melee from walking back to Vek'nilash
// while a teleport is due.
class RcAq40TwinsHoldMultiplier : public Multiplier
{
public:
    RcAq40TwinsHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 twins hold") {}
    float GetValue(Action* action) override;
};

// The bot is the tank Vek'nilash is on, and he is close enough to Vek'lor to be healed (or, with
// the torch spots set, it isn't on his).
class RcAq40TwinsApartTrigger : public Trigger
{
public:
    RcAq40TwinsApartTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 twins apart") {}
    bool IsActive() override;
};

class RcAq40TwinsApartAction : public MovementAction
{
public:
    RcAq40TwinsApartAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc aq40 twins apart"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// The bot's pet is to be kept by its side (a teleport is due, or all it could hit is Vek'lor), or
// was and may fight again.
class RcAq40TwinsPetTrigger : public Trigger
{
public:
    RcAq40TwinsPetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 twins pet") {}
    bool IsActive() override;
};

class RcAq40TwinsPetAction : public Action
{
public:
    RcAq40TwinsPetAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 twins pet") {}
    bool Execute(Event event) override;
};

// The bot is Vek'lor's tank and has Avenger's Shield or Exorcism ready for him.
class RcAq40VeklorTankThreatTrigger : public Trigger
{
public:
    RcAq40VeklorTankThreatTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc aq40 veklor tank threat") {}
    bool IsActive() override;
};

class RcAq40VeklorTankThreatAction : public Action
{
public:
    RcAq40VeklorTankThreatAction(PlayerbotAI* botAI) : Action(botAI, "rc aq40 veklor tank threat") {}
    bool Execute(Event event) override;
};

// Holds damage on Vek'lor under his tank's threat.
class RcAq40VeklorThreatMultiplier : public Multiplier
{
public:
    RcAq40VeklorThreatMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc aq40 veklor threat") {}
    float GetValue(Action* action) override;
};

#endif
