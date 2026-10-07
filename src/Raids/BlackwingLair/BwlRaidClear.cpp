/*
 * mod-raid-clear: Blackwing Lair (map 469). See BwlRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "BwlRaidClear.h"

#include "GameObject.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"

#include <array>
#include <list>
#include <string>

using namespace RaidClear::BlackwingLair;

std::vector<RaidClear::KillOrderEntry> const& RaidClear::BlackwingLair::KillOrder()
{
    static std::vector<KillOrderEntry> const table = {
        // Each living warlock keeps opening portals that keep summoning felguards.
        { NPC_BLACKWING_WARLOCK, 0 },

        // Heals the pack (Healing Circle), polymorphs, buffs the pack and marks for detonation.
        { NPC_BLACKWING_TASKMASTER, 1 },
        { NPC_BLACKWING_SPELLBINDER, 1 },
        { NPC_DEATH_TALON_CAPTAIN, 1 },
        // The Suppression Room elites, before Broodlord if he gets pulled with them.
        { NPC_DEATH_TALON_HATCHER, 1 },

        // Whatever the portals let out before the warlocks died.
        { NPC_ENRAGED_FELGUARD, 2 },
    };
    return table;
}

std::vector<uint32> const& RaidClear::BlackwingLair::TankSplitCoTankBosses()
{
    // Broodlord: Knock Away (25778) halves his victim's threat. The drakes: Wing Buffet takes 75%.
    // Ebonroc also needs a second tank for Shadow of Ebonroc.
    static std::vector<uint32> const bosses = { NPC_BROODLORD, NPC_FIREMAW, NPC_EBONROC, NPC_FLAMEGOR };
    return bosses;
}

std::vector<uint32> const& RaidClear::BlackwingLair::TankSplitIgnore()
{
    // ~160 of them on a 30s respawn; there's no picking them all up, and every one an off-tank
    // chases is time it isn't on Broodlord.
    static std::vector<uint32> const whelps = {
        NPC_CORRUPTED_RED_WHELP, NPC_CORRUPTED_GREEN_WHELP, NPC_CORRUPTED_BLUE_WHELP, NPC_CORRUPTED_BRONZE_WHELP,
    };
    return whelps;
}

void RaidClearBlackwingLairStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("rc kill order", { NextAction("rc mark kill order", ACTION_RAID + 2) }));

    triggers.push_back(
        new TriggerNode("rc bwl suppression device", { NextAction("rc bwl disarm suppression", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc bwl broodlord ranged", { NextAction("rc bwl broodlord move out", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc bwl ebonroc taunt", { NextAction("rc bwl ebonroc taunt", ACTION_RAID + 3) }));
}

void RaidClearBlackwingLairStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcBwlShadowOfEbonrocMultiplier(botAI));
}

// --- Suppression Room -----------------------------------------------------

namespace
{
    // The Suppression Room's devices, with the aura's reach around them. Outside it there's
    // nothing to look for, so the grid search is skipped for most of the instance.
    bool InSuppressionRoom(Player* bot)
    {
        float const x = bot->GetPositionX();
        float const y = bot->GetPositionY();
        return x > -7706.0f && x < -7547.0f && y > -1149.0f && y < -939.0f;
    }

    // Same rule as mod-playerbots' own disarm: rogues always, everyone else with the "raid" cheat.
    bool MayDisarm(PlayerbotAI* botAI, Player* bot)
    {
        return bot->IsClass(CLASS_ROGUE) || botAI->HasCheat(BotCheatMask::raid);
    }

    // Armed devices in reach, despawned ones included: without mod-raid-bwl, a device a player
    // disarmed is invisible but still in the "ready" state and still casting.
    void ArmedDevicesInReach(Player* bot, std::list<GameObject*>& out)
    {
        std::list<GameObject*> found;
        bot->GetGameObjectListWithEntryInGrid(found, GO_SUPPRESSION_DEVICE, SUPPRESSION_DISARM_RANGE);
        for (GameObject* go : found)
            if (go && go->GetGoState() == GO_STATE_READY)
                out.push_back(go);
    }
}

bool RcBwlSuppressionDeviceTrigger::IsActive()
{
    if (!InSuppressionRoom(bot) || !MayDisarm(botAI, bot))
        return false;

    std::list<GameObject*> armed;
    ArmedDevicesInReach(bot, armed);
    return !armed.empty();
}

bool RcBwlDisarmSuppressionAction::Execute(Event /*event*/)
{
    std::list<GameObject*> armed;
    ArmedDevicesInReach(bot, armed);

    // What mod-playerbots' disarm does. mod-raid-bwl, if installed, passes it on to the device's
    // script.
    for (GameObject* go : armed)
        go->SetGoState(GO_STATE_ACTIVE);
    return !armed.empty();
}

// --- Broodlord Lashlayer --------------------------------------------------

bool RcBwlBroodlordRangedTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot) || !(PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot)))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "broodlord lashlayer");
    if (!boss || !boss->IsAlive() || !boss->IsInCombat())
        return false;

    return bot->GetDistance2d(boss) < BROODLORD_RANGED_MIN;
}

bool RcBwlBroodlordMoveOutAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "broodlord lashlayer");
    if (!boss)
        return false;

    float const distance = BROODLORD_RANGED_TARGET - bot->GetDistance2d(boss);
    if (distance <= 0.0f)
        return false;

    return MoveAway(boss, distance);
}

// --- Ebonroc --------------------------------------------------------------

namespace
{
    // Every tank class's single-target taunt, by its playerbots spell name.
    constexpr std::array<char const*, 4> TAUNTS = { "taunt", "growl", "hand of reckoning", "dark command" };

    // The other taunt-like actions a tank strategy can pick.
    constexpr std::array<char const*, 8> TAUNT_ACTIONS = {
        "taunt", "growl", "hand of reckoning", "dark command",
        "mocking blow", "righteous defense", "challenging shout", "challenging roar",
    };

    Unit* EbonrocInCombat(PlayerbotAI* botAI)
    {
        Unit* boss = botAI->GetAiObjectContext()->GetValue<Unit*>("find target", "ebonroc")->Get();
        return boss && boss->IsAlive() && boss->IsInCombat() ? boss : nullptr;
    }
}

bool RcBwlEbonrocTauntTrigger::IsActive()
{
    if (!PlayerbotAI::IsTank(bot) || bot->HasAura(SPELL_SHADOW_OF_EBONROC))
        return false;

    Unit* boss = EbonrocInCombat(botAI);
    if (!boss)
        return false;

    Unit* victim = boss->GetVictim();
    return victim && victim != bot && victim->HasAura(SPELL_SHADOW_OF_EBONROC);
}

bool RcBwlEbonrocTauntAction::Execute(Event /*event*/)
{
    Unit* boss = EbonrocInCombat(botAI);
    if (!boss)
        return false;

    for (char const* taunt : TAUNTS)
        if (botAI->CanCastSpell(taunt, boss) && botAI->CastSpell(taunt, boss))
            return true;
    return false;
}

float RcBwlShadowOfEbonrocMultiplier::GetValue(Action* action)
{
    if (!action || !bot->HasAura(SPELL_SHADOW_OF_EBONROC) || !EbonrocInCombat(botAI))
        return 1.0f;

    std::string const name = action->getName();
    for (char const* taunt : TAUNT_ACTIONS)
        if (name == taunt)
            return 0.0f;
    return 1.0f;
}
