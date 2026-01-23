#include "client/graphics/weather_config_loader.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>
#include <algorithm>

namespace EQT {
namespace Graphics {

WeatherConfigLoader& WeatherConfigLoader::instance() {
    static WeatherConfigLoader instance;
    return instance;
}

bool WeatherConfigLoader::load(const std::string& path) {
    configPath_ = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN(MOD_GRAPHICS, "WeatherConfigLoader: Could not open '{}', using defaults", path);
        setDefaults();
        loaded_ = true;
        return true;  // Not an error, just use defaults
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "WeatherConfigLoader: Failed to parse '{}': {}", path, errors);
        setDefaults();
        loaded_ = true;
        return false;
    }

    // Start with defaults, then override with config values
    setDefaults();

    // Load each section
    loadRainSettings(root);
    loadRainSplashSettings(root);
    loadSnowSettings(root);
    loadStormSettings(root);
    loadStormCloudSettings(root);
    loadRainOverlaySettings(root);

    // Load quality preset name and apply it
    if (root.isMember("qualityPreset")) {
        qualityPreset_ = root["qualityPreset"].asString();
        // Set the preset in the manager (this doesn't apply yet)
        WeatherQualityPreset preset;
        if (WeatherQualityManager::stringToPreset(qualityPreset_, preset)) {
            WeatherQualityManager::instance().setPreset(preset);
        }
    }

    // Apply quality preset overrides (unless Custom mode)
    applyQualityPreset();

    loaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "WeatherConfigLoader: Loaded settings from '{}' (preset: {})",
             path, qualityPreset_.empty() ? "custom" : qualityPreset_);
    return true;
}

bool WeatherConfigLoader::reload() {
    if (configPath_.empty()) {
        LOG_WARN(MOD_GRAPHICS, "WeatherConfigLoader: No config path set, cannot reload");
        return false;
    }

    bool result = load(configPath_);

    if (result && reloadCallback_) {
        reloadCallback_();
        LOG_INFO(MOD_GRAPHICS, "WeatherConfigLoader: Reload callback invoked");
    }

    return result;
}

void WeatherConfigLoader::setDefaults() {
    // Rain defaults
    rainSettings_ = Environment::RainSettings{};

    // Rain splash defaults
    rainSplashSettings_ = Environment::RainSplashSettings{};

    // Snow defaults
    snowSettings_ = Environment::SnowSettings{};

    // Storm cloud defaults
    stormCloudSettings_ = Environment::StormCloudSettings{};

    // Rain overlay defaults
    rainOverlaySettings_ = Environment::RainOverlaySettings{};

    // Weather effects config defaults
    weatherConfig_ = WeatherEffectsConfig{};

    // No preset by default
    qualityPreset_.clear();

    LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: Using default settings");
}

float WeatherConfigLoader::clampFloat(float value, float minVal, float maxVal, const char* name) {
    if (value < minVal || value > maxVal) {
        float clamped = std::clamp(value, minVal, maxVal);
        LOG_WARN(MOD_GRAPHICS, "WeatherConfigLoader: '{}' value {} clamped to [{}, {}] -> {}",
                 name, value, minVal, maxVal, clamped);
        return clamped;
    }
    return value;
}

int WeatherConfigLoader::clampInt(int value, int minVal, int maxVal, const char* name) {
    if (value < minVal || value > maxVal) {
        int clamped = std::clamp(value, minVal, maxVal);
        LOG_WARN(MOD_GRAPHICS, "WeatherConfigLoader: '{}' value {} clamped to [{}, {}] -> {}",
                 name, value, minVal, maxVal, clamped);
        return clamped;
    }
    return value;
}

void WeatherConfigLoader::loadRainSettings(const Json::Value& root) {
    if (!root.isMember("rain")) {
        LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: No 'rain' section, using defaults");
        return;
    }

    const Json::Value& json = root["rain"];

    if (json.isMember("enabled")) {
        rainSettings_.enabled = json["enabled"].asBool();
    }
    if (json.isMember("maxParticles")) {
        rainSettings_.maxParticles = clampInt(json["maxParticles"].asInt(), 10, 2000, "rain.maxParticles");
    }
    if (json.isMember("spawnRate")) {
        rainSettings_.spawnRate = clampFloat(json["spawnRate"].asFloat(), 1.0f, 1000.0f, "rain.spawnRate");
    }
    if (json.isMember("spawnRadius")) {
        rainSettings_.spawnRadius = clampFloat(json["spawnRadius"].asFloat(), 10.0f, 200.0f, "rain.spawnRadius");
    }
    if (json.isMember("spawnHeight")) {
        rainSettings_.spawnHeight = clampFloat(json["spawnHeight"].asFloat(), 20.0f, 200.0f, "rain.spawnHeight");
    }
    if (json.isMember("dropSpeed")) {
        rainSettings_.dropSpeed = clampFloat(json["dropSpeed"].asFloat(), 5.0f, 100.0f, "rain.dropSpeed");
    }
    if (json.isMember("dropSpeedVariance")) {
        rainSettings_.dropSpeedVariance = clampFloat(json["dropSpeedVariance"].asFloat(), 0.0f, 20.0f, "rain.dropSpeedVariance");
    }
    if (json.isMember("windInfluence")) {
        rainSettings_.windInfluence = clampFloat(json["windInfluence"].asFloat(), 0.0f, 2.0f, "rain.windInfluence");
    }
    if (json.isMember("sizeMin")) {
        rainSettings_.sizeMin = clampFloat(json["sizeMin"].asFloat(), 0.01f, 1.0f, "rain.sizeMin");
    }
    if (json.isMember("sizeMax")) {
        rainSettings_.sizeMax = clampFloat(json["sizeMax"].asFloat(), 0.01f, 2.0f, "rain.sizeMax");
    }
    if (json.isMember("lengthScale")) {
        rainSettings_.lengthScale = clampFloat(json["lengthScale"].asFloat(), 1.0f, 10.0f, "rain.lengthScale");
    }
    if (json.isMember("intensityScale")) {
        rainSettings_.intensityScale = clampFloat(json["intensityScale"].asFloat(), 1.0f, 50.0f, "rain.intensityScale");
    }
    if (json.isMember("colorR")) {
        rainSettings_.colorR = clampFloat(json["colorR"].asFloat(), 0.0f, 1.0f, "rain.colorR");
    }
    if (json.isMember("colorG")) {
        rainSettings_.colorG = clampFloat(json["colorG"].asFloat(), 0.0f, 1.0f, "rain.colorG");
    }
    if (json.isMember("colorB")) {
        rainSettings_.colorB = clampFloat(json["colorB"].asFloat(), 0.0f, 1.0f, "rain.colorB");
    }
    if (json.isMember("colorA")) {
        rainSettings_.colorA = clampFloat(json["colorA"].asFloat(), 0.0f, 1.0f, "rain.colorA");
    }

    LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: Loaded rain settings (maxParticles={}, spawnRate={})",
              rainSettings_.maxParticles, rainSettings_.spawnRate);
}

void WeatherConfigLoader::loadRainSplashSettings(const Json::Value& root) {
    if (!root.isMember("rainSplash")) {
        LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: No 'rainSplash' section, using defaults");
        return;
    }

    const Json::Value& json = root["rainSplash"];

    if (json.isMember("enabled")) {
        rainSplashSettings_.enabled = json["enabled"].asBool();
    }
    if (json.isMember("maxParticles")) {
        rainSplashSettings_.maxParticles = clampInt(json["maxParticles"].asInt(), 10, 1000, "rainSplash.maxParticles");
    }
    if (json.isMember("spawnRate")) {
        rainSplashSettings_.spawnRate = clampFloat(json["spawnRate"].asFloat(), 1.0f, 500.0f, "rainSplash.spawnRate");
    }
    if (json.isMember("spawnRadius")) {
        rainSplashSettings_.spawnRadius = clampFloat(json["spawnRadius"].asFloat(), 5.0f, 100.0f, "rainSplash.spawnRadius");
    }
    if (json.isMember("splashSpeed")) {
        rainSplashSettings_.splashSpeed = clampFloat(json["splashSpeed"].asFloat(), 0.5f, 10.0f, "rainSplash.splashSpeed");
    }
    if (json.isMember("splashSpeedVariance")) {
        rainSplashSettings_.splashSpeedVariance = clampFloat(json["splashSpeedVariance"].asFloat(), 0.0f, 5.0f, "rainSplash.splashSpeedVariance");
    }
    if (json.isMember("gravity")) {
        rainSplashSettings_.gravity = clampFloat(json["gravity"].asFloat(), 1.0f, 50.0f, "rainSplash.gravity");
    }
    if (json.isMember("sizeMin")) {
        rainSplashSettings_.sizeMin = clampFloat(json["sizeMin"].asFloat(), 0.01f, 0.5f, "rainSplash.sizeMin");
    }
    if (json.isMember("sizeMax")) {
        rainSplashSettings_.sizeMax = clampFloat(json["sizeMax"].asFloat(), 0.01f, 1.0f, "rainSplash.sizeMax");
    }
    if (json.isMember("lifetime")) {
        rainSplashSettings_.lifetime = clampFloat(json["lifetime"].asFloat(), 0.1f, 2.0f, "rainSplash.lifetime");
    }
    if (json.isMember("colorR")) {
        rainSplashSettings_.colorR = clampFloat(json["colorR"].asFloat(), 0.0f, 1.0f, "rainSplash.colorR");
    }
    if (json.isMember("colorG")) {
        rainSplashSettings_.colorG = clampFloat(json["colorG"].asFloat(), 0.0f, 1.0f, "rainSplash.colorG");
    }
    if (json.isMember("colorB")) {
        rainSplashSettings_.colorB = clampFloat(json["colorB"].asFloat(), 0.0f, 1.0f, "rainSplash.colorB");
    }
    if (json.isMember("colorA")) {
        rainSplashSettings_.colorA = clampFloat(json["colorA"].asFloat(), 0.0f, 1.0f, "rainSplash.colorA");
    }

    LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: Loaded rain splash settings (maxParticles={}, lifetime={})",
              rainSplashSettings_.maxParticles, rainSplashSettings_.lifetime);
}

void WeatherConfigLoader::loadSnowSettings(const Json::Value& root) {
    if (!root.isMember("snow")) {
        LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: No 'snow' section, using defaults");
        return;
    }

    const Json::Value& json = root["snow"];

    if (json.isMember("enabled")) {
        snowSettings_.enabled = json["enabled"].asBool();
    }
    if (json.isMember("maxParticles")) {
        snowSettings_.maxParticles = clampInt(json["maxParticles"].asInt(), 10, 2000, "snow.maxParticles");
    }
    if (json.isMember("spawnRate")) {
        snowSettings_.spawnRate = clampFloat(json["spawnRate"].asFloat(), 1.0f, 500.0f, "snow.spawnRate");
    }
    if (json.isMember("spawnRadius")) {
        snowSettings_.spawnRadius = clampFloat(json["spawnRadius"].asFloat(), 10.0f, 200.0f, "snow.spawnRadius");
    }
    if (json.isMember("spawnHeight")) {
        snowSettings_.spawnHeight = clampFloat(json["spawnHeight"].asFloat(), 20.0f, 200.0f, "snow.spawnHeight");
    }
    if (json.isMember("fallSpeed")) {
        snowSettings_.fallSpeed = clampFloat(json["fallSpeed"].asFloat(), 0.5f, 20.0f, "snow.fallSpeed");
    }
    if (json.isMember("fallSpeedVariance")) {
        snowSettings_.fallSpeedVariance = clampFloat(json["fallSpeedVariance"].asFloat(), 0.0f, 10.0f, "snow.fallSpeedVariance");
    }
    if (json.isMember("swayAmplitude")) {
        snowSettings_.swayAmplitude = clampFloat(json["swayAmplitude"].asFloat(), 0.0f, 10.0f, "snow.swayAmplitude");
    }
    if (json.isMember("swayFrequency")) {
        snowSettings_.swayFrequency = clampFloat(json["swayFrequency"].asFloat(), 0.1f, 5.0f, "snow.swayFrequency");
    }
    if (json.isMember("windInfluence")) {
        snowSettings_.windInfluence = clampFloat(json["windInfluence"].asFloat(), 0.0f, 2.0f, "snow.windInfluence");
    }
    if (json.isMember("sizeMin")) {
        snowSettings_.sizeMin = clampFloat(json["sizeMin"].asFloat(), 0.01f, 1.0f, "snow.sizeMin");
    }
    if (json.isMember("sizeMax")) {
        snowSettings_.sizeMax = clampFloat(json["sizeMax"].asFloat(), 0.01f, 2.0f, "snow.sizeMax");
    }
    if (json.isMember("colorR")) {
        snowSettings_.colorR = clampFloat(json["colorR"].asFloat(), 0.0f, 1.0f, "snow.colorR");
    }
    if (json.isMember("colorG")) {
        snowSettings_.colorG = clampFloat(json["colorG"].asFloat(), 0.0f, 1.0f, "snow.colorG");
    }
    if (json.isMember("colorB")) {
        snowSettings_.colorB = clampFloat(json["colorB"].asFloat(), 0.0f, 1.0f, "snow.colorB");
    }
    if (json.isMember("colorA")) {
        snowSettings_.colorA = clampFloat(json["colorA"].asFloat(), 0.0f, 1.0f, "snow.colorA");
    }

    LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: Loaded snow settings (maxParticles={}, fallSpeed={})",
              snowSettings_.maxParticles, snowSettings_.fallSpeed);
}

void WeatherConfigLoader::loadStormSettings(const Json::Value& root) {
    if (!root.isMember("storm")) {
        LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: No 'storm' section, using defaults");
        return;
    }

    const Json::Value& json = root["storm"];

    // Weather effects config - storm section
    if (json.isMember("skyDarkeningEnabled")) {
        weatherConfig_.storm.skyDarkeningEnabled = json["skyDarkeningEnabled"].asBool();
    }
    if (json.isMember("maxDarkening")) {
        weatherConfig_.storm.maxDarkening = clampFloat(json["maxDarkening"].asFloat(), 0.0f, 1.0f, "storm.maxDarkening");
    }
    if (json.isMember("lightningEnabled")) {
        weatherConfig_.storm.lightningEnabled = json["lightningEnabled"].asBool();
    }
    if (json.isMember("lightningMinInterval")) {
        weatherConfig_.storm.lightningMinInterval = clampFloat(json["lightningMinInterval"].asFloat(), 1.0f, 120.0f, "storm.lightningMinInterval");
    }
    if (json.isMember("lightningMaxInterval")) {
        weatherConfig_.storm.lightningMaxInterval = clampFloat(json["lightningMaxInterval"].asFloat(), 2.0f, 300.0f, "storm.lightningMaxInterval");
    }
    if (json.isMember("flashDuration")) {
        weatherConfig_.storm.flashDuration = clampFloat(json["flashDuration"].asFloat(), 0.01f, 1.0f, "storm.flashDuration");
    }
    if (json.isMember("boltDuration")) {
        weatherConfig_.storm.boltDuration = clampFloat(json["boltDuration"].asFloat(), 0.05f, 2.0f, "storm.boltDuration");
    }

    // Also update the rain config section
    if (json.isMember("fogDensityMultiplier")) {
        // Store in weather config (not directly in rain settings)
        // This would need to be applied through WeatherEffectsController
    }

    LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: Loaded storm settings (lightning={}, darkening={})",
              weatherConfig_.storm.lightningEnabled, weatherConfig_.storm.maxDarkening);
}

void WeatherConfigLoader::loadStormCloudSettings(const Json::Value& root) {
    if (!root.isMember("stormClouds")) {
        LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: No 'stormClouds' section, using defaults");
        return;
    }

    const Json::Value& json = root["stormClouds"];

    if (json.isMember("enabled")) {
        stormCloudSettings_.enabled = json["enabled"].asBool();
    }
    if (json.isMember("domeRadius")) {
        stormCloudSettings_.domeRadius = clampFloat(json["domeRadius"].asFloat(), 100.0f, 2000.0f, "stormClouds.domeRadius");
    }
    if (json.isMember("domeHeight")) {
        stormCloudSettings_.domeHeight = clampFloat(json["domeHeight"].asFloat(), 50.0f, 500.0f, "stormClouds.domeHeight");
    }
    if (json.isMember("domeSegments")) {
        stormCloudSettings_.domeSegments = clampInt(json["domeSegments"].asInt(), 8, 64, "stormClouds.domeSegments");
    }
    if (json.isMember("scrollSpeedBase")) {
        stormCloudSettings_.scrollSpeedBase = clampFloat(json["scrollSpeedBase"].asFloat(), 0.0f, 0.5f, "stormClouds.scrollSpeedBase");
    }
    if (json.isMember("scrollSpeedVariance")) {
        stormCloudSettings_.scrollSpeedVariance = clampFloat(json["scrollSpeedVariance"].asFloat(), 0.0f, 0.2f, "stormClouds.scrollSpeedVariance");
    }
    if (json.isMember("windInfluence")) {
        stormCloudSettings_.windInfluence = clampFloat(json["windInfluence"].asFloat(), 0.0f, 2.0f, "stormClouds.windInfluence");
    }
    if (json.isMember("frameCount")) {
        stormCloudSettings_.frameCount = clampInt(json["frameCount"].asInt(), 1, 16, "stormClouds.frameCount");
    }
    if (json.isMember("frameDuration")) {
        stormCloudSettings_.frameDuration = clampFloat(json["frameDuration"].asFloat(), 0.5f, 30.0f, "stormClouds.frameDuration");
    }
    if (json.isMember("blendDuration")) {
        stormCloudSettings_.blendDuration = clampFloat(json["blendDuration"].asFloat(), 0.1f, 10.0f, "stormClouds.blendDuration");
    }
    if (json.isMember("maxOpacity")) {
        stormCloudSettings_.maxOpacity = clampFloat(json["maxOpacity"].asFloat(), 0.0f, 1.0f, "stormClouds.maxOpacity");
    }
    if (json.isMember("fadeInSpeed")) {
        stormCloudSettings_.fadeInSpeed = clampFloat(json["fadeInSpeed"].asFloat(), 0.01f, 5.0f, "stormClouds.fadeInSpeed");
    }
    if (json.isMember("fadeOutSpeed")) {
        stormCloudSettings_.fadeOutSpeed = clampFloat(json["fadeOutSpeed"].asFloat(), 0.01f, 5.0f, "stormClouds.fadeOutSpeed");
    }
    if (json.isMember("intensityThreshold")) {
        stormCloudSettings_.intensityThreshold = clampInt(json["intensityThreshold"].asInt(), 0, 10, "stormClouds.intensityThreshold");
    }
    // Cloud colors
    if (json.isMember("cloudColorR")) {
        stormCloudSettings_.cloudColorR = clampFloat(json["cloudColorR"].asFloat(), 0.0f, 1.0f, "stormClouds.cloudColorR");
    }
    if (json.isMember("cloudColorG")) {
        stormCloudSettings_.cloudColorG = clampFloat(json["cloudColorG"].asFloat(), 0.0f, 1.0f, "stormClouds.cloudColorG");
    }
    if (json.isMember("cloudColorB")) {
        stormCloudSettings_.cloudColorB = clampFloat(json["cloudColorB"].asFloat(), 0.0f, 1.0f, "stormClouds.cloudColorB");
    }
    // Time-of-day brightness settings
    if (json.isMember("dayBrightness")) {
        stormCloudSettings_.dayBrightness = clampFloat(json["dayBrightness"].asFloat(), 0.0f, 1.0f, "stormClouds.dayBrightness");
    }
    if (json.isMember("nightBrightness")) {
        stormCloudSettings_.nightBrightness = clampFloat(json["nightBrightness"].asFloat(), 0.0f, 1.0f, "stormClouds.nightBrightness");
    }
    if (json.isMember("dawnStartHour")) {
        stormCloudSettings_.dawnStartHour = clampFloat(json["dawnStartHour"].asFloat(), 0.0f, 12.0f, "stormClouds.dawnStartHour");
    }
    if (json.isMember("dawnEndHour")) {
        stormCloudSettings_.dawnEndHour = clampFloat(json["dawnEndHour"].asFloat(), 0.0f, 12.0f, "stormClouds.dawnEndHour");
    }
    if (json.isMember("duskStartHour")) {
        stormCloudSettings_.duskStartHour = clampFloat(json["duskStartHour"].asFloat(), 12.0f, 24.0f, "stormClouds.duskStartHour");
    }
    if (json.isMember("duskEndHour")) {
        stormCloudSettings_.duskEndHour = clampFloat(json["duskEndHour"].asFloat(), 12.0f, 24.0f, "stormClouds.duskEndHour");
    }
    if (json.isMember("lightningFlashMultiplier")) {
        stormCloudSettings_.lightningFlashMultiplier = clampFloat(json["lightningFlashMultiplier"].asFloat(), 0.0f, 5.0f, "stormClouds.lightningFlashMultiplier");
    }

    LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: Loaded storm cloud settings (dayBrightness={}, nightBrightness={})",
              stormCloudSettings_.dayBrightness, stormCloudSettings_.nightBrightness);
}

void WeatherConfigLoader::loadRainOverlaySettings(const Json::Value& root) {
    if (!root.isMember("rainOverlay")) {
        LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: No 'rainOverlay' section, using defaults");
        return;
    }

    const Json::Value& json = root["rainOverlay"];

    if (json.isMember("enabled")) {
        rainOverlaySettings_.enabled = json["enabled"].asBool();
    }
    if (json.isMember("scrollSpeedBase")) {
        rainOverlaySettings_.scrollSpeedBase = clampFloat(json["scrollSpeedBase"].asFloat(), 0.01f, 10.0f, "rainOverlay.scrollSpeedBase");
    }
    if (json.isMember("scrollSpeedIntensity")) {
        rainOverlaySettings_.scrollSpeedIntensity = clampFloat(json["scrollSpeedIntensity"].asFloat(), 0.0f, 5.0f, "rainOverlay.scrollSpeedIntensity");
    }
    if (json.isMember("numLayers")) {
        rainOverlaySettings_.numLayers = clampInt(json["numLayers"].asInt(), 1, 5, "rainOverlay.numLayers");
    }
    if (json.isMember("layerDepthMin")) {
        rainOverlaySettings_.layerDepthMin = clampFloat(json["layerDepthMin"].asFloat(), 0.5f, 20.0f, "rainOverlay.layerDepthMin");
    }
    if (json.isMember("layerDepthMax")) {
        rainOverlaySettings_.layerDepthMax = clampFloat(json["layerDepthMax"].asFloat(), 1.0f, 50.0f, "rainOverlay.layerDepthMax");
    }
    if (json.isMember("layerScaleMin")) {
        rainOverlaySettings_.layerScaleMin = clampFloat(json["layerScaleMin"].asFloat(), 0.1f, 5.0f, "rainOverlay.layerScaleMin");
    }
    if (json.isMember("layerScaleMax")) {
        rainOverlaySettings_.layerScaleMax = clampFloat(json["layerScaleMax"].asFloat(), 0.1f, 10.0f, "rainOverlay.layerScaleMax");
    }
    if (json.isMember("layerSpeedMin")) {
        rainOverlaySettings_.layerSpeedMin = clampFloat(json["layerSpeedMin"].asFloat(), 0.1f, 2.0f, "rainOverlay.layerSpeedMin");
    }
    if (json.isMember("layerSpeedMax")) {
        rainOverlaySettings_.layerSpeedMax = clampFloat(json["layerSpeedMax"].asFloat(), 0.1f, 2.0f, "rainOverlay.layerSpeedMax");
    }
    if (json.isMember("baseOpacity")) {
        rainOverlaySettings_.baseOpacity = clampFloat(json["baseOpacity"].asFloat(), 0.0f, 1.0f, "rainOverlay.baseOpacity");
    }
    if (json.isMember("maxOpacity")) {
        rainOverlaySettings_.maxOpacity = clampFloat(json["maxOpacity"].asFloat(), 0.0f, 1.0f, "rainOverlay.maxOpacity");
    }
    if (json.isMember("windTiltBase")) {
        rainOverlaySettings_.windTiltBase = clampFloat(json["windTiltBase"].asFloat(), -1.0f, 1.0f, "rainOverlay.windTiltBase");
    }
    if (json.isMember("windTiltIntensity")) {
        rainOverlaySettings_.windTiltIntensity = clampFloat(json["windTiltIntensity"].asFloat(), 0.0f, 2.0f, "rainOverlay.windTiltIntensity");
    }
    if (json.isMember("fogReductionEnabled")) {
        rainOverlaySettings_.fogReductionEnabled = json["fogReductionEnabled"].asBool();
    }
    if (json.isMember("fogStartMin")) {
        rainOverlaySettings_.fogStartMin = clampFloat(json["fogStartMin"].asFloat(), 1.0f, 100.0f, "rainOverlay.fogStartMin");
    }
    if (json.isMember("fogStartMax")) {
        rainOverlaySettings_.fogStartMax = clampFloat(json["fogStartMax"].asFloat(), 10.0f, 500.0f, "rainOverlay.fogStartMax");
    }
    if (json.isMember("fogEndMin")) {
        rainOverlaySettings_.fogEndMin = clampFloat(json["fogEndMin"].asFloat(), 10.0f, 200.0f, "rainOverlay.fogEndMin");
    }
    if (json.isMember("fogEndMax")) {
        rainOverlaySettings_.fogEndMax = clampFloat(json["fogEndMax"].asFloat(), 50.0f, 1000.0f, "rainOverlay.fogEndMax");
    }
    if (json.isMember("daylightReductionEnabled")) {
        rainOverlaySettings_.daylightReductionEnabled = json["daylightReductionEnabled"].asBool();
    }
    if (json.isMember("daylightMin")) {
        rainOverlaySettings_.daylightMin = clampFloat(json["daylightMin"].asFloat(), 0.0f, 1.0f, "rainOverlay.daylightMin");
    }
    if (json.isMember("daylightMax")) {
        rainOverlaySettings_.daylightMax = clampFloat(json["daylightMax"].asFloat(), 0.0f, 1.0f, "rainOverlay.daylightMax");
    }

    LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: Loaded rain overlay settings (numLayers={}, baseOpacity={}, maxOpacity={})",
              rainOverlaySettings_.numLayers, rainOverlaySettings_.baseOpacity, rainOverlaySettings_.maxOpacity);
}

WeatherQualityPreset WeatherConfigLoader::getQualityPreset() const {
    return WeatherQualityManager::instance().getCurrentPreset();
}

bool WeatherConfigLoader::setQualityPreset(const std::string& presetName) {
    WeatherQualityPreset preset;
    if (!WeatherQualityManager::stringToPreset(presetName, preset)) {
        LOG_WARN(MOD_GRAPHICS, "WeatherConfigLoader: Invalid preset name '{}'", presetName);
        return false;
    }

    setQualityPreset(preset);
    return true;
}

void WeatherConfigLoader::setQualityPreset(WeatherQualityPreset preset) {
    qualityPreset_ = WeatherQualityManager::presetToString(preset);
    WeatherQualityManager::instance().setPreset(preset);

    // Re-apply the preset to current settings
    applyQualityPreset();

    // Trigger reload callback to update emitters
    if (reloadCallback_) {
        reloadCallback_();
        LOG_INFO(MOD_GRAPHICS, "WeatherConfigLoader: Preset changed to '{}', callback invoked", qualityPreset_);
    }
}

void WeatherConfigLoader::applyQualityPreset() {
    auto& qm = WeatherQualityManager::instance();

    // Custom preset doesn't override JSON settings
    if (qm.getCurrentPreset() == WeatherQualityPreset::Custom) {
        LOG_DEBUG(MOD_GRAPHICS, "WeatherConfigLoader: Custom preset, using JSON values");
        return;
    }

    // Apply preset overrides to settings
    qm.applyToRainSettings(rainSettings_);
    qm.applyToRainSplashSettings(rainSplashSettings_);
    qm.applyToSnowSettings(snowSettings_);
    qm.applyToWeatherConfig(weatherConfig_);

    LOG_INFO(MOD_GRAPHICS, "WeatherConfigLoader: Applied '{}' preset overrides",
             qm.getCurrentPresetName());
}

} // namespace Graphics
} // namespace EQT
