#pragma once

#include "client/graphics/environment/particle_emitter.h"

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * FireflyEmitter - Glowing particles at night near water/forests.
 *
 * Characteristics:
 * - Small glowing yellow-green particles
 * - Only active at night (20:00 - 6:00)
 * - Erratic, wandering movement pattern
 * - Pulsing glow animation
 * - Spawn near water and in forests
 * - Less wind-affected than other particles
 */
class FireflyEmitter : public ParticleEmitter {
public:
    FireflyEmitter();
    ~FireflyEmitter() override = default;

    /**
     * Check if this emitter should be active.
     * Fireflies only appear at night in appropriate biomes.
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

protected:
    /**
     * Initialize a newly spawned firefly.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a firefly's movement and glow.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on conditions.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    // Configuration
    static constexpr int MAX_PARTICLES = 30;
    static constexpr float BASE_SPAWN_RATE = 2.0f;
    static constexpr float SPAWN_RADIUS_MIN = 5.0f;
    static constexpr float SPAWN_RADIUS_MAX = 30.0f;
    static constexpr float SPAWN_HEIGHT_MIN = 0.5f;
    static constexpr float SPAWN_HEIGHT_MAX = 4.0f;
    static constexpr float PARTICLE_SIZE_MIN = 0.05f;
    static constexpr float PARTICLE_SIZE_MAX = 0.10f;
    static constexpr float LIFETIME_MIN = 15.0f;
    static constexpr float LIFETIME_MAX = 30.0f;
    static constexpr float WANDER_SPEED = 1.0f;
    static constexpr float GLOW_SPEED_MIN = 1.5f;
    static constexpr float GLOW_SPEED_MAX = 3.0f;

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    bool hasWater_ = false;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
