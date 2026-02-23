#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/particle_types.h"

namespace EQT {
namespace Graphics {
namespace Environment {
namespace SpellPresets {

// === Helpers ===

static SpawnShape parseSpawnShape(const std::string& s) {
    if (s == "BOX") return SpawnShape::BOX;
    if (s == "SPHERE") return SpawnShape::SPHERE;
    if (s == "RING") return SpawnShape::RING;
    return SpawnShape::POINT;
}

static MotionType parseMotionType(const std::string& s) {
    if (s == "LINEAR") return MotionType::LINEAR;
    if (s == "ORBITAL") return MotionType::ORBITAL;
    if (s == "RADIAL_EXPAND") return MotionType::RADIAL_EXPAND;
    if (s == "BURST") return MotionType::BURST;
    if (s == "CAMERA_RELATIVE") return MotionType::CAMERA_RELATIVE;
    return MotionType::LINEAR;
}

static UnifiedBlendMode parseBlendMode(const std::string& s) {
    if (s == "ALPHA") return UnifiedBlendMode::ALPHA;
    return UnifiedBlendMode::ADDITIVE;
}

// Apply a StyleSettings to an EmitterConfig, using color for start/end RGBA
static void applyStyle(const SpellEffectsConfig::StyleSettings& s, EmitterConfig& c, glm::vec4 color) {
    const auto& cfg = SpellEffectsConfig::instance();
    float density = cfg.getGlobal().densityMultiplier;

    if (s.hasMotionType) c.motionType = parseMotionType(s.motionType);
    if (s.hasSpawnRate) c.spawnRate = s.spawnRate * density;
    if (s.hasBurstCount) c.burstCount = static_cast<int>(s.burstCount * density);
    if (s.hasEmitterLifetime) c.emitterLifetime = s.emitterLifetime;

    if (s.hasSpawnShape) c.spawnShape = parseSpawnShape(s.spawnShape);
    if (s.hasSpawnExtents) c.spawnExtents = s.spawnExtents;

    if (s.hasVelocityBase) c.velocityBase = s.velocityBase;
    if (s.hasVelocitySpread) c.velocitySpread = s.velocitySpread;

    if (s.hasGravity) c.gravity = s.gravity;
    if (s.hasDrag) c.drag = s.drag;

    c.colorStart = glm::vec4(color.r, color.g, color.b,
        s.hasColorStartAlpha ? s.colorStartAlpha : 0.8f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b,
        s.hasColorEndAlpha ? s.colorEndAlpha : 0.0f);

    if (s.hasSizeStartMin) c.sizeStartMin = s.sizeStartMin;
    if (s.hasSizeStartMax) c.sizeStartMax = s.sizeStartMax;
    if (s.hasSizeEndMin) c.sizeEndMin = s.sizeEndMin;
    if (s.hasSizeEndMax) c.sizeEndMax = s.sizeEndMax;
    if (s.hasLifetimeMin) c.lifetimeMin = s.lifetimeMin;
    if (s.hasLifetimeMax) c.lifetimeMax = s.lifetimeMax;

    if (s.hasBlendMode) c.blendMode = parseBlendMode(s.blendMode);

    if (s.hasTextureRegions && !s.textureRegions.empty()) {
        c.textureRegionCount = static_cast<uint8_t>(
            std::min(s.textureRegions.size(), size_t(4)));
        for (uint8_t i = 0; i < c.textureRegionCount; ++i) {
            c.textureRegions[i] = cfg.getTextureIndex(s.textureRegions[i]);
        }
    }

    if (s.hasOrbitalRadius) c.orbitalRadius = s.orbitalRadius;
    if (s.hasOrbitalAngularVelocity) c.orbitalAngularVelocity = s.orbitalAngularVelocity;
    if (s.hasExpandSpeed) c.expandSpeed = s.expandSpeed;
}

// === Preset Functions ===

SpellEffectDef CastSpray(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("castSpray", spellId);

    SpellEffectDef def;
    def.name = "CastSpray";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

    applyStyle(s, ed.config, color);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef CastGlow(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("castGlow", spellId);

    SpellEffectDef def;
    def.name = "CastGlow";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 0.5f, 0.0f);

    applyStyle(s, ed.config, color);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef SpellComplete(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("spellComplete", spellId);

    SpellEffectDef def;
    def.name = "SpellComplete";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

    applyStyle(s, ed.config, color);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef Impact(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("impact", spellId);

    SpellEffectDef def;
    def.name = "Impact";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::TARGET;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 1.0f, 0.0f);

    applyStyle(s, ed.config, color);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef BuffAura(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("buffAura", spellId);

    SpellEffectDef def;
    def.name = "BuffAura";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 0.5f, 0.0f);

    applyStyle(s, ed.config, color);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef Projectile(glm::vec4 color, uint32_t spellId) {
    SpellEffectDef def;
    def.name = "Projectile";

    // Emitter 0: Trail particles (attached to the moving projectile)
    {
        auto s = SpellEffectsConfig::instance().getStyleSettings("projectileTrail", spellId);

        SpellEmitterDef ed;
        ed.trigger = SpellTrigger::IMMEDIATE;
        ed.attach = SpellAttach::PROJECTILE_PATH;
        ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

        applyStyle(s, ed.config, color);

        def.emitters.push_back(ed);
    }

    // Emitter 1: Impact burst on arrival (triggers when projectile hits)
    {
        auto s = SpellEffectsConfig::instance().getStyleSettings("projectileImpact", spellId);

        SpellEmitterDef ed;
        ed.trigger = SpellTrigger::ON_HIT;
        ed.attach = SpellAttach::TARGET;
        ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 1.0f, 0.0f);

        applyStyle(s, ed.config, color);

        def.emitters.push_back(ed);
    }

    return def;
}

SpellEffectDef SpellRain(glm::vec4 color, float radius, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("spellRain", spellId);

    SpellEffectDef def;
    def.name = "SpellRain";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::GROUND_TARGET;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 10.0f, 0.0f);

    applyStyle(s, ed.config, color);

    // Override spawn extents with the radius parameter
    ed.config.spawnExtents = glm::vec3(radius, 0.0f, radius);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef GroundCircle(glm::vec4 color, float radius, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("groundCircle", spellId);

    SpellEffectDef def;
    def.name = "GroundCircle";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::GROUND_TARGET;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 0.3f, 0.0f);

    applyStyle(s, ed.config, color);

    // Override radius-dependent fields with the radius parameter
    ed.config.spawnExtents = glm::vec3(radius, 0.0f, 0.0f);
    ed.config.orbitalRadius = radius;

    def.emitters.push_back(ed);
    return def;
}

// Map resist type to particle color — delegates to config
glm::vec4 resistToParticleColor(uint8_t resistType) {
    return SpellEffectsConfig::instance().getResistColor(resistType);
}

} // namespace SpellPresets
} // namespace Environment
} // namespace Graphics
} // namespace EQT
