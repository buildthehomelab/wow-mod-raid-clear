/*
 * mod-raid-clear: Blackwing Lair, where a bot stands: the dragons' breath, Nefarian's roar and
 * tail and who tanks him, and the Wyrmguards' War Stomp. See BwlRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "BwlRaidClear.h"

#include "RaidClearConfig.h"
#include "Common/BossLookup.h"

#include "AllSpellScript.h"
#include "AttackAction.h"
#include "Creature.h"
#include "GenericSpellActions.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace RaidClear::BlackwingLair;

namespace
{
    Creature* FightingBoss(Player* bot, uint32 entry)
    {
        return bot->GetMapId() == MAP_ID ? RaidClear::FightingCreature(bot, { entry }) : nullptr;
    }

    // The bots that have no business near a boss: ranged damage dealers and healers.
    bool KeepsBack(Player* bot)
    {
        return !PlayerbotAI::IsTank(bot) && (PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot));
    }

    // Walking somewhere, or getting there with Charge, Intercept or Feral Charge. Picking a
    // target isn't movement.
    bool Walks(Action* action)
    {
        return (dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action)) ||
               dynamic_cast<CastReachTargetSpellAction*>(action);
    }

    // --- Who holds Nefarian -----------------------------------------------

    // His class calls decide it: the paladins' does nothing to a tank, the warriors' (Berserker
    // Stance) costs one its shield and a tenth more damage, the druids' (cat form) kills one.
    int NefarianTankRank(Player const* tank)
    {
        switch (tank->getClass())
        {
            case CLASS_PALADIN:      return 0;
            case CLASS_DEATH_KNIGHT: return 1;
            case CLASS_WARRIOR:      return 2;
            default:                 return 3;
        }
    }

    // The tank that should have him: the first paladin in group order, and so on down the list.
    // nullptr with a real player among the tanks: who tanks is theirs to settle.
    Player* NefarianTank(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        Player* best = nullptr;
        int bestRank = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || !member->IsInMap(bot) || !PlayerbotAI::IsTank(member))
                continue;
            if (!GET_PLAYERBOT_AI(member))
                return nullptr;
            int const rank = NefarianTankRank(member);
            if (!best || rank < bestRank)
            {
                best = member;
                bestRank = rank;
            }
        }
        return best;
    }

    // --- Nefarian's Bone Constructs ------------------------------------------

    // Whether enough of them are up: one answer for the whole raid. Each bot only counts what it
    // can see, and a pile that forms and breaks up as one bot's count crosses the line is worse
    // than none, so a bot that counts enough keeps them "up" for everyone for a few seconds.
    constexpr uint32 CONSTRUCTS_SEEN_MS = 5000;
    std::mutex sConstructLock;
    std::unordered_map<uint32, uint32> sConstructsSeenMs;  // by instance id

    bool ConstructsUp(PlayerbotAI* botAI, Player* bot)
    {
        uint32 constructs = 0;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (unit && unit->GetEntry() == NPC_BONE_CONSTRUCT && unit->IsAlive())
                ++constructs;
        }

        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(sConstructLock);
        uint32& seenMs = sConstructsSeenMs[bot->GetInstanceId()];
        if (constructs >= CONSTRUCTS_TO_GATHER)
            seenMs = now ? now : 1;
        return seenMs && getMSTimeDiff(seenMs, now) < CONSTRUCTS_SEEN_MS;
    }

    // The tank the pile is made on while the Bone Constructs are up: the next one down the same
    // list that isn't holding Nefarian (a paladin's Consecration is the best thing to stand in).
    // nullptr while too few are up, or with no spare tank.
    Player* ConstructAnchor(PlayerbotAI* botAI, Player* bot, Creature* nefarian)
    {
        if (nefarian->GetHealthPct() > CONSTRUCT_PHASE_PCT)
            return nullptr;
        Player* holder = NefarianTank(bot);
        Group* group = bot->GetGroup();
        if (!holder || !group || !ConstructsUp(botAI, bot))
            return nullptr;

        Unit* victim = nefarian->GetVictim();
        Player* best = nullptr;
        int bestRank = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == holder || member == victim || !member->IsAlive() || !member->IsInMap(bot) ||
                !PlayerbotAI::IsTank(member) || !GET_PLAYERBOT_AI(member))
                continue;
            int const rank = NefarianTankRank(member);
            if (!best || rank < bestRank)
            {
                best = member;
                bestRank = rank;
            }
        }
        return best;
    }

    // --- The dragons: breath, tail, roar ----------------------------------

    constexpr float DRAGON_NEAR = 90.0f;

    // The dragon with a frontal breath that is fighting near the bot, if any.
    Creature* BreathingDragon(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID)
            return nullptr;
        for (uint32 const entry : { NPC_NEFARIAN, NPC_FIREMAW, NPC_EBONROC, NPC_FLAMEGOR })
            if (Creature* boss = RaidClear::FightingCreature(bot, { entry }))
                return bot->GetExactDist(boss) <= DRAGON_NEAR ? boss : nullptr;
        return nullptr;
    }

    // The tanks that stand in front of it with whoever it is on, so it doesn't turn through the
    // raid when it changes hands.
    bool StandsInFront(Player* bot, Creature* boss)
    {
        if (boss->GetVictim() == bot)
            return true;
        if (!PlayerbotAI::IsTank(bot))
            return false;
        if (boss->GetEntry() == NPC_NEFARIAN)
            return NefarianTank(bot) == bot;
        Group* group = bot->GetGroup();
        return group && RaidClear::Tanks::ActingMainTank(group) == bot;
    }

    // What a bot keeps out of around one dragon, as seen from that bot.
    struct Zone
    {
        float x = 0.0f;
        float y = 0.0f;
        float facing = 0.0f;
        float frontHalf = BREATH_KEEP_HALF_ANGLE;  // how far off his facing the front is kept clear
        bool tail = false;                         // Nefarian's Tail Lash
        float tailHalf = TAIL_HALF_ANGLE;
        bool roar = false;                         // Bellowing Roar, for a bot that keeps back
        float reach = 0.0f;                        // yards whoever he is on has to stay within; 0: any
        bool hasVictim = false;
        float victimX = 0.0f;
        float victimY = 0.0f;
    };

    // Yards from a spot `r` from the dragon and `off` radians off the middle of a cone to the
    // nearest way out of that cone: over its side or past its end. Negative inside.
    float ConeGap(float r, float off, float halfAngle, float range)
    {
        float const side = r * std::sin(std::min(std::fabs(off - halfAngle), float(M_PI) / 2.0f));
        if (off >= halfAngle)
            return r >= range ? std::max(side, r - range) : side;
        if (r >= range)
            return r - range;
        return -std::min(side, range - r);
    }

    // Radians from the dragon's facing round to a spot, anticlockwise: -pi to pi.
    float SignedOff(Zone const& zone, float x, float y)
    {
        float const off = Position::NormalizeOrientation(std::atan2(y - zone.y, x - zone.x) - zone.facing);
        return off > float(M_PI) ? off - 2.0f * float(M_PI) : off;
    }

    // Radians between the dragon's facing and the direction of a spot: 0 dead ahead, pi behind.
    float OffFacing(Zone const& zone, float x, float y)
    {
        return std::fabs(SignedOff(zone, x, y));
    }

    // Yards clear of the breath itself (the real cone, no margin). Negative inside.
    float BreathGap(Zone const& zone, float x, float y)
    {
        float const r = std::hypot(x - zone.x, y - zone.y);
        if (r < 0.5f)
            return -BREATH_RANGE;
        return ConeGap(r, OffFacing(zone, x, y), BREATH_HALF_ANGLE, BREATH_RANGE);
    }

    // Yards clear of everything this bot keeps out of around the dragon. Negative inside any.
    float DragonGap(Zone const& zone, float x, float y)
    {
        float const r = std::hypot(x - zone.x, y - zone.y);
        if (r < 0.5f)
            return -BREATH_RANGE;
        float const off = OffFacing(zone, x, y);

        float gap = ConeGap(r, off, zone.frontHalf, BREATH_RANGE);
        if (zone.tail)
            gap = std::min(gap, ConeGap(r, float(M_PI) - off, zone.tailHalf, TAIL_RANGE));
        if (zone.roar)
            gap = std::min(gap, r - ROAR_KEEP_AWAY);
        // A spot whoever he is on can't be reached from is no use to a healer.
        if (zone.reach > 0.0f && zone.hasVictim && std::hypot(x - zone.victimX, y - zone.victimY) > zone.reach)
            gap = std::min(gap, -ROAR_KEEP_AWAY);
        return gap;
    }

    // Where the dragon is and which way it breathes: the same for every bot.
    Zone PlaceOf(Creature* boss)
    {
        Zone zone;
        zone.x = boss->GetPositionX();
        zone.y = boss->GetPositionY();
        // A breath goes where he faces when the cast ends, and he keeps facing whoever he is on.
        // Somebody standing inside him has no direction worth the name.
        Unit* victim = boss->GetVictim();
        zone.facing = victim && boss->GetExactDist2d(victim) > 3.0f ? boss->GetAngle(victim) : boss->GetOrientation();
        zone.tail = boss->GetEntry() == NPC_NEFARIAN;
        if (victim)
        {
            zone.hasVictim = true;
            zone.victimX = victim->GetPositionX();
            zone.victimY = victim->GetPositionY();
        }
        return zone;
    }

    // The Bone Constructs' pile is made at Nefarian's side: the middle of the flank, so the bots
    // standing round the tank it is made on are clear of his front and tail too, and near enough
    // to whoever he is on for the healers in it.
    Zone AnchorZone(Creature* boss)
    {
        Zone zone = PlaceOf(boss);
        zone.frontHalf = CONSTRUCT_ANCHOR_FRONT;
        zone.tailHalf = CONSTRUCT_ANCHOR_TAIL;
        zone.reach = CONSTRUCT_ANCHOR_REACH;
        return zone;
    }

    // The tank the pile is made on, once it stands where the pile belongs. Until then (and with
    // nowhere for it to stand) there is no pile, and everyone keeps to the usual rules.
    Player* ConstructPile(PlayerbotAI* botAI, Player* bot, Creature* boss)
    {
        Player* anchor = ConstructAnchor(botAI, bot, boss);
        if (!anchor || DragonGap(AnchorZone(boss), anchor->GetPositionX(), anchor->GetPositionY()) < 0.0f)
            return nullptr;
        return anchor;
    }

    Zone ZoneOf(Player* bot, Creature* boss)
    {
        Zone zone = PlaceOf(boss);
        if (boss->GetEntry() != NPC_NEFARIAN)
            return zone;

        zone.roar = KeepsBack(bot);
        if (zone.roar && PlayerbotAI::IsHeal(bot))
            zone.reach = ROAR_HEALER_REACH;
        if (boss->GetHealthPct() > CONSTRUCT_PHASE_PCT)
            return zone;

        // The Bone Constructs' pile is inside the roar: with some 45 of them up, standing
        // together is worth a fear every half minute.
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        Player* anchor = botAI ? ConstructAnchor(botAI, bot, boss) : nullptr;
        if (anchor == bot)
            return AnchorZone(boss);
        if (anchor && zone.roar && ConstructPile(botAI, bot, boss))
        {
            zone.roar = false;
            zone.reach = 0.0f;
        }
        return zone;
    }

    // Whether the straight walk between two spots stays out of everything in the zone.
    bool WalkClear(Zone const& zone, float fromX, float fromY, float toX, float toY)
    {
        int const steps = std::max(1, int(std::hypot(toX - fromX, toY - fromY) / 3.0f));
        for (int i = 1; i <= steps; ++i)
        {
            float const t = float(i) / float(steps);
            if (DragonGap(zone, fromX + (toX - fromX) * t, fromY + (toY - fromY) * t) < 0.0f)
                return false;
        }
        return true;
    }

    // Behind something the dragon can't see past, a bot is in nobody's breath: the cover Firemaw's
    // Flame Buffet sends it to is as often as not in front of him.
    bool InDragonsSight(Player* bot, Creature* boss)
    {
        return boss->IsWithinLOSInMap(bot);
    }

    // A bot on its way to that cover (or due to go) isn't sent to his flank first.
    bool GoingForCover(Player* bot, Creature* boss)
    {
        if (boss->GetEntry() != NPC_FIREMAW)
            return false;
        Aura const* buffet = bot->GetAura(SPELL_FLAME_BUFFET);
        return buffet && buffet->GetStackAmount() >= FLAME_BUFFET_HIDE_STACKS && !PlayerbotAI::IsTank(bot);
    }

    // The breath or buffet the dragon is casting right now, or 0.
    uint32 ConeCast(Creature* boss)
    {
        Spell* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        uint32 const id = spell && spell->GetSpellInfo() ? spell->GetSpellInfo()->Id : 0;
        return id == SPELL_SHADOW_FLAME || id == SPELL_WING_BUFFET ? id : 0;
    }

    // --- Nefarian's rogue call ----------------------------------------------

    // The raid members he has stunned in reach of his breath (the rogues, 5 yd in front of his
    // middle when it lands).
    void ParalyzedNear(Player* bot, Creature* boss, std::vector<Player*>& out)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot && member->IsAlive() && member->IsInMap(bot) &&
                member->HasAura(SPELL_ROGUE_PARALYZE) && member->GetExactDist(boss) < BREATH_RANGE)
                out.push_back(member);
        }
    }

    // Whether a breath along `facing` would miss all of them by `clear` radians or more.
    bool BreathMisses(Creature* boss, float facing, std::vector<Player*> const& paralyzed, float clear)
    {
        Zone zone;
        zone.x = boss->GetPositionX();
        zone.y = boss->GetPositionY();
        zone.facing = facing;
        for (Player const* member : paralyzed)
            if (OffFacing(zone, member->GetPositionX(), member->GetPositionY()) < BREATH_HALF_ANGLE + clear)
                return false;
        return true;
    }

    // Where he faces with this bot as his target.
    float FacingToward(Creature* boss, Player* bot)
    {
        return boss->GetExactDist2d(bot) > 3.0f ? boss->GetAngle(bot) : boss->GetOrientation();
    }

    // --- Death Talon Wyrmguards -------------------------------------------

    // The Wyrmguards in the fight whose War Stomp this bot has no reason to stand in.
    void WyrmguardsToAvoid(PlayerbotAI* botAI, Player* bot, std::vector<Unit*>& out)
    {
        AiObjectContext* context = botAI->GetAiObjectContext();
        // A melee bot is in reach of the one it is hitting, and of any other standing next to
        // that one: nothing to do about either until the tanks have them apart.
        Unit* mine = KeepsBack(bot) ? nullptr : context->GetValue<Unit*>("current target")->Get();
        for (ObjectGuid const& guid : context->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || unit->GetEntry() != NPC_DEATH_TALON_WYRMGUARD || !unit->IsAlive() || !unit->IsInCombat())
                continue;
            if (mine && (unit == mine || unit->GetExactDist(mine) < WYRMGUARD_APART))
                continue;
            out.push_back(unit);
        }
    }

    float WyrmguardGap(std::vector<Unit*> const& guards, float x, float y, float z)
    {
        float gap = 1000.0f;
        for (Unit const* guard : guards)
            gap = std::min(gap, guard->GetExactDist(x, y, z));
        return gap;
    }

    // --- Nefarian: fear ------------------------------------------------

    // Victor Nefarius' own Fear in phase one, Bellowing Roar after he lands.
    bool InNefarianFight(Player* bot)
    {
        return FightingBoss(bot, NPC_NEFARIAN) || FightingBoss(bot, NPC_VICTOR_NEFARIUS);
    }

    // The tank a priest's Fear Ward should be on and isn't: whoever should have Nefarian, or with
    // a player tanking, whoever he is on. nullptr too far off to walk over for.
    Player* FearWardTarget(PlayerbotAI* botAI, Player* bot)
    {
        if (!bot->IsClass(CLASS_PRIEST) ||
            !botAI->GetAiObjectContext()->GetValue<uint32>("spell id", "fear ward")->Get())
            return nullptr;
        Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
        if (!boss)
            return nullptr;

        Player* tank = NefarianTank(bot);
        if (!tank)
        {
            Player* victim = boss->GetVictim() ? boss->GetVictim()->ToPlayer() : nullptr;
            tank = victim && PlayerbotAI::IsTank(victim) ? victim : nullptr;
        }
        if (!tank || !tank->IsAlive() || tank->HasAura(SPELL_FEAR_WARD) || bot->GetExactDist(tank) > FEAR_WARD_WALK)
            return nullptr;

        // One priest at a time walks in under the roar for it: the first in group order that has
        // it ready.
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member == bot)
                    break;
                // ...and is in a state to cast it: in the fight, with mana.
                if (member && member->IsAlive() && member->IsInMap(bot) && member->IsClass(CLASS_PRIEST) &&
                    GET_PLAYERBOT_AI(member) && member->HasSpell(SPELL_FEAR_WARD) &&
                    !member->HasSpellCooldown(SPELL_FEAR_WARD) && member->IsInCombat() &&
                    member->GetPowerPct(POWER_MANA) >= 10.0f && member->GetExactDist(tank) <= FEAR_WARD_WALK)
                    return nullptr;
            }
        return tank;
    }
}

// --- The dragons: breath, tail, roar --------------------------------------

bool RcBwlDragonPositionTrigger::IsActive()
{
    // A bot that is feared, or rooted in front of him by the rogue call, isn't going anywhere.
    if (!bot->IsAlive() || !botAI->CanMove())
        return false;
    Creature* boss = BreathingDragon(bot);
    if (!boss || StandsInFront(bot, boss) || GoingForCover(bot, boss))
        return false;
    return DragonGap(ZoneOf(bot, boss), bot->GetPositionX(), bot->GetPositionY()) < 0.0f &&
           InDragonsSight(bot, boss);
}

bool RcBwlDragonBreathTrigger::IsActive()
{
    if (!bot->IsAlive() || !botAI->CanMove())
        return false;
    Creature* boss = BreathingDragon(bot);
    if (!boss || !ConeCast(boss) || StandsInFront(bot, boss))
        return false;
    return BreathGap(ZoneOf(bot, boss), bot->GetPositionX(), bot->GetPositionY()) < 0.0f &&
           InDragonsSight(bot, boss);
}

bool RcBwlDragonMoveAction::Execute(Event /*event*/)
{
    Creature* boss = BreathingDragon(bot);
    if (!boss || StandsInFront(bot, boss))
        return false;

    Zone const zone = ZoneOf(bot, boss);
    auto const gapAt = [&zone](float x, float y, float /*z*/) { return DragonGap(zone, x, y); };
    if (gapAt(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= 0.0f || !InDragonsSight(bot, boss))
        return false;
    return _walk.Go(gapAt, DRAGON_SPOT_MIN, DRAGON_SPOT, DRAGON_WORTH_IT, DRAGON_SEARCH);
}

float RcBwlDragonHoldMultiplier::GetValue(Action* action)
{
    if (!action || !KeepsBack(bot))
        return 1.0f;

    // Playerbots sends every bot that isn't the dragon's victim to its flank at melee range,
    // above almost everything else the bot does. Right for melee; for a caster or a healer it is
    // a walk into Cleave, Tail Lash and the roar.
    // (By class: the actions are named after the boss, "nefarian rear flank", not after the
    // names the strategy asks for them by.)
    if (dynamic_cast<BossRearFlankAction*>(action))
        return 0.0f;

    // Nefarian: nothing walks a bot that keeps back in under the roar. Everything on him and at
    // his feet is in spell range from outside it; the walk to the tank for a Fear Ward is the
    // one exception.
    if (!Walks(action) || dynamic_cast<RcBwlFearWardAction*>(action))
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;
    Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
    if (!boss || bot->GetExactDist(boss) > ROAR_HOLD || target->GetExactDist(boss) >= ROAR_KEEP_AWAY)
        return 1.0f;
    // Not while the Bone Constructs' pile is on: that is inside the roar.
    return ConstructPile(botAI, bot, boss) ? 1.0f : 0.0f;
}

// --- Who holds Nefarian -----------------------------------------------------

float RcBwlNefarianTankMultiplier::GetValue(Action* action)
{
    if (!action || !PlayerbotAI::IsTank(bot))
        return 1.0f;

    // Playerbots' "tank face" walks the tank he is on to where he faces away from the ranged
    // group. With the rogues stunned in front of him the tank has just walked round him to turn
    // his back on them, and this would walk it back.
    if (dynamic_cast<TankFaceAction*>(action))
    {
        Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
        if (!boss || boss->GetVictim() != bot)
            return 1.0f;
        std::vector<Player*> paralyzed;
        ParalyzedNear(bot, boss, paralyzed);
        return paralyzed.empty() ? 1.0f : 0.0f;
    }

    // No taunt lands on him, so the order of the tanks' threat is all there is: every other tank
    // stays under the one that should have him. Swings carry on; specials and spells are held.
    if (action->getThreatType() == Action::ActionThreatType::None)
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || target->GetEntry() != NPC_NEFARIAN || !target->IsCreature() || target->GetMapId() != MAP_ID)
        return 1.0f;

    Player* tank = NefarianTank(bot);
    if (!tank || tank == bot || !tank->IsWithinMeleeRange(target))
        return 1.0f;
    // Only at the start, while nobody has much threat: the others wait for the tank that should
    // have him to take him. Past that he stays where he is. A tank that was just revived can't
    // catch up with minutes of threat, and holding the one he is on back for it would stall the
    // whole raid behind the threat cap.
    ThreatManager const& threat = target->GetThreatMgr();
    float const mine = threat.GetThreat(bot);
    float const theirs = threat.GetThreat(tank);
    if (mine >= NEFARIAN_HANDOVER_THREAT && (target->GetVictim() == bot || theirs < mine * NEFARIAN_CATCH_UP_SHARE))
        return 1.0f;
    return mine >= theirs * NEFARIAN_OFFTANK_SHARE ? 0.0f : 1.0f;
}

// --- Nefarian: fear ---------------------------------------------------------

bool RcBwlNefarianTremorTrigger::IsActive()
{
    if (bot->getClass() != CLASS_SHAMAN || !bot->IsInCombat() || !InNefarianFight(bot))
        return false;
    return !AI_VALUE2(bool, "has totem", "tremor totem");
}

float RcBwlNefarianTremorMultiplier::GetValue(Action* action)
{
    if (!action || bot->getClass() != CLASS_SHAMAN || !bot->IsInCombat())
        return 1.0f;

    // Playerbots' names for whatever puts a totem in the earth slot or takes this one away.
    static constexpr std::array<char const*, 6> EARTH_SLOT = {
        "stoneskin totem", "strength of earth totem", "earthbind totem", "stoneclaw totem",
        "call of the elements", "totemic recall",
    };
    std::string const name = action->getName();
    if (std::none_of(EARTH_SLOT.begin(), EARTH_SLOT.end(), [&name](char const* other) { return name == other; }))
        return 1.0f;

    // A shaman without Tremor Totem keeps what it has.
    return InNefarianFight(bot) && AI_VALUE2(uint32, "spell id", "tremor totem") ? 0.0f : 1.0f;
}

Unit* RcBwlFearWardAction::GetTarget()
{
    return FearWardTarget(botAI, bot);
}

bool RcBwlFearWardTrigger::IsActive()
{
    return bot->IsAlive() && bot->IsInCombat() && !bot->HasSpellCooldown(SPELL_FEAR_WARD) &&
           FearWardTarget(botAI, bot);
}

bool RcBwlFearWardAction::Execute(Event /*event*/)
{
    Unit* tank = GetTarget();
    if (!tank)
        return false;
    // In range and it wouldn't cast a moment ago (silenced, out of mana): not worth walking
    // back in for every time the bot is sent out from under the roar again.
    uint32 const now = getMSTime();
    if (_failedAtMs && getMSTimeDiff(_failedAtMs, now) < FEAR_WARD_RETRY_MS)
        return false;
    if (bot->GetExactDist(tank) > FEAR_WARD_RANGE || !bot->IsWithinLOSInMap(tank))
        return MoveNear(tank, FEAR_WARD_RANGE - 5.0f, MovementPriority::MOVEMENT_COMBAT);
    if (botAI->CanCastSpell("fear ward", tank) && botAI->CastSpell("fear ward", tank))
    {
        _failedAtMs = 0;
        return true;
    }
    _failedAtMs = now ? now : 1;
    return false;
}

// --- Nefarian's rogue call --------------------------------------------------

bool RcBwlNefarianRogueCallTrigger::IsActive()
{
    if (!bot->IsAlive() || !botAI->CanMove())
        return false;
    Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
    if (!boss || boss->GetVictim() != bot)
        return false;
    std::vector<Player*> paralyzed;
    ParalyzedNear(bot, boss, paralyzed);
    return !paralyzed.empty() && !BreathMisses(boss, FacingToward(boss, bot), paralyzed, 0.0f);
}

bool RcBwlNefarianTurnAction::Execute(Event /*event*/)
{
    Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
    if (!boss || boss->GetVictim() != bot)
        return false;
    std::vector<Player*> paralyzed;
    ParalyzedNear(bot, boss, paralyzed);
    if (paralyzed.empty() || BreathMisses(boss, FacingToward(boss, bot), paralyzed, 0.0f))
        return false;

    // Already on the way to a spot that does it.
    if (bot->isMoving() && !bot->movespline->Finalized())
    {
        G3D::Vector3 const dest = bot->movespline->FinalDestination();
        if (boss->GetExactDist2d(dest.x, dest.y) > 3.0f &&
            BreathMisses(boss, boss->GetAngle(dest.x, dest.y), paralyzed, NEFARIAN_TURN_CLEAR))
            return true;
    }

    // Straight across from them, as far from his middle as the bot stands now, and failing that
    // (a wall, the edge of the balcony) the nearest spot either way round that still does it.
    float sumX = 0.0f;
    float sumY = 0.0f;
    for (Player const* member : paralyzed)
    {
        sumX += member->GetPositionX();
        sumY += member->GetPositionY();
    }
    float const away = boss->GetAngle(sumX / float(paralyzed.size()), sumY / float(paralyzed.size())) + float(M_PI);
    float const reach = std::clamp(bot->GetExactDist2d(boss), 6.0f, std::max(6.0f, boss->GetCombatReach()));

    Map* map = bot->GetMap();
    for (float const turn : { 0.0f, 0.6f, -0.6f, 1.2f, -1.2f })
    {
        float const angle = Position::NormalizeOrientation(away + turn);
        if (!BreathMisses(boss, angle, paralyzed, NEFARIAN_TURN_CLEAR))
            continue;
        float const x = boss->GetPositionX() + std::cos(angle) * reach;
        float const y = boss->GetPositionY() + std::sin(angle) * reach;
        float const z = map->GetHeight(bot->GetPhaseMask(), x, y, bot->GetPositionZ() + RaidClear::WAY_OUT_UP);
        if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > RaidClear::WAY_OUT_DOWN)
            continue;

        PathGenerator path(bot);
        constexpr uint32 NOT_A_WALK = PATHFIND_INCOMPLETE | PATHFIND_NOPATH | PATHFIND_SHORTCUT |
                                      PATHFIND_NOT_USING_PATH | PATHFIND_FARFROMPOLY;
        if (!path.CalculatePath(x, y, z) || !(path.GetPathType() & PATHFIND_NORMAL) || (path.GetPathType() & NOT_A_WALK))
            continue;

        AI_VALUE(LastMovement&, "last movement").clear();
        return MoveTo(bot->GetMapId(), x, y, z, false, false, false, true, MovementPriority::MOVEMENT_FORCED);
    }
    return false;
}

// --- Nefarian's Bone Constructs ---------------------------------------------

bool RcBwlConstructGatherTrigger::IsActive()
{
    if (!bot->IsAlive() || !KeepsBack(bot) || !botAI->CanMove())
        return false;
    Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
    if (!boss)
        return false;
    Player* anchor = ConstructPile(botAI, bot, boss);
    return anchor && anchor != bot && bot->GetExactDist(anchor) > CONSTRUCT_GATHER_FAR;
}

bool RcBwlConstructGatherAction::Execute(Event /*event*/)
{
    Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
    Player* anchor = boss ? ConstructPile(botAI, bot, boss) : nullptr;
    if (!anchor || anchor == bot || bot->GetExactDist(anchor) <= CONSTRUCT_GATHER_FAR)
        return false;

    // Straight there when that doesn't cross his front or his tail.
    Zone const zone = ZoneOf(bot, boss);
    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    if (WalkClear(zone, botX, botY, anchor->GetPositionX(), anchor->GetPositionY()))
        return MoveNear(anchor, CONSTRUCT_GATHER_SPOT, MovementPriority::MOVEMENT_COMBAT);

    // Otherwise a step round him, far enough out to pass behind his tail: toward the pile's
    // angle on its own side of him, and from his other side by the back, never across the front.
    float const mine = SignedOff(zone, botX, botY);
    float const theirs = SignedOff(zone, anchor->GetPositionX(), anchor->GetPositionY());
    float next = mine;
    if ((mine >= 0.0f) == (theirs >= 0.0f))
        next += std::clamp(theirs - mine, -CONSTRUCT_RING_STEP, CONSTRUCT_RING_STEP);
    else
        next += mine >= 0.0f ? CONSTRUCT_RING_STEP : -CONSTRUCT_RING_STEP;

    float const r = std::max(std::hypot(botX - zone.x, botY - zone.y), TAIL_RANGE + 3.0f);
    float const angle = Position::NormalizeOrientation(zone.facing + next);
    float const x = zone.x + std::cos(angle) * r;
    float const y = zone.y + std::sin(angle) * r;
    float const z = bot->GetMap()->GetHeight(bot->GetPhaseMask(), x, y, bot->GetPositionZ() + RaidClear::WAY_OUT_UP);
    if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > RaidClear::WAY_OUT_DOWN ||
        !WalkClear(zone, botX, botY, x, y))
        return false;
    return MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT);
}

float RcBwlConstructAnchorMultiplier::GetValue(Action* action)
{
    if (!action || !Walks(action) || !PlayerbotAI::IsTank(bot))
        return 1.0f;
    Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
    if (!boss || ConstructAnchor(botAI, bot, boss) != bot)
        return 1.0f;
    // A step to something that has come to the pile is fine.
    Unit* target = action->GetTarget();
    return target && target != bot && bot->GetExactDist(target) < CONSTRUCT_GATHER_NEAR ? 1.0f : 0.0f;
}

// --- Death Talon Wyrmguards -------------------------------------------------

bool RcBwlWyrmguardKeepAwayTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || PlayerbotAI::IsTank(bot) || !botAI->CanMove())
        return false;
    std::vector<Unit*> guards;
    WyrmguardsToAvoid(botAI, bot, guards);
    return !guards.empty() &&
           WyrmguardGap(guards, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) < WYRMGUARD_KEEP_AWAY;
}

bool RcBwlWyrmguardKeepAwayAction::Execute(Event /*event*/)
{
    if (PlayerbotAI::IsTank(bot))
        return false;
    std::vector<Unit*> guards;
    WyrmguardsToAvoid(botAI, bot, guards);
    if (guards.empty())
        return false;

    auto const gapAt = [&guards](float x, float y, float z) { return WyrmguardGap(guards, x, y, z); };
    if (gapAt(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= WYRMGUARD_KEEP_AWAY)
        return false;
    return _walk.Go(gapAt, WYRMGUARD_KEEP_AWAY, WYRMGUARD_SPOT, WYRMGUARD_WORTH_IT);
}

float RcBwlWyrmguardHoldMultiplier::GetValue(Action* action)
{
    // Casters and healers reach everything round a Wyrmguard from outside its stomp, so nothing
    // walks them in. Melee go to their own target and are sent out from the others.
    if (!action || !Walks(action) || !KeepsBack(bot) || !bot->IsInCombat())
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;

    std::vector<Unit*> guards;
    WyrmguardsToAvoid(botAI, bot, guards);
    for (Unit const* guard : guards)
        if (bot->GetExactDist(guard) < WYRMGUARD_HOLD && target->GetExactDist(guard) < WYRMGUARD_KEEP_AWAY)
            return 0.0f;
    return 1.0f;
}

// --- Spell hooks ------------------------------------------------------------

// A bot in the middle of a cast doesn't run its engine until the cast ends, and a breath is two
// seconds: wake the ones standing in it the moment the dragon starts. The same hook keeps
// Chromaggus' breath clock.
class RaidClearBwlSpellScript : public AllSpellScript
{
public:
    RaidClearBwlSpellScript() : AllSpellScript("RaidClearBwlSpellScript", { ALLSPELLHOOK_ON_PREPARE }) {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || caster->GetMapId() != MAP_ID || !caster->IsCreature())
            return;
        if (!RaidClear::GetConfig().IsRaidEnabled(MAP_ID))
            return;

        Creature* boss = caster->ToCreature();
        if (boss->GetEntry() == NPC_CHROMAGGUS)
        {
            NoteChromaggusBreath(boss, spellInfo->Id);
            return;
        }

        if (spellInfo->Id != SPELL_SHADOW_FLAME && spellInfo->Id != SPELL_WING_BUFFET)
            return;
        Map::PlayerList const& players = boss->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive() || player->GetExactDist(boss) > BREATH_RANGE + 5.0f)
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->HasStrategy("rc bwl", BOT_STATE_COMBAT) || StandsInFront(player, boss))
                continue;
            if (BreathGap(ZoneOf(player, boss), player->GetPositionX(), player->GetPositionY()) >= 0.0f ||
                !InDragonsSight(player, boss))
                continue;

            botAI->RequestSpellInterrupt();
            botAI->ResetActionDuration();
        }
    }
};

void RaidClear::BlackwingLair::AddPositionScripts()
{
    new RaidClearBwlSpellScript();
}
