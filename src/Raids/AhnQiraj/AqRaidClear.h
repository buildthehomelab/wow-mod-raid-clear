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
 *     the fuse is lit.
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
 *
 * Temple of Ahn'Qiraj, the Bug Trio (Princess Yauj, Lord Kri, Vem):
 *   - Kill order. A bug that drops while another is alive is eaten by the others, who get their
 *     health back; Yauj leaves ten Yauj Broods, Kri a Poison Cloud, and Vem's death enrages the
 *     other two (Vengeance). Skull: Broods, then Yauj (she heals the others and fears), then Kri,
 *     then Vem. A bug that is being eaten can't be hurt and one on its way to eat gets its health
 *     back when it arrives: skull leaves both meanwhile.
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
 *   - Globs. When he shatters, the Globs of Viscidus that crawl back to the middle of the room
 *     each heal him if they get there: skull goes on them first.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_AHN_QIRAJ_H
#define MOD_RAID_CLEAR_AHN_QIRAJ_H

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

    // The blast's reach, centre to centre, from the spell data (20 yd on a stock server).
    float ExplodeRadius();

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

    // --- Fankriss --------------------------------------------------------
    enum : uint32
    {
        NPC_SPAWN_OF_FANKRISS = 15630,
        NPC_VEKNISS_HATCHLING = 15962,
    };

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

    std::vector<KillOrderEntry> const& KillOrder();

    // A bug that is eating or being eaten: no damage lands on it, so skull leaves it.
    bool KillOrderSkips(Player* bot, Unit const* unit);

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

#endif
