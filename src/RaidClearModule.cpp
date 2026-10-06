/*
 * mod-raid-clear: registration and the per-map strategy gate.
 *
 * mod-playerbots has no extension API, so this follows mod-dungeon-clear's approach:
 *
 *  1. On the first world tick (after playerbots built its shared contexts in
 *     OnBeforeWorldInitialized) append our strategy/action/trigger contexts to each of the ten
 *     per-class shared lists. Every bot of a class reads its class list by reference, so bots
 *     that already exist see the new names too. The per-class lists are public statics.
 *
 *  2. Install "rc <raid>" on the combat engine of every bot on that raid's map, and strip it
 *     when the bot leaves. Login and map-change hooks do it immediately; a sweep every few
 *     seconds puts it back after anything that resets a bot's strategies inside the raid
 *     (group change, talent swap, `.playerbots reset`).
 *
 * Released under the MIT License.
 */

#include "RaidClearConfig.h"
#include "RaidClearContexts.h"
#include "RaidClearRegistry.h"
#include "Common/TankRoles.h"

#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"

#include "DKAiObjectContext.h"
#include "DruidAiObjectContext.h"
#include "HunterAiObjectContext.h"
#include "MageAiObjectContext.h"
#include "PaladinAiObjectContext.h"
#include "PriestAiObjectContext.h"
#include "RogueAiObjectContext.h"
#include "ShamanAiObjectContext.h"
#include "WarlockAiObjectContext.h"
#include "WarriorAiObjectContext.h"

namespace RaidClear
{
    namespace
    {
        Config sConfig;
        bool sRegistered = false;
    }

    Config const& GetConfig() { return sConfig; }

    void LoadConfig()
    {
        sConfig.enable = sConfigMgr->GetOption<bool>("RaidClear.Enable", true);
        sConfig.killOrder = sConfigMgr->GetOption<bool>("RaidClear.KillOrder", true);
        sConfig.assignMainTank = sConfigMgr->GetOption<bool>("RaidClear.Tanks.AssignMainTank", true);
        sConfig.tankSplit = sConfigMgr->GetOption<bool>("RaidClear.Tanks.Split", true);
        sConfig.tankSeparation = sConfigMgr->GetOption<float>("RaidClear.Tanks.Separation", 12.0f);
        sConfig.separateOnBosses = sConfigMgr->GetOption<bool>("RaidClear.Tanks.SeparateOnBosses", false);
        sConfig.raidEnabled.clear();
        for (RaidEntry const& raid : Raids)
            sConfig.raidEnabled[raid.mapId] =
                sConfigMgr->GetOption<bool>(std::string("RaidClear.") + raid.confKey + ".Enable", true);
    }

    // Install the strategies for the bot's current raid, strip every other raid's.
    void Reconcile(Player* player)
    {
        if (!sRegistered || !player)
            return;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (!botAI)
            return;  // a real player

        Map* map = player->GetMap();
        uint32 const mapId = map ? map->GetId() : 0;

        // The tank split applies in every raid, supported or not.
        bool const wantTanks = sConfig.enable && sConfig.tankSplit && map && map->IsRaid();
        bool const hasTanks = botAI->HasStrategy(TANKS_STRATEGY, BOT_STATE_COMBAT);
        if (wantTanks && !hasTanks)
            botAI->ChangeStrategy(std::string("+") + TANKS_STRATEGY, BOT_STATE_COMBAT);
        else if (!wantTanks && hasTanks)
            botAI->ChangeStrategy(std::string("-") + TANKS_STRATEGY, BOT_STATE_COMBAT);

        for (RaidEntry const& raid : Raids)
        {
            bool const want = raid.mapId == mapId && sConfig.IsRaidEnabled(raid.mapId);
            bool const has = botAI->HasStrategy(raid.strategy, BOT_STATE_COMBAT);
            if (want && !has)
                botAI->ChangeStrategy(std::string("+") + raid.strategy, BOT_STATE_COMBAT);
            else if (!want && has)
                botAI->ChangeStrategy(std::string("-") + raid.strategy, BOT_STATE_COMBAT);
        }
    }

    namespace
    {
        template <class Ctx>
        void RegisterClassContexts()
        {
            Ctx::sharedStrategyContexts.Add(new RaidClearStrategyContext());
            Ctx::sharedActionContexts.Add(new RaidClearActionContext());
            Ctx::sharedTriggerContexts.Add(new RaidClearTriggerContext());
        }
    }
}

using namespace RaidClear;

class RaidClearWorldScript : public WorldScript
{
public:
    RaidClearWorldScript() : WorldScript("RaidClearWorldScript") {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    void OnUpdate(uint32 diff) override
    {
        if (!_started)
        {
            _started = true;

            // Disabled at startup: register nothing, so no bot can ever get an "rc" strategy.
            // Turning it back on needs a restart, which the conf says.
            if (!GetConfig().enable)
                return;

            RegisterClassContexts<WarriorAiObjectContext>();
            RegisterClassContexts<PaladinAiObjectContext>();
            RegisterClassContexts<DruidAiObjectContext>();
            RegisterClassContexts<DKAiObjectContext>();
            RegisterClassContexts<HunterAiObjectContext>();
            RegisterClassContexts<MageAiObjectContext>();
            RegisterClassContexts<PriestAiObjectContext>();
            RegisterClassContexts<RogueAiObjectContext>();
            RegisterClassContexts<ShamanAiObjectContext>();
            RegisterClassContexts<WarlockAiObjectContext>();

            sRegistered = true;
            LOG_INFO("module", "mod-raid-clear: registered raid strategies with mod-playerbots.");
        }

        if (!sRegistered)
            return;

        _sweepMs += diff;
        if (_sweepMs < SWEEP_INTERVAL_MS)
            return;
        _sweepMs = 0;

        // World thread, outside map updates: the same place mod-dungeon-clear reconciles its
        // strategies from.
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            Reconcile(player);

            // Group flags are only touched here, on the world thread.
            if (GetConfig().assignMainTank && player->GetMap() && player->GetMap()->IsRaid())
                Tanks::AssignMainTank(player->GetGroup());
        }
    }

private:
    static constexpr uint32 SWEEP_INTERVAL_MS = 3000;
    uint32 _sweepMs = 0;
    bool _started = false;
};

class RaidClearPlayerScript : public PlayerScript
{
public:
    RaidClearPlayerScript()
        : PlayerScript("RaidClearPlayerScript", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_MAP_CHANGED }) {}

    void OnPlayerLogin(Player* player) override { Reconcile(player); }
    void OnPlayerMapChanged(Player* player) override { Reconcile(player); }
};

void AddRaidClearScripts()
{
    new RaidClearWorldScript();
    new RaidClearPlayerScript();
}
