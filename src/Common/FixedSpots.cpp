/*
 * mod-raid-clear: fixed spots from the config. See FixedSpots.h.
 *
 * Released under the MIT License.
 */

#include "FixedSpots.h"

#include "Common/WayOut.h"

#include "AttackAction.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MovementActions.h"
#include "Player.h"
#include "ReachTargetActions.h"

#include <cmath>
#include <cstdlib>
#include <regex>
#include <vector>

namespace
{
    // A player's eyes above the floor it stands on.
    constexpr float EYE_HEIGHT = 2.0f;
}

std::optional<Position> RaidClear::Spots::Parse(std::string const& key, std::string const& text)
{
    if (text.empty())
        return std::nullopt;
    static std::regex const number(R"(-?\d+(?:\.\d+)?)");
    std::vector<float> values;
    bool bad = false;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), number); it != std::sregex_iterator() && values.size() < 3;
         ++it)
    {
        // strtof, not stof: a number too long for a float throws from stof, and this runs in the
        // config load.
        std::string const word = it->str();
        float const value = std::strtof(word.c_str(), nullptr);
        if (!std::isfinite(value) || std::fabs(value) > 100000.0f)
            bad = true;
        values.push_back(value);
    }
    if (bad || values.size() < 3)
    {
        LOG_ERROR("module", "mod-raid-clear: {} = \"{}\" isn't \"x y z\"; the spot is ignored.", key, text);
        return std::nullopt;
    }
    return Position(values[0], values[1], values[2]);
}

std::optional<Position> RaidClear::Spots::Load(std::string const& key)
{
    return Parse(key, sConfigMgr->GetOption<std::string>(key, "", false));
}

Position RaidClear::Spots::Scatter(Player* bot, Position const& spot)
{
    uint32 const n = bot->GetGUID().GetCounter();
    float const angle = float(n % 16) * 2.0f * float(M_PI) / 16.0f;
    float const radius = SCATTER * float(1 + (n / 16) % 3) / 3.0f;
    float const x = spot.GetPositionX() + std::cos(angle) * radius;
    float const y = spot.GetPositionY() + std::sin(angle) * radius;
    Map* map = bot->GetMap();
    float const z = map->GetHeight(bot->GetPhaseMask(), x, y, spot.GetPositionZ() + WAY_OUT_UP);
    if (z <= INVALID_HEIGHT || std::fabs(z - spot.GetPositionZ()) > WAY_OUT_UP)
        return spot;
    if (!map->isInLineOfSight(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ() + EYE_HEIGHT, x, y,
                              z + EYE_HEIGHT, bot->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
        return spot;
    return Position(x, y, z);
}

bool RaidClear::Spots::OnTheWay(Player* bot, Position const& to)
{
    // A path that can't reach the spot ends short of it, so the walk's end is only checked to be
    // near.
    if (!bot->isMoving() || bot->movespline->Finalized())
        return false;
    G3D::Vector3 const dest = bot->movespline->FinalDestination();
    return to.GetExactDist2d(dest.x, dest.y) < 3.0f;
}

bool RaidClear::Spots::Walks(Action* action)
{
    return (dynamic_cast<MovementAction*>(action) && !dynamic_cast<AttackAction*>(action)) ||
           dynamic_cast<CastReachTargetSpellAction*>(action);
}

bool RaidClear::Spots::IsDodge(Action* action)
{
    std::string const name = action->getName();
    return name.rfind("rc ", 0) == 0 || name == "avoid aoe";
}
