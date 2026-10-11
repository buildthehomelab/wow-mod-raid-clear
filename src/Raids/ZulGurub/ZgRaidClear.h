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
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_ZUL_GURUB_H
#define MOD_RAID_CLEAR_ZUL_GURUB_H

#include "Common/KillOrder.h"

#include "Action.h"
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
    };

    enum Spells : uint32
    {
        SPELL_VENOXIS_TRANSFORM = 23849,  // snake form, from 50%: no more Holy Wrath
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

    std::vector<KillOrderEntry> const& KillOrder();

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

#endif
