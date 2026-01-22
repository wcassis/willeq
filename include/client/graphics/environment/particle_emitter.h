#pragma once

#include "particle_types.h"
#include <vector>
#include <random>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * ParticleEmitter - Base class for particle emitters.
 *
 * Each emitter manages a pool of particles of a specific type.
 * Subclasses implement spawn logic, movement patterns, and visual styles.
 */
class ParticleEmitter {
public:
    ParticleEmitter(ParticleType type, int maxParticles);
    virtual ~ParticleEmitter() = default;

    // Non-copyable
    ParticleEmitter(const ParticleEmitter&) = delete;
    ParticleEmitter& operator=(const ParticleEmitter&) = delete;

    /**
     * Update all particles.
     * @param deltaTime Time since last update (seconds)
     * @param env Current environmental state
     */
    virtual void update(float deltaTime, const EnvironmentState& env);

    /**
     * Get all active particles for rendering.
     */
    const std::vector<Particle>& getParticles() const { return particles_; }

    /**
     * Get the particle type this emitter produces.
     */
    ParticleType getType() const { return type_; }

    /**
     * Get the number of currently active particles.
     */
    int getActiveCount() const { return activeCount_; }

    /**
     * Get the maximum particle capacity.
     */
    int getMaxParticles() const { return maxParticles_; }

    /**
     * Enable or disable this emitter.
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    /**
     * Set the density multiplier (0-1).
     * Affects spawn rate and maximum active particles.
     */
    void setDensityMultiplier(float mult) { densityMult_ = glm::clamp(mult, 0.0f, 1.0f); }
    float getDensityMultiplier() const { return densityMult_; }

    /**
     * Check if this emitter should be active given current conditions.
     * Override in subclasses for time-of-day or weather restrictions.
     */
    virtual bool shouldBeActive(const EnvironmentState& env) const { return enabled_; }

    /**
     * Called when entering a zone. Override to set up zone-specific behavior.
     */
    virtual void onZoneEnter(const std::string& zoneName, ZoneBiome biome) {}

    /**
     * Called when leaving a zone. Override to clean up.
     */
    virtual void onZoneLeave() { clearAllParticles(); }

    /**
     * Reload settings from config file.
     * Override in subclasses to update type-specific settings.
     */
    virtual void reloadSettings() {}

protected:
    /**
     * Spawn a new particle at the given position.
     * @return Pointer to the spawned particle, or nullptr if pool is full.
     */
    Particle* spawnParticle();

    /**
     * Kill a particle immediately.
     */
    void killParticle(Particle& p);

    /**
     * Clear all particles.
     */
    void clearAllParticles();

    /**
     * Update a single particle's physics.
     * Override for custom movement patterns.
     */
    virtual void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env);

    /**
     * Initialize a newly spawned particle.
     * Override to set type-specific properties.
     */
    virtual void initParticle(Particle& p, const EnvironmentState& env) = 0;

    /**
     * Calculate the spawn rate based on current conditions.
     * @return Particles to spawn per second
     */
    virtual float getSpawnRate(const EnvironmentState& env) const { return baseSpawnRate_; }

    /**
     * Get a random spawn position around the player.
     */
    glm::vec3 getRandomSpawnPosition(const EnvironmentState& env, float minRadius, float maxRadius, float minHeight, float maxHeight) const;

    /**
     * Get a random float in range [min, max].
     */
    float randomFloat(float min, float max) const;

    /**
     * Get a random unit vector for direction.
     */
    glm::vec3 randomDirection() const;

    /**
     * Apply wind to a particle's velocity.
     */
    void applyWind(Particle& p, const EnvironmentState& env, float windFactor);

    // Configuration
    ParticleType type_;
    int maxParticles_;
    float baseSpawnRate_ = 10.0f;   // Particles per second
    float spawnRadius_ = 30.0f;     // Spawn area radius around player

    // State
    bool enabled_ = true;
    float densityMult_ = 1.0f;
    int activeCount_ = 0;
    float spawnAccumulator_ = 0.0f; // Accumulates fractional spawns

    // Particle pool
    std::vector<Particle> particles_;

    // Random number generation
    mutable std::mt19937 rng_;
    mutable std::uniform_real_distribution<float> dist01_{0.0f, 1.0f};
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
