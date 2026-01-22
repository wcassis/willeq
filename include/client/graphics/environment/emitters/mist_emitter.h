#pragma once

#include "client/graphics/environment/particle_emitter.h"
#include "client/graphics/environment/environment_config.h"

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
 *
 * Settings are loaded from config/environment_effects.json and can be
 * reloaded at runtime with /reloadeffects.
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

    /**
     * Reload settings from config file.
     */
    void reloadSettings();

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
    // Cached settings from config (reloaded on reloadSettings())
    EnvironmentEffectsConfig::EmitterSettings settings_;

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    bool hasWater_ = false;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
