#pragma once

#include "client/graphics/environment/particle_emitter.h"

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * SandDustEmitter - Windblown sand particles for desert environments.
 *
 * Characteristics:
 * - Small, fast-moving particles
 * - Strongly wind-driven (horizontal streaks)
 * - Sandy brown/tan colors
 * - Only in Desert biome
 * - Intensity scales with wind strength
 */
class SandDustEmitter : public ParticleEmitter {
public:
    SandDustEmitter();
    ~SandDustEmitter() override = default;

    /**
     * Check if this emitter should be active.
     * Sand particles only appear in desert areas with some wind.
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

protected:
    /**
     * Initialize a newly spawned sand particle.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a sand particle's movement.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on conditions.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    // Configuration
    static constexpr int MAX_PARTICLES = 100;
    static constexpr float BASE_SPAWN_RATE = 10.0f;
    static constexpr float SPAWN_RADIUS_MIN = 5.0f;
    static constexpr float SPAWN_RADIUS_MAX = 25.0f;
    static constexpr float SPAWN_HEIGHT_MIN = 0.0f;     // Ground level
    static constexpr float SPAWN_HEIGHT_MAX = 3.0f;     // Low to ground
    static constexpr float PARTICLE_SIZE_MIN = 0.02f;   // Small particles
    static constexpr float PARTICLE_SIZE_MAX = 0.06f;
    static constexpr float LIFETIME_MIN = 3.0f;         // Short-lived
    static constexpr float LIFETIME_MAX = 6.0f;
    static constexpr float BASE_SPEED = 2.0f;           // Fast movement
    static constexpr float WIND_FACTOR = 5.0f;          // Very wind-sensitive

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
