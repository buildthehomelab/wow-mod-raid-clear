/*
 * mod-raid-clear: Temple of Ahn'Qiraj, Princess Huhuran. See AqRaidClear.h for the constants.
 *
 *   - Wyvern Sting (26180). Every 25-43 s she stings the 10 raid members nearest to her: 12 s
 *     asleep. A poison, and taken off by anything but time it hits for 3,000 nature (26233, the
 *     script's AURA_REMOVE_BY_ENEMY_SPELL: any dispel). Playerbots cures every poison it sees: in
 *     one bot raid all 28 stings were cured within 1.8 s and five of the sleepers died of it. So no
 *     poison cure goes to anyone with the sting (Cure Poison, Cleanse, Purify, Cure Toxins,
 *     Cleanse Spirit: whatever dispels poison), and no Abolish Poison or Cleansing Totem goes out
 *     at all while she is fighting: both take a poison off seconds later, on their own. A shaman
 *     that has a Cleansing Totem down destroys it (as a player would right-click it away).
 *   - Berserk (26068). From 30% (or after 5 minutes) Poison Bolt hits the 15 raid members nearest
 *     to her every 2 s for about 1,500 nature (the script sorts the targets by distance). Tanks
 *     and players in plate (warriors, paladins, death knights) stay with her and take it; every
 *     other bot stands POISON_BOLT_KEEP away from her middle (still in reach of its spells and
 *     heals), from HUHURAN_BERSERK_PCT on so that nobody is caught walking, and walks no closer.
 *     With fewer than 15 near her the farthest still get hit, but the fewest of them.
 *   - Not done: Noxious Poison (26053) silences and poisons everyone within 15 yd of a random
 *     raid member; spreading 40 bots out 15 yd apart doesn't fit around her. Frenzy is playerbots'
 *     Tranquilizing Shot (Common/RaidDiscipline.h teaches it).
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"

#include "Creature.h"
#include "GenericSpellActions.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Totem.h"

#include <cmath>

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    Creature* FightingHuhuran(Player* bot) { return RaidClear::FightingCreature(bot, { NPC_HUHURAN }); }

    bool DispelsPoison(SpellInfo const* info)
    {
        for (SpellEffectInfo const& effect : info->Effects)
            if (effect.IsEffect(SPELL_EFFECT_DISPEL) && effect.MiscValue == DISPEL_POISON)
                return true;
        return false;
    }

    // A spell that takes a poison off now or seconds later (Abolish Poison, a periodic dispel).
    bool CuresPoison(SpellInfo const* info, bool& later)
    {
        later = false;
        if (DispelsPoison(info))
            return true;
        for (SpellEffectInfo const& effect : info->Effects)
        {
            if (!effect.IsAura(SPELL_AURA_PERIODIC_TRIGGER_SPELL) || !effect.TriggerSpell)
                continue;
            SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell);
            if (triggered && DispelsPoison(triggered))
            {
                later = true;
                return true;
            }
        }
        return false;
    }

    // The bot's Cleansing Totem, if it has one down.
    constexpr uint32 SPELL_CLEANSING_TOTEM = 8170;
    Totem* CleansingTotem(Player* bot)
    {
        for (uint8 slot = SUMMON_SLOT_TOTEM_FIRE; slot < MAX_TOTEM_SLOT; ++slot)
        {
            if (bot->m_SummonSlot[slot].IsEmpty())
                continue;
            Creature* totem = bot->GetMap()->GetCreature(bot->m_SummonSlot[slot]);
            if (totem && totem->IsTotem() && totem->GetUInt32Value(UNIT_CREATED_BY_SPELL) == SPELL_CLEANSING_TOTEM)
                return totem->ToTotem();
        }
        return nullptr;
    }

    // Poison Bolt goes to the nearest: these take it.
    bool StaysWithHer(Player* bot)
    {
        uint8 const cls = bot->getClass();
        return PlayerbotAI::IsTank(bot) || cls == CLASS_WARRIOR || cls == CLASS_PALADIN || cls == CLASS_DEATH_KNIGHT;
    }

    bool PoisonBoltTime(Creature const* boss)
    {
        return boss->HasAura(SPELL_HUHURAN_BERSERK) || boss->GetHealthPct() <= HUHURAN_BERSERK_PCT;
    }

    // Yards from a spot to where the bot may stand (less than 0: too close), centre to centre.
    float BoltGap(Creature const* boss, float x, float y)
    {
        return std::hypot(x - boss->GetPositionX(), y - boss->GetPositionY()) - POISON_BOLT_KEEP;
    }
}

// --- Wyvern Sting ---------------------------------------------------------

float RcAq40WyvernStingMultiplier::GetValue(Action* action)
{
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || !bot->IsInCombat() || !FightingHuhuran(bot))
        return 1.0f;
    std::string const spell = cast->getSpell();
    bool const totem = spell == "cleansing totem";
    uint32 const spellId = totem ? 0 : AI_VALUE2(uint32, "spell id", spell);
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    bool later = false;
    if (!totem && (!info || !CuresPoison(info, later)))
        return 1.0f;
    if (totem || later)
        return 0.0f;
    Unit* target = action->GetTarget();
    return target && target->HasAura(SPELL_WYVERN_STING) ? 0.0f : 1.0f;
}

bool RcAq40CleansingTotemTrigger::IsActive()
{
    return bot->getClass() == CLASS_SHAMAN && bot->IsAlive() && bot->IsInCombat() && CleansingTotem(bot) &&
           FightingHuhuran(bot);
}

bool RcAq40DestroyCleansingTotemAction::Execute(Event /*event*/)
{
    Totem* totem = CleansingTotem(bot);
    if (!totem)
        return false;
    totem->UnSummon();
    return true;
}

// --- Poison Bolt ------------------------------------------------------------

bool RcAq40PoisonBoltTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat() || !botAI->CanMove() || StaysWithHer(bot))
        return false;
    Creature* boss = FightingHuhuran(bot);
    return boss && PoisonBoltTime(boss) && BoltGap(boss, bot->GetPositionX(), bot->GetPositionY()) < 0.0f;
}

bool RcAq40PoisonBoltKeepAwayAction::Execute(Event /*event*/)
{
    Creature* boss = FightingHuhuran(bot);
    if (!boss || !PoisonBoltTime(boss) || BoltGap(boss, bot->GetPositionX(), bot->GetPositionY()) >= 0.0f)
        return false;
    auto const gapAt = [boss](float x, float y, float /*z*/) { return BoltGap(boss, x, y); };
    return _walk.Go(gapAt, 0.0f, POISON_BOLT_SPOT, POISON_BOLT_WORTH_IT, POISON_BOLT_SEARCH, true);
}

float RcAq40PoisonBoltHoldMultiplier::GetValue(Action* action)
{
    Unit* target = RaidClear::AhnQiraj::WalkTarget(action, bot);
    if (!target || target->GetEntry() != NPC_HUHURAN || StaysWithHer(bot))
        return 1.0f;
    Creature* boss = FightingHuhuran(bot);
    if (!boss || boss != target || !PoisonBoltTime(boss))
        return 1.0f;
    // Only walks: a spell with a reach of its own is cast from where the bot stands, or not at all.
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action))
        return 1.0f;
    return BoltGap(boss, bot->GetPositionX(), bot->GetPositionY()) < POISON_BOLT_HOLD_EXTRA ? 0.0f : 1.0f;
}
