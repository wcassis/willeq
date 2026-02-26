#include "client/graphics/weather_system.h"
#include "client/graphics/simulation_worker.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace EQT {
namespace Graphics {

WeatherSystem::WeatherSystem() = default;

void WeatherSystem::setWeather(WeatherType type) {
    WeatherCommandData cmd;
    cmd.type = WeatherCommand::SetWeatherImmediate;
    cmd.weatherType = static_cast<uint8_t>(type);
    pendingCommands_.push_back(std::move(cmd));
}

void WeatherSystem::transitionToWeather(WeatherType type, float transitionTime) {
    WeatherCommandData cmd;
    cmd.type = WeatherCommand::TransitionToWeather;
    cmd.weatherType = static_cast<uint8_t>(type);
    cmd.transitionTime = transitionTime;
    pendingCommands_.push_back(std::move(cmd));
}

void WeatherSystem::setZoneConfig(const ZoneWeatherConfig& config) {
    zoneConfig_ = config;

    WeatherCommandData cmd;
    cmd.type = WeatherCommand::SetZoneConfig;
    cmd.zoneConfig = config;
    pendingCommands_.push_back(std::move(cmd));
}

void WeatherSystem::setWeatherFromZone(const std::string& zoneName) {
    // Cache zone config locally for debug info
    ZoneWeatherConfig config;
    if (loadZoneWeatherConfig(zoneName, config)) {
        zoneConfig_ = config;
    } else {
        zoneConfig_.zoneName = zoneName;
        zoneConfig_.defaultWeather = WeatherType::Normal;
        zoneConfig_.enabled = true;
    }

    WeatherCommandData cmd;
    cmd.type = WeatherCommand::SetWeatherFromZone;
    cmd.zoneName = zoneName;
    pendingCommands_.push_back(std::move(cmd));
}

void WeatherSystem::setSimulationEnabled(bool enabled) {
    simulationEnabled_ = enabled;

    WeatherCommandData cmd;
    cmd.type = WeatherCommand::SetSimulationEnabled;
    cmd.simulationEnabled = enabled;
    pendingCommands_.push_back(std::move(cmd));
}

std::vector<WeatherCommandData> WeatherSystem::drainCommands() {
    std::vector<WeatherCommandData> commands;
    commands.swap(pendingCommands_);
    return commands;
}

void WeatherSystem::applyWorkerResults(uint8_t currentWeather, uint8_t targetWeather,
                                       float transitionProgress, float windIntensity,
                                       bool weatherChanged, uint8_t newWeatherType) {
    currentWeather_ = static_cast<WeatherType>(currentWeather);
    targetWeather_ = static_cast<WeatherType>(targetWeather);
    transitionProgress_ = transitionProgress;
    windIntensity_ = windIntensity;

    if (weatherChanged) {
        WeatherType newType = static_cast<WeatherType>(newWeatherType);
        notifyListeners(newType);
    }
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

    ss << " | Wind: " << static_cast<int>(windIntensity_ * 100) << "%";
    ss << " | Zone: " << (zoneConfig_.zoneName.empty() ? "none" : zoneConfig_.zoneName);
    ss << " | Sim: " << (simulationEnabled_ ? "ON" : "OFF");

    return ss.str();
}

bool loadZoneWeatherConfig(const std::string& zoneName, ZoneWeatherConfig& config) {
    // Try zone-specific config
    std::string configPath = "config/zones/" + zoneName + "/weather.json";
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
