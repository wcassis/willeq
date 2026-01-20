#ifndef EQT_GRAPHICS_TREE_WIND_CONTROLLER_H
#define EQT_GRAPHICS_TREE_WIND_CONTROLLER_H

#include "client/graphics/tree_wind_config.h"
#include <irrlicht.h>
#include <string>

namespace EQT {
namespace Graphics {

/**
 * Controls wind-driven animation for trees.
 *
 * Calculates per-vertex displacement based on:
 * - Height within the tree (base fixed, top moves most)
 * - Current weather conditions
 * - Time-based oscillation with gusts and turbulence
 * - Spatial variation for natural wave effects
 */
class TreeWindController {
public:
    TreeWindController();
    ~TreeWindController() = default;

    /**
     * Load wind configuration from file.
     * Uses fallback chain: cmdline -> zone-specific -> default -> built-in.
     * @param configPath Command-line specified config path (optional)
     * @param zoneName Current zone name for zone-specific config (optional)
     * @return true if config loaded successfully
     */
    bool loadConfig(const std::string& configPath = "", const std::string& zoneName = "");

    /**
     * Set the current weather type.
     * Weather affects wind intensity with smooth transitions.
     * @param weather The new weather type
     */
    void setWeather(WeatherType weather);

    /**
     * Get the current weather type.
     */
    WeatherType getWeather() const { return currentWeather_; }

    /**
     * Update wind state (call each frame).
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    /**
     * Calculate displacement for a vertex at given world position and height.
     *
     * @param worldPos World position of the vertex
     * @param normalizedHeight Height within the tree, 0 = base, 1 = top
     * @param meshSeed Unique seed for this tree mesh (for variation between trees)
     * @return Displacement vector to apply to the vertex
     */
    irr::core::vector3df getDisplacement(
        const irr::core::vector3df& worldPos,
        float normalizedHeight,
        float meshSeed) const;

    /**
     * Get current effective wind strength (including weather multiplier).
     * @return Current wind strength value
     */
    float getCurrentStrength() const;

    /**
     * Check if tree wind animation is enabled.
     */
    bool isEnabled() const { return config_.enabled; }

    /**
     * Set enabled state.
     */
    void setEnabled(bool enabled) { config_.enabled = enabled; }

    /**
     * Get the current configuration.
     */
    const TreeWindConfig& getConfig() const { return config_; }

    /**
     * Set the configuration directly.
     */
    void setConfig(const TreeWindConfig& config);

    /**
     * Get animation time (for debugging).
     */
    float getTime() const { return time_; }

    /**
     * Get current weather multiplier (for debugging).
     */
    float getWeatherMultiplier() const { return weatherMultiplier_; }

    /**
     * Set wind direction (normalized XZ plane direction).
     * @param direction Wind direction vector (will be normalized)
     */
    void setWindDirection(const irr::core::vector2df& direction);

    /**
     * Get current wind direction.
     */
    irr::core::vector2df getWindDirection() const { return windDirection_; }

private:
    // Calculate influence factor based on normalized height
    float calculateInfluence(float normalizedHeight) const;

    // Update weather multiplier transition
    void updateWeatherTransition(float deltaTime);

    TreeWindConfig config_;
    WeatherType currentWeather_ = WeatherType::Normal;
    float time_ = 0.0f;

    // Current and target weather multiplier for smooth transitions
    float weatherMultiplier_ = 1.0f;
    float targetMultiplier_ = 1.0f;
    float transitionSpeed_ = 0.5f;  // Units per second

    // Wind direction in XZ plane (normalized)
    irr::core::vector2df windDirection_{1.0f, 0.0f};

    // Mathematical constant
    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_TREE_WIND_CONTROLLER_H
