#include "client/graphics/environment/emitters/rain_splash_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

RainSplashEmitter::RainSplashEmitter()
    : ParticleEmitter(ParticleType::Snowflake, 200)  // Reuse water droplet texture
{
    // Set default settings
    maxParticles_ = settings_.maxParticles;
    baseSpawnRate_ = settings_.spawnRate;
    spawnRadius_ = settings_.spawnRadius;
    enabled_ = settings_.enabled;

    // Resize particle pool
    particles_.resize(maxParticles_);
}

void RainSplashEmitter::setIntensity(uint8_t intensity) {
    intensity_ = std::min(intensity, static_cast<uint8_t>(10));
}

void RainSplashEmitter::setSettings(const RainSplashSettings& settings) {
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

bool RainSplashEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_ || !settings_.enabled) return false;

    // Only active when it's raining (intensity > 0)
    return intensity_ > 0;
}

void RainSplashEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
    // Indoor zones don't get rain splashes
    if (biome == ZoneBiome::Dungeon || biome == ZoneBiome::Cave) {
        enabled_ = false;
    } else {
        enabled_ = settings_.enabled;
    }
}

void RainSplashEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Spawn on ground around player
    float angle = randomFloat(0.0f, 6.283185f);  // 2*PI
    float radius = randomFloat(0.0f, settings_.spawnRadius);

    // Spawn at player's estimated ground level
    p.position.x = env.playerPosition.x + std::cos(angle) * radius;
    p.position.y = env.playerPosition.y + std::sin(angle) * radius;
    p.position.z = env.playerPosition.z - 5.0f;  // Slightly below player eye level

    // Initial velocity - outward and upward splash
    float outwardAngle = angle + randomFloat(-0.5f, 0.5f);  // Slight randomization
    float speed = settings_.splashSpeed + randomFloat(-settings_.splashSpeedVariance, settings_.splashSpeedVariance);

    // Outward and upward
    p.velocity.x = std::cos(outwardAngle) * speed * 0.3f;
    p.velocity.y = std::sin(outwardAngle) * speed * 0.3f;
    p.velocity.z = speed;  // Upward

    // Random size (smaller splashes more common)
    float sizeFactor = randomFloat(0.0f, 1.0f);
    sizeFactor = sizeFactor * sizeFactor;  // Bias toward smaller
    p.size = settings_.sizeMin + sizeFactor * (settings_.sizeMax - settings_.sizeMin);

    // Short lifetime
    p.lifetime = settings_.lifetime + randomFloat(-0.1f, 0.1f);
    p.maxLifetime = p.lifetime;

    // Color with slight variation
    float brightness = randomFloat(0.9f, 1.0f);
    p.color = glm::vec4(
        settings_.colorR * brightness,
        settings_.colorG * brightness,
        settings_.colorB * brightness,
        settings_.colorA);

    // Use water droplet texture
    p.textureIndex = ParticleAtlas::WaterDroplet;

    // No rotation
    p.rotation = 0.0f;
    p.rotationSpeed = randomFloat(-3.0f, 3.0f);  // Slight spin

    // Start fully visible
    p.alpha = settings_.colorA;
}

void RainSplashEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Apply gravity
    p.velocity.z -= settings_.gravity * deltaTime;

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Apply rotation
    p.rotation += p.rotationSpeed * deltaTime;

    // Fade out over lifetime
    float lifeFraction = p.lifetime / p.maxLifetime;
    p.alpha = settings_.colorA * lifeFraction;

    // Shrink as it fades
    p.size *= (0.95f + 0.05f * lifeFraction);

    // Kill if below spawn point (hit ground again)
    if (p.velocity.z < 0 && p.position.z < env.playerPosition.z - 6.0f) {
        p.lifetime = 0.0f;
    }
}

float RainSplashEmitter::getSpawnRate(const EnvironmentState& env) const {
    if (intensity_ == 0) return 0.0f;

    // Scale spawn rate with intensity (1-10)
    // intensity 1 = 20% of max, intensity 10 = 100% of max
    float intensityFactor = 0.1f + (intensity_ / 10.0f) * 0.9f;
    return settings_.spawnRate * intensityFactor;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
