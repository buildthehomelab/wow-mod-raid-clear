/*
 * mod-raid-clear: Temple of Ahn'Qiraj, Battleguard Sartura and her Royal Guards. See
 * AqRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"
#include "Common/Interrupts.h"

#include "Creature.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <list>
#include <unordered_map>

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    // --- Whirlwind --------------------------------------------------------

    // Sartura or a guard in the middle of a whirlwind: where it was a moment ago, how far its
    // hits reach (centre to centre) and who it is on.
    struct Whirl
    {
        float x;
        float y;
        float reach;
        ObjectGuid victim;
    };

    float SarturaReach()
    {
        static float const reach = RaidClear::AhnQiraj::SpellRadius(SPELL_SARTURA_WHIRLWIND_HIT, 10.0f);
        return reach;
    }

    float GuardReach()
    {
        static float const reach = RaidClear::AhnQiraj::SpellRadius(SPELL_GUARD_WHIRLWIND_HIT, 10.0f);
        return reach;
    }

    // Every whirlwind in her room, looked up again after WHIRLS_FRESH_MS (she covers 15 yd a
    // second, so not much later than that): the triggers and the hold multiplier of the whole
    // raid ask many times a second. One instance's bots update one after another on one thread.
    constexpr uint32 WHIRLS_FRESH_MS = 100;
    std::vector<Whirl> const& Whirls(Player* bot)
    {
        thread_local uint32 tInstanceId = 0;
        thread_local uint32 tAtMs = 0;
        thread_local std::vector<Whirl> tWhirls;
        uint32 const now = getMSTime();
        if (tInstanceId == bot->GetInstanceId() && tAtMs && getMSTimeDiff(tAtMs, now) < WHIRLS_FRESH_MS)
            return tWhirls;

        tWhirls.clear();
        tInstanceId = bot->GetInstanceId();
        tAtMs = now ? now : 1;
        // Her fight, or her guards' (they can be pulled on their own).
        Creature* anchor = RaidClear::FightingCreature(bot, { NPC_SARTURA, NPC_SARTURA_ROYAL_GUARD });
        if (!anchor)
            return tWhirls;

        std::list<Creature*> found;
        anchor->GetCreatureListWithEntryInGrid(found, { NPC_SARTURA, NPC_SARTURA_ROYAL_GUARD }, WHIRLWIND_SEARCH);
        for (Creature const* mob : found)
        {
            if (!mob->IsAlive())
                continue;
            float reach = 0.0f;
            if (mob->GetEntry() == NPC_SARTURA && mob->HasAura(SPELL_SARTURA_WHIRLWIND))
                reach = SarturaReach();
            else if (mob->GetEntry() == NPC_SARTURA_ROYAL_GUARD && mob->HasAura(SPELL_GUARD_WHIRLWIND))
                reach = GuardReach();
            if (reach <= 0.0f)
                continue;
            Unit const* victim = mob->GetVictim();
            tWhirls.push_back({ mob->GetPositionX(), mob->GetPositionY(), reach,
                                victim ? victim->GetGUID() : ObjectGuid::Empty });
        }
        return tWhirls;
    }

    // Yards from a spot to the edge of the nearest whirlwind's reach (less than 0 inside one).
    // Those on `skip` don't count: there is no getting away from one that chases you.
    float WhirlGap(std::vector<Whirl> const& whirls, float x, float y, ObjectGuid skip = ObjectGuid::Empty)
    {
        float gap = 1000.0f;
        for (Whirl const& whirl : whirls)
            if (!skip || whirl.victim != skip)
                gap = std::min(gap, std::hypot(whirl.x - x, whirl.y - y) - whirl.reach);
        return gap;
    }

    float WhirlGap(std::vector<Whirl> const& whirls, Unit const* unit, ObjectGuid skip = ObjectGuid::Empty)
    {
        return WhirlGap(whirls, unit->GetPositionX(), unit->GetPositionY(), skip);
    }

    // --- Stuns ------------------------------------------------------------

    struct Stun
    {
        char const* spell;  // playerbots name
        float range;        // 0 = melee reach
    };
    constexpr std::array<Stun, 5> STUNS = {{
        { "hammer of justice", 10.0f }, { "concussion blow", 0.0f }, { "kidney shot", 0.0f },
        { "bash", 0.0f }, { "maim", 0.0f },
    }};

    // When a bot of the instance last sent a stun at each target (getMSTime()). One instance's
    // bots update one after another on one thread.
    std::unordered_map<ObjectGuid, uint32>& StunsSent()
    {
        thread_local std::unordered_map<ObjectGuid, uint32> tSent;
        return tSent;
    }

    bool StunJustSent(Unit const* target)
    {
        auto& sent = StunsSent();
        auto it = sent.find(target->GetGUID());
        if (it == sent.end())
            return false;
        if (getMSTimeDiff(it->second, getMSTime()) < STUN_SPREAD_MS)
            return true;
        sent.erase(it);
        return false;
    }

    // Whether this one should be stunned now: not stunned already, not just sent one, and (for
    // her) not in her whirlwind, which makes her immune.
    bool WantsStun(Creature const* mob)
    {
        if (!mob || !mob->IsAlive() || !mob->IsInCombat() || mob->HasUnitState(UNIT_STATE_STUNNED) ||
            StunJustSent(mob))
            return false;
        return mob->GetEntry() != NPC_SARTURA || !mob->HasAura(SPELL_SARTURA_WHIRLWIND);
    }

    // The bot's first stun that is ready, paid for and in reach of the target, and that the
    // target isn't immune to. CanCastSpell lets an out-of-range target through.
    char const* ReadyStun(PlayerbotAI* botAI, Player* bot, Unit* target)
    {
        for (Stun const& stun : STUNS)
        {
            bool const inReach =
                stun.range > 0.0f ? bot->GetDistance(target) <= stun.range : bot->IsWithinMeleeRange(target);
            if (!inReach)
                continue;
            uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", stun.spell)->Get();
            SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
            if (!info || target->IsImmunedToSpell(info))
                continue;
            if (botAI->CanCastSpell(stun.spell, target) && RaidClear::CanPayFor(botAI, bot, stun.spell))
                return bot->IsWithinLOSInMap(target) ? stun.spell : nullptr;
        }
        return nullptr;
    }

    // What the bot stuns, and with what: a guard in its whirlwind first, then Sartura out of hers,
    // then any other guard.
    Unit* StunTarget(PlayerbotAI* botAI, Player* bot, char const*& spell)
    {
        Creature* sartura = RaidClear::FightingCreature(bot, { NPC_SARTURA });
        if (!sartura)
            return nullptr;

        std::list<Creature*> guards;
        bot->GetCreatureListWithEntryInGrid(guards, NPC_SARTURA_ROYAL_GUARD, 15.0f);
        std::vector<Creature*> order;
        for (Creature* guard : guards)
            if (guard->HasAura(SPELL_GUARD_WHIRLWIND))
                order.push_back(guard);
        order.push_back(sartura);
        for (Creature* guard : guards)
            if (!guard->HasAura(SPELL_GUARD_WHIRLWIND))
                order.push_back(guard);

        for (Creature* mob : order)
        {
            if (!WantsStun(mob))
                continue;
            if ((spell = ReadyStun(botAI, bot, mob)))
                return mob;
        }
        return nullptr;
    }
}

// --- Whirlwind ------------------------------------------------------------

bool RcAq40WhirlwindTrigger::IsActive()
{
    // A bot that is feared or rooted isn't going anywhere.
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove())
        return false;
    std::vector<Whirl> const& whirls = Whirls(bot);
    return !whirls.empty() && WhirlGap(whirls, bot, bot->GetGUID()) < WHIRLWIND_KEEP_EXTRA;
}

bool RcAq40WhirlwindKeepAwayAction::Execute(Event /*event*/)
{
    std::vector<Whirl> whirls = Whirls(bot);
    ObjectGuid const me = bot->GetGUID();
    whirls.erase(std::remove_if(whirls.begin(), whirls.end(), [me](Whirl const& w) { return w.victim == me; }),
                 whirls.end());
    if (whirls.empty() || WhirlGap(whirls, bot) >= WHIRLWIND_KEEP_EXTRA)
        return false;
    auto const gapAt = [&whirls](float x, float y, float /*z*/) { return WhirlGap(whirls, x, y); };
    float const reach = std::max(SarturaReach(), GuardReach());
    // Up to the whole whirlwind across from the middle of it.
    return _walk.Go(gapAt, WHIRLWIND_KEEP_EXTRA, WHIRLWIND_SPOT_EXTRA, WHIRLWIND_WORTH_IT,
                    reach + WHIRLWIND_SPOT_EXTRA + 3.0f, whirls.size() == 1);
}

float RcAq40WhirlwindHoldMultiplier::GetValue(Action* action)
{
    Unit* target = RaidClear::AhnQiraj::WalkTarget(action, bot);
    if (!target)
        return 1.0f;
    std::vector<Whirl> const& whirls = Whirls(bot);
    if (whirls.empty() || WhirlGap(whirls, bot) >= WHIRLWIND_HOLD_EXTRA)
        return 1.0f;
    return WhirlGap(whirls, target) < WHIRLWIND_KEEP_EXTRA ? 0.0f : 1.0f;
}

// --- Stuns ----------------------------------------------------------------

bool RcAq40SarturaStunTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    char const* spell = nullptr;
    return StunTarget(botAI, bot, spell) != nullptr;
}

bool RcAq40SarturaStunAction::Execute(Event /*event*/)
{
    char const* spell = nullptr;
    Unit* target = StunTarget(botAI, bot, spell);
    if (!target || !spell || !botAI->CastSpell(spell, target))
        return false;
    uint32 const now = getMSTime();
    auto& sent = StunsSent();
    // Guards that died with a stamp on them: dropped once it has run out.
    for (auto it = sent.begin(); it != sent.end();)
        it = getMSTimeDiff(it->second, now) >= STUN_SPREAD_MS ? sent.erase(it) : std::next(it);
    sent[target->GetGUID()] = now ? now : 1;
    return true;
}
