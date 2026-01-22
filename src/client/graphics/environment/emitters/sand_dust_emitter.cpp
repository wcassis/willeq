#include "client/graphics/environment/emitters/sand_dust_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

SandDustEmitter::SandDustEmitter()
    : ParticleEmitter(ParticleType::SandDust, MAX_PARTICLES)
{
    baseSpawnRate_ = BASE_SPAWN_RATE;
    spawnRadius_ = SPAWN_RADIUS_MAX;
}

bool SandDustEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_) return false;

    // Only in desert biome
    if (currentBiome_ != ZoneBiome::Desert) {
        return false;
    }

    // Need at least some wind for blowing sand
    return env.windStrength > 0.1f;
}

void SandDustEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
}

void SandDustEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Spawn at ground level around player
    p.position = getRandomSpawnPosition(env,
        SPAWN_RADIUS_MIN, SPAWN_RADIUS_MAX,
        SPAWN_HEIGHT_MIN, SPAWN_HEIGHT_MAX);

    // Small particle size
    p.size = randomFloat(PARTICLE_SIZE_MIN, PARTICLE_SIZE_MAX);

    // Short lifetime
    p.lifetime = randomFloat(LIFETIME_MIN, LIFETIME_MAX);
    p.maxLifetime = p.lifetime;

    // Initial velocity - primarily wind-driven
    p.velocity = env.windDirection * env.windStrength * WIND_FACTOR;

    // Add some random variation
    glm::vec3 randomOffset = randomDirection();
    randomOffset.z *= 0.3f;  // Less vertical variation
    p.velocity += randomOffset * BASE_SPEED * randomFloat(0.3f, 0.7f);

    // Slight upward lift
    p.velocity.z += 0.5f;

    // Color: sandy tan/brown
    float hueVariation = randomFloat(-0.1f, 0.1f);
    p.color = glm::vec4(
        0.85f + hueVariation,               // R: tan base
        0.70f + hueVariation * 0.8f,        // G: sandy
        0.50f + hueVariation * 0.5f,        // B: brown tint
        1.0f
    );

    // Texture: small dust
    p.textureIndex = ParticleAtlas::GrainShape;

    // Fast tumbling
    p.rotation = randomFloat(0.0f, 6.28f);
    p.rotationSpeed = randomFloat(-3.0f, 3.0f);

    // Start visible (sand appears quickly)
    p.alpha = 0.5f;
}

void SandDustEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Wind is the primary driver
    glm::vec3 windForce = env.windDirection * env.windStrength * WIND_FACTOR * deltaTime;
    p.velocity += windForce;

    // Gravity pulls down
    p.velocity.z -= 2.0f * deltaTime;

    // Ground bounce/settle
    if (p.position.z < 0.0f) {
        p.position.z = 0.0f;
        if (p.velocity.z < 0.0f) {
            // Small bounce or just slide
            if (randomFloat(0.0f, 1.0f) < 0.3f) {
                p.velocity.z = -p.velocity.z * 0.3f;  // Small bounce
            } else {
                p.velocity.z = 0.0f;  // Slide along ground
            }
        }
    }

    // Dampen slightly
    p.velocity *= 0.995f;

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Update rotation
    p.rotation += p.rotationSpeed * deltaTime;

    // Alpha based on lifetime - quick fade
    float normalizedLife = p.getNormalizedLifetime();
    if (normalizedLife > 0.9f) {
        // Quick fade in
        p.alpha = (1.0f - normalizedLife) * 10.0f;
    } else if (normalizedLife < 0.1f) {
        // Quick fade out
        p.alpha = normalizedLife * 10.0f;
    } else {
        p.alpha = 1.0f;
    }

    // Base visibility scales with wind
    float windAlpha = 0.3f + env.windStrength * 0.4f;
    p.alpha *= windAlpha;

    // Particles near ground are more visible (denser)
    if (p.position.z < 1.0f) {
        p.alpha *= 1.2f;
    }
}

float SandDustEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = BASE_SPAWN_RATE;

    // Strong correlation with wind
    rate *= (0.2f + env.windStrength * 2.0f);

    // Daytime heat creates more thermals/dust
    if (env.isDaytime()) {
        // Peak at midday
        float hourFromNoon = std::abs(env.timeOfDay - 12.0f);
        if (hourFromNoon < 4.0f) {
            rate *= 1.0f + (4.0f - hourFromNoon) * 0.15f;
        }
    }

    // Less sand during rain (wet sand doesn't blow)
    if (env.isRaining()) {
        rate *= 0.1f;  // Almost none
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
