#pragma once

#include "client/graphics/environment/particle_emitter.h"
#include "client/graphics/environment/environment_config.h"
#include <vector>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * SmokeEmitter - Larger semi-transparent gray particles rising from fire sources.
 *
 * Characteristics:
 * - Spawns at fire source positions (torches, campfires, braziers)
 * - Larger, slower particles than embers
 * - Drift upward with sinusoidal swirl
 * - Grow in size as smoke dissipates
 * - Low alpha (semi-transparent gray)
 * - Wind pushes horizontally, rain increases (steam effect)
 */
class SmokeEmitter : public ParticleEmitter {
public:
    SmokeEmitter();
    ~SmokeEmitter() override = default;

    bool shouldBeActive(const EnvironmentState& env) const override;
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;
    void reloadSettings() override;

    /**
     * Set fire source positions for spawning smoke.
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
