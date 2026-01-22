#pragma once

#include "boids_types.h"
#include "particle_types.h"  // For EnvironmentState, ZoneBiome
#include <glm/glm.hpp>
#include <vector>
#include <random>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * FlockController - Manages a single flock of creatures using Boids algorithm.
 *
 * Implements the classic Boids behaviors:
 *   - Separation: Avoid crowding neighbors
 *   - Alignment: Steer toward average heading of neighbors
 *   - Cohesion: Steer toward average position of neighbors
 *   - Destination: Gentle pull toward patrol waypoint
 *   - Avoidance: Avoid boundaries and player
 */
class FlockController {
public:
    /**
     * Constructor.
     * @param config Flock configuration
     * @param spawnPosition Initial spawn center
     * @param rng Reference to random number generator
     */
    FlockController(const FlockConfig& config, const glm::vec3& spawnPosition, std::mt19937& rng);

    // Non-copyable
    FlockController(const FlockController&) = delete;
    FlockController& operator=(const FlockController&) = delete;

    // Move constructors
    FlockController(FlockController&&) = default;
    FlockController& operator=(FlockController&&) = default;

    /**
     * Update all creatures in the flock.
     * @param deltaTime Time since last update (seconds)
     * @param env Current environment state
     */
    void update(float deltaTime, const EnvironmentState& env);

    /**
     * Get all creatures in this flock.
     */
    const std::vector<Creature>& getCreatures() const { return creatures_; }

    /**
     * Get the flock configuration.
     */
    const FlockConfig& getConfig() const { return config_; }

    /**
     * Get the creature type for this flock.
     */
    CreatureType getType() const { return config_.type; }

    /**
     * Get the center of the flock.
     */
    glm::vec3 getCenter() const { return center_; }

    /**
     * Get the current destination waypoint.
     */
    glm::vec3 getDestination() const { return destination_; }

    /**
     * Check if the flock has exited the zone bounds.
     * Used to determine when to despawn.
     */
    bool hasExitedBounds() const { return exitedBounds_; }

    /**
     * Get time alive (for lifecycle management).
     */
    float getTimeAlive() const { return timeAlive_; }

    /**
     * Set zone bounds for boundary avoidance.
     * @param min Minimum corner of zone bounds
     * @param max Maximum corner of zone bounds
     */
    void setZoneBounds(const glm::vec3& min, const glm::vec3& max);

    /**
     * Set a new destination waypoint.
     */
    void setDestination(const glm::vec3& destination);

    /**
     * Scatter the flock (e.g., when player gets close).
     * @param sourcePosition Position to scatter away from
     * @param strength How strongly to scatter (0-1)
     */
    void scatter(const glm::vec3& sourcePosition, float strength);

    /**
     * Get debug info string.
     */
    std::string getDebugInfo() const;

private:
    // Boids steering behaviors
    glm::vec3 calculateSeparation(const Creature& creature) const;
    glm::vec3 calculateAlignment(const Creature& creature) const;
    glm::vec3 calculateCohesion(const Creature& creature) const;
    glm::vec3 calculateDestinationSteer(const Creature& creature) const;
    glm::vec3 calculateBoundsAvoidance(const Creature& creature) const;
    glm::vec3 calculatePlayerAvoidance(const Creature& creature, const glm::vec3& playerPos) const;

    // Helper methods
    void initializeCreatures(const glm::vec3& spawnPosition);
    void updateCenter();
    void updateDestination(float deltaTime, float playerZ);
    void clampVelocity(Creature& creature);
    float randomFloat(float min, float max);

    // Configuration
    FlockConfig config_;

    // Creatures
    std::vector<Creature> creatures_;
    glm::vec3 center_{0.0f};           // Average position of flock

    // Navigation
    glm::vec3 anchor_{0.0f};           // Anchor point to patrol around (spawn position)
    glm::vec3 destination_{0.0f};      // Current patrol waypoint
    float destinationTimer_ = 0.0f;    // Time until new waypoint
    float destinationInterval_ = 15.0f; // Seconds between waypoints

    // Zone bounds
    glm::vec3 boundsMin_{-1000.0f};
    glm::vec3 boundsMax_{1000.0f};
    bool hasBounds_ = false;

    // Lifecycle
    float timeAlive_ = 0.0f;
    bool exitedBounds_ = false;
    bool scattering_ = false;
    glm::vec3 scatterSource_{0.0f};
    float scatterStrength_ = 0.0f;
    float scatterTimer_ = 0.0f;

    // RNG
    std::mt19937& rng_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
