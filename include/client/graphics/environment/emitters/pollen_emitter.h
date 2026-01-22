#pragma once

#include "client/graphics/environment/particle_emitter.h"
#include "client/graphics/environment/environment_config.h"

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
 *
 * Settings are loaded from config/environment_effects.json and can be
 * reloaded at runtime with /reloadeffects.
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

    /**
     * Reload settings from config file.
     */
    void reloadSettings();

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
    // Cached settings from config (reloaded on reloadSettings())
    EnvironmentEffectsConfig::EmitterSettings settings_;

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
