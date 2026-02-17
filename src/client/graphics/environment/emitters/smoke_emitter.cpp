#include "client/graphics/environment/emitters/smoke_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

SmokeEmitter::SmokeEmitter()
    : ParticleEmitter(ParticleType::Smoke, 20)
{
    reloadSettings();
}

void SmokeEmitter::reloadSettings() {
    settings_ = EnvironmentEffectsConfig::instance().getSmoke();

    maxParticles_ = settings_.maxParticles;
    baseSpawnRate_ = settings_.spawnRate;
    spawnRadius_ = settings_.spawnRadiusMax;
    enabled_ = settings_.enabled;

    if (static_cast<int>(particles_.size()) < settings_.maxParticles) {
        particles_.resize(settings_.maxParticles);
    }
}

bool SmokeEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_ || !settings_.enabled) return false;
    return !fireSources_.empty();
}

void SmokeEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    // Fire sources are set externally via setFireSources()
}

void SmokeEmitter::setFireSources(const std::vector<glm::vec3>& positions) {
    fireSources_ = positions;
}

void SmokeEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    if (fireSources_.empty()) return;

    // Pick a random fire source
    int idx = static_cast<int>(randomFloat(0.0f, static_cast<float>(fireSources_.size()) - 0.01f));
    const glm::vec3& source = fireSources_[idx];

    // Spawn slightly above the fire source
    p.position = source + glm::vec3(
        randomFloat(-0.5f, 0.5f),
        randomFloat(-0.5f, 0.5f),
        randomFloat(0.5f, 1.5f)
    );

    // Gentle upward + slight horizontal velocity
    p.velocity = glm::vec3(
        randomFloat(-0.2f, 0.2f),
        randomFloat(-0.2f, 0.2f),
        randomFloat(0.3f, 0.8f)
    );

    // Larger size
    p.size = randomFloat(settings_.sizeMin, settings_.sizeMax);

    // Longer lifetime
    p.lifetime = randomFloat(settings_.lifetimeMin, settings_.lifetimeMax);
    p.maxLifetime = p.lifetime;

    // Gray color, low alpha
    float gray = randomFloat(0.6f, 0.8f);
    p.color = glm::vec4(gray, gray, gray, 1.0f);

    p.textureIndex = ParticleAtlas::SmokeWisp;
    p.alpha = randomFloat(0.15f, 0.25f);

    // Slow rotation for visual variety
    p.rotation = randomFloat(0.0f, 6.28f);
    p.rotationSpeed = randomFloat(-0.3f, 0.3f);

    // Use glowPhase for swirl animation
    p.glowPhase = randomFloat(0.0f, 6.28f);
    p.glowSpeed = randomFloat(0.8f, 1.5f);
}

void SmokeEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Sinusoidal horizontal swirl
    p.glowPhase += p.glowSpeed * deltaTime;
    float swirlX = std::sin(p.glowPhase) * 0.3f * deltaTime;
    float swirlY = std::cos(p.glowPhase * 0.7f) * 0.3f * deltaTime;
    p.position.x += swirlX;
    p.position.y += swirlY;

    // Wind pushes horizontally
    applyWind(p, env, settings_.windFactor);

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Rotation
    p.rotation += p.rotationSpeed * deltaTime;

    // Grow in size as smoke dissipates
    float normalizedLife = p.getNormalizedLifetime();
    float growFactor = 1.0f + (1.0f - normalizedLife) * 1.5f;
    float baseSize = (settings_.sizeMin + settings_.sizeMax) * 0.5f;
    p.size = baseSize * growFactor;

    // Smooth alpha fade
    float baseAlpha = randomFloat(0.15f, 0.25f);
    if (normalizedLife > 0.8f) {
        // Fade in during first 20%
        p.alpha = baseAlpha * (1.0f - normalizedLife) / 0.2f;
    } else if (normalizedLife < 0.3f) {
        // Fade out during last 30%
        p.alpha = baseAlpha * normalizedLife / 0.3f;
    } else {
        p.alpha = baseAlpha;
    }
}

float SmokeEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = settings_.spawnRate;

    // Wind disperses smoke (lower rate)
    if (env.windStrength > 0.5f) {
        rate *= (1.0f - env.windStrength * 0.3f);
    }

    // Rain increases (steam effect)
    if (env.isRaining()) {
        rate *= 1.5f;
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
