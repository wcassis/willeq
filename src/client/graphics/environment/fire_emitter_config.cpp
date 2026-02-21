#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/particle_types.h"

namespace EQT {
namespace Graphics {
namespace Environment {
namespace FirePresets {

// Torch — small, focused flame for wall torches, sconces, candelabras
// ~12 particles/sec, short-lived, rising embers
EmitterConfig Torch() {
    EmitterConfig c;
    c.spawnRate = 12.0f;
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;  // Permanent

    c.spawnShape = SpawnShape::POINT;
    c.spawnExtents = glm::vec3(0.0f);

    // Rise upward (Irrlicht Y-up)
    c.velocityBase = glm::vec3(0.0f, 2.5f, 0.0f);
    c.velocitySpread = glm::vec3(0.3f, 0.5f, 0.3f);

    // Slight upward gravity (buoyancy), moderate drag
    c.gravity = glm::vec3(0.0f, 0.3f, 0.0f);
    c.drag = 0.3f;
    c.windResponse = 0.2f;

    c.motionType = MotionType::LINEAR;

    // Yellow-white at birth → red-transparent at death
    c.colorStart = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f);
    c.colorEnd = glm::vec4(1.0f, 0.2f, 0.0f, 0.0f);

    c.sizeStartMin = 0.3f;
    c.sizeStartMax = 0.5f;
    c.sizeEndMin = 0.6f;
    c.sizeEndMax = 0.8f;
    c.lifetimeMin = 0.3f;
    c.lifetimeMax = 0.6f;

    c.rendererType = UnifiedRendererType::POINT_SPRITE;
    c.blendMode = UnifiedBlendMode::ADDITIVE;

    // SoftCircle (0) and Ember (7)
    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::Ember;
    c.textureRegionCount = 2;

    return c;
}

// CampfireFlame — larger, wider flame for campfires, fire pits, braziers
// ~20 particles/sec, BOX spawn for wider base
EmitterConfig CampfireFlame() {
    EmitterConfig c;
    c.spawnRate = 20.0f;
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;

    c.spawnShape = SpawnShape::BOX;
    c.spawnExtents = glm::vec3(0.8f, 0.1f, 0.8f);  // Wide base, thin vertical

    // Rise upward
    c.velocityBase = glm::vec3(0.0f, 3.0f, 0.0f);
    c.velocitySpread = glm::vec3(0.5f, 0.8f, 0.5f);

    c.gravity = glm::vec3(0.0f, 0.4f, 0.0f);
    c.drag = 0.25f;
    c.windResponse = 0.15f;

    c.motionType = MotionType::LINEAR;

    // Bright yellow → deep red fade
    c.colorStart = glm::vec4(1.0f, 0.85f, 0.4f, 1.0f);
    c.colorEnd = glm::vec4(0.9f, 0.15f, 0.0f, 0.0f);

    c.sizeStartMin = 0.4f;
    c.sizeStartMax = 0.7f;
    c.sizeEndMin = 0.8f;
    c.sizeEndMax = 1.1f;
    c.lifetimeMin = 0.4f;
    c.lifetimeMax = 0.8f;

    c.rendererType = UnifiedRendererType::POINT_SPRITE;
    c.blendMode = UnifiedBlendMode::ADDITIVE;

    c.textureRegions[0] = ParticleAtlas::SoftCircle;
    c.textureRegions[1] = ParticleAtlas::Ember;
    c.textureRegionCount = 2;

    return c;
}

// CampfireEmber — small, bright sparks that arc upward from campfires
// ~5 particles/sec, faster initial velocity, longer-lived, smaller
EmitterConfig CampfireEmber() {
    EmitterConfig c;
    c.spawnRate = 5.0f;
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;

    c.spawnShape = SpawnShape::BOX;
    c.spawnExtents = glm::vec3(0.5f, 0.0f, 0.5f);

    // Fast upward launch with more spread
    c.velocityBase = glm::vec3(0.0f, 4.0f, 0.0f);
    c.velocitySpread = glm::vec3(1.5f, 1.5f, 1.5f);

    // Negative gravity (decelerating rise, then fall) and drag
    c.gravity = glm::vec3(0.0f, -1.5f, 0.0f);
    c.drag = 0.15f;
    c.windResponse = 0.4f;

    c.motionType = MotionType::LINEAR;

    // Bright orange → dim red
    c.colorStart = glm::vec4(1.0f, 0.6f, 0.1f, 1.0f);
    c.colorEnd = glm::vec4(0.5f, 0.1f, 0.0f, 0.0f);

    c.sizeStartMin = 0.15f;
    c.sizeStartMax = 0.25f;
    c.sizeEndMin = 0.06f;
    c.sizeEndMax = 0.12f;
    c.lifetimeMin = 0.8f;
    c.lifetimeMax = 1.5f;

    c.rendererType = UnifiedRendererType::POINT_SPRITE;
    c.blendMode = UnifiedBlendMode::ADDITIVE;

    // Ember only
    c.textureRegions[0] = ParticleAtlas::Ember;
    c.textureRegionCount = 1;

    return c;
}

} // namespace FirePresets
} // namespace Environment
} // namespace Graphics
} // namespace EQT
