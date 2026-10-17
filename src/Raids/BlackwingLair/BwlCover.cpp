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
#include <mutex>
#include <optional>
#include <regex>
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
}

bool RaidClear::BlackwingLair::SeenBy(Unit const* boss, WorldObject const* who)
{
    // What Spell::CheckEffectTarget asks: M2 models (small props) don't block a spell.
    return boss && who && boss->IsWithinLOSInMap(who, VMAP::ModelIgnoreFlags::M2);
}

bool RaidClear::BlackwingLair::SeesSpot(Unit const* boss, float x, float y, float z)
{
    if (!boss || !boss->IsInWorld())
        return false;
    // WorldObject::IsWithinLOS(x, y, z) raises the spot by the asker's own height, a dragon's:
    // that looks over a wall a player hides behind. From the boss's eyes to a player's.
    float const eyeZ = z + EYE_HEIGHT;
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

    // "x y z", or whatever .gps printed ("X: -7486.3 Y: -1030.1 Z: 449.3 Orientation: ..."): the
    // first three numbers.
    std::optional<Position> ParseSpot(std::string const& key, std::string const& text)
    {
        if (text.empty())
            return std::nullopt;
        static std::regex const number(R"(-?\d+(?:\.\d+)?)");
        std::vector<float> values;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), number); it != std::sregex_iterator() && values.size() < 3;
             ++it)
            values.push_back(std::stof(it->str()));
        if (values.size() < 3)
        {
            LOG_ERROR("module", "mod-raid-clear: {} = \"{}\" isn't \"x y z\"; the spot is ignored.", key, text);
            return std::nullopt;
        }
        return Position(values[0], values[1], values[2]);
    }

    void LoadSet(SpotSet& set, std::string const& boss)
    {
        for (size_t i = 0; i < set.size(); ++i)
        {
            std::string const key = "RaidClear.BlackwingLair." + boss + "." + SPOT_KEYS[i];
            set[i] = ParseSpot(key, sConfigMgr->GetOption<std::string>(key, "", false));
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
        if (hideFrom && SeesSpot(hideFrom, x, y, z))
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
                if (SeesSpot(from, x, y, z))
                    continue;

                PathGenerator path(bot);
                if (!path.CalculatePath(x, y, z) || !(path.GetPathType() & PATHFIND_NORMAL) ||
                    (path.GetPathType() & NOT_A_WALK))
                    continue;
                float const length = path.getPathLength();
                if (length > maxPath || (found && length >= bestLength))
                    continue;
                G3D::Vector3 const& end = path.GetActualEndPosition();
                if (std::hypot(end.x - x, end.y - y) > RaidClear::WAY_OUT_END_SLACK || SeesSpot(from, end.x, end.y, end.z) ||
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

    struct FiremawHides
    {
        ObjectGuid boss;
        std::unordered_map<ObjectGuid, uint32> hiding;  // since when
        std::unordered_map<ObjectGuid, uint32> gaveUp;  // when it last gave up
    };
    std::mutex sHideLock;
    std::unordered_map<uint32, FiremawHides> sHides;  // by instance id

    // The record for this fight. Call with the lock held.
    FiremawHides& HidesOf(uint32 instanceId, ObjectGuid boss)
    {
        FiremawHides& hides = sHides[instanceId];
        if (hides.boss != boss)
        {
            hides = FiremawHides();
            hides.boss = boss;
        }
        return hides;
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
    bool HealerTurn(Player* bot, Creature* boss, uint8 stacks, std::vector<ObjectGuid> const& hiding)
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
            if (std::find(hiding.begin(), hiding.end(), member->GetGUID()) != hiding.end())
            {
                if (theirs)
                    ++hidden;
                continue;
            }
            if (!GET_PLAYERBOT_AI(member) || boss->GetVictim() == member || theirs < FlameBuffetThreshold(member))
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
                !GET_PLAYERBOT_AI(member) || BuffetStacks(member) > TANK_SPARE_STACKS ||
                std::find(hiding.begin(), hiding.end(), member->GetGUID()) != hiding.end())
                continue;
            return true;
        }
        return false;
    }

    // Whether a bot that isn't hiding yet should go now.
    bool WantsToHide(Player* bot, Creature* boss, uint8 stacks, std::vector<ObjectGuid> const& hiding)
    {
        if (PlayerbotAI::IsTank(bot))
        {
            // One of his two tanks rests while another can stand in for it; a spare one hides
            // like everyone else.
            if (HoldsDrake(bot, boss))
                return stacks >= TANK_REST_STACKS && SpareTank(bot, boss, hiding);
            return stacks >= FlameBuffetThreshold(bot);
        }
        if (stacks < FlameBuffetThreshold(bot))
            return false;
        return !PlayerbotAI::IsHeal(bot) || HealerTurn(bot, boss, stacks, hiding);
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
        std::vector<ObjectGuid> hiding;
        {
            std::lock_guard<std::mutex> guard(sHideLock);
            FiremawHides& hides = HidesOf(bot->GetInstanceId(), boss->GetGUID());
            auto mine = hides.hiding.find(me);
            if (mine != hides.hiding.end())
            {
                // Gone this long and still in his sight: there is nowhere to go. Fight on.
                if (getMSTimeDiff(mine->second, now) >= HIDE_GIVE_UP_MS && SeenBy(boss, bot))
                {
                    hides.hiding.erase(mine);
                    hides.gaveUp[me] = now ? now : 1;
                    return false;
                }
                return true;
            }
            auto gaveUp = hides.gaveUp.find(me);
            if (gaveUp != hides.gaveUp.end() && getMSTimeDiff(gaveUp->second, now) < HIDE_RETRY_MS)
                return false;
            hiding.reserve(hides.hiding.size());
            for (auto const& [guid, since] : hides.hiding)
                hiding.push_back(guid);
        }

        // Looked at without the lock: it asks about other bots' hiding through it.
        if (!WantsToHide(bot, boss, stacks, hiding))
            return false;

        std::lock_guard<std::mutex> guard(sHideLock);
        HidesOf(bot->GetInstanceId(), boss->GetGUID()).hiding[me] = now ? now : 1;
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

bool RcBwlHideAction::Execute(Event /*event*/)
{
    Unit* from = HideFrom();
    if (!from)
        return false;

    // Already out of sight: nothing to do. RcBwlHoldCoverMultiplier keeps other movement from
    // walking the bot back into view, while heals and casts at targets it can see go on.
    if (!SeenBy(from, bot))
        return false;

    // The role's spot from the config, unless he can see it from where he stands now.
    if (Position const* spot = HideSpotFor(bot, from->GetEntry()))
    {
        Position const place = PlaceAt(bot, *spot, from);
        if (!SeesSpot(from, place.GetPositionX(), place.GetPositionY(), place.GetPositionZ()))
        {
            AI_VALUE(LastMovement&, "last movement").clear();
            return MoveTo(bot->GetMapId(), place.GetPositionX(), place.GetPositionY(), place.GetPositionZ(), false,
                          false, false, false, MovementPriority::MOVEMENT_FORCED);
        }
    }

    uint64 const now = GameTime::GetGameTimeMS().count();
    bool const cached = _coverFrom == from->GetGUID() && now - _coverFoundMs < COVER_REUSE_MS &&
                        !SeesSpot(from, _cover.GetPositionX(), _cover.GetPositionY(), _cover.GetPositionZ());
    if (!cached)
    {
        if (now < _noCoverUntilMs)
            return false;
        if (!FindCover(botAI, bot, from, _range, _maxPath, _cover))
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

    // Where this bot belongs in the fight, if its role has a spot and it isn't hiding: the tank
    // the boss is on at the tank spot, Firemaw's other tank at the off-tank spot, ranged and
    // healers round theirs. Melee and spare tanks have none: they go where the fight is.
    bool WantedSpot(Player* bot, Position& out, bool& tank)
    {
        Creature* boss = SpotBoss(bot);
        if (!boss || !bot->IsAlive())
            return false;
        uint32 const entry = boss->GetEntry();
        if (entry == NPC_FIREMAW ? HidingFromFiremaw(bot) : HideFromChromaggus(bot, boss))
            return false;

        Position const* spot = nullptr;
        tank = PlayerbotAI::IsTank(bot);
        if (tank)
        {
            if (boss->GetVictim() == bot)
                spot = BossSpot(entry, Spot::Tank);
            else if (entry == NPC_FIREMAW && HoldsDrake(bot, boss))
                spot = BossSpot(entry, Spot::OffTank);
        }
        else if (PlayerbotAI::IsHeal(bot))
        {
            spot = BossSpot(entry, Spot::Healer);
            if (!spot)
                spot = BossSpot(entry, Spot::Ranged);
        }
        else if (KeepsBack(bot))
            spot = BossSpot(entry, Spot::Ranged);
        if (!spot)
            return false;

        out = tank ? *spot : PlaceAt(bot, *spot, nullptr);
        return true;
    }
}

bool RcBwlBossSpotTrigger::IsActive()
{
    if (!botAI->CanMove())
        return false;
    Position spot;
    bool tank = false;
    return WantedSpot(bot, spot, tank) && bot->GetExactDist(spot) > (tank ? SPOT_TANK_NEAR : SPOT_NEAR);
}

bool RcBwlBossSpotAction::Execute(Event /*event*/)
{
    Position spot;
    bool tank = false;
    if (!WantedSpot(bot, spot, tank) || bot->GetExactDist(spot) <= (tank ? SPOT_TANK_NEAR : SPOT_NEAR))
        return false;
    // Already on the way.
    if (bot->isMoving() && !bot->movespline->Finalized())
    {
        G3D::Vector3 const dest = bot->movespline->FinalDestination();
        if (spot.GetExactDist(dest.x, dest.y, dest.z) < 1.0f)
            return true;
    }
    if (tank)
        AI_VALUE(LastMovement&, "last movement").clear();
    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false, false,
                  false, tank ? MovementPriority::MOVEMENT_FORCED : MovementPriority::MOVEMENT_COMBAT);
}

float RcBwlSpotHoldMultiplier::GetValue(Action* action)
{
    if (!action || dynamic_cast<RcBwlBossSpotAction*>(action) || !Walks(action))
        return 1.0f;

    uint32 const now = getMSTime();
    if (!_checkedAtMs || getMSTimeDiff(_checkedAtMs, now) >= 250)
    {
        _checkedAtMs = now ? now : 1;
        Position spot;
        bool tank = false;
        _holds = WantedSpot(bot, spot, tank) && bot->GetExactDist(spot) <= SPOT_HOLD;
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
