/*
 * mod-raid-clear: Blackwing Lair, Chromaggus' Elemental Shield. See BwlRaidClear.h.
 *
 * Every 17-25s he puts up one of five shields (22277-22281). Each takes 76% off Holy, Fire,
 * Nature, Frost, Shadow and Arcane damage and adds 1100% to one of the five, so that school hits
 * about 2.9 times as hard and the others a quarter as hard: a twelvefold gap. Playerbots casts
 * its usual rotation whatever he has up, so most spells landed for a quarter.
 *
 * A ranged damage dealer on him now casts its spells of the weak school first (the trigger per
 * class and school), and while it has one it can cast it leaves its damage spells of the other
 * schools alone (the multiplier). With none to cast (a warlock while he is weak to frost, Immolate
 * already on him) it goes on with its rotation: a quarter of the damage is more than none.
 * A weak-school spell only counts while he is in its reach. Physical damage is untouched by the
 * shield, and so is everything that deals none (curses, Hunter's Mark). Healers and melee are
 * left alone: a cat or an enhancement shaman would leave its form or its weapons for a few spells.
 *
 * Released under the MIT License.
 */

#include "BwlRaidClear.h"

#include "Creature.h"
#include "GenericSpellActions.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

using namespace RaidClear::BlackwingLair;

uint32 RaidClear::BlackwingLair::ChromaggusWeakSchool(Unit const* unit)
{
    if (!unit || unit->GetEntry() != NPC_CHROMAGGUS || !unit->IsAlive())
        return 0;
    static std::pair<uint32, uint32> const shields[] = {
        { SPELL_FIRE_ELEMENTAL_SHIELD, SPELL_SCHOOL_MASK_FIRE },
        { SPELL_FROST_ELEMENTAL_SHIELD, SPELL_SCHOOL_MASK_FROST },
        { SPELL_SHADOW_ELEMENTAL_SHIELD, SPELL_SCHOOL_MASK_SHADOW },
        { SPELL_NATURE_ELEMENTAL_SHIELD, SPELL_SCHOOL_MASK_NATURE },
        { SPELL_ARCANE_ELEMENTAL_SHIELD, SPELL_SCHOOL_MASK_ARCANE },
    };
    for (auto const& [shield, school] : shields)
        if (unit->HasAura(shield))
            return school;
    return 0;
}

std::vector<ChromaggusSchoolSpells> const& RaidClear::BlackwingLair::ChromaggusSchoolSpellList()
{
    // Best first: instants and short cooldowns ahead of the filler they share the school with.
    // Spells a bot hasn't learned (Arcane Blast and Barrage at 60) are simply never possible.
    // Arcane Shot and Frost Shock are this module's plain casts at the current target
    // (RaidClearContexts.h).
    // Not Searing Pain (its threat pulls him off the tanks), Soul Fire (a shard and a 6s cast)
    // or Scorch (Fireball hits harder per second).
    static std::vector<ChromaggusSchoolSpells> const list = {
        { CLASS_MAGE, SPELL_SCHOOL_MASK_FIRE, "rc bwl chromaggus mage fire", { "fire blast", "fireball" } },
        { CLASS_MAGE, SPELL_SCHOOL_MASK_FROST, "rc bwl chromaggus mage frost", { "frostbolt" } },
        { CLASS_MAGE, SPELL_SCHOOL_MASK_ARCANE, "rc bwl chromaggus mage arcane",
          { "arcane barrage", "arcane blast", "arcane missiles" } },
        { CLASS_WARLOCK, SPELL_SCHOOL_MASK_FIRE, "rc bwl chromaggus warlock fire", { "immolate" } },
        { CLASS_WARLOCK, SPELL_SCHOOL_MASK_SHADOW, "rc bwl chromaggus warlock shadow", { "shadow bolt" } },
        { CLASS_SHAMAN, SPELL_SCHOOL_MASK_NATURE, "rc bwl chromaggus shaman nature",
          { "earth shock", "chain lightning", "lightning bolt" } },
        { CLASS_SHAMAN, SPELL_SCHOOL_MASK_FIRE, "rc bwl chromaggus shaman fire", { "flame shock" } },
        { CLASS_SHAMAN, SPELL_SCHOOL_MASK_FROST, "rc bwl chromaggus shaman frost", { "rc bwl frost shock" } },
        { CLASS_PRIEST, SPELL_SCHOOL_MASK_SHADOW, "rc bwl chromaggus priest shadow", { "mind blast", "mind flay" } },
        { CLASS_DRUID, SPELL_SCHOOL_MASK_NATURE, "rc bwl chromaggus druid nature", { "insect swarm", "wrath" } },
        { CLASS_DRUID, SPELL_SCHOOL_MASK_ARCANE, "rc bwl chromaggus druid arcane", { "moonfire", "starfire" } },
        { CLASS_HUNTER, SPELL_SCHOOL_MASK_ARCANE, "rc bwl chromaggus hunter arcane", { "rc bwl arcane shot" } },
        { CLASS_HUNTER, SPELL_SCHOOL_MASK_NATURE, "rc bwl chromaggus hunter nature", { "serpent sting" } },
    };
    return list;
}

namespace
{
    // The bots this is for: damage dealers that fight from range (casters, hunters).
    bool CastsAtRange(Player* bot) { return PlayerbotAI::IsRangedDps(bot); }

    // The bot's target, when that is Chromaggus with a shield up: his weak school, else 0.
    uint32 TargetWeakSchool(PlayerbotAI* botAI, Player* bot)
    {
        if (bot->GetMapId() != MAP_ID || !bot->IsInCombat())
            return 0;
        return ChromaggusWeakSchool(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get());
    }

    // The spell hurts its target: direct damage, a damage over time, a drain, or a channel that
    // fires damaging missiles (Arcane Missiles).
    bool DealsDamage(SpellInfo const* info)
    {
        for (SpellEffectInfo const& effect : info->Effects)
        {
            if (effect.IsEffect(SPELL_EFFECT_SCHOOL_DAMAGE) || effect.IsEffect(SPELL_EFFECT_HEALTH_LEECH) ||
                effect.IsEffect(SPELL_EFFECT_TRIGGER_MISSILE) || effect.IsAura(SPELL_AURA_PERIODIC_DAMAGE) ||
                effect.IsAura(SPELL_AURA_PERIODIC_LEECH) || effect.IsAura(SPELL_AURA_PERIODIC_TRIGGER_SPELL))
                return true;
        }
        return false;
    }

    // One of the bot's spells of that school would land on him now: learned, off cooldown, not
    // already on him where it is a damage over time, aimed at him, and in reach (playerbots
    // counts a spell out of range as castable, and the 20 yd shocks are from where ranged stand).
    bool HasSpellOfSchool(PlayerbotAI* botAI, Player* bot, Unit* target, uint32 school)
    {
        for (ChromaggusSchoolSpells const& entry : ChromaggusSchoolSpellList())
        {
            if (entry.cls != bot->getClass() || entry.school != school)
                continue;
            for (char const* name : entry.spells)
            {
                CastSpellAction* cast = dynamic_cast<CastSpellAction*>(botAI->GetAiObjectContext()->GetAction(name));
                if (!cast || cast->GetTarget() != target)
                    continue;
                uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", cast->getSpell())->Get();
                SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
                if (!info || bot->GetDistance(target) > info->GetMaxRange(false, bot))
                    continue;
                if (cast->isUseful() && cast->isPossible())
                    return true;
            }
        }
        return false;
    }
}

bool RcBwlChromaggusWeakTrigger::IsActive()
{
    uint32 const school = TargetWeakSchool(botAI, bot);
    if (!school || !CastsAtRange(bot))
        return false;
    for (ChromaggusSchoolSpells const& entry : ChromaggusSchoolSpellList())
        if (getName() == entry.trigger)
            return entry.cls == bot->getClass() && entry.school == school;
    return false;
}

float RcBwlChromaggusSchoolMultiplier::GetValue(Action* action)
{
    // The map and the target first: this runs for every action of every bot in the raid.
    uint32 const school = TargetWeakSchool(botAI, bot);
    if (!school)
        return 1.0f;
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    Unit* const target = AI_VALUE(Unit*, "current target");
    if (!cast || !CastsAtRange(bot) || action->GetTarget() != target)
        return 1.0f;

    uint32 const spellId = AI_VALUE2(uint32, "spell id", cast->getSpell());
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    if (!info || !info->NeedsExplicitUnitTarget() || !DealsDamage(info))
        return 1.0f;
    uint32 const mask = info->GetSchoolMask();
    // Physical (his shield doesn't touch it), or a spell of the weak school.
    if ((mask & SPELL_SCHOOL_MASK_NORMAL) || (mask & school))
        return 1.0f;
    uint32 const now = getMSTime();
    if (!_checkedMs || getMSTimeDiff(_checkedMs, now) > 100 || school != _checkedSchool)
    {
        _checkedMs = now;
        _checkedSchool = school;
        _hasSpell = HasSpellOfSchool(botAI, bot, target, school);
    }
    return _hasSpell ? 0.0f : 1.0f;
}
