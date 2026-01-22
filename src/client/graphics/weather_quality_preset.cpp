#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/environment/emitters/rain_emitter.h"
#include "client/graphics/environment/emitters/rain_splash_emitter.h"
#include "client/graphics/environment/emitters/snow_emitter.h"
#include "client/graphics/weather_effects_controller.h"
#include "common/logging.h"
#include <algorithm>
#include <cctype>

namespace EQT {
namespace Graphics {

WeatherQualityManager& WeatherQualityManager::instance() {
    static WeatherQualityManager instance;
    return instance;
}

WeatherQualityManager::WeatherQualityManager() {
    initializePresets();
}

void WeatherQualityManager::initializePresets() {
    // Low preset - minimal particles, no advanced effects
    lowPreset_.rainMaxParticles = 150;
    lowPreset_.rainSpawnRate = 60.0f;
    lowPreset_.rainSplashMaxParticles = 50;
    lowPreset_.rainSplashSpawnRate = 30.0f;
    lowPreset_.snowMaxParticles = 200;
    lowPreset_.snowSpawnRate = 50.0f;
    lowPreset_.lightningEnabled = false;
    lowPreset_.maxBranchLevel = 0;
    lowPreset_.ripplesEnabled = false;
    lowPreset_.cloudOverlayEnabled = false;
    lowPreset_.snowAccumulationEnabled = false;
    lowPreset_.fogTransitionSpeed = 0.0f;  // Instant

    // Medium preset - balanced particles, basic lightning
    mediumPreset_.rainMaxParticles = 300;
    mediumPreset_.rainSpawnRate = 120.0f;
    mediumPreset_.rainSplashMaxParticles = 100;
    mediumPreset_.rainSplashSpawnRate = 60.0f;
    mediumPreset_.snowMaxParticles = 400;
    mediumPreset_.snowSpawnRate = 100.0f;
    mediumPreset_.lightningEnabled = true;
    mediumPreset_.maxBranchLevel = 2;
    mediumPreset_.ripplesEnabled = false;
    mediumPreset_.cloudOverlayEnabled = false;
    mediumPreset_.snowAccumulationEnabled = false;
    mediumPreset_.fogTransitionSpeed = 0.2f;  // Fast

    // High preset - full particles, all effects
    highPreset_.rainMaxParticles = 500;
    highPreset_.rainSpawnRate = 200.0f;
    highPreset_.rainSplashMaxParticles = 200;
    highPreset_.rainSplashSpawnRate = 100.0f;
    highPreset_.snowMaxParticles = 600;
    highPreset_.snowSpawnRate = 150.0f;
    highPreset_.lightningEnabled = true;
    highPreset_.maxBranchLevel = 4;
    highPreset_.ripplesEnabled = true;
    highPreset_.cloudOverlayEnabled = true;
    highPreset_.snowAccumulationEnabled = false;
    highPreset_.fogTransitionSpeed = 0.5f;  // Smooth

    // Ultra preset - maximum particles, all effects at highest quality
    ultraPreset_.rainMaxParticles = 800;
    ultraPreset_.rainSpawnRate = 300.0f;
    ultraPreset_.rainSplashMaxParticles = 400;
    ultraPreset_.rainSplashSpawnRate = 180.0f;
    ultraPreset_.snowMaxParticles = 1000;
    ultraPreset_.snowSpawnRate = 250.0f;
    ultraPreset_.lightningEnabled = true;
    ultraPreset_.maxBranchLevel = 6;
    ultraPreset_.ripplesEnabled = true;
    ultraPreset_.cloudOverlayEnabled = true;
    ultraPreset_.snowAccumulationEnabled = true;
    ultraPreset_.fogTransitionSpeed = 0.5f;  // Smooth

    // Custom preset - matches high by default, but won't override JSON
    customPreset_ = highPreset_;

    LOG_DEBUG(MOD_GRAPHICS, "WeatherQualityManager: Initialized presets");
}

std::string WeatherQualityManager::getCurrentPresetName() const {
    return presetToString(currentPreset_);
}

void WeatherQualityManager::setPreset(WeatherQualityPreset preset) {
    currentPreset_ = preset;
    LOG_INFO(MOD_GRAPHICS, "Weather quality preset set to: {}", presetToString(preset));
}

bool WeatherQualityManager::setPresetByName(const std::string& name) {
    WeatherQualityPreset preset;
    if (stringToPreset(name, preset)) {
        setPreset(preset);
        return true;
    }
    LOG_WARN(MOD_GRAPHICS, "Invalid weather quality preset name: '{}'", name);
    return false;
}

const WeatherPresetValues& WeatherQualityManager::getPresetValues(WeatherQualityPreset preset) const {
    switch (preset) {
        case WeatherQualityPreset::Low:
            return lowPreset_;
        case WeatherQualityPreset::Medium:
            return mediumPreset_;
        case WeatherQualityPreset::High:
            return highPreset_;
        case WeatherQualityPreset::Ultra:
            return ultraPreset_;
        case WeatherQualityPreset::Custom:
        default:
            return customPreset_;
    }
}

void WeatherQualityManager::applyToRainSettings(Environment::RainSettings& settings) const {
    if (currentPreset_ == WeatherQualityPreset::Custom) {
        // Custom mode: don't override JSON settings
        return;
    }

    const auto& preset = getCurrentPresetValues();
    settings.maxParticles = preset.rainMaxParticles;
    settings.spawnRate = preset.rainSpawnRate;

    LOG_DEBUG(MOD_GRAPHICS, "Applied {} preset to rain: maxParticles={}, spawnRate={}",
              presetToString(currentPreset_), settings.maxParticles, settings.spawnRate);
}

void WeatherQualityManager::applyToRainSplashSettings(Environment::RainSplashSettings& settings) const {
    if (currentPreset_ == WeatherQualityPreset::Custom) {
        return;
    }

    const auto& preset = getCurrentPresetValues();
    settings.maxParticles = preset.rainSplashMaxParticles;
    settings.spawnRate = preset.rainSplashSpawnRate;

    LOG_DEBUG(MOD_GRAPHICS, "Applied {} preset to rain splash: maxParticles={}, spawnRate={}",
              presetToString(currentPreset_), settings.maxParticles, settings.spawnRate);
}

void WeatherQualityManager::applyToSnowSettings(Environment::SnowSettings& settings) const {
    if (currentPreset_ == WeatherQualityPreset::Custom) {
        return;
    }

    const auto& preset = getCurrentPresetValues();
    settings.maxParticles = preset.snowMaxParticles;
    settings.spawnRate = preset.snowSpawnRate;

    LOG_DEBUG(MOD_GRAPHICS, "Applied {} preset to snow: maxParticles={}, spawnRate={}",
              presetToString(currentPreset_), settings.maxParticles, settings.spawnRate);
}

void WeatherQualityManager::applyToWeatherConfig(WeatherEffectsConfig& config) const {
    if (currentPreset_ == WeatherQualityPreset::Custom) {
        return;
    }

    const auto& preset = getCurrentPresetValues();
    config.storm.lightningEnabled = preset.lightningEnabled;
    // Note: maxBranchLevel would need to be added to WeatherEffectsConfig
    // For now, lightning enabled/disabled is the main control

    LOG_DEBUG(MOD_GRAPHICS, "Applied {} preset to weather config: lightning={}",
              presetToString(currentPreset_), config.storm.lightningEnabled);
}

std::string WeatherQualityManager::presetToString(WeatherQualityPreset preset) {
    switch (preset) {
        case WeatherQualityPreset::Low:    return "low";
        case WeatherQualityPreset::Medium: return "medium";
        case WeatherQualityPreset::High:   return "high";
        case WeatherQualityPreset::Ultra:  return "ultra";
        case WeatherQualityPreset::Custom: return "custom";
        default:                           return "unknown";
    }
}

bool WeatherQualityManager::stringToPreset(const std::string& name, WeatherQualityPreset& outPreset) {
    // Convert to lowercase for comparison
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "low") {
        outPreset = WeatherQualityPreset::Low;
        return true;
    } else if (lower == "medium" || lower == "med") {
        outPreset = WeatherQualityPreset::Medium;
        return true;
    } else if (lower == "high") {
        outPreset = WeatherQualityPreset::High;
        return true;
    } else if (lower == "ultra" || lower == "max") {
        outPreset = WeatherQualityPreset::Ultra;
        return true;
    } else if (lower == "custom") {
        outPreset = WeatherQualityPreset::Custom;
        return true;
    }

    return false;
}

std::vector<std::string> WeatherQualityManager::getAllPresetNames() {
    return {"low", "medium", "high", "ultra", "custom"};
}

} // namespace Graphics
} // namespace EQT
