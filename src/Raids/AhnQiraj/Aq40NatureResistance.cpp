/*
 * mod-raid-clear: Temple of Ahn'Qiraj, nature resistance for the nature fights (Viscidus, Princess
 * Huhuran, the Bug Trio). See AqRaidClear.h.
 *
 * Released under the MIT License.
 */

#include "AqRaidClear.h"

#include "Common/BossLookup.h"

#include "Creature.h"
#include "Group.h"
#include "Playerbots.h"

#include <algorithm>
#include <array>
#include <string>

using namespace RaidClear::TempleOfAhnQiraj;

namespace
{
    // The nature fight the bot is in: its boss, or nullptr.
    Creature* NatureBoss(Player* bot)
    {
        if (Creature* viscidus = RaidClear::FightingCreature(bot, { NPC_VISCIDUS }))
            return viscidus;
        if (Creature* huhuran = RaidClear::FightingCreature(bot, { NPC_HUHURAN }))
            return huhuran;
        // Each bug is its own lookup: one with several entries stops at the first spawn.
        for (uint32 const bug : { uint32(NPC_KRI), uint32(NPC_VEM), uint32(NPC_YAUJ) })
            if (Creature* boss = RaidClear::FightingCreature(bot, { bug }))
                return boss;
        return nullptr;
    }

    bool KnowsAspectOfTheWild(Player const* player)
    {
        return std::any_of(std::begin(ASPECT_OF_THE_WILD_RANKS), std::end(ASPECT_OF_THE_WILD_RANKS),
                           [player](uint32 spell) { return player->HasActiveSpell(spell); });
    }

    bool CanHelp(Player const* player, uint8 cls)
    {
        if (player->getClass() != cls)
            return false;
        return cls == CLASS_SHAMAN ? player->HasSpell(SPELL_NATURE_RESISTANCE_TOTEM_RANK_1)
                                   : KnowsAspectOfTheWild(player);
    }

    // Whether the bot is the raid's one shaman (or hunter) for the aura in this fight: the first
    // living bot of its class in group order who knows it and is near the boss.
    bool IsNatureHelper(Player* bot, Creature const* boss)
    {
        uint8 const cls = bot->getClass();
        if ((cls != CLASS_SHAMAN && cls != CLASS_HUNTER) || !CanHelp(bot, cls))
            return false;
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || !member->IsInMap(bot) || !GET_PLAYERBOT_AI(member) ||
                !CanHelp(member, cls) || member->GetExactDist(boss) > NATURE_HELPER_RANGE)
                continue;
            return member == bot;
        }
        return false;
    }
}

bool RcAq40NatureResistanceTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    Creature* boss = NatureBoss(bot);
    if (!boss || !IsNatureHelper(bot, boss))
        return false;
    if (bot->getClass() == CLASS_SHAMAN)
        return !AI_VALUE2(bool, "has totem", "nature resistance") && botAI->CanCastSpell("nature resistance totem", bot);
    return !botAI->HasAura("aspect of the wild", bot) && botAI->CanCastSpell("aspect of the wild", bot);
}

bool RcAq40NatureResistanceAction::Execute(Event /*event*/)
{
    char const* spell = bot->getClass() == CLASS_SHAMAN ? "nature resistance totem" : "aspect of the wild";
    return botAI->CastSpell(spell, bot);
}

float RcAq40NatureResistanceMultiplier::GetValue(Action* action)
{
    uint8 const cls = bot->getClass();
    if (!action || (cls != CLASS_SHAMAN && cls != CLASS_HUNTER) || !bot->IsInCombat())
        return 1.0f;

    // Playerbots' names for whatever puts a totem in the air slot or takes this one away, and for
    // the other aspects.
    static constexpr std::array<char const*, 5> AIR_SLOT = {
        "windfury totem", "wrath of air totem", "grounding totem", "call of the elements", "totemic recall",
    };
    static constexpr std::array<char const*, 7> OTHER_ASPECTS = {
        "aspect of the hawk", "aspect of the dragonhawk", "aspect of the viper", "aspect of the monkey",
        "aspect of the pack", "aspect of the cheetah", "aspect of the beast",
    };
    std::string const name = action->getName();
    auto const named = [&name](char const* other) { return name == other; };
    bool const blocked = cls == CLASS_SHAMAN ? std::any_of(AIR_SLOT.begin(), AIR_SLOT.end(), named)
                                             : std::any_of(OTHER_ASPECTS.begin(), OTHER_ASPECTS.end(), named);
    if (!blocked)
        return 1.0f;
    Creature* boss = NatureBoss(bot);
    return boss && IsNatureHelper(bot, boss) ? 0.0f : 1.0f;
}
