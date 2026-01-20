#ifndef EQT_GRAPHICS_DETAIL_SEASONAL_CONTROLLER_H
#define EQT_GRAPHICS_DETAIL_SEASONAL_CONTROLLER_H

#include <irrlicht.h>
#include <string>
#include <map>
#include "client/graphics/detail/detail_types.h"

namespace EQT {
namespace Graphics {
namespace Detail {

// Maps zones to seasons based on name patterns
class SeasonalController {
public:
    SeasonalController();
    ~SeasonalController() = default;

    // Determine season for a zone based on zone name
    Season detectSeason(const std::string& zoneName) const;

    // Get color tint for season (applied to vertex colors)
    irr::video::SColor getSeasonTint(Season season) const;

    // Get density multiplier for season (deserts have less vegetation)
    float getSeasonDensityMultiplier(Season season) const;

    // Override automatic detection
    void setSeasonOverride(Season season) { overrideSeason_ = season; hasOverride_ = true; }
    void clearOverride() { hasOverride_ = false; }
    bool hasOverride() const { return hasOverride_; }
    Season getOverride() const { return overrideSeason_; }

    // Get season name for display
    static const char* getSeasonName(Season season);

private:
    void initZoneMappings();

    // Zone name patterns for season detection
    std::map<std::string, Season> zoneSeasons_;

    // Override state
    bool hasOverride_ = false;
    Season overrideSeason_ = Season::Default;
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_SEASONAL_CONTROLLER_H
