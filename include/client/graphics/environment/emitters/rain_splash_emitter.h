#pragma once

#include "client/graphics/environment/particle_emitter.h"

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * RainSplashSettings - Configuration for rain splash particle effects.
 */
struct RainSplashSettings {
    bool enabled = true;
    int maxParticles = 200;
    float spawnRate = 100.0f;       // Particles per second at max intensity
    float spawnRadius = 30.0f;      // Radius around player to spawn splashes
    float splashSpeed = 2.0f;       // Initial outward velocity
    float splashSpeedVariance = 1.0f;
    float gravity = 15.0f;          // Gravity pull on splashes
    float sizeMin = 0.05f;          // Minimum splash size
    float sizeMax = 0.15f;          // Maximum splash size
    float lifetime = 0.3f;          // Splash duration
    // Color (white-blue for water splash)
    float colorR = 0.8f;
    float colorG = 0.9f;
    float colorB = 1.0f;
    float colorA = 0.5f;
};

/**
 * RainSplashEmitter - Creates splash effects when rain hits ground.
 *
 * Characteristics:
 * - Splashes spawn at ground level around player
 * - Small particles burst outward and fade quickly
 * - Intensity (1-10) controls spawn rate
 * - Only active when it's raining
 */
class RainSplashEmitter : public ParticleEmitter {
public:
    RainSplashEmitter();
    ~RainSplashEmitter() override = default;

    /**
     * Set the rain intensity from server weather packet.
     * @param intensity 0-10 (0 = no rain, 10 = heavy rain)
     */
    void setIntensity(uint8_t intensity);
    uint8_t getIntensity() const { return intensity_; }

    /**
     * Check if splashes should be active.
     * Only active when intensity > 0.
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

    /**
     * Update splash settings from configuration.
     */
    void setSettings(const RainSplashSettings& settings);
    const RainSplashSettings& getSettings() const { return settings_; }

protected:
    /**
     * Initialize a newly spawned splash particle.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a splash particle's movement.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on intensity.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    RainSplashSettings settings_;
    uint8_t intensity_ = 0;         // 0-10 from server
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
