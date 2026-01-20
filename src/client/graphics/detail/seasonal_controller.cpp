#include "client/graphics/detail/seasonal_controller.h"
#include <algorithm>
#include <cctype>

namespace EQT {
namespace Graphics {
namespace Detail {

SeasonalController::SeasonalController() {
    initZoneMappings();
}

void SeasonalController::initZoneMappings() {
    // Snow zones (Velious and cold regions)
    zoneSeasons_["everfrost"] = Season::Snow;
    zoneSeasons_["permafrost"] = Season::Snow;
    zoneSeasons_["halas"] = Season::Snow;
    zoneSeasons_["eastwastes"] = Season::Snow;
    zoneSeasons_["cobaltscar"] = Season::Snow;
    zoneSeasons_["wakening"] = Season::Snow;
    zoneSeasons_["westwastes"] = Season::Snow;
    zoneSeasons_["crystal"] = Season::Snow;
    zoneSeasons_["sleeper"] = Season::Snow;
    zoneSeasons_["necropolis"] = Season::Snow;
    zoneSeasons_["templeveeshan"] = Season::Snow;
    zoneSeasons_["thurgadina"] = Season::Snow;
    zoneSeasons_["thurgadinb"] = Season::Snow;
    zoneSeasons_["kael"] = Season::Snow;
    zoneSeasons_["sirens"] = Season::Snow;
    zoneSeasons_["iceclad"] = Season::Snow;
    zoneSeasons_["growthplane"] = Season::Snow;  // Plane of Growth has some snowy areas
    zoneSeasons_["velketor"] = Season::Snow;
    zoneSeasons_["skyshrine"] = Season::Snow;

    // Desert zones (reduced/dry vegetation)
    zoneSeasons_["nro"] = Season::Desert;
    zoneSeasons_["sro"] = Season::Desert;
    zoneSeasons_["oasis"] = Season::Desert;
    zoneSeasons_["scarlet"] = Season::Desert;  // Scarlet Desert

    // Swamp zones (murky green palette)
    zoneSeasons_["innothule"] = Season::Swamp;
    zoneSeasons_["swampofnohope"] = Season::Swamp;
    zoneSeasons_["feerrott"] = Season::Swamp;
    zoneSeasons_["cazicthule"] = Season::Swamp;
    zoneSeasons_["trakanon"] = Season::Swamp;  // Trakanon's Teeth has swampy areas

    // Note: Autumn is not mapped to any zones by default
    // It can be used via season override for testing
}

Season SeasonalController::detectSeason(const std::string& zoneName) const {
    // Check for override first
    if (hasOverride_) {
        return overrideSeason_;
    }

    // Convert zone name to lowercase for matching
    std::string lowerName = zoneName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Look up in zone mappings
    auto it = zoneSeasons_.find(lowerName);
    if (it != zoneSeasons_.end()) {
        return it->second;
    }

    // Default to standard vegetation
    return Season::Default;
}

irr::video::SColor SeasonalController::getSeasonTint(Season season) const {
    switch (season) {
        case Season::Snow:
            // Slight blue/white tint for snow zones
            return irr::video::SColor(255, 200, 220, 255);

        case Season::Desert:
            // Yellowed, dry vegetation
            return irr::video::SColor(255, 255, 230, 180);

        case Season::Swamp:
            // Murky green/brown tint
            return irr::video::SColor(255, 180, 200, 150);

        case Season::Autumn:
            // Orange/brown tint for fall colors
            return irr::video::SColor(255, 255, 200, 150);

        case Season::Default:
        default:
            // No tint - use original colors
            return irr::video::SColor(255, 255, 255, 255);
    }
}

float SeasonalController::getSeasonDensityMultiplier(Season season) const {
    switch (season) {
        case Season::Snow:
            // Reduced vegetation in snow zones
            return 0.4f;

        case Season::Desert:
            // Very sparse vegetation in deserts
            return 0.2f;

        case Season::Swamp:
            // Dense vegetation in swamps
            return 1.2f;

        case Season::Autumn:
            // Normal density
            return 1.0f;

        case Season::Default:
        default:
            return 1.0f;
    }
}

const char* SeasonalController::getSeasonName(Season season) {
    switch (season) {
        case Season::Snow:    return "Snow";
        case Season::Desert:  return "Desert";
        case Season::Swamp:   return "Swamp";
        case Season::Autumn:  return "Autumn";
        case Season::Default:
        default:              return "Default";
    }
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
