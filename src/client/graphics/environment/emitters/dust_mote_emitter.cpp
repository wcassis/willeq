#include "client/graphics/environment/emitters/dust_mote_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

DustMoteEmitter::DustMoteEmitter()
    : ParticleEmitter(ParticleType::DustMote, 100)  // Initial max, will be updated
{
    reloadSettings();
}

void DustMoteEmitter::reloadSettings() {
    settings_ = EnvironmentEffectsConfig::instance().getDustMotes();

    // Update base class values
    maxParticles_ = settings_.maxParticles;
    baseSpawnRate_ = settings_.spawnRate;
    spawnRadius_ = settings_.spawnRadiusMax;
    enabled_ = settings_.enabled;

    // Resize particle pool if needed
    if (static_cast<int>(particles_.size()) < settings_.maxParticles) {
        particles_.resize(settings_.maxParticles);
    }
}

bool DustMoteEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_ || !settings_.enabled) return false;

    // Dust motes are most visible in dungeons and caves
    // but can appear anywhere
    return true;
}

void DustMoteEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
    isIndoor_ = (biome == ZoneBiome::Dungeon || biome == ZoneBiome::Cave);
}

void DustMoteEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Random spawn position around player
    p.position = getRandomSpawnPosition(env,
        settings_.spawnRadiusMin, settings_.spawnRadiusMax,
        settings_.spawnHeightMin, settings_.spawnHeightMax);

    // Random size (smaller particles are more common)
    float sizeFactor = randomFloat(0.0f, 1.0f);
    sizeFactor = sizeFactor * sizeFactor; // Bias toward smaller
    p.size = settings_.sizeMin + sizeFactor * (settings_.sizeMax - settings_.sizeMin);

    // Random lifetime
    p.lifetime = randomFloat(settings_.lifetimeMin, settings_.lifetimeMax);
    p.maxLifetime = p.lifetime;

    // Initial velocity - very slow random drift
    glm::vec3 drift = randomDirection();
    drift.z *= 0.3f; // Reduce vertical component
    p.velocity = drift * settings_.driftSpeed * randomFloat(0.5f, 1.5f);

    // Apply initial wind influence
    if (env.windStrength > 0.1f) {
        p.velocity += env.windDirection * env.windStrength * settings_.windFactor * 0.5f;
    }

    // Color from config with slight brightness variation
    float brightness = randomFloat(0.8f, 1.0f);
    p.color = glm::vec4(
        settings_.colorR * brightness,
        settings_.colorG * brightness,
        settings_.colorB * brightness,
        settings_.colorA);

    // Texture: soft circle
    p.textureIndex = ParticleAtlas::SoftCircle;

    // Slow rotation
    p.rotation = randomFloat(0.0f, 6.28f);
    p.rotationSpeed = randomFloat(-0.2f, 0.2f);

    // Start with fade-in
    p.alpha = 0.0f;
}

void DustMoteEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Apply wind continuously (dust is very light)
    if (env.windStrength > 0.05f) {
        glm::vec3 windForce = env.windDirection * env.windStrength * settings_.windFactor * deltaTime;
        p.velocity += windForce;
    }

    // Add subtle random drift (Brownian motion effect)
    glm::vec3 randomDrift = randomDirection() * 0.5f * deltaTime;
    p.velocity += randomDrift;

    // Gentle upward thermal drift (warm air rises)
    p.velocity.z += 0.1f * deltaTime;

    // Dampen velocity to prevent runaway speed
    float speed = glm::length(p.velocity);
    float maxSpeed = settings_.driftSpeed * 3.0f + env.windStrength * settings_.windFactor;
    if (speed > maxSpeed) {
        p.velocity = p.velocity * (maxSpeed / speed);
    }

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Update rotation
    p.rotation += p.rotationSpeed * deltaTime;

    // Calculate alpha based on lifetime with slow fade
    float normalizedLife = p.getNormalizedLifetime();
    if (normalizedLife > 0.9f) {
        // Fade in over first 10%
        p.alpha = (1.0f - normalizedLife) * 10.0f;
    } else if (normalizedLife < 0.15f) {
        // Fade out over last 15%
        p.alpha = normalizedLife / 0.15f;
    } else {
        p.alpha = 1.0f;
    }

    // Apply indoor/outdoor alpha from config
    if (isIndoor_) {
        p.alpha *= settings_.alphaIndoor;
    } else {
        p.alpha *= settings_.alphaOutdoor;
    }
}

float DustMoteEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = settings_.spawnRate;

    // More dust in indoor areas
    if (isIndoor_) {
        rate *= 1.5f;
    }

    // Wind stirs up more dust
    if (env.windStrength > 0.2f) {
        rate *= 1.0f + env.windStrength;
    }

    // Less dust during rain (it settles)
    if (env.isRaining()) {
        rate *= 0.3f;
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
