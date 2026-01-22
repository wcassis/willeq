#include "client/graphics/environment/particle_emitter.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace EQT {
namespace Graphics {
namespace Environment {

ParticleEmitter::ParticleEmitter(ParticleType type, int maxParticles)
    : type_(type)
    , maxParticles_(maxParticles)
{
    // Seed RNG with current time
    auto seed = static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    rng_.seed(seed);

    // Pre-allocate particle pool
    particles_.resize(maxParticles);
}

void ParticleEmitter::update(float deltaTime, const EnvironmentState& env) {
    if (!enabled_ || !shouldBeActive(env)) {
        // Fade out existing particles but don't spawn new ones
        for (auto& p : particles_) {
            if (p.isAlive()) {
                p.lifetime -= deltaTime;
                if (p.lifetime <= 0.0f) {
                    killParticle(p);
                }
            }
        }
        return;
    }

    // Update existing particles
    for (auto& p : particles_) {
        if (p.isAlive()) {
            updateParticle(p, deltaTime, env);

            // Update lifetime
            p.lifetime -= deltaTime;
            if (p.lifetime <= 0.0f) {
                killParticle(p);
            }
        }
    }

    // Spawn new particles based on spawn rate
    float effectiveSpawnRate = getSpawnRate(env) * densityMult_;
    int effectiveMaxParticles = static_cast<int>(maxParticles_ * densityMult_);

    spawnAccumulator_ += effectiveSpawnRate * deltaTime;

    while (spawnAccumulator_ >= 1.0f && activeCount_ < effectiveMaxParticles) {
        Particle* p = spawnParticle();
        if (p) {
            initParticle(*p, env);
        }
        spawnAccumulator_ -= 1.0f;
    }

    // Clamp accumulator to prevent burst spawning after pauses
    spawnAccumulator_ = std::min(spawnAccumulator_, 5.0f);
}

Particle* ParticleEmitter::spawnParticle() {
    // Find an inactive particle slot
    for (auto& p : particles_) {
        if (!p.isAlive()) {
            // Reset particle to default state
            p = Particle{};
            p.lifetime = 1.0f;  // Will be set by initParticle
            p.maxLifetime = 1.0f;
            activeCount_++;
            return &p;
        }
    }
    return nullptr;
}

void ParticleEmitter::killParticle(Particle& p) {
    // Note: lifetime may already be <= 0 when called from update loop
    // Only decrement if we haven't already counted this particle as dead
    if (p.lifetime != 0.0f) {
        activeCount_--;
    }
    p.lifetime = 0.0f;
}

void ParticleEmitter::clearAllParticles() {
    for (auto& p : particles_) {
        p.lifetime = 0.0f;
    }
    activeCount_ = 0;
    spawnAccumulator_ = 0.0f;
}

void ParticleEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Basic physics: apply velocity
    p.position += p.velocity * deltaTime;

    // Update rotation
    p.rotation += p.rotationSpeed * deltaTime;

    // Update glow phase for glowing particles
    p.glowPhase += p.glowSpeed * deltaTime;

    // Fade alpha based on lifetime (fade in first 10%, fade out last 20%)
    float normalizedLife = p.getNormalizedLifetime();
    if (normalizedLife > 0.9f) {
        // Fade in
        p.alpha = (1.0f - normalizedLife) * 10.0f;
    } else if (normalizedLife < 0.2f) {
        // Fade out
        p.alpha = normalizedLife * 5.0f;
    } else {
        p.alpha = 1.0f;
    }
}

glm::vec3 ParticleEmitter::getRandomSpawnPosition(
    const EnvironmentState& env,
    float minRadius, float maxRadius,
    float minHeight, float maxHeight) const
{
    // Random angle
    float angle = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));

    // Random radius (weighted toward outer edge for more even distribution)
    float r = std::sqrt(randomFloat(0.0f, 1.0f)) * (maxRadius - minRadius) + minRadius;

    // Random height
    float h = randomFloat(minHeight, maxHeight);

    return env.playerPosition + glm::vec3(
        r * std::cos(angle),
        r * std::sin(angle),
        h
    );
}

float ParticleEmitter::randomFloat(float min, float max) const {
    return min + dist01_(rng_) * (max - min);
}

glm::vec3 ParticleEmitter::randomDirection() const {
    // Generate random point on unit sphere
    float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
    float phi = std::acos(randomFloat(-1.0f, 1.0f));

    return glm::vec3(
        std::sin(phi) * std::cos(theta),
        std::sin(phi) * std::sin(theta),
        std::cos(phi)
    );
}

void ParticleEmitter::applyWind(Particle& p, const EnvironmentState& env, float windFactor) {
    // Apply wind influence to particle velocity
    glm::vec3 windVelocity = env.windDirection * env.windStrength * windFactor;
    p.velocity += windVelocity;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
