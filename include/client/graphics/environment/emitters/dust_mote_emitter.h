#pragma once

#include "client/graphics/environment/particle_emitter.h"

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * DustMoteEmitter - Floating dust particles visible in light.
 *
 * Characteristics:
 * - Small, soft particles that drift slowly
 * - Spawn in a cylinder around the player
 * - More visible near light sources
 * - Affected by wind (gentle drift)
 * - Active in all zones, especially dungeons/interiors
 * - Density increases when windy (dust gets stirred up)
 */
class DustMoteEmitter : public ParticleEmitter {
public:
    DustMoteEmitter();
    ~DustMoteEmitter() override = default;

    /**
     * Check if this emitter should be active.
     * Dust motes are always active (in appropriate biomes).
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

protected:
    /**
     * Initialize a newly spawned dust mote.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a dust mote's movement.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on conditions.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    // Configuration
    static constexpr int MAX_PARTICLES = 80;
    static constexpr float BASE_SPAWN_RATE = 8.0f;      // Particles per second
    static constexpr float SPAWN_RADIUS_MIN = 5.0f;     // Min spawn distance from player
    static constexpr float SPAWN_RADIUS_MAX = 25.0f;    // Max spawn distance from player
    static constexpr float SPAWN_HEIGHT_MIN = -2.0f;    // Min height relative to player
    static constexpr float SPAWN_HEIGHT_MAX = 8.0f;     // Max height relative to player
    static constexpr float PARTICLE_SIZE_MIN = 0.03f;
    static constexpr float PARTICLE_SIZE_MAX = 0.08f;
    static constexpr float LIFETIME_MIN = 8.0f;
    static constexpr float LIFETIME_MAX = 15.0f;
    static constexpr float DRIFT_SPEED = 0.3f;          // Base drift speed
    static constexpr float WIND_FACTOR = 2.0f;          // How much wind affects movement

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    bool isIndoor_ = false;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
