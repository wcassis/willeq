#include "client/graphics/weather_quality_preset.h"
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
    // Low preset - minimal effects
    lowPreset_.lightningEnabled = false;
    lowPreset_.maxBranchLevel = 0;
    lowPreset_.cloudOverlayEnabled = false;
    lowPreset_.fogTransitionSpeed = 0.0f;  // Instant

    // Medium preset - basic lightning
    mediumPreset_.lightningEnabled = true;
    mediumPreset_.maxBranchLevel = 2;
    mediumPreset_.cloudOverlayEnabled = false;
    mediumPreset_.fogTransitionSpeed = 0.2f;  // Fast

    // High preset - all effects
    highPreset_.lightningEnabled = true;
    highPreset_.maxBranchLevel = 4;
    highPreset_.cloudOverlayEnabled = true;
    highPreset_.fogTransitionSpeed = 0.5f;  // Smooth

    // Ultra preset - all effects at highest quality
    ultraPreset_.lightningEnabled = true;
    ultraPreset_.maxBranchLevel = 6;
    ultraPreset_.cloudOverlayEnabled = true;
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
