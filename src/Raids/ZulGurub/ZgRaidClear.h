/*
 * mod-raid-clear: Zul'Gurub (map 309).
 *
 * mod-playerbots has no Zul'Gurub strategy, so everything here is new (mechanics from the core
 * scripts in scripts/EasternKingdoms/ZulGurub):
 *   - Kill order: the Razzashi Cobras that come with High Priest Venoxis die before him.
 *   - High Priest Venoxis, troll form (above 50%): Holy Wrath (23979) hits his top-threat target
 *     (usually his victim, but a damage dealer who has passed the tank on threat counts too) and
 *     chains to the nearest player within 10 yd of it, again and again, each jump about 30%
 *     harder than the last. Nine jumps in, it hits for over 40,000. So the chain gets nowhere to
 *     go: everyone but the tanks stays 13 yd (edge to edge, about 16 between two players) away
 *     from his victim, from whoever tops his threat, and from any tank or pet standing within a
 *     jump of those. Melee don't walk toward anything inside that zone at all; ranged and healers
 *     stop outside it. At 50% he turns into a snake, Holy Wrath stops and melee go in.
 *   - Hakkar keeps taking his tank away. Cause Insanity (24327) mind-controls whoever tops his
 *     threat for 10 s, every 35-40 s. For every High Priest still alive he also has that priest's
 *     Aspect: Arlokk's (24690, every 10-15 s) and Mar'li's (24686, every 45 s) stun the top-threat
 *     player in melee range and wipe that player's threat. Arlokk only appears when someone
 *     rings the Gong of Bethekk, so a bot raid usually arrives with her Aspect on him. Each time,
 *     he turns to whoever is next on threat, and once Frenzy (24318) is up he kills a mage in two
 *     swings. So he is listed as a boss whose threat the tanks share and the damage dealers stay
 *     under (see Common/TankRoles.h): the next in line is always a tank.
 *   - High Priest Thekal comes with Zealot Lor'Khan and Zealot Zath, and until he turns into a
 *     tiger none of the three stays dead: one that drops gets up at full health 10 s later unless
 *     the other two are down by then, and each zealot that gets up heals Thekal to full as well.
 *     So they are brought down together. Skull sits on whichever of the three has the most health
 *     left (plain health, not percent: Thekal has almost twice Lor'Khan's) and moves as they
 *     level out; damage dealers follow it, the three reach zero within a few seconds of each
 *     other and the fight moves on to his tiger form. Area damage would undo that (it ignores
 *     skull, and Lor'Khan has the least health, so he would drop first every time), so nobody but
 *     the tanks uses a spell that hits more than one enemy until one of the three is down; then
 *     everything goes. The two Zulian Tigers that stand with him die first. Lor'Khan's Great Heal
 *     (24208, a 4 s cast worth 40,000 health, every 15-20 s) is interrupted by whoever can reach
 *     him, his target or not (a tank only when he is on that tank).
 *   - Trash. The packs' healers die first: a Hakkari Blood Priest (Blood Funnel), then a Hakkari
 *     Priest (Cleanse Nova, Heal), then the Gurubashi Blood Drinkers that heal themselves off the
 *     whole raid. While a Hakkari Priest has Anti-Magic Shield (24021, 8 s, every spell school)
 *     skull comes off it, for everyone, and casters don't pick it as a target at all.
 *   - A Gurubashi Bat Rider below 30% casts Unstable Concoction (24024): 3 s, then it blows
 *     itself up for 4,400-5,600 fire damage to everyone within 10 yd. It dies either way, so
 *     everyone within reach, tanks too, walks out and stays out until it has gone off: to the
 *     nearest spot, all the way round, that is clear of every Bat Rider about to blow, that a
 *     whole path leads to without a detour, and that the aggro leash won't stop the walk to. It
 *     comes before everything but the leash, and nothing else happens while the bot is running.
 *   - Nobody goes on hitting a mob that evades everything, or a raid member an enemy has taken
 *     over (Will of Hakkar, Brain Wash, Cause Insanity): target picking skips the evading ones,
 *     a bot already on either lets go, and so does its pet. Area spells aren't cast within 12 yd
 *     of a mind-controlled member either.
 *   - A mind-controlled member is stopped as well: a mage's Polymorph or a paladin's Hammer of
 *     Justice, one bot at a time (Will of Hakkar runs 20 s, Cause Insanity 10 s; Polymorph lasts
 *     10 s on a player, Hammer of Justice 6 s). When the mind control ends first, the bot that
 *     cast it takes its sheep or stun off again, so nobody comes back as a sheep. Not Blind:
 *     nothing takes that off.
 *   - High Priestess Jeklik, from 50%: her Bat Riders drop Liquid Fire (GO 180125) on a random
 *     raid member every 8 s. It burns for 20 s and casts Blaze (23972, 874 fire to everyone
 *     within 5 yd) whenever someone stands within 3 yd of it. Playerbots only walks out of traps
 *     whose spell is a periodic aura, so it never sees this one. Every bot within 6.5 yd of a
 *     fire walks out. The tank she is on takes her along when either of them stands in one, far
 *     enough that she ends up outside, and until then melee don't walk to her.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_ZUL_GURUB_H
#define MOD_RAID_CLEAR_ZUL_GURUB_H

#include "Common/KillOrder.h"

#include "Action.h"
#include "AttackAction.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "Strategy.h"
#include "Trigger.h"

#include <vector>

namespace RaidClear::ZulGurub
{
    constexpr uint32 MAP_ID = 309;

    enum Creatures : uint32
    {
        NPC_VENOXIS        = 14507,
        NPC_RAZZASHI_COBRA = 11373,  // four of them guard Venoxis
        NPC_HAKKAR         = 14834,
        NPC_THEKAL         = 14509,
        NPC_ZEALOT_LORKHAN = 11347,  // heals the three
        NPC_ZEALOT_ZATH    = 11348,
        NPC_ZULIAN_TIGER   = 11361,  // two stand with Thekal
        NPC_HAKKARI_BLOOD_PRIEST = 11340,
        NPC_HAKKARI_PRIEST = 11830,
        NPC_BLOOD_DRINKER  = 11353,
        NPC_BAT_RIDER      = 14750,
        NPC_JEKLIK         = 14517,
    };

    enum GameObjects : uint32
    {
        // What her Bat Riders drop on a random raid member every 8 s, from 50%: it burns for 20 s
        // and casts Blaze (23972: 874 fire to everyone within 5 yd) whenever someone is within
        // 3 yd of it.
        GO_LIQUID_FIRE = 180125,
    };

    enum Spells : uint32
    {
        SPELL_VENOXIS_TRANSFORM = 23849,  // snake form, from 50%: no more Holy Wrath
        SPELL_THEKAL_TIGER_FORM = 24169,  // once all three were down together: he dies for good now
        SPELL_LORKHAN_GREAT_HEAL = 24208,
        SPELL_ANTI_MAGIC_SHIELD = 24021,
        SPELL_UNSTABLE_CONCOCTION = 24024,
    };

    // A harmful magic chain jumps up to 10 yd (Spell::SearchChainTargets; Holy Wrath has no
    // spell_jump_distance override), measured between the two units' edges: the core adds both
    // combat reaches, so two players can be 13 yd apart centre to centre and still be hit. Every
    // distance below is edge to edge as well.
    constexpr float HOLY_WRATH_JUMP = 10.0f;
    // Tanks and pets this close to someone in the chain are in it too (a yard of slack).
    constexpr float HOLY_WRATH_CHAIN_LINK = HOLY_WRATH_JUMP + 1.0f;
    // Everyone else keeps this far from anyone in the chain, moves out to the target distance,
    // and (ranged and healers) within HOLD of it doesn't walk toward something inside.
    constexpr float HOLY_WRATH_KEEP_AWAY = HOLY_WRATH_JUMP + 3.0f;
    constexpr float HOLY_WRATH_KEEP_AWAY_TARGET = HOLY_WRATH_JUMP + 5.0f;
    constexpr float HOLY_WRATH_HOLD = HOLY_WRATH_JUMP + 10.0f;
    // A spot more than this above or below the bot is down the temple's stairs or off its edge.
    constexpr float HOLY_WRATH_SPOT_HEIGHT = 3.0f;

    // Thekal and his zealots: skull stays where it is until the one it is on has fallen this
    // share below the healthiest of the three, so melee aren't sent back and forth every second.
    // The share shrinks as they do; it never goes under MIN health, or skull would move on every
    // hit near the end. When the first one drops, the other two are left with about that much
    // each: a few seconds of damage, well inside the 10 s.
    constexpr float EVEN_KILL_MARGIN = 0.15f;
    constexpr float EVEN_KILL_MARGIN_MIN = 3000.0f;

    // Unstable Concoction reaches 10 yd, centre to centre. A bot closer than KEEP_AWAY to a Bat
    // Rider casting it walks to the nearest spot at least SPOT from every one of them (looked
    // for up to SEARCH yards round the bot, no more than SPOT_HEIGHT above or below it), and
    // nobody within HOLD walks toward anything inside. A bot that can't get that far still goes
    // where it ends up at least WORTH_IT yards further from them than it is.
    constexpr float CONCOCTION_KEEP_AWAY = 14.0f;
    constexpr float CONCOCTION_SPOT = 15.0f;
    constexpr float CONCOCTION_HOLD = 24.0f;
    constexpr float CONCOCTION_WORTH_IT = 2.0f;
    // How far round a bot a way out is looked for, how far above or below it a spot may be (more
    // is a ledge or a pit), how far from the spot the walk there may end, and how long a bot that
    // found nowhere to go waits before it looks again.
    constexpr float WAY_OUT_SEARCH = 21.0f;
    constexpr float WAY_OUT_UP = 2.5f;
    constexpr float WAY_OUT_DOWN = 5.0f;
    constexpr float WAY_OUT_END_SLACK = 1.5f;
    constexpr uint32 WAY_OUT_RETRY_MS = 400;
    // A bot that was sent off and isn't running (the aggro leash stopped the walk, or it ended
    // short) isn't sent again sooner than this: the leash holds a bot it stopped for as long.
    constexpr uint32 WAY_OUT_RESEND_MS = 700;

    // Blaze reaches 5 yd from the middle of a Liquid Fire. A bot closer than KEEP_AWAY walks to
    // the nearest spot at least SPOT from every fire. Whoever Jeklik is on goes TANK_SPOT, when
    // either of them is inside, so that she ends up outside too. Melee don't walk to a target
    // that stands inside.
    constexpr float LIQUID_FIRE_KEEP_AWAY = 6.5f;
    constexpr float LIQUID_FIRE_SPOT = 7.5f;
    constexpr float LIQUID_FIRE_TANK_SPOT = 14.0f;
    constexpr float LIQUID_FIRE_WORTH_IT = 2.0f;
    constexpr float LIQUID_FIRE_SCAN = 40.0f;

    // No spell that hits more than one enemy is cast this close to a mind-controlled raid member.
    constexpr float CHARMED_SPLASH_RANGE = 12.0f;

    std::vector<KillOrderEntry> const& KillOrder();
    // Enemies skull stays off for now, though they are in the table or the player marked them.
    bool KillOrderSkips(Player* bot, Unit const* unit);
    // Thekal's fight moves skull by its own rule (see above); the kill order keeps out of it.
    bool KillOrderSuspended(Player* bot);

    void AddScripts();

    // Bosses the off-tanks build threat on as well (see Common/TankRoles.h).
    std::vector<uint32> const& TankSplitCoTankBosses();
    // Bosses whose threat the tanks share with taunts and the damage dealers stay under.
    std::vector<uint32> const& TankSplitThreatShareBosses();
}

class RaidClearZulGurubStrategy : public Strategy
{
public:
    RaidClearZulGurubStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "rc zg"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
    bool HasTargetExclusions() const override { return true; }
    void AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type) override;
};

// --- Trash ----------------------------------------------------------------

// The bot's target, or its pet's, is one nobody should be hitting (see ZulGurub::KillOrderSkips
// and the strategy's target exclusions).
class RcZgBadTargetTrigger : public Trigger
{
public:
    RcZgBadTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc zg bad target") {}
    bool IsActive() override;
};

class RcZgLeaveBadTargetAction : public AttackAction
{
public:
    RcZgLeaveBadTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "rc zg leave bad target") {}
    bool Execute(Event event) override;
};

// Keeps a bot from switching to such a target in the first place.
class RcZgBadTargetMultiplier : public Multiplier
{
public:
    RcZgBadTargetMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc zg bad target") {}
    float GetValue(Action* action) override;
};

// A Gurubashi Bat Rider is casting Unstable Concoction with the bot in reach.
class RcZgConcoctionTrigger : public Trigger
{
public:
    RcZgConcoctionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc zg concoction") {}
    bool IsActive() override;
};

class RcZgConcoctionMoveAwayAction : public MovementAction
{
public:
    RcZgConcoctionMoveAwayAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc zg concoction move away") {}
    bool Execute(Event event) override;

private:
    uint32 _sentAtMs = 0;
    uint32 _noSpotAtMs = 0;
};

// Keeps bots from walking back to a Bat Rider that is about to go off.
class RcZgConcoctionHoldMultiplier : public Multiplier
{
public:
    RcZgConcoctionHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc zg concoction hold") {}
    float GetValue(Action* action) override;
};

// The bot stands in (or next to) the Liquid Fire of Jeklik's Bat Riders.
class RcZgLiquidFireTrigger : public Trigger
{
public:
    RcZgLiquidFireTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc zg liquid fire") {}
    bool IsActive() override;
};

class RcZgLeaveLiquidFireAction : public MovementAction
{
public:
    RcZgLeaveLiquidFireAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc zg leave liquid fire") {}
    bool Execute(Event event) override;

private:
    uint32 _sentAtMs = 0;
    uint32 _noSpotAtMs = 0;
};

// Keeps melee from walking to a target that stands in Liquid Fire.
class RcZgLiquidFireHoldMultiplier : public Multiplier
{
public:
    RcZgLiquidFireHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc zg liquid fire hold") {}
    float GetValue(Action* action) override;
};

// An enemy has taken over a raid member this bot can stop (Polymorph, Hammer of Justice), or a
// member it stopped is itself again.
class RcZgCharmedMemberTrigger : public Trigger
{
public:
    RcZgCharmedMemberTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc zg charmed member") {}
    bool IsActive() override;
};

class RcZgControlCharmedAction : public Action
{
public:
    RcZgControlCharmedAction(PlayerbotAI* botAI) : Action(botAI, "rc zg control charmed") {}
    bool Execute(Event event) override;
};

// Zeroes spells that hit more than one enemy while a mind-controlled raid member stands where
// they would land.
class RcZgCharmedSplashMultiplier : public Multiplier
{
public:
    RcZgCharmedSplashMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc zg charmed splash") {}
    float GetValue(Action* action) override;
};

// --- High Priest Venoxis --------------------------------------------------

class RcZgHolyWrathKeepAwayTrigger : public Trigger
{
public:
    RcZgHolyWrathKeepAwayTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc zg holy wrath keep away") {}
    bool IsActive() override;
};

class RcZgHolyWrathKeepAwayAction : public MovementAction
{
public:
    RcZgHolyWrathKeepAwayAction(PlayerbotAI* botAI) : MovementAction(botAI, "rc zg holy wrath keep away") {}
    bool Execute(Event event) override;
};

// Keeps bots that are out of the chain from walking back into it (melee reaching Venoxis).
class RcZgHolyWrathHoldMultiplier : public Multiplier
{
public:
    RcZgHolyWrathHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc zg holy wrath hold") {}
    float GetValue(Action* action) override;
};

// --- High Priest Thekal ---------------------------------------------------

// Fires on the marking bot (see Common/KillOrder.h) when skull isn't on the one of Thekal, Zealot
// Lor'Khan and Zealot Zath that has the most health left.
class RcZgThekalEvenKillTrigger : public Trigger
{
public:
    RcZgThekalEvenKillTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc zg thekal even kill") {}
    bool IsActive() override;
};

class RcZgThekalEvenKillAction : public Action
{
public:
    RcZgThekalEvenKillAction(PlayerbotAI* botAI) : Action(botAI, "rc zg thekal even kill") {}
    bool Execute(Event event) override;
};

// While all three are up, zeroes every spell of a non-tank that hits more than one enemy.
class RcZgThekalSingleTargetMultiplier : public Multiplier
{
public:
    RcZgThekalSingleTargetMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "rc zg thekal single target") {}
    float GetValue(Action* action) override;
};

// Zealot Lor'Khan is casting Great Heal and this bot has an interrupt ready and in reach.
class RcZgLorkhanHealTrigger : public Trigger
{
public:
    RcZgLorkhanHealTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rc zg lorkhan heal") {}
    bool IsActive() override;
};

class RcZgLorkhanInterruptAction : public Action
{
public:
    RcZgLorkhanInterruptAction(PlayerbotAI* botAI) : Action(botAI, "rc zg lorkhan interrupt") {}
    bool Execute(Event event) override;
};

#endif
