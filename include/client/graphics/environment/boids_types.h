#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * CreatureType - Types of ambient creatures that use Boids flocking behavior.
 */
enum class CreatureType : uint8_t {
    Bird,       // Daytime, outdoor zones
    Bat,        // Night or dungeon zones
    Butterfly,  // Day, forests/plains
    Dragonfly,  // Near water
    Firefly,    // Night (flocking version, complements particle fireflies)
    Crow,       // Urban areas, graveyards
    Seagull,    // Coastal zones

    Count
};

/**
 * Get the display name for a creature type.
 */
inline const char* getCreatureTypeName(CreatureType type) {
    switch (type) {
        case CreatureType::Bird:      return "Bird";
        case CreatureType::Bat:       return "Bat";
        case CreatureType::Butterfly: return "Butterfly";
        case CreatureType::Dragonfly: return "Dragonfly";
        case CreatureType::Firefly:   return "Firefly";
        case CreatureType::Crow:      return "Crow";
        case CreatureType::Seagull:   return "Seagull";
        default:                      return "Unknown";
    }
}

/**
 * Creature - A single boid instance in a flock.
 */
struct Creature {
    glm::vec3 position{0.0f};       // World position
    glm::vec3 velocity{0.0f};       // Current velocity
    float speed = 10.0f;            // Base movement speed
    float size = 1.0f;              // Billboard size
    uint8_t textureIndex = 0;       // Index into creature atlas
    float animFrame = 0.0f;         // Animation frame (for wing flap)
    float animSpeed = 1.0f;         // Animation speed multiplier

    // Visual properties
    glm::vec4 color{1.0f};          // Tint color (RGBA)
    float alpha = 1.0f;             // Transparency

    // Get the current animation frame (0 or 1 for two-frame animations)
    int getCurrentFrame() const { return static_cast<int>(animFrame) % 2; }
};

/**
 * FlockBehavior - Weights for Boids steering behaviors.
 */
struct FlockBehavior {
    float separation = 1.5f;        // Avoid crowding neighbors
    float alignment = 1.0f;         // Steer toward average heading
    float cohesion = 1.0f;          // Steer toward average position
    float destination = 0.3f;       // Gentle pull toward waypoint
    float avoidance = 2.0f;         // Strong push from boundaries
    float playerAvoidance = 1.0f;   // Scatter when player approaches
};

/**
 * FlockConfig - Configuration for a flock of creatures.
 */
struct FlockConfig {
    CreatureType type = CreatureType::Bird;
    int minSize = 5;                // Minimum creatures per flock
    int maxSize = 12;               // Maximum creatures per flock
    float minSpeed = 8.0f;          // Minimum movement speed
    float maxSpeed = 15.0f;         // Maximum movement speed
    float neighborRadius = 10.0f;   // Radius for flock behavior calculations
    float separationRadius = 3.0f;  // Minimum distance between creatures
    float patrolRadius = 50.0f;     // Radius of patrol area
    float heightMin = 10.0f;        // Minimum flight height above terrain
    float heightMax = 40.0f;        // Maximum flight height
    FlockBehavior behavior;         // Behavior weights

    // Animation
    float animSpeedMin = 4.0f;      // Min wing flaps per second
    float animSpeedMax = 8.0f;      // Max wing flaps per second
};

/**
 * BoidsBudget - Creature count limits per quality level.
 */
struct BoidsBudget {
    int maxFlocks;          // Maximum active flocks
    int maxCreatures;       // Maximum total creatures
    float densityMult;      // Density multiplier (0-1)
    float viewDistance;     // Distance at which creatures are visible
    float cullDistance;     // Distance at which to cull creatures

    static BoidsBudget fromQuality(int quality) {
        // 0 = Off, 1 = Low, 2 = Medium, 3 = High
        switch (quality) {
            case 0:  // Off
                return {0, 0, 0.0f, 0.0f, 0.0f};
            case 1:  // Low
                return {2, 30, 0.5f, 100.0f, 120.0f};
            case 2:  // Medium
                return {3, 50, 0.75f, 130.0f, 150.0f};
            case 3:  // High
            default:
                return {4, 80, 1.0f, 150.0f, 180.0f};
        }
    }
};

/**
 * CreatureAtlas - Indices into the creature texture atlas.
 * Each creature type has 2 frames for wing animation.
 */
namespace CreatureAtlas {
    constexpr uint8_t BirdWingsUp = 0;
    constexpr uint8_t BirdWingsDown = 1;
    constexpr uint8_t BatWingsUp = 2;
    constexpr uint8_t BatWingsDown = 3;
    constexpr uint8_t ButterflyWingsUp = 4;
    constexpr uint8_t ButterflyWingsDown = 5;
    constexpr uint8_t DragonflyWingsUp = 6;
    constexpr uint8_t DragonflyWingsDown = 7;
    constexpr uint8_t CrowWingsUp = 8;
    constexpr uint8_t CrowWingsDown = 9;
    constexpr uint8_t SeagullWingsUp = 10;
    constexpr uint8_t SeagullWingsDown = 11;
    constexpr uint8_t FireflyGlow1 = 12;
    constexpr uint8_t FireflyGlow2 = 13;

    constexpr uint8_t TileCount = 14;
    constexpr uint8_t AtlasColumns = 4;  // 4x4 atlas
    constexpr uint8_t AtlasRows = 4;

    // Get base texture index for a creature type
    inline uint8_t getBaseIndex(CreatureType type) {
        switch (type) {
            case CreatureType::Bird:      return BirdWingsUp;
            case CreatureType::Bat:       return BatWingsUp;
            case CreatureType::Butterfly: return ButterflyWingsUp;
            case CreatureType::Dragonfly: return DragonflyWingsUp;
            case CreatureType::Crow:      return CrowWingsUp;
            case CreatureType::Seagull:   return SeagullWingsUp;
            case CreatureType::Firefly:   return FireflyGlow1;
            default:                      return BirdWingsUp;
        }
    }
}

/**
 * Get default FlockConfig for a creature type.
 */
inline FlockConfig getDefaultFlockConfig(CreatureType type) {
    FlockConfig config;
    config.type = type;

    switch (type) {
        case CreatureType::Bird:
            config.minSpeed = 10.0f;
            config.maxSpeed = 18.0f;
            config.heightMin = 15.0f;
            config.heightMax = 50.0f;
            config.animSpeedMin = 4.0f;
            config.animSpeedMax = 6.0f;
            break;

        case CreatureType::Bat:
            config.minSpeed = 8.0f;
            config.maxSpeed = 15.0f;
            config.heightMin = 5.0f;
            config.heightMax = 30.0f;
            config.behavior.alignment = 0.8f;  // Bats are less aligned
            config.animSpeedMin = 8.0f;
            config.animSpeedMax = 12.0f;
            break;

        case CreatureType::Butterfly:
            config.minSpeed = 3.0f;
            config.maxSpeed = 6.0f;
            config.minSize = 3;
            config.maxSize = 8;
            config.heightMin = 1.0f;
            config.heightMax = 10.0f;
            config.neighborRadius = 8.0f;
            config.behavior.cohesion = 0.5f;  // More independent
            config.animSpeedMin = 6.0f;
            config.animSpeedMax = 10.0f;
            break;

        case CreatureType::Dragonfly:
            config.minSpeed = 6.0f;
            config.maxSpeed = 12.0f;
            config.minSize = 2;
            config.maxSize = 6;
            config.heightMin = 0.5f;
            config.heightMax = 5.0f;
            config.behavior.alignment = 0.6f;
            config.animSpeedMin = 15.0f;  // Fast wings
            config.animSpeedMax = 20.0f;
            break;

        case CreatureType::Firefly:
            config.minSpeed = 1.0f;
            config.maxSpeed = 3.0f;
            config.minSize = 5;
            config.maxSize = 15;
            config.heightMin = 1.0f;
            config.heightMax = 8.0f;
            config.neighborRadius = 5.0f;
            config.behavior.cohesion = 0.3f;
            config.behavior.alignment = 0.3f;
            config.animSpeedMin = 0.5f;  // Slow glow pulse
            config.animSpeedMax = 1.5f;
            break;

        case CreatureType::Crow:
            config.minSpeed = 8.0f;
            config.maxSpeed = 14.0f;
            config.minSize = 3;
            config.maxSize = 8;
            config.heightMin = 10.0f;
            config.heightMax = 35.0f;
            config.animSpeedMin = 3.0f;
            config.animSpeedMax = 5.0f;
            break;

        case CreatureType::Seagull:
            config.minSpeed = 10.0f;
            config.maxSpeed = 16.0f;
            config.minSize = 4;
            config.maxSize = 10;
            config.heightMin = 8.0f;
            config.heightMax = 40.0f;
            config.animSpeedMin = 2.0f;  // Slower gliding
            config.animSpeedMax = 4.0f;
            break;

        default:
            break;
    }

    return config;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
