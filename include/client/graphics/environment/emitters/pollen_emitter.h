#pragma once

#include "client/graphics/environment/particle_emitter.h"

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * PollenEmitter - Larger floating particles in forests/plains.
 *
 * Characteristics:
 * - Larger, yellowish-green particles
 * - Only active during daytime (6:00 - 20:00)
 * - Gentle upward drift with wind influence
 * - Spawn near ground level (from plants)
 * - Common in Forest and Plains biomes
 */
class PollenEmitter : public ParticleEmitter {
public:
    PollenEmitter();
    ~PollenEmitter() override = default;

    /**
     * Check if this emitter should be active.
     * Pollen only appears during daytime in outdoor areas.
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

protected:
    /**
     * Initialize a newly spawned pollen particle.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a pollen particle's movement.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on conditions.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    // Configuration
    static constexpr int MAX_PARTICLES = 50;
    static constexpr float BASE_SPAWN_RATE = 5.0f;
    static constexpr float SPAWN_RADIUS_MIN = 3.0f;
    static constexpr float SPAWN_RADIUS_MAX = 20.0f;
    static constexpr float SPAWN_HEIGHT_MIN = 0.0f;     // Ground level
    static constexpr float SPAWN_HEIGHT_MAX = 3.0f;     // Low, near plants
    static constexpr float PARTICLE_SIZE_MIN = 0.06f;
    static constexpr float PARTICLE_SIZE_MAX = 0.12f;
    static constexpr float LIFETIME_MIN = 10.0f;
    static constexpr float LIFETIME_MAX = 20.0f;
    static constexpr float RISE_SPEED = 0.5f;           // Upward drift
    static constexpr float WIND_FACTOR = 3.0f;          // Wind sensitivity

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
