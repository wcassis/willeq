#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/particle_types.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>

namespace EQT {
namespace Graphics {
namespace Environment {

SpellEffectsConfig::SpellEffectsConfig() {
    buildTextureNameMap();
}

SpellEffectsConfig& SpellEffectsConfig::instance() {
    static SpellEffectsConfig instance;
    return instance;
}

void SpellEffectsConfig::buildTextureNameMap() {
    textureNameMap_["SoftCircle"] = ParticleAtlas::SoftCircle;
    textureNameMap_["StarShape"] = ParticleAtlas::StarShape;
    textureNameMap_["WispyCloud"] = ParticleAtlas::WispyCloud;
    textureNameMap_["SporeShape"] = ParticleAtlas::SporeShape;
    textureNameMap_["GrainShape"] = ParticleAtlas::GrainShape;
    textureNameMap_["LeafShape"] = ParticleAtlas::LeafShape;
    textureNameMap_["Snowflake"] = ParticleAtlas::Snowflake;
    textureNameMap_["Ember"] = ParticleAtlas::Ember;
    textureNameMap_["SpellSparkle"] = ParticleAtlas::SpellSparkle;
    textureNameMap_["WaterDroplet"] = ParticleAtlas::WaterDroplet;
    textureNameMap_["RippleRing"] = ParticleAtlas::RippleRing;
    textureNameMap_["SnowPatch"] = ParticleAtlas::SnowPatch;
    textureNameMap_["RainStreak"] = ParticleAtlas::RainStreak;
    textureNameMap_["SmokeWisp"] = ParticleAtlas::SmokeWisp;
    textureNameMap_["IceCrystal"] = ParticleAtlas::IceCrystal;
}

uint8_t SpellEffectsConfig::getTextureIndex(const std::string& name) const {
    auto it = textureNameMap_.find(name);
    if (it != textureNameMap_.end()) {
        return it->second;
    }
    return ParticleAtlas::SoftCircle;
}

bool SpellEffectsConfig::load(const std::string& path) {
    configPath_ = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN(MOD_GRAPHICS, "SpellEffectsConfig: Could not open '{}', using defaults", path);
        setDefaults();
        loaded_ = true;
        return true;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: Failed to parse '{}': {}", path, errors);
        setDefaults();
        loaded_ = true;
        return false;
    }

    // Start with defaults, then override from JSON
    setDefaults();

    // Load global settings
    if (root.isMember("global")) {
        const Json::Value& g = root["global"];
        if (g.isMember("enabled")) global_.enabled = g["enabled"].asBool();
        if (g.isMember("maxParticles")) global_.maxParticles = g["maxParticles"].asInt();
        if (g.isMember("densityMultiplier")) global_.densityMultiplier = g["densityMultiplier"].asFloat();
    }

    // Load resist colors
    if (root.isMember("resistColors")) {
        const Json::Value& rc = root["resistColors"];
        for (const auto& name : rc.getMemberNames()) {
            const Json::Value& arr = rc[name];
            if (arr.isArray() && arr.size() >= 4) {
                resistColors_[name] = glm::vec4(
                    arr[0].asFloat(), arr[1].asFloat(),
                    arr[2].asFloat(), arr[3].asFloat());
            }
        }
    }

    // Load styles (override defaults)
    if (root.isMember("styles")) {
        const Json::Value& stylesJson = root["styles"];
        for (const auto& styleName : stylesJson.getMemberNames()) {
            // Start from existing default if present
            StyleSettings& style = styles_[styleName];
            loadStyle(stylesJson[styleName], styleName, style);
        }
    }

    // Load per-spell overrides
    if (root.isMember("spellOverrides")) {
        loadSpellOverrides(root["spellOverrides"]);
    }

    loaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: Loaded settings from '{}'", path);
    return true;
}

bool SpellEffectsConfig::reload() {
    if (configPath_.empty()) {
        LOG_WARN(MOD_GRAPHICS, "SpellEffectsConfig: No config path set, cannot reload");
        return false;
    }

    bool result = load(configPath_);

    if (result && reloadCallback_) {
        reloadCallback_();
        LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: Reload callback invoked");
    }

    return result;
}

static glm::vec3 readVec3(const Json::Value& arr) {
    if (arr.isArray() && arr.size() >= 3) {
        return glm::vec3(arr[0].asFloat(), arr[1].asFloat(), arr[2].asFloat());
    }
    return glm::vec3(0.0f);
}

void SpellEffectsConfig::loadStyle(const Json::Value& json, const std::string& name, StyleSettings& s) {
    if (json.isMember("spawnRate")) { s.spawnRate = json["spawnRate"].asFloat(); s.hasSpawnRate = true; }
    if (json.isMember("burstCount")) { s.burstCount = json["burstCount"].asInt(); s.hasBurstCount = true; }
    if (json.isMember("emitterLifetime")) { s.emitterLifetime = json["emitterLifetime"].asFloat(); s.hasEmitterLifetime = true; }

    if (json.isMember("spawnShape")) { s.spawnShape = json["spawnShape"].asString(); s.hasSpawnShape = true; }
    if (json.isMember("spawnExtents")) { s.spawnExtents = readVec3(json["spawnExtents"]); s.hasSpawnExtents = true; }

    if (json.isMember("motionType")) { s.motionType = json["motionType"].asString(); s.hasMotionType = true; }
    if (json.isMember("velocityBase")) { s.velocityBase = readVec3(json["velocityBase"]); s.hasVelocityBase = true; }
    if (json.isMember("velocitySpread")) { s.velocitySpread = readVec3(json["velocitySpread"]); s.hasVelocitySpread = true; }

    if (json.isMember("gravity")) { s.gravity = readVec3(json["gravity"]); s.hasGravity = true; }
    if (json.isMember("drag")) { s.drag = json["drag"].asFloat(); s.hasDrag = true; }

    if (json.isMember("colorStartAlpha")) { s.colorStartAlpha = json["colorStartAlpha"].asFloat(); s.hasColorStartAlpha = true; }
    if (json.isMember("colorEndAlpha")) { s.colorEndAlpha = json["colorEndAlpha"].asFloat(); s.hasColorEndAlpha = true; }

    if (json.isMember("sizeStartMin")) { s.sizeStartMin = json["sizeStartMin"].asFloat(); s.hasSizeStartMin = true; }
    if (json.isMember("sizeStartMax")) { s.sizeStartMax = json["sizeStartMax"].asFloat(); s.hasSizeStartMax = true; }
    if (json.isMember("sizeEndMin")) { s.sizeEndMin = json["sizeEndMin"].asFloat(); s.hasSizeEndMin = true; }
    if (json.isMember("sizeEndMax")) { s.sizeEndMax = json["sizeEndMax"].asFloat(); s.hasSizeEndMax = true; }
    if (json.isMember("lifetimeMin")) { s.lifetimeMin = json["lifetimeMin"].asFloat(); s.hasLifetimeMin = true; }
    if (json.isMember("lifetimeMax")) { s.lifetimeMax = json["lifetimeMax"].asFloat(); s.hasLifetimeMax = true; }

    if (json.isMember("blendMode")) { s.blendMode = json["blendMode"].asString(); s.hasBlendMode = true; }
    if (json.isMember("textureRegions")) {
        s.textureRegions.clear();
        const Json::Value& tr = json["textureRegions"];
        for (Json::ArrayIndex i = 0; i < tr.size(); ++i) {
            s.textureRegions.push_back(tr[i].asString());
        }
        s.hasTextureRegions = true;
    }

    if (json.isMember("orbitalRadius")) { s.orbitalRadius = json["orbitalRadius"].asFloat(); s.hasOrbitalRadius = true; }
    if (json.isMember("orbitalAngularVelocity")) { s.orbitalAngularVelocity = json["orbitalAngularVelocity"].asFloat(); s.hasOrbitalAngularVelocity = true; }
    if (json.isMember("expandSpeed")) { s.expandSpeed = json["expandSpeed"].asFloat(); s.hasExpandSpeed = true; }

    if (json.isMember("positionOffset")) { s.positionOffset = readVec3(json["positionOffset"]); s.hasPositionOffset = true; }

    LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: Loaded style '{}'", name);
}

void SpellEffectsConfig::loadSpellOverrides(const Json::Value& root) {
    spellOverrides_.clear();

    for (const auto& spellIdStr : root.getMemberNames()) {
        uint32_t spellId = static_cast<uint32_t>(std::stoul(spellIdStr));
        const Json::Value& spellJson = root[spellIdStr];

        for (const auto& styleName : spellJson.getMemberNames()) {
            StyleSettings override;
            loadStyle(spellJson[styleName], styleName, override);
            spellOverrides_[spellId][styleName] = override;
        }
    }

    if (!spellOverrides_.empty()) {
        LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: Loaded overrides for {} spells",
                 spellOverrides_.size());
    }
}

SpellEffectsConfig::StyleSettings SpellEffectsConfig::mergeOverrides(
        const StyleSettings& base, const StyleSettings& over) {
    StyleSettings result = base;

    if (over.hasSpawnRate) { result.spawnRate = over.spawnRate; result.hasSpawnRate = true; }
    if (over.hasBurstCount) { result.burstCount = over.burstCount; result.hasBurstCount = true; }
    if (over.hasEmitterLifetime) { result.emitterLifetime = over.emitterLifetime; result.hasEmitterLifetime = true; }
    if (over.hasSpawnShape) { result.spawnShape = over.spawnShape; result.hasSpawnShape = true; }
    if (over.hasSpawnExtents) { result.spawnExtents = over.spawnExtents; result.hasSpawnExtents = true; }
    if (over.hasMotionType) { result.motionType = over.motionType; result.hasMotionType = true; }
    if (over.hasVelocityBase) { result.velocityBase = over.velocityBase; result.hasVelocityBase = true; }
    if (over.hasVelocitySpread) { result.velocitySpread = over.velocitySpread; result.hasVelocitySpread = true; }
    if (over.hasGravity) { result.gravity = over.gravity; result.hasGravity = true; }
    if (over.hasDrag) { result.drag = over.drag; result.hasDrag = true; }
    if (over.hasColorStartAlpha) { result.colorStartAlpha = over.colorStartAlpha; result.hasColorStartAlpha = true; }
    if (over.hasColorEndAlpha) { result.colorEndAlpha = over.colorEndAlpha; result.hasColorEndAlpha = true; }
    if (over.hasSizeStartMin) { result.sizeStartMin = over.sizeStartMin; result.hasSizeStartMin = true; }
    if (over.hasSizeStartMax) { result.sizeStartMax = over.sizeStartMax; result.hasSizeStartMax = true; }
    if (over.hasSizeEndMin) { result.sizeEndMin = over.sizeEndMin; result.hasSizeEndMin = true; }
    if (over.hasSizeEndMax) { result.sizeEndMax = over.sizeEndMax; result.hasSizeEndMax = true; }
    if (over.hasLifetimeMin) { result.lifetimeMin = over.lifetimeMin; result.hasLifetimeMin = true; }
    if (over.hasLifetimeMax) { result.lifetimeMax = over.lifetimeMax; result.hasLifetimeMax = true; }
    if (over.hasBlendMode) { result.blendMode = over.blendMode; result.hasBlendMode = true; }
    if (over.hasTextureRegions) { result.textureRegions = over.textureRegions; result.hasTextureRegions = true; }
    if (over.hasOrbitalRadius) { result.orbitalRadius = over.orbitalRadius; result.hasOrbitalRadius = true; }
    if (over.hasOrbitalAngularVelocity) { result.orbitalAngularVelocity = over.orbitalAngularVelocity; result.hasOrbitalAngularVelocity = true; }
    if (over.hasExpandSpeed) { result.expandSpeed = over.expandSpeed; result.hasExpandSpeed = true; }
    if (over.hasPositionOffset) { result.positionOffset = over.positionOffset; result.hasPositionOffset = true; }

    return result;
}

SpellEffectsConfig::StyleSettings SpellEffectsConfig::getStyleSettings(
        const std::string& styleName, uint32_t spellId) const {
    // Find base style
    auto baseIt = styles_.find(styleName);
    if (baseIt == styles_.end()) {
        LOG_WARN(MOD_GRAPHICS, "SpellEffectsConfig: Unknown style '{}', returning empty", styleName);
        return StyleSettings{};
    }

    const StyleSettings& base = baseIt->second;

    // Check for spell-specific overrides
    if (spellId != 0) {
        auto spellIt = spellOverrides_.find(spellId);
        if (spellIt != spellOverrides_.end()) {
            auto styleIt = spellIt->second.find(styleName);
            if (styleIt != spellIt->second.end()) {
                return mergeOverrides(base, styleIt->second);
            }
        }
    }

    return base;
}

glm::vec4 SpellEffectsConfig::getResistColor(uint8_t resistType) const {
    switch (resistType) {
        case 1: return getResistColor("magic");
        case 2: return getResistColor("fire");
        case 3: return getResistColor("cold");
        case 4: return getResistColor("poison");
        case 5: return getResistColor("disease");
        default: return getResistColor("none");
    }
}

glm::vec4 SpellEffectsConfig::getResistColor(const std::string& name) const {
    auto it = resistColors_.find(name);
    if (it != resistColors_.end()) {
        return it->second;
    }
    // Fallback: light blue default
    return glm::vec4(0.59f, 0.71f, 1.0f, 1.0f);
}

// Helper to set all has* flags to true for a fully-populated StyleSettings
static void markAllPresent(SpellEffectsConfig::StyleSettings& s) {
    s.hasSpawnRate = true;
    s.hasBurstCount = true;
    s.hasEmitterLifetime = true;
    s.hasSpawnShape = true;
    s.hasSpawnExtents = true;
    s.hasMotionType = true;
    s.hasVelocityBase = true;
    s.hasVelocitySpread = true;
    s.hasGravity = true;
    s.hasDrag = true;
    s.hasColorStartAlpha = true;
    s.hasColorEndAlpha = true;
    s.hasSizeStartMin = true;
    s.hasSizeStartMax = true;
    s.hasSizeEndMin = true;
    s.hasSizeEndMax = true;
    s.hasLifetimeMin = true;
    s.hasLifetimeMax = true;
    s.hasBlendMode = true;
    s.hasTextureRegions = true;
    s.hasOrbitalRadius = true;
    s.hasOrbitalAngularVelocity = true;
    s.hasExpandSpeed = true;
    s.hasPositionOffset = true;
}

void SpellEffectsConfig::setDefaults() {
    global_.enabled = true;
    global_.maxParticles = 1024;
    global_.densityMultiplier = 1.0f;

    // Resist colors
    resistColors_["none"] = glm::vec4(0.59f, 0.71f, 1.0f, 1.0f);
    resistColors_["magic"] = glm::vec4(0.78f, 0.39f, 1.0f, 1.0f);
    resistColors_["fire"] = glm::vec4(1.0f, 0.47f, 0.2f, 1.0f);
    resistColors_["cold"] = glm::vec4(0.39f, 0.71f, 1.0f, 1.0f);
    resistColors_["poison"] = glm::vec4(0.31f, 0.78f, 0.31f, 1.0f);
    resistColors_["disease"] = glm::vec4(0.59f, 0.39f, 0.2f, 1.0f);
    resistColors_["chromatic"] = glm::vec4(0.59f, 0.71f, 1.0f, 1.0f);
    resistColors_["corruption"] = glm::vec4(0.4f, 0.1f, 0.5f, 1.0f);

    styles_.clear();
    spellOverrides_.clear();

    // castSpray
    {
        StyleSettings& s = styles_["castSpray"];
        s.spawnRate = 15.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "LINEAR";
        s.velocityBase = glm::vec3(4.0f, 0.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.8f, 0.8f, 0.8f);
        s.gravity = glm::vec3(0.0f, -3.0f, 0.0f);
        s.drag = 0.3f;
        s.colorStartAlpha = 0.8f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.04f;
        s.sizeStartMax = 0.08f;
        s.sizeEndMin = 0.02f;
        s.sizeEndMax = 0.04f;
        s.lifetimeMin = 0.4f;
        s.lifetimeMax = 0.7f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "Ember"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f);
        markAllPresent(s);
    }

    // castGlow
    {
        StyleSettings& s = styles_["castGlow"];
        s.spawnRate = 8.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "ORBITAL";
        s.velocityBase = glm::vec3(0.0f, 2.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.0f);
        s.gravity = glm::vec3(0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.7f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.08f;
        s.sizeStartMax = 0.12f;
        s.sizeEndMin = 0.04f;
        s.sizeEndMax = 0.06f;
        s.lifetimeMin = 0.8f;
        s.lifetimeMax = 1.2f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SpellSparkle", "StarShape"};
        s.orbitalRadius = 1.5f;
        s.orbitalAngularVelocity = 4.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f, 0.5f, 0.0f);
        markAllPresent(s);
    }

    // spellComplete
    {
        StyleSettings& s = styles_["spellComplete"];
        s.spawnRate = 0.0f;
        s.burstCount = 15;
        s.emitterLifetime = 0.1f;
        s.spawnShape = "RING";
        s.spawnExtents = glm::vec3(0.5f, 0.0f, 0.0f);
        s.motionType = "BURST";
        s.velocityBase = glm::vec3(3.0f, 1.5f, 0.0f);
        s.velocitySpread = glm::vec3(1.0f, 0.5f, 1.0f);
        s.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.9f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.06f;
        s.sizeStartMax = 0.10f;
        s.sizeEndMin = 0.12f;
        s.sizeEndMax = 0.18f;
        s.lifetimeMin = 0.5f;
        s.lifetimeMax = 0.8f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SpellSparkle", "StarShape"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f);
        markAllPresent(s);
    }

    // impact
    {
        StyleSettings& s = styles_["impact"];
        s.spawnRate = 0.0f;
        s.burstCount = 12;
        s.emitterLifetime = 0.1f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "RADIAL_EXPAND";
        s.velocityBase = glm::vec3(0.0f, 1.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.5f, 0.5f, 0.5f);
        s.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.8f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.05f;
        s.sizeStartMax = 0.08f;
        s.sizeEndMin = 0.10f;
        s.sizeEndMax = 0.15f;
        s.lifetimeMin = 0.4f;
        s.lifetimeMax = 0.8f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "Ember"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 4.0f;
        s.positionOffset = glm::vec3(0.0f, 1.0f, 0.0f);
        markAllPresent(s);
    }

    // buffAura
    {
        StyleSettings& s = styles_["buffAura"];
        s.spawnRate = 3.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "ORBITAL";
        s.velocityBase = glm::vec3(0.0f, 0.5f, 0.0f);
        s.velocitySpread = glm::vec3(0.0f);
        s.gravity = glm::vec3(0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.4f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.06f;
        s.sizeStartMax = 0.08f;
        s.sizeEndMin = 0.10f;
        s.sizeEndMax = 0.14f;
        s.lifetimeMin = 1.5f;
        s.lifetimeMax = 2.5f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SpellSparkle", "StarShape"};
        s.orbitalRadius = 1.2f;
        s.orbitalAngularVelocity = 2.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f, 0.5f, 0.0f);
        markAllPresent(s);
    }

    // projectileTrail
    {
        StyleSettings& s = styles_["projectileTrail"];
        s.spawnRate = 25.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "LINEAR";
        s.velocityBase = glm::vec3(0.0f, 0.3f, 0.0f);
        s.velocitySpread = glm::vec3(0.3f, 0.3f, 0.3f);
        s.gravity = glm::vec3(0.0f);
        s.drag = 2.0f;
        s.colorStartAlpha = 0.9f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.06f;
        s.sizeStartMax = 0.10f;
        s.sizeEndMin = 0.02f;
        s.sizeEndMax = 0.04f;
        s.lifetimeMin = 0.2f;
        s.lifetimeMax = 0.4f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "Ember"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f);
        markAllPresent(s);
    }

    // projectileImpact
    {
        StyleSettings& s = styles_["projectileImpact"];
        s.spawnRate = 0.0f;
        s.burstCount = 12;
        s.emitterLifetime = 0.1f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "RADIAL_EXPAND";
        s.velocityBase = glm::vec3(0.0f, 1.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.5f, 0.5f, 0.5f);
        s.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.8f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.05f;
        s.sizeStartMax = 0.08f;
        s.sizeEndMin = 0.10f;
        s.sizeEndMax = 0.15f;
        s.lifetimeMin = 0.4f;
        s.lifetimeMax = 0.8f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "Ember"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 4.0f;
        s.positionOffset = glm::vec3(0.0f, 1.0f, 0.0f);
        markAllPresent(s);
    }

    // spellRain
    {
        StyleSettings& s = styles_["spellRain"];
        s.spawnRate = 15.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "BOX";
        s.spawnExtents = glm::vec3(1.0f, 0.0f, 1.0f);  // Placeholder; overridden by radius param
        s.motionType = "LINEAR";
        s.velocityBase = glm::vec3(0.0f, -8.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.5f, 1.0f, 0.5f);
        s.gravity = glm::vec3(0.0f, -3.0f, 0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.7f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.03f;
        s.sizeStartMax = 0.06f;
        s.sizeEndMin = 0.02f;
        s.sizeEndMax = 0.04f;
        s.lifetimeMin = 0.6f;
        s.lifetimeMax = 1.2f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "RainStreak"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f, 10.0f, 0.0f);
        markAllPresent(s);
    }

    // groundCircle
    {
        StyleSettings& s = styles_["groundCircle"];
        s.spawnRate = 6.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "RING";
        s.spawnExtents = glm::vec3(1.0f, 0.0f, 0.0f);  // Placeholder; overridden by radius param
        s.motionType = "ORBITAL";
        s.velocityBase = glm::vec3(0.0f, 0.5f, 0.0f);
        s.velocitySpread = glm::vec3(0.0f);
        s.gravity = glm::vec3(0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.6f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.05f;
        s.sizeStartMax = 0.08f;
        s.sizeEndMin = 0.08f;
        s.sizeEndMax = 0.12f;
        s.lifetimeMin = 1.2f;
        s.lifetimeMax = 2.0f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SpellSparkle", "StarShape"};
        s.orbitalRadius = 1.0f;  // Placeholder; overridden by radius param
        s.orbitalAngularVelocity = 1.5f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f, 0.3f, 0.0f);
        markAllPresent(s);
    }

    LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: Using default settings");
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
