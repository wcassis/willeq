#include "client/graphics/environment/emitters/mist_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

MistEmitter::MistEmitter()
    : ParticleEmitter(ParticleType::Mist, MAX_PARTICLES)
{
    baseSpawnRate_ = BASE_SPAWN_RATE;
    spawnRadius_ = SPAWN_RADIUS_MAX;
}

bool MistEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_) return false;

    // Mist is always possible in swamps
    if (currentBiome_ == ZoneBiome::Swamp) {
        return true;
    }

    // Near water in other biomes
    if (hasWater_) {
        return true;
    }

    // Ocean/coastal areas
    if (currentBiome_ == ZoneBiome::Ocean) {
        return true;
    }

    // Some mist in forests at dawn/dusk
    if (currentBiome_ == ZoneBiome::Forest) {
        return env.isDawn() || env.isDusk();
    }

    return false;
}

void MistEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
    // Note: hasWater_ would be set by ParticleManager based on ZoneBiomeDetector
    hasWater_ = false;  // Default, will be updated by manager
}

void MistEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Spawn around player at low height
    p.position = getRandomSpawnPosition(env,
        SPAWN_RADIUS_MIN, SPAWN_RADIUS_MAX,
        SPAWN_HEIGHT_MIN, SPAWN_HEIGHT_MAX);

    // Large particle size
    p.size = randomFloat(PARTICLE_SIZE_MIN, PARTICLE_SIZE_MAX);

    // Long lifetime
    p.lifetime = randomFloat(LIFETIME_MIN, LIFETIME_MAX);
    p.maxLifetime = p.lifetime;

    // Initial velocity - slow horizontal drift
    glm::vec3 dir = randomDirection();
    dir.z = 0.0f;  // Keep horizontal
    if (glm::length(dir) > 0.01f) {
        dir = glm::normalize(dir);
    }
    p.velocity = dir * DRIFT_SPEED * randomFloat(0.5f, 1.0f);

    // Apply wind
    if (env.windStrength > 0.1f) {
        p.velocity += env.windDirection * env.windStrength * WIND_FACTOR * 0.3f;
    }

    // Color: white/gray mist
    float brightness = randomFloat(0.85f, 1.0f);
    p.color = glm::vec4(brightness, brightness, brightness, 1.0f);

    // Texture: soft cloud shape
    p.textureIndex = ParticleAtlas::WispyCloud;

    // Very slow rotation
    p.rotation = randomFloat(0.0f, 6.28f);
    p.rotationSpeed = randomFloat(-0.05f, 0.05f);

    // Start invisible (fade in)
    p.alpha = 0.0f;
}

void MistEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Apply wind
    if (env.windStrength > 0.05f) {
        glm::vec3 windForce = env.windDirection * env.windStrength * WIND_FACTOR * deltaTime;
        windForce.z = 0.0f;  // Keep mist low
        p.velocity += windForce;
    }

    // Gentle swirling motion
    float swirl = p.glowPhase;
    p.glowPhase += deltaTime * 0.5f;
    float swirlX = std::sin(swirl) * 0.05f * deltaTime;
    float swirlY = std::cos(swirl * 0.7f) * 0.05f * deltaTime;
    p.velocity.x += swirlX;
    p.velocity.y += swirlY;

    // Keep mist at low altitude
    if (p.position.z > SPAWN_HEIGHT_MAX + 1.0f) {
        p.velocity.z -= 0.1f * deltaTime;  // Push back down
    }
    if (p.position.z < SPAWN_HEIGHT_MIN - 1.0f) {
        p.velocity.z += 0.1f * deltaTime;  // Push back up
    }

    // Dampen velocity
    float dampening = 0.98f;
    p.velocity *= dampening;

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Update rotation
    p.rotation += p.rotationSpeed * deltaTime;

    // Alpha based on lifetime with long fades
    float normalizedLife = p.getNormalizedLifetime();
    if (normalizedLife > 0.8f) {
        // Slow fade in over first 20%
        p.alpha = (1.0f - normalizedLife) / 0.2f;
    } else if (normalizedLife < 0.2f) {
        // Slow fade out over last 20%
        p.alpha = normalizedLife / 0.2f;
    } else {
        p.alpha = 1.0f;
    }

    // Mist is translucent
    float baseAlpha = 0.25f;

    // More visible at dawn/dusk and night
    if (env.isDawn() || env.isDusk()) {
        baseAlpha = 0.35f;
    } else if (env.isNighttime()) {
        baseAlpha = 0.3f;
    }

    // Rain increases mist visibility
    if (env.isRaining()) {
        baseAlpha *= 1.3f;
    }

    p.alpha *= baseAlpha;

    // Size grows slightly as mist dissipates
    float ageFactor = 1.0f - normalizedLife;  // 0 at start, 1 at end
    p.size = PARTICLE_SIZE_MIN + (PARTICLE_SIZE_MAX - PARTICLE_SIZE_MIN) *
             (0.5f + ageFactor * 0.5f);
}

float MistEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = BASE_SPAWN_RATE;

    // More mist in swamps
    if (currentBiome_ == ZoneBiome::Swamp) {
        rate *= 2.0f;
    }

    // Near water
    if (hasWater_) {
        rate *= 1.5f;
    }

    // Peak mist at dawn and dusk
    if (env.isDawn() || env.isDusk()) {
        rate *= 1.8f;
    } else if (env.isNighttime()) {
        rate *= 1.3f;
    }

    // More mist after/during rain
    if (env.isRaining()) {
        rate *= 1.5f;
    }

    // Wind disperses mist
    if (env.windStrength > 0.5f) {
        rate *= 0.6f;
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
