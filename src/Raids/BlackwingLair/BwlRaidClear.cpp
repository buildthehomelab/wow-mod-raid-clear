/*
 * mod-raid-clear: Blackwing Lair (map 469). See BwlRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "BwlRaidClear.h"

#include "RaidClearConfig.h"

#include "GameObject.h"
#include "GameTime.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "GenericSpellActions.h"
#include "PathGenerator.h"
#include "PlayerScript.h"
#include "Playerbots.h"
#include "Spell.h"
#include "PlayerbotAI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <list>
#include <string>

using namespace RaidClear::BlackwingLair;

std::vector<RaidClear::KillOrderEntry> const& RaidClear::BlackwingLair::KillOrder()
{
    static std::vector<KillOrderEntry> const table = {
        // Each living warlock keeps opening portals that keep summoning felguards.
        { NPC_BLACKWING_WARLOCK, 0 },

        // Heals the pack (Healing Circle), polymorphs.
        { NPC_BLACKWING_TASKMASTER, 1 },
        { NPC_BLACKWING_SPELLBINDER, 1 },
        // The Suppression Room elites, before Broodlord if he gets pulled with them.
        { NPC_DEATH_TALON_HATCHER, 1 },

        // Death Talon packs: the Wyrmkin's Fireball Volley hits the whole raid, so they go first.
        // The Captain is off on his own tank (Mark of Detonation) and dies last, once the melee
        // has nothing else to hit near the main tank.
        { NPC_DEATH_TALON_WYRMKIN, 1 },
        { NPC_DEATH_TALON_FLAMESCALE, 2 },
        { NPC_DEATH_TALON_SEETHER, 2 },
        { NPC_DEATH_TALON_CAPTAIN, 3 },

        // Razorgore's adds: dragonkin, then mages, then the melee.
        { NPC_DEATH_TALON_DRAGONSPAWN, 1 },
        { NPC_BLACKWING_MAGE, 2 },
        { NPC_BLACKWING_LEGIONNAIRE, 3 },

        // Nefarian's shaman call: corrupted totems. The Healing Stream heals him, so it dies first;
        // Stoneskin and Windfury buff him and his adds within 40 yd. They're immune to area
        // damage, so it takes the skull. The Fire Nova totem isn't here: it goes off 4s after it
        // lands whatever happens, so bots walk away from it instead of running onto it.
        { NPC_CORRUPTED_HEALING_TOTEM, 0 },
        { NPC_CORRUPTED_STONESKIN_TOTEM, 1 },
        { NPC_CORRUPTED_WINDFURY_TOTEM, 1 },

        // Nefarian: adds always before him.
        { NPC_CHROMATIC_DRAKONID, 1 },
        { NPC_BLUE_DRAKONID, 1 },
        { NPC_GREEN_DRAKONID, 1 },
        { NPC_BRONZE_DRAKONID, 1 },
        { NPC_RED_DRAKONID, 1 },
        { NPC_BLACK_DRAKONID, 1 },
        { NPC_BONE_CONSTRUCT, 2 },
        // His warlock call summons them on the warlocks, in the middle of the ranged and healers.
        { NPC_CORRUPTED_INFERNAL, 1 },

        // Whatever the portals let out before the warlocks died.
        { NPC_ENRAGED_FELGUARD, 2 },
    };
    return table;
}

std::vector<uint32> const& RaidClear::BlackwingLair::TankSplitCoTankBosses()
{
    // Broodlord: Knock Away (25778) halves his victim's threat. The drakes: Wing Buffet takes 75%.
    // Ebonroc also needs a second tank for Shadow of Ebonroc. Vaelastrasz can't be taunted, so
    // whoever is next on threat when Burning Adrenaline kills his tank gets him.
    static std::vector<uint32> const bosses = {
        NPC_BROODLORD, NPC_FIREMAW, NPC_EBONROC, NPC_FLAMEGOR, NPC_VAELASTRASZ,
    };
    return bosses;
}

std::vector<uint32> const& RaidClear::BlackwingLair::TankSplitSkipBosses()
{
    static std::vector<uint32> const bosses = { NPC_RAZORGORE };
    return bosses;
}

std::vector<RaidClear::Tanks::SplashRadius> const& RaidClear::BlackwingLair::TankSplitSplashRadii()
{
    // War Stomp (24375) reaches 15 yd: two Wyrmguards end up 30 yd apart.
    static std::vector<Tanks::SplashRadius> const radii = {
        { NPC_DEATH_TALON_WYRMGUARD, 15.0f },
        // Not his own AoE: the Mark of Detonation on his tank, 30 yd around it.
        { NPC_DEATH_TALON_CAPTAIN, CAPTAIN_SPLASH },
    };
    return radii;
}

std::vector<uint32> const& RaidClear::BlackwingLair::TankSplitOwnTankAdds()
{
    static std::vector<uint32> const adds = { NPC_DEATH_TALON_CAPTAIN };
    return adds;
}

std::vector<uint32> const& RaidClear::BlackwingLair::TankSplitSpreadAdds()
{
    // Three of them before Ebonroc (and before Flamegor), each hitting like a drake and stomping
    // 15 yd: two on one tank kill it.
    static std::vector<uint32> const adds = { NPC_DEATH_TALON_WYRMGUARD };
    return adds;
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

namespace
{
    Unit* LivingBossInCombat(PlayerbotAI* botAI, char const* name)
    {
        Unit* boss = botAI->GetAiObjectContext()->GetValue<Unit*>("find target", name)->Get();
        return boss && boss->IsAlive() && boss->IsInCombat() ? boss : nullptr;
    }
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

    triggers.push_back(
        new TriggerNode("rc bwl technician spread", { NextAction("rc bwl technician spread", ACTION_RAID) }));

    triggers.push_back(
        new TriggerNode("rc bwl firemaw hide", { NextAction("rc bwl firemaw hide", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc bwl chromaggus breath", { NextAction("rc bwl chromaggus hide", ACTION_RAID + 4) }));

    triggers.push_back(
        new TriggerNode("rc bwl nefarian ranged", { NextAction("rc bwl nefarian move out", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc bwl detonation keep away", { NextAction("rc bwl detonation keep away", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("rc bwl seether tranq", { NextAction("rc bwl seether tranq", ACTION_RAID) }));
    triggers.push_back(new TriggerNode("rc bwl fire nova totem",
                                       { NextAction("rc bwl fire nova totem move away", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc bwl black affliction",
                                       { NextAction("rc bwl remove black affliction", ACTION_RAID + 1) }));

    // Above playerbots' "tank assist" (50), below the raid mechanics.
    triggers.push_back(
        new TriggerNode("rc bwl captain hand off", { NextAction("rc bwl captain hand off", ACTION_RAID - 3) }));
}

void RaidClearBlackwingLairStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcBwlShadowOfEbonrocMultiplier(botAI));
    multipliers.push_back(new RcBwlHoldCoverMultiplier(botAI));
    multipliers.push_back(new RcBwlDetonationHoldMultiplier(botAI));
    multipliers.push_back(new RcBwlCaptainMainTankMultiplier(botAI));
    multipliers.push_back(new RcBwlCorruptedHealingMultiplier(botAI));
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

    // Armed devices in reach, despawned ones included: before core 2a2211c, a device a player
    // disarmed despawns but stays in the "ready" state and keeps casting. From 2a2211c the
    // device script retracts a player-disarmed device instead (GO_STATE_ACTIVE), so the state
    // check below skips it.
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

    // What mod-playerbots' disarm does. The device's cast checks for GO_STATE_READY, so this stops
    // it on any core. From core 2a2211c the device script re-arms a device 30-120s after a
    // player's Disarm Trap, but it only hears about loot state changes, so a device turned off
    // here stays off.
    for (GameObject* go : armed)
        go->SetGoState(GO_STATE_ACTIVE);
    return !armed.empty();
}

// --- Technician packs -----------------------------------------------------

namespace
{
    bool SpreadsFromBombs(Player* bot)
    {
        return !PlayerbotAI::IsTank(bot) && (PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot));
    }

    // Push away from every living group member closer than TECHNICIAN_SPREAD, nearer ones
    // harder. Zero when nobody is that close.
    void Repulsion(Player* bot, float& outX, float& outY)
    {
        outX = 0.0f;
        outY = 0.0f;
        Group* group = bot->GetGroup();
        if (!group)
            return;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                continue;

            float const dist = bot->GetExactDist2d(member);
            if (dist >= TECHNICIAN_SPREAD)
                continue;

            float const weight = 1.0f - dist / TECHNICIAN_SPREAD;
            if (dist < 0.1f)
            {
                // Standing on top of each other: split by GUID so the two go opposite ways.
                float const angle = bot->GetGUID() < member->GetGUID() ? 0.0f : float(M_PI);
                outX += std::cos(angle) * weight;
                outY += std::sin(angle) * weight;
                continue;
            }
            outX += (bot->GetPositionX() - member->GetPositionX()) / dist * weight;
            outY += (bot->GetPositionY() - member->GetPositionY()) / dist * weight;
        }
    }

    bool TechnicianFighting(Player* bot)
    {
        std::list<Creature*> technicians;
        bot->GetCreatureListWithEntryInGrid(technicians, NPC_BLACKWING_TECHNICIAN, TECHNICIAN_RANGE);
        for (Creature* technician : technicians)
            if (technician && technician->IsAlive() && technician->IsInCombat())
                return true;
        return false;
    }
}

bool RcBwlTechnicianSpreadTrigger::IsActive()
{
    if (!SpreadsFromBombs(bot) || !bot->IsInCombat())
        return false;

    uint64 const now = GameTime::GetGameTimeMS().count();
    if (now < _nextStepMs)
        return false;

    // The cheap test first: only a bot in a clump has anything to do.
    float x, y;
    Repulsion(bot, x, y);
    if (x == 0.0f && y == 0.0f)
        return false;

    if (!TechnicianFighting(bot))
        return false;

    _nextStepMs = now + TECHNICIAN_STEP_INTERVAL_MS;
    return true;
}

bool RcBwlTechnicianSpreadAction::Execute(Event /*event*/)
{
    float pushX, pushY;
    Repulsion(bot, pushX, pushY);
    float const length = std::sqrt(pushX * pushX + pushY * pushY);
    if (length < 0.01f)
        return false;

    Map* map = bot->GetMap();
    Unit* target = AI_VALUE(Unit*, "current target");
    float const base = std::atan2(pushY, pushX);

    // Straight out of the clump first, then slide along walls; keep sight of the target so
    // casters don't step out of their own fight.
    static float const offsets[] = { 0.0f, float(M_PI_4), -float(M_PI_4), float(M_PI_2), -float(M_PI_2) };
    for (float const offset : offsets)
    {
        float const angle = base + offset;
        float const x = bot->GetPositionX() + std::cos(angle) * TECHNICIAN_STEP;
        float const y = bot->GetPositionY() + std::sin(angle) * TECHNICIAN_STEP;
        float const z = map->GetHeight(bot->GetPhaseMask(), x, y, bot->GetPositionZ() + 3.0f);

        if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > 3.0f)
            continue;
        if (!bot->IsWithinLOS(x, y, z + 2.0f))
            continue;
        if (target && !target->IsWithinLOS(x, y, z + 2.0f))
            continue;

        if (MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
            return true;
    }
    return false;
}

// --- Line-of-sight cover (Firemaw, Chromaggus) ----------------------------

namespace
{
    // Eye height used for the boss-to-spot line of sight.
    constexpr float COVER_EYE_HEIGHT = 2.0f;
    // A spot more than this above or below the bot is a ledge or a pit.
    constexpr float COVER_MAX_HEIGHT_DIFF = 5.0f;
    // How long a found spot is trusted before searching again, and how long to wait after a
    // search found nothing (each one is up to ~150 line-of-sight checks and paths).
    constexpr uint64 COVER_REUSE_MS = 15000;
    constexpr uint64 COVER_RETRY_MS = 3000;

    bool SeesSpot(Unit* from, float x, float y, float z)
    {
        return from->IsWithinLOS(x, y, z + COVER_EYE_HEIGHT);
    }

    // The nearest spot (by walking distance, at most `maxPath`) within `range` that `from` can't
    // see. Rings of candidates from close to far; the first ring with any cover wins.
    bool FindCover(Player* bot, Unit* from, float range, float maxPath, Position& out)
    {
        Map* map = bot->GetMap();
        float const botZ = bot->GetPositionZ();
        constexpr int ANGLES = 16;

        for (float radius = 4.0f; radius <= range; radius += 3.0f)
        {
            float bestLength = 0.0f;
            bool found = false;
            for (int i = 0; i < ANGLES; ++i)
            {
                float const angle = float(i) * 2.0f * float(M_PI) / ANGLES;
                float const x = bot->GetPositionX() + std::cos(angle) * radius;
                float const y = bot->GetPositionY() + std::sin(angle) * radius;
                float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + COVER_MAX_HEIGHT_DIFF);
                if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > COVER_MAX_HEIGHT_DIFF)
                    continue;
                if (SeesSpot(from, x, y, z))
                    continue;

                // Cover is usually around a corner, so the walk is checked, not the straight line.
                PathGenerator path(bot);
                if (!path.CalculatePath(x, y, z) || !(path.GetPathType() & PATHFIND_NORMAL))
                    continue;
                float const length = path.getPathLength();
                if (length > maxPath)
                    continue;

                if (!found || length < bestLength)
                {
                    out.Relocate(x, y, z);
                    bestLength = length;
                    found = true;
                }
            }
            if (found)
                return true;
        }
        return false;
    }

    bool IsAvoidableBreath(uint32 spellId)
    {
        switch (spellId)
        {
            case SPELL_INCINERATE:
            case SPELL_CORROSIVE_ACID:
            case SPELL_IGNITE_FLESH:
            case SPELL_FROST_BURN:
                return true;
            default:
                return false;  // Time Lapse included: everyone should take it
        }
    }

    // The breath Chromaggus is casting right now, or 0.
    uint32 ChromaggusBreath(Unit* boss)
    {
        Spell* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell || !spell->GetSpellInfo())
            return 0;
        uint32 const id = spell->GetSpellInfo()->Id;
        return IsAvoidableBreath(id) ? id : 0;
    }
}

bool RcBwlHideAction::Execute(Event /*event*/)
{
    Unit* from = HideFrom();
    if (!from)
        return false;

    // Already out of sight: nothing to do. RcBwlHoldCoverMultiplier keeps other movement from
    // walking the bot back into view, while heals and casts at targets it can see go on.
    if (!from->IsWithinLOSInMap(bot))
        return false;

    uint64 const now = GameTime::GetGameTimeMS().count();
    bool const cached = _coverFrom == from->GetGUID() && now - _coverFoundMs < COVER_REUSE_MS &&
                        !SeesSpot(from, _cover.GetPositionX(), _cover.GetPositionY(), _cover.GetPositionZ());
    if (!cached)
    {
        if (now < _noCoverUntilMs)
            return false;
        if (!FindCover(bot, from, _range, _maxPath, _cover))
        {
            _noCoverUntilMs = now + COVER_RETRY_MS;
            return false;  // nowhere to hide: fight on
        }
        _coverFrom = from->GetGUID();
        _coverFoundMs = now;
    }

    return MoveTo(bot->GetMapId(), _cover.GetPositionX(), _cover.GetPositionY(), _cover.GetPositionZ(), false, false,
                  false, false, MovementPriority::MOVEMENT_FORCED);
}

namespace
{
    // Hidden from Firemaw while Flame Buffet wears off, or from Chromaggus mid-breath.
    bool HoldingCover(PlayerbotAI* botAI, Player* bot)
    {
        if (bot->HasAura(SPELL_FLAME_BUFFET))
            if (Unit* boss = LivingBossInCombat(botAI, "firemaw"))
                if (!boss->IsWithinLOSInMap(bot))
                    return true;

        if (Unit* boss = LivingBossInCombat(botAI, "chromaggus"))
            if (ChromaggusBreath(boss) && !boss->IsWithinLOSInMap(bot))
                return true;
        return false;
    }

    // Firemaw stack threshold for this bot: 5 to 7, so the raid doesn't all leave together.
    uint8 FlameBuffetThreshold(Player* bot)
    {
        return FLAME_BUFFET_HIDE_STACKS + uint8(bot->GetGUID().GetCounter() % FLAME_BUFFET_HIDE_SPREAD);
    }

    // Healers may hide only while enough of the others are still healing.
    bool HealerMayHide(Player* bot, Unit* boss)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return true;

        uint32 healers = 0;
        uint32 hidden = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId() || !PlayerbotAI::IsHeal(member))
                continue;
            ++healers;
            if (member != bot && member->HasAura(SPELL_FLAME_BUFFET) && !boss->IsWithinLOSInMap(member))
                ++hidden;
        }
        return hidden < std::max<uint32>(1, healers / FIREMAW_HEALER_HIDE_SHARE);
    }
}

float RcBwlHoldCoverMultiplier::GetValue(Action* action)
{
    if (!action || dynamic_cast<RcBwlHideAction*>(action) || !dynamic_cast<MovementAction*>(action) ||
        PlayerbotAI::IsTank(bot))
        return 1.0f;
    return HoldingCover(botAI, bot) ? 0.0f : 1.0f;
}

bool RcBwlFiremawHideTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;

    Aura* buffet = bot->GetAura(SPELL_FLAME_BUFFET);
    if (!buffet)
        return false;

    Unit* boss = LivingBossInCombat(botAI, "firemaw");
    if (!boss)
        return false;

    // Once hidden, stay hidden until the last stack is gone.
    if (!boss->IsWithinLOSInMap(bot))
        return true;

    // Thresholds are staggered per bot, and healers take turns, so the tanks always have heals.
    if (buffet->GetStackAmount() < FlameBuffetThreshold(bot))
        return false;
    return !PlayerbotAI::IsHeal(bot) || HealerMayHide(bot, boss);
}

Unit* RcBwlFiremawHideAction::HideFrom()
{
    return LivingBossInCombat(botAI, "firemaw");
}

bool RcBwlChromaggusBreathTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;

    Unit* boss = LivingBossInCombat(botAI, "chromaggus");
    return boss && ChromaggusBreath(boss) != 0;
}

Unit* RcBwlChromaggusHideAction::HideFrom()
{
    Unit* boss = LivingBossInCombat(botAI, "chromaggus");
    return boss && ChromaggusBreath(boss) ? boss : nullptr;
}

// --- Keep out of a boss's AoE (Broodlord, Nefarian) -----------------------

namespace
{
    // Healers stay this close to the boss's target, whatever the AoE: an out-of-range healer
    // costs more than one Blast Wave or fear.
    constexpr float HEALER_REACH = 38.0f;

    bool KeepsOut(Player* bot)
    {
        return !PlayerbotAI::IsTank(bot) && (PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot));
    }

    // The spot `distance` yards (edge to edge) straight out from the boss through the bot.
    Position KeepOutSpot(Player* bot, Unit* boss, float distance)
    {
        float angle = boss->GetAngle(bot);
        if (boss->GetExactDist2d(bot) < 0.5f)
            angle = boss->GetOrientation() + float(M_PI);

        float const reach = distance + boss->GetObjectSize() + bot->GetObjectSize();
        float const x = boss->GetPositionX() + std::cos(angle) * reach;
        float const y = boss->GetPositionY() + std::sin(angle) * reach;
        float const z = bot->GetMap()->GetHeight(bot->GetPhaseMask(), x, y, bot->GetPositionZ() + 5.0f);
        return Position(x, y, z > INVALID_HEIGHT ? z : bot->GetPositionZ());
    }

    // A healer only backs off if it can still reach whoever the boss is hitting from there.
    bool SpotKeepsHealerInReach(Player* bot, Unit* boss, Position const& spot)
    {
        if (!PlayerbotAI::IsHeal(bot))
            return true;
        Unit* victim = boss->GetVictim();
        return !victim || victim->GetExactDist2d(spot.GetPositionX(), spot.GetPositionY()) <= HEALER_REACH;
    }
}

bool RcBwlKeepOutTrigger::IsActive()
{
    if (!KeepsOut(bot))
        return false;

    Unit* boss = LivingBossInCombat(botAI, _bossName);
    if (!boss || bot->GetDistance2d(boss) >= _minDistance)
        return false;

    return SpotKeepsHealerInReach(bot, boss, KeepOutSpot(bot, boss, _targetDistance));
}

bool RcBwlKeepOutAction::Execute(Event /*event*/)
{
    Unit* boss = LivingBossInCombat(botAI, _bossName);
    if (!boss || bot->GetDistance2d(boss) >= _targetDistance)
        return false;

    Position const spot = KeepOutSpot(bot, boss, _targetDistance);
    if (!SpotKeepsHealerInReach(bot, boss, spot))
        return false;

    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false, false,
                  false, MovementPriority::MOVEMENT_COMBAT);
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
}

bool RcBwlEbonrocTauntTrigger::IsActive()
{
    if (!PlayerbotAI::IsTank(bot) || bot->HasAura(SPELL_SHADOW_OF_EBONROC))
        return false;

    Unit* boss = LivingBossInCombat(botAI, "ebonroc");
    if (!boss)
        return false;

    Unit* victim = boss->GetVictim();
    return victim && victim != bot && victim->HasAura(SPELL_SHADOW_OF_EBONROC);
}

bool RcBwlEbonrocTauntAction::Execute(Event /*event*/)
{
    Unit* boss = LivingBossInCombat(botAI, "ebonroc");
    if (!boss)
        return false;

    for (char const* taunt : TAUNTS)
        if (botAI->CanCastSpell(taunt, boss) && botAI->CastSpell(taunt, boss))
            return true;
    return false;
}

float RcBwlShadowOfEbonrocMultiplier::GetValue(Action* action)
{
    if (!action || !bot->HasAura(SPELL_SHADOW_OF_EBONROC) || !LivingBossInCombat(botAI, "ebonroc"))
        return 1.0f;

    std::string const name = action->getName();
    for (char const* taunt : TAUNT_ACTIONS)
        if (name == taunt)
            return 0.0f;
    return 1.0f;
}

// --- Death Talon packs ----------------------------------------------------

namespace
{
    // A Captain or a Wyrmguard the main tank leaves to an off-tank (see Tanks::HandedOffAdds).
    bool MainTankLeaves(PlayerbotAI* botAI, Player* bot, Unit* unit)
    {
        if (!RaidClear::GetConfig().tankSplit || !unit ||
            (!RaidClear::Tanks::IsOwnTankAdd(unit) && !RaidClear::Tanks::IsSpreadAdd(unit)))
            return false;
        if (RaidClear::Tanks::ActingMainTank(bot->GetGroup()) != bot)
            return false;

        GuidVector const& attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        return RaidClear::Tanks::HandedOffAdds(botAI, bot, attackers).count(unit->GetGUID()) != 0;
    }

    // The explosion hits the marked player's allies; tanks stay with their mobs. A non-tank that
    // carries a Mark itself still keeps out of another one.
    bool AvoidsDetonation(Player* bot)
    {
        return !PlayerbotAI::IsTank(bot);
    }

    // The nearest other group member carrying Mark of Detonation within `range` of `pos`.
    Player* MarkedAllyNear(Player* bot, WorldObject const* pos, float range)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        Player* nearest = nullptr;
        float best = range;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || !member->IsInMap(bot) ||
                !member->HasAura(SPELL_MARK_OF_DETONATION))
                continue;

            float const dist = pos->GetExactDist(member);
            if (dist < best)
            {
                nearest = member;
                best = dist;
            }
        }
        return nearest;
    }
}

void RaidClearBlackwingLairStrategy::AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type)
{
    // The main tank's own target picking skips the Captains and Wyrmguards its off-tanks take.
    if (type != TargetValueExclusionType::Tank || !RaidClear::GetConfig().tankSplit)
        return;

    Player* bot = botAI->GetBot();
    if (RaidClear::Tanks::ActingMainTank(bot->GetGroup()) != bot)
        return;

    GuidVector const& attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const& guid : RaidClear::Tanks::HandedOffAdds(botAI, bot, attackers))
        exclusions.insert(guid);
}

bool RcBwlCaptainHandOffTrigger::IsActive()
{
    return bot->IsInCombat() && MainTankLeaves(botAI, bot, AI_VALUE(Unit*, "current target"));
}

bool RcBwlCaptainHandOffAction::Execute(Event /*event*/)
{
    // "tank target" already skips the Captain (the exclusions above). "tank assist" only switches
    // targets while the tank has aggro on its current one, so it never moves off him by itself.
    Unit* next = AI_VALUE(Unit*, "tank target");
    if (next && next != AI_VALUE(Unit*, "current target") && !MainTankLeaves(botAI, bot, next))
        return Attack(next);

    // Nothing else to hold: stop rather than follow him to his tank.
    bot->AttackStop();
    context->GetValue<Unit*>("current target")->Set(nullptr);
    return true;
}

bool RcBwlDetonationKeepAwayTrigger::IsActive()
{
    return bot->IsInCombat() && AvoidsDetonation(bot) && MarkedAllyNear(bot, bot, DETONATION_KEEP_AWAY);
}

bool RcBwlDetonationKeepAwayAction::Execute(Event /*event*/)
{
    Player* marked = MarkedAllyNear(bot, bot, DETONATION_KEEP_AWAY);
    Group* group = bot->GetGroup();
    if (!marked || !group)
        return false;

    // Out of the circle, on the side nearest the main tank: that's where the healers, the melee
    // and the rest of the pack are.
    Player* mainTank = RaidClear::Tanks::ActingMainTank(bot->GetGroup());
    Map* map = bot->GetMap();
    float const base = marked->GetAngle(bot);
    float const reach = DETONATION_KEEP_AWAY_TARGET + marked->GetObjectSize() + bot->GetObjectSize();

    static float const offsets[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f, 2.0f, -2.0f };
    bool found = false;
    float bestX = 0.0f, bestY = 0.0f, bestZ = 0.0f, bestScore = 0.0f;
    for (float const offset : offsets)
    {
        float const angle = base + offset;
        float const x = marked->GetPositionX() + std::cos(angle) * reach;
        float const y = marked->GetPositionY() + std::sin(angle) * reach;
        float const z = map->GetHeight(bot->GetPhaseMask(), x, y, bot->GetPositionZ() + 5.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > 5.0f)
            continue;
        if (!bot->IsWithinLOS(x, y, z + 2.0f))
            continue;
        // Not into another marked player's circle.
        Position const spot(x, y, z);
        bool inOther = false;
        for (GroupReference* ref = group->GetFirstMember(); ref && !inOther; ref = ref->next())
            if (Player* member = ref->GetSource())
                inOther = member != bot && member != marked && member->IsAlive() && member->IsInMap(bot) &&
                          member->HasAura(SPELL_MARK_OF_DETONATION) &&
                          member->GetExactDist(&spot) < DETONATION_KEEP_AWAY;
        if (inOther)
            continue;

        float const score = mainTank && mainTank != marked ? mainTank->GetExactDist(&spot) : bot->GetExactDist(&spot);
        if (!found || score < bestScore)
        {
            found = true;
            bestX = x;
            bestY = y;
            bestZ = z;
            bestScore = score;
        }
    }

    return found &&
           MoveTo(bot->GetMapId(), bestX, bestY, bestZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT);
}

float RcBwlDetonationHoldMultiplier::GetValue(Action* action)
{
    // Picking a target isn't movement; only walking somewhere is held.
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action) ||
        dynamic_cast<RcBwlDetonationKeepAwayAction*>(action))
        return 1.0f;

    if (!bot->IsInCombat() || !AvoidsDetonation(bot))
        return 1.0f;

    // Only movement toward something inside a marked player's circle, and only once the bot is
    // already close enough to work from where it stands. A healer further out may still come in
    // to heal the marked tank; it stops at spell range, outside the circle.
    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    // Walking toward the marked player itself only happens to heal it (into range or line of
    // sight); keep-away pulls the healer back out if that takes it too close.
    if (target->ToPlayer() && target->ToPlayer()->HasAura(SPELL_MARK_OF_DETONATION))
        return 1.0f;

    Player* marked = MarkedAllyNear(bot, target, DETONATION_KEEP_AWAY);
    if (!marked || bot->GetExactDist(marked) > DETONATION_HOLD + 4.0f)
        return 1.0f;
    return 0.0f;
}

float RcBwlCaptainMainTankMultiplier::GetValue(Action* action)
{
    bool const taunt = std::find(TAUNT_ACTIONS.begin(), TAUNT_ACTIONS.end(), action->getName()) != TAUNT_ACTIONS.end();
    if (!taunt && !dynamic_cast<AttackAction*>(action))
        return 1.0f;
    if (dynamic_cast<RcBwlCaptainHandOffAction*>(action) || !PlayerbotAI::IsTank(bot))
        return 1.0f;

    return MainTankLeaves(botAI, bot, action->GetTarget()) ? 0.0f : 1.0f;
}

namespace
{
    // An enraged Seether within Tranquilizing Shot range that this hunter should take: the nearest
    // of the group's hunter bots that can shoot it now, so they don't all fire at the same one.
    Creature* SeetherToTranq(PlayerbotAI* botAI, Player* bot)
    {
        std::list<Creature*> seethers;
        bot->GetCreatureListWithEntryInGrid(seethers, NPC_DEATH_TALON_SEETHER, TRANQUILIZING_SHOT_RANGE);
        for (Creature* seether : seethers)
        {
            if (!seether->IsAlive() || !seether->HasAura(SPELL_SEETHER_ENRAGE) ||
                !botAI->CanCastSpell("tranquilizing shot", seether))
                continue;

            float const mine = bot->GetExactDist(seether);
            bool closer = false;
            if (Group* group = bot->GetGroup())
                for (GroupReference* ref = group->GetFirstMember(); ref && !closer; ref = ref->next())
                    if (Player* member = ref->GetSource())
                    {
                        if (member == bot || !member->IsAlive() || !member->IsClass(CLASS_HUNTER) ||
                            !member->IsInMap(bot) || member->GetExactDist(seether) >= mine)
                            continue;
                        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
                        closer = memberAI && memberAI->CanCastSpell("tranquilizing shot", seether);
                    }
            if (!closer)
                return seether;
        }
        return nullptr;
    }
}

bool RcBwlSeetherTranqTrigger::IsActive()
{
    return bot->IsClass(CLASS_HUNTER) && bot->IsInCombat() && SeetherToTranq(botAI, bot);
}

bool RcBwlSeetherTranqAction::Execute(Event /*event*/)
{
    Creature* seether = SeetherToTranq(botAI, bot);
    return seether && botAI->CastSpell("tranquilizing shot", seether);
}

// --- Chromaggus: Brood Affliction: Black ----------------------------------

namespace
{
    // Remove Curse (mage and druid) and Cleanse Spirit all reach 40 yd.
    constexpr float CURSE_REMOVAL_RANGE = 40.0f;
    constexpr std::array<char const*, 2> CURSE_REMOVALS = { "remove curse", "cleanse spirit" };
    // Healers leave Black alone while a tank is this low: their global cooldown is a heal first.
    constexpr float BLACK_HEALER_TANK_PCT = 50.0f;
    // The trigger's answer, reused by the action that follows it on the same tick.
    constexpr uint32 BLACK_CACHE_MS = 100;

    bool CanRemoveCurses(Player* player)
    {
        switch (player->getClass())
        {
            case CLASS_MAGE:   return player->HasSpell(SPELL_MAGE_REMOVE_CURSE);
            case CLASS_DRUID:  return player->HasSpell(SPELL_DRUID_REMOVE_CURSE);
            case CLASS_SHAMAN: return player->HasSpell(SPELL_CLEANSE_SPIRIT);
            default:           return false;
        }
    }

    // The Black-afflicted group member this bot should cleanse. Everyone who can remove curses
    // takes every n-th cursed player (n = removers), so they don't all hit the same one and waste
    // the global cooldown; if its own is out of reach, the first one it can reach. Healers sit it
    // out while a tank is low.
    Player* FindBlackToRemove(PlayerbotAI* botAI, Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group || !CanRemoveCurses(bot) || !LivingBossInCombat(botAI, "chromaggus"))
            return nullptr;

        std::vector<Player*> cursed;
        std::vector<Player*> members;
        bool tankLow = false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || !member->IsInMap(bot))
                continue;
            members.push_back(member);
            if (member->HasAura(SPELL_BROOD_AFFLICTION_BLACK))
                cursed.push_back(member);
            if (PlayerbotAI::IsTank(member) && member->GetHealthPct() < BLACK_HEALER_TANK_PCT)
                tankLow = true;
        }
        if (cursed.empty())
            return nullptr;

        auto removes = [&](Player* member)
        {
            return GET_PLAYERBOT_AI(member) && CanRemoveCurses(member) && !(tankLow && PlayerbotAI::IsHeal(member));
        };
        if (!removes(bot))
            return nullptr;

        size_t removers = 0;
        size_t mine = 0;
        for (Player* member : members)
            if (removes(member))
            {
                if (member == bot)
                    mine = removers;
                ++removers;
            }

        std::stable_sort(cursed.begin(), cursed.end(), [](Player* a, Player* b)
        {
            bool const aTank = PlayerbotAI::IsTank(a);
            bool const bTank = PlayerbotAI::IsTank(b);
            if (aTank != bTank)
                return aTank;
            return a->GetHealthPct() < b->GetHealthPct();
        });

        auto reachable = [&](Player* target)
        {
            if (bot->GetExactDist(target) > CURSE_REMOVAL_RANGE)
                return false;
            bool castable = false;
            for (char const* spell : CURSE_REMOVALS)
                castable = castable || botAI->CanCastSpell(spell, target);
            return castable && bot->IsWithinLOSInMap(target);
        };

        Player* assigned = cursed[mine % cursed.size()];
        if (reachable(assigned))
            return assigned;
        for (Player* target : cursed)
            if (target != assigned && reachable(target))
                return target;
        return nullptr;
    }

    struct BlackCache
    {
        ObjectGuid bot;
        ObjectGuid target;
        uint32 ms = 0;
    };
    thread_local BlackCache tBlackCache;

    Player* BlackToRemove(PlayerbotAI* botAI, Player* bot, bool fresh)
    {
        uint32 const now = getMSTime();
        if (!fresh && tBlackCache.bot == bot->GetGUID() && getMSTimeDiff(tBlackCache.ms, now) <= BLACK_CACHE_MS)
        {
            Player* target = tBlackCache.target ? ObjectAccessor::GetPlayer(*bot, tBlackCache.target) : nullptr;
            return target && target->IsAlive() && target->HasAura(SPELL_BROOD_AFFLICTION_BLACK) ? target : nullptr;
        }

        Player* target = FindBlackToRemove(botAI, bot);
        tBlackCache = { bot->GetGUID(), target ? target->GetGUID() : ObjectGuid::Empty, now };
        return target;
    }
}

bool RcBwlBlackAfflictionTrigger::IsActive()
{
    return bot->IsInCombat() && BlackToRemove(botAI, bot, true);
}

bool RcBwlBlackAfflictionAction::Execute(Event /*event*/)
{
    Player* target = BlackToRemove(botAI, bot, false);
    if (!target)
        return false;
    for (char const* spell : CURSE_REMOVALS)
        if (botAI->CanCastSpell(spell, target) && botAI->CastSpell(spell, target))
            return true;
    return false;
}

// --- Nefarian: class calls -------------------------------------------------

namespace
{
    // The priests' debuff. Matched by id and, as a fallback, by name, in case the call applies
    // its aura under a different id than the call spell itself.
    bool HasCorruptedHealing(Player* bot)
    {
        if (bot->HasAura(SPELL_CORRUPTED_HEALING))
            return true;
        for (auto const& [id, aurApp] : bot->GetAppliedAuras())
            if (!aurApp->IsPositive() &&
                std::strcmp(aurApp->GetBase()->GetSpellInfo()->SpellName[LOCALE_enUS], "Corrupted Healing") == 0)
                return true;
        return false;
    }

    Creature* FireNovaTotemNear(Player* bot)
    {
        std::list<Creature*> totems;
        bot->GetCreatureListWithEntryInGrid(totems, NPC_CORRUPTED_FIRE_NOVA_TOTEM, FIRE_NOVA_KEEP_AWAY);
        for (Creature* totem : totems)
            if (totem->IsAlive())
                return totem;
        return nullptr;
    }
}

float RcBwlCorruptedHealingMultiplier::GetValue(Action* action)
{
    // Every heal a priest casts under Corrupted Healing puts a shadow DoT on its target, so the
    // priests' heals land on the tanks as damage. Power Word: Shield isn't a heal and still goes
    // out, and a tank about to die still gets healed (the DoT is the lesser evil); otherwise the
    // other healers carry the raid until the call wears off.
    if (!action || !bot->IsClass(CLASS_PRIEST))
        return 1.0f;
    auto* heal = dynamic_cast<CastHealingSpellAction*>(action);
    if (!heal || heal->getSpell() == "power word: shield" || !LivingBossInCombat(botAI, "nefarian") ||
        !HasCorruptedHealing(bot))
        return 1.0f;

    Unit* target = action->GetTarget();
    Player* player = target ? target->ToPlayer() : nullptr;
    if (player && PlayerbotAI::IsTank(player) && player->GetHealthPct() < CORRUPTED_HEALING_TANK_PCT)
        return 1.0f;
    return 0.0f;
}

bool RcBwlFireNovaTotemTrigger::IsActive()
{
    // Tanks stay with their mobs; everyone else walks out before the totem goes off.
    return bot->IsInCombat() && !PlayerbotAI::IsTank(bot) && LivingBossInCombat(botAI, "nefarian") &&
           FireNovaTotemNear(bot);
}

bool RcBwlFireNovaTotemMoveAwayAction::Execute(Event /*event*/)
{
    Creature* totem = FireNovaTotemNear(bot);
    if (!totem)
        return false;
    return MoveAway(totem, FIRE_NOVA_KEEP_AWAY + 2.0f - bot->GetExactDist2d(totem));
}

class RaidClearBwlPlayerScript : public PlayerScript
{
public:
    RaidClearBwlPlayerScript() : PlayerScript("RaidClearBwlPlayerScript", { PLAYERHOOK_ON_PLAYER_RESURRECT }) {}

    // Playerbots puts the Onyxia Scale Cloak's aura on its bots in Blackwing Lair from a random
    // check every few seconds, so a bot revived in front of Nefarian or a drake could eat a
    // Shadow Flame without it. Death strips it; this puts it back the moment the bot stands up.
    void OnPlayerResurrect(Player* player, float /*restorePercent*/, bool& /*applySickness*/) override
    {
        using namespace RaidClear::BlackwingLair;
        if (player->GetMapId() != MAP_ID || !RaidClear::GetConfig().IsRaidEnabled(MAP_ID) || !GET_PLAYERBOT_AI(player))
            return;
        if (!player->HasAura(SPELL_ONYXIA_SCALE_CLOAK))
            player->AddAura(SPELL_ONYXIA_SCALE_CLOAK, player);
    }
};

void RaidClear::BlackwingLair::AddScripts()
{
    new RaidClearBwlPlayerScript();
}
