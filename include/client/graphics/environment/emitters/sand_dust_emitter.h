#pragma once

#include "client/graphics/environment/particle_emitter.h"
#include "client/graphics/environment/environment_config.h"

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
 *
 * Settings are loaded from config/environment_effects.json and can be
 * reloaded at runtime with /reloadeffects.
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

    /**
     * Reload settings from config file.
     */
    void reloadSettings();

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
    // Cached settings from config (reloaded on reloadSettings())
    EnvironmentEffectsConfig::EmitterSettings settings_;

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
