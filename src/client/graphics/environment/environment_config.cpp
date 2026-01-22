#include "client/graphics/environment/environment_config.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>

namespace EQT {
namespace Graphics {
namespace Environment {

EnvironmentEffectsConfig& EnvironmentEffectsConfig::instance() {
    static EnvironmentEffectsConfig instance;
    return instance;
}

bool EnvironmentEffectsConfig::load(const std::string& path) {
    configPath_ = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN(MOD_GRAPHICS, "EnvironmentEffectsConfig: Could not open '{}', using defaults", path);
        setDefaults();
        loaded_ = true;
        return true;  // Not an error, just use defaults
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "EnvironmentEffectsConfig: Failed to parse '{}': {}", path, errors);
        setDefaults();
        loaded_ = true;
        return false;
    }

    // Load each emitter type
    loadEmitterSettings(root, "dustMotes", dustMotes_);
    loadEmitterSettings(root, "pollen", pollen_);
    loadEmitterSettings(root, "fireflies", fireflies_);
    loadEmitterSettings(root, "mist", mist_);
    loadEmitterSettings(root, "sandDust", sandDust_);
    loadEmitterSettings(root, "shorelineWaves", shorelineWaves_);
    loadDetailSettings(root);
    loadBoidsSettings(root);

    loaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "EnvironmentEffectsConfig: Loaded settings from '{}'", path);
    return true;
}

bool EnvironmentEffectsConfig::reload() {
    if (configPath_.empty()) {
        LOG_WARN(MOD_GRAPHICS, "EnvironmentEffectsConfig: No config path set, cannot reload");
        return false;
    }

    bool result = load(configPath_);

    if (result && reloadCallback_) {
        reloadCallback_();
        LOG_INFO(MOD_GRAPHICS, "EnvironmentEffectsConfig: Reload callback invoked");
    }

    return result;
}

void EnvironmentEffectsConfig::loadEmitterSettings(const Json::Value& root, const std::string& name, EmitterSettings& settings) {
    if (!root.isMember(name)) {
        LOG_DEBUG(MOD_GRAPHICS, "EnvironmentEffectsConfig: No settings for '{}', using defaults", name);
        return;
    }

    const Json::Value& json = root[name];

    if (json.isMember("enabled")) settings.enabled = json["enabled"].asBool();
    if (json.isMember("maxParticles")) settings.maxParticles = json["maxParticles"].asInt();
    if (json.isMember("spawnRate")) settings.spawnRate = json["spawnRate"].asFloat();
    if (json.isMember("spawnRadiusMin")) settings.spawnRadiusMin = json["spawnRadiusMin"].asFloat();
    if (json.isMember("spawnRadiusMax")) settings.spawnRadiusMax = json["spawnRadiusMax"].asFloat();
    if (json.isMember("spawnHeightMin")) settings.spawnHeightMin = json["spawnHeightMin"].asFloat();
    if (json.isMember("spawnHeightMax")) settings.spawnHeightMax = json["spawnHeightMax"].asFloat();
    if (json.isMember("sizeMin")) settings.sizeMin = json["sizeMin"].asFloat();
    if (json.isMember("sizeMax")) settings.sizeMax = json["sizeMax"].asFloat();
    if (json.isMember("lifetimeMin")) settings.lifetimeMin = json["lifetimeMin"].asFloat();
    if (json.isMember("lifetimeMax")) settings.lifetimeMax = json["lifetimeMax"].asFloat();
    if (json.isMember("driftSpeed")) settings.driftSpeed = json["driftSpeed"].asFloat();
    if (json.isMember("windFactor")) settings.windFactor = json["windFactor"].asFloat();
    if (json.isMember("alphaIndoor")) settings.alphaIndoor = json["alphaIndoor"].asFloat();
    if (json.isMember("alphaOutdoor")) settings.alphaOutdoor = json["alphaOutdoor"].asFloat();
    if (json.isMember("colorR")) settings.colorR = json["colorR"].asFloat();
    if (json.isMember("colorG")) settings.colorG = json["colorG"].asFloat();
    if (json.isMember("colorB")) settings.colorB = json["colorB"].asFloat();
    if (json.isMember("colorA")) settings.colorA = json["colorA"].asFloat();

    LOG_DEBUG(MOD_GRAPHICS, "EnvironmentEffectsConfig: Loaded '{}' settings", name);
}

void EnvironmentEffectsConfig::loadDetailSettings(const Json::Value& root) {
    if (!root.isMember("detailObjects")) {
        return;
    }

    const Json::Value& json = root["detailObjects"];

    if (json.isMember("enabled")) detailObjects_.enabled = json["enabled"].asBool();
    if (json.isMember("density")) detailObjects_.density = json["density"].asFloat();
    if (json.isMember("viewDistance")) detailObjects_.viewDistance = json["viewDistance"].asFloat();
    if (json.isMember("grassEnabled")) detailObjects_.grassEnabled = json["grassEnabled"].asBool();
    if (json.isMember("plantsEnabled")) detailObjects_.plantsEnabled = json["plantsEnabled"].asBool();
    if (json.isMember("rocksEnabled")) detailObjects_.rocksEnabled = json["rocksEnabled"].asBool();
    if (json.isMember("debrisEnabled")) detailObjects_.debrisEnabled = json["debrisEnabled"].asBool();

    LOG_DEBUG(MOD_GRAPHICS, "EnvironmentEffectsConfig: Loaded detail object settings");
}

void EnvironmentEffectsConfig::loadBoidsSettings(const Json::Value& root) {
    if (!root.isMember("boids")) {
        return;
    }

    const Json::Value& json = root["boids"];

    if (json.isMember("enabled")) boids_.enabled = json["enabled"].asBool();
    if (json.isMember("maxFlocks")) boids_.maxFlocks = json["maxFlocks"].asInt();
    if (json.isMember("flockSizeMin")) boids_.flockSizeMin = json["flockSizeMin"].asInt();
    if (json.isMember("flockSizeMax")) boids_.flockSizeMax = json["flockSizeMax"].asInt();
    if (json.isMember("spawnCooldown")) boids_.spawnCooldown = json["spawnCooldown"].asFloat();
    if (json.isMember("viewDistance")) boids_.viewDistance = json["viewDistance"].asFloat();
    if (json.isMember("scatterRadius")) boids_.scatterRadius = json["scatterRadius"].asFloat();
    if (json.isMember("separation")) boids_.separation = json["separation"].asFloat();
    if (json.isMember("alignment")) boids_.alignment = json["alignment"].asFloat();
    if (json.isMember("cohesion")) boids_.cohesion = json["cohesion"].asFloat();
    if (json.isMember("avoidance")) boids_.avoidance = json["avoidance"].asFloat();

    LOG_DEBUG(MOD_GRAPHICS, "EnvironmentEffectsConfig: Loaded boids settings");
}

void EnvironmentEffectsConfig::setDefaults() {
    // Dust motes - subtle floating particles
    dustMotes_ = EmitterSettings{};
    dustMotes_.maxParticles = 80;
    dustMotes_.spawnRate = 10.0f;
    dustMotes_.spawnRadiusMin = 3.0f;
    dustMotes_.spawnRadiusMax = 20.0f;
    dustMotes_.spawnHeightMin = -1.0f;
    dustMotes_.spawnHeightMax = 6.0f;
    dustMotes_.sizeMin = 0.15f;
    dustMotes_.sizeMax = 0.35f;
    dustMotes_.lifetimeMin = 6.0f;
    dustMotes_.lifetimeMax = 10.0f;
    dustMotes_.driftSpeed = 0.3f;
    dustMotes_.windFactor = 2.0f;
    dustMotes_.alphaIndoor = 0.9f;
    dustMotes_.alphaOutdoor = 0.8f;

    // Pollen - larger, slower particles in forests
    pollen_ = EmitterSettings{};
    pollen_.maxParticles = 60;
    pollen_.spawnRate = 8.0f;
    pollen_.spawnRadiusMin = 5.0f;
    pollen_.spawnRadiusMax = 25.0f;
    pollen_.spawnHeightMin = 0.0f;
    pollen_.spawnHeightMax = 8.0f;
    pollen_.sizeMin = 0.2f;
    pollen_.sizeMax = 0.4f;
    pollen_.lifetimeMin = 8.0f;
    pollen_.lifetimeMax = 12.0f;
    pollen_.driftSpeed = 0.2f;
    pollen_.windFactor = 1.5f;
    pollen_.alphaIndoor = 0.7f;
    pollen_.alphaOutdoor = 0.6f;
    pollen_.colorR = 1.0f;
    pollen_.colorG = 1.0f;
    pollen_.colorB = 0.7f;

    // Fireflies - glowing night particles
    fireflies_ = EmitterSettings{};
    fireflies_.maxParticles = 40;
    fireflies_.spawnRate = 5.0f;
    fireflies_.spawnRadiusMin = 5.0f;
    fireflies_.spawnRadiusMax = 30.0f;
    fireflies_.spawnHeightMin = 0.5f;
    fireflies_.spawnHeightMax = 4.0f;
    fireflies_.sizeMin = 0.1f;
    fireflies_.sizeMax = 0.2f;
    fireflies_.lifetimeMin = 10.0f;
    fireflies_.lifetimeMax = 20.0f;
    fireflies_.driftSpeed = 0.5f;
    fireflies_.windFactor = 0.5f;
    fireflies_.alphaIndoor = 1.0f;
    fireflies_.alphaOutdoor = 1.0f;
    fireflies_.colorR = 0.8f;
    fireflies_.colorG = 1.0f;
    fireflies_.colorB = 0.4f;

    // Mist - large translucent particles
    mist_ = EmitterSettings{};
    mist_.maxParticles = 50;
    mist_.spawnRate = 6.0f;
    mist_.spawnRadiusMin = 5.0f;
    mist_.spawnRadiusMax = 35.0f;
    mist_.spawnHeightMin = -0.5f;
    mist_.spawnHeightMax = 2.0f;
    mist_.sizeMin = 2.0f;
    mist_.sizeMax = 5.0f;
    mist_.lifetimeMin = 10.0f;
    mist_.lifetimeMax = 18.0f;
    mist_.driftSpeed = 0.1f;
    mist_.windFactor = 1.0f;
    mist_.alphaIndoor = 0.3f;
    mist_.alphaOutdoor = 0.25f;

    // Sand dust - fast-moving desert particles
    sandDust_ = EmitterSettings{};
    sandDust_.maxParticles = 100;
    sandDust_.spawnRate = 15.0f;
    sandDust_.spawnRadiusMin = 3.0f;
    sandDust_.spawnRadiusMax = 25.0f;
    sandDust_.spawnHeightMin = 0.0f;
    sandDust_.spawnHeightMax = 3.0f;
    sandDust_.sizeMin = 0.1f;
    sandDust_.sizeMax = 0.3f;
    sandDust_.lifetimeMin = 3.0f;
    sandDust_.lifetimeMax = 6.0f;
    sandDust_.driftSpeed = 0.5f;
    sandDust_.windFactor = 4.0f;
    sandDust_.alphaIndoor = 0.6f;
    sandDust_.alphaOutdoor = 0.5f;
    sandDust_.colorR = 0.9f;
    sandDust_.colorG = 0.8f;
    sandDust_.colorB = 0.6f;

    // Shoreline waves - foam and spray at water edges
    shorelineWaves_ = EmitterSettings{};
    shorelineWaves_.maxParticles = 120;
    shorelineWaves_.spawnRate = 15.0f;
    shorelineWaves_.spawnRadiusMin = 2.0f;
    shorelineWaves_.spawnRadiusMax = 40.0f;
    shorelineWaves_.spawnHeightMin = -0.2f;
    shorelineWaves_.spawnHeightMax = 0.5f;
    shorelineWaves_.sizeMin = 0.3f;
    shorelineWaves_.sizeMax = 0.8f;
    shorelineWaves_.lifetimeMin = 2.0f;
    shorelineWaves_.lifetimeMax = 5.0f;
    shorelineWaves_.driftSpeed = 0.5f;
    shorelineWaves_.windFactor = 2.0f;
    shorelineWaves_.alphaIndoor = 0.7f;
    shorelineWaves_.alphaOutdoor = 0.6f;
    shorelineWaves_.colorR = 0.95f;
    shorelineWaves_.colorG = 0.98f;
    shorelineWaves_.colorB = 1.0f;
    shorelineWaves_.colorA = 0.8f;

    // Detail objects
    detailObjects_ = DetailSettings{};

    // Boids (ambient creatures)
    boids_ = BoidsSettings{};
    boids_.enabled = true;
    boids_.maxFlocks = 3;
    boids_.flockSizeMin = 5;
    boids_.flockSizeMax = 12;
    boids_.spawnCooldown = 30.0f;
    boids_.viewDistance = 150.0f;
    boids_.scatterRadius = 20.0f;
    boids_.separation = 1.5f;
    boids_.alignment = 1.0f;
    boids_.cohesion = 1.0f;
    boids_.avoidance = 2.0f;

    LOG_DEBUG(MOD_GRAPHICS, "EnvironmentEffectsConfig: Using default settings");
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
