#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/particle_types.h"

namespace EQT {
namespace Graphics {
namespace Environment {
namespace SpellPresets {

// CastSpray — LINEAR particles sprayed from hands like a garden hose
// Direction is set dynamically from bone orientation via dynamicDirection
SpellEffectDef CastSpray(glm::vec4 color) {
    SpellEffectDef def;
    def.name = "CastSpray";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = glm::vec3(0.0f);  // Bone position is already accurate

    EmitterConfig& c = ed.config;
    c.motionType = MotionType::LINEAR;  // Straight-line with gravity
    c.spawnRate = 15.0f;                // Medium-intensity stream
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;           // Controlled by cast duration

    c.spawnShape = SpawnShape::POINT;

    // Speed along bone direction (set dynamically, used as magnitude)
    c.velocityBase = glm::vec3(4.0f, 0.0f, 0.0f);
    // Cone spread — creates the garden hose spray cone
    c.velocitySpread = glm::vec3(0.8f, 0.8f, 0.8f);

    c.gravity = glm::vec3(0.0f, -3.0f, 0.0f);  // Droplets arc downward
    c.drag = 0.3f;                                // Slight air resistance

    c.colorStart = glm::vec4(color.r, color.g, color.b, 0.8f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

    c.sizeStartMin = 0.04f;
    c.sizeStartMax = 0.08f;
    c.sizeEndMin = 0.02f;
    c.sizeEndMax = 0.04f;
    c.lifetimeMin = 0.4f;
    c.lifetimeMax = 0.7f;

    c.blendMode = UnifiedBlendMode::ADDITIVE;
    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::Ember;
    c.textureRegionCount = 2;

    def.emitters.push_back(ed);
    return def;
}

// CastGlow — ORBITAL particles around caster during casting (renamed: "smolder")
SpellEffectDef CastGlow(glm::vec4 color) {
    SpellEffectDef def;
    def.name = "CastGlow";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = glm::vec3(0.0f, 0.5f, 0.0f);

    EmitterConfig& c = ed.config;
    c.motionType = MotionType::ORBITAL;
    c.spawnRate = 8.0f;
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;  // Controlled externally by cast duration

    c.spawnShape = SpawnShape::POINT;

    c.orbitalRadius = 1.5f;
    c.orbitalAngularVelocity = 4.0f;
    c.velocityBase = glm::vec3(0.0f, 2.0f, 0.0f);  // Upward drift
    c.velocitySpread = glm::vec3(0.0f);

    c.gravity = glm::vec3(0.0f);
    c.drag = 0.0f;

    c.colorStart = glm::vec4(color.r, color.g, color.b, 0.7f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

    c.sizeStartMin = 0.08f;
    c.sizeStartMax = 0.12f;
    c.sizeEndMin = 0.04f;
    c.sizeEndMax = 0.06f;
    c.lifetimeMin = 0.8f;
    c.lifetimeMax = 1.2f;

    c.blendMode = UnifiedBlendMode::ADDITIVE;
    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::StarShape;
    c.textureRegionCount = 2;

    def.emitters.push_back(ed);
    return def;
}

// SpellComplete — BURST at caster on successful cast
SpellEffectDef SpellComplete(glm::vec4 color) {
    SpellEffectDef def;
    def.name = "SpellComplete";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = glm::vec3(0.0f);  // Entity callback already provides chest-height position

    EmitterConfig& c = ed.config;
    c.motionType = MotionType::BURST;
    c.spawnRate = 0.0f;
    c.burstCount = 15;
    c.emitterLifetime = 0.1f;  // One-shot

    c.spawnShape = SpawnShape::RING;
    c.spawnExtents = glm::vec3(0.5f, 0.0f, 0.0f);

    c.velocityBase = glm::vec3(3.0f, 1.5f, 0.0f);
    c.velocitySpread = glm::vec3(1.0f, 0.5f, 1.0f);

    c.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
    c.drag = 0.0f;

    c.colorStart = glm::vec4(color.r, color.g, color.b, 0.9f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

    c.sizeStartMin = 0.06f;
    c.sizeStartMax = 0.10f;
    c.sizeEndMin = 0.12f;
    c.sizeEndMax = 0.18f;
    c.lifetimeMin = 0.5f;
    c.lifetimeMax = 0.8f;

    c.blendMode = UnifiedBlendMode::ADDITIVE;
    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::StarShape;
    c.textureRegionCount = 2;

    def.emitters.push_back(ed);
    return def;
}

// Impact — RADIAL_EXPAND at target
SpellEffectDef Impact(glm::vec4 color) {
    SpellEffectDef def;
    def.name = "Impact";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::TARGET;
    ed.positionOffset = glm::vec3(0.0f, 1.0f, 0.0f);

    EmitterConfig& c = ed.config;
    c.motionType = MotionType::RADIAL_EXPAND;
    c.spawnRate = 0.0f;
    c.burstCount = 12;
    c.emitterLifetime = 0.1f;  // One-shot

    c.spawnShape = SpawnShape::POINT;

    c.expandSpeed = 4.0f;
    c.velocityBase = glm::vec3(0.0f, 1.0f, 0.0f);  // Slight upward
    c.velocitySpread = glm::vec3(0.5f, 0.5f, 0.5f);

    c.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
    c.drag = 0.0f;

    c.colorStart = glm::vec4(color.r, color.g, color.b, 0.8f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

    c.sizeStartMin = 0.05f;
    c.sizeStartMax = 0.08f;
    c.sizeEndMin = 0.10f;
    c.sizeEndMax = 0.15f;
    c.lifetimeMin = 0.4f;
    c.lifetimeMax = 0.8f;

    c.blendMode = UnifiedBlendMode::ADDITIVE;
    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::Ember;
    c.textureRegionCount = 2;

    def.emitters.push_back(ed);
    return def;
}

// BuffAura — persistent ORBITAL around entity
SpellEffectDef BuffAura(glm::vec4 color) {
    SpellEffectDef def;
    def.name = "BuffAura";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = glm::vec3(0.0f, 0.5f, 0.0f);

    EmitterConfig& c = ed.config;
    c.motionType = MotionType::ORBITAL;
    c.spawnRate = 3.0f;
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;  // Permanent until removed

    c.spawnShape = SpawnShape::POINT;

    c.orbitalRadius = 1.2f;
    c.orbitalAngularVelocity = 2.0f;
    c.velocityBase = glm::vec3(0.0f, 0.5f, 0.0f);  // Gentle upward drift
    c.velocitySpread = glm::vec3(0.0f);

    c.gravity = glm::vec3(0.0f);
    c.drag = 0.0f;

    c.colorStart = glm::vec4(color.r, color.g, color.b, 0.4f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

    c.sizeStartMin = 0.06f;
    c.sizeStartMax = 0.08f;
    c.sizeEndMin = 0.10f;
    c.sizeEndMax = 0.14f;
    c.lifetimeMin = 1.5f;
    c.lifetimeMax = 2.5f;

    c.blendMode = UnifiedBlendMode::ADDITIVE;
    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::StarShape;
    c.textureRegionCount = 2;

    def.emitters.push_back(ed);
    return def;
}

// Projectile — trail particles along travel path + impact burst on arrival
SpellEffectDef Projectile(glm::vec4 color) {
    SpellEffectDef def;
    def.name = "Projectile";

    // Emitter 0: Trail particles (attached to the moving projectile)
    {
        SpellEmitterDef ed;
        ed.trigger = SpellTrigger::IMMEDIATE;
        ed.attach = SpellAttach::PROJECTILE_PATH;
        ed.positionOffset = glm::vec3(0.0f);

        EmitterConfig& c = ed.config;
        c.motionType = MotionType::LINEAR;
        c.spawnRate = 25.0f;
        c.burstCount = 0;
        c.emitterLifetime = 0.0f;  // Overridden by caller to match travel duration

        c.spawnShape = SpawnShape::POINT;

        c.velocityBase = glm::vec3(0.0f, 0.3f, 0.0f);  // Slight upward drift
        c.velocitySpread = glm::vec3(0.3f, 0.3f, 0.3f);

        c.gravity = glm::vec3(0.0f);
        c.drag = 2.0f;  // Particles linger behind quickly

        c.colorStart = glm::vec4(color.r, color.g, color.b, 0.9f);
        c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

        c.sizeStartMin = 0.06f;
        c.sizeStartMax = 0.10f;
        c.sizeEndMin = 0.02f;
        c.sizeEndMax = 0.04f;
        c.lifetimeMin = 0.2f;
        c.lifetimeMax = 0.4f;

        c.blendMode = UnifiedBlendMode::ADDITIVE;
        c.textureRegions[0] = ParticleAtlas::SoftCircle;
        c.textureRegions[1] = ParticleAtlas::Ember;
        c.textureRegionCount = 2;

        def.emitters.push_back(ed);
    }

    // Emitter 1: Impact burst on arrival (triggers when projectile hits)
    {
        SpellEmitterDef ed;
        ed.trigger = SpellTrigger::ON_HIT;
        ed.attach = SpellAttach::TARGET;
        ed.positionOffset = glm::vec3(0.0f, 1.0f, 0.0f);

        EmitterConfig& c = ed.config;
        c.motionType = MotionType::RADIAL_EXPAND;
        c.spawnRate = 0.0f;
        c.burstCount = 12;
        c.emitterLifetime = 0.1f;

        c.spawnShape = SpawnShape::POINT;

        c.expandSpeed = 4.0f;
        c.velocityBase = glm::vec3(0.0f, 1.0f, 0.0f);
        c.velocitySpread = glm::vec3(0.5f, 0.5f, 0.5f);

        c.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
        c.drag = 0.0f;

        c.colorStart = glm::vec4(color.r, color.g, color.b, 0.8f);
        c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

        c.sizeStartMin = 0.05f;
        c.sizeStartMax = 0.08f;
        c.sizeEndMin = 0.10f;
        c.sizeEndMax = 0.15f;
        c.lifetimeMin = 0.4f;
        c.lifetimeMax = 0.8f;

        c.blendMode = UnifiedBlendMode::ADDITIVE;
        c.textureRegions[0] = ParticleAtlas::SoftCircle;
        c.textureRegions[1] = ParticleAtlas::Ember;
        c.textureRegionCount = 2;

        def.emitters.push_back(ed);
    }

    return def;
}

// SpellRain — falling particles in a box area (AE rain spells)
SpellEffectDef SpellRain(glm::vec4 color, float radius) {
    SpellEffectDef def;
    def.name = "SpellRain";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::GROUND_TARGET;
    ed.positionOffset = glm::vec3(0.0f, 10.0f, 0.0f);  // Spawn above target

    EmitterConfig& c = ed.config;
    c.motionType = MotionType::LINEAR;
    c.spawnRate = 15.0f;
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;  // Overridden by caller

    c.spawnShape = SpawnShape::BOX;
    c.spawnExtents = glm::vec3(radius, 0.0f, radius);

    c.velocityBase = glm::vec3(0.0f, -8.0f, 0.0f);  // Falling down
    c.velocitySpread = glm::vec3(0.5f, 1.0f, 0.5f);

    c.gravity = glm::vec3(0.0f, -3.0f, 0.0f);
    c.drag = 0.0f;

    c.colorStart = glm::vec4(color.r, color.g, color.b, 0.7f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

    c.sizeStartMin = 0.03f;
    c.sizeStartMax = 0.06f;
    c.sizeEndMin = 0.02f;
    c.sizeEndMax = 0.04f;
    c.lifetimeMin = 0.6f;
    c.lifetimeMax = 1.2f;

    c.blendMode = UnifiedBlendMode::ADDITIVE;
    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::RainStreak;
    c.textureRegionCount = 2;

    def.emitters.push_back(ed);
    return def;
}

// GroundCircle — orbital ring of particles at ground level (AE indicator)
SpellEffectDef GroundCircle(glm::vec4 color, float radius) {
    SpellEffectDef def;
    def.name = "GroundCircle";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::GROUND_TARGET;
    ed.positionOffset = glm::vec3(0.0f, 0.3f, 0.0f);  // Slightly above ground

    EmitterConfig& c = ed.config;
    c.motionType = MotionType::ORBITAL;
    c.spawnRate = 6.0f;
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;  // Overridden by caller

    c.spawnShape = SpawnShape::RING;
    c.spawnExtents = glm::vec3(radius, 0.0f, 0.0f);

    c.orbitalRadius = radius;
    c.orbitalAngularVelocity = 1.5f;
    c.velocityBase = glm::vec3(0.0f, 0.5f, 0.0f);  // Gentle upward drift
    c.velocitySpread = glm::vec3(0.0f);

    c.gravity = glm::vec3(0.0f);
    c.drag = 0.0f;

    c.colorStart = glm::vec4(color.r, color.g, color.b, 0.6f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b, 0.0f);

    c.sizeStartMin = 0.05f;
    c.sizeStartMax = 0.08f;
    c.sizeEndMin = 0.08f;
    c.sizeEndMax = 0.12f;
    c.lifetimeMin = 1.2f;
    c.lifetimeMax = 2.0f;

    c.blendMode = UnifiedBlendMode::ADDITIVE;
    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::StarShape;
    c.textureRegionCount = 2;

    def.emitters.push_back(ed);
    return def;
}

// Map resist type to particle color
glm::vec4 resistToParticleColor(uint8_t resistType) {
    switch (resistType) {
        case 2:  // Fire
            return glm::vec4(1.0f, 0.47f, 0.2f, 1.0f);
        case 3:  // Cold
            return glm::vec4(0.39f, 0.71f, 1.0f, 1.0f);
        case 4:  // Poison
            return glm::vec4(0.31f, 0.78f, 0.31f, 1.0f);
        case 5:  // Disease
            return glm::vec4(0.59f, 0.39f, 0.2f, 1.0f);
        case 1:  // Magic
            return glm::vec4(0.78f, 0.39f, 1.0f, 1.0f);
        default: // None, Physical, Chromatic, etc.
            return glm::vec4(0.59f, 0.71f, 1.0f, 1.0f);
    }
}

} // namespace SpellPresets
} // namespace Environment
} // namespace Graphics
} // namespace EQT
