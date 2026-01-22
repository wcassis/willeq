#include "client/graphics/environment/emitters/pollen_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

PollenEmitter::PollenEmitter()
    : ParticleEmitter(ParticleType::Pollen, MAX_PARTICLES)
{
    baseSpawnRate_ = BASE_SPAWN_RATE;
    spawnRadius_ = SPAWN_RADIUS_MAX;
}

bool PollenEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_) return false;

    // Pollen only during daytime
    if (!env.isDaytime()) return false;

    // Only in appropriate biomes
    switch (currentBiome_) {
        case ZoneBiome::Forest:
        case ZoneBiome::Plains:
        case ZoneBiome::Swamp:  // Swamps have plants too
            return true;
        default:
            return false;
    }
}

void PollenEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
}

void PollenEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Spawn near ground level around player
    p.position = getRandomSpawnPosition(env,
        SPAWN_RADIUS_MIN, SPAWN_RADIUS_MAX,
        SPAWN_HEIGHT_MIN, SPAWN_HEIGHT_MAX);

    // Random size
    p.size = randomFloat(PARTICLE_SIZE_MIN, PARTICLE_SIZE_MAX);

    // Random lifetime
    p.lifetime = randomFloat(LIFETIME_MIN, LIFETIME_MAX);
    p.maxLifetime = p.lifetime;

    // Initial velocity - gentle upward drift
    p.velocity = glm::vec3(
        randomFloat(-0.2f, 0.2f),
        randomFloat(-0.2f, 0.2f),
        RISE_SPEED * randomFloat(0.8f, 1.2f)
    );

    // Apply wind
    if (env.windStrength > 0.1f) {
        p.velocity += env.windDirection * env.windStrength * WIND_FACTOR * 0.3f;
    }

    // Color: yellow-green pollen
    float hueVariation = randomFloat(-0.1f, 0.1f);
    p.color = glm::vec4(
        0.95f + hueVariation,           // R: slightly yellow
        0.95f + hueVariation * 0.5f,    // G: green-yellow
        0.6f + hueVariation,            // B: less blue
        1.0f
    );

    // Texture: spore shape
    p.textureIndex = ParticleAtlas::SporeShape;

    // Slow tumbling rotation
    p.rotation = randomFloat(0.0f, 6.28f);
    p.rotationSpeed = randomFloat(-0.5f, 0.5f);

    // Start faded
    p.alpha = 0.0f;
}

void PollenEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Continue upward drift (thermal currents)
    p.velocity.z += RISE_SPEED * 0.1f * deltaTime;

    // Apply wind
    if (env.windStrength > 0.05f) {
        glm::vec3 windForce = env.windDirection * env.windStrength * WIND_FACTOR * deltaTime;
        p.velocity += windForce;
    }

    // Gentle swaying motion
    float swayTime = p.glowPhase;  // Reuse glow phase for sway timing
    p.glowPhase += deltaTime;
    float swayX = std::sin(swayTime * 2.0f) * 0.1f * deltaTime;
    float swayY = std::cos(swayTime * 1.5f) * 0.1f * deltaTime;
    p.velocity.x += swayX;
    p.velocity.y += swayY;

    // Dampen horizontal velocity
    p.velocity.x *= 0.99f;
    p.velocity.y *= 0.99f;

    // Cap vertical speed
    if (p.velocity.z > RISE_SPEED * 2.0f) {
        p.velocity.z = RISE_SPEED * 2.0f;
    }

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Update rotation
    p.rotation += p.rotationSpeed * deltaTime;

    // Alpha based on lifetime
    float normalizedLife = p.getNormalizedLifetime();
    if (normalizedLife > 0.85f) {
        // Fade in
        p.alpha = (1.0f - normalizedLife) / 0.15f;
    } else if (normalizedLife < 0.2f) {
        // Fade out
        p.alpha = normalizedLife / 0.2f;
    } else {
        p.alpha = 1.0f;
    }

    // Pollen is fairly visible
    p.alpha *= 0.6f;
}

float PollenEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = BASE_SPAWN_RATE;

    // More pollen in forests
    if (currentBiome_ == ZoneBiome::Forest) {
        rate *= 1.5f;
    }

    // Peak pollen around midday
    float hourFromNoon = std::abs(env.timeOfDay - 12.0f);
    if (hourFromNoon < 4.0f) {
        rate *= 1.0f + (4.0f - hourFromNoon) * 0.1f;
    }

    // Less pollen during rain
    if (env.isRaining()) {
        rate *= 0.2f;
    }

    // Wind stirs up more pollen
    if (env.windStrength > 0.3f) {
        rate *= 1.0f + env.windStrength * 0.5f;
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
