/*
 * mod-raid-clear: which of a bot's spells reach more than the enemy they are cast at, and how far.
 *
 * Released under the MIT License.
 */

#ifndef MOD_RAID_CLEAR_SPLASH_SPELLS_H
#define MOD_RAID_CLEAR_SPLASH_SPELLS_H

#include "Define.h"

#include <string>

class Player;
class PlayerbotAI;

namespace RaidClear::Splash
{
    // A harmful spell, by its playerbots name, that hits more than its target: area and cone
    // spells, ground effects (Blizzard, Rain of Fire, Volley, Consecration), chains (Multi-Shot,
    // Cleave, Chain Lightning), and the ones whose damage sits in a spell they trigger or in
    // something they summon (Starfall, Magma Totem, Explosive Trap).
    bool IsSplashSpell(PlayerbotAI* botAI, std::string const& name);

    // How far such a spell reaches, in yards from the middle of the caster and from the middle of
    // its target (or the spot on the ground it is aimed at). 0 = not at all. A chain that bounces
    // on from each enemy it hits (Chain Lightning, Avenger's Shield) makes up to `chainHops`
    // jumps of `chainJump` yards after its target; one that only takes enemies next to its target
    // (Cleave, Multi-Shot) counts as an area around the target.
    struct Reach
    {
        float aroundCaster = 0.0f;
        float aroundTarget = 0.0f;
        float chainJump = 0.0f;
        uint32 chainHops = 0;

        bool Any() const { return aroundCaster > 0.0f || aroundTarget > 0.0f || chainHops > 0; }
    };
    Reach ReachOf(PlayerbotAI* botAI, Player* bot, std::string const& name);
}

#endif
