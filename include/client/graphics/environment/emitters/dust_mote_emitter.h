#pragma once

#include "client/graphics/environment/particle_emitter.h"
#include "client/graphics/environment/environment_config.h"

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
 *
 * Settings are loaded from config/environment_effects.json and can be
 * reloaded at runtime with /reloadeffects.
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

    /**
     * Reload settings from config file.
     */
    void reloadSettings();

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
    // Cached settings from config (reloaded on reloadSettings())
    EnvironmentEffectsConfig::EmitterSettings settings_;

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    bool isIndoor_ = false;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
