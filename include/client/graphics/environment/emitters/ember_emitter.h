#pragma once

#include "client/graphics/environment/particle_emitter.h"
#include "client/graphics/environment/environment_config.h"
#include <vector>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * EmberEmitter - Small bright orange particles drifting upward from fire sources.
 *
 * Characteristics:
 * - Spawns at fire source positions (torches, campfires, braziers)
 * - Small bright orange-yellow particles
 * - Drift upward with slight random horizontal movement
 * - Shrink and fade over lifetime
 * - Wind increases sparks slightly, rain reduces heavily
 */
class EmberEmitter : public ParticleEmitter {
public:
    EmberEmitter();
    ~EmberEmitter() override = default;

    bool shouldBeActive(const EnvironmentState& env) const override;
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;
    void reloadSettings() override;

    /**
     * Set fire source positions for spawning embers.
     * Positions are in EQ coordinates (Z-up).
     */
    void setFireSources(const std::vector<glm::vec3>& positions);

protected:
    void initParticle(Particle& p, const EnvironmentState& env) override;
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    EnvironmentEffectsConfig::EmitterSettings settings_;
    std::vector<glm::vec3> fireSources_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
