#include "client/graphics/environment/emitters/dust_mote_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

DustMoteEmitter::DustMoteEmitter()
    : ParticleEmitter(ParticleType::DustMote, MAX_PARTICLES)
{
    baseSpawnRate_ = BASE_SPAWN_RATE;
    spawnRadius_ = SPAWN_RADIUS_MAX;
}

bool DustMoteEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_) return false;

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
        SPAWN_RADIUS_MIN, SPAWN_RADIUS_MAX,
        SPAWN_HEIGHT_MIN, SPAWN_HEIGHT_MAX);

    // Random size (smaller particles are more common)
    float sizeFactor = randomFloat(0.0f, 1.0f);
    sizeFactor = sizeFactor * sizeFactor; // Bias toward smaller
    p.size = PARTICLE_SIZE_MIN + sizeFactor * (PARTICLE_SIZE_MAX - PARTICLE_SIZE_MIN);

    // Random lifetime
    p.lifetime = randomFloat(LIFETIME_MIN, LIFETIME_MAX);
    p.maxLifetime = p.lifetime;

    // Initial velocity - very slow random drift
    glm::vec3 drift = randomDirection();
    drift.z *= 0.3f; // Reduce vertical component
    p.velocity = drift * DRIFT_SPEED * randomFloat(0.5f, 1.5f);

    // Apply initial wind influence
    if (env.windStrength > 0.1f) {
        p.velocity += env.windDirection * env.windStrength * WIND_FACTOR * 0.5f;
    }

    // Color: white/gray with slight variation
    float brightness = randomFloat(0.8f, 1.0f);
    p.color = glm::vec4(brightness, brightness, brightness, 1.0f);

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
        glm::vec3 windForce = env.windDirection * env.windStrength * WIND_FACTOR * deltaTime;
        p.velocity += windForce;
    }

    // Add subtle random drift (Brownian motion effect)
    glm::vec3 randomDrift = randomDirection() * 0.5f * deltaTime;
    p.velocity += randomDrift;

    // Gentle upward thermal drift (warm air rises)
    p.velocity.z += 0.1f * deltaTime;

    // Dampen velocity to prevent runaway speed
    float speed = glm::length(p.velocity);
    float maxSpeed = DRIFT_SPEED * 3.0f + env.windStrength * WIND_FACTOR;
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

    // Dust motes in indoor areas are slightly more visible
    if (isIndoor_) {
        p.alpha *= 0.7f;  // More visible in darker areas
    } else {
        p.alpha *= 0.4f;  // Less visible outdoors
    }
}

float DustMoteEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = BASE_SPAWN_RATE;

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
