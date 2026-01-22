#pragma once

#include "client/graphics/environment/emitters/rain_emitter.h"
#include "client/graphics/environment/emitters/rain_splash_emitter.h"
#include "client/graphics/environment/emitters/snow_emitter.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_quality_preset.h"
#include <functional>
#include <string>

namespace Json {
class Value;
}

namespace EQT {
namespace Graphics {

/**
 * WeatherConfigLoader - Loads weather effect settings from JSON configuration.
 *
 * Singleton class that manages weather-related configuration including:
 * - Rain particle settings
 * - Rain splash settings
 * - Snow particle settings
 * - Storm/lightning settings
 *
 * Supports hot-reload via reload() method with callback notification.
 */
class WeatherConfigLoader {
public:
    /**
     * Get singleton instance.
     */
    static WeatherConfigLoader& instance();

    // Non-copyable
    WeatherConfigLoader(const WeatherConfigLoader&) = delete;
    WeatherConfigLoader& operator=(const WeatherConfigLoader&) = delete;

    /**
     * Load configuration from JSON file.
     * @param path Path to weather_effects.json
     * @return true if loaded successfully (defaults used if file not found)
     */
    bool load(const std::string& path);

    /**
     * Reload configuration from last loaded path.
     * Invokes reload callback if set.
     * @return true if reloaded successfully
     */
    bool reload();

    /**
     * Set callback invoked after successful reload.
     * Use to update emitters with new settings.
     */
    void setReloadCallback(std::function<void()> callback) {
        reloadCallback_ = std::move(callback);
    }

    /**
     * Check if configuration has been loaded.
     */
    bool isLoaded() const { return loaded_; }

    /**
     * Get config file path.
     */
    const std::string& getConfigPath() const { return configPath_; }

    // Accessors for loaded settings
    const Environment::RainSettings& getRainSettings() const { return rainSettings_; }
    const Environment::RainSplashSettings& getRainSplashSettings() const { return rainSplashSettings_; }
    const Environment::SnowSettings& getSnowSettings() const { return snowSettings_; }
    const WeatherEffectsConfig& getWeatherEffectsConfig() const { return weatherConfig_; }

    /**
     * Get the configured quality preset name.
     * Returns empty string if not set.
     */
    const std::string& getQualityPresetName() const { return qualityPreset_; }

    /**
     * Get the current quality preset enum.
     */
    WeatherQualityPreset getQualityPreset() const;

    /**
     * Set quality preset by name and apply to settings.
     * @param presetName Preset name: "low", "medium", "high", "ultra", "custom"
     * @return true if valid preset name
     */
    bool setQualityPreset(const std::string& presetName);

    /**
     * Set quality preset by enum and apply to settings.
     */
    void setQualityPreset(WeatherQualityPreset preset);

private:
    WeatherConfigLoader() = default;

    /**
     * Set all settings to default values.
     */
    void setDefaults();

    /**
     * Load rain settings from JSON.
     */
    void loadRainSettings(const Json::Value& root);

    /**
     * Load rain splash settings from JSON.
     */
    void loadRainSplashSettings(const Json::Value& root);

    /**
     * Load snow settings from JSON.
     */
    void loadSnowSettings(const Json::Value& root);

    /**
     * Load storm/lightning settings from JSON.
     */
    void loadStormSettings(const Json::Value& root);

    /**
     * Validate and clamp a float value to range.
     */
    float clampFloat(float value, float minVal, float maxVal, const char* name);

    /**
     * Validate and clamp an int value to range.
     */
    int clampInt(int value, int minVal, int maxVal, const char* name);

    /**
     * Apply quality preset overrides to loaded settings.
     */
    void applyQualityPreset();

    // Settings storage
    Environment::RainSettings rainSettings_;
    Environment::RainSplashSettings rainSplashSettings_;
    Environment::SnowSettings snowSettings_;
    WeatherEffectsConfig weatherConfig_;
    std::string qualityPreset_;

    // State
    std::string configPath_;
    bool loaded_ = false;
    std::function<void()> reloadCallback_;
};

} // namespace Graphics
} // namespace EQT
