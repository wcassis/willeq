#include "client/graphics/environment/emitters/firefly_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

FireflyEmitter::FireflyEmitter()
    : ParticleEmitter(ParticleType::Firefly, MAX_PARTICLES)
{
    baseSpawnRate_ = BASE_SPAWN_RATE;
    spawnRadius_ = SPAWN_RADIUS_MAX;
}

bool FireflyEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_) return false;

    // Fireflies only at night
    if (!env.isNighttime()) return false;

    // Only in appropriate biomes
    switch (currentBiome_) {
        case ZoneBiome::Forest:
        case ZoneBiome::Swamp:
        case ZoneBiome::Plains:
            return true;
        default:
            // Also appear near water in other biomes
            return hasWater_;
    }
}

void FireflyEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
    // Note: hasWater_ would be set by ParticleManager based on ZoneBiomeDetector
    hasWater_ = false;  // Default, will be updated by manager
}

void FireflyEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Spawn around player at low-medium height
    p.position = getRandomSpawnPosition(env,
        SPAWN_RADIUS_MIN, SPAWN_RADIUS_MAX,
        SPAWN_HEIGHT_MIN, SPAWN_HEIGHT_MAX);

    // Random size
    p.size = randomFloat(PARTICLE_SIZE_MIN, PARTICLE_SIZE_MAX);

    // Long lifetime
    p.lifetime = randomFloat(LIFETIME_MIN, LIFETIME_MAX);
    p.maxLifetime = p.lifetime;

    // Initial velocity - slow random direction
    glm::vec3 dir = randomDirection();
    dir.z *= 0.3f;  // Mostly horizontal movement
    p.velocity = dir * WANDER_SPEED * randomFloat(0.5f, 1.0f);

    // Color: yellow-green glow
    float greenTint = randomFloat(0.8f, 1.0f);
    p.color = glm::vec4(
        0.8f + randomFloat(0.0f, 0.2f),     // R: yellow
        greenTint,                           // G: bright green
        0.2f + randomFloat(0.0f, 0.2f),     // B: slight
        1.0f
    );

    // Texture: star/glow shape
    p.textureIndex = ParticleAtlas::StarShape;

    // Glow animation
    p.glowPhase = randomFloat(0.0f, 6.28f);  // Random starting phase
    p.glowSpeed = randomFloat(GLOW_SPEED_MIN, GLOW_SPEED_MAX);

    // No rotation needed for glow
    p.rotation = 0.0f;
    p.rotationSpeed = 0.0f;

    // Start visible (fireflies don't fade in slowly)
    p.alpha = 0.5f;
}

void FireflyEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Update glow phase
    p.glowPhase += p.glowSpeed * deltaTime;

    // Erratic direction changes (firefly behavior)
    float dirChangeChance = 2.0f * deltaTime;  // About 2 direction changes per second
    if (randomFloat(0.0f, 1.0f) < dirChangeChance) {
        // New random direction
        glm::vec3 newDir = randomDirection();
        newDir.z *= 0.3f;  // Keep mostly horizontal

        // Blend with current direction for smooth transition
        p.velocity = p.velocity * 0.3f + newDir * WANDER_SPEED * 0.7f;
    }

    // Occasional pause (fireflies stop and hover)
    float pauseChance = 0.5f * deltaTime;
    if (randomFloat(0.0f, 1.0f) < pauseChance) {
        p.velocity *= 0.1f;  // Almost stop
    }

    // Keep within reasonable height
    if (p.position.z < SPAWN_HEIGHT_MIN) {
        p.velocity.z = std::abs(p.velocity.z);
    }
    if (p.position.z > SPAWN_HEIGHT_MAX + 2.0f) {
        p.velocity.z = -std::abs(p.velocity.z);
    }

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Calculate glow intensity (pulsing)
    float glowIntensity = (std::sin(p.glowPhase) + 1.0f) * 0.5f;  // 0 to 1

    // Fireflies have distinct on/off phases
    if (glowIntensity < 0.3f) {
        glowIntensity = 0.1f;  // Dim
    } else {
        glowIntensity = 0.3f + glowIntensity * 0.7f;  // Bright
    }

    // Alpha based on glow and lifetime
    float normalizedLife = p.getNormalizedLifetime();
    float lifetimeAlpha = 1.0f;
    if (normalizedLife > 0.9f) {
        lifetimeAlpha = (1.0f - normalizedLife) * 10.0f;
    } else if (normalizedLife < 0.1f) {
        lifetimeAlpha = normalizedLife * 10.0f;
    }

    p.alpha = glowIntensity * lifetimeAlpha;

    // Size pulses slightly with glow
    float baseSize = (PARTICLE_SIZE_MIN + PARTICLE_SIZE_MAX) * 0.5f;
    p.size = baseSize * (0.8f + glowIntensity * 0.4f);
}

float FireflyEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = BASE_SPAWN_RATE;

    // More fireflies in swamps and forests
    if (currentBiome_ == ZoneBiome::Swamp) {
        rate *= 1.5f;
    } else if (currentBiome_ == ZoneBiome::Forest) {
        rate *= 1.3f;
    }

    // Near water
    if (hasWater_) {
        rate *= 1.3f;
    }

    // Peak activity around dusk/dawn
    if (env.isDusk() || env.isDawn()) {
        rate *= 1.5f;
    }

    // Fewer during rain
    if (env.isRaining()) {
        rate *= 0.3f;
    }

    // Fewer when very windy
    if (env.windStrength > 0.5f) {
        rate *= 0.5f;
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
