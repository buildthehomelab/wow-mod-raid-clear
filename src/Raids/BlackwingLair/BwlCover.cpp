/*
 * mod-raid-clear: Blackwing Lair, getting out of a boss's sight (Firemaw's Flame Buffet,
 * Chromaggus' breaths) and the fixed spots those two fights can have. See BwlRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "BwlRaidClear.h"

#include "RaidClearConfig.h"
#include "Common/AggroLeash.h"
#include "Common/BossLookup.h"
#include "Common/FixedSpots.h"

#include "Config.h"
#include "Creature.h"
#include "GameTime.h"
#include "GenericSpellActions.h"
#include "Group.h"
#include "LastMovementValue.h"
#include "Log.h"
#include "Map.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace RaidClear::BlackwingLair;

// --- Sight -------------------------------------------------------------------

namespace
{
    // A player's eyes above the floor it stands on.
    constexpr float EYE_HEIGHT = 2.0f;
    // How long a searched spot is trusted before searching again, and how long to wait after a
    // search found nothing (each one is up to ~150 line-of-sight checks and paths).
    constexpr uint64 COVER_REUSE_MS = 15000;
    constexpr uint64 COVER_RETRY_MS = 3000;
    constexpr uint64 HIDE_RESEND_MS = 1500;
}

bool RaidClear::BlackwingLair::SeenBy(Unit const* boss, WorldObject const* who)
{
    // What Spell::CheckEffectTarget asks: M2 models (small props) don't block a spell.
    return boss && who && boss->IsWithinLOSInMap(who, VMAP::ModelIgnoreFlags::M2);
}

bool RaidClear::BlackwingLair::SeesSpot(Unit const* boss, float x, float y, float z, float height)
{
    if (!boss || !boss->IsInWorld())
        return false;
    // WorldObject::IsWithinLOS(x, y, z) raises the spot by the asker's own height, a dragon's:
    // that looks over a wall a player hides behind. From the boss's eyes to the player's, as
    // IsWithinLOSInMap does for a player standing there.
    float const eyeZ = z + (height > 0.0f ? height : EYE_HEIGHT);
    Position const from = boss->GetHitSpherePointFor({ x, y, eyeZ });
    return boss->GetMap()->isInLineOfSight(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(), x, y, eyeZ,
                                           boss->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS,
                                           VMAP::ModelIgnoreFlags::M2);
}

// --- Fixed spots ---------------------------------------------------------------

namespace
{
    using SpotSet = std::array<std::optional<Position>, size_t(Spot::Count)>;
    SpotSet sFiremawSpots;
    SpotSet sChromaggusSpots;

    constexpr std::array<char const*, size_t(Spot::Count)> SPOT_KEYS = {
        "TankSpot", "OffTankSpot", "TankHideSpot", "MeleeHideSpot",
        "RangedSpot", "RangedHideSpot", "HealerSpot", "HealerHideSpot",
    };

    void LoadSet(SpotSet& set, std::string const& boss)
    {
        for (size_t i = 0; i < set.size(); ++i)
        {
            std::string const key = "RaidClear.BlackwingLair." + boss + "." + SPOT_KEYS[i];
            set[i] = RaidClear::Spots::Load(key);
        }
    }
}

void RaidClear::BlackwingLair::LoadSpots()
{
    LoadSet(sFiremawSpots, "Firemaw");
    LoadSet(sChromaggusSpots, "Chromaggus");
}

Position const* RaidClear::BlackwingLair::BossSpot(uint32 bossEntry, Spot spot)
{
    SpotSet const* set = bossEntry == NPC_FIREMAW ? &sFiremawSpots : bossEntry == NPC_CHROMAGGUS ? &sChromaggusSpots : nullptr;
    if (!set || spot >= Spot::Count)
        return nullptr;
    std::optional<Position> const& value = (*set)[size_t(spot)];
    return value ? &*value : nullptr;
}

namespace
{
    Creature* FightingBoss(Player* bot, uint32 entry)
    {
        return bot->GetMapId() == MAP_ID ? RaidClear::FightingCreature(bot, { entry }) : nullptr;
    }

    bool KeepsBack(Player* bot)
    {
        return !PlayerbotAI::IsTank(bot) && (PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot));
    }

    // Walking somewhere, or getting there with a charge. Picking a target isn't movement.
    bool Walks(Action* action)
    {
        return (dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action)) ||
               dynamic_cast<CastReachTargetSpellAction*>(action);
    }

    // Steps a held bot still takes: the raid mechanics' own (out of a breath, a bomb, a stomp;
    // all named "rc ...") and playerbots' step out of a fire on the ground.
    bool IsDodge(Action* action)
    {
        std::string const name = action->getName();
        return name.rfind("rc ", 0) == 0 || name == "avoid aoe";
    }

    // The role's hide spot: tanks, melee, ranged, healers, each falling back to the next one
    // that makes sense.
    Position const* HideSpotFor(Player* bot, uint32 boss)
    {
        if (PlayerbotAI::IsTank(bot))
        {
            Position const* spot = BossSpot(boss, Spot::TankHide);
            return spot ? spot : BossSpot(boss, Spot::MeleeHide);
        }
        if (PlayerbotAI::IsHeal(bot))
        {
            Position const* spot = BossSpot(boss, Spot::HealerHide);
            return spot ? spot : BossSpot(boss, Spot::RangedHide);
        }
        return BossSpot(boss, PlayerbotAI::IsRanged(bot) ? Spot::RangedHide : Spot::MeleeHide);
    }

    // This bot's own place round a spot, so forty of them don't stand in one: up to SPOT_SCATTER
    // off it, the same place every time (by GUID). The spot itself where that place is no good:
    // off the floor, behind a wall from the spot, or (for a hide spot) in the boss's sight.
    Position PlaceAt(Player* bot, Position const& spot, Unit const* hideFrom)
    {
        uint32 const n = bot->GetGUID().GetCounter();
        float const angle = float(n % 16) * 2.0f * float(M_PI) / 16.0f;
        float const radius = SPOT_SCATTER * float(1 + (n / 16) % 3) / 3.0f;
        float const x = spot.GetPositionX() + std::cos(angle) * radius;
        float const y = spot.GetPositionY() + std::sin(angle) * radius;
        Map* map = bot->GetMap();
        float const z = map->GetHeight(bot->GetPhaseMask(), x, y, spot.GetPositionZ() + RaidClear::WAY_OUT_UP);
        if (z <= INVALID_HEIGHT || std::fabs(z - spot.GetPositionZ()) > RaidClear::WAY_OUT_UP)
            return spot;
        if (!map->isInLineOfSight(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ() + EYE_HEIGHT, x, y,
                                  z + EYE_HEIGHT, bot->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS,
                                  VMAP::ModelIgnoreFlags::Nothing))
            return spot;
        if (hideFrom && SeesSpot(hideFrom, x, y, z, bot->GetCollisionHeight()))
            return spot;
        return Position(x, y, z);
    }

    // The nearest spot (by walking distance, at most `maxPath`) within `range` that `from` can't
    // see. Rings of candidates from close to far; the first ring with any cover wins. Only a
    // whole walk over the floor counts, judged where the walk really ends (a spot out of sight
    // because it is inside a pillar is no cover), and not one the aggro leash would stop.
    bool FindCover(PlayerbotAI* botAI, Player* bot, Unit* from, float range, float maxPath, Position& out)
    {
        Map* map = bot->GetMap();
        float const botZ = bot->GetPositionZ();
        RaidClear::Leash::WalkCheck const leash(botAI, bot);
        constexpr int ANGLES = 16;
        constexpr uint32 NOT_A_WALK = PATHFIND_INCOMPLETE | PATHFIND_NOPATH | PATHFIND_SHORTCUT |
                                      PATHFIND_NOT_USING_PATH | PATHFIND_FARFROMPOLY;

        for (float radius = 4.0f; radius <= range; radius += 3.0f)
        {
            float bestLength = 0.0f;
            bool found = false;
            for (int i = 0; i < ANGLES; ++i)
            {
                float const angle = float(i) * 2.0f * float(M_PI) / ANGLES;
                float const x = bot->GetPositionX() + std::cos(angle) * radius;
                float const y = bot->GetPositionY() + std::sin(angle) * radius;
                float const z = map->GetHeight(bot->GetPhaseMask(), x, y, botZ + RaidClear::WAY_OUT_DOWN);
                if (z <= INVALID_HEIGHT || std::fabs(z - botZ) > RaidClear::WAY_OUT_DOWN)
                    continue;
                if (SeesSpot(from, x, y, z, bot->GetCollisionHeight()))
                    continue;

                PathGenerator path(bot);
                if (!path.CalculatePath(x, y, z) || !(path.GetPathType() & PATHFIND_NORMAL) ||
                    (path.GetPathType() & NOT_A_WALK))
                    continue;
                float const length = path.getPathLength();
                if (length > maxPath || (found && length >= bestLength))
                    continue;
                G3D::Vector3 const& end = path.GetActualEndPosition();
                if (std::hypot(end.x - x, end.y - y) > RaidClear::WAY_OUT_END_SLACK ||
                    SeesSpot(from, end.x, end.y, end.z, bot->GetCollisionHeight()) ||
                    leash.Pulls(path.GetPath()))
                    continue;

                out.Relocate(end.x, end.y, end.z);
                bestLength = length;
                found = true;
            }
            if (found)
                return true;
        }
        return false;
    }

    // --- Chromaggus' breath clock -----------------------------------------------

    bool IsBreath(uint32 spellId)
    {
        switch (spellId)
        {
            case SPELL_INCINERATE:
            case SPELL_CORROSIVE_ACID:
            case SPELL_IGNITE_FLESH:
            case SPELL_FROST_BURN:
            case SPELL_TIME_LAPSE:
                return true;
            default:
                return false;
        }
    }

    // The breath Chromaggus is casting right now, or 0.
    uint32 ChromaggusBreath(Unit* boss)
    {
        Spell* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell || !spell->GetSpellInfo())
            return 0;
        uint32 const id = spell->GetSpellInfo()->Id;
        return IsBreath(id) ? id : 0;
    }

    // His breath clock. The core schedules one breath 30s into the fight and the other at 60s,
    // each then every 60s: one every 30s, taking turns. Two seconds of cast are not enough to
    // reach cover in, so the bots go by the clock: when the fight was first seen and when he
    // last breathed.
    struct BreathClock
    {
        ObjectGuid boss;
        uint32 engagedMs = 0;     // when a bot first saw him in this fight
        uint32 seenMs = 0;        // when a bot last saw him fighting
        uint32 lastBreathMs = 0;  // 0: none yet in this fight
    };
    // A fight nobody has looked at for this long is over: the next look starts a new one.
    constexpr uint32 BREATH_CLOCK_STALE_MS = 10000;

    std::mutex sBreathLock;
    std::unordered_map<uint32, BreathClock> sBreathClocks;  // by instance id

    // The clock for this boss in this fight. Call with the lock held.
    BreathClock& ClockOf(Creature* boss, uint32 now)
    {
        BreathClock& clock = sBreathClocks[boss->GetInstanceId()];
        if (clock.boss != boss->GetGUID() || getMSTimeDiff(clock.seenMs, now) > BREATH_CLOCK_STALE_MS)
        {
            clock = BreathClock();
            clock.boss = boss->GetGUID();
            clock.engagedMs = now;
        }
        clock.seenMs = now;
        return clock;
    }

    // A breath is due within `leadMs`, or overdue by less than a few seconds.
    bool BreathDue(Creature* boss, uint32 leadMs)
    {
        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(sBreathLock);
        BreathClock const& clock = ClockOf(boss, now);
        uint32 const since = getMSTimeDiff(clock.lastBreathMs ? clock.lastBreathMs : clock.engagedMs, now);
        return since + leadMs >= CHROMAGGUS_BREATH_INTERVAL_MS &&
               since <= CHROMAGGUS_BREATH_INTERVAL_MS + CHROMAGGUS_BREATH_LATE_MS;
    }

    // Time for this bot to be behind something: he is breathing, or about to. Not the bot he is
    // on: he would turn to follow it, and his tank takes the breath (Time Lapse above all).
    bool HideFromChromaggus(Player* bot, Creature* boss)
    {
        if (boss->GetVictim() == bot)
            return false;
        if (ChromaggusBreath(boss))
            return true;
        return BreathDue(boss, PlayerbotAI::IsHeal(bot) ? CHROMAGGUS_HEALER_HIDE_LEAD_MS : CHROMAGGUS_HIDE_LEAD_MS);
    }

    // --- Firemaw: who is hiding ---------------------------------------------------

    struct Hide
    {
        uint32 sinceMs = 0;   // when it set off
        uint32 unseenMs = 0;  // when it was last out of his sight; 0: not yet
    };
    struct FiremawHides
    {
        ObjectGuid boss;
        uint32 touchedMs = 0;                           // when a bot last looked at it
        std::unordered_map<ObjectGuid, Hide> hiding;
        std::unordered_map<ObjectGuid, uint32> gaveUp;  // when it last gave up
    };
    std::mutex sHideLock;
    std::unordered_map<uint32, FiremawHides> sHides;  // by instance id
    // A record nobody has looked at for this long is from an earlier pull (he keeps his GUID
    // through a wipe): the next look starts a new one.
    constexpr uint32 HIDES_STALE_MS = 10000;

    // The record for this fight. Call with the lock held.
    FiremawHides& HidesOf(uint32 instanceId, ObjectGuid boss, uint32 now)
    {
        FiremawHides& hides = sHides[instanceId];
        if (hides.boss != boss || getMSTimeDiff(hides.touchedMs, now) > HIDES_STALE_MS)
        {
            hides = FiremawHides();
            hides.boss = boss;
        }
        hides.touchedMs = now;
        return hides;
    }

    bool Listed(std::vector<ObjectGuid> const& guids, ObjectGuid guid)
    {
        return std::find(guids.begin(), guids.end(), guid) != guids.end();
    }

    void StopHiding(Player* bot)
    {
        std::lock_guard<std::mutex> guard(sHideLock);
        auto it = sHides.find(bot->GetInstanceId());
        if (it != sHides.end())
            it->second.hiding.erase(bot->GetGUID());
    }

    uint8 BuffetStacks(Player const* player)
    {
        Aura const* buffet = player->GetAura(SPELL_FLAME_BUFFET);
        return buffet ? buffet->GetStackAmount() : 0;
    }

    // Firemaw stack threshold for this bot: 5 to 7, so the raid doesn't all leave together.
    uint8 FlameBuffetThreshold(Player const* bot)
    {
        return FLAME_BUFFET_HIDE_STACKS + uint8(bot->GetGUID().GetCounter() % FLAME_BUFFET_HIDE_SPREAD);
    }

    bool InFight(Player* member, Player* bot)
    {
        return member && member->IsAlive() && member->IsInMap(bot);
    }

    // Healers take turns: at most a third of them out at once, those with the most stacks
    // first. One with HEALER_MUST_HIDE_STACKS goes anyway: it is about to die where it stands.
    bool HealerTurn(Player* bot, Creature* boss, uint8 stacks, std::vector<ObjectGuid> const& hiding,
                    std::vector<ObjectGuid> const& gaveUp)
    {
        if (stacks >= HEALER_MUST_HIDE_STACKS)
            return true;
        Group* group = bot->GetGroup();
        if (!group)
            return true;

        uint32 healers = 0;
        uint32 hidden = 0;
        uint32 ahead = 0;  // healers waiting for a turn that go before this one
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!InFight(member, bot) || !PlayerbotAI::IsHeal(member))
                continue;
            ++healers;
            if (member == bot)
                continue;
            uint8 const theirs = BuffetStacks(member);
            if (Listed(hiding, member->GetGUID()))
            {
                if (theirs)
                    ++hidden;
                continue;
            }
            // One that found nowhere to go a moment ago isn't waiting for a turn it can't use.
            if (!GET_PLAYERBOT_AI(member) || boss->GetVictim() == member || theirs < FlameBuffetThreshold(member) ||
                Listed(gaveUp, member->GetGUID()))
                continue;
            if (theirs > stacks || (theirs == stacks && member->GetGUID() < bot->GetGUID()))
                ++ahead;
        }
        uint32 const turns = std::max<uint32>(1, healers / FIREMAW_HEALER_HIDE_SHARE);
        return hidden + ahead < turns;
    }

    // Another tank that can stand in for this one while it drops its stacks.
    bool SpareTank(Player* bot, Creature* boss, std::vector<ObjectGuid> const& hiding)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!InFight(member, bot) || member == bot || boss->GetVictim() == member || !PlayerbotAI::IsTank(member) ||
                !GET_PLAYERBOT_AI(member) || BuffetStacks(member) > TANK_SPARE_STACKS || Listed(hiding, member->GetGUID()))
                continue;
            return true;
        }
        return false;
    }

    // Whether a bot that isn't hiding yet should go now.
    bool WantsToHide(Player* bot, Creature* boss, uint8 stacks, std::vector<ObjectGuid> const& hiding,
                     std::vector<ObjectGuid> const& gaveUp)
    {
        if (PlayerbotAI::IsTank(bot))
        {
            // One of his two tanks rests while another can stand in for it, or (with only two)
            // once he is on a fresh one that holds him on its own. A spare one hides like
            // everyone else.
            if (HoldsDrake(bot, boss))
            {
                if (stacks < TANK_REST_STACKS)
                    return false;
                Unit* victim = boss->GetVictim();
                Player* onTank = victim ? victim->ToPlayer() : nullptr;
                bool const tankOn = onTank && onTank != bot && PlayerbotAI::IsTank(onTank);
                if (tankOn && BuffetStacks(onTank) + TANK_SWAP_GAP <= stacks)
                    return true;
                if (SpareTank(bot, boss, hiding))
                    return true;
                // Only two tanks: both stand in his sight and stack together, so no gap ever
                // opens. The one he isn't on goes first (he has a tank meanwhile, and any tank
                // may take him off someone else after DRAKE_LOOSE_TAUNT_MS); the swap does the
                // rest once it is back.
                return tankOn && BuffetStacks(onTank) <= stacks;
            }
            return stacks >= FlameBuffetThreshold(bot);
        }
        if (stacks < FlameBuffetThreshold(bot))
            return false;
        return !PlayerbotAI::IsHeal(bot) || HealerTurn(bot, boss, stacks, hiding, gaveUp);
    }

    // Decides, for this bot, whether it hides from Firemaw now, and keeps the record. Only a bot's
    // own trigger calls it.
    bool DecideFiremawHide(Player* bot)
    {
        Creature* boss = FightingBoss(bot, NPC_FIREMAW);
        uint8 const stacks = boss ? BuffetStacks(bot) : 0;
        // Nothing to drop, or he is on this bot: whoever he is on stays where he is.
        if (!stacks || boss->GetVictim() == bot)
        {
            StopHiding(bot);
            return false;
        }

        uint32 const now = getMSTime();
        ObjectGuid const me = bot->GetGUID();
        // Asked before the lock: a sight test is all it is, but nothing else runs under it.
        bool const seen = SeenBy(boss, bot);
        std::vector<ObjectGuid> hiding;
        std::vector<ObjectGuid> gaveUp;
        {
            std::lock_guard<std::mutex> guard(sHideLock);
            FiremawHides& hides = HidesOf(bot->GetInstanceId(), boss->GetGUID(), now);
            auto mine = hides.hiding.find(me);
            if (mine != hides.hiding.end())
            {
                Hide& hide = mine->second;
                if (!seen)
                {
                    hide.unseenMs = now ? now : 1;
                    return true;
                }
                // In his sight for this long, since it set off or since it last was out of it
                // (a dodge, a turn of his): there is nowhere to go. Fight on.
                uint32 const lastTry = hide.unseenMs ? hide.unseenMs : hide.sinceMs;
                if (getMSTimeDiff(lastTry, now) >= HIDE_GIVE_UP_MS)
                {
                    hides.hiding.erase(mine);
                    hides.gaveUp[me] = now ? now : 1;
                    return false;
                }
                return true;
            }
            for (auto const& [guid, at] : hides.gaveUp)
                if (getMSTimeDiff(at, now) < HIDE_RETRY_MS)
                    gaveUp.push_back(guid);
            if (Listed(gaveUp, me))
                return false;
            hiding.reserve(hides.hiding.size());
            for (auto const& [guid, hide] : hides.hiding)
                hiding.push_back(guid);
        }

        // Looked at without the lock: it asks about other bots' hiding through it.
        if (!WantsToHide(bot, boss, stacks, hiding, gaveUp))
            return false;

        std::lock_guard<std::mutex> guard(sHideLock);
        Hide& hide = HidesOf(bot->GetInstanceId(), boss->GetGUID(), now).hiding[me];
        hide.sinceMs = now ? now : 1;
        hide.unseenMs = seen ? 0 : hide.sinceMs;
        return true;
    }

    // Hidden from Firemaw while Flame Buffet wears off, or from Chromaggus mid-breath.
    bool HoldingCover(Player* bot)
    {
        if (HidingFromFiremaw(bot))
            if (Creature* boss = FightingBoss(bot, NPC_FIREMAW))
                if (!SeenBy(boss, bot))
                    return true;

        if (Creature* boss = FightingBoss(bot, NPC_CHROMAGGUS))
            if (HideFromChromaggus(bot, boss) && !SeenBy(boss, bot))
                return true;
        return false;
    }
}

bool RaidClear::BlackwingLair::HidingFromFiremaw(Player* bot)
{
    if (!bot || !bot->IsAlive() || !bot->HasAura(SPELL_FLAME_BUFFET))
        return false;
    std::lock_guard<std::mutex> guard(sHideLock);
    auto it = sHides.find(bot->GetInstanceId());
    return it != sHides.end() && it->second.hiding.count(bot->GetGUID());
}

void RaidClear::BlackwingLair::NoteChromaggusBreath(Creature* boss, uint32 spellId)
{
    if (!boss || !IsBreath(spellId))
        return;
    uint32 const now = getMSTime();
    std::lock_guard<std::mutex> guard(sBreathLock);
    BreathClock& clock = ClockOf(boss, now);
    clock.lastBreathMs = now ? now : 1;
}

// --- Hiding ------------------------------------------------------------------------

namespace
{
    // Already walking toward there: a walk sent again every tick plans its path again every
    // tick. A path that can't reach the spot ends short of it, so the walk's end is only checked
    // to be near.
    bool OnTheWay(Player* bot, Position const& to)
    {
        if (!bot->isMoving() || bot->movespline->Finalized())
            return false;
        G3D::Vector3 const dest = bot->movespline->FinalDestination();
        return to.GetExactDist2d(dest.x, dest.y) < 3.0f;
    }
}

bool RcBwlHideAction::Execute(Event /*event*/)
{
    Unit* from = HideFrom();
    if (!from)
        return false;

    // Already out of sight: nothing to do. RcBwlHoldCoverMultiplier keeps other movement from
    // walking the bot back into view, while heals and casts at targets it can see go on.
    if (!SeenBy(from, bot))
        return false;

    float const height = bot->GetCollisionHeight();
    Position target;
    bool found = false;

    // The role's spot from the config, unless he can see it from where he stands now, or the bot
    // stands on it and he still sees it there.
    if (Position const* spot = HideSpotFor(bot, from->GetEntry()))
    {
        Position const place = PlaceAt(bot, *spot, from);
        if (bot->GetExactDist(place) > 1.0f &&
            !SeesSpot(from, place.GetPositionX(), place.GetPositionY(), place.GetPositionZ(), height))
        {
            target = place;
            found = true;
        }
    }

    if (!found)
    {
        uint64 const now = GameTime::GetGameTimeMS().count();
        bool const cached =
            _coverFrom == from->GetGUID() && now - _coverFoundMs < COVER_REUSE_MS && bot->GetExactDist(_cover) > 1.0f &&
            !SeesSpot(from, _cover.GetPositionX(), _cover.GetPositionY(), _cover.GetPositionZ(), height);
        if (!cached)
        {
            if (now < _noCoverUntilMs)
                return false;
            if (!FindCover(botAI, bot, from, _range, _maxPath, _cover) || bot->GetExactDist(_cover) <= 1.0f)
            {
                _coverFrom = ObjectGuid::Empty;
                _noCoverUntilMs = now + COVER_RETRY_MS;
                return false;  // nowhere to hide: fight on
            }
            _coverFrom = from->GetGUID();
            _coverFoundMs = now;
        }
        target = _cover;
    }

    // On its way: nothing else this tick, so no cast stops the run. A walk that ends short of
    // the spot (the path can't reach it) isn't sent again for a moment either.
    if (OnTheWay(bot, target))
        return true;
    uint64 const sentNow = GameTime::GetGameTimeMS().count();
    if (_sentAtMs && sentNow - _sentAtMs < HIDE_RESEND_MS && _sentTo.GetExactDist(target) < 1.0f)
        return false;
    AI_VALUE(LastMovement&, "last movement").clear();
    if (!MoveTo(bot->GetMapId(), target.GetPositionX(), target.GetPositionY(), target.GetPositionZ(), false, false,
                false, false, MovementPriority::MOVEMENT_FORCED))
        return false;
    _sentTo = target;
    _sentAtMs = sentNow;
    return true;
}

float RcBwlHoldCoverMultiplier::GetValue(Action* action)
{
    if (!action || dynamic_cast<RcBwlHideAction*>(action) || !Walks(action) || IsDodge(action))
        return 1.0f;
    return HoldingCover(bot) ? 0.0f : 1.0f;
}

bool RcBwlFiremawHideTrigger::IsActive()
{
    return bot->IsAlive() && DecideFiremawHide(bot);
}

Unit* RcBwlFiremawHideAction::HideFrom()
{
    Creature* boss = FightingBoss(bot, NPC_FIREMAW);
    return boss && HidingFromFiremaw(bot) ? boss : nullptr;
}

bool RcBwlChromaggusBreathTrigger::IsActive()
{
    if (!bot->IsAlive())
        return false;
    Creature* boss = FightingBoss(bot, NPC_CHROMAGGUS);
    return boss && HideFromChromaggus(bot, boss);
}

Unit* RcBwlChromaggusHideAction::HideFrom()
{
    Creature* boss = FightingBoss(bot, NPC_CHROMAGGUS);
    return boss && HideFromChromaggus(bot, boss) ? boss : nullptr;
}

// --- Fixed spots ---------------------------------------------------------------------

namespace
{
    // The boss with fixed spots this bot is fighting, if any.
    Creature* SpotBoss(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID)
            return nullptr;
        for (uint32 const entry : { NPC_FIREMAW, NPC_CHROMAGGUS })
            if (Creature* boss = FightingBoss(bot, entry))
                return boss;
        return nullptr;
    }

    // Spell and heal reach, with a margin, between two players' middles (heals) and from a
    // caster's middle to the edge of a boss (damage).
    constexpr float SPOT_HEAL_REACH = 38.0f;
    constexpr float SPOT_SPELL_REACH = 33.0f;

    // Whether a caster or healer standing there can do its job: a healer reach and see whoever
    // the boss is on, a damage dealer reach and see the boss. A spot it can't is no spot: the
    // bot is left to playerbots' own positioning until it can again.
    bool CanWorkFrom(Player* bot, Creature* boss, Position const& place)
    {
        float const eyeZ = place.GetPositionZ() + bot->GetCollisionHeight();
        if (PlayerbotAI::IsHeal(bot))
        {
            Unit* victim = boss->GetVictim();
            if (!victim)
                return true;
            return victim->GetExactDist(place) <= SPOT_HEAL_REACH &&
                   bot->GetMap()->isInLineOfSight(place.GetPositionX(), place.GetPositionY(), eyeZ,
                                                  victim->GetPositionX(), victim->GetPositionY(),
                                                  victim->GetPositionZ() + victim->GetCollisionHeight(),
                                                  bot->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS,
                                                  VMAP::ModelIgnoreFlags::M2);
        }
        return boss->GetExactDist2d(place.GetPositionX(), place.GetPositionY()) - boss->GetCombatReach() <=
                   SPOT_SPELL_REACH &&
               SeesSpot(boss, place.GetPositionX(), place.GetPositionY(), place.GetPositionZ(),
                        bot->GetCollisionHeight());
    }

    // A caster's or healer's spot dropped for being out of reach is kept dropped for this long,
    // so a tank at the edge of heal range doesn't flip the bot between its spot and free
    // movement every tick. By bot, for all instances (a bot is in one at a time).
    constexpr uint32 SPOT_DROP_MS = 3000;
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
        bool still = false;  // stays where it is for now (he is breathing): held, not walked
    };

    // Firemaw's two tank spots, for one of his tanks: the one he is on keeps to whichever of the
    // two it stands at (a swap doesn't drag him across), else the tank spot; the other takes the
    // one the tank he is on doesn't.
    Position const* VictimSpot(Unit const* victim, Position const* main, Position const* side)
    {
        if (main && side && victim->GetExactDist(*side) < victim->GetExactDist(*main) &&
            victim->GetExactDist(*side) <= SPOT_HOLD)
            return side;
        return main ? main : side;
    }

    Position const* FiremawTankSpot(Player* bot, Creature* boss)
    {
        Position const* main = BossSpot(NPC_FIREMAW, Spot::Tank);
        Position const* side = BossSpot(NPC_FIREMAW, Spot::OffTank);
        Unit* victim = boss->GetVictim();
        if (victim == bot)
            return VictimSpot(bot, main, side);
        if (!HoldsDrake(bot, boss))
            return nullptr;
        if (!victim)
            return side;
        Position const* theirs = VictimSpot(victim, main, side);
        return theirs == main ? side : main;
    }

    // Where this bot belongs in the fight, if its role has a spot and it isn't hiding: the tank
    // the boss is on at the tank spot, Firemaw's other tank at the other one, ranged and
    // healers round theirs. Melee and spare tanks have none: they go where the fight is.
    bool WantedSpot(Player* bot, SpotWant& want)
    {
        Creature* boss = SpotBoss(bot);
        if (!boss || !bot->IsAlive())
            return false;
        uint32 const entry = boss->GetEntry();
        if (entry == NPC_FIREMAW ? HidingFromFiremaw(bot) : HideFromChromaggus(bot, boss))
            return false;

        Position const* spot = nullptr;
        want.tank = PlayerbotAI::IsTank(bot);
        if (want.tank)
        {
            if (entry == NPC_FIREMAW)
                spot = FiremawTankSpot(bot, boss);
            else if (boss->GetVictim() == bot)
                spot = BossSpot(entry, Spot::Tank);
            if (!spot)
                return false;
            want.place = *spot;
            // He turns with whoever he is on, and a breath or buffet lands where he faces when
            // the cast ends.
            want.still = boss->GetVictim() == bot && DragonBreathing(boss);
            return true;
        }

        if (PlayerbotAI::IsHeal(bot))
        {
            spot = BossSpot(entry, Spot::Healer);
            if (!spot)
                spot = BossSpot(entry, Spot::Ranged);
        }
        else if (KeepsBack(bot))
            spot = BossSpot(entry, Spot::Ranged);
        if (!spot)
            return false;

        uint32 const now = getMSTime();
        if (SpotDropped(bot, now))
            return false;
        want.place = PlaceAt(bot, *spot, nullptr);
        // Not a place in Firemaw's front (where that is moves with his tank), and not one it
        // can't heal or hit from.
        if ((entry == NPC_FIREMAW && InDragonsWay(bot, boss, want.place.GetPositionX(), want.place.GetPositionY())) ||
            !CanWorkFrom(bot, boss, want.place))
        {
            DropSpot(bot, now);
            return false;
        }
        return true;
    }

    float SpotNear(SpotWant const& want)
    {
        return want.tank ? SPOT_TANK_NEAR : SPOT_NEAR;
    }
}

bool RcBwlBossSpotTrigger::IsActive()
{
    if (!botAI->CanMove())
        return false;
    SpotWant want;
    return WantedSpot(bot, want) && !want.still && bot->GetExactDist(want.place) > SpotNear(want);
}

bool RcBwlBossSpotAction::Execute(Event /*event*/)
{
    SpotWant want;
    if (!WantedSpot(bot, want) || want.still || bot->GetExactDist(want.place) <= SpotNear(want))
        return false;
    // On its way: a tank holds the tick (nothing else should walk it), a caster or healer lets
    // its heals and instants go out meanwhile.
    if (OnTheWay(bot, want.place))
        return want.tank;
    if (want.tank)
        AI_VALUE(LastMovement&, "last movement").clear();
    return MoveTo(bot->GetMapId(), want.place.GetPositionX(), want.place.GetPositionY(), want.place.GetPositionZ(),
                  false, false, false, false,
                  want.tank ? MovementPriority::MOVEMENT_FORCED : MovementPriority::MOVEMENT_COMBAT);
}

float RcBwlSpotHoldMultiplier::GetValue(Action* action)
{
    if (!action || dynamic_cast<RcBwlBossSpotAction*>(action) || !Walks(action))
        return 1.0f;

    uint32 const now = getMSTime();
    if (!_checkedAtMs || getMSTimeDiff(_checkedAtMs, now) >= 250)
    {
        _checkedAtMs = now ? now : 1;
        SpotWant want;
        _holds = WantedSpot(bot, want) && bot->GetExactDist(want.place) <= SPOT_HOLD;
    }
    if (!_holds)
        return 1.0f;

    // A tank on its spot has its back to the wall: the knockback footing would walk it off, and
    // Firemaw's second tank doesn't go stand by the other.
    std::string const name = action->getName();
    if (name == "rc knockback footing" || name == "rc bwl drake front")
        return 0.0f;
    return IsDodge(action) ? 1.0f : 0.0f;
}
