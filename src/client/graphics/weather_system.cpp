#include "client/graphics/weather_system.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace EQT {
namespace Graphics {

WeatherSystem::WeatherSystem() {
    // Seed random number generator with current time
    auto seed = static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    rng_.seed(seed);
}

void WeatherSystem::update(float deltaTime) {
    // Update transition if in progress
    if (transitionProgress_ < 1.0f) {
        transitionProgress_ += deltaTime / transitionDuration_;
        if (transitionProgress_ >= 1.0f) {
            transitionProgress_ = 1.0f;
            currentWeather_ = targetWeather_;
            LOG_DEBUG(MOD_GRAPHICS, "WeatherSystem: Transition complete, now {}",
                      getWeatherName(currentWeather_));
        }
    }

    // Update weather simulation if enabled
    if (simulationEnabled_ && zoneConfig_.enabled) {
        timeSinceLastCheck_ += deltaTime;
        currentWeatherElapsed_ += deltaTime;

        // Check for weather change at intervals or when current weather duration expires
        bool shouldCheck = (timeSinceLastCheck_ >= zoneConfig_.checkIntervalSeconds);
        bool durationExpired = (currentWeatherDuration_ > 0 &&
                               currentWeatherElapsed_ >= currentWeatherDuration_);

        if (shouldCheck || durationExpired) {
            timeSinceLastCheck_ = 0.0f;
            checkWeatherChange();
        }
    }
}

void WeatherSystem::setWeather(WeatherType type) {
    if (type == currentWeather_ && transitionProgress_ >= 1.0f) {
        return;  // Already at this weather
    }

    WeatherType oldWeather = currentWeather_;
    currentWeather_ = type;
    targetWeather_ = type;
    transitionProgress_ = 1.0f;

    // Reset weather duration tracking
    currentWeatherElapsed_ = 0.0f;
    currentWeatherDuration_ = 0.0f;

    LOG_INFO(MOD_GRAPHICS, "WeatherSystem: Weather changed from {} to {}",
             getWeatherName(oldWeather), getWeatherName(type));

    notifyListeners(type);
}

void WeatherSystem::transitionToWeather(WeatherType type, float transitionTime) {
    if (type == targetWeather_ && transitionProgress_ >= 1.0f) {
        return;  // Already transitioning to or at this weather
    }

    targetWeather_ = type;
    transitionDuration_ = std::max(0.1f, transitionTime);
    transitionProgress_ = 0.0f;

    // Reset weather duration tracking
    currentWeatherElapsed_ = 0.0f;
    currentWeatherDuration_ = 0.0f;

    LOG_INFO(MOD_GRAPHICS, "WeatherSystem: Starting transition from {} to {} over {:.1f}s",
             getWeatherName(currentWeather_), getWeatherName(type), transitionTime);

    // Notify immediately so listeners can start their own transitions
    notifyListeners(type);
}

float WeatherSystem::getWindIntensity() const {
    float currentIntensity = getWindIntensityForWeather(currentWeather_);
    float targetIntensity = getWindIntensityForWeather(targetWeather_);

    // Smooth interpolation during transition
    if (transitionProgress_ < 1.0f) {
        // Use smoothstep for more natural transition
        float t = transitionProgress_;
        float smooth = t * t * (3.0f - 2.0f * t);
        return currentIntensity + (targetIntensity - currentIntensity) * smooth;
    }

    return currentIntensity;
}

void WeatherSystem::setZoneConfig(const ZoneWeatherConfig& config) {
    zoneConfig_ = config;

    // Reset simulation state
    timeSinceLastCheck_ = 0.0f;
    currentWeatherElapsed_ = 0.0f;
    currentWeatherDuration_ = 0.0f;

    LOG_DEBUG(MOD_GRAPHICS, "WeatherSystem: Zone config set for '{}', enabled={}",
              config.zoneName, config.enabled);

    // Set initial weather based on zone default
    if (config.enabled) {
        setWeather(config.defaultWeather);
    }
}

void WeatherSystem::setWeatherFromZone(const std::string& zoneName) {
    ZoneWeatherConfig config;

    if (loadZoneWeatherConfig(zoneName, config)) {
        LOG_INFO(MOD_GRAPHICS, "WeatherSystem: Loaded weather config for zone '{}'", zoneName);
    } else {
        // Use defaults
        config.zoneName = zoneName;
        config.defaultWeather = WeatherType::Normal;
        config.enabled = true;
        LOG_DEBUG(MOD_GRAPHICS, "WeatherSystem: Using default weather config for zone '{}'", zoneName);
    }

    setZoneConfig(config);
}

void WeatherSystem::addListener(IWeatherListener* listener) {
    if (listener) {
        listeners_.push_back(listener);
    }
}

void WeatherSystem::removeListener(IWeatherListener* listener) {
    listeners_.erase(
        std::remove(listeners_.begin(), listeners_.end(), listener),
        listeners_.end());
}

void WeatherSystem::addCallback(std::function<void(WeatherType)> callback) {
    if (callback) {
        callbacks_.push_back(std::move(callback));
    }
}

void WeatherSystem::notifyListeners(WeatherType newWeather) {
    for (auto* listener : listeners_) {
        if (listener) {
            listener->onWeatherChanged(newWeather);
        }
    }

    for (const auto& callback : callbacks_) {
        if (callback) {
            callback(newWeather);
        }
    }
}

void WeatherSystem::checkWeatherChange() {
    // If we're in non-default weather and duration expired, return to default
    if (currentWeather_ != zoneConfig_.defaultWeather &&
        currentWeatherDuration_ > 0 &&
        currentWeatherElapsed_ >= currentWeatherDuration_) {

        LOG_DEBUG(MOD_GRAPHICS, "WeatherSystem: Weather duration expired, returning to {}",
                  getWeatherName(zoneConfig_.defaultWeather));
        transitionToWeather(zoneConfig_.defaultWeather, 10.0f);
        return;
    }

    // Roll for new weather
    WeatherType newWeather = rollForWeather();

    if (newWeather != currentWeather_) {
        transitionToWeather(newWeather, 10.0f);  // 10 second transition

        // Set duration for the new weather
        if (newWeather == WeatherType::Rain) {
            // Use rain duration from config (in minutes, convert to seconds)
            std::uniform_int_distribution<int> slot(0, 3);
            int idx = slot(rng_);
            currentWeatherDuration_ = zoneConfig_.rainDuration[idx] * 60.0f;
        } else if (newWeather == WeatherType::Storm) {
            // Storms are shorter than rain
            std::uniform_real_distribution<float> duration(60.0f, 300.0f);
            currentWeatherDuration_ = duration(rng_);
        } else {
            currentWeatherDuration_ = 0.0f;  // Default weather has no duration limit
        }
    }
}

WeatherType WeatherSystem::rollForWeather() {
    // Check rain chance
    int totalRainChance = 0;
    for (int i = 0; i < 4; i++) {
        totalRainChance += zoneConfig_.rainChance[i];
    }

    if (totalRainChance > 0) {
        std::uniform_int_distribution<int> roll(0, 399);  // 4 slots * 100 max
        int rainRoll = roll(rng_);

        if (rainRoll < totalRainChance) {
            // It's raining! Determine intensity
            std::uniform_int_distribution<int> intensity(0, 99);
            int intensityRoll = intensity(rng_);

            // Higher intensity roll = storm
            if (intensityRoll > 80) {
                return WeatherType::Storm;
            } else if (intensityRoll > 40) {
                return WeatherType::Rain;
            } else {
                // Light rain is just normal weather with slightly more wind
                return WeatherType::Normal;
            }
        }
    }

    // Check for calm conditions (inverse of rain in clear weather)
    std::uniform_int_distribution<int> calmRoll(0, 99);
    if (calmRoll(rng_) < 20) {  // 20% chance of calm
        return WeatherType::Calm;
    }

    return zoneConfig_.defaultWeather;
}

float WeatherSystem::getWindIntensityForWeather(WeatherType type) {
    switch (type) {
        case WeatherType::Calm:   return 0.3f;
        case WeatherType::Normal: return 0.6f;
        case WeatherType::Rain:   return 0.8f;
        case WeatherType::Storm:  return 1.0f;
        default:                  return 0.6f;
    }
}

const char* WeatherSystem::getWeatherName(WeatherType type) {
    switch (type) {
        case WeatherType::Calm:   return "Calm";
        case WeatherType::Normal: return "Normal";
        case WeatherType::Rain:   return "Rain";
        case WeatherType::Storm:  return "Storm";
        default:                  return "Unknown";
    }
}

std::string WeatherSystem::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Weather: " << getWeatherName(currentWeather_);

    if (transitionProgress_ < 1.0f) {
        ss << " -> " << getWeatherName(targetWeather_);
        ss << " (" << static_cast<int>(transitionProgress_ * 100) << "%)";
    }

    ss << " | Wind: " << static_cast<int>(getWindIntensity() * 100) << "%";
    ss << " | Zone: " << (zoneConfig_.zoneName.empty() ? "none" : zoneConfig_.zoneName);
    ss << " | Sim: " << (simulationEnabled_ ? "ON" : "OFF");

    return ss.str();
}

bool loadZoneWeatherConfig(const std::string& zoneName, ZoneWeatherConfig& config) {
    // Try zone-specific config
    std::string configPath = "data/config/zones/" + zoneName + "/weather.json";
    std::ifstream file(configPath);

    if (!file.is_open()) {
        // Try default weather config
        configPath = "data/config/weather.json";
        file.open(configPath);

        if (!file.is_open()) {
            return false;
        }
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "WeatherSystem: JSON parse error in {}: {}", configPath, errors);
        return false;
    }

    config.zoneName = zoneName;

    // Parse weather section
    const Json::Value& weather = root["weather"];
    if (weather.isNull()) {
        return false;
    }

    // Parse rain chances
    const Json::Value& rain = weather["rain_chance"];
    if (rain.isArray() && rain.size() >= 4) {
        for (int i = 0; i < 4; i++) {
            config.rainChance[i] = static_cast<uint8_t>(rain[i].asInt());
        }
    }

    // Parse rain durations
    const Json::Value& rainDur = weather["rain_duration"];
    if (rainDur.isArray() && rainDur.size() >= 4) {
        for (int i = 0; i < 4; i++) {
            config.rainDuration[i] = static_cast<uint8_t>(rainDur[i].asInt());
        }
    }

    // Parse default weather
    std::string defaultWeatherStr = weather.get("default", "normal").asString();
    if (defaultWeatherStr == "calm") {
        config.defaultWeather = WeatherType::Calm;
    } else if (defaultWeatherStr == "rain") {
        config.defaultWeather = WeatherType::Rain;
    } else if (defaultWeatherStr == "storm") {
        config.defaultWeather = WeatherType::Storm;
    } else {
        config.defaultWeather = WeatherType::Normal;
    }

    // Parse enabled flag
    config.enabled = weather.get("enabled", true).asBool();

    // Parse check interval
    config.checkIntervalSeconds = weather.get("check_interval", 60.0f).asFloat();

    LOG_DEBUG(MOD_GRAPHICS, "WeatherSystem: Loaded config from {}", configPath);
    return true;
}

} // namespace Graphics
} // namespace EQT
