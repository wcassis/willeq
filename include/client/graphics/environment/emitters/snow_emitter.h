#pragma once

#include "client/graphics/environment/particle_emitter.h"

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * SnowSettings - Configuration for snow particle effects.
 */
struct SnowSettings {
    bool enabled = true;
    int maxParticles = 600;
    float spawnRate = 150.0f;        // Particles per second at max intensity
    float spawnRadius = 60.0f;       // Cylinder radius around player
    float spawnHeight = 80.0f;       // Height above player to spawn
    float fallSpeed = 3.0f;          // Base fall speed (slow)
    float fallSpeedVariance = 1.5f;  // Random variation
    float swayAmplitude = 2.0f;      // Side-to-side sway amplitude
    float swayFrequency = 0.5f;      // How fast it sways
    float windInfluence = 0.4f;      // How much wind affects trajectory
    float sizeMin = 0.1f;            // Minimum flake size
    float sizeMax = 0.25f;           // Maximum flake size
    // Color (white for snow)
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
    float colorA = 0.8f;
};

/**
 * SnowEmitter - Snowflakes falling from the sky.
 *
 * Characteristics:
 * - Flakes spawn in a cylinder above the player
 * - Slow fall speed with gentle swaying motion
 * - Heavily affected by wind
 * - Intensity (1-10) controls spawn rate
 * - Uses snowflake texture from atlas
 */
class SnowEmitter : public ParticleEmitter {
public:
    SnowEmitter();
    ~SnowEmitter() override = default;

    /**
     * Set the snow intensity from server weather packet.
     * @param intensity 0-10 (0 = no snow, 10 = blizzard)
     */
    void setIntensity(uint8_t intensity);
    uint8_t getIntensity() const { return intensity_; }

    /**
     * Check if snow should be active.
     * Only active when intensity > 0.
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

    /**
     * Update snow settings from configuration.
     */
    void setSettings(const SnowSettings& settings);
    const SnowSettings& getSettings() const { return settings_; }

protected:
    /**
     * Initialize a newly spawned snowflake.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a snowflake's movement.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on intensity.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    SnowSettings settings_;
    uint8_t intensity_ = 0;         // 0-10 from server
    float groundLevel_ = 0.0f;      // Estimated ground level
    float swayTime_ = 0.0f;         // Global sway timer
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
