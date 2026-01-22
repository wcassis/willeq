#include "client/graphics/environment/emitters/snow_emitter.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

SnowEmitter::SnowEmitter()
    : ParticleEmitter(ParticleType::Snowflake, 600)
{
    // Set default settings
    maxParticles_ = settings_.maxParticles;
    baseSpawnRate_ = settings_.spawnRate;
    spawnRadius_ = settings_.spawnRadius;
    enabled_ = settings_.enabled;

    // Resize particle pool
    particles_.resize(maxParticles_);
}

void SnowEmitter::setIntensity(uint8_t intensity) {
    intensity_ = std::min(intensity, static_cast<uint8_t>(10));
}

void SnowEmitter::setSettings(const SnowSettings& settings) {
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

bool SnowEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_ || !settings_.enabled) return false;

    // Only active when it's snowing (intensity > 0)
    return intensity_ > 0;
}

void SnowEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
    // Indoor zones don't get snow
    if (biome == ZoneBiome::Dungeon || biome == ZoneBiome::Cave) {
        enabled_ = false;
    } else {
        enabled_ = settings_.enabled;
    }
}

void SnowEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Spawn in cylinder above player
    float angle = randomFloat(0.0f, 6.283185f);  // 2*PI
    float radius = randomFloat(0.0f, settings_.spawnRadius);

    p.position.x = env.playerPosition.x + std::cos(angle) * radius;
    p.position.y = env.playerPosition.y + std::sin(angle) * radius;
    p.position.z = env.playerPosition.z + settings_.spawnHeight;

    // Store ground level estimate (player Z minus typical height)
    groundLevel_ = env.playerPosition.z - 5.0f;

    // Initial velocity - slow downward with some wind
    float speed = settings_.fallSpeed + randomFloat(-settings_.fallSpeedVariance, settings_.fallSpeedVariance);
    p.velocity = glm::vec3(0.0f, 0.0f, -speed);

    // Apply wind influence
    if (env.windStrength > 0.01f) {
        p.velocity.x += env.windDirection.x * env.windStrength * settings_.windInfluence * speed * 2.0f;
        p.velocity.y += env.windDirection.y * env.windStrength * settings_.windInfluence * speed * 2.0f;
    }

    // Random size
    p.size = settings_.sizeMin + randomFloat(0.0f, 1.0f) * (settings_.sizeMax - settings_.sizeMin);

    // Lifetime based on fall distance (longer than rain due to slower speed)
    float fallDistance = settings_.spawnHeight + 15.0f;
    p.lifetime = fallDistance / speed;
    p.maxLifetime = p.lifetime;

    // Store random phase offset for sway (using userData)
    // We'll encode phase in the rotation value since snow doesn't need directional rotation
    p.rotation = randomFloat(0.0f, 6.283185f);  // Random phase
    p.rotationSpeed = randomFloat(-1.0f, 1.0f);  // Slight spin

    // Color - pure white with slight variation
    float brightness = randomFloat(0.95f, 1.0f);
    p.color = glm::vec4(
        settings_.colorR * brightness,
        settings_.colorG * brightness,
        settings_.colorB * brightness,
        settings_.colorA);

    // Use snowflake texture
    p.textureIndex = ParticleAtlas::Snowflake;

    // Start fully visible
    p.alpha = settings_.colorA;
}

void SnowEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    // Apply continuous wind influence
    if (env.windStrength > 0.01f) {
        float windForce = settings_.windInfluence * deltaTime * 5.0f;
        p.velocity.x += env.windDirection.x * env.windStrength * windForce;
        p.velocity.y += env.windDirection.y * env.windStrength * windForce;
    }

    // Add swaying motion (sine wave side-to-side)
    // Use particle's stored phase (in rotation) for variation
    float swayPhase = p.rotation + swayTime_;
    float swayOffset = std::sin(swayPhase * settings_.swayFrequency * 6.283185f) * settings_.swayAmplitude;

    // Apply base velocity
    p.position += p.velocity * deltaTime;

    // Apply sway as a position offset (not velocity, so it oscillates smoothly)
    float prevSway = std::sin((swayPhase - deltaTime) * settings_.swayFrequency * 6.283185f) * settings_.swayAmplitude;
    float swayDelta = swayOffset - prevSway;
    p.position.x += swayDelta * 0.3f;
    p.position.y += swayDelta * 0.2f;

    // Update global sway time
    // (Note: this will drift slightly per-particle due to parallel updates, but that's fine)

    // Spin slowly
    p.rotation += p.rotationSpeed * deltaTime;

    // Kill particle if it hits ground level
    if (p.position.z < groundLevel_) {
        p.lifetime = 0.0f;
        return;
    }

    // Gentle fade out near ground
    float heightAboveGround = p.position.z - groundLevel_;
    if (heightAboveGround < 8.0f) {
        p.alpha = settings_.colorA * (heightAboveGround / 8.0f);
    }

    // Kill if too far from player horizontally
    float dx = p.position.x - env.playerPosition.x;
    float dy = p.position.y - env.playerPosition.y;
    float distSq = dx * dx + dy * dy;
    float maxDistSq = settings_.spawnRadius * settings_.spawnRadius * 1.8f;
    if (distSq > maxDistSq) {
        p.lifetime = 0.0f;
    }
}

float SnowEmitter::getSpawnRate(const EnvironmentState& env) const {
    if (intensity_ == 0) return 0.0f;

    // Scale spawn rate with intensity (1-10)
    // intensity 1 = 15% of max (light flurries), intensity 10 = 100% of max (blizzard)
    float intensityFactor = 0.05f + (intensity_ / 10.0f) * 0.95f;
    float rate = settings_.spawnRate * intensityFactor;

    // More snow during strong winds (blowing snow)
    if (env.windStrength > 0.3f) {
        rate *= 1.0f + (env.windStrength - 0.3f) * 0.5f;
    }

    return rate;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
