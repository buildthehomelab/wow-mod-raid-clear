/*
 * mod-raid-clear: Molten Core (map 409). See McRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "McRaidClear.h"

#include "RaidClearConfig.h"
#include "Common/BossLookup.h"
#include "Common/SpellReflect.h"

#include "AllSpellScript.h"
#include "AttackAction.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Pet.h"
#include "GenericSpellActions.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "ReachTargetActions.h"
#include "LastMovementValue.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <array>
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

    // Above the heals: eight pulses of Inferno are more than anyone is healed through.
    triggers.push_back(new TriggerNode("rc mc inferno", { NextAction("rc mc leave inferno", ACTION_EMERGENCY + 7) }));
    triggers.push_back(new TriggerNode("rc mc hot coal", { NextAction("rc mc leave hot coal", ACTION_EMERGENCY + 4) }));
    // Under the way out of Inferno: a bot with the bomb that stands in it leaves that first.
    triggers.push_back(
        new TriggerNode("rc mc living bomb", { NextAction("rc mc carry living bomb", ACTION_EMERGENCY + 6) }));

    triggers.push_back(new TriggerNode("rc mc garr gather", { NextAction("rc mc garr gather", ACTION_RAID + 1) }));
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
    return IsAnxiousFiresworn(bot, unit);
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

bool RcMcAnxiousFireswornTrigger::IsActive()
{
    // The multiplier keeps a bot from starting on one; this is for a bot that was already on it
    // when it ran out of Garr's range. Auto-attack, Auto Shot and wands need no action to go on.
    if (!bot->IsInCombat() || PlayerbotAI::IsTank(bot))
        return false;
    return IsAnxiousFiresworn(bot, bot->GetVictim());
}

bool RcMcLeaveFireswornAction::Execute(Event /*event*/)
{
    bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    bot->AttackStop();
    if (Pet* pet = bot->GetPet())
        if (IsAnxiousFiresworn(bot, pet->GetVictim()))
            pet->AttackStop();
    return true;
}

float RcMcAnxiousFireswornMultiplier::GetValue(Action* action)
{
    // Tanks keep hitting it: they are the ones bringing it back to Garr.
    if (!action || !bot->IsInCombat() || PlayerbotAI::IsTank(bot))
        return 1.0f;
    if (!dynamic_cast<CastSpellAction*>(action) && !dynamic_cast<AttackAction*>(action))
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!target || target->GetEntry() != NPC_FIRESWORN || !bot->IsValidAttackTarget(target))
        return 1.0f;
    return IsAnxiousFiresworn(bot, target) ? 0.0f : 1.0f;
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
}

bool RcMcRagnarosRangedTrigger::IsActive()
{
    if (!bot->IsAlive() || !KeepsOutOfWrath(bot) || !botAI->CanMove())
        return false;

    // From his middle: the spell measures from there, and he is 20 yd wide.
    Creature* boss = EmergedRagnaros(bot);
    return boss && bot->GetExactDist(boss) < RAGNAROS_KEEP_AWAY;
}

bool RcMcRagnarosMoveOutAction::Execute(Event /*event*/)
{
    Creature* boss = EmergedRagnaros(bot);
    if (!boss || bot->GetExactDist(boss) >= RAGNAROS_KEEP_AWAY)
        return false;

    // He sits in the lava in the middle of the room and the raid stands on the rock round it:
    // a spot further from him only counts when it is dry.
    Position const centre = boss->GetPosition();
    Map* map = bot->GetMap();
    uint32 const phaseMask = bot->GetPhaseMask();
    float const height = bot->GetCollisionHeight();
    auto const gapAt = [&centre, map, phaseMask, height](float x, float y, float z)
    {
        LiquidData const liquid = map->GetLiquidData(phaseMask, x, y, z, height, {});
        return liquid.Status & MAP_LIQUID_STATUS_IN_CONTACT ? 0.0f : centre.GetExactDist(x, y, z);
    };
    // A bot knocked into the lava scores 0 where it stands: every ring is worth a look then.
    bool const dry = gapAt(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) > 0.0f;
    return _walk.Go(gapAt, RAGNAROS_KEEP_AWAY, RAGNAROS_SPOT, RAGNAROS_WORTH_IT, RAGNAROS_SEARCH, dry);
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
