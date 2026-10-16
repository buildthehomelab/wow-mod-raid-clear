/*
 * mod-raid-clear: raid members an enemy has mind-controlled. See Charmed.h.
 *
 * Released under the MIT License.
 */

#include "Charmed.h"

#include "Common/Interrupts.h"
#include "Common/SplashSpells.h"

#include "GenericSpellActions.h"
#include "Group.h"
#include "Pet.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

bool RaidClear::Charmed::IsCharmedRaidMember(Player* bot, Unit const* unit)
{
    Player const* player = unit ? unit->ToPlayer() : nullptr;
    Group const* group = bot->GetGroup();
    return player && player != bot && player->IsCharmed() && group && group->IsMember(player->GetGUID());
}

bool RaidClear::Charmed::IsCharmedByEnemy(Player const* bot)
{
    Unit const* charmer = bot->IsCharmed() ? bot->GetCharmer() : nullptr;
    return charmer && !charmer->IsControlledByPlayer();
}

using RaidClear::Charmed::IsCharmedRaidMember;

// --- The member itself --------------------------------------------------------

float RcCharmedSelfMultiplier::GetValue(Action* /*action*/)
{
    return RaidClear::Charmed::IsCharmedByEnemy(bot) ? 0.0f : 1.0f;
}

// --- Area spells --------------------------------------------------------------

float RcCharmedSplashMultiplier::GetValue(Action* action)
{
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || !bot->IsInCombat() || !RaidClear::Splash::IsSplashSpell(botAI, cast->getSpell()))
        return 1.0f;
    Group* group = bot->GetGroup();
    if (!group)
        return 1.0f;

    // Where the spell lands: on its target, or around the bot itself (Arcane Explosion).
    Unit* target = action->GetTarget();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || !member->IsCharmed() || !member->IsInMap(bot))
            continue;
        if (member->GetExactDist(bot) <= RaidClear::Charmed::SPLASH_RANGE ||
            (target && member->GetExactDist(target) <= RaidClear::Charmed::SPLASH_RANGE))
            return 0.0f;
    }
    return 1.0f;
}

// --- Stopping the member ------------------------------------------------------

namespace
{
    struct Control
    {
        char const* spell;  // playerbots name
        float range;
        int32 needMs;       // how much longer the mind control has to run for this to be worth it
    };
    // What stops a raid member an enemy has taken over without hurting it. On a player Polymorph
    // lasts 10 s (after a 1.5 s cast) and Hammer of Justice 6 s; whichever is still on the member
    // when the mind control ends is taken off again by the bot that cast it. Not Blind: nothing
    // takes that off.
    constexpr std::array<Control, 2> CONTROLS = {{
        { "polymorph", 30.0f, 6000 },
        { "hammer of justice", 10.0f, 3000 },
    }};
    // A hold with no end of its own (Brain Wash lasts as long as its totem stands, and the totem
    // dies within seconds) counts as a short one: only the stun is used on it.
    constexpr int32 CHARM_NO_END_MS = 5000;
    // One bot at a time per member: whoever starts first has this long to land it.
    constexpr uint32 CONTROL_CLAIM_MS = 2500;

    struct ControlClaim
    {
        ObjectGuid by;
        uint32 atMs = 0;
    };
    // By mind-controlled member. Bots of one raid think on that raid's map update, but which
    // thread runs that update changes from one update to the next.
    std::mutex sControlClaimsMutex;
    std::unordered_map<ObjectGuid, ControlClaim> sControlClaims;

    bool ControlClaimedByOther(Player const* bot, Player const* member)
    {
        std::lock_guard<std::mutex> lock(sControlClaimsMutex);
        auto it = sControlClaims.find(member->GetGUID());
        return it != sControlClaims.end() && it->second.by != bot->GetGUID() &&
               getMSTimeDiff(it->second.atMs, getMSTime()) < CONTROL_CLAIM_MS;
    }

    void ClaimControl(Player const* bot, Player const* member)
    {
        std::lock_guard<std::mutex> lock(sControlClaimsMutex);
        if (sControlClaims.size() > 64)
            sControlClaims.clear();
        sControlClaims[member->GetGUID()] = { bot->GetGUID(), getMSTime() };
    }

    void DropControlClaim(Player const* member)
    {
        std::lock_guard<std::mutex> lock(sControlClaimsMutex);
        sControlClaims.erase(member->GetGUID());
    }

    // How much longer an enemy's hold on this member runs, in ms.
    int32 CharmLeftMs(Player const* member)
    {
        int32 left = 0;
        for (AuraType const type : { SPELL_AURA_MOD_CHARM, SPELL_AURA_MOD_POSSESS, SPELL_AURA_AOE_CHARM })
            for (AuraEffect const* effect : member->GetAuraEffectsByType(type))
            {
                int32 const duration = effect->GetBase()->GetDuration();
                left = std::max(left, duration < 0 ? CHARM_NO_END_MS : duration);
            }
        return left;
    }

    // A mind-controlled member of the raid this bot can stop right now, and with what. Nobody
    // who is stopped already, and nobody another bot has just started on.
    Player* MemberToControl(PlayerbotAI* botAI, Player* bot, char const*& with)
    {
        Group* group = bot->GetGroup();
        if (!group || !bot->IsAlive() || !bot->IsInCombat() || bot->IsCharmed())
            return nullptr;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || !member->IsCharmed() || !member->IsInMap(bot))
                continue;
            if (member->HasUnitState(UNIT_STATE_CONTROLLED) || ControlClaimedByOther(bot, member))
                continue;

            int32 const left = CharmLeftMs(member);
            float const dist = bot->GetDistance(member);
            for (Control const& control : CONTROLS)
            {
                if (left < control.needMs || dist > control.range)
                    continue;
                if (!bot->IsWithinLOSInMap(member) || !botAI->CanCastSpell(control.spell, member) ||
                    !RaidClear::CanPayFor(botAI, bot, control.spell))
                    continue;
                with = control.spell;
                return member;
            }
        }
        return nullptr;
    }

    // The bot's own stuns and Polymorphs on members of its raid who are themselves again (the mind
    // control ran out first, or whatever cast it died).
    std::vector<std::pair<Player*, uint32>> ControlsToLetGo(Player* bot)
    {
        std::vector<std::pair<Player*, uint32>> spells;
        Group* group = bot->GetGroup();
        if (!group)
            return spells;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || member->IsCharmed() || !member->IsInMap(bot) ||
                !member->HasUnitState(UNIT_STATE_CONTROLLED))
                continue;
            for (AuraType const type : { SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_STUN })
                for (AuraEffect const* effect : member->GetAuraEffectsByType(type))
                    if (effect->GetCasterGUID() == bot->GetGUID())
                        spells.emplace_back(member, effect->GetId());
        }
        return spells;
    }

    bool ControlsCharmed(Player const* bot)
    {
        return bot->getClass() == CLASS_MAGE || bot->getClass() == CLASS_PALADIN;
    }
}

bool RcCharmedMemberTrigger::IsActive()
{
    if (!ControlsCharmed(bot) || !bot->IsAlive())
        return false;
    char const* with = nullptr;
    return !ControlsToLetGo(bot).empty() || MemberToControl(botAI, bot, with) != nullptr;
}

bool RcControlCharmedAction::Execute(Event /*event*/)
{
    if (!ControlsCharmed(bot))
        return false;

    // A member who is back before the sheep or the stun has worn off: let go of it. It isn't this
    // bot's whole turn.
    for (auto const& [freed, spellId] : ControlsToLetGo(bot))
        freed->RemoveAurasDueToSpell(spellId, bot->GetGUID());

    char const* with = nullptr;
    Player* member = MemberToControl(botAI, bot, with);
    if (!member)
        return false;

    ClaimControl(bot, member);
    if (botAI->CastSpell(with, member))
        return true;
    DropControlClaim(member);
    return false;
}

// --- Letting go of a target ---------------------------------------------------

bool RcLeaveTargetAction::Execute(Event /*event*/)
{
    if (Pet* pet = bot->GetPet())
    {
        if (IsBadPetTarget(pet->GetVictim()))
        {
            pet->AttackStop();
            pet->InterruptNonMeleeSpells(false);
        }
    }

    // Stopping the pet alone isn't the bot's action for this tick: a pet that is being hit goes
    // straight back, and its owner would do nothing else.
    Unit* current = AI_VALUE(Unit*, "current target");
    if (!IsBadTarget(current))
        return false;

    // What playerbots would pick now, if the bot can attack it from here.
    Unit* next = AI_VALUE(Unit*, PlayerbotAI::IsTank(bot) ? "tank target" : "dps target");
    if (next && next != current && next->IsAlive() && !IsBadTarget(next) && Attack(next))
        return true;

    // That can be the bad target itself (skull is picked before anything is skipped): then the
    // nearest enemy the bot can hurt.
    Unit* nearest = nullptr;
    float nearestDist = 0.0f;
    for (ObjectGuid const& guid : AI_VALUE(GuidVector, "attackers"))
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || unit == current || !unit->IsAlive() || IsBadTarget(unit) || !bot->IsValidAttackTarget(unit))
            continue;
        float const dist = bot->GetDistance(unit);
        if ((nearest && dist >= nearestDist) || !bot->IsWithinLOSInMap(unit))
            continue;
        nearest = unit;
        nearestDist = dist;
    }
    if (nearest && Attack(nearest))
        return true;

    // Nothing else to hit: let go, and playerbots picks again once there is.
    context->GetValue<Unit*>("current target")->Set(nullptr);
    bot->SetTarget(ObjectGuid::Empty);
    bot->SetSelection(ObjectGuid::Empty);
    bot->AttackStop();
    bot->InterruptNonMeleeSpells(false);
    return true;
}

bool RcCharmedTargetTrigger::IsActive()
{
    if (!bot->IsAlive() || !bot->IsInCombat())
        return false;
    if (IsCharmedRaidMember(bot, AI_VALUE(Unit*, "current target")))
        return true;
    Pet* pet = bot->GetPet();
    return pet && IsCharmedRaidMember(bot, pet->GetVictim());
}

bool RcLeaveCharmedTargetAction::IsBadTarget(Unit const* unit)
{
    return IsCharmedRaidMember(bot, unit);
}

bool RcLeaveCharmedTargetAction::IsBadPetTarget(Unit const* unit)
{
    return IsCharmedRaidMember(bot, unit);
}
