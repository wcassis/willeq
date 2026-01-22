#pragma once

#include "client/graphics/environment/particle_emitter.h"

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * MistEmitter - Low-lying fog/mist particles near water and in swamps.
 *
 * Characteristics:
 * - Large, translucent particles
 * - Slow horizontal drift
 * - Stays close to ground/water level
 * - Most visible at dawn/dusk and night
 * - Common in Swamp, Coast, and near water
 */
class MistEmitter : public ParticleEmitter {
public:
    MistEmitter();
    ~MistEmitter() override = default;

    /**
     * Check if this emitter should be active.
     * Mist appears near water, in swamps, and during certain times.
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

protected:
    /**
     * Initialize a newly spawned mist particle.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a mist particle's movement.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on conditions.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    // Configuration
    static constexpr int MAX_PARTICLES = 40;
    static constexpr float BASE_SPAWN_RATE = 3.0f;
    static constexpr float SPAWN_RADIUS_MIN = 10.0f;
    static constexpr float SPAWN_RADIUS_MAX = 40.0f;
    static constexpr float SPAWN_HEIGHT_MIN = -1.0f;    // Below player (water level)
    static constexpr float SPAWN_HEIGHT_MAX = 2.0f;     // Low to ground
    static constexpr float PARTICLE_SIZE_MIN = 2.0f;    // Large particles
    static constexpr float PARTICLE_SIZE_MAX = 5.0f;
    static constexpr float LIFETIME_MIN = 15.0f;
    static constexpr float LIFETIME_MAX = 30.0f;
    static constexpr float DRIFT_SPEED = 0.5f;          // Slow drift
    static constexpr float WIND_FACTOR = 1.0f;          // Moderate wind response

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    bool hasWater_ = false;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
