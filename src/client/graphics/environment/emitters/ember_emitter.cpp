#include "client/graphics/environment/emitters/ember_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

EmberEmitter::EmberEmitter()
    : ParticleEmitter(ParticleType::Ember, 40)
{
    reloadSettings();
}

void EmberEmitter::reloadSettings() {
    settings_ = EnvironmentEffectsConfig::instance().getEmbers();

    maxParticles_ = settings_.maxParticles;
    baseSpawnRate_ = settings_.spawnRate;
    spawnRadius_ = settings_.spawnRadiusMax;
    enabled_ = settings_.enabled;

    if (static_cast<int>(particles_.size()) < settings_.maxParticles) {
        particles_.resize(settings_.maxParticles);
    }
}

bool EmberEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_ || !settings_.enabled) return false;
    return !fireSources_.empty();
}

void EmberEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    // Fire sources are set externally via setFireSources()
}

void EmberEmitter::setFireSources(const std::vector<glm::vec3>& positions) {
    fireSources_ = positions;
}

void EmberEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    if (fireSources_.empty()) return;

    // Pick a random fire source
    int idx = static_cast<int>(randomFloat(0.0f, static_cast<float>(fireSources_.size()) - 0.01f));
    const glm::vec3& source = fireSources_[idx];

    // Spawn near the fire source with small offset
    p.position = source + glm::vec3(
        randomFloat(-1.0f, 1.0f),
        randomFloat(-1.0f, 1.0f),
        randomFloat(0.0f, 0.5f)
    );

    // Mostly upward velocity with slight horizontal drift
    p.velocity = glm::vec3(
        randomFloat(-0.3f, 0.3f),
        randomFloat(-0.3f, 0.3f),
        randomFloat(1.0f, 2.5f)
    );

    // Size
    p.size = randomFloat(settings_.sizeMin, settings_.sizeMax);

    // Lifetime
    p.lifetime = randomFloat(settings_.lifetimeMin, settings_.lifetimeMax);
    p.maxLifetime = p.lifetime;

    // Orange-yellow color with variation
    p.color = glm::vec4(
        randomFloat(0.9f, 1.0f),   // R: bright
        randomFloat(0.4f, 0.7f),   // G: orange variation
        randomFloat(0.0f, 0.15f),  // B: very little blue
        1.0f
    );

    p.textureIndex = ParticleAtlas::Ember;
    p.alpha = 1.0f;
    p.rotation = 0.0f;
    p.rotationSpeed = 0.0f;
}

void EmberEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Slight random horizontal drift
    p.velocity.x += randomFloat(-0.5f, 0.5f) * deltaTime;
    p.velocity.y += randomFloat(-0.5f, 0.5f) * deltaTime;

    // Upward buoyancy
    p.velocity.z += 0.5f * deltaTime;

    // Apply wind
    applyWind(p, env, settings_.windFactor * 0.3f);

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Shrink over lifetime
    float normalizedLife = p.getNormalizedLifetime();
    float baseSize = (settings_.sizeMin + settings_.sizeMax) * 0.5f;
    p.size = baseSize * normalizedLife;

    // Fade out over last 30%
    if (normalizedLife < 0.3f) {
        p.alpha = normalizedLife / 0.3f;
    } else {
        p.alpha = 1.0f;
    }
}

float EmberEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = settings_.spawnRate;

    // Wind increases sparks slightly
    if (env.windStrength > 0.3f) {
        rate *= 1.0f + env.windStrength * 0.3f;
    }

    // Rain reduces heavily
    if (env.isRaining()) {
        rate *= 0.2f;
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
