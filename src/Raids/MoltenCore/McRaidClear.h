/*
 * mod-raid-clear: Molten Core (map 409).
 *
 * Runs next to mod-playerbots' "moltencore" strategy, which already handles Shazzrah's Arcane
 * Explosion range, the Golemagg / Core Rager tank split, Core Hound
 * skull rotation, lava escape and the resistance auras/totems. Fire patches (Lava Bomb traps)
 * and Rain of Fire are left to playerbots' generic "avoid aoe".
 *
 * Added here (mechanics from the core scripts in scripts/EasternKingdoms/BlackrockMountain/
 * MoltenCore):
 *   - Kill order: Lava Spawn (splits every 15s), the healing adds (Sulfuron's priests,
 *     Majordomo's healers), then the other boss adds and Ragnaros' Sons of Flame.
 *   - Magmadar: every shaman keeps Tremor Totem down. Panic is a 45 yd raid-wide fear.
 *     Playerbots keeps each shaman's own earth totem (Stoneskin, Strength of Earth) down the
 *     same way, and the two share a slot: left alone they replace each other every second and
 *     a half and the shaman does nothing else all fight. So near Magmadar a shaman who knows
 *     Tremor Totem places no other earth totem (and no Call of the Elements, which would).
 *   - Baron Geddon: Inferno (19695, 8 s) burns everyone within 20 yd of him once a second, for
 *     500 at first and 5,000 at the end; he stands still for it. Playerbots walks its bots out,
 *     but only those it finds through "find target", which is a bot's own threat list: a tank
 *     holding an add, a bot on the trash that came with him, a bot revived mid-fight never see
 *     him and stand in all eight pulses. Here every bot within 23 yd of him walks to the nearest
 *     spot 25 yd out (see Common/WayOut.h), whoever he is on included, and nobody walks back
 *     toward anything that close to him until it is over. A bot in the middle of a cast is
 *     told to drop it the moment Inferno starts, and one still inside casts nothing it has to
 *     stand still for.
 *   - Majordomo: nobody casts at an enemy with Magic Reflection, and melee (not tanks) stop
 *     swinging at one with Damage Reflection. Both shields cover all of his adds.
 *     The Hot Coal in the middle of his room (the pit his Teleport throws players into) is a
 *     trap: with a player within about 7 yd of its middle, everyone within 10 yd takes Intense
 *     Heat (20533, about 750 fire a second, logged as environmental damage). Nothing in
 *     playerbots knows it (its lava check looks at the liquid under a bot; this is a game
 *     object). In combat a bot that has stopped within 12 yd of it walks out, on the side its
 *     target is on; one only crossing the pit is left alone. A tank with a mob on it goes 22 yd
 *     out when either of them is within 12 yd, so the mob follows it out of reach. Melee whose
 *     target stands in it stay on that target.
 *   - Ragnaros: ranged and healers stay out of Wrath of Ragnaros (20566), which reaches 25 yd
 *     from his middle. He is 20 yd wide himself (his combat reach), so this is measured centre
 *     to centre: 27.5 yd from his middle is only 6 yd from his edge. A bot closer than that
 *     walks to the nearest dry spot 29.5 yd out, and doesn't walk back toward anything that
 *     close to him. Not while he is under the lava.
 *   - Baron Geddon's Living Bomb (20475) goes off after 8 s and hits every raid member within
 *     18 yd of its carrier. Playerbots moves the carrier away from the middle of the raid by
 *     as many yards as its nearest neighbour is short of 20: enough for a bot at the edge of
 *     the group, not for one in the middle of it, which is where the healers stand. Here the
 *     carrier walks to the nearest spot 24.5 yd from every other raid member (looked for up to
 *     40 yd away) and not within Inferno's reach of Geddon, or to the emptiest spot there is.
 *   - Garr: a Firesworn more than 40 yd from him gets Separation Anxiety (23492, +300% damage),
 *     and a Firesworn erupts when it dies (Eruption, 19497: about 2,000 fire to everyone within
 *     15 yd). With the two together that is 8,000, which kills everyone around it. So the fight
 *     stays with Garr: a bot more than 30 yd from him walks back to 22 (a Firesworn follows
 *     whoever it is on; a tank only comes when a Firesworn is on it, so trash pulled with the
 *     boss stays where its tank holds it), nobody but the tanks hits a Firesworn that has
 *     Separation Anxiety or is about to get it (bots already on one stop, pets too), and the
 *     kill order skips it.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_MOLTEN_CORE_H
#define MOD_RAID_CLEAR_MOLTEN_CORE_H

#include "Common/KillOrder.h"
#include "Common/TankRoles.h"
#include "Common/WayOut.h"

#include "Action.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "Strategy.h"
#include "Trigger.h"

#include <vector>

namespace RaidClear::MoltenCore
{
    constexpr uint32 MAP_ID = 409;

    enum Creatures : uint32
    {
        NPC_MAGMADAR             = 11982,
        NPC_GARR                 = 12057,
        NPC_BARON_GEDDON         = 12056,
        NPC_GOLEMAGG             = 11988,

        NPC_MOLTEN_GIANT         = 11658,  // Smash: 10 yd AoE, Knock Away
        NPC_MOLTEN_DESTROYER     = 11659,  // Massive Tremor: 13 yd AoE, Knockdown
        NPC_MAJORDOMO            = 12018,
        NPC_RAGNAROS             = 11502,

        NPC_FLAMEWALKER          = 11661,  // Gehennas
        NPC_FLAMEWALKER_PRIEST   = 11662,  // Sulfuron
        NPC_FLAMEWAKER_HEALER    = 11663,  // Majordomo
        NPC_FLAMEWAKER_ELITE     = 11664,  // Majordomo
        NPC_FIRESWORN            = 12099,  // Garr
        NPC_FLAMEWALKER_PROTECTOR = 12119, // Lucifron
        NPC_SON_OF_FLAME         = 12143,  // Ragnaros, submerge phase
        NPC_LAVA_SPAWN           = 12265,  // Firelord trash, splits every 15s
    };

    enum Spells : uint32
    {
        SPELL_MAGIC_REFLECTION   = 20619,
        SPELL_DAMAGE_REFLECTION  = 21075,
        SPELL_SEPARATION_ANXIETY = 23492,  // on a Firesworn more than 40 yd from Garr
        SPELL_INFERNO            = 19695,  // on Baron Geddon; each pulse is 19698, 20 yd around him
        SPELL_LIVING_BOMB        = 20475,  // on a player; explodes as 20476, 18 yd around it
    };

    // The Living Bomb's explosion reaches 18 yd, centre to centre. Its carrier walks to the
    // nearest spot at least SPOT from every other raid member, looked for up to SEARCH yards
    // away, or to one WORTH_IT yards emptier than where it stands when there is none.
    // (KEEP_AWAY is what playerbots' own rule asks for, 20 yd edge to edge: with less, that
    // rule would nudge a bot this one had just put in a good spot.)
    constexpr float LIVING_BOMB_KEEP_AWAY = 23.0f;
    constexpr float LIVING_BOMB_SPOT = 24.5f;
    constexpr float LIVING_BOMB_WORTH_IT = 3.0f;
    constexpr float LIVING_BOMB_SEARCH = 40.0f;

    // Inferno reaches 20 yd, centre to centre. A bot closer than KEEP_AWAY to him walks to the
    // nearest spot at least SPOT from him (looked for up to SEARCH yards round the bot), or to
    // one WORTH_IT yards further out than it stands when there is none.
    constexpr float INFERNO_KEEP_AWAY = 23.0f;
    constexpr float INFERNO_SPOT = 25.0f;
    constexpr float INFERNO_WORTH_IT = 3.0f;
    constexpr float INFERNO_SEARCH = 30.0f;

    // The Hot Coal in Majordomo's room (game object 178164, a trap 10 yd across; its one spawn
    // is where his Teleport lands). Intense Heat reaches 10 yd from it.
    constexpr float HOT_COAL_X = 736.653f;
    constexpr float HOT_COAL_Y = -1176.32f;
    constexpr float HOT_COAL_Z = -118.097f;
    constexpr float HOT_COAL_KEEP_AWAY = 12.0f;
    constexpr float HOT_COAL_SPOT = 13.5f;
    // A tank with a mob on it goes further out: the mob follows and stops a few yards short.
    constexpr float HOT_COAL_TANK_KEEP_AWAY = 20.0f;
    constexpr float HOT_COAL_TANK_SPOT = 22.0f;
    constexpr float HOT_COAL_WORTH_IT = 2.0f;
    constexpr float HOT_COAL_SEARCH = 27.0f;

    // Panic (19408) radius.
    constexpr float PANIC_RANGE = 45.0f;
    // Wrath of Ragnaros (20566) reaches 25 yd from his middle. Ranged and healers closer than
    // KEEP_AWAY to his middle walk to the nearest dry spot at least SPOT from it.
    constexpr float RAGNAROS_KEEP_AWAY = 27.5f;
    constexpr float RAGNAROS_SPOT = 29.5f;
    constexpr float RAGNAROS_WORTH_IT = 2.0f;
    constexpr float RAGNAROS_SEARCH = 30.0f;
    // Within this of his middle they don't walk toward anything closer to him than KEEP_AWAY.
    constexpr float RAGNAROS_HOLD = 36.0f;

    // Garr checks his Firesworn once a second: more than 40 yd from him (edge to edge) and it
    // gets Separation Anxiety. A Firesworn this far out counts as having it already.
    constexpr float GARR_ANXIOUS_RANGE = 36.0f;
    // A bot further than this from Garr walks back to the target distance.
    constexpr float GARR_GATHER = 30.0f;
    constexpr float GARR_GATHER_TARGET = 22.0f;

    std::vector<KillOrderEntry> const& KillOrder();
    // Adds the kill order leaves alone for now (a Firesworn with Separation Anxiety).
    bool KillOrderSkips(Player* bot, Unit const* unit);

    // Bosses where mod-playerbots places the tanks itself (Golemagg: main tank on him, assist
    // tank on the Core Ragers at fixed spots), so the generic off-tank split stays out.
    std::vector<uint32> const& TankSplitSkipBosses();

    // Trash whose AoE needs more room than the default tank separation.
    std::vector<Tanks::SplashRadius> const& TankSplitSplashRadii();

    void AddScripts();
}

class RaidClearMoltenCoreStrategy : public Strategy
{
public:
    RaidClearMoltenCoreStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc moltencore"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// --- Magmadar -------------------------------------------------------------

class RcMcMagmadarTremorTrigger : public Trigger
{
public:
    RcMcMagmadarTremorTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc magmadar tremor") {}
    bool IsActive() override;
};

class RcMcTremorTotemAction : public Action
{
public:
    RcMcTremorTotemAction(PlayerbotAI* botAI) : Action(botAI, "rc mc tremor totem") {}
    bool Execute(Event event) override;
};

// Near Magmadar the earth slot is Tremor Totem's: nothing else is placed in it.
class RcMcTremorMultiplier : public Multiplier
{
public:
    RcMcTremorMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc mc tremor") {}
    float GetValue(Action* action) override;
};

// --- Baron Geddon ---------------------------------------------------------

// Baron Geddon has Inferno up and the bot is within its reach.
class RcMcInfernoTrigger : public Trigger
{
public:
    RcMcInfernoTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc inferno") {}
    bool IsActive() override;
};

// Not a movement action itself: playerbots' own Geddon rule switches those off (all but its two)
// for as long as Inferno lasts.
class RcMcLeaveInfernoAction : public Action
{
public:
    RcMcLeaveInfernoAction(PlayerbotAI* botAI) : Action(botAI, "rc mc leave inferno"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// The bot carries a Living Bomb and a raid member is within its reach.
class RcMcLivingBombTrigger : public Trigger
{
public:
    RcMcLivingBombTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc living bomb") {}
    bool IsActive() override;
};

// Not a movement action itself, for the same reason as the way out of Inferno: playerbots lets a
// bot with the bomb do no walking but its own.
class RcMcCarryLivingBombAction : public Action
{
public:
    RcMcCarryLivingBombAction(PlayerbotAI* botAI) : Action(botAI, "rc mc carry living bomb"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// While Inferno lasts nobody walks (or charges) toward something within its reach.
class RcMcInfernoHoldMultiplier : public Multiplier
{
public:
    RcMcInfernoHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc mc inferno hold") {}
    float GetValue(Action* action) override;
};

// --- Majordomo Executus ---------------------------------------------------

class RcMcDamageReflectionTrigger : public Trigger
{
public:
    RcMcDamageReflectionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc damage reflection") {}
    bool IsActive() override;
};

class RcMcStopMeleeAction : public Action
{
public:
    RcMcStopMeleeAction(PlayerbotAI* botAI) : Action(botAI, "rc mc stop melee") {}
    bool Execute(Event event) override;
};

class RcMcReflectionMultiplier : public Multiplier
{
public:
    RcMcReflectionMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc mc reflection") {}
    float GetValue(Action* action) override;
};

// The bot is in combat and has stopped within reach of the Hot Coal.
class RcMcHotCoalTrigger : public Trigger
{
public:
    RcMcHotCoalTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc hot coal") {}
    bool IsActive() override;

private:
    uint32 _lastLookMs = 0;
    bool _lastStands = false;
    bool _lastResult = false;
    Position _lastSpot;
};

// A bot that has just stopped at the Hot Coal starts no spell it has to stand still for.
class RcMcHotCoalHoldMultiplier : public Multiplier
{
public:
    RcMcHotCoalHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc mc hot coal hold") {}
    float GetValue(Action* action) override;

private:
    uint32 _sinceMs = 0;
};

class RcMcLeaveHotCoalAction : public Action
{
public:
    RcMcLeaveHotCoalAction(PlayerbotAI* botAI) : Action(botAI, "rc mc leave hot coal"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

// --- Garr -----------------------------------------------------------------

class RcMcGarrGatherTrigger : public Trigger
{
public:
    RcMcGarrGatherTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc garr gather") {}
    bool IsActive() override;
};

class RcMcGarrGatherAction : public MovementAction
{
public:
    RcMcGarrGatherAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc mc garr gather") {}
    bool Execute(Event event) override;
};

// A bot that isn't a tank is attacking a Firesworn that has Separation Anxiety.
class RcMcAnxiousFireswornTrigger : public Trigger
{
public:
    RcMcAnxiousFireswornTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc anxious firesworn") {}
    bool IsActive() override;
};

// Stops the swing, the Auto Shot or the wand, and the pet.
class RcMcLeaveFireswornAction : public Action
{
public:
    RcMcLeaveFireswornAction(PlayerbotAI* botAI) : Action(botAI, "rc mc leave firesworn") {}
    bool Execute(Event event) override;
};

// Nobody but the tanks hits a Firesworn that would erupt for four times the damage.
class RcMcAnxiousFireswornMultiplier : public Multiplier
{
public:
    RcMcAnxiousFireswornMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc mc anxious firesworn") {}
    float GetValue(Action* action) override;
};

// --- Ragnaros -------------------------------------------------------------

class RcMcRagnarosRangedTrigger : public Trigger
{
public:
    RcMcRagnarosRangedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc mc ragnaros ranged") {}
    bool IsActive() override;
};

// Ranged bots and healers at Ragnaros don't walk toward something inside his Wrath.
class RcMcRagnarosHoldMultiplier : public Multiplier
{
public:
    RcMcRagnarosHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc mc ragnaros hold") {}
    float GetValue(Action* action) override;
};

class RcMcRagnarosMoveOutAction : public Action
{
public:
    RcMcRagnarosMoveOutAction(PlayerbotAI* botAI) : Action(botAI, "rc mc ragnaros move out"), _walk(botAI) {}
    bool Execute(Event event) override;

private:
    RaidClear::WayOutWalk _walk;
};

#endif
