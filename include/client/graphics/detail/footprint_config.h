#ifndef EQT_GRAPHICS_DETAIL_FOOTPRINT_CONFIG_H
#define EQT_GRAPHICS_DETAIL_FOOTPRINT_CONFIG_H

#include <json/json.h>

namespace EQT {
namespace Graphics {
namespace Detail {

// Configuration for footprint system
// All values can be tuned via JSON config
struct FootprintConfig {
    bool enabled = true;

    // Timing
    float fadeDuration = 15.0f;       // Seconds until fully faded
    float placementInterval = 1.5f;   // Min distance between footprints (units)

    // Appearance
    float footprintSize = 1.2f;       // Base size of footprint quad
    float groundOffset = 0.05f;       // Height above ground to prevent z-fighting
    float playerHeightOffset = 3.0f;  // Offset from player center to ground (half player height)

    // Surfaces that show footprints (soft surfaces only by default)
    bool showOnGrass = true;
    bool showOnDirt = true;
    bool showOnSand = true;
    bool showOnSnow = true;
    bool showOnSwamp = true;
    bool showOnJungle = true;

    // Load config from JSON, using defaults for missing values
    static FootprintConfig loadFromJson(const Json::Value& json) {
        FootprintConfig config;

        if (json.isNull()) return config;  // Use all defaults

        config.enabled = json.get("enabled", config.enabled).asBool();
        config.fadeDuration = json.get("fade_duration", config.fadeDuration).asFloat();
        config.placementInterval = json.get("placement_interval", config.placementInterval).asFloat();
        config.footprintSize = json.get("footprint_size", config.footprintSize).asFloat();
        config.groundOffset = json.get("ground_offset", config.groundOffset).asFloat();
        config.playerHeightOffset = json.get("player_height_offset", config.playerHeightOffset).asFloat();
        config.showOnGrass = json.get("show_on_grass", config.showOnGrass).asBool();
        config.showOnDirt = json.get("show_on_dirt", config.showOnDirt).asBool();
        config.showOnSand = json.get("show_on_sand", config.showOnSand).asBool();
        config.showOnSnow = json.get("show_on_snow", config.showOnSnow).asBool();
        config.showOnSwamp = json.get("show_on_swamp", config.showOnSwamp).asBool();
        config.showOnJungle = json.get("show_on_jungle", config.showOnJungle).asBool();

        return config;
    }
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_FOOTPRINT_CONFIG_H
