/*
 * mod-raid-clear: raid death log.
 *
 * Every player death on a raid map gets one line in the server log (logger "module"), so a bad
 * night can be read back fight by fight and the mechanic that is killing the bots found:
 *
 *   [RaidClear] death: Name (Mage, healer, bot) BWL | Chromaggus 41% [Frenzy, Elemental Shield]
 *     22 yd, in LOS | killed by Incinerate (Chromaggus) 3120 | last 6s: Incinerate (Chromaggus)
 *     3120; Brood Affliction: Red (Chromaggus) 2x 410 | debuffs: Brood Affliction: Black, ...
 *
 * and when a boss dies or resets after deaths, a tally of what killed people in that pull:
 *
 *   [RaidClear] Chromaggus killed (BWL): 24 deaths | Incinerate 9, melee (Chromaggus) 6, ...
 *
 * Damage is read as dealt (after armor, resistances and absorbs), from OnDamage. The spell is
 * taken from the damage hook that ran just before on the same thread; anything that can't be
 * matched is reported as "unknown".
 *
 * Released under the MIT License.
 */

#include "DeathLog.h"

#include "RaidClearConfig.h"

#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "UnitScript.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"

#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace
{
    // Hits older than this don't make the "last" list.
    constexpr uint32 HISTORY_MS = 6000;
    constexpr size_t HISTORY_MAX = 24;
    // A boss that hit someone this recently is the fight a death belongs to.
    constexpr uint32 BOSS_RECENT_MS = 30000;

    struct Hit
    {
        uint32 ms;
        uint32 spellId;  // 0 = melee
        bool unknown;
        ObjectGuid source;
        std::string sourceName;
        uint32 amount;
        bool lethal;  // took the victim's last health
    };

    struct Victim
    {
        std::deque<Hit> hits;
        // Debuffs as they were when the lethal hit landed: the core strips them before any death
        // hook runs.
        std::string debuffs;
        uint32 lastMs = 0;
    };

    struct Fight
    {
        ObjectGuid boss;
        std::string bossName;
        uint32 lastSeenMs = 0;
        uint32 deaths = 0;
        std::map<std::string, uint32> causes;
    };

    // Victims and fights nobody has touched for this long (left the raid alive, instance unloaded
    // mid-pull) are dropped on the next prune.
    constexpr uint32 STALE_MS = 10 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 PRUNE_INTERVAL_MS = MINUTE * IN_MILLISECONDS;

    std::mutex sLock;
    std::unordered_map<ObjectGuid, Victim> sVictims;  // by victim
    std::unordered_map<uint32, Fight> sFights;        // by instance id
    uint32 sLastPruneMs = 0;

    // Under sLock.
    void PruneStale(uint32 now)
    {
        if (getMSTimeDiff(sLastPruneMs, now) < PRUNE_INTERVAL_MS)
            return;
        sLastPruneMs = now;
        for (auto it = sVictims.begin(); it != sVictims.end();)
            it = getMSTimeDiff(it->second.lastMs, now) > STALE_MS ? sVictims.erase(it) : std::next(it);
        for (auto it = sFights.begin(); it != sFights.end();)
            it = getMSTimeDiff(it->second.lastSeenMs, now) > STALE_MS ? sFights.erase(it) : std::next(it);
    }

    // The damage hook that ran last on this thread, waiting for its OnDamage.
    struct Pending
    {
        ObjectGuid target;
        ObjectGuid attacker;
        uint32 spellId = 0;
        uint32 ms = 0;
        bool set = false;
    };
    thread_local Pending tPending;
    // A damage hook and its OnDamage run in the same call; anything older is someone else's.
    constexpr uint32 PENDING_MS = 50;

    bool Tracked(Unit const* victim)
    {
        if (!victim || !victim->IsPlayer() || !RaidClear::GetConfig().enable || !RaidClear::GetConfig().deathLog)
            return false;
        Map const* map = victim->GetMap();
        return map && map->IsRaid();
    }

    bool IsBoss(Unit const* unit)
    {
        Creature const* creature = unit ? unit->ToCreature() : nullptr;
        return creature && (creature->IsDungeonBoss() || creature->isWorldBoss());
    }

    std::string SpellName(uint32 spellId)
    {
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        return info ? std::string(info->SpellName[LOCALE_enUS]) : std::to_string(spellId);
    }

    std::string Cause(Hit const& hit)
    {
        if (hit.unknown)
            return "unknown (" + hit.sourceName + ")";
        if (!hit.spellId)
            return "melee (" + hit.sourceName + ")";
        return SpellName(hit.spellId) + " (" + hit.sourceName + ")";
    }

    char const* ClassName(uint8 cls)
    {
        switch (cls)
        {
            case CLASS_WARRIOR:      return "Warrior";
            case CLASS_PALADIN:      return "Paladin";
            case CLASS_HUNTER:       return "Hunter";
            case CLASS_ROGUE:        return "Rogue";
            case CLASS_PRIEST:       return "Priest";
            case CLASS_DEATH_KNIGHT: return "Death Knight";
            case CLASS_SHAMAN:       return "Shaman";
            case CLASS_MAGE:         return "Mage";
            case CLASS_WARLOCK:      return "Warlock";
            case CLASS_DRUID:        return "Druid";
            default:                 return "?";
        }
    }

    // Auras on the unit, debuffs or buffs, with stacks; passive and permanent ones left out.
    std::string AuraList(Unit const* unit, bool debuffs)
    {
        std::ostringstream out;
        bool first = true;
        for (auto const& [id, aurApp] : unit->GetAppliedAuras())
        {
            Aura const* aura = aurApp->GetBase();
            if (aurApp->IsPositive() == debuffs || aura->IsPassive() || aura->GetMaxDuration() <= 0)
                continue;
            out << (first ? "" : ", ") << aura->GetSpellInfo()->SpellName[LOCALE_enUS];
            if (aura->GetStackAmount() > 1)
                out << " x" << uint32(aura->GetStackAmount());
            first = false;
        }
        return first ? "none" : out.str();
    }

    void Record(Unit* attacker, Unit* victim, uint32 spellId, bool unknown, uint32 amount)
    {
        uint32 const now = getMSTime();
        bool const lethal = amount >= victim->GetHealth();
        Hit hit{ now, spellId, unknown, attacker ? attacker->GetGUID() : ObjectGuid::Empty,
                 attacker ? attacker->GetName() : std::string("environment"), amount, lethal };
        std::string debuffs = lethal ? AuraList(victim, true) : std::string();

        std::lock_guard<std::mutex> guard(sLock);
        PruneStale(now);
        Victim& record = sVictims[victim->GetGUID()];
        record.lastMs = now;
        if (lethal)
            record.debuffs = std::move(debuffs);
        std::deque<Hit>& hits = record.hits;
        hits.push_back(std::move(hit));
        while (hits.size() > HISTORY_MAX || (!hits.empty() && getMSTimeDiff(hits.front().ms, now) > HISTORY_MS))
            hits.pop_front();

        if (IsBoss(attacker))
        {
            // A second boss joining the fight (Lord Victor Nefarius, then Nefarian) takes over the
            // name and the "boss" shown on deaths; the deaths counted so far stay.
            Fight& fight = sFights[victim->GetInstanceId()];
            if (fight.lastSeenMs && getMSTimeDiff(fight.lastSeenMs, now) > BOSS_RECENT_MS)
                fight = Fight();  // an earlier pull that never ended cleanly
            fight.boss = attacker->GetGUID();
            fight.bossName = attacker->GetName();
            fight.lastSeenMs = now;
        }
    }

    void LogDeath(Player* victim, Unit* killer)
    {
        uint32 const now = getMSTime();
        std::deque<Hit> hits;
        std::string debuffs;
        ObjectGuid bossGuid;
        {
            std::lock_guard<std::mutex> guard(sLock);
            auto it = sVictims.find(victim->GetGUID());
            if (it != sVictims.end())
            {
                hits = std::move(it->second.hits);
                debuffs = std::move(it->second.debuffs);
                sVictims.erase(it);
            }
            auto f = sFights.find(victim->GetInstanceId());
            if (f != sFights.end() && getMSTimeDiff(f->second.lastSeenMs, now) <= BOSS_RECENT_MS)
                bossGuid = f->second.boss;
        }

        std::ostringstream out;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(victim);
        char const* role = PlayerbotAI::IsTank(victim) ? "tank" : PlayerbotAI::IsHeal(victim) ? "healer" : "dps";
        out << "[RaidClear] death: " << victim->GetName() << " (" << ClassName(victim->getClass()) << ", " << role
            << ", " << (botAI ? "bot" : "player") << ") map " << victim->GetMapId() << " instance "
            << victim->GetInstanceId();

        // The fight: the boss that hit someone in this instance in the last 30s, else trash.
        Creature* boss = bossGuid ? ObjectAccessor::GetCreature(*victim, bossGuid) : nullptr;
        if (boss && boss->IsAlive())
            out << " | " << boss->GetName() << " " << uint32(boss->GetHealthPct()) << "% [" << AuraList(boss, false)
                << "] " << uint32(victim->GetExactDist(boss)) << " yd, "
                << (boss->IsWithinLOSInMap(victim) ? "in LOS" : "out of LOS");
        else
            out << " | trash";

        // The killing blow: the last hit, if it took the last of the victim's health. A death
        // without one (an instant kill, a mind-controlled player killed by the raid) names the
        // killer instead of blaming whatever hit last.
        Hit const* last = hits.empty() || !hits.back().lethal ? nullptr : &hits.back();
        std::string cause;
        if (last)
            cause = Cause(*last);
        else if (killer && killer != victim)
            cause = "no lethal hit (" + killer->GetName() + ")";
        else
            cause = "no lethal hit";
        out << " | killed by " << cause;
        if (last)
            out << " " << last->amount;

        // Everything in the last 6s, grouped by cause.
        std::map<std::string, std::pair<uint32, uint32>> grouped;  // cause -> hits, total
        for (Hit const& hit : hits)
        {
            auto& [count, total] = grouped[Cause(hit)];
            ++count;
            total += hit.amount;
        }
        out << " | last 6s:";
        if (grouped.empty())
            out << " nothing";
        bool first = true;
        for (auto const& [name, stats] : grouped)
        {
            out << (first ? " " : "; ") << name << " ";
            if (stats.first > 1)
                out << stats.first << "x ";
            out << stats.second;
            first = false;
        }

        out << " | debuffs: " << (debuffs.empty() ? std::string("unknown") : debuffs);
        LOG_INFO("module", "{}", out.str());

        std::lock_guard<std::mutex> guard(sLock);
        auto f = sFights.find(victim->GetInstanceId());
        if (f != sFights.end() && getMSTimeDiff(f->second.lastSeenMs, now) <= BOSS_RECENT_MS)
        {
            ++f->second.deaths;
            ++f->second.causes[cause];
        }
    }

    // A boss died or reset: one line with what killed people during its fight.
    void LogFightEnd(Creature* boss, char const* outcome)
    {
        Fight fight;
        {
            std::lock_guard<std::mutex> guard(sLock);
            auto it = sFights.find(boss->GetInstanceId());
            if (it == sFights.end() || it->second.boss != boss->GetGUID())
                return;
            fight = std::move(it->second);
            sFights.erase(it);
        }
        if (!fight.deaths)
            return;

        std::vector<std::pair<std::string, uint32>> causes(fight.causes.begin(), fight.causes.end());
        std::sort(causes.begin(), causes.end(), [](auto const& a, auto const& b) { return a.second > b.second; });

        std::ostringstream out;
        out << "[RaidClear] " << fight.bossName << " " << outcome << " (map " << boss->GetMapId() << " instance "
            << boss->GetInstanceId() << "): " << fight.deaths << " deaths |";
        bool first = true;
        for (auto const& [cause, count] : causes)
        {
            out << (first ? " " : ", ") << cause << " " << count;
            first = false;
        }
        LOG_INFO("module", "{}", out.str());
    }
}

class RaidClearDeathLogUnitScript : public UnitScript
{
public:
    RaidClearDeathLogUnitScript()
        : UnitScript("RaidClearDeathLogUnitScript", true,
                     { UNITHOOK_ON_DAMAGE, UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK, UNITHOOK_MODIFY_MELEE_DAMAGE,
                       UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN, UNITHOOK_ON_UNIT_DEATH, UNITHOOK_ON_UNIT_ENTER_EVADE_MODE })
    {
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& /*damage*/, SpellInfo const* spellInfo) override
    {
        Remember(target, attacker, spellInfo ? spellInfo->Id : 0);
    }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& /*damage*/) override
    {
        Remember(target, attacker, 0);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& /*damage*/, SpellInfo const* spellInfo) override
    {
        Remember(target, attacker, spellInfo ? spellInfo->Id : 0);
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!damage || !Tracked(victim) || attacker == victim)
            return;

        // The damage hook just before this, if it was about the same two units and only moments ago
        // (a hook whose damage was absorbed or resisted never reaches here, and must not label a
        // later hit).
        bool const matched = tPending.set && getMSTimeDiff(tPending.ms, getMSTime()) <= PENDING_MS &&
                             tPending.target == victim->GetGUID() &&
                             tPending.attacker == (attacker ? attacker->GetGUID() : ObjectGuid::Empty);
        uint32 const spellId = matched ? tPending.spellId : 0;
        tPending.set = false;
        Record(attacker, victim, spellId, !matched, damage);
    }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (Player* player = unit->ToPlayer())
        {
            if (Tracked(player))
                LogDeath(player, killer);
            return;
        }
        if (IsBoss(unit) && unit->GetMap() && unit->GetMap()->IsRaid())
            LogFightEnd(unit->ToCreature(), "killed");
    }

    void OnUnitEnterEvadeMode(Unit* unit, uint8 /*evadeReason*/) override
    {
        if (IsBoss(unit) && unit->GetMap() && unit->GetMap()->IsRaid())
            LogFightEnd(unit->ToCreature(), "reset");
    }

private:
    static void Remember(Unit* target, Unit* attacker, uint32 spellId)
    {
        if (!Tracked(target))
            return;
        tPending.target = target->GetGUID();
        tPending.attacker = attacker ? attacker->GetGUID() : ObjectGuid::Empty;
        tPending.spellId = spellId;
        tPending.ms = getMSTime();
        tPending.set = true;
    }
};

class RaidClearDeathLogPlayerScript : public PlayerScript
{
public:
    RaidClearDeathLogPlayerScript() : PlayerScript("RaidClearDeathLogPlayerScript", { PLAYERHOOK_ON_LOGOUT }) {}

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(sLock);
        sVictims.erase(player->GetGUID());
    }
};

void RaidClear::DeathLog::AddScripts()
{
    new RaidClearDeathLogUnitScript();
    new RaidClearDeathLogPlayerScript();
}
