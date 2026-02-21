#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/particle_types.h"

namespace EQT {
namespace Graphics {
namespace Environment {
namespace WeatherPresets {

// Rain — camera-relative falling rain streaks
// Additive blend for bright streaks against dark storm sky
EmitterConfig Rain(uint8_t intensity) {
    EmitterConfig c;
    c.motionType = MotionType::CAMERA_RELATIVE;
    c.targetCount = 200 + static_cast<int>(intensity) * 20;
    c.spawnRate = 0.0f;  // Not used for target-count mode
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;  // Permanent (managed by weather controller)

    c.spawnShape = SpawnShape::BOX;
    c.spawnVolumeHalfExtents = glm::vec3(30.0f, 10.0f, 20.0f);
    c.spawnVolumeTopBias = 0.9f;

    // Fast downward fall (Irrlicht Y-up: negative Y = down)
    c.velocityBase = glm::vec3(0.0f, -25.0f, 0.0f);
    c.velocitySpread = glm::vec3(1.5f, 3.0f, 1.5f);
    c.gravity = glm::vec3(0.0f, -5.0f, 0.0f);
    c.drag = 0.0f;
    c.windResponse = 0.8f;

    // Blue-white streaks, fade out near end of life
    c.colorStart = glm::vec4(0.7f, 0.75f, 0.85f, 0.6f);
    c.colorEnd = glm::vec4(0.7f, 0.75f, 0.85f, 0.2f);

    // World-space size — perspective-scaled in VS by: screenPixels = size * screenHeight / clipW
    // At 720p, size 0.08 at dist 15 = 0.08*720/15 = 3.8px (thin streak)
    // At dist 5 (close) = 0.08*720/5 = 11.5px, at dist 40 (far) = 1.4px
    c.sizeStartMin = 0.04f;
    c.sizeStartMax = 0.12f;
    c.sizeEndMin = 0.04f;
    c.sizeEndMax = 0.12f;
    c.lifetimeMin = 0.4f;
    c.lifetimeMax = 0.8f;

    c.rendererType = UnifiedRendererType::POINT_SPRITE;
    c.blendMode = UnifiedBlendMode::ADDITIVE;

    c.textureRegions[0] = ParticleAtlas::RainStreak;
    c.textureRegionCount = 1;

    // No drift/twinkle for rain
    c.driftFrequency = 0.0f;
    c.driftAmplitude = 0.0f;
    c.twinkleSpeed = 0.0f;
    c.sizeSpeedCorrelation = 0.0f;

    return c;
}

// Snow — camera-relative drifting snowflakes
// Alpha blend for soft, opaque-ish flakes
EmitterConfig Snow(uint8_t intensity) {
    EmitterConfig c;
    c.motionType = MotionType::CAMERA_RELATIVE;
    c.targetCount = 100 + static_cast<int>(intensity) * 15;
    c.spawnRate = 0.0f;
    c.burstCount = 0;
    c.emitterLifetime = 0.0f;

    c.spawnShape = SpawnShape::BOX;
    c.spawnVolumeHalfExtents = glm::vec3(40.0f, 15.0f, 30.0f);
    c.spawnVolumeTopBias = 0.7f;

    // Slow downward drift (Irrlicht Y-up)
    c.velocityBase = glm::vec3(0.0f, -3.0f, 0.0f);
    c.velocitySpread = glm::vec3(0.5f, 1.0f, 0.5f);
    c.gravity = glm::vec3(0.0f, -0.5f, 0.0f);
    c.drag = 0.3f;
    c.windResponse = 1.0f;

    // White flakes, gentle alpha fade
    c.colorStart = glm::vec4(1.0f, 1.0f, 1.0f, 0.7f);
    c.colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, 0.3f);

    // World-space size — perspective-scaled in VS
    // At 720p, size 0.15 at dist 15 = 0.15*720/15 = 7.2px (soft flake)
    // At dist 5 (close) = 0.15*720/5 = 21.6px, at dist 40 (far) = 2.7px
    c.sizeStartMin = 0.06f;
    c.sizeStartMax = 0.20f;
    c.sizeEndMin = 0.06f;
    c.sizeEndMax = 0.20f;
    c.lifetimeMin = 2.0f;
    c.lifetimeMax = 5.0f;

    c.rendererType = UnifiedRendererType::POINT_SPRITE;
    c.blendMode = UnifiedBlendMode::ALPHA;

    // Mix of snowflake and soft circle textures
    c.textureRegions[0] = ParticleAtlas::Snowflake;
    c.textureRegions[1] = ParticleAtlas::SoftCircle;
    c.textureRegionCount = 2;

    // Snow-specific motion
    c.driftFrequency = 1.5f;
    c.driftAmplitude = 0.8f;
    c.twinkleSpeed = 3.0f;
    c.sizeSpeedCorrelation = 0.4f;

    return c;
}

} // namespace WeatherPresets
} // namespace Environment
} // namespace Graphics
} // namespace EQT
