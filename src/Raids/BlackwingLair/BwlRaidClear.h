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
 *     polymorph or buff: Taskmasters, Spellbinders, Hatchers, Death Talon Captains.
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
 *   - Technician packs: Blackwing Technicians throw Bomb (22334, 5 yd splash) at random raiders
 *     within 30 yd, so ranged and healers keep 6 yd from each other while one is fighting nearby.
 *     Only bots in a clump move, one short step at a time.
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
        NPC_ENRAGED_FELGUARD      = 14101,  // from the warlocks' Demon Portals

        NPC_CORRUPTED_RED_WHELP    = 14022,
        NPC_CORRUPTED_GREEN_WHELP  = 14023,
        NPC_CORRUPTED_BLUE_WHELP   = 14024,
        NPC_CORRUPTED_BRONZE_WHELP = 14025,
    };

    enum Spells : uint32
    {
        SPELL_SHADOW_OF_EBONROC = 23340,
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

    std::vector<KillOrderEntry> const& KillOrder();

    // Bosses that cut their tank's threat: the off-tank builds threat on them too.
    std::vector<uint32> const& TankSplitCoTankBosses();

    // Adds the off-tank leaves alone (the Suppression Room's endless whelps).
    std::vector<uint32> const& TankSplitIgnore();
}

class RaidClearBlackwingLairStrategy : public Strategy
{
public:
    RaidClearBlackwingLairStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc bwl"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
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
    uint32 _nextStepMs = 0;  // game time (ms) before which this bot doesn't step again
};

class RcBwlTechnicianSpreadAction : public MovementAction
{
public:
    RcBwlTechnicianSpreadAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl technician spread") {}
    bool Execute(Event event) override;
};

// --- Broodlord Lashlayer --------------------------------------------------

class RcBwlBroodlordRangedTrigger : public Trigger
{
public:
    RcBwlBroodlordRangedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc bwl broodlord ranged") {}
    bool IsActive() override;
};

class RcBwlBroodlordMoveOutAction : public MovementAction
{
public:
    RcBwlBroodlordMoveOutAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc bwl broodlord move out") {}
    bool Execute(Event event) override;
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

#endif
