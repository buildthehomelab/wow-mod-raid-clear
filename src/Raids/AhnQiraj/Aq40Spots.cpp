/*
 * mod-raid-clear: Temple of Ahn'Qiraj, fixed spots from the raid guide: Skeram's platforms, the
 * Bug Trio tanked apart (Vem in his alcove), and the config behind them (the Twin Emperors' torch
 * spots are read in Aq40TwinEmperors.cpp). See AqRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"
#include "Common/Charmed.h"
#include "Common/FixedSpots.h"

#include "Creature.h"
#include "Group.h"
#include "LastMovementValue.h"
#include "Log.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Playerbots.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace RaidClear::TempleOfAhnQiraj;
namespace Spots = RaidClear::Spots;

// --- Config -----------------------------------------------------------------

namespace
{
    std::array<std::optional<Position>, size_t(Spot::Count)> sSpots;

    constexpr std::array<char const*, size_t(Spot::Count)> SPOT_KEYS = {
        "Skeram.TankSpot",        "Skeram.RangedSpot",      "Skeram.HealerSpot",
        "BugTrio.VemTankSpot",    "BugTrio.YaujTankSpot",   "BugTrio.KriTankSpot",
        "BugTrio.RangedSpot",     "BugTrio.HealerSpot",
        "TwinEmperors.TankSpotA", "TwinEmperors.TankSpotB",
    };

    // A bug within 10 yd of this spot evades (boss_bug_trio.cpp: the way out of their room). A tank
    // spot this close to it would reset the fight.
    Position const BUG_RESET_POINT(-8582.0f, 2047.0f, -1.62f);
    constexpr float BUG_RESET_KEEP = 15.0f;

    std::string KeyOf(Spot spot) { return std::string("RaidClear.TempleOfAhnQiraj.") + SPOT_KEYS[size_t(spot)]; }
}

void RaidClear::TempleOfAhnQiraj::LoadSpots()
{
    for (size_t i = 0; i < sSpots.size(); ++i)
        sSpots[i] = Spots::Load(KeyOf(Spot(i)));

    for (Spot const bugSpot : { Spot::VemTank, Spot::YaujTank, Spot::KriTank })
    {
        std::optional<Position>& spot = sSpots[size_t(bugSpot)];
        if (spot && spot->GetExactDist2d(BUG_RESET_POINT) < BUG_RESET_KEEP)
        {
            LOG_ERROR("module", "mod-raid-clear: {} is within {} yd of where the Bug Trio evades; the spot is ignored.",
                      KeyOf(bugSpot), BUG_RESET_KEEP);
            spot.reset();
        }
    }

    // Heal Brother reaches 60 yd between the twins' edges: spots closer than that keep them healing.
    std::optional<Position> const& a = sSpots[size_t(Spot::TwinsA)];
    std::optional<Position> const& b = sSpots[size_t(Spot::TwinsB)];
    if (a && b && a->GetExactDist2d(*b) < TWINS_HEAL_RANGE + 2.0f * TWINS_APART_MARGIN)
        LOG_ERROR("module", "mod-raid-clear: {} and {} are {:.0f} yd apart; the Twin Emperors heal each other within "
                  "60 yd (edge to edge), so they keep healing at those spots.",
                  KeyOf(Spot::TwinsA), KeyOf(Spot::TwinsB), a->GetExactDist2d(*b));
}

Position const* RaidClear::TempleOfAhnQiraj::SpotOf(Spot spot)
{
    if (spot >= Spot::Count)
        return nullptr;
    std::optional<Position> const& value = sSpots[size_t(spot)];
    return value ? &*value : nullptr;
}

// --- Where each role stands ----------------------------------------------------

namespace
{
    // Spell and heal reach, with a margin, between two players' middles (heals) and from a
    // caster's middle to the edge of a boss (damage).
    constexpr float SPOT_HEAL_REACH = 38.0f;
    constexpr float SPOT_SPELL_REACH = 33.0f;
    constexpr float EYE_HEIGHT = 2.0f;

    bool SeesFrom(Player* bot, Position const& place, Unit const* other)
    {
        return bot->GetMap()->isInLineOfSight(place.GetPositionX(), place.GetPositionY(), place.GetPositionZ() + EYE_HEIGHT,
                                              other->GetPositionX(), other->GetPositionY(),
                                              other->GetPositionZ() + other->GetCollisionHeight(), bot->GetPhaseMask(),
                                              LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2);
    }

    // Whether a caster or healer standing there can do its job: a healer reach and see whoever
    // each boss of the fight is on (the Bug Trio's three tanks), a damage dealer reach and see the
    // first boss. A spot it can't is no spot: the bot is left to playerbots' own positioning until
    // it can again.
    bool CanWorkFrom(Player* bot, std::vector<Unit*> const& bosses, Position const& place)
    {
        if (PlayerbotAI::IsHeal(bot))
        {
            for (Unit* boss : bosses)
            {
                Unit* victim = boss->GetVictim();
                if (victim && (victim->GetExactDist(place) > SPOT_HEAL_REACH || !SeesFrom(bot, place, victim)))
                    return false;
            }
            return true;
        }
        Unit* boss = bosses.front();
        return boss->GetExactDist2d(place.GetPositionX(), place.GetPositionY()) - boss->GetCombatReach() <=
                   SPOT_SPELL_REACH &&
               SeesFrom(bot, place, boss);
    }

    // A caster's or healer's spot dropped for being out of reach, or anyone's that its walks
    // don't get it to, is kept dropped for this long, so a tank at the edge of heal range doesn't
    // flip the bot between its spot and free movement every tick. By bot, for all instances (a bot
    // is in one at a time).
    constexpr uint32 SPOT_DROP_MS = 3000;
    // A walk that should have arrived this recently, and ended short of the spot, counts as a spot
    // the bot can't get to.
    constexpr uint32 SPOT_STUCK_MS = 5000;
    // Within this of the end of that walk counts as having run it all.
    constexpr float SPOT_STUCK_END = 1.5f;
    std::mutex sSpotDropLock;
    std::unordered_map<ObjectGuid, uint32> sSpotDropped;  // when

    bool SpotDropped(Player* bot, uint32 now)
    {
        std::lock_guard<std::mutex> guard(sSpotDropLock);
        auto it = sSpotDropped.find(bot->GetGUID());
        if (it == sSpotDropped.end())
            return false;
        if (getMSTimeDiff(it->second, now) < SPOT_DROP_MS)
            return true;
        sSpotDropped.erase(it);
        return false;
    }

    void DropSpot(Player* bot, uint32 now)
    {
        std::lock_guard<std::mutex> guard(sSpotDropLock);
        sSpotDropped[bot->GetGUID()] = now ? now : 1;
    }

    struct SpotWant
    {
        Position place;
        bool tank = false;
    };

    // A ranged damage dealer's or healer's spot in this fight, if the config has one it can work
    // from. `bosses`: the fight's bosses, the one the damage dealers hit first.
    bool BackRowWant(Player* bot, std::vector<Unit*> const& bosses, Spot ranged, Spot healer, SpotWant& want)
    {
        if (PlayerbotAI::IsTank(bot))
            return false;
        Position const* spot = nullptr;
        if (PlayerbotAI::IsHeal(bot))
        {
            spot = SpotOf(healer);
            if (!spot)
                spot = SpotOf(ranged);
        }
        else if (PlayerbotAI::IsRanged(bot))
            spot = SpotOf(ranged);
        if (!spot)
            return false;

        want.place = Spots::Scatter(bot, *spot);
        if (!CanWorkFrom(bot, bosses, want.place))
        {
            DropSpot(bot, getMSTime());
            return false;
        }
        return true;
    }

    // --- Skeram -------------------------------------------------------------

    // Skeram's three platforms, where his Blinks land (the core's spell_target_position). Read
    // once: the spell data is loaded long before any fight.
    std::vector<Position> const& SkeramPlatforms()
    {
        static std::vector<Position> const platforms = []
        {
            std::vector<Position> found;
            for (uint32 const spell : { SPELL_SKERAM_BLINK_1, SPELL_SKERAM_BLINK_2, SPELL_SKERAM_BLINK_3 })
                if (SpellTargetPosition const* to = sSpellMgr->GetSpellTargetPosition(spell, EFFECT_0))
                    if (to->target_mapId == MAP_ID)
                        found.emplace_back(to->target_X, to->target_Y, to->target_Z);
            return found;
        }();
        return platforms;
    }

    // Skeram himself (his images are summons; the lookup finds the spawn), alive and fighting.
    Creature* FightingSkeram(Player* bot)
    {
        Creature* skeram = RaidClear::FightingCreature(bot, { NPC_SKERAM });
        return skeram && !skeram->IsSummon() ? skeram : nullptr;
    }

    // Splitting into images: rooted, passive and out of reach for a moment.
    bool Splitting(Creature const* skeram)
    {
        return skeram->HasReactState(REACT_PASSIVE) || skeram->IsImmuneToPC();
    }

    // Where his tank holds him: the config's spot, else his own platform (the one nearest his
    // spawn).
    Position const* SkeramTankSpot(Creature const* skeram)
    {
        if (Position const* spot = SpotOf(Spot::SkeramTank))
            return spot;
        Position const* best = nullptr;
        for (Position const& platform : SkeramPlatforms())
            if (!best || skeram->GetHomePosition().GetExactDist(platform) < skeram->GetHomePosition().GetExactDist(*best))
                best = &platform;
        return best;
    }

    // This tank's platform: the first two off-tanks in group order (living bots that aren't the
    // acting main tank, nor mind-controlled) take the two platforms other than the middle one, in
    // the spells' order. nullptr for anyone else.
    Position const* SkeramPost(Player* bot, Position const& tankSpot)
    {
        if (!PlayerbotAI::IsTank(bot))
            return nullptr;
        Group* group = bot->GetGroup();
        Player* mainTank = group ? RaidClear::Tanks::ActingMainTank(group) : nullptr;
        if (!mainTank || mainTank == bot)
            return nullptr;

        std::vector<Position> const& platforms = SkeramPlatforms();
        size_t middle = 0;
        for (size_t i = 1; i < platforms.size(); ++i)
            if (platforms[i].GetExactDist(tankSpot) < platforms[middle].GetExactDist(tankSpot))
                middle = i;
        std::vector<Position const*> sides;
        for (size_t i = 0; i < platforms.size(); ++i)
            if (i != middle)
                sides.push_back(&platforms[i]);

        size_t n = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref && n < sides.size(); ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == mainTank || !member->IsAlive() || !member->IsInMap(bot) ||
                !GET_PLAYERBOT_AI(member) || !PlayerbotAI::IsTank(member) ||
                RaidClear::Charmed::IsCharmedByEnemy(member))
                continue;
            if (member == bot)
                return sides[n];
            ++n;
        }
        return nullptr;
    }

    // Skeram or one of his images is on the bot.
    bool HeldBySkeram(Player* bot)
    {
        for (Unit* attacker : bot->getAttackers())
            if (attacker->GetEntry() == NPC_SKERAM && attacker->IsAlive() && attacker->GetVictim() == bot)
                return true;
        return false;
    }

    bool SkeramWant(PlayerbotAI* botAI, Player* bot, Creature* skeram, SpotWant& want)
    {
        Position const* tankSpot = SkeramTankSpot(skeram);
        if (!tankSpot)
            return false;

        if (PlayerbotAI::IsTank(bot))
        {
            // Whoever he is on brings him back to the middle after a blink (he follows it).
            if (skeram->GetVictim() == bot)
            {
                if (Splitting(skeram))
                    return false;
                want.place = *tankSpot;
                want.tank = true;
                return true;
            }
            // An off-tank with nothing of its own (or only hitting him while another tank holds
            // him) waits on its platform. One that has picked up an image, or is going for one,
            // fights it where it is.
            Position const* post = SkeramPost(bot, *tankSpot);
            if (!post || HeldBySkeram(bot))
                return false;
            Unit* target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
            if (target && target->IsAlive() && target != skeram)
                return false;
            want.place = *post;
            want.tank = true;
            return true;
        }

        return BackRowWant(bot, { skeram }, Spot::SkeramRanged, Spot::SkeramHealer, want);
    }

    // --- Bug Trio -----------------------------------------------------------

    // Not a spot in one of Kri's Poison Clouds: the bot would walk in and straight out again.
    bool SpotClear(Player* bot, Position const& place)
    {
        return ToxinGap(BugTrioClouds(bot), &place) >= TOXIN_KEEP + 1.0f;
    }

    bool BugWant(Player* bot, SpotWant& want)
    {
        // Each bug is its own lookup: one with several entries stops at the first spawn.
        Creature* vem = RaidClear::FightingCreature(bot, { NPC_VEM });
        Creature* yauj = RaidClear::FightingCreature(bot, { NPC_YAUJ });
        Creature* kri = RaidClear::FightingCreature(bot, { NPC_KRI });
        if (!vem && !yauj && !kri)
            return false;

        if (PlayerbotAI::IsTank(bot))
        {
            // A bug walking over to eat another (passive) follows nobody: no spot then. With two
            // on one tank, Vem's spot first: his is the one that keeps his Charge off the raid.
            struct BugSpot
            {
                Creature* bug;
                Spot spot;
            };
            for (BugSpot const& one : { BugSpot{ vem, Spot::VemTank }, BugSpot{ yauj, Spot::YaujTank },
                                        BugSpot{ kri, Spot::KriTank } })
            {
                if (!one.bug || one.bug->GetVictim() != bot || one.bug->HasReactState(REACT_PASSIVE))
                    continue;
                if (Position const* spot = SpotOf(one.spot))
                {
                    want.place = *spot;
                    want.tank = true;
                    return SpotClear(bot, want.place);
                }
            }
            return false;
        }

        // The first of them in the kill order is what the damage dealers have to reach; the
        // healers have to reach all three tanks.
        std::vector<Unit*> bosses;
        for (Creature* bug : { kri, yauj, vem })
            if (bug)
                bosses.push_back(bug);
        return BackRowWant(bot, bosses, Spot::BugRanged, Spot::BugHealer, want) && SpotClear(bot, want.place);
    }

    // Where this bot belongs right now, if its role has a spot in the fight it is in.
    bool WantedSpot(PlayerbotAI* botAI, Player* bot, SpotWant& want)
    {
        if (bot->GetMapId() != MAP_ID || !bot->IsAlive() || !bot->IsInCombat() ||
            RaidClear::Charmed::IsCharmedByEnemy(bot) || SpotDropped(bot, getMSTime()))
            return false;
        if (Creature* skeram = FightingSkeram(bot))
            return SkeramWant(botAI, bot, skeram, want);
        return BugWant(bot, want);
    }

    float SpotNear(SpotWant const& want) { return want.tank ? Spots::TANK_NEAR : Spots::NEAR; }

    // Skeram himself, landed on this off-tank's platform (it is standing on it) and not on a tank:
    // the off-tank takes him before he Earth Shocks whoever he is on.
    Creature* SkeramToPickUp(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID || !bot->IsAlive() || !PlayerbotAI::IsTank(bot) ||
            RaidClear::Charmed::IsCharmedByEnemy(bot))
            return nullptr;
        Creature* skeram = FightingSkeram(bot);
        if (!skeram || Splitting(skeram))
            return nullptr;
        Position const* tankSpot = SkeramTankSpot(skeram);
        Position const* post = tankSpot ? SkeramPost(bot, *tankSpot) : nullptr;
        if (!post || bot->GetExactDist(*post) > SKERAM_POST_NEAR || skeram->GetExactDist(*post) > SKERAM_PICKUP_RANGE)
            return nullptr;
        Unit* victim = skeram->GetVictim();
        if (victim == bot)
            return nullptr;
        Player* player = victim ? victim->ToPlayer() : nullptr;
        if (player && PlayerbotAI::IsTank(player) && !RaidClear::Charmed::IsCharmedByEnemy(player))
            return nullptr;
        return skeram;
    }
}

bool RcAq40BossSpotTrigger::IsActive()
{
    if (!botAI->CanMove())
        return false;
    uint32 const now = getMSTime();
    if (!_checkedAtMs || getMSTimeDiff(_checkedAtMs, now) >= 250)
    {
        _checkedAtMs = now ? now : 1;
        SpotWant want;
        _active = WantedSpot(botAI, bot, want) && bot->GetExactDist(want.place) > SpotNear(want);
    }
    return _active;
}

bool RcAq40BossSpotAction::Execute(Event /*event*/)
{
    SpotWant want;
    if (!WantedSpot(botAI, bot, want) || bot->GetExactDist(want.place) <= SpotNear(want))
        return false;
    // On its way: a tank holds the tick (nothing else should walk it), a caster or healer lets its
    // heals and instants go out meanwhile.
    if (Spots::OnTheWay(bot, want.place))
        return want.tank;

    // The last walk to this spot ran to its end, and its end is short of the spot (the path can't
    // get there): the spot is dropped for a while rather than sent again every tick, the bot's
    // rotation stopped meanwhile. A walk cut off on the way (a knockback, a stun, a fear, a dodge)
    // ends elsewhere and is simply sent again.
    uint32 const now = getMSTime();
    if (_arriveMs && getMSTimeDiff(_arriveMs, now) < SPOT_STUCK_MS && !bot->isMoving() &&
        _sentSpot.GetExactDist(want.place) < 1.0f && bot->GetExactDist(_sentEnd) < SPOT_STUCK_END &&
        _sentEnd.GetExactDist(want.place) > SpotNear(want))
    {
        _arriveMs = 0;
        DropSpot(bot, now);
        return false;
    }

    if (want.tank)
        AI_VALUE(LastMovement&, "last movement").clear();
    if (!MoveTo(bot->GetMapId(), want.place.GetPositionX(), want.place.GetPositionY(), want.place.GetPositionZ(),
                false, false, false, false,
                want.tank ? MovementPriority::MOVEMENT_FORCED : MovementPriority::MOVEMENT_COMBAT))
        return false;
    // Only a walk that started arms the check (a channelling bot is sent nowhere), from when it
    // should arrive: a long walk takes longer than the check's window.
    _arriveMs = 0;
    if (bot->isMoving() && !bot->movespline->Finalized())
    {
        G3D::Vector3 const end = bot->movespline->FinalDestination();
        _sentSpot = want.place;
        _sentEnd.Relocate(end.x, end.y, end.z);
        uint32 const arrive = now + uint32(std::max(0, bot->movespline->Duration()));
        _arriveMs = arrive ? arrive : 1;
    }
    return true;
}

float RcAq40SpotHoldMultiplier::GetValue(Action* action)
{
    if (!action || dynamic_cast<RcAq40BossSpotAction*>(action) || !Spots::Walks(action))
        return 1.0f;

    uint32 const now = getMSTime();
    if (!_checkedAtMs || getMSTimeDiff(_checkedAtMs, now) >= 250)
    {
        _checkedAtMs = now ? now : 1;
        SpotWant want;
        _holds = WantedSpot(botAI, bot, want) && bot->GetExactDist(want.place) <= Spots::HOLD;
    }
    if (!_holds)
        return 1.0f;

    // A tank on its spot has its back to the wall: the knockback footing would walk it off.
    if (action->getName() == "rc knockback footing")
        return 0.0f;
    return Spots::IsDodge(action) ? 1.0f : 0.0f;
}

bool RcAq40SkeramPickupTrigger::IsActive()
{
    Creature* skeram = SkeramToPickUp(bot);
    return skeram && RaidClear::Tanks::CanTaunt(botAI, skeram);
}

bool RcAq40SkeramPickupAction::Execute(Event /*event*/)
{
    Creature* skeram = SkeramToPickUp(bot);
    return skeram && RaidClear::Tanks::Taunt(botAI, skeram);
}
