/*
 * mod-raid-clear: nobody is thrown into the next pack. See Knockback.h.
 *
 * Released under the MIT License.
 */

#include "Knockback.h"

#include "RaidClearConfig.h"
#include "Common/AggroLeash.h"
#include "Raids/BlackwingLair/BwlRaidClear.h"
#include "Raids/MoltenCore/McRaidClear.h"

#include "AllSpellScript.h"
#include "Creature.h"
#include "LastMovementValue.h"
#include "Log.h"
#include "Map.h"
#include "MovementTypedefs.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

using namespace RaidClear;

namespace
{
    // Whom a throw catches.
    enum class Catches : uint8
    {
        Victim,  // whoever the mob is on
        Around,  // everyone within the radius
        Front,   // those of them in front of the mob
        Behind   // those of them behind it
    };

    struct Throw
    {
        uint32 spell;
        Catches who;
        float radius;    // Around, Front, Behind: yards from the mob's middle
        float distance;  // yards thrown over level ground
    };

    // A throw shorter than this doesn't take anyone anywhere.
    constexpr float MIN_THROW = 3.0f;
    // A bot this far outside a throw's radius still counts as in it: the mob shifts about.
    constexpr float REACH_SLACK = 2.0f;
    // How far from its middle a mob's single-target throw can start: its victim stands at its
    // edge, a few yards out.
    constexpr float VICTIM_REACH = 6.0f;
    // A flight is looked at this often along the way.
    constexpr float FLIGHT_STEP = 3.0f;
    // A landing closer than MIN_GAP to an idle mob's aggro radius is one to move for; the new
    // spot should leave WANT_GAP, or failing that be at least WORTH_IT better than this one.
    constexpr float MIN_GAP = 1.0f;
    constexpr float WANT_GAP = 5.0f;
    constexpr float WORTH_IT = 4.0f;
    // Throwers further from the bot than this aren't its concern.
    constexpr float THROWER_RANGE = 60.0f;
    constexpr uint32 CHECK_MS = 500;
    // After finding nowhere better to stand, wait this long before looking again.
    constexpr uint32 RETRY_MS = 2000;
    // Round a mob, the spots looked at: every 22.5 degrees.
    constexpr int ROUND_STEPS = 16;

    // Per creature entry. Written from any map's update (a spell cast) and read from any other
    // (a bot's AI).
    std::shared_mutex sThrowsMutex;
    std::unordered_map<uint32, std::vector<Throw>> sThrows;
    std::once_flag sListed;

    bool HasThrow(SpellInfo const* info)
    {
        for (SpellEffectInfo const& effect : info->Effects)
            if (effect.Effect == SPELL_EFFECT_KNOCK_BACK || effect.Effect == SPELL_EFFECT_KNOCK_BACK_DEST)
                return true;
        return false;
    }

    // The spell's throw (its longest, if it has more than one), worked out the way the core does
    // it for a bot: see Spell::EffectKnockBack and MotionMaster::MoveKnockbackFrom.
    bool ReadThrow(SpellInfo const* info, Unit* caster, Throw& out)
    {
        bool found = false;
        for (SpellEffectInfo const& effect : info->Effects)
        {
            if (effect.Effect != SPELL_EFFECT_KNOCK_BACK && effect.Effect != SPELL_EFFECT_KNOCK_BACK_DEST)
                continue;
            float const along = float(effect.MiscValue) * 0.1f;
            float const up = float(effect.CalcValue(caster)) * 0.1f;
            if (along <= 0.1f || up <= 0.1f)
                continue;
            float const distance = 2.0f * (up / float(Movement::gravity)) * along;
            if (distance < MIN_THROW || (found && distance <= out.distance))
                continue;

            Catches who = Catches::Victim;
            float radius = 0.0f;
            if (effect.TargetA.GetSelectionCategory() == TARGET_SELECT_CATEGORY_CONE ||
                effect.TargetB.GetSelectionCategory() == TARGET_SELECT_CATEGORY_CONE)
                who = info->HasAttribute(SPELL_ATTR0_CU_CONE_BACK) ? Catches::Behind : Catches::Front;
            else if (effect.TargetA.IsArea() || effect.TargetB.IsArea())
                who = Catches::Around;
            if (who != Catches::Victim)
            {
                radius = effect.CalcRadius(caster);
                if (radius <= 0.0f)
                    radius = info->GetMaxRange(false, caster);
            }

            out = { info->Id, who, radius, distance };
            found = true;
        }
        return found;
    }

    void Learn(uint32 entry, Throw const& thrown, bool seen)
    {
        {
            std::shared_lock<std::shared_mutex> lock(sThrowsMutex);
            auto const it = sThrows.find(entry);
            if (it != sThrows.end())
                for (Throw const& known : it->second)
                    if (known.spell == thrown.spell)
                        return;
        }

        std::unique_lock<std::shared_mutex> lock(sThrowsMutex);
        std::vector<Throw>& throws = sThrows[entry];
        for (Throw const& known : throws)
            if (known.spell == thrown.spell)
                return;
        throws.push_back(thrown);
        if (seen)
            LOG_INFO("module", "mod-raid-clear: creature {} throws players {:.0f} yd with spell {}.", entry,
                     thrown.distance, thrown.spell);
    }

    // The throwers each raid lists. On first use: the spell data has to be loaded.
    void ListKnown()
    {
        std::call_once(sListed, []
        {
            for (std::vector<Knockback::Thrower> const* list : { &MoltenCore::Throwers(), &BlackwingLair::Throwers() })
                for (Knockback::Thrower const& thrower : *list)
                {
                    SpellInfo const* info = sSpellMgr->GetSpellInfo(thrower.spell);
                    Throw thrown;
                    if (info && ReadThrow(info, nullptr, thrown))
                        Learn(thrower.entry, thrown, false);
                }
        });
    }

    std::vector<Throw> ThrowsOf(uint32 entry)
    {
        ListKnown();
        std::shared_lock<std::shared_mutex> lock(sThrowsMutex);
        auto const it = sThrows.find(entry);
        return it != sThrows.end() ? it->second : std::vector<Throw>();
    }

    struct Threat
    {
        Creature* mob;
        Throw thrown;
    };

    // Whether anything in the fight is known to throw at all. Asked first: it costs a few
    // lookups, and nearly every fight ends here.
    bool ThrowerInFight(PlayerbotAI* botAI)
    {
        ListKnown();
        std::shared_lock<std::shared_mutex> lock(sThrowsMutex);
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
            if (guid.IsCreatureOrVehicle() && sThrows.count(guid.GetEntry()))
                return true;
        return false;
    }

    // The throwers in the fight around this bot, each with the throws that could end near an
    // idle mob at all. With no idle mob anywhere near one, this is empty and costs next to nothing.
    std::vector<Threat> ThreatsNear(PlayerbotAI* botAI, Player* bot, Leash::WalkCheck const& leash)
    {
        std::vector<Threat> threats;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            if (!guid.IsCreatureOrVehicle())
                continue;
            Unit* unit = botAI->GetUnit(guid);
            Creature* mob = unit ? unit->ToCreature() : nullptr;
            if (!mob || !mob->IsAlive() || !mob->IsInCombat() || bot->GetExactDist2d(mob) > THROWER_RANGE)
                continue;
            std::vector<Throw> const throws = ThrowsOf(mob->GetEntry());
            if (throws.empty())
                continue;

            // Yards of clear ground around the mob, whether or not the idle mobs can see it: they
            // only have to see where someone lands.
            float const room = leash.Gap(mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), false);
            for (Throw const& thrown : throws)
            {
                float const from =
                    thrown.who == Catches::Victim ? mob->GetCombatReach() + VICTIM_REACH : thrown.radius + REACH_SLACK;
                if (room < from + thrown.distance + WANT_GAP)
                    threats.push_back({ mob, thrown });
            }
        }
        return threats;
    }

    // Whether the throw would catch the bot if it stood at this spot.
    bool WouldCatch(Threat const& threat, Player* bot, float x, float y, float z)
    {
        Creature* mob = threat.mob;
        bool const victim = mob->GetVictim() == bot;
        if (threat.thrown.who == Catches::Victim)
            return victim;
        if (mob->GetExactDist2d(x, y) > threat.thrown.radius + REACH_SLACK)
            return false;
        if (threat.thrown.who == Catches::Around)
            return true;
        // The mob turns to whoever it is on, wherever that one goes; for anyone else its front is
        // where it faces now (the half circle: it shifts about).
        if (victim)
            return threat.thrown.who == Catches::Front;
        Position const spot(x, y, z);
        return mob->HasInArc(float(M_PI), &spot) == (threat.thrown.who == Catches::Front);
    }

    // Yards between the throw's flight from this spot (the landing included) and the nearest idle
    // mob's aggro radius. The flight ends where the core ends it for a bot: along the floor, at
    // the first wall or drop.
    float LandingGap(Player* bot, Leash::WalkCheck const& leash, Threat const& threat, float x, float y, float z)
    {
        Creature* mob = threat.mob;
        float dirX = x - mob->GetPositionX();
        float dirY = y - mob->GetPositionY();
        float const out = std::hypot(dirX, dirY);
        if (out < 0.5f)
        {
            dirX = std::cos(mob->GetOrientation());
            dirY = std::sin(mob->GetOrientation());
        }
        else
        {
            dirX /= out;
            dirY /= out;
        }

        float toX = x + dirX * threat.thrown.distance;
        float toY = y + dirY * threat.thrown.distance;
        float toZ = z;
        if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, x, y, z, toX, toY, toZ, false))
            return leash.Gap(x, y, z);  // thrown nowhere

        float const flown = std::hypot(toX - x, toY - y);
        int const steps = std::max(1, int(std::ceil(flown / FLIGHT_STEP)));
        float gap = Leash::WalkCheck::NO_MOBS;
        for (int step = 1; step <= steps; ++step)
        {
            float const part = float(step) / float(steps);
            gap = std::min(gap, leash.Gap(x + (toX - x) * part, y + (toY - y) * part, z + (toZ - z) * part));
        }
        return gap;
    }

    // The worst landing among the throws that would catch the bot at this spot; NO_MOBS when
    // none would.
    float FootingAt(Player* bot, Leash::WalkCheck const& leash, std::vector<Threat> const& threats, float x, float y,
                    float z)
    {
        float gap = Leash::WalkCheck::NO_MOBS;
        for (Threat const& threat : threats)
            if (WouldCatch(threat, bot, x, y, z))
                gap = std::min(gap, LandingGap(bot, leash, threat, x, y, z));
        return gap;
    }

    bool Enabled()
    {
        Config const& config = GetConfig();
        return config.enable && config.aggroLeash && config.aggroLeashKnockback;
    }
}

bool RcKnockbackFootingTrigger::IsActive()
{
    if (!Enabled() || !bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove() ||
        PlayerbotAI::IsTank(bot) != _tanks)
    {
        _lastResult = false;
        return false;
    }

    uint32 const now = getMSTime();
    if (_lastCheckMs && getMSTimeDiff(_lastCheckMs, now) < CHECK_MS)
        return _lastResult;
    _lastCheckMs = now ? now : 1;

    // A bot on its way somewhere (or in the air) is judged where it stops.
    _lastResult = false;
    if (bot->isMoving() || !ThrowerInFight(botAI))
        return false;

    Leash::WalkCheck const leash(botAI, bot);
    std::vector<Threat> const threats = ThreatsNear(botAI, bot, leash);
    _lastResult = !threats.empty() && FootingAt(bot, leash, threats, bot->GetPositionX(), bot->GetPositionY(),
                                                bot->GetPositionZ()) < MIN_GAP;
    return _lastResult;
}

bool RcKnockbackFootingAction::isUseful()
{
    return (!_noSpotAtMs || getMSTimeDiff(_noSpotAtMs, getMSTime()) >= RETRY_MS) && MovementAction::isUseful();
}

bool RcKnockbackFootingAction::Execute(Event /*event*/)
{
    uint32 const now = getMSTime();
    // Sent off a moment ago and not walking: the aggro leash stopped that walk, and holds the bot
    // as long.
    if (!Enabled() || (_sentAtMs && getMSTimeDiff(_sentAtMs, now) < WAY_OUT_RESEND_MS) || !ThrowerInFight(botAI))
        return false;

    Leash::WalkCheck const leash(botAI, bot);
    std::vector<Threat> const threats = ThreatsNear(botAI, bot, leash);
    auto const gapAt = [&](float x, float y, float z) { return FootingAt(bot, leash, threats, x, y, z); };

    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    float const botZ = bot->GetPositionZ();
    float const here = gapAt(botX, botY, botZ);
    if (threats.empty() || here >= MIN_GAP)
        return false;

    // In melee with one of them (a tank it is on, or a bot hitting it from within reach): round
    // it, at the distance the bot keeps now. The mob stays where it is and only turns.
    Creature* held = nullptr;
    Unit* target = AI_VALUE(Unit*, "current target");
    bool const tank = PlayerbotAI::IsTank(bot);
    for (Threat const& threat : threats)
        if ((tank && threat.mob->GetVictim() == bot) ||
            (threat.mob == target && bot->IsWithinMeleeRange(threat.mob) && !PlayerbotAI::IsHeal(bot)))
        {
            held = threat.mob;
            break;
        }

    if (!held)
    {
        // Anyone else: the nearest spot out of reach of the throw, or with a clear landing.
        if (_walk.Go(gapAt, MIN_GAP, WANT_GAP, WORTH_IT))
            return true;
        _noSpotAtMs = now ? now : 1;
        return false;
    }

    Map* map = bot->GetMap();
    float const dist = std::max(bot->GetExactDist2d(held), 1.0f);
    float const base = held->GetAngle(bot);
    bool found = false;
    // What a spot has to beat: good enough, or that much better than here.
    float bestGap = std::min(here + WORTH_IT, WANT_GAP) - 0.01f;
    Position spot;
    for (int i = 1; i < ROUND_STEPS && (!found || bestGap < WANT_GAP); ++i)
    {
        // The nearest side first: a step one way, a step the other, two steps one way...
        int const turn = ((i + 1) / 2) * (i % 2 ? 1 : -1);
        float const angle = base + float(turn) * 2.0f * float(M_PI) / float(ROUND_STEPS);
        float const x = held->GetPositionX() + std::cos(angle) * dist;
        float const y = held->GetPositionY() + std::sin(angle) * dist;
        float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + WAY_OUT_UP);
        if (z <= INVALID_HEIGHT || z < botZ - WAY_OUT_DOWN || gapAt(x, y, z) <= bestGap)
            continue;

        // A whole path over the floor that ends at the spot, round the mob and not round the room.
        PathGenerator path(bot);
        constexpr uint32 NOT_A_WALK = PATHFIND_INCOMPLETE | PATHFIND_NOPATH | PATHFIND_SHORTCUT |
                                      PATHFIND_NOT_USING_PATH | PATHFIND_FARFROMPOLY;
        if (!path.CalculatePath(x, y, z) || !(path.GetPathType() & PATHFIND_NORMAL) ||
            (path.GetPathType() & NOT_A_WALK))
            continue;
        G3D::Vector3 const& end = path.GetActualEndPosition();
        if (std::hypot(end.x - x, end.y - y) > WAY_OUT_END_SLACK || path.getPathLength() > dist * 3.0f + 4.0f)
            continue;
        float const gap = gapAt(end.x, end.y, end.z);
        if (gap <= bestGap || leash.Pulls(path.GetPath()))
            continue;

        found = true;
        bestGap = gap;
        spot.Relocate(end.x, end.y, end.z);
    }

    // Whatever the bot was told last would make this walk wait its turn. Straight to the spot the
    // path was checked for: playerbots' own planning could settle for another.
    if (found)
        AI_VALUE(LastMovement&, "last movement").clear();
    if (!found || !MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false,
                          false, false, true, MovementPriority::MOVEMENT_FORCED))
    {
        _noSpotAtMs = now ? now : 1;
        return false;
    }
    _sentAtMs = now ? now : 1;
    _noSpotAtMs = 0;
    return true;
}

float RcKnockbackFootingMultiplier::GetValue(Action* action)
{
    if (!action || !dynamic_cast<TankFaceAction*>(action) || !Enabled() || !bot->IsInCombat())
        return 1.0f;

    uint32 const now = getMSTime();
    if (!_lastCheckMs || getMSTimeDiff(_lastCheckMs, now) >= CHECK_MS)
    {
        _lastCheckMs = now ? now : 1;
        _lastResult = false;
        if (ThrowerInFight(botAI))
        {
            Leash::WalkCheck const leash(botAI, bot);
            for (Threat const& threat : ThreatsNear(botAI, bot, leash))
                if (threat.mob->GetVictim() == bot)
                    _lastResult = true;
        }
    }
    return _lastResult ? 0.0f : 1.0f;
}

// A mob nobody listed throws a player: remember what it threw with.
class RaidClearThrowSpellScript : public AllSpellScript
{
public:
    RaidClearThrowSpellScript() : AllSpellScript("RaidClearThrowSpellScript", { ALLSPELLHOOK_ON_CAST }) {}

    void OnSpellCast(Spell* spell, Unit* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
    {
        // Every spell cast on the server comes through here: the cheapest test first.
        if (!spell || !caster || !spellInfo || !HasThrow(spellInfo))
            return;
        Creature* mob = caster->ToCreature();
        if (!mob || !Enabled() || !mob->GetMap()->IsRaid() || mob->IsPet() || mob->IsTotem() ||
            mob->GetCharmerOrOwnerGUID().IsPlayer())
            return;

        Throw thrown;
        if (!ReadThrow(spellInfo, mob, thrown))
            return;
        // Aimed at somebody other than whoever it is on (a random player, the furthest one):
        // nobody can stand out of the way of that.
        if (thrown.who == Catches::Victim && (!mob->GetVictim() || spell->m_targets.GetUnitTarget() != mob->GetVictim()))
            return;
        Learn(mob->GetEntry(), thrown, true);
    }
};

void RaidClear::Knockback::AddScripts()
{
    new RaidClearThrowSpellScript();
}
