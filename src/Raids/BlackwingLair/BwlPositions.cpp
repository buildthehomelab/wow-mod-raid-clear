/*
 * mod-raid-clear: Blackwing Lair, where a bot stands: the dragons' breath, Nefarian's roar and
 * tail and who tanks him, who holds the drakes, and the Wyrmguards' War Stomp. See
 * BwlRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "BwlRaidClear.h"

#include "RaidClearConfig.h"
#include "Common/BossLookup.h"
#include "Common/TankRoles.h"

#include "AllSpellScript.h"
#include "AttackAction.h"
#include "Creature.h"
#include "FollowActions.h"
#include "Formations.h"
#include "GenericSpellActions.h"
#include "Group.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
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

    // --- Nefarian: Tremor Totem for his tanks ---------------------------------

    // The tank whose Tremor Totem this shaman keeps down. Tremor only frees its shaman's own
    // party, so a tank in the shaman's party: the one that should have Nefarian first (the same
    // order as NefarianTank). One shaman per party, the first in group order that knows it;
    // nullptr for the others, and in a party without a tank.
    Player* TremorTankOf(Player* bot)
    {
        if (!bot->IsClass(CLASS_SHAMAN) || PlayerbotAI::IsTank(bot) || !bot->HasSpell(SPELL_TREMOR_TOTEM))
            return nullptr;
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        uint8 const party = bot->GetSubGroup();
        bool shamanSeen = false;
        bool mine = false;
        Player* tank = nullptr;
        int tankRank = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || !member->IsInMap(bot) || member->GetSubGroup() != party)
                continue;
            if (!shamanSeen && member->IsClass(CLASS_SHAMAN) && !PlayerbotAI::IsTank(member) &&
                member->HasSpell(SPELL_TREMOR_TOTEM) && GET_PLAYERBOT_AI(member))
            {
                shamanSeen = true;
                mine = member == bot;
            }
            if (!PlayerbotAI::IsTank(member))
                continue;
            int const rank = NefarianTankRank(member);
            if (!tank || rank < tankRank)
            {
                tank = member;
                tankRank = rank;
            }
        }
        return mine ? tank : nullptr;
    }

    // The shaman's own Tremor Totem, while it stands.
    Creature* TremorTotemOf(Player* shaman)
    {
        ObjectGuid const guid = shaman->m_SummonSlot[SUMMON_SLOT_TOTEM_EARTH];
        Creature* totem = guid.IsEmpty() ? nullptr : shaman->GetMap()->GetCreature(guid);
        return totem && totem->IsAlive() && totem->GetUInt32Value(UNIT_CREATED_BY_SPELL) == SPELL_TREMOR_TOTEM
                   ? totem
                   : nullptr;
    }

    // The tank this shaman has to put Tremor Totem down for now: its tank has none of its
    // totems in reach. nullptr while the one it put down still covers the tank.
    Player* TremorToPlace(Player* bot, Creature* nefarian)
    {
        if (!nefarian || !bot->IsClass(CLASS_SHAMAN))
            return nullptr;
        Player* tank = TremorTankOf(bot);
        if (!tank)
            return nullptr;
        Creature* totem = TremorTotemOf(bot);
        return totem && totem->GetExactDist(tank) <= TREMOR_TANK_REACH ? nullptr : tank;
    }

    // --- The dragons: breath, tail, roar ----------------------------------

    constexpr float DRAGON_NEAR = 90.0f;

    // Firemaw, Ebonroc or Flamegor fighting near the bot, if any.
    Creature* FightingDrake(Player* bot)
    {
        if (bot->GetMapId() != MAP_ID)
            return nullptr;
        for (uint32 const entry : { NPC_FIREMAW, NPC_EBONROC, NPC_FLAMEGOR })
            if (Creature* boss = RaidClear::FightingCreature(bot, { entry }))
                return bot->GetExactDist(boss) <= DRAGON_NEAR ? boss : nullptr;
        return nullptr;
    }

    bool IsDrake(Creature const* boss)
    {
        uint32 const entry = boss->GetEntry();
        return entry == NPC_FIREMAW || entry == NPC_EBONROC || entry == NPC_FLAMEGOR;
    }

    // A mob other than the drake is on this tank: an add it holds.
    bool HoldsAnAdd(Player* tank, Unit const* drake)
    {
        for (Unit* attacker : tank->getAttackers())
            if (attacker != drake && attacker->IsAlive() && attacker->GetVictim() == tank)
                return true;
        return false;
    }

    // A tank that can be one of the drake's two: alive, here, and not off dropping Flame Buffet
    // stacks. Only bots, past the main tank: who a real player tanks with is theirs to settle.
    bool MayHoldDrake(Player* member, Creature* drake, bool botOnly)
    {
        return member && member->IsAlive() && member->IsInMap(drake) && PlayerbotAI::IsTank(member) &&
               (!botOnly || GET_PLAYERBOT_AI(member)) && !HidingFromFiremaw(member);
    }

    // Per instance: the drake's second tank, kept for the fight so who may taunt him and whose
    // threat the others stay under don't change from one tick to the next as adds come and go,
    // and since when the drake has been loose on someone who isn't a tank.
    struct DrakeState
    {
        ObjectGuid drake;
        uint32 touchedMs = 0;    // when a bot last looked at it
        ObjectGuid second;
        uint32 secondAddMs = 0;  // since when the second has held an add; 0: it holds none
        uint32 looseMs = 0;      // since when he has been on someone who isn't a tank; 0: he isn't
    };
    std::mutex sDrakeLock;
    std::unordered_map<uint32, DrakeState> sDrakes;  // by instance id

    // A record nobody has looked at for this long is from an earlier pull (a drake keeps his
    // GUID through a wipe): the next look starts a new one.
    constexpr uint32 DRAKE_STATE_STALE_MS = 10000;

    // The record for this drake. Call with the lock held.
    DrakeState& DrakeStateOf(Creature* drake)
    {
        uint32 const now = getMSTime();
        DrakeState& state = sDrakes[drake->GetInstanceId()];
        if (state.drake != drake->GetGUID() || getMSTimeDiff(state.touchedMs, now) > DRAKE_STATE_STALE_MS)
        {
            state = DrakeState();
            state.drake = drake->GetGUID();
        }
        state.touchedMs = now;
        return state;
    }

    uint8 FlameBuffetOn(Unit const* unit)
    {
        Aura const* buffet = unit ? unit->GetAura(SPELL_FLAME_BUFFET) : nullptr;
        return buffet ? buffet->GetStackAmount() : 0;
    }

    // Keeps the loose clock: started when he is first seen on someone who isn't a tank, cleared
    // when he is seen on one. Call with the lock held.
    uint32 NoteLoose(DrakeState& state, Creature* drake, uint32 now)
    {
        Unit* victim = drake->GetVictim();
        Player* player = victim ? victim->ToPlayer() : nullptr;
        if (player && PlayerbotAI::IsTank(player))
        {
            state.looseMs = 0;
            return 0;
        }
        if (!state.looseMs)
            state.looseMs = now ? now : 1;
        return getMSTimeDiff(state.looseMs, now);
    }

    struct DrakeHolders
    {
        Player* first = nullptr;   // the tank he is on, else the main tank
        Player* second = nullptr;  // the one that takes him next
    };

    // The drake's two tanks. The first is the tank he is on (a swap or a Wing Buffet hands him to
    // the other), else the main tank. The second is the main tank when he is on someone else,
    // else the one kept for the fight; a new one (the first free bot tank in group order) when
    // that one is gone, off dropping stacks, or has held an add for DRAKE_SECOND_ADD_MS.
    DrakeHolders HoldersOf(Group* group, Creature* drake)
    {
        uint32 const now = getMSTime();
        {
            std::lock_guard<std::mutex> guard(sDrakeLock);
            NoteLoose(DrakeStateOf(drake), drake, now);
        }

        DrakeHolders holders;
        Player* mainTank = RaidClear::Tanks::ActingMainTank(group);
        Unit* victim = drake->GetVictim();
        Player* onTank = victim ? victim->ToPlayer() : nullptr;
        if (onTank && PlayerbotAI::IsTank(onTank) && onTank->GetGroup() == group && onTank->IsInMap(drake))
            holders.first = onTank;
        else if (MayHoldDrake(mainTank, drake, false))
            holders.first = mainTank;

        if (mainTank && mainTank != holders.first && MayHoldDrake(mainTank, drake, false))
        {
            holders.second = mainTank;
            return holders;
        }

        ObjectGuid kept;
        uint32 keptAddMs = 0;
        {
            std::lock_guard<std::mutex> guard(sDrakeLock);
            DrakeState const& state = DrakeStateOf(drake);
            kept = state.second;
            keptAddMs = state.secondAddMs;
        }

        // Looked up without the lock: MayHoldDrake asks about hiding through a lock of its own.
        Player* second = kept ? ObjectAccessor::GetPlayer(*drake, kept) : nullptr;
        if (second && (second == holders.first || !MayHoldDrake(second, drake, true)))
            second = nullptr;
        bool const keptHoldsAdd = second && HoldsAnAdd(second, drake);
        if (second && keptHoldsAdd && keptAddMs && getMSTimeDiff(keptAddMs, now) >= DRAKE_SECOND_ADD_MS)
            second = nullptr;

        // A new one: the free bot tank with the fewest Flame Buffet stacks (on Ebonroc and
        // Flamegor, the first in group order).
        bool picked = false;
        if (!second)
        {
            uint8 fewest = 0;
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || member == holders.first || member == mainTank || !MayHoldDrake(member, drake, true) ||
                    HoldsAnAdd(member, drake))
                    continue;
                uint8 const stacks = FlameBuffetOn(member);
                if (!second || stacks < fewest)
                {
                    second = member;
                    fewest = stacks;
                    picked = true;
                }
            }
        }

        {
            std::lock_guard<std::mutex> guard(sDrakeLock);
            DrakeState& state = DrakeStateOf(drake);
            if (picked)
            {
                state.second = second->GetGUID();
                state.secondAddMs = 0;
            }
            else if (second && keptHoldsAdd && !state.secondAddMs)
                state.secondAddMs = now ? now : 1;
            else if (second && !keptHoldsAdd)
                state.secondAddMs = 0;
        }
        holders.second = second;
        return holders;
    }

    // How long the drake has been on someone who isn't a tank (or no one); 0 while he is on a
    // tank. Kept per instance, so every tank asking sees the same.
    uint32 LooseForMs(Creature* drake)
    {
        std::lock_guard<std::mutex> guard(sDrakeLock);
        return NoteLoose(DrakeStateOf(drake), drake, getMSTime());
    }

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
        return HoldsDrake(bot, boss);
    }

    // What a bot keeps out of around one dragon, as seen from that bot.
    struct Zone
    {
        float x = 0.0f;
        float y = 0.0f;
        float facing = 0.0f;
        float frontHalf = BREATH_KEEP_HALF_ANGLE;  // how far off his facing the front is kept clear
        float frontRange = BREATH_RANGE;           // ...and how far out
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

        float gap = ConeGap(r, off, zone.frontHalf, zone.frontRange);
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
    // standing round the tank it is made on are clear of his front and tail too, outside his
    // roar (a feared pile runs into his front), and near enough to whoever he is on for the
    // healers in it: where the healers stand anyway.
    Zone AnchorZone(Creature* boss)
    {
        Zone zone = PlaceOf(boss);
        zone.frontHalf = CONSTRUCT_ANCHOR_FRONT;
        // Past the breath's reach is no place for it either: the bots round the tank would be in it.
        zone.frontRange = 1000.0f;
        zone.tailHalf = CONSTRUCT_ANCHOR_TAIL;
        zone.roar = true;
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
        // A shaman putting Tremor Totem down by its tank goes in under the roar to do it.
        if (zone.roar && TremorToPlace(bot, boss))
        {
            zone.roar = false;
            zone.reach = 0.0f;
        }
        if (boss->GetHealthPct() > CONSTRUCT_PHASE_PCT)
            return zone;

        // The tank the Bone Constructs' pile is made on keeps to the pile's place. The pile is
        // outside the roar, so everyone else keeps the usual rules on the way to it.
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && ConstructAnchor(botAI, bot, boss) == bot)
            return AnchorZone(boss);
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
        return SeenBy(boss, bot);
    }

    // A bot on its way to that cover isn't sent to his flank first. Only one that is really
    // going: one that has the stacks but not its turn yet keeps out of his front like everyone.
    bool GoingForCover(Player* bot, Creature* boss)
    {
        return boss->GetEntry() == NPC_FIREMAW && HidingFromFiremaw(bot);
    }

    // The breath or buffet the dragon is casting right now, or 0.
    uint32 ConeCast(Creature* boss)
    {
        Spell* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        uint32 const id = spell && spell->GetSpellInfo() ? spell->GetSpellInfo()->Id : 0;
        return id == SPELL_SHADOW_FLAME || id == SPELL_WING_BUFFET ? id : 0;
    }

    // --- The drakes: who taunts them -----------------------------------------

    // Playerbots' taunts (by action name), and ours.
    constexpr std::array<char const*, 10> DRAKE_TAUNTS = {
        "taunt", "growl", "hand of reckoning", "dark command", "death grip", "mocking blow",
        "righteous defense", "challenging shout", "challenging roar", "rc bwl ebonroc taunt",
    };

    bool IsTaunt(std::string const& name)
    {
        return std::find(DRAKE_TAUNTS.begin(), DRAKE_TAUNTS.end(), name) != DRAKE_TAUNTS.end();
    }

    // Whether this taunt would take the drake. Righteous Defense is cast on the friend it is
    // hitting; the shouts take everything round the tank (10 yd).
    bool TauntsDrake(Player* bot, Action* action, std::string const& name, Creature* drake)
    {
        if (name == "rc bwl ebonroc taunt")
            return drake->GetEntry() == NPC_EBONROC;
        if (name == "challenging shout" || name == "challenging roar")
            return bot->IsWithinCombatRange(drake, 10.0f);
        Unit* target = action->GetTarget();
        if (!target)
            return false;
        return target == drake || (name == "righteous defense" && drake->GetVictim() == target);
    }

    // From here a taunt doesn't turn the drake: in front of him, or next to whoever he is on.
    bool TauntsFromFront(Player* bot, Creature* drake)
    {
        Unit* victim = drake->GetVictim();
        if (victim && bot->GetExactDist(victim) <= DRAKE_TAUNT_NEAR)
            return true;
        return OffFacing(PlaceOf(drake), bot->GetPositionX(), bot->GetPositionY()) <= DRAKE_TAUNT_ARC;
    }

    // The tank the second one stands next to: a tank the drake is on, unless that is this bot.
    Player* DrakeVictimToJoin(Player* bot, Creature* drake)
    {
        Unit* victim = drake->GetVictim();
        Player* player = victim ? victim->ToPlayer() : nullptr;
        if (!player || player == bot || !player->IsAlive() || !PlayerbotAI::IsTank(player))
            return nullptr;
        return player;
    }

    bool AwayFromFront(Player* bot, Creature* drake, Player* victim)
    {
        return bot->GetExactDist(victim) > DRAKE_SECOND_NEAR ||
               OffFacing(PlaceOf(drake), bot->GetPositionX(), bot->GetPositionY()) > DRAKE_FRONT_ARC;
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

    // The Wyrmguards fighting the group, read from the instance's spawns: a bot still waiting at
    // the camp has none of them among its "attackers" yet, and that is when it has to get out of
    // the way. One answer per instance for WYRMGUARD_CACHE_MS, kept by GUID, and the same
    // wherever the bot that looked stands.
    struct WyrmguardCache
    {
        uint32 ms = 0;
        GuidVector guards;
    };
    std::mutex sWyrmguardLock;
    std::unordered_map<uint32, WyrmguardCache> sWyrmguards;  // by instance id

    bool FightsGroup(Unit const* unit, Group const* group)
    {
        Unit const* victim = unit->GetVictim();
        Player const* player = victim ? victim->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        return player && player->GetGroup() == group;
    }

    void FightingWyrmguards(Player* bot, std::vector<Creature*>& out)
    {
        Group* group = bot->GetGroup();
        if (!group || bot->GetMapId() != MAP_ID)
            return;

        uint32 const now = getMSTime();
        GuidVector guids;
        bool cached = false;
        {
            std::lock_guard<std::mutex> guard(sWyrmguardLock);
            auto it = sWyrmguards.find(bot->GetInstanceId());
            if (it != sWyrmguards.end() && getMSTimeDiff(it->second.ms, now) < WYRMGUARD_CACHE_MS)
            {
                guids = it->second.guards;
                cached = true;
            }
        }
        if (!cached)
        {
            for (auto const& [spawnId, creature] : bot->GetMap()->GetCreatureBySpawnIdStore())
                if (creature && creature->GetEntry() == NPC_DEATH_TALON_WYRMGUARD && creature->IsAlive() &&
                    creature->IsInCombat() && FightsGroup(creature, group))
                    guids.push_back(creature->GetGUID());
            std::lock_guard<std::mutex> guard(sWyrmguardLock);
            WyrmguardCache& cache = sWyrmguards[bot->GetInstanceId()];
            cache.ms = now;
            cache.guards = guids;
        }
        for (ObjectGuid const& guid : guids)
            if (Creature* creature = ObjectAccessor::GetCreature(*bot, guid))
                if (creature->IsAlive() && creature->IsInCombat() && bot->GetExactDist(creature) <= WYRMGUARD_SEARCH)
                    out.push_back(creature);
    }

    // Every spot whose War Stomp this bot has no reason to stand in: each Wyrmguard, and while
    // one is still running in, whoever it is running to (the tank that pulled the pack, which is
    // where the raid waits). A melee bot may stand by the one it is hitting once no other is
    // within WYRMGUARD_APART of that one: a single stomp is something to heal, two at once are
    // a death.
    void WyrmguardStomps(PlayerbotAI* botAI, Player* bot, std::vector<Position>& out)
    {
        std::vector<Creature*> guards;
        FightingWyrmguards(bot, guards);
        if (guards.empty())
            return;

        Unit* mine = KeepsBack(bot) ? nullptr : botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (mine && std::find(guards.begin(), guards.end(), mine) == guards.end())
            mine = nullptr;
        // Only one a tank holds, and only while the bot can take a stomp.
        if (mine)
        {
            Player* holder = mine->GetVictim() ? mine->GetVictim()->ToPlayer() : nullptr;
            if (!holder || !PlayerbotAI::IsTank(holder) || bot->GetHealthPct() < WYRMGUARD_MELEE_HEALTH_PCT)
                mine = nullptr;
        }
        // Stop at the first one too close: the loop must not measure from mine once it is null.
        if (mine)
            for (Creature const* other : guards)
                if (other != mine && other->GetExactDist(mine) < WYRMGUARD_APART)
                {
                    mine = nullptr;
                    break;
                }

        bool running = false;
        for (Creature* guard : guards)
        {
            if (guard == mine)
                continue;
            out.push_back(guard->GetPosition());
            Unit* victim = guard->GetVictim();
            if (victim && guard->GetExactDist(victim) > WYRMGUARD_RUNNING)
            {
                running = true;
                if (victim != bot)
                    out.push_back(victim->GetPosition());
            }
        }
        // While one is still on its way, the pack ends up on the main tank (where
        // mod-dungeon-clear's camp waits): that spot is cleared before it gets there.
        // Not for a melee bot on a guard it may stand by: it goes where that guard is held.
        Player* mainTank = running && !mine ? RaidClear::Tanks::ActingMainTank(bot->GetGroup()) : nullptr;
        if (mainTank && mainTank != bot && mainTank->IsAlive() && mainTank->IsInMap(bot))
            out.push_back(mainTank->GetPosition());
    }

    // The stomps above, per bot for WYRMGUARD_STOMPS_FRESH_MS: the trigger, the way out and the
    // hold below ask for them on every tick, the hold once for every action. One map thread
    // updates a bot at a time.
    std::vector<Position> const& CachedStomps(PlayerbotAI* botAI, Player* bot)
    {
        struct Entry
        {
            uint32 ms = 0;
            std::vector<Position> stomps;
        };
        thread_local std::unordered_map<ObjectGuid, Entry> tStomps;
        uint32 const now = getMSTime();
        Entry& entry = tStomps[bot->GetGUID()];
        if (!entry.ms || getMSTimeDiff(entry.ms, now) >= WYRMGUARD_STOMPS_FRESH_MS)
        {
            entry.stomps.clear();
            WyrmguardStomps(botAI, bot, entry.stomps);
            entry.ms = now ? now : 1;
        }
        return entry.stomps;
    }

    float StompGap(std::vector<Position> const& stomps, float x, float y, float z)
    {
        float gap = 1000.0f;
        for (Position const& stomp : stomps)
            gap = std::min(gap, stomp.GetExactDist(x, y, z));
        return gap;
    }

    // --- Nefarian: fear ------------------------------------------------

    // Nefarian, in the middle of Bellowing Roar.
    Creature* Roaring(Player* bot)
    {
        Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
        Spell const* spell = boss ? boss->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
        return spell && spell->GetSpellInfo() && spell->GetSpellInfo()->Id == SPELL_BELLOWING_ROAR ? boss : nullptr;
    }

    // A warrior the roar would reach: Berserker Rage (fear immunity for 10 s) before it lands.
    bool RagesThroughRoar(PlayerbotAI* botAI, Player* bot, Creature* boss)
    {
        return bot->IsClass(CLASS_WARRIOR) && bot->IsAlive() && !bot->HasAura(SPELL_BERSERKER_RAGE) &&
               bot->GetExactDist(boss) <= ROAR_KEEP_AWAY + bot->GetObjectSize() &&
               botAI->CanCastSpell("berserker rage", bot);
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

bool RaidClear::BlackwingLair::InDragonsWay(Player* bot, Creature* boss, float x, float y)
{
    return bot && boss && DragonGap(ZoneOf(bot, boss), x, y) < 0.0f;
}

bool RaidClear::BlackwingLair::DragonBreathing(Creature* boss)
{
    return boss && ConeCast(boss) != 0;
}

bool RaidClear::BlackwingLair::WalksBack(PlayerbotAI* botAI, Action* action, Position& to, bool& known)
{
    known = false;
    if (dynamic_cast<FollowAction*>(action))
    {
        // Follow walks to the bot's place in its formation.
        Formation* formation = botAI->GetAiObjectContext()->GetValue<Formation*>("formation")->Get();
        WorldLocation const place = formation ? formation->GetLocation() : Formation::NullLocation;
        if (!Formation::IsNullLocation(place))
        {
            to.Relocate(place.GetPositionX(), place.GetPositionY(), place.GetPositionZ());
            known = true;
        }
        return true;
    }

    // Their target is the bot itself: mod-dungeon-clear's camp hold, its follow, assist,
    // regroup and transit walks, and playerbots' "stay". Not its healers' reposition: that one
    // is how a healer gets back in range and sight of its tank, and holding it loses the tank.
    static constexpr std::array<char const*, 8> WALKS_BACK = {
        "dungeon clear hold at camp", "dungeon clear stay at camp", "dungeon clear assist camp",
        "dungeon clear assist camp combat", "dungeon clear follow tank", "dungeon clear regroup combat",
        "dungeon clear transit pack", "return to stay position",
    };
    std::string const name = action->getName();
    return std::any_of(WALKS_BACK.begin(), WALKS_BACK.end(), [&name](char const* walk) { return name == walk; });
}

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
    if (!action)
        return 1.0f;

    // Playerbots sends every bot that isn't the dragon's victim to its flank at melee range,
    // above almost everything else the bot does. Right for melee; for a caster or a healer it is
    // a walk into Cleave, Tail Lash and the roar, and a drake's second tank belongs in front.
    // (By class: the actions are named after the boss, "nefarian rear flank", not after the
    // names the strategy asks for them by.)
    if (dynamic_cast<BossRearFlankAction*>(action))
    {
        if (KeepsBack(bot))
            return 0.0f;
        Creature* drake = PlayerbotAI::IsTank(bot) ? FightingDrake(bot) : nullptr;
        return drake && HoldsDrake(bot, drake) ? 0.0f : 1.0f;
    }
    // The knockback footing sends a tank in front of a thrower to the side with clear ground
    // behind it. The drake's second tank stands by the one he is on and is thrown with it.
    if (action->getName() == "rc knockback footing" && PlayerbotAI::IsTank(bot))
    {
        Creature* drake = FightingDrake(bot);
        return drake && drake->GetVictim() != bot && HoldsDrake(bot, drake) ? 0.0f : 1.0f;
    }

    // Walking back to its place (follow, formation, a camp) doesn't take a bot into the front,
    // tail or roar it was just walked out of. Charge, Intercept and Feral Charge land on the
    // dragon's near side, which for a bot in front of him is in front of him. Not the bots that
    // stand in front, nor one on its way to Firemaw's cover.
    if (Walks(action))
    {
        Creature* dragon = BreathingDragon(bot);
        if (dragon && !StandsInFront(bot, dragon) && !GoingForCover(bot, dragon))
        {
            Position to;
            bool known = false;
            if (WalksBack(botAI, action, to, known))
            {
                if (known && DragonGap(ZoneOf(bot, dragon), to.GetPositionX(), to.GetPositionY()) < 0.0f)
                    return 0.0f;
            }
            else if (dynamic_cast<CastReachTargetSpellAction*>(action) && action->GetTarget() == dragon)
            {
                float const angle = dragon->GetAngle(bot);
                float const reach = dragon->GetCombatReach();
                if (DragonGap(ZoneOf(bot, dragon), dragon->GetPositionX() + std::cos(angle) * reach,
                              dragon->GetPositionY() + std::sin(angle) * reach) < 0.0f)
                    return 0.0f;
            }
        }
    }
    if (!KeepsBack(bot))
        return 1.0f;

    // Nefarian: nothing walks a bot that keeps back in under the roar. Everything on him and at
    // his feet is in spell range from outside it; the walks to the tank for a Fear Ward or a
    // Tremor Totem, and a bot he is on walking to his tank, are the exceptions.
    if (!Walks(action) || dynamic_cast<RcBwlFearWardAction*>(action) || dynamic_cast<RcBwlTremorTankAction*>(action) ||
        dynamic_cast<RcBwlNefarianToTankAction*>(action))
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || target == bot)
        return 1.0f;
    Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
    if (!boss || bot->GetExactDist(boss) > ROAR_HOLD || target->GetExactDist(boss) >= ROAR_KEEP_AWAY)
        return 1.0f;
    return 0.0f;
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
    if (!bot->IsClass(CLASS_SHAMAN) || !bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    return TremorToPlace(bot, FightingBoss(bot, NPC_NEFARIAN)) != nullptr;
}

bool RcBwlTremorTankAction::Execute(Event /*event*/)
{
    Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
    Player* tank = TremorToPlace(bot, boss);
    if (!tank)
        return false;

    // Without the roar: this walk goes in under it.
    Zone const zone = ZoneOf(bot, boss);
    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    if (bot->GetExactDist2d(tank) <= TREMOR_TANK_NEAR && DragonGap(zone, botX, botY) >= 0.0f)
    {
        if (bot->isMoving())
        {
            AI_VALUE(LastMovement&, "last movement").clear();
            bot->StopMoving();
        }
        return botAI->CanCastSpell("tremor totem", bot) && botAI->CastSpell("tremor totem", bot);
    }

    // Already on the way to such a spot.
    if (bot->isMoving() && !bot->movespline->Finalized())
    {
        G3D::Vector3 const dest = bot->movespline->FinalDestination();
        if (tank->GetExactDist2d(dest.x, dest.y) <= TREMOR_TANK_NEAR && DragonGap(zone, dest.x, dest.y) >= DRAGON_SPOT_MIN)
            return true;
    }

    // On rings round the tank, out of his front and tail, the nearest to the bot first; one the
    // straight walk to doesn't cross his front before one it does.
    struct Spot
    {
        float x, y, z, dist;
        bool clear;
    };
    std::vector<Spot> spots;
    Map* map = bot->GetMap();
    constexpr int STEPS = 16;
    for (float ring = TREMOR_RING; ring <= TREMOR_TANK_NEAR; ring += TREMOR_RING)
        for (int i = 0; i < STEPS; ++i)
        {
            float const angle = float(i) * 2.0f * float(M_PI) / float(STEPS);
            float const x = tank->GetPositionX() + std::cos(angle) * ring;
            float const y = tank->GetPositionY() + std::sin(angle) * ring;
            if (DragonGap(zone, x, y) < DRAGON_SPOT_MIN)
                continue;
            float const z = map->GetHeight(bot->GetPhaseMask(), x, y, tank->GetPositionZ() + RaidClear::WAY_OUT_UP);
            if (z <= INVALID_HEIGHT || std::fabs(z - tank->GetPositionZ()) > RaidClear::WAY_OUT_DOWN)
                continue;
            spots.push_back({ x, y, z, bot->GetExactDist2d(x, y), WalkClear(zone, botX, botY, x, y) });
        }
    std::sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b)
    {
        return a.clear != b.clear ? a.clear : a.dist < b.dist;
    });

    uint32 tries = 0;
    for (Spot const& spot : spots)
    {
        if (++tries > TREMOR_SPOT_TRIES)
            break;
        PathGenerator path(bot);
        constexpr uint32 NOT_A_WALK = PATHFIND_INCOMPLETE | PATHFIND_NOPATH | PATHFIND_SHORTCUT |
                                      PATHFIND_NOT_USING_PATH | PATHFIND_FARFROMPOLY;
        if (!path.CalculatePath(spot.x, spot.y, spot.z) || !(path.GetPathType() & PATHFIND_NORMAL) ||
            (path.GetPathType() & NOT_A_WALK))
            continue;
        return MoveTo(bot->GetMapId(), spot.x, spot.y, spot.z, false, false, false, false,
                      MovementPriority::MOVEMENT_COMBAT);
    }
    return false;
}

float RcBwlNefarianTremorMultiplier::GetValue(Action* action)
{
    if (!action || !bot->IsClass(CLASS_SHAMAN) || !bot->IsInCombat())
        return 1.0f;

    // Playerbots' names for whatever puts a totem in the earth slot or takes this one away,
    // its own Tremor Totem included: that one goes down by the shaman, wherever the shaman is.
    static constexpr std::array<char const*, 7> EARTH_SLOT = {
        "stoneskin totem", "strength of earth totem", "earthbind totem", "stoneclaw totem",
        "tremor totem", "call of the elements", "totemic recall",
    };
    std::string const name = action->getName();
    if (std::none_of(EARTH_SLOT.begin(), EARTH_SLOT.end(), [&name](char const* other) { return name == other; }))
        return 1.0f;

    // Only the shaman that keeps a tank's Tremor; the others keep their own totems.
    return FightingBoss(bot, NPC_NEFARIAN) && TremorTankOf(bot) ? 0.0f : 1.0f;
}

bool RcBwlBellowingRoarTrigger::IsActive()
{
    Creature* boss = bot->IsClass(CLASS_WARRIOR) ? Roaring(bot) : nullptr;
    return boss && RagesThroughRoar(botAI, bot, boss);
}

bool RcBwlBerserkerRageAction::Execute(Event /*event*/)
{
    Creature* boss = Roaring(bot);
    return boss && RagesThroughRoar(botAI, bot, boss) && botAI->CastSpell("berserker rage", bot);
}

// --- Nefarian on someone who isn't a tank -------------------------------------

namespace
{
    // The tank a non-tank he is on walks to, or nullptr.
    Player* LooseNefarianTank(PlayerbotAI* botAI, Player* bot)
    {
        if (!bot->IsAlive() || PlayerbotAI::IsTank(bot) || !botAI->CanMove())
            return nullptr;
        Creature* boss = FightingBoss(bot, NPC_NEFARIAN);
        if (!boss || boss->GetVictim() != bot)
            return nullptr;
        Player* tank = NefarianTank(bot);
        return tank && tank != bot && bot->GetExactDist2d(tank) > NEFARIAN_LOOSE_NEAR ? tank : nullptr;
    }
}

bool RcBwlNefarianLooseTrigger::IsActive()
{
    return LooseNefarianTank(botAI, bot) != nullptr;
}

bool RcBwlNefarianToTankAction::Execute(Event /*event*/)
{
    Player* tank = LooseNefarianTank(botAI, bot);
    if (!tank)
        return false;
    // Already on its way there.
    if (bot->isMoving() && !bot->movespline->Finalized())
    {
        G3D::Vector3 const dest = bot->movespline->FinalDestination();
        if (tank->GetExactDist2d(dest.x, dest.y) <= NEFARIAN_LOOSE_NEAR)
            return true;
    }
    // His breath follows the bot: the cast it is in the middle of can wait.
    AI_VALUE(LastMovement&, "last movement").clear();
    bot->CastStop();
    return MoveNear(tank, NEFARIAN_LOOSE_NEAR / 2.0f, MovementPriority::MOVEMENT_FORCED);
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

    // Straight there when that doesn't cross his front or his tail. A healer may be out of reach
    // of the tank on the way (round his back): it is walking to where it will be in reach.
    Zone zone = ZoneOf(bot, boss);
    zone.reach = 0.0f;
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

    float const r = std::max(std::hypot(botX - zone.x, botY - zone.y), (zone.roar ? ROAR_KEEP_AWAY : TAIL_RANGE) + 3.0f);
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
    // Not only in combat: the bots waiting at the camp while the tank brings the pack in are
    // out of it until the pack arrives, and the pack stomps 3-4 s after that.
    if (!bot->IsAlive() || PlayerbotAI::IsTank(bot) || !botAI->CanMove())
        return false;
    std::vector<Position> const& stomps = CachedStomps(botAI, bot);
    return !stomps.empty() &&
           StompGap(stomps, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) < WYRMGUARD_KEEP_AWAY;
}

bool RcBwlWyrmguardKeepAwayAction::Execute(Event /*event*/)
{
    if (PlayerbotAI::IsTank(bot))
        return false;
    // A copy: the walk asks for paths, and the cache is the bot's own anyway.
    std::vector<Position> const stomps = CachedStomps(botAI, bot);
    if (stomps.empty())
        return false;

    auto const gapAt = [&stomps](float x, float y, float z) { return StompGap(stomps, x, y, z); };
    if (gapAt(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) >= WYRMGUARD_KEEP_AWAY)
        return false;
    return _walk.Go(gapAt, WYRMGUARD_KEEP_AWAY, WYRMGUARD_SPOT, WYRMGUARD_WORTH_IT, WYRMGUARD_WAY_OUT_SEARCH);
}

float RcBwlWyrmguardHoldMultiplier::GetValue(Action* action)
{
    // Nothing walks a bot that isn't a tank into a stomp it should be out of: casters and
    // healers reach their target from outside, melee only go to the one Wyrmguard they may stand
    // by. Picking a target isn't held (see Walks): a bot held here still fights from where it is.
    if (!action || !Walks(action) || PlayerbotAI::IsTank(bot))
        return 1.0f;

    // Walks back to a spot (follow and formation, mod-dungeon-clear's camp hold, assist and
    // follow, playerbots' "stay"), from wherever the bot starts: the camp is where the pack is
    // dragged to, and the way out stops sending for 700 ms after each send, long enough for one
    // of these to walk the bot back in. Where the walk ends isn't always known: then it waits
    // until no Wyrmguard is fighting near it.
    Position to;
    bool known = false;
    bool const walksBack = WalksBack(botAI, action, to, known);
    Unit* target = action->GetTarget();
    if (!walksBack && (!target || target == bot))
        return 1.0f;

    std::vector<Position> const& stomps = CachedStomps(botAI, bot);
    if (stomps.empty())
        return 1.0f;
    // A walk that ends somewhere unknown is only held for a bot already near the fight; one
    // further off (rezzed at the entrance, behind on the route) has to be able to come back.
    if (walksBack)
    {
        if (known)
            return StompGap(stomps, to.GetPositionX(), to.GetPositionY(), to.GetPositionZ()) < WYRMGUARD_KEEP_AWAY
                       ? 0.0f
                       : 1.0f;
        return StompGap(stomps, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) < WYRMGUARD_HOLD
                   ? 0.0f
                   : 1.0f;
    }

    // Toward a target: melee end up next to it, casters and healers stop at spell range, so for
    // them only once they are close already.
    if (StompGap(stomps, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ()) >= WYRMGUARD_KEEP_AWAY)
        return 1.0f;
    if (!KeepsBack(bot))
        return 0.0f;
    return StompGap(stomps, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) < WYRMGUARD_HOLD ? 0.0f
                                                                                                             : 1.0f;
}

// --- The drakes: who holds them ---------------------------------------------

bool RaidClear::BlackwingLair::HoldsDrake(Player* tank, Creature* drake)
{
    if (!tank || !drake || !IsDrake(drake) || !PlayerbotAI::IsTank(tank))
        return false;
    if (drake->GetVictim() == tank)
        return true;
    Group* group = tank->GetGroup();
    if (!group)
        return false;
    DrakeHolders const holders = HoldersOf(group, drake);
    return tank == holders.first || tank == holders.second;
}

bool RcBwlDrakeFrontTrigger::IsActive()
{
    if (!bot->IsAlive() || !botAI->CanMove() || !PlayerbotAI::IsTank(bot))
        return false;
    Creature* drake = FightingDrake(bot);
    if (!drake || !HoldsDrake(bot, drake))
        return false;
    // Firemaw with both tank spots set: his second tank stands at one of them ("rc bwl boss spot").
    if (drake->GetEntry() == NPC_FIREMAW && BossSpot(NPC_FIREMAW, Spot::Tank) && BossSpot(NPC_FIREMAW, Spot::OffTank))
        return false;
    Player* victim = DrakeVictimToJoin(bot, drake);
    return victim && AwayFromFront(bot, drake, victim);
}

bool RcBwlDrakeFrontAction::Execute(Event /*event*/)
{
    Creature* drake = FightingDrake(bot);
    if (!drake || !HoldsDrake(bot, drake))
        return false;
    Player* victim = DrakeVictimToJoin(bot, drake);
    if (!victim || !AwayFromFront(bot, drake, victim))
        return false;
    return MoveNear(victim, DRAKE_SECOND_SPOT, MovementPriority::MOVEMENT_COMBAT);
}

namespace
{
    // One of the drake's two tanks is in range of him with a taunt ready.
    bool HolderCanTakeBack(Player* bot, Creature* drake)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        DrakeHolders const holders = HoldersOf(group, drake);
        for (Player* tank : { holders.first, holders.second })
        {
            PlayerbotAI* tankAI = tank && tank->IsAlive() ? GET_PLAYERBOT_AI(tank) : nullptr;
            if (tankAI && RaidClear::Tanks::CanTaunt(tankAI, drake))
                return true;
        }
        return false;
    }

    // The threat the drake's other tanks stay under: that of the one of his two that would take
    // him next. With him on one of them, the other; otherwise the lower of the two.
    float DrakeThreatCap(Player* bot, Creature* drake)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return 0.0f;
        DrakeHolders const holders = HoldersOf(group, drake);
        ThreatManager const& threat = drake->GetThreatMgr();
        Unit* victim = drake->GetVictim();
        float cap = -1.0f;
        for (Player* tank : { holders.first, holders.second })
        {
            if (!tank || !tank->IsAlive() || tank == victim)
                continue;
            float const theirs = threat.GetThreat(tank);
            cap = cap < 0.0f ? theirs : std::min(cap, theirs);
        }
        if (cap < 0.0f && victim && (victim == holders.first || victim == holders.second))
            cap = threat.GetThreat(victim);
        return std::max(cap, 0.0f);
    }

    // Firemaw: this tank, one of his two, has so many fewer Flame Buffet stacks than the tank he
    // is on that it should have him.
    bool FresherThanVictim(Player* bot, Creature* drake, uint8 victimAtLeast)
    {
        uint8 const theirs = FlameBuffetOn(drake->GetVictim());
        uint8 const mine = FlameBuffetOn(bot);
        return theirs >= victimAtLeast && theirs >= mine + TANK_SWAP_GAP;
    }
}

// --- Firemaw: his two tanks trade him ------------------------------------------

bool RcBwlDrakeSwapTrigger::IsActive()
{
    if (!bot->IsAlive() || !PlayerbotAI::IsTank(bot))
        return false;
    Creature* drake = FightingBoss(bot, NPC_FIREMAW);
    if (!drake || drake->GetVictim() == bot || !DrakeVictimToJoin(bot, drake) || !HoldsDrake(bot, drake))
        return false;
    if (!FresherThanVictim(bot, drake, TANK_SWAP_STACKS) || ConeCast(drake) == SPELL_SHADOW_FLAME ||
        !TauntsFromFront(bot, drake))
        return false;
    return RaidClear::Tanks::CanTaunt(botAI, drake);
}

bool RcBwlDrakeSwapAction::Execute(Event /*event*/)
{
    Creature* drake = FightingBoss(bot, NPC_FIREMAW);
    return drake && drake->GetVictim() != bot && RaidClear::Tanks::Taunt(botAI, drake);
}

float RcBwlDrakeTauntMultiplier::GetValue(Action* action)
{
    if (!action || !PlayerbotAI::IsTank(bot))
        return 1.0f;
    std::string const name = action->getName();
    bool const taunt = IsTaunt(name);
    if (!taunt && action->getThreatType() == Action::ActionThreatType::None)
        return 1.0f;
    Creature* drake = FightingDrake(bot);
    if (!drake)
        return 1.0f;

    if (taunt)
    {
        if (!TauntsDrake(bot, action, name, drake))
            return 1.0f;
        Unit* victim = drake->GetVictim();
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        bool const onTank = victimPlayer && PlayerbotAI::IsTank(victimPlayer);
        // He turns to whoever takes him: only his two tanks do, unless he is loose on someone
        // who isn't a tank and neither of them can take him back right now (both thrown out of
        // range by the Wing Buffet, taunts on cooldown), or neither has for DRAKE_LOOSE_TAUNT_MS
        // (busy with an add, or "could" but didn't).
        uint32 const looseMs = LooseForMs(drake);
        if (!HoldsDrake(bot, drake))
            return !onTank && (!HolderCanTakeBack(bot, drake) || looseMs >= DRAKE_LOOSE_TAUNT_MS) ? 1.0f : 0.0f;
        if (!onTank)
            return 1.0f;
        // A breath goes where he faces when it ends.
        if (ConeCast(drake) == SPELL_SHADOW_FLAME)
            return 0.0f;
        // Firemaw on his other tank: only take him when that one has far more stacks (the swap),
        // so the two don't hand him back and forth after every Wing Buffet.
        if (drake->GetEntry() == NPC_FIREMAW && victim != bot && !FresherThanVictim(bot, drake, 0))
            return 0.0f;
        return TauntsFromFront(bot, drake) ? 1.0f : 0.0f;
    }

    // Every other tank stays under the one of his two that takes him next, so a Wing Buffet on
    // one hands him to the other and not to whichever tank happens to be next on his threat.
    // Swings carry on. Area threat (Consecration, Thunder Clap, Swipe) counts once the bot is at
    // his feet, unless it is holding an add there.
    if (action->getThreatType() == Action::ActionThreatType::Aoe)
    {
        if (!bot->IsWithinCombatRange(drake, 10.0f) || HoldsAnAdd(bot, drake))
            return 1.0f;
    }
    else if (action->GetTarget() != drake)
        return 1.0f;
    if (HoldsDrake(bot, drake))
    {
        // Firemaw: the holder he isn't on, with more stacks than the tank he is on, stays under
        // that one's threat. He was handed over to get him off it, and it would take him back at
        // 110% in melee (and then nobody may taunt him off it: the gap rule).
        Unit* victim = drake->GetVictim();
        Player* onTank = victim ? victim->ToPlayer() : nullptr;
        if (drake->GetEntry() != NPC_FIREMAW || !onTank || onTank == bot || !PlayerbotAI::IsTank(onTank) ||
            FlameBuffetOn(bot) <= FlameBuffetOn(onTank))
            return 1.0f;
        float const theirs = drake->GetThreatMgr().GetThreat(onTank);
        return theirs > 0.0f && drake->GetThreatMgr().GetThreat(bot) >= theirs * DRAKE_OFFTANK_SHARE ? 0.0f : 1.0f;
    }
    float const cap = DrakeThreatCap(bot, drake);
    if (cap <= 0.0f)
        return 1.0f;
    return drake->GetThreatMgr().GetThreat(bot) >= cap * DRAKE_OFFTANK_SHARE ? 0.0f : 1.0f;
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
        // Bellowing Roar: the warriors it would reach look at once, for Berserker Rage.
        if (boss->GetEntry() == NPC_NEFARIAN && spellInfo->Id == SPELL_BELLOWING_ROAR)
        {
            Map::PlayerList const& players = boss->GetMap()->GetPlayers();
            for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
            {
                Player* player = it->GetSource();
                PlayerbotAI* botAI = player && player->IsClass(CLASS_WARRIOR) ? GET_PLAYERBOT_AI(player) : nullptr;
                if (botAI && botAI->HasStrategy("rc bwl", BOT_STATE_COMBAT) && RagesThroughRoar(botAI, player, boss))
                    botAI->ResetActionDuration();
            }
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
