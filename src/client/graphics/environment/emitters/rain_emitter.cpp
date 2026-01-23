#include "client/graphics/environment/emitters/rain_emitter.h"
#include "common/logging.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

RainEmitter::RainEmitter()
    : ParticleEmitter(ParticleType::Snowflake, 500)  // Reuse water droplet texture for now
{
    // Set default settings
    maxParticles_ = settings_.maxParticles;
    baseSpawnRate_ = settings_.spawnRate;
    spawnRadius_ = settings_.spawnRadius;
    enabled_ = settings_.enabled;

    // Resize particle pool
    particles_.resize(maxParticles_);
}

void RainEmitter::setIntensity(uint8_t intensity) {
    uint8_t newIntensity = std::min(intensity, static_cast<uint8_t>(10));
    if (newIntensity != intensity_) {
        LOG_INFO(MOD_GRAPHICS, "RainEmitter::setIntensity: {} -> {} (enabled_={} settings_.enabled={})",
                 intensity_, newIntensity, enabled_, settings_.enabled);
    }
    intensity_ = newIntensity;
}

void RainEmitter::setSettings(const RainSettings& settings) {
    settings_ = settings;

    // Update base class values
    maxParticles_ = settings_.maxParticles;
    baseSpawnRate_ = settings_.spawnRate;
    spawnRadius_ = settings_.spawnRadius;
    enabled_ = settings_.enabled;

    // Resize particle pool if needed
    if (static_cast<int>(particles_.size()) < settings_.maxParticles) {
        particles_.resize(settings_.maxParticles);
    }
}

bool RainEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_ || !settings_.enabled) {
        LOG_DEBUG(MOD_GRAPHICS, "RainEmitter::shouldBeActive returning false: enabled_={} settings_.enabled={}",
                  enabled_, settings_.enabled);
        return false;
    }

    // Only active when it's raining (intensity > 0)
    if (intensity_ == 0) {
        LOG_DEBUG(MOD_GRAPHICS, "RainEmitter::shouldBeActive returning false: intensity_=0");
        return false;
    }
    return true;
}

void RainEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
    // Indoor zones don't get rain
    if (biome == ZoneBiome::Dungeon || biome == ZoneBiome::Cave) {
        LOG_INFO(MOD_GRAPHICS, "RainEmitter::onZoneEnter: Zone '{}' biome={} -> DISABLING rain (indoor zone)",
                 zoneName, static_cast<int>(biome));
        enabled_ = false;
    } else {
        LOG_INFO(MOD_GRAPHICS, "RainEmitter::onZoneEnter: Zone '{}' biome={} -> enabled={}",
                 zoneName, static_cast<int>(biome), settings_.enabled);
        enabled_ = settings_.enabled;
    }
}

void RainEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Spawn in cylinder above player
    float angle = randomFloat(0.0f, 6.283185f);  // 2*PI
    float radius = randomFloat(0.0f, settings_.spawnRadius);

    p.position.x = env.playerPosition.x + std::cos(angle) * radius;
    p.position.y = env.playerPosition.y + std::sin(angle) * radius;
    p.position.z = env.playerPosition.z + settings_.spawnHeight;

    // Store ground level estimate (player Z minus typical height)
    groundLevel_ = env.playerPosition.z - 5.0f;

    // Initial velocity - primarily downward
    float speed = settings_.dropSpeed + randomFloat(-settings_.dropSpeedVariance, settings_.dropSpeedVariance);
    p.velocity = glm::vec3(0.0f, 0.0f, -speed);

    // Apply wind influence
    if (env.windStrength > 0.01f) {
        p.velocity.x += env.windDirection.x * env.windStrength * settings_.windInfluence * speed;
        p.velocity.y += env.windDirection.y * env.windStrength * settings_.windInfluence * speed;
    }

    // Small thin rain drops
    float sizeFactor = randomFloat(0.0f, 1.0f);
    sizeFactor = sizeFactor * sizeFactor;  // Bias toward smaller
    p.size = settings_.sizeMin + sizeFactor * (settings_.sizeMax - settings_.sizeMin);

    // Stretch factor for vertical elongation (rain streak effect)
    // Faster drops appear more stretched due to motion blur
    float speedFactor = speed / settings_.dropSpeed;  // Normalize to base speed
    p.stretch = settings_.lengthScale * (0.8f + 0.4f * speedFactor);

    // Align billboard with velocity direction for streak effect
    p.velocityAligned = true;

    // Lifetime based on fall distance
    float fallDistance = settings_.spawnHeight + 10.0f;  // Some buffer
    p.lifetime = fallDistance / speed;
    p.maxLifetime = p.lifetime;

    // Color with slight variation - light blue-white streaks
    float brightness = randomFloat(0.9f, 1.0f);
    p.color = glm::vec4(
        settings_.colorR * brightness,
        settings_.colorG * brightness,
        settings_.colorB * brightness,
        settings_.colorA);

    // Use rain streak texture (vertical elongated streak)
    p.textureIndex = ParticleAtlas::RainStreak;

    // No rotation needed - velocity alignment handles orientation
    p.rotation = 0.0f;
    p.rotationSpeed = 0.0f;

    // Start fully visible
    p.alpha = settings_.colorA;
}

void RainEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Apply continuous wind influence
    if (env.windStrength > 0.01f) {
        float windForce = settings_.windInfluence * deltaTime * 10.0f;
        p.velocity.x += env.windDirection.x * env.windStrength * windForce;
        p.velocity.y += env.windDirection.y * env.windStrength * windForce;
    }

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Calculate stretch factor based on velocity for motion blur effect
    // (This would be used by renderer if we had streak rendering)

    // Kill particle if it hits ground level
    if (p.position.z < groundLevel_) {
        p.lifetime = 0.0f;
        return;
    }

    // Fade out near ground for softer transition
    float heightAboveGround = p.position.z - groundLevel_;
    if (heightAboveGround < 5.0f) {
        p.alpha = settings_.colorA * (heightAboveGround / 5.0f);
    }

    // Kill if too far from player horizontally (drifted out of view)
    float dx = p.position.x - env.playerPosition.x;
    float dy = p.position.y - env.playerPosition.y;
    float distSq = dx * dx + dy * dy;
    float maxDistSq = settings_.spawnRadius * settings_.spawnRadius * 1.5f;
    if (distSq > maxDistSq) {
        p.lifetime = 0.0f;
    }
}

float RainEmitter::getSpawnRate(const EnvironmentState& env) const {
    if (intensity_ == 0) return 0.0f;

    // Scale spawn rate with intensity (1-10)
    // intensity 1 = 20% of max, intensity 10 = 100% of max
    float intensityFactor = 0.1f + (intensity_ / 10.0f) * 0.9f;
    float rate = settings_.spawnRate * intensityFactor;

    // Slightly more rain when windy (wind brings rain)
    if (env.windStrength > 0.3f) {
        rate *= 1.0f + (env.windStrength - 0.3f) * 0.3f;
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
