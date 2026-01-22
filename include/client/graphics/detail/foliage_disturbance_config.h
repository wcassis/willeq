#ifndef EQT_GRAPHICS_DETAIL_FOLIAGE_DISTURBANCE_CONFIG_H
#define EQT_GRAPHICS_DETAIL_FOLIAGE_DISTURBANCE_CONFIG_H

#include <json/json.h>

namespace EQT {
namespace Graphics {
namespace Detail {

// Configuration for foliage disturbance effect (grass bending when player walks through)
// All values can be tuned via JSON config
struct FoliageDisturbanceConfig {
    bool enabled = true;

    // Player disturbance source
    float playerRadius = 2.5f;        // Effect radius around player (units)
    float playerStrength = 1.0f;      // Base displacement strength (0-1)

    // Displacement behavior
    float maxDisplacement = 0.5f;     // Max horizontal displacement (units)
    float verticalDipFactor = 0.1f;   // Downward bend amount (fraction of horizontal)
    float velocityInfluence = 0.5f;   // How much movement direction affects push (0-1)
    float heightExponent = 2.0f;      // Exponent for height-based falloff (higher = more top-heavy)

    // Recovery
    float recoveryRate = 0.7f;        // Speed of return to neutral (higher = faster, ~1-2s at 0.7)

    // Category filtering
    bool affectGrass = true;
    bool affectPlants = true;

    // Load config from JSON, using defaults for missing values
    static FoliageDisturbanceConfig loadFromJson(const Json::Value& json) {
        FoliageDisturbanceConfig config;

        if (json.isNull()) return config;  // Use all defaults

        config.enabled = json.get("enabled", config.enabled).asBool();
        config.playerRadius = json.get("player_radius", config.playerRadius).asFloat();
        config.playerStrength = json.get("player_strength", config.playerStrength).asFloat();
        config.maxDisplacement = json.get("max_displacement", config.maxDisplacement).asFloat();
        config.verticalDipFactor = json.get("vertical_dip_factor", config.verticalDipFactor).asFloat();
        config.velocityInfluence = json.get("velocity_influence", config.velocityInfluence).asFloat();
        config.heightExponent = json.get("height_exponent", config.heightExponent).asFloat();
        config.recoveryRate = json.get("recovery_rate", config.recoveryRate).asFloat();
        config.affectGrass = json.get("affect_grass", config.affectGrass).asBool();
        config.affectPlants = json.get("affect_plants", config.affectPlants).asBool();

        return config;
    }
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_FOLIAGE_DISTURBANCE_CONFIG_H
