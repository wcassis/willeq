#pragma once

#include "client/graphics/environment/particle_emitter.h"

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * RainSettings - Configuration for rain particle effects.
 */
struct RainSettings {
    bool enabled = true;
    int maxParticles = 500;
    float spawnRate = 300.0f;       // Particles per second at max intensity
    float spawnRadius = 50.0f;      // Cylinder radius around player
    float spawnHeight = 80.0f;      // Height above player to spawn
    float dropSpeed = 30.0f;        // Fall speed (faster = more streak)
    float dropSpeedVariance = 8.0f; // Random variation in fall speed
    float windInfluence = 0.3f;     // How much wind affects trajectory
    float sizeMin = 0.03f;          // Minimum drop width (small streaks)
    float sizeMax = 0.06f;          // Maximum drop width
    float lengthScale = 4.0f;       // Stretch factor for streak length
    // Color (light blue-white for rain streaks)
    float colorR = 0.75f;
    float colorG = 0.85f;
    float colorB = 1.0f;
    float colorA = 0.5f;
};

/**
 * RainEmitter - Rain drops falling from the sky.
 *
 * Characteristics:
 * - Drops spawn in a cylinder above the player
 * - Fall speed affected by wind
 * - Intensity (1-10) controls spawn rate
 * - Drops are elongated streaks for motion blur effect
 * - Disappear at ground level
 */
class RainEmitter : public ParticleEmitter {
public:
    RainEmitter();
    ~RainEmitter() override = default;

    /**
     * Set the rain intensity from server weather packet.
     * @param intensity 0-10 (0 = no rain, 10 = heavy rain)
     */
    void setIntensity(uint8_t intensity);
    uint8_t getIntensity() const { return intensity_; }

    /**
     * Check if rain should be active.
     * Only active when intensity > 0.
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

    /**
     * Update rain settings from configuration.
     */
    void setSettings(const RainSettings& settings);
    const RainSettings& getSettings() const { return settings_; }

protected:
    /**
     * Initialize a newly spawned rain drop.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a rain drop's movement.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on intensity.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    RainSettings settings_;
    uint8_t intensity_ = 0;         // 0-10 from server
    float groundLevel_ = 0.0f;      // Estimated ground level
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
