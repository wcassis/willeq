#pragma once

#include "client/graphics/environment/particle_emitter.h"
#include "client/graphics/environment/environment_config.h"

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
 *
 * Settings are loaded from config/environment_effects.json and can be
 * reloaded at runtime with /reloadeffects.
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

    /**
     * Reload settings from config file.
     */
    void reloadSettings();

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
    // Cached settings from config (reloaded on reloadSettings())
    EnvironmentEffectsConfig::EmitterSettings settings_;

    // Glow animation speeds (derived from config)
    float glowSpeedMin_ = 1.5f;
    float glowSpeedMax_ = 3.0f;

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    bool hasWater_ = false;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
