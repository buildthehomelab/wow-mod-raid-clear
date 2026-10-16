/*
 * mod-raid-clear: Ruins of Ahn'Qiraj (map 509) and Temple of Ahn'Qiraj (map 531).
 *
 * Mechanics from the core scripts in scripts/Kalimdor/RuinsOfAhnQiraj and TempleOfAhnQiraj, and
 * the spell data.
 *
 * Both raids:
 *   - Anubisath Explode. An Anubisath Guardian (and any other mob that does it) can go off at
 *     10% health: Explode (25698) is a 6 s fuse on the mob, then 25699 hits everyone within
 *     20 yd for about 4,700. Every bot in reach, tanks too, walks out and stays out until it has
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
 *       - One bot uses the crystal: the bot (not a tank) nearest to it, picked once for the
 *         crystal. It walks there ahead of him, waits, and uses it as soon as he is in reach and
 *         has Strength or less than CLICK_AHEAD of weakness left. A bot that can't get there is
 *         replaced for a while. After a use the plan stays on that crystal until the weakness has
 *         landed (the crystal's trigger casts it), so the tank doesn't take him out of its reach
 *         first.
 *     Everyone else fights on; playerbots' own crystal walk is switched off.
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
        SPELL_VISCIDUS_INVIS = 25905,  // shattered into globs
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

    std::vector<KillOrderEntry> const& KillOrder();
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

#endif
