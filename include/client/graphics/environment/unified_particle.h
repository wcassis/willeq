#pragma once

// Unified Particle System — Phase 1: Core data structures
// Point-sprite-based particle rendering for GLES2 (Mali 400 / embedded GPUs)
// Coexists with the existing billboard particle path (ambient/environmental effects)

#include <glm/glm.hpp>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace Environment {

// === Enums ===

enum class MotionType : uint8_t {
    LINEAR = 0,           // pos += vel * dt; vel += gravity * dt; vel *= (1 - drag*dt)
    CAMERA_RELATIVE = 1,  // Spawns/recycles within volume around camera (rain, snow)
    RADIAL_EXPAND = 2,    // Outward from center in XZ plane (spell impacts)
    ORBITAL = 3,          // Orbit center with vertical drift (cast glows, auras)
    BURST = 4,            // One-shot scatter (same physics as LINEAR)
};

enum class UnifiedBlendMode : uint8_t {
    ADDITIVE = 0,   // glBlendFunc(GL_ONE, GL_ONE) — fire, sparks, holy, lightning
    ALPHA = 1,      // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) — smoke, snow
};

enum class UnifiedRendererType : uint8_t {
    POINT_SPRITE = 0,   // GL_POINTS with gl_PointSize + gl_PointCoord
    BATCHED_QUAD = 1,   // Future: indexed quads for larger/shaped particles
};

enum class SpawnShape : uint8_t {
    POINT = 0,  // Spawn at emitter position
    BOX = 1,    // Random within box half-extents
    SPHERE = 2, // Random within sphere of radius spawnExtents.x
    RING = 3,   // Random angle at fixed radius spawnExtents.x
};

// === Particle Structure ===

// Single unified particle instance (~96 bytes)
// Stored contiguously in a fixed pool for cache-friendly linear scan
struct UnifiedParticle {
    // Core state — 48 bytes
    glm::vec3 position{0.0f};       // World position (Irrlicht Y-up)
    glm::vec3 velocity{0.0f};       // Units per second
    glm::vec4 color{1.0f};          // Current RGBA
    float size = 0.0f;              // Current point size (screen pixels before perspective)
    float age = 0.0f;               // Seconds since spawn
    float maxLifetime = 1.0f;       // Seconds until death

    // Spawn parameters for interpolation — 24 bytes
    glm::vec4 colorStart{1.0f};     // Color at birth
    glm::vec4 colorEnd{0.0f};       // Color at death
    float sizeStart = 1.0f;         // Size at birth
    float sizeEnd = 2.0f;           // Size at death

    // Motion parameters — 16 bytes
    float phase = 0.0f;             // Random phase for oscillation
    float angularVelocity = 0.0f;   // For orbital/helical motion
    float radius = 0.0f;            // Current orbital radius
    float drag = 0.0f;              // Velocity damping per second

    // Metadata — 8 bytes
    uint16_t emitterID = 0;         // Which emitter owns this particle
    uint16_t textureIndex = 0;      // Region within atlas (UV offset selector)
    uint8_t flags = 0;              // Bit 0: alive, Bit 1: blend mode (0=additive, 1=alpha)
    MotionType motionType = MotionType::LINEAR;
    uint16_t padding = 0;

    // UV rotation for rain streaks (radians, 0 = no rotation)
    float rotation = 0.0f;

    bool isAlive() const { return (flags & 0x01) != 0; }
    void setAlive(bool alive) { if (alive) flags |= 0x01; else flags &= ~0x01; }

    UnifiedBlendMode getBlendMode() const {
        return (flags & 0x02) ? UnifiedBlendMode::ALPHA : UnifiedBlendMode::ADDITIVE;
    }
    void setBlendMode(UnifiedBlendMode mode) {
        if (mode == UnifiedBlendMode::ALPHA) flags |= 0x02; else flags &= ~0x02;
    }

    // Get normalized age (0 = just spawned, 1 = about to die)
    float getNormalizedAge() const {
        return maxLifetime > 0.0f ? age / maxLifetime : 1.0f;
    }
};

// === Emitter Configuration ===

// Data-driven emitter definition. Same structure serves fire, weather, and spells.
struct EmitterConfig {
    // Spawning
    float spawnRate = 10.0f;         // Particles per second (0 = burst mode)
    int burstCount = 0;              // For burst mode: how many at once
    float emitterLifetime = 0.0f;    // 0 = permanent (torches), >0 = temporary (spells)

    // Spawn volume
    SpawnShape spawnShape = SpawnShape::POINT;
    glm::vec3 spawnExtents{0.0f};    // Half-extents for BOX

    // Initial velocity
    glm::vec3 velocityBase{0.0f, 2.5f, 0.0f};   // Base velocity (Irrlicht Y-up)
    glm::vec3 velocitySpread{0.0f};               // Random spread per-axis

    // Forces
    glm::vec3 gravity{0.0f};        // Acceleration (Y-up; positive Y = up)
    float drag = 0.0f;              // Velocity damping per second (0=none, 1=full stop)
    float windResponse = 0.0f;      // How much global wind affects this particle (0-1)

    // Motion pattern
    MotionType motionType = MotionType::LINEAR;

    // Appearance
    glm::vec4 colorStart{1.0f, 1.0f, 0.5f, 1.0f};  // RGBA at birth
    glm::vec4 colorEnd{1.0f, 0.2f, 0.0f, 0.0f};    // RGBA at death
    float sizeStartMin = 1.5f;      // Min size at birth (randomized per particle)
    float sizeStartMax = 2.5f;      // Max size at birth
    float sizeEndMin = 3.0f;        // Min size at death
    float sizeEndMax = 4.0f;        // Max size at death
    float lifetimeMin = 0.3f;       // Min seconds
    float lifetimeMax = 0.6f;       // Max seconds

    // Rendering
    UnifiedRendererType rendererType = UnifiedRendererType::POINT_SPRITE;
    UnifiedBlendMode blendMode = UnifiedBlendMode::ADDITIVE;
    uint8_t textureRegions[4] = {0, 0, 0, 0};  // Possible atlas tile indices
    uint8_t textureRegionCount = 1;              // How many regions to pick from

    // Spell motion parameters (ORBITAL / RADIAL_EXPAND)
    float orbitalRadius = 1.0f;           // ORBITAL: orbit radius
    float orbitalAngularVelocity = 4.0f;  // ORBITAL: rad/s
    float expandSpeed = 3.0f;             // RADIAL_EXPAND: outward speed multiplier

    // Weather-specific (CAMERA_RELATIVE motion)
    int targetCount = 0;                  // 0 = use spawnRate; >0 = maintain count via recycling
    glm::vec3 spawnVolumeHalfExtents{0};  // Camera-relative spawn box half-extents
    float spawnVolumeTopBias = 0.8f;      // Fraction of spawns placed at top of volume
    float driftFrequency = 0.0f;          // Snow lateral sine-wave frequency
    float driftAmplitude = 0.0f;          // Snow lateral drift displacement
    float twinkleSpeed = 0.0f;            // Snow alpha modulation speed (0=disabled)
    float sizeSpeedCorrelation = 0.0f;    // Large particles fall slower (0-1)
};

// === Active Emitter ===

// Runtime state for a spawning emitter
struct ActiveEmitter {
    EmitterConfig config;
    glm::vec3 position{0.0f};       // World position (Irrlicht Y-up)
    float spawnAccumulator = 0.0f;   // Fractional particle accumulation
    float emitterAge = 0.0f;         // Time since activation (for lifetime check)
    bool active = true;
    uint16_t emitterID = 0;
    float lightRadius = 0.0f;        // Source light radius (for classification)
    float transitionAlpha = 1.0f;    // 0→1 ramp for smooth weather onset
    float transitionRate = 0.5f;     // Per-second ramp speed

    // Entity attachment (spell effects)
    uint16_t attachEntityID = 0;     // Entity to follow (0 = static)
    glm::vec3 attachOffset{0.0f};    // Offset from entity position (Irrlicht Y-up)
    bool isBurstSpawned = false;     // BURST: prevent re-spawning after initial burst

    // Dynamic velocity direction (spray effects — updated each frame via callback)
    bool useDynamicDirection = false;
    glm::vec3 dynamicDirection{0.0f, 0.0f, 1.0f};  // Normalized direction for spray
};

// === Fire Emitter Presets ===

namespace FirePresets {
    EmitterConfig Torch();
    EmitterConfig CampfireFlame();
    EmitterConfig CampfireEmber();
}

// === Weather Emitter Presets ===

namespace WeatherPresets {
    EmitterConfig Rain(uint8_t intensity = 5);
    EmitterConfig Snow(uint8_t intensity = 5);
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
