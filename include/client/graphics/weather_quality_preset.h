#pragma once

#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

// Forward declarations
namespace Environment {
struct RainSettings;
struct RainSplashSettings;
struct SnowSettings;
}
struct WeatherEffectsConfig;

/**
 * WeatherQualityPreset - Predefined quality levels for weather effects.
 */
enum class WeatherQualityPreset {
    Low,      // Minimal particles, no advanced effects
    Medium,   // Balanced particles, basic lightning
    High,     // Full particles, all effects enabled
    Ultra,    // Maximum particles, all effects at highest quality
    Custom    // User-defined settings from JSON (no preset overrides)
};

/**
 * WeatherPresetValues - All tunable values for a quality preset.
 */
struct WeatherPresetValues {
    // Rain
    int rainMaxParticles = 500;
    float rainSpawnRate = 200.0f;

    // Rain splash
    int rainSplashMaxParticles = 200;
    float rainSplashSpawnRate = 100.0f;

    // Snow
    int snowMaxParticles = 600;
    float snowSpawnRate = 150.0f;

    // Lightning
    bool lightningEnabled = true;
    int maxBranchLevel = 4;

    // Future features (flags for Phase 7-9)
    bool ripplesEnabled = false;
    bool cloudOverlayEnabled = false;
    bool snowAccumulationEnabled = false;

    // Fog transition speed (seconds to reach target)
    float fogTransitionSpeed = 0.5f;  // Smooth = 0.5, Fast = 0.2, Instant = 0.0
};

/**
 * WeatherQualityManager - Manages quality presets for weather effects.
 *
 * Provides predefined presets and applies them to weather settings.
 */
class WeatherQualityManager {
public:
    /**
     * Get singleton instance.
     */
    static WeatherQualityManager& instance();

    // Non-copyable
    WeatherQualityManager(const WeatherQualityManager&) = delete;
    WeatherQualityManager& operator=(const WeatherQualityManager&) = delete;

    /**
     * Get the current quality preset.
     */
    WeatherQualityPreset getCurrentPreset() const { return currentPreset_; }

    /**
     * Get the current preset name as string.
     */
    std::string getCurrentPresetName() const;

    /**
     * Set quality preset by enum.
     * @param preset The preset to apply
     */
    void setPreset(WeatherQualityPreset preset);

    /**
     * Set quality preset by name (case-insensitive).
     * @param name Preset name: "low", "medium", "high", "ultra", "custom"
     * @return true if valid preset name
     */
    bool setPresetByName(const std::string& name);

    /**
     * Get preset values for a specific preset level.
     */
    const WeatherPresetValues& getPresetValues(WeatherQualityPreset preset) const;

    /**
     * Get current preset values.
     */
    const WeatherPresetValues& getCurrentPresetValues() const {
        return getPresetValues(currentPreset_);
    }

    /**
     * Apply current preset to rain settings.
     * Only modifies particle count and spawn rate fields.
     */
    void applyToRainSettings(Environment::RainSettings& settings) const;

    /**
     * Apply current preset to rain splash settings.
     */
    void applyToRainSplashSettings(Environment::RainSplashSettings& settings) const;

    /**
     * Apply current preset to snow settings.
     */
    void applyToSnowSettings(Environment::SnowSettings& settings) const;

    /**
     * Apply current preset to weather effects config.
     */
    void applyToWeatherConfig(WeatherEffectsConfig& config) const;

    /**
     * Convert preset enum to string.
     */
    static std::string presetToString(WeatherQualityPreset preset);

    /**
     * Convert string to preset enum.
     * @param name Case-insensitive preset name
     * @param outPreset Output preset enum
     * @return true if valid name
     */
    static bool stringToPreset(const std::string& name, WeatherQualityPreset& outPreset);

    /**
     * Get list of all preset names.
     */
    static std::vector<std::string> getAllPresetNames();

private:
    WeatherQualityManager();

    /**
     * Initialize preset values.
     */
    void initializePresets();

    WeatherQualityPreset currentPreset_ = WeatherQualityPreset::High;

    // Preset value storage
    WeatherPresetValues lowPreset_;
    WeatherPresetValues mediumPreset_;
    WeatherPresetValues highPreset_;
    WeatherPresetValues ultraPreset_;
    WeatherPresetValues customPreset_;  // Placeholder, uses JSON values
};

} // namespace Graphics
} // namespace EQT
