/*
 * mod-raid-clear: Molten Core (map 409). See McRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "McRaidClear.h"

#include "RaidClearConfig.h"
#include "Common/BossLookup.h"
#include "Common/SpellReflect.h"
#include "Common/SplashSpells.h"
#include "Common/TankRoles.h"

#include "AllSpellScript.h"
#include "AttackAction.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Pet.h"
#include "Spell.h"
#include "GenericSpellActions.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "ReachTargetActions.h"
#include "RtiTargetValue.h"
#include "LastMovementValue.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <list>
#include <vector>
#include <mutex>
#include <memory>
#include <map>
#include <cmath>
#include <string>

using namespace RaidClear::MoltenCore;

std::vector<RaidClear::KillOrderEntry> const& RaidClear::MoltenCore::KillOrder()
{
    static std::vector<KillOrderEntry> const table = {
        // Splits in two every 15s while it lives (up to 16), so it dies before anything else.
        { NPC_LAVA_SPAWN, 0 },

        // Healers: Sulfuron's priests cast Dark Mending, Majordomo's healers keep his elites up.
        { NPC_FLAMEWALKER_PRIEST, 1 },
        { NPC_FLAMEWAKER_HEALER, 1 },

        // The remaining boss adds, before their boss. Majordomo himself can't die (the fight
        // ends when his adds do) and Ragnaros can't be attacked while his Sons are up.
        { NPC_FLAMEWALKER_PROTECTOR, 2 },
        { NPC_FLAMEWALKER, 2 },
        { NPC_FLAMEWAKER_ELITE, 2 },
        { NPC_FIRESWORN, 2 },
        { NPC_SON_OF_FLAME, 2 },
    };
    return table;
}

std::vector<uint32> const& RaidClear::MoltenCore::TankSplitSkipBosses()
{
    static std::vector<uint32> const bosses = { NPC_GOLEMAGG };
    return bosses;
}

std::vector<uint32> const& RaidClear::MoltenCore::TankSplitStackAdds()
{
    // A pack has to come down inside one Play Dead (see the Core Hounds below): in one heap the
    // area spells hit them all.
    static std::vector<uint32> const stack = { NPC_CORE_HOUND };
    return stack;
}

std::vector<RaidClear::Tanks::SplashRadius> const& RaidClear::MoltenCore::TankSplitSplashRadii()
{
    // AoE radius plus ~5 yd for the knockback on their tank, which throws it (and so the mob)
    // back toward the other pair. Two giants end up 30 yd apart, two destroyers 36.
    static std::vector<Tanks::SplashRadius> const radii = {
        { NPC_MOLTEN_GIANT, 15.0f },
        { NPC_MOLTEN_DESTROYER, 18.0f },
    };
    return radii;
}

std::vector<RaidClear::Knockback::Thrower> const& RaidClear::MoltenCore::Throwers()
{
    static std::vector<Knockback::Thrower> const throwers = {
        { NPC_MOLTEN_GIANT, SPELL_GIANT_KNOCK_AWAY },
        { NPC_LAVA_SURGER, SPELL_SURGE },
        { NPC_SULFURON, SPELL_HAND_OF_RAGNAROS },
    };
    return throwers;
}

void RaidClearMoltenCoreStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Under a reflect: the spell of the other school that playerbots ranks below the wand (a
    // frost mage's Fireball), just above it. "rc spell reflect" zeroes whichever is reflected.
    triggers.push_back(new TriggerNode("rc mage target reflects", { NextAction("fireball", 5.15f) }));
    triggers.push_back(new TriggerNode("rc warlock target reflects", { NextAction("immolate", 5.15f) }));

    triggers.push_back(new TriggerNode("rc kill order", { NextAction("rc mark kill order", ACTION_RAID + 2) }));

    triggers.push_back(
        new TriggerNode("rc mc magmadar tremor", { NextAction("rc mc tremor totem", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc mc damage reflection", { NextAction("rc mc stop melee", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc mc ragnaros ranged", { NextAction("rc mc ragnaros move out", ACTION_RAID + 1) }));
    // Above the way out of his Wrath: the spot is out of it too, and further from the eruptions.
    triggers.push_back(
        new TriggerNode("rc mc ragnaros spot", { NextAction("rc mc ragnaros spot", ACTION_RAID + 2) }));

    // Above the heals: eight pulses of Inferno are more than anyone is healed through.
    triggers.push_back(new TriggerNode("rc mc inferno", { NextAction("rc mc leave inferno", ACTION_EMERGENCY + 7) }));
    triggers.push_back(new TriggerNode("rc mc hot coal", { NextAction("rc mc leave hot coal", ACTION_EMERGENCY + 4) }));
    // Under the way out of Inferno: a bot with the bomb that stands in it leaves that first.
    triggers.push_back(
        new TriggerNode("rc mc living bomb", { NextAction("rc mc carry living bomb", ACTION_EMERGENCY + 6) }));

    triggers.push_back(new TriggerNode("rc mc garr gather", { NextAction("rc mc garr gather", ACTION_RAID + 1) }));

    triggers.push_back(
        new TriggerNode("rc mc splash trash", { NextAction("rc mc leave splash trash", ACTION_RAID + 1) }));

    // Above playerbots' hound marking (ACTION_RAID), which the hound multiplier turns off.
    triggers.push_back(new TriggerNode("rc mc hound skull", { NextAction("rc mc mark hound", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("rc mc hound target", { NextAction("rc mc leave hound", ACTION_RAID + 2) }));
    triggers.push_back(
        new TriggerNode("rc mc anxious firesworn", { NextAction("rc mc leave firesworn", ACTION_RAID + 2) }));
}

void RaidClearMoltenCoreStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new RcMcTremorMultiplier(botAI));
    multipliers.push_back(new RcMcInfernoHoldMultiplier(botAI));
    multipliers.push_back(new RcMcHotCoalHoldMultiplier(botAI));
    multipliers.push_back(new RcMcRagnarosHoldMultiplier(botAI));
    multipliers.push_back(new RcMcReflectionMultiplier(botAI));
    multipliers.push_back(new RcMcAnxiousFireswornMultiplier(botAI));
    multipliers.push_back(new RcSpellReflectMultiplier(botAI));
    multipliers.push_back(new RcMcCoreHoundMultiplier(botAI));
}

// --- Magmadar -------------------------------------------------------------

namespace
{
    // Magmadar, alive and fighting, with the bot inside Panic's reach. Not through playerbots'
    // "find target", which only knows what the bot itself has threat on.
    bool NearMagmadar(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID)
            return false;
        Creature* boss = RaidClear::FightingCreature(bot, { NPC_MAGMADAR });
        return boss && bot->GetDistance(boss) <= PANIC_RANGE;
    }
}

bool RcMcMagmadarTremorTrigger::IsActive()
{
    if (bot->getClass() != CLASS_SHAMAN || !bot->IsInCombat() || !NearMagmadar(bot))
        return false;
    return !AI_VALUE2(bool, "has totem", "tremor totem");
}

bool RcMcTremorTotemAction::Execute(Event /*event*/)
{
    return botAI->CanCastSpell("tremor totem", bot) && botAI->CastSpell("tremor totem", bot);
}

float RcMcTremorMultiplier::GetValue(Action* action)
{
    if (!action || bot->getClass() != CLASS_SHAMAN || !bot->IsInCombat())
        return 1.0f;

    // Playerbots' names for whatever puts a totem in the earth slot or takes this one away.
    static constexpr std::array<char const*, 6> EARTH_SLOT = {
        "stoneskin totem", "strength of earth totem", "earthbind totem", "stoneclaw totem",
        "call of the elements", "totemic recall",
    };
    std::string const name = action->getName();
    bool earthSlot = false;
    for (char const* other : EARTH_SLOT)
        if (name == other)
            earthSlot = true;
    if (!earthSlot)
        return 1.0f;

    // A shaman without Tremor Totem keeps what it has.
    return NearMagmadar(bot) && AI_VALUE2(uint32, "spell id", "tremor totem") ? 0.0f : 1.0f;
}

// --- Baron Geddon ---------------------------------------------------------

namespace
{
    // Baron Geddon in the middle of an Inferno, or nullptr.
    Creature* GeddonInInferno(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID)
            return nullptr;
        Creature* geddon = RaidClear::FightingCreature(bot, { NPC_BARON_GEDDON });
        return geddon && geddon->HasAura(SPELL_INFERNO) ? geddon : nullptr;
    }
}

bool RcMcInfernoTrigger::IsActive()
{
    // A bot that is feared or rooted isn't going anywhere.
    if (!bot->IsAlive() || !botAI->CanMove())
        return false;
    Creature* geddon = GeddonInInferno(bot);
    return geddon && bot->GetExactDist(geddon) < INFERNO_KEEP_AWAY;
}

bool RcMcLeaveInfernoAction::Execute(Event /*event*/)
{
    Creature* geddon = GeddonInInferno(bot);
    if (!geddon || bot->GetExactDist(geddon) >= INFERNO_KEEP_AWAY)
        return false;

    // He stands still for all of it (the core makes him passive), so where he is now is where
    // the fire stays.
    Position const centre = geddon->GetPosition();
    auto const gapAt = [&centre](float x, float y, float z) { return centre.GetExactDist(x, y, z); };
    return _walk.Go(gapAt, INFERNO_KEEP_AWAY, INFERNO_SPOT, INFERNO_WORTH_IT, INFERNO_SEARCH, true);
}

float RcMcInfernoHoldMultiplier::GetValue(Action* action)
{
    // Picking a target isn't movement; only getting there is held: on foot, or with Charge,
    // Intercept or Feral Charge, which would undo the walk out in one cast.
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action);
    bool const reaches = walks || dynamic_cast<CastReachTargetSpellAction*>(action);
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!reaches && !cast)
        return 1.0f;

    Creature* geddon = GeddonInInferno(bot);
    if (!geddon)
        return 1.0f;

    // A bot still in it whose way out was stopped or that found none (it looks again within the
    // second) doesn't fill the wait with a spell it has to stand still for: that would be two or
    // three more pulses before it looks up. Instants are fine.
    if (cast && !reaches && bot->GetExactDist(geddon) < INFERNO_KEEP_AWAY && botAI->CanMove())
    {
        uint32 const spellId = AI_VALUE2(uint32, "spell id", cast->getSpell());
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        return info && (info->CalcCastTime(bot) > 0 || info->IsChanneled()) ? 0.0f : 1.0f;
    }
    if (!reaches)
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;
    return target->GetExactDist(geddon) < INFERNO_KEEP_AWAY ? 0.0f : 1.0f;
}

// A bot in the middle of a cast doesn't run its engine until the cast ends, and Inferno is two or
// three pulses in by then: wake it the moment he starts.
class RaidClearMcInfernoSpellScript : public AllSpellScript
{
public:
    RaidClearMcInfernoSpellScript() : AllSpellScript("RaidClearMcInfernoSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || spellInfo->Id != SPELL_INFERNO || caster->GetMapId() != MAP_ID ||
            caster->GetEntry() != NPC_BARON_GEDDON)
            return;
        if (!RaidClear::GetConfig().IsRaidEnabled(MAP_ID))
            return;

        Map::PlayerList const& players = caster->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive() || player->GetExactDist(caster) >= INFERNO_KEEP_AWAY)
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->HasStrategy("rc moltencore", BOT_STATE_COMBAT))
                continue;

            botAI->RequestSpellInterrupt();
            botAI->ResetActionDuration();
        }
    }
};

void RaidClear::MoltenCore::AddScripts()
{
    new RaidClearMcInfernoSpellScript();
}

// --- Majordomo Executus ---------------------------------------------------

namespace
{
    // Melee that isn't tanking: these take Damage Reflection's damage shield on every hit.
    bool IsReflectableMelee(Player* bot)
    {
        return PlayerbotAI::IsMelee(bot) && !PlayerbotAI::IsTank(bot);
    }
}

bool RcMcDamageReflectionTrigger::IsActive()
{
    if (!IsReflectableMelee(bot))
        return false;

    Unit* victim = bot->GetVictim();
    return victim && victim->HasAura(SPELL_DAMAGE_REFLECTION);
}

bool RcMcStopMeleeAction::Execute(Event /*event*/)
{
    // The multiplier keeps the attack actions from starting the swing again until the shield
    // (10s) is gone.
    return bot->AttackStop();
}

float RcMcReflectionMultiplier::GetValue(Action* action)
{
    if (!action || !bot->IsInCombat())
        return 1.0f;

    // Both shields are Majordomo's, cast on himself and every add within 100 yd.
    if (!AI_VALUE2(Unit*, "find target", "majordomo executus"))
        return 1.0f;

    bool const isSpell = dynamic_cast<CastSpellAction*>(action) != nullptr;
    bool const isSwing = dynamic_cast<AttackAction*>(action) != nullptr;
    if (!isSpell && !isSwing)
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || !bot->IsValidAttackTarget(target))
        return 1.0f;  // heals, buffs, self casts

    // Magic Reflection sends single-target spells back at the caster.
    if (isSpell && target->HasAura(SPELL_MAGIC_REFLECTION))
        return 0.0f;

    // Damage Reflection hurts whoever hits it in melee, abilities included.
    if (IsReflectableMelee(bot) && target->HasAura(SPELL_DAMAGE_REFLECTION))
        return 0.0f;

    return 1.0f;
}

namespace
{
    float HotCoalGap(float x, float y, float z)
    {
        return std::sqrt((x - HOT_COAL_X) * (x - HOT_COAL_X) + (y - HOT_COAL_Y) * (y - HOT_COAL_Y) +
                         (z - HOT_COAL_Z) * (z - HOT_COAL_Z));
    }

    float HotCoalGap(Unit const* unit)
    {
        return HotCoalGap(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ());
    }

    // How far from the Hot Coal this bot stays; 0 when it has business closer.
    float HotCoalKeepAway(PlayerbotAI* botAI, Player* bot)
    {
        if (PlayerbotAI::IsTank(bot))
        {
            // A tank with a mob on it takes the mob along when either of them is in reach of the
            // coal, far enough that the mob ends up outside too. The melee wait for that.
            float const gap = HotCoalGap(bot);
            for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
            {
                Unit* unit = botAI->GetUnit(guid);
                if (unit && unit->IsAlive() && unit->GetVictim() == bot &&
                    (gap < HOT_COAL_KEEP_AWAY || HotCoalGap(unit) < HOT_COAL_KEEP_AWAY))
                    return HOT_COAL_TANK_KEEP_AWAY;
            }
            return HOT_COAL_KEEP_AWAY;
        }

        // Melee go where their target is. Sent out, they would walk straight back in to reach
        // it: it is the tanks that take a mob out of the pit.
        if (!PlayerbotAI::IsRanged(bot) && !PlayerbotAI::IsHeal(bot))
        {
            constexpr float MELEE_ROOM = 3.0f;
            Unit* victim = bot->GetVictim();
            if (victim && victim->IsAlive() && HotCoalGap(victim) < HOT_COAL_KEEP_AWAY + MELEE_ROOM)
                return 0.0f;
        }
        return HOT_COAL_KEEP_AWAY;
    }

    // In combat and closer to the coal than this bot should be.
    bool AtHotCoal(PlayerbotAI* botAI, Player* bot)
    {
        if (bot->GetMapId() != MAP_ID || !bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
            return false;
        float const gap = HotCoalGap(bot);
        return gap < HOT_COAL_TANK_KEEP_AWAY && gap < HotCoalKeepAway(botAI, bot);
    }
}

bool RcMcHotCoalTrigger::IsActive()
{
    if (!AtHotCoal(botAI, bot))
    {
        _lastStands = false;
        _lastResult = false;
        return false;
    }

    // The pit is in the middle of the room and half the walks in this fight cross it (playerbots
    // walks to a target in legs, and stops between them). A bot on the move is left alone, unless
    // it is the walk out this trigger started: that one keeps the trigger up until it is done.
    if (bot->isMoving() && !bot->movespline->Finalized())
    {
        G3D::Vector3 const dest = bot->movespline->FinalDestination();
        if (_lastResult && HotCoalGap(dest.x, dest.y, dest.z) >= HOT_COAL_KEEP_AWAY)
            return true;
        _lastStands = false;
        _lastResult = false;
        return false;
    }

    // Standing on the same spot on two looks in a row: stopped, not between two legs of a walk
    // (a bot nobody leads only looks twice a second, and a short leg fits in between).
    constexpr uint32 LOOK_MS = 500;
    constexpr float SAME_SPOT = 1.0f;
    uint32 const now = getMSTime();
    if (_lastLookMs && getMSTimeDiff(_lastLookMs, now) < LOOK_MS)
        return _lastResult;
    _lastLookMs = now ? now : 1;
    _lastResult = _lastStands && bot->GetExactDist(&_lastSpot) < SAME_SPOT;
    _lastStands = true;
    _lastSpot = bot->GetPosition();
    return _lastResult;
}

float RcMcHotCoalHoldMultiplier::GetValue(Action* action)
{
    // The trigger wants two looks half a second apart, and a bot that starts a three second cast
    // after the first doesn't look again until it lands. So a bot that has just stopped at the
    // coal casts nothing it has to stand still for. Only for that long: one that then finds no
    // way out goes back to what it was doing.
    constexpr uint32 HOLD_MS = 1500;
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || bot->GetMapId() != MAP_ID || bot->isMoving() || !AtHotCoal(botAI, bot))
    {
        if (cast)
            _sinceMs = 0;
        return 1.0f;
    }

    uint32 const now = getMSTime();
    if (!_sinceMs)
        _sinceMs = now ? now : 1;
    if (getMSTimeDiff(_sinceMs, now) > HOLD_MS)
        return 1.0f;

    uint32 const spellId = AI_VALUE2(uint32, "spell id", cast->getSpell());
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    return info && (info->CalcCastTime(bot) > 0 || info->IsChanneled()) ? 0.0f : 1.0f;
}

bool RcMcLeaveHotCoalAction::Execute(Event /*event*/)
{
    float const keepAway = HotCoalKeepAway(botAI, bot);
    if (HotCoalGap(bot) >= keepAway)
        return false;
    bool const tank = PlayerbotAI::IsTank(bot);
    float const want = keepAway >= HOT_COAL_TANK_KEEP_AWAY ? HOT_COAL_TANK_SPOT : HOT_COAL_SPOT;

    // Out on the side the fight is on: of the spots far enough from the coal, the one nearest
    // the bot's target (the main tank for a bot without one) scores best. Majordomo throws his
    // victim right onto the coal, where every way out is the same length.
    Unit* toward = bot->GetVictim();
    if (!toward || !toward->IsAlive())
        toward = AI_VALUE(Unit*, "current target");
    if (!toward || !toward->IsAlive() || toward == bot)
        toward = RaidClear::Tanks::ActingMainTank(bot->GetGroup());
    Position const pull = toward && toward != bot ? toward->GetPosition() : bot->GetPosition();
    auto const gapAt = [want, &pull](float x, float y, float z)
    {
        float const gap = HotCoalGap(x, y, z);
        return gap < want ? gap : want + 1.0f / (1.0f + pull.GetExactDist2d(x, y));
    };

    // A tank on its way out still taunts and hits back: its abilities are instants, and
    // Majordomo wiped its threat when he threw it here.
    if (tank && RaidClear::OnItsWayOut(bot, gapAt, keepAway))
        return false;
    return _walk.Go(gapAt, keepAway, want, HOT_COAL_WORTH_IT, HOT_COAL_SEARCH);
}

// --- Garr -----------------------------------------------------------------

namespace
{
    // Garr, alive and fighting.
    Creature* FightingGarr(Player* bot)
    {
        return bot->GetMapId() == MAP_ID ? RaidClear::FightingCreature(bot, { NPC_GARR }) : nullptr;
    }

    // A living Firesworn that has Separation Anxiety, or is far enough from Garr to get it on his
    // next check: it hits for four times the damage, and so does its Eruption when it dies.
    bool IsAnxiousFiresworn(Player* bot, Unit const* unit)
    {
        if (!unit || unit->GetEntry() != NPC_FIRESWORN || !unit->IsAlive())
            return false;
        if (unit->HasAura(SPELL_SEPARATION_ANXIETY))
            return true;
        Creature* garr = FightingGarr(bot);
        return garr && unit->GetDistance(garr) > GARR_ANXIOUS_RANGE;
    }
}

bool RaidClear::MoltenCore::KillOrderSkips(Player* bot, Unit const* unit)
{
    return IsAnxiousFiresworn(bot, unit) || (unit && unit->HasAura(SPELL_PLAY_DEAD));
}

bool RcMcGarrGatherTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;

    // Whoever Garr is on stays where it is: everyone else comes to it.
    Creature* garr = FightingGarr(bot);
    if (!garr || garr->GetVictim() == bot || bot->GetDistance(garr) <= GARR_GATHER)
        return false;

    // Only while there is a Firesworn left to bring along. A tank only comes when one is on it:
    // a tank holding trash that was pulled with the boss keeps it away from the raid.
    bool const tank = PlayerbotAI::IsTank(bot);
    GuidVector const attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && unit->GetEntry() == NPC_FIRESWORN && unit->IsAlive() && (!tank || unit->GetVictim() == bot))
            return true;
    }
    return false;
}

bool RcMcGarrGatherAction::Execute(Event /*event*/)
{
    Creature* garr = FightingGarr(bot);
    if (!garr || bot->GetDistance(garr) <= GARR_GATHER_TARGET)
        return false;
    return MoveTo(garr, GARR_GATHER_TARGET, MovementPriority::MOVEMENT_COMBAT);
}

namespace
{
    // The unit a spell the bot is casting or channelling is aimed at, or nullptr.
    Unit* CastingAt(Player* bot, CurrentSpellTypes type)
    {
        Spell* spell = bot->GetCurrentSpell(type);
        return spell ? spell->m_targets.GetUnitTarget() : nullptr;
    }

    // The living enemies in the bot's fight, but `except`.
    std::vector<Unit*> Fighting(PlayerbotAI* botAI, Player* bot, Unit const* except)
    {
        std::vector<Unit*> fighting;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (unit && unit != except && unit->IsAlive() && unit->IsInMap(bot))
                fighting.push_back(unit);
        }
        return fighting;
    }

    bool AnyFiresworn(std::vector<Unit*> const& units)
    {
        for (Unit* unit : units)
            if (unit->GetEntry() == NPC_FIRESWORN)
                return true;
        return false;
    }

    // An anxious Firesworn low enough that a tank's hits would finish it before it is back with Garr.
    bool AnxiousAndLow(Player* bot, Unit const* unit)
    {
        return IsAnxiousFiresworn(bot, unit) && unit->GetHealthPct() < GARR_TANK_SPARES_BELOW_PCT;
    }

    // A spell the bot is casting or channelling that reaches more than its target (a Blizzard or a
    // Volley on the ground, a Chain Lightning at Garr) and would land on an anxious Firesworn.
    bool RunningSplashHitsAnxious(PlayerbotAI* botAI, Player* bot, CurrentSpellTypes type)
    {
        Spell* spell = bot->GetCurrentSpell(type);
        if (!spell || !spell->GetSpellInfo())
            return false;
        RaidClear::Splash::Reach const reach = RaidClear::Splash::ReachOfSpell(bot, spell->GetSpellInfo());
        if (!reach.Any())
            return false;
        Unit* target = spell->m_targets.GetUnitTarget();
        if (target == bot)
            target = nullptr;
        std::vector<Unit*> const fighting = Fighting(botAI, bot, target);
        if (!AnyFiresworn(fighting))
            return false;
        Player* const self = bot;
        auto const anxious = [self](Unit* unit) { return IsAnxiousFiresworn(self, unit); };
        if (RaidClear::Splash::LandsOn(bot, reach, target, fighting, anxious))
            return true;
        // Aimed at the ground: what it hits is around that spot.
        if (!target && reach.aroundTarget > 0.0f && spell->m_targets.HasDst())
        {
            WorldLocation const* dest = spell->m_targets.GetDstPos();
            for (Unit* unit : fighting)
                if (dest->GetExactDist(unit) <= reach.aroundTarget + unit->GetCombatReach() && anxious(unit))
                    return true;
        }
        return false;
    }
}

bool RcMcAnxiousFireswornTrigger::IsActive()
{
    // The multiplier keeps a bot from starting on one; this is for a bot that was already on it
    // when it ran out of Garr's range: a swing, an Auto Shot or a wand goes on by itself, and so
    // does a cast or a channel (Arcane Missiles, or a Blizzard it has walked into). A tank only
    // lets go of one that is low: it holds it with taunts until Separation Anxiety is gone.
    if (!bot->IsInCombat() || bot->GetMapId() != MAP_ID)
        return false;
    if (PlayerbotAI::IsTank(bot))
        return AnxiousAndLow(bot, bot->GetVictim());
    if (IsAnxiousFiresworn(bot, bot->GetVictim()) ||
        IsAnxiousFiresworn(bot, CastingAt(bot, CURRENT_GENERIC_SPELL)) ||
        IsAnxiousFiresworn(bot, CastingAt(bot, CURRENT_CHANNELED_SPELL)) ||
        RunningSplashHitsAnxious(botAI, bot, CURRENT_GENERIC_SPELL) ||
        RunningSplashHitsAnxious(botAI, bot, CURRENT_CHANNELED_SPELL))
        return true;
    Pet* pet = bot->GetPet();
    return pet && IsAnxiousFiresworn(bot, pet->GetVictim());
}

bool RcMcLeaveFireswornAction::Execute(Event /*event*/)
{
    bool const tank = PlayerbotAI::IsTank(bot);
    if (tank ? AnxiousAndLow(bot, bot->GetVictim()) : IsAnxiousFiresworn(bot, bot->GetVictim()))
    {
        bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
        bot->AttackStop();
    }
    if (tank)
        return true;
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        if (IsAnxiousFiresworn(bot, CastingAt(bot, type)) || RunningSplashHitsAnxious(botAI, bot, type))
            bot->InterruptSpell(type);
    if (Pet* pet = bot->GetPet())
        if (IsAnxiousFiresworn(bot, pet->GetVictim()))
            pet->AttackStop();
    return true;
}

float RcMcAnxiousFireswornMultiplier::GetValue(Action* action)
{
    if (!action || !bot->IsInCombat() || bot->GetMapId() != MAP_ID)
        return 1.0f;
    if (!dynamic_cast<CastSpellAction*>(action) && !dynamic_cast<AttackAction*>(action))
        return 1.0f;

    bool const tank = PlayerbotAI::IsTank(bot);
    Unit* target = action->GetTarget();
    if (target == bot || (target && !bot->IsValidAttackTarget(target)))
        target = nullptr;  // self casts (Consecration, Arcane Explosion), heals

    // Tanks keep hitting one: they are the ones bringing it back to Garr. Not one that is low:
    // that one they only taunt, and their swings wait until Separation Anxiety is gone.
    if (target && IsAnxiousFiresworn(bot, target))
    {
        if (!tank)
            return 0.0f;
        if (AnxiousAndLow(bot, target) && !RaidClear::Tanks::IsTauntAction(action))
            return 0.0f;
    }

    // Nor does a spell that hits more than its target land on one: a Multi-Shot or a Cleave at
    // the Firesworn next to it, a Chain Lightning that jumps from Garr to one behind him, a
    // Consecration under it. A tank's area spells only spare one that is low.
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast)
        return 1.0f;
    std::vector<Unit*> const fighting = Fighting(botAI, bot, target);
    if (!AnyFiresworn(fighting))
        return 1.0f;
    RaidClear::Splash::Reach const reach = RaidClear::Splash::ReachOf(botAI, bot, cast->getSpell());
    if (!reach.Any())
        return 1.0f;
    Player* const self = bot;
    auto const spare = [self, tank](Unit* unit)
    { return tank ? AnxiousAndLow(self, unit) : IsAnxiousFiresworn(self, unit); };
    return RaidClear::Splash::LandsOn(bot, reach, target, fighting, spare) ? 0.0f : 1.0f;
}

// --- Molten Giants and Destroyers ---------------------------------------------

namespace
{
    struct Blast
    {
        Position centre;
        float radius;
    };

    // Where the Giants and Destroyers in the fight stand, with how far their AoE reaches. Empty
    // for a bot that belongs in it (tanks, melee).
    std::vector<Blast> SplashTrashFor(PlayerbotAI* botAI, Player* bot)
    {
        std::vector<Blast> blasts;
        if (bot->GetMapId() != MAP_ID || !bot->IsInCombat() || PlayerbotAI::IsTank(bot) ||
            (PlayerbotAI::IsMelee(bot) && !PlayerbotAI::IsHeal(bot)))
            return blasts;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || !unit->IsInCombat() || !unit->IsInMap(bot))
                continue;
            if (unit->GetEntry() == NPC_MOLTEN_GIANT)
                blasts.push_back({ unit->GetPosition(), SMASH_RADIUS });
            else if (unit->GetEntry() == NPC_MOLTEN_DESTROYER)
                blasts.push_back({ unit->GetPosition(), MASSIVE_TREMOR_RADIUS });
        }
        return blasts;
    }

    // Yards from (x, y, z) to the edge of the nearest of them (negative inside one).
    float SplashTrashGap(std::vector<Blast> const& blasts, float x, float y, float z)
    {
        float gap = 1000.0f;
        for (Blast const& blast : blasts)
            gap = std::min(gap, blast.centre.GetExactDist(x, y, z) - blast.radius);
        return gap;
    }
}

bool RcMcSplashTrashTrigger::IsActive()
{
    if (!bot->IsAlive() || !botAI->CanMove())
        return false;
    std::vector<Blast> const blasts = SplashTrashFor(botAI, bot);
    return !blasts.empty() &&
           SplashTrashGap(blasts, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) <
               SPLASH_TRASH_KEEP_AWAY;
}

bool RcMcLeaveSplashTrashAction::Execute(Event /*event*/)
{
    std::vector<Blast> const blasts = SplashTrashFor(botAI, bot);
    if (blasts.empty())
        return false;
    auto const gapAt = [&blasts](float x, float y, float z) { return SplashTrashGap(blasts, x, y, z); };
    if (gapAt(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= SPLASH_TRASH_KEEP_AWAY)
        return false;
    return _walk.Go(gapAt, SPLASH_TRASH_KEEP_AWAY, SPLASH_TRASH_SPOT, SPLASH_TRASH_WORTH_IT, SPLASH_TRASH_SEARCH);
}

// --- Core Hounds -----------------------------------------------------------

namespace
{
    bool PlayingDead(Unit const* unit)
    {
        return unit && unit->GetEntry() == NPC_CORE_HOUND && unit->IsAlive() && unit->HasAura(SPELL_PLAY_DEAD);
    }

    // The Core Hounds in the fight, and whether one of them is playing dead.
    struct Hounds
    {
        std::vector<Unit*> standing;
        std::vector<Unit*> down;
    };

    Hounds HoundsInFight(PlayerbotAI* botAI, Player* bot)
    {
        Hounds hounds;
        if (bot->GetMapId() != MAP_ID)
            return hounds;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || unit->GetEntry() != NPC_CORE_HOUND || !unit->IsAlive() || !unit->IsInCombat() ||
                !unit->IsInMap(bot))
                continue;
            (unit->HasAura(SPELL_PLAY_DEAD) ? hounds.down : hounds.standing).push_back(unit);
        }
        return hounds;
    }

    // The standing hound that keeps one playing dead from dying and has the least health: the
    // one to put down next. Looked for the way the core decides (spell_mc_play_dead_aura): any
    // Core Hound within 80 yd of the one playing dead and in its line of sight, alive, in combat
    // and not playing dead itself, whether or not anyone in the group has it on its list.
    Unit* NextHound(Player* bot, Hounds const& hounds)
    {
        Unit* best = nullptr;
        for (Unit* down : hounds.down)
        {
            std::list<Creature*> near;
            down->GetCreaturesWithEntryInRange(near, CORE_HOUND_LINK_RANGE, NPC_CORE_HOUND);
            for (Creature* hound : near)
            {
                if (hound == down || !hound->IsAlive() || !hound->IsInCombat() || hound->HasAura(SPELL_PLAY_DEAD) ||
                    !down->IsWithinLOSInMap(hound) || !bot->IsValidAttackTarget(hound))
                    continue;
                if (!best || hound->GetHealth() < best->GetHealth())
                    best = hound;
            }
        }
        return best;
    }

    // The healthiest standing hound, by share of its health.
    Unit* HealthiestHound(Player* bot, Hounds const& hounds)
    {
        Unit* best = nullptr;
        for (Unit* hound : hounds.standing)
            if (bot->IsValidAttackTarget(hound) && (!best || hound->GetHealthPct() > best->GetHealthPct()))
                best = hound;
        return best;
    }

    // While none plays dead, a standing hound further than the band below the healthiest one is
    // left alone: hitting it would bring it down while the others still have a long way to go.
    // Never the skull, which is what everyone is sent to.
    bool HeldHound(Player* bot, Hounds const& hounds, Unit const* unit)
    {
        if (!unit || unit->GetEntry() != NPC_CORE_HOUND || !hounds.down.empty() || hounds.standing.size() < 2 ||
            unit->HasAura(SPELL_PLAY_DEAD))
            return false;
        Group* group = bot->GetGroup();
        if (group && group->GetTargetIcon(RtiTargetValue::skullIndex) == unit->GetGUID())
            return false;
        Unit* healthiest = HealthiestHound(bot, hounds);
        return healthiest && unit->GetHealthPct() < healthiest->GetHealthPct() - CORE_HOUND_HEALTH_BAND;
    }

    // Some hound is held back: the pack is out of step (one got up again at full health).
    bool PackUneven(Player* bot, Hounds const& hounds)
    {
        return std::any_of(hounds.standing.begin(), hounds.standing.end(),
                           [bot, &hounds](Unit* hound) { return HeldHound(bot, hounds, hound); });
    }

    // Where skull should be: with one playing dead, the hound that keeps it from dying with the
    // least health; otherwise the healthiest. Kept on the current skull while it is a standing
    // hound within half the band of the healthiest, so the damage dealers don't switch on every
    // swing. Nothing (leave skull alone) while it sits on something the kill order ranks.
    Unit* WantedHoundSkull(PlayerbotAI* botAI, Player* bot, Hounds const& hounds)
    {
        if (hounds.standing.empty())
            return nullptr;
        if (!hounds.down.empty())
            return NextHound(bot, hounds);

        Group* group = bot->GetGroup();
        Unit* skull = group ? botAI->GetUnit(group->GetTargetIcon(RtiTargetValue::skullIndex)) : nullptr;
        if (skull && skull->IsAlive() && skull->IsInCombat() && skull->GetEntry() != NPC_CORE_HOUND)
        {
            std::vector<RaidClear::KillOrderEntry> const& order = RaidClear::MoltenCore::KillOrder();
            if (std::any_of(order.begin(), order.end(),
                            [skull](RaidClear::KillOrderEntry const& e) { return e.entry == skull->GetEntry(); }))
                return nullptr;
        }

        Unit* healthiest = HealthiestHound(bot, hounds);
        if (healthiest && skull && skull != healthiest && skull->GetEntry() == NPC_CORE_HOUND && skull->IsAlive() &&
            skull->IsInCombat() && !skull->HasAura(SPELL_PLAY_DEAD) &&
            skull->GetHealthPct() >= healthiest->GetHealthPct() - CORE_HOUND_HEALTH_BAND / 2.0f)
            return skull;
        return healthiest;
    }

    // A tank keeps hitting a hound that is on it: that is its threat.
    bool HoldsHound(Player* bot, Unit const* unit)
    {
        return PlayerbotAI::IsTank(bot) && unit && unit->GetVictim() == bot;
    }

    // A hound nobody but the tank holding it should be hitting right now.
    bool LeaveHound(Player* bot, Hounds const& hounds, Unit const* unit)
    {
        return PlayingDead(unit) || (!HoldsHound(bot, unit) && HeldHound(bot, hounds, unit));
    }
}

bool RaidClear::MoltenCore::KillOrderSuspended(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    return botAI && !HoundsInFight(botAI, bot).down.empty();
}

bool RcMcHoundTargetTrigger::IsActive()
{
    if (!bot->IsInCombat() || bot->GetMapId() != MAP_ID)
        return false;
    // Casts and channels at a hound playing dead are wasted; one at a hound that is only held
    // back may finish.
    if (PlayingDead(CastingAt(bot, CURRENT_GENERIC_SPELL)) || PlayingDead(CastingAt(bot, CURRENT_CHANNELED_SPELL)))
        return true;
    Unit* victim = bot->GetVictim();
    Pet* pet = bot->GetPet();
    Unit* petVictim = pet ? pet->GetVictim() : nullptr;
    if ((!victim || victim->GetEntry() != NPC_CORE_HOUND) && (!petVictim || petVictim->GetEntry() != NPC_CORE_HOUND))
        return false;
    Hounds const hounds = HoundsInFight(botAI, bot);
    return LeaveHound(bot, hounds, victim) || (petVictim && LeaveHound(bot, hounds, petVictim));
}

bool RcMcLeaveHoundAction::Execute(Event /*event*/)
{
    Hounds const hounds = HoundsInFight(botAI, bot);
    if (LeaveHound(bot, hounds, bot->GetVictim()))
    {
        bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
        bot->AttackStop();
    }
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        if (PlayingDead(CastingAt(bot, type)))
            bot->InterruptSpell(type);
    if (Pet* pet = bot->GetPet())
        if (LeaveHound(bot, hounds, pet->GetVictim()))
            pet->AttackStop();
    return true;
}

bool RcMcHoundSkullTrigger::IsActive()
{
    if (!bot->IsInCombat() || RaidClear::KillOrderMarker(bot->GetGroup()) != bot)
        return false;
    Unit* want = WantedHoundSkull(botAI, bot, HoundsInFight(botAI, bot));
    return want && bot->GetGroup()->GetTargetIcon(RtiTargetValue::skullIndex) != want->GetGUID();
}

bool RcMcMarkHoundAction::Execute(Event /*event*/)
{
    Group* group = bot->GetGroup();
    Unit* want = group ? WantedHoundSkull(botAI, bot, HoundsInFight(botAI, bot)) : nullptr;
    if (!want)
        return false;
    group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), want->GetGUID());
    return true;
}

float RcMcCoreHoundMultiplier::GetValue(Action* action)
{
    if (!action || !bot->IsInCombat() || bot->GetMapId() != MAP_ID)
        return 1.0f;

    // Playerbots' own marking (the healthiest of the hounds on the main tank's threat list only)
    // is replaced by "rc mc mark hound", which sees every hound in the fight.
    if (action->getName() == "mc core hound mark")
        return 0.0f;

    if (!dynamic_cast<CastSpellAction*>(action) && !dynamic_cast<AttackAction*>(action))
        return 1.0f;
    Unit* target = action->GetTarget();
    bool const atHound = target && target->GetEntry() == NPC_CORE_HOUND;
    // A spell aimed at a hound playing dead is wasted.
    if (atHound && PlayingDead(target))
        return 0.0f;
    if (HoldsHound(bot, target))
        return 1.0f;

    // An area or chain spell at the heap (or round the caster) keeps an even pack even, which is
    // the point; on a pack out of step it would bring the low ones down first again.
    if (dynamic_cast<CastSpellAction*>(action) && RaidClear::Splash::IsSplashSpell(botAI, action->getName()))
    {
        if (PlayerbotAI::IsTank(bot) || (target && target != bot && !atHound))
            return 1.0f;
        Hounds const hounds = HoundsInFight(botAI, bot);
        return hounds.standing.empty() || !PackUneven(bot, hounds) ? 1.0f : 0.0f;
    }
    if (!atHound)
        return 1.0f;
    return HeldHound(bot, HoundsInFight(botAI, bot), target) ? 0.0f : 1.0f;
}

// --- Ragnaros -------------------------------------------------------------

namespace
{
    // Ragnaros, fighting and out of the lava: under it (his Sons are up) he casts nothing. Not
    // through playerbots' "find target", which only knows what the bot itself has threat on.
    Creature* EmergedRagnaros(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID)
            return nullptr;
        Creature* boss = RaidClear::FightingCreature(bot, { NPC_RAGNAROS });
        return boss && !boss->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE) ? boss : nullptr;
    }

    bool KeepsOutOfWrath(Player* bot)
    {
        return !PlayerbotAI::IsTank(bot) && (PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot));
    }

    // Where Lava Burst erupts (spell_target_position of 21886, 21900-21903 and 21905-21907).
    constexpr std::array<std::array<float, 2>, 8> LAVA_BURSTS = { {
        { 871.540f, -839.115f }, { 843.509f, -798.313f }, { 827.276f, -874.028f }, { 864.573f, -806.463f },
        { 819.757f, -807.176f }, { 892.633f, -790.410f }, { 906.695f, -828.611f }, { 862.862f, -866.955f },
    } };

    float GapToLavaBurst(float x, float y)
    {
        float gap = 1000.0f;
        for (std::array<float, 2> const& at : LAVA_BURSTS)
            gap = std::min(gap, std::hypot(x - at[0], y - at[1]));
        return gap;
    }

    struct RagnarosSpot
    {
        float x, y, z, gap;
    };

    bool DryAt(Map* map, uint32 phaseMask, float x, float y, float z)
    {
        LiquidData const liquid = map->GetLiquidData(phaseMask, x, y, z, 2.0f, {});
        return !(liquid.Status & MAP_LIQUID_STATUS_IN_CONTACT);
    }

    // Every spot on the rock round him a ranged bot could hold, furthest from the eruptions
    // first: on the rings, a spacing apart, on dry ground (a couple of yards round it too, so a
    // bot that stops a step short isn't in the lava), near his height and in his line of sight.
    std::vector<RagnarosSpot> SpotsRound(Creature* boss)
    {
        std::vector<RagnarosSpot> spots;
        Map* map = boss->GetMap();
        uint32 const phaseMask = boss->GetPhaseMask();
        for (float const ring : RAGNAROS_RINGS)
        {
            int const steps = int(2.0f * float(M_PI) * ring / RAGNAROS_SPOT_SPACING);
            for (int i = 0; i < steps; ++i)
            {
                float const angle = 2.0f * float(M_PI) * float(i) / float(steps);
                float const x = boss->GetPositionX() + std::cos(angle) * ring;
                float const y = boss->GetPositionY() + std::sin(angle) * ring;
                float const z = map->GetHeight(phaseMask, x, y, boss->GetPositionZ() + 10.0f);
                if (z <= INVALID_HEIGHT || z < boss->GetPositionZ() - 4.0f || z > boss->GetPositionZ() + 8.0f)
                    continue;
                bool dry = DryAt(map, phaseMask, x, y, z);
                for (float const side : { 0.0f, 0.5f, 1.0f, 1.5f })
                {
                    float const a = side * float(M_PI);
                    dry = dry && DryAt(map, phaseMask, x + std::cos(a) * RAGNAROS_SPOT_DRY,
                                       y + std::sin(a) * RAGNAROS_SPOT_DRY, z);
                }
                if (!dry || !boss->IsWithinLOS(x, y, z + 2.0f))
                    continue;
                spots.push_back({ x, y, z, GapToLavaBurst(x, y) });
            }
        }
        std::stable_sort(spots.begin(), spots.end(),
                         [](RagnarosSpot const& a, RagnarosSpot const& b) { return a.gap > b.gap; });
        return spots;
    }

    // Who holds which spot, per Ragnaros. Handed out once and kept, also for a bot that dies (it
    // may be rezzed), so a death or a tank thrown back by his Wrath doesn't move everyone else.
    struct RagnarosLayout
    {
        ObjectGuid boss;
        std::vector<RagnarosSpot> spots;
        std::map<ObjectGuid, std::size_t> holder;
    };

    // This bot's spot. The first time a bot of the group has none, every ranged bot and healer
    // still without one gets one: healers first and each in group order, every one the spot
    // furthest from the eruptions that is a spacing clear of those already held; a healer's
    // within reach of where the main tank stands then, if any is. Map updates run on several
    // threads, hence the lock.
    bool SpotFor(Player* bot, Creature* boss, RagnarosSpot& out)
    {
        static std::mutex lock;
        static std::map<uint32, RagnarosLayout> layouts;

        Group* group = bot->GetGroup();
        if (!group)
            return false;

        std::lock_guard<std::mutex> guard(lock);
        RagnarosLayout& layout = layouts[boss->GetInstanceId()];
        if (layout.boss != boss->GetGUID())
        {
            layout.boss = boss->GetGUID();
            layout.spots = SpotsRound(boss);
            layout.holder.clear();
        }

        auto const held = layout.holder.find(bot->GetGUID());
        if (held != layout.holder.end())
        {
            out = layout.spots[held->second];
            return true;
        }
        if (layout.spots.empty())
            return false;

        std::vector<Player*> healers;
        std::vector<Player*> ranged;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || !member->IsInMap(bot) || !GET_PLAYERBOT_AI(member) ||
                !KeepsOutOfWrath(member) || layout.holder.count(member->GetGUID()))
                continue;
            (PlayerbotAI::IsHeal(member) ? healers : ranged).push_back(member);
        }

        Player* mainTank = RaidClear::Tanks::ActingMainTank(group);
        auto const clear = [&layout](RagnarosSpot const& spot)
        {
            for (auto const& [guid, index] : layout.holder)
            {
                RagnarosSpot const& other = layout.spots[index];
                if (std::hypot(spot.x - other.x, spot.y - other.y) < RAGNAROS_SPOT_SPACING - 0.1f)
                    return false;
            }
            return true;
        };
        auto const pick = [&](bool healer) -> std::size_t
        {
            std::size_t fallback = layout.spots.size();
            for (std::size_t i = 0; i < layout.spots.size(); ++i)
            {
                RagnarosSpot const& spot = layout.spots[i];
                if (!clear(spot))
                    continue;
                if (!healer || !mainTank || mainTank->GetExactDist2d(spot.x, spot.y) <= RAGNAROS_HEAL_REACH)
                    return i;
                if (fallback == layout.spots.size())
                    fallback = i;
            }
            return fallback;
        };

        for (bool const healer : { true, false })
            for (Player* member : healer ? healers : ranged)
            {
                std::size_t const index = pick(healer);
                if (index == layout.spots.size())
                    break;
                layout.holder[member->GetGUID()] = index;
            }

        auto const mine = layout.holder.find(bot->GetGUID());
        if (mine == layout.holder.end())
            return false;
        out = layout.spots[mine->second];
        return true;
    }
}

bool RcMcRagnarosSpotTrigger::IsActive()
{
    if (!bot->IsAlive() || !KeepsOutOfWrath(bot) || !botAI->CanMove())
        return false;
    Creature* boss = EmergedRagnaros(bot);
    RagnarosSpot spot;
    return boss && SpotFor(bot, boss, spot) && bot->GetExactDist2d(spot.x, spot.y) > RAGNAROS_SPOT_LEAVE;
}

bool RcMcRagnarosSpotAction::Execute(Event /*event*/)
{
    Creature* boss = EmergedRagnaros(bot);
    RagnarosSpot spot;
    if (!boss || !SpotFor(bot, boss, spot))
        return false;
    return MoveTo(bot->GetMapId(), spot.x, spot.y, spot.z, false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

float RcMcRagnarosHoldMultiplier::GetValue(Action* action)
{
    // A healer whose patient stands on the far side of him would walk round the inner edge of the
    // rock to reach it, be sent out again, and so on. From this close everything on him and at
    // his feet is in spell range already; what isn't is somebody else's to heal.
    bool const walks = dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action);
    if (!walks && !dynamic_cast<CastReachTargetSpellAction*>(action))
        return 1.0f;
    if (!KeepsOutOfWrath(bot))
        return 1.0f;

    // On its spot, a bot doesn't walk off it for anything but this module's own way out, or out
    // of the lava.
    if (walks && action->getName().rfind("rc ", 0) != 0 && action->getName() != "mc move from lava" &&
        DryAt(bot->GetMap(), bot->GetPhaseMask(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        if (Creature* boss = EmergedRagnaros(bot))
        {
            RagnarosSpot spot;
            if (SpotFor(bot, boss, spot) && bot->GetExactDist2d(spot.x, spot.y) <= RAGNAROS_SPOT_HOLD)
                return 0.0f;
        }

    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    Creature* boss = EmergedRagnaros(bot);
    if (!boss || bot->GetExactDist(boss) > RAGNAROS_HOLD)
        return 1.0f;
    return target->GetExactDist(boss) < RAGNAROS_KEEP_AWAY ? 0.0f : 1.0f;
}

// --- Baron Geddon: Living Bomb --------------------------------------------

namespace
{
    // Yards from a spot to the nearest living raid member other than the bot (centre to centre,
    // as the explosion measures).
    float GapToRaid(Player* bot, float x, float y, float z)
    {
        float gap = 1000.0f;
        Group* group = bot->GetGroup();
        if (!group)
            return gap;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot && member->IsAlive() && member->IsInMap(bot))
                gap = std::min(gap, member->GetExactDist(x, y, z));
        }
        return gap;
    }
}

bool RcMcLivingBombTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->HasAura(SPELL_LIVING_BOMB) || !botAI->CanMove())
        return false;
    return GapToRaid(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) < LIVING_BOMB_KEEP_AWAY;
}

bool RcMcCarryLivingBombAction::Execute(Event /*event*/)
{
    if (!bot->HasAura(SPELL_LIVING_BOMB))
        return false;

    if (GapToRaid(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= LIVING_BOMB_KEEP_AWAY)
        return false;

    // Not to his feet: with the raid backed off for an Inferno that is the emptiest spot in the
    // room, and the way out of Inferno would send the bot straight back.
    Player* carrier = bot;
    Creature* geddon = bot->GetMapId() == MAP_ID ? RaidClear::FightingCreature(bot, { NPC_BARON_GEDDON }) : nullptr;
    bool const hasGeddon = geddon != nullptr;
    Position const geddonAt = geddon ? geddon->GetPosition() : Position();
    auto const gapAt = [carrier, hasGeddon, &geddonAt](float x, float y, float z)
    {
        if (hasGeddon && geddonAt.GetExactDist(x, y, z) < INFERNO_KEEP_AWAY)
            return 0.0f;
        return GapToRaid(carrier, x, y, z);
    };
    return _walk.Go(gapAt, LIVING_BOMB_KEEP_AWAY, LIVING_BOMB_SPOT, LIVING_BOMB_WORTH_IT, LIVING_BOMB_SEARCH);
}
