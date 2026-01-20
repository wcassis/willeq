#include "client/graphics/tree_wind_controller.h"
#include "common/logging.h"
#include <cmath>
#include <algorithm>

namespace EQT {
namespace Graphics {

TreeWindController::TreeWindController() {
    // Initialize with default config
    config_ = TreeWindConfig{};
    weatherMultiplier_ = config_.getWeatherMultiplier(currentWeather_);
    targetMultiplier_ = weatherMultiplier_;
}

bool TreeWindController::loadConfig(const std::string& configPath, const std::string& zoneName) {
    bool success = TreeWindConfigLoader::loadConfig(config_, configPath, zoneName);

    // Update weather multiplier to match current weather with new config
    targetMultiplier_ = config_.getWeatherMultiplier(currentWeather_);

    LOG_DEBUG(MOD_GRAPHICS, "TreeWindController: Config loaded, enabled={}, baseStrength={}",
              config_.enabled, config_.baseStrength);

    return success;
}

void TreeWindController::setWeather(WeatherType weather) {
    if (weather == currentWeather_) {
        return;
    }

    WeatherType oldWeather = currentWeather_;
    currentWeather_ = weather;

    // Set target multiplier for smooth transition
    targetMultiplier_ = config_.getWeatherMultiplier(weather);

    LOG_DEBUG(MOD_GRAPHICS, "TreeWindController: Weather changed from {} to {}, "
              "target multiplier: {}",
              static_cast<int>(oldWeather), static_cast<int>(weather), targetMultiplier_);
}

void TreeWindController::update(float deltaTime) {
    // Accumulate time for animation
    time_ += deltaTime;

    // Wrap time to prevent floating point precision issues over long sessions
    // Use a large value that's still precise enough for our frequencies
    if (time_ > 10000.0f) {
        time_ -= 10000.0f;
    }

    // Update weather transition
    updateWeatherTransition(deltaTime);
}

void TreeWindController::updateWeatherTransition(float deltaTime) {
    if (std::abs(weatherMultiplier_ - targetMultiplier_) < 0.001f) {
        weatherMultiplier_ = targetMultiplier_;
        return;
    }

    // Smooth transition towards target
    float diff = targetMultiplier_ - weatherMultiplier_;
    float step = transitionSpeed_ * deltaTime;

    if (std::abs(diff) <= step) {
        weatherMultiplier_ = targetMultiplier_;
    } else if (diff > 0) {
        weatherMultiplier_ += step;
    } else {
        weatherMultiplier_ -= step;
    }
}

float TreeWindController::calculateInfluence(float normalizedHeight) const {
    // No movement below influence start height
    if (normalizedHeight <= config_.influenceStartHeight) {
        return 0.0f;
    }

    // Clamp height to valid range
    float clampedHeight = std::min(normalizedHeight, 1.0f);

    // Calculate influence factor with power curve
    // Maps [influenceStartHeight, 1.0] to [0, 1] then applies exponent
    float heightRange = 1.0f - config_.influenceStartHeight;
    if (heightRange <= 0.0f) {
        return 0.0f;  // Invalid config, avoid division by zero
    }

    float heightFactor = (clampedHeight - config_.influenceStartHeight) / heightRange;
    float influence = std::pow(heightFactor, config_.influenceExponent);

    return influence;
}

irr::core::vector3df TreeWindController::getDisplacement(
    const irr::core::vector3df& worldPos,
    float normalizedHeight,
    float meshSeed) const
{
    // Early out if disabled or no wind
    if (!config_.enabled || weatherMultiplier_ < 0.001f) {
        return irr::core::vector3df(0, 0, 0);
    }

    // Calculate height-based influence
    float influence = calculateInfluence(normalizedHeight);
    if (influence < 0.001f) {
        return irr::core::vector3df(0, 0, 0);
    }

    // Primary sway (slow, large movement)
    // Each tree has its own phase offset based on meshSeed
    float primaryPhase = time_ * config_.baseFrequency * TWO_PI + meshSeed;
    float primarySway = std::sin(primaryPhase) * config_.baseStrength;

    // Secondary gust (faster, irregular pattern)
    // Use a different seed multiplier for variety
    float gustPhase = time_ * config_.gustFrequency * TWO_PI + meshSeed * 1.7f;
    // Nested sine creates irregular gust pattern
    float gustSway = std::sin(gustPhase * 3.0f + std::sin(gustPhase)) * config_.gustStrength;

    // Turbulence (high frequency noise based on position)
    // Creates small rapid movements that vary spatially
    float turbX = std::sin(time_ * 5.0f + worldPos.X * 0.1f + meshSeed) * config_.turbulence;
    float turbZ = std::cos(time_ * 4.3f + worldPos.Z * 0.1f + meshSeed) * config_.turbulence;

    // Combine primary and gust movement, apply weather multiplier and influence
    float totalStrength = (primarySway + gustSway) * weatherMultiplier_ * influence;

    // Calculate displacement vector
    // X displacement follows wind direction
    // Y is zero (trees sway horizontally, don't move up/down)
    // Z displacement is slightly less than X for asymmetric movement
    float dispX = totalStrength * windDirection_.X + turbX * influence * weatherMultiplier_;
    float dispY = 0.0f;
    float dispZ = totalStrength * 0.7f * windDirection_.Y + turbZ * influence * weatherMultiplier_;

    return irr::core::vector3df(dispX, dispY, dispZ);
}

float TreeWindController::getCurrentStrength() const {
    return config_.baseStrength * weatherMultiplier_;
}

void TreeWindController::setConfig(const TreeWindConfig& config) {
    config_ = config;
    // Update target multiplier with new config values
    targetMultiplier_ = config_.getWeatherMultiplier(currentWeather_);
}

void TreeWindController::setWindDirection(const irr::core::vector2df& direction) {
    windDirection_ = direction;
    // Normalize the direction
    float length = std::sqrt(windDirection_.X * windDirection_.X +
                             windDirection_.Y * windDirection_.Y);
    if (length > 0.001f) {
        windDirection_.X /= length;
        windDirection_.Y /= length;
    } else {
        // Default to positive X if zero vector
        windDirection_.X = 1.0f;
        windDirection_.Y = 0.0f;
    }
}

} // namespace Graphics
} // namespace EQT
