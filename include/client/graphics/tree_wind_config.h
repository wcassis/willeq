#ifndef EQT_GRAPHICS_TREE_WIND_CONFIG_H
#define EQT_GRAPHICS_TREE_WIND_CONFIG_H

#include <string>
#include <cstdint>

namespace EQT {
namespace Graphics {

// Weather types that affect wind intensity
enum class WeatherType : uint8_t {
    Calm,       // Light breeze
    Normal,     // Standard conditions
    Rain,       // Increased wind
    Storm       // Heavy wind, gusts
};

// Configuration for tree wind animation
struct TreeWindConfig {
    // Base wind parameters
    float baseStrength = 0.3f;        // Base sway amplitude (units)
    float baseFrequency = 0.4f;       // Oscillations per second
    float gustStrength = 0.5f;        // Additional gust amplitude
    float gustFrequency = 0.1f;       // Gust cycle frequency
    float turbulence = 0.2f;          // Random variation factor

    // Height-based influence curve
    float influenceStartHeight = 0.3f; // Normalized height where sway begins (0-1)
    float influenceExponent = 2.0f;    // Power curve for influence falloff

    // Weather multipliers
    float calmMultiplier = 0.5f;
    float normalMultiplier = 1.0f;
    float rainMultiplier = 1.5f;
    float stormMultiplier = 2.5f;

    // Performance settings
    float updateDistance = 300.0f;    // Max distance to animate trees
    float lodDistance = 150.0f;       // Distance for reduced animation quality
    bool enabled = true;

    // Get weather multiplier for a given weather type
    float getWeatherMultiplier(WeatherType weather) const {
        switch (weather) {
            case WeatherType::Calm:   return calmMultiplier;
            case WeatherType::Normal: return normalMultiplier;
            case WeatherType::Rain:   return rainMultiplier;
            case WeatherType::Storm:  return stormMultiplier;
            default:                  return normalMultiplier;
        }
    }
};

// Loader class for TreeWindConfig
class TreeWindConfigLoader {
public:
    // Load config from JSON file
    // Returns true on success, false on failure (default values used)
    static bool loadFromFile(const std::string& filepath, TreeWindConfig& config);

    // Load config with fallback chain:
    // 1. Command line specified path
    // 2. Zone-specific: data/config/zones/<zoneName>/tree_wind.json
    // 3. Default: data/config/tree_wind.json
    // 4. Built-in defaults
    static bool loadConfig(TreeWindConfig& config,
                          const std::string& cmdLinePath = "",
                          const std::string& zoneName = "");

    // Save config to JSON file (for generating default config)
    static bool saveToFile(const std::string& filepath, const TreeWindConfig& config);

private:
    static std::string findConfigFile(const std::string& cmdLinePath,
                                      const std::string& zoneName);
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_TREE_WIND_CONFIG_H
