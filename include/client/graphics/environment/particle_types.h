#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * ParticleType - Types of environmental particles.
 */
enum class ParticleType : uint8_t {
    DustMote,       // Floating dust particles visible in light
    Pollen,         // Larger floating particles in forests/plains (day only)
    Firefly,        // Glowing particles at night near water/forests
    Mist,           // Low-lying fog particles in swamps, near water
    SandDust,       // Desert dust clouds
    Leaf,           // Falling/blowing leaves
    Snowflake,      // Snow particles
    Ember,          // Fire embers/sparks
    ShorelineWave,  // Foam and spray at water edges

    Count
};

/**
 * ZoneBiome - Biome classification for zones.
 * Determines which particle types are appropriate.
 */
enum class ZoneBiome : uint8_t {
    Unknown,
    Forest,         // Pollen (day), fireflies (night), leaves (wind)
    Swamp,          // Mist, marsh gas, fireflies, will-o-wisps
    Desert,         // Sand dust, especially when windy
    Snow,           // Ice crystals, snowflakes
    Plains,         // Pollen, dandelion seeds, fireflies (night)
    Dungeon,        // Dust motes, cobwebs
    Urban,          // Dust, leaves in autumn wind
    Ocean,          // Sea spray, salt mist
    Volcanic,       // Embers, ash, smoke
    Cave,           // Dust motes, dripping water particles

    Count
};

/**
 * WeatherType - Current weather affecting particle behavior.
 */
enum class WeatherType : uint8_t {
    Clear,
    Cloudy,
    Rain,
    Storm,
    Snow,
    Fog,

    Count
};

/**
 * EffectQuality - Quality levels for environmental effects.
 * Matches the UI options in OptionsWindow.
 */
enum class EffectQuality : uint8_t {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3
};

/**
 * Particle - A single particle instance.
 */
struct Particle {
    glm::vec3 position{0.0f};       // World position
    glm::vec3 velocity{0.0f};       // Current velocity
    float lifetime = 0.0f;          // Remaining lifetime (seconds)
    float maxLifetime = 1.0f;       // Initial lifetime (for alpha fade)
    float size = 1.0f;              // Billboard size
    float alpha = 1.0f;             // Current alpha (0-1)
    glm::vec4 color{1.0f};          // RGBA color
    uint8_t textureIndex = 0;       // Index into particle atlas
    float rotation = 0.0f;          // Billboard rotation (radians)
    float rotationSpeed = 0.0f;     // Rotation speed (radians/sec)

    // For fireflies and other pulsing effects
    float glowPhase = 0.0f;         // Current glow animation phase
    float glowSpeed = 1.0f;         // Glow animation speed

    // Check if particle is still alive
    bool isAlive() const { return lifetime > 0.0f; }

    // Get normalized lifetime (0 = dead, 1 = just spawned)
    float getNormalizedLifetime() const {
        return maxLifetime > 0.0f ? lifetime / maxLifetime : 0.0f;
    }
};

/**
 * ParticleAtlasTile - Indices into the particle texture atlas.
 */
namespace ParticleAtlas {
    constexpr uint8_t SoftCircle = 0;   // Dust motes
    constexpr uint8_t StarShape = 1;    // Fireflies
    constexpr uint8_t WispyCloud = 2;   // Mist
    constexpr uint8_t SporeShape = 3;   // Pollen
    constexpr uint8_t GrainShape = 4;   // Sand
    constexpr uint8_t LeafShape = 5;    // Leaves
    constexpr uint8_t Snowflake = 6;    // Snow
    constexpr uint8_t Ember = 7;        // Fire embers
    constexpr uint8_t FoamSpray = 8;    // Wave foam/spray
    constexpr uint8_t WaterDroplet = 9; // Small water droplet
    constexpr uint8_t RippleRing = 10;  // Water ripple ring (Phase 7)
    constexpr uint8_t SnowPatch = 11;   // Snow ground patch (Phase 9)

    constexpr uint8_t TileCount = 12;
    constexpr uint8_t AtlasColumns = 4; // 4x3 atlas
    constexpr uint8_t AtlasRows = 3;
}

/**
 * ParticleBudget - Particle count limits per quality level.
 */
struct ParticleBudget {
    int maxTotal;           // Maximum total particles
    float densityMult;      // Density multiplier (0-1)
    float updateDistance;   // Distance at which to update particles
    float cullDistance;     // Distance at which to cull particles

    static ParticleBudget fromQuality(EffectQuality quality) {
        switch (quality) {
            case EffectQuality::Off:
                return {0, 0.0f, 0.0f, 0.0f};
            case EffectQuality::Low:
                return {100, 0.25f, 30.0f, 40.0f};
            case EffectQuality::Medium:
                return {300, 0.50f, 40.0f, 50.0f};
            case EffectQuality::High:
            default:
                return {500, 1.0f, 50.0f, 60.0f};
        }
    }
};

/**
 * EnvironmentState - Current environmental conditions.
 * Passed to emitters to influence particle behavior.
 */
struct EnvironmentState {
    float timeOfDay = 12.0f;        // Hour (0-24)
    WeatherType weather = WeatherType::Clear;
    glm::vec3 windDirection{1.0f, 0.0f, 0.0f};
    float windStrength = 0.0f;      // 0 = calm, 1 = strong wind
    glm::vec3 playerPosition{0.0f};
    float playerHeading = 0.0f;     // Radians

    // Time-of-day helpers
    bool isDaytime() const { return timeOfDay >= 6.0f && timeOfDay < 20.0f; }
    bool isNighttime() const { return !isDaytime(); }
    bool isDawn() const { return timeOfDay >= 5.0f && timeOfDay < 7.0f; }
    bool isDusk() const { return timeOfDay >= 19.0f && timeOfDay < 21.0f; }

    // Weather helpers
    bool isRaining() const { return weather == WeatherType::Rain || weather == WeatherType::Storm; }
    bool isSnowing() const { return weather == WeatherType::Snow; }
    bool isFoggy() const { return weather == WeatherType::Fog; }
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
