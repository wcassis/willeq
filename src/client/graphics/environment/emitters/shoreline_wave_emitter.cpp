#include "client/graphics/environment/emitters/shoreline_wave_emitter.h"
#include "client/graphics/environment/zone_biome.h"
#include "common/logging.h"
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

ShorelineWaveEmitter::ShorelineWaveEmitter()
    : ParticleEmitter(ParticleType::ShorelineWave, 120)  // Initial max, will be updated
{
    reloadSettings();
}

void ShorelineWaveEmitter::reloadSettings() {
    settings_ = EnvironmentEffectsConfig::instance().getShorelineWaves();

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

bool ShorelineWaveEmitter::shouldBeActive(const EnvironmentState& env) const {
    if (!enabled_ || !settings_.enabled) return false;

    // Need water in the zone
    if (!zoneHasWater_) return false;

    // Need a surface map to detect shorelines
    if (!surfaceMap_ || !surfaceMap_->isLoaded()) return false;

    // Need cached shoreline points
    if (shorelinePoints_.empty()) return false;

    return true;
}

void ShorelineWaveEmitter::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;

    // Check if this zone has water
    const auto& detector = ZoneBiomeDetector::instance();
    zoneHasWater_ = detector.hasWater(zoneName);

    // Clear cached shoreline data
    shorelinePoints_.clear();
    shorelineCacheTimer_ = 0.0f;
    lastCachePosition_ = glm::vec3(0.0f);
    wavePhase_ = 0.0f;
}

void ShorelineWaveEmitter::update(float deltaTime, const EnvironmentState& env) {
    // Update wave phase for oscillation
    wavePhase_ += deltaTime;

    // Check if we need to refresh shoreline cache
    shorelineCacheTimer_ -= deltaTime;
    float distMoved = glm::distance(env.playerPosition, lastCachePosition_);

    if (shorelineCacheTimer_ <= 0.0f || distMoved > cacheMovementThreshold_) {
        detectShorelineNearPlayer(env.playerPosition);
        shorelineCacheTimer_ = cacheUpdateInterval_;

        // Debug: log particle state
        LOG_DEBUG(MOD_GRAPHICS, "ShorelineWaveEmitter: activeParticles={}, shorelinePoints={}, shouldBeActive={}",
                  activeCount_, shorelinePoints_.size(), shouldBeActive(env) ? "yes" : "no");
    }

    // Call base class update for particle spawning and movement
    ParticleEmitter::update(deltaTime, env);
}

void ShorelineWaveEmitter::initParticle(Particle& p, const EnvironmentState& env) {
    // Get a shoreline point to spawn at
    const ShorelinePoint* shore = getRandomShorelinePoint();
    if (!shore) {
        // Fallback: spawn around player (shouldn't happen if shoreline detection worked)
        p.position = getRandomSpawnPosition(env,
            settings_.spawnRadiusMin, settings_.spawnRadiusMax,
            settings_.spawnHeightMin, settings_.spawnHeightMax);
        LOG_DEBUG(MOD_GRAPHICS, "ShorelineWaveEmitter: Fallback spawn at ({:.1f}, {:.1f}, {:.1f})",
                  p.position.x, p.position.y, p.position.z);
    } else {
        // Spawn at shoreline with some offset along the edge
        p.position = shore->position;

        // Add random offset along the shoreline (perpendicular to normal)
        glm::vec3 tangent = glm::cross(shore->normal, glm::vec3(0.0f, 0.0f, 1.0f));
        if (glm::length(tangent) < 0.01f) {
            tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        tangent = glm::normalize(tangent);
        p.position += tangent * randomFloat(-3.0f, 3.0f);

        // Slight offset toward water
        p.position -= shore->normal * randomFloat(0.0f, 1.0f);

        // Set height at water surface level (derived from adjacent land height)
        p.position.z = shore->waterHeight + randomFloat(0.0f, 0.5f);

        LOG_DEBUG(MOD_GRAPHICS, "ShorelineWaveEmitter: Spawned at ({:.1f}, {:.1f}, {:.1f}), waterHeight={:.1f}, player=({:.1f}, {:.1f}, {:.1f})",
                  p.position.x, p.position.y, p.position.z, shore->waterHeight,
                  env.playerPosition.x, env.playerPosition.y, env.playerPosition.z);
    }

    // Decide particle type: foam (70%) or spray (30%)
    bool isFoam = randomFloat(0.0f, 1.0f) < 0.7f;

    if (isFoam) {
        // Foam: larger, longer-lived, white - rises gently then falls
        p.size = randomFloat(settings_.sizeMin * 1.5f, settings_.sizeMax * 1.5f);
        p.lifetime = randomFloat(settings_.lifetimeMin, settings_.lifetimeMax);
        p.textureIndex = ParticleAtlas::FoamSpray;

        // Outward drift from shore
        if (shore) {
            p.velocity = -shore->normal * settings_.driftSpeed * randomFloat(0.3f, 0.7f);
        } else {
            p.velocity = randomDirection() * settings_.driftSpeed * 0.5f;
        }
        // Gentle upward launch (wave crest)
        p.velocity.z = randomFloat(1.0f, 2.5f);
    } else {
        // Spray: smaller, short-lived, faster upward arc
        p.size = randomFloat(settings_.sizeMin * 0.5f, settings_.sizeMax * 0.8f);
        p.lifetime = randomFloat(settings_.lifetimeMin * 0.3f, settings_.lifetimeMax * 0.5f);
        p.textureIndex = ParticleAtlas::WaterDroplet;

        // Outward from shore
        if (shore) {
            p.velocity = -shore->normal * settings_.driftSpeed * randomFloat(1.0f, 2.0f);
        } else {
            p.velocity = randomDirection() * settings_.driftSpeed;
        }
        // Faster upward launch
        p.velocity.z = randomFloat(2.0f, 4.0f);
    }

    p.maxLifetime = p.lifetime;

    // Color from config with slight variation
    float brightness = randomFloat(0.9f, 1.0f);
    p.color = glm::vec4(
        settings_.colorR * brightness,
        settings_.colorG * brightness,
        settings_.colorB * brightness,
        settings_.colorA
    );

    // Store whether this is foam (0) or spray (1) in rotation (used in update)
    p.rotation = isFoam ? 0.0f : 1.0f;
    // Store original size in rotationSpeed so we can shrink over lifetime
    p.rotationSpeed = p.size;

    // Store wave phase offset in glowPhase for oscillation
    p.glowPhase = randomFloat(0.0f, 6.28f);

    // Store the water height reference in glowSpeed for foam particles
    // (so updateParticle can oscillate relative to the actual water surface)
    p.glowSpeed = shore ? shore->waterHeight : p.position.z;

    // Start semi-transparent
    p.alpha = 0.0f;
}

void ShorelineWaveEmitter::updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) {
    bool isSpray = p.rotation > 0.5f;
    float waterHeight = p.glowSpeed;  // Stored in initParticle

    if (isSpray) {
        // Spray: faster gravity-affected arc
        p.velocity.z -= 8.0f * deltaTime;  // Gravity

        // Spray dies when hitting water level
        if (p.position.z < waterHeight && p.velocity.z < 0.0f) {
            p.lifetime = 0.0f;
            return;
        }
    } else {
        // Foam: gentler gravity, more drift
        p.velocity.z -= 4.0f * deltaTime;  // Lighter gravity

        // Foam floats on water surface instead of dying
        if (p.position.z <= waterHeight) {
            p.position.z = waterHeight;
            p.velocity.z = 0.0f;  // Stop vertical movement, float on surface
        }

        // Apply wind for foam drift
        if (env.windStrength > 0.1f) {
            glm::vec3 windForce = env.windDirection * env.windStrength *
                                  settings_.windFactor * 0.5f * deltaTime;
            windForce.z = 0.0f;
            p.velocity += windForce;
        }

        // Dampen horizontal velocity for foam
        p.velocity.x *= 0.98f;
        p.velocity.y *= 0.98f;
    }

    // Apply velocity
    p.position += p.velocity * deltaTime;

    // Alpha based on lifetime
    float normalizedLife = p.getNormalizedLifetime();
    if (normalizedLife > 0.85f) {
        // Quick fade in
        p.alpha = (1.0f - normalizedLife) / 0.15f;
    } else if (normalizedLife < 0.15f) {
        // Quick fade out
        p.alpha = normalizedLife / 0.15f;
    } else {
        p.alpha = 1.0f;
    }

    // Base alpha from config
    float baseAlpha = settings_.alphaOutdoor;

    // More visible with stronger wind (bigger waves)
    if (env.windStrength > 0.3f) {
        baseAlpha *= 1.0f + (env.windStrength - 0.3f) * 0.5f;
    }

    p.alpha *= baseAlpha;

    // Foam dissipates (shrinks) over lifetime - use stored original size
    // p.rotationSpeed holds the original size from initParticle
    float originalSize = p.rotationSpeed;
    // Shrink from 100% to 40% over lifetime
    float sizeFactor = 0.4f + 0.6f * normalizedLife;
    p.size = originalSize * sizeFactor;
}

float ShorelineWaveEmitter::getSpawnRate(const EnvironmentState& env) const {
    float rate = settings_.spawnRate;

    // Scale with available shoreline points
    if (shorelinePoints_.size() < 5) {
        rate *= 0.5f;
    } else if (shorelinePoints_.size() > 20) {
        rate *= 1.5f;
    }

    // More spray with stronger wind
    rate *= (0.5f + env.windStrength * 1.5f);

    // Biome modifiers
    if (currentBiome_ == ZoneBiome::Ocean) {
        rate *= 1.5f;  // More waves on ocean/coast
    } else if (currentBiome_ == ZoneBiome::Swamp) {
        rate *= 0.5f;  // Less waves in still swamp water
    }

    // Less during heavy rain (rain splashes dominate)
    if (env.isRaining()) {
        rate *= 0.7f;
    }

    return rate;
}

void ShorelineWaveEmitter::detectShorelineNearPlayer(const glm::vec3& playerPos) {
    if (!surfaceMap_ || !surfaceMap_->isLoaded()) {
        LOG_DEBUG(MOD_GRAPHICS, "ShorelineWaveEmitter: No surface map loaded (map={}, loaded={})",
                  surfaceMap_ ? "present" : "null",
                  surfaceMap_ ? (surfaceMap_->isLoaded() ? "yes" : "no") : "n/a");
        return;
    }

    shorelinePoints_.clear();
    int waterCells = 0;
    int shorelineCells = 0;
    int sandCells = 0;
    int unknownCells = 0;
    int otherCells = 0;

    float cellSize = surfaceMap_->getCellSize();
    if (cellSize < 0.1f) cellSize = 2.0f;  // Default cell size

    // Log the surface type at exact player position
    Detail::SurfaceType playerCellType = surfaceMap_->getSurfaceType(playerPos.x, playerPos.y);
    float playerCellHeight = surfaceMap_->getHeight(playerPos.x, playerPos.y);
    const char* typeNames[] = {"Unknown", "Grass", "Dirt", "Stone", "Brick", "Wood", "Sand", "Snow", "Water", "Lava", "Jungle", "Swamp", "Rock"};
    int typeIdx = static_cast<int>(playerCellType);
    if (typeIdx < 0 || typeIdx > 12) typeIdx = 0;
    LOG_DEBUG(MOD_GRAPHICS, "ShorelineWaveEmitter: Player cell at ({:.1f}, {:.1f}) = {} (height={:.1f})",
              playerPos.x, playerPos.y, typeNames[typeIdx], playerCellHeight);

    // Scan grid around player
    float scanRadius = detectionRadius_;
    int cellsToScan = static_cast<int>(scanRadius / cellSize) + 1;

    // Find nearest non-water cell for debugging
    float nearestNonWaterDist = 999999.0f;
    float nearestNonWaterX = 0, nearestNonWaterY = 0;
    Detail::SurfaceType nearestNonWaterType = Detail::SurfaceType::Unknown;

    for (int dy = -cellsToScan; dy <= cellsToScan; ++dy) {
        for (int dx = -cellsToScan; dx <= cellsToScan; ++dx) {
            float x = playerPos.x + dx * cellSize;
            float y = playerPos.y + dy * cellSize;

            // Check distance
            float distSq = (x - playerPos.x) * (x - playerPos.x) +
                          (y - playerPos.y) * (y - playerPos.y);
            if (distSq > scanRadius * scanRadius) continue;

            // Check if this is a shoreline cell
            Detail::SurfaceType cellType = surfaceMap_->getSurfaceType(x, y);
            if (cellType == Detail::SurfaceType::Water) {
                waterCells++;
            } else if (cellType == Detail::SurfaceType::Sand) {
                sandCells++;
                // Track nearest non-water cell
                if (distSq < nearestNonWaterDist) {
                    nearestNonWaterDist = distSq;
                    nearestNonWaterX = x;
                    nearestNonWaterY = y;
                    nearestNonWaterType = cellType;
                }
            } else if (cellType == Detail::SurfaceType::Unknown) {
                unknownCells++;
            } else {
                otherCells++;
                // Track nearest non-water cell
                if (distSq < nearestNonWaterDist) {
                    nearestNonWaterDist = distSq;
                    nearestNonWaterX = x;
                    nearestNonWaterY = y;
                    nearestNonWaterType = cellType;
                }
            }

            glm::vec3 normal;
            float waterSurfaceZ;
            if (isShorelineCell(x, y, normal, waterSurfaceZ)) {
                shorelineCells++;
                ShorelinePoint sp;
                sp.position = glm::vec3(x, y, waterSurfaceZ);  // Use water surface Z, not seafloor
                sp.normal = normal;
                sp.waterHeight = waterSurfaceZ;

                shorelinePoints_.push_back(sp);
            }
        }
    }

    lastCachePosition_ = playerPos;

    // Log detailed results
    LOG_DEBUG(MOD_GRAPHICS, "ShorelineWaveEmitter: Scan at ({:.1f}, {:.1f}): water={}, sand={}, unknown={}, other={}, shoreline={}",
              playerPos.x, playerPos.y, waterCells, sandCells, unknownCells, otherCells, shorelineCells);

    if (nearestNonWaterDist < 999999.0f) {
        int nwTypeIdx = static_cast<int>(nearestNonWaterType);
        if (nwTypeIdx < 0 || nwTypeIdx > 12) nwTypeIdx = 0;
        LOG_DEBUG(MOD_GRAPHICS, "ShorelineWaveEmitter: Nearest non-water cell: {} at ({:.1f}, {:.1f}), dist={:.1f}",
                  typeNames[nwTypeIdx], nearestNonWaterX, nearestNonWaterY, std::sqrt(nearestNonWaterDist));
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "ShorelineWaveEmitter: No non-water cells found within {} units", scanRadius);
    }
}

bool ShorelineWaveEmitter::isShorelineCell(float x, float y, glm::vec3& outNormal, float& outWaterSurfaceZ) const {
    if (!surfaceMap_) return false;

    // Check if current cell is water
    Detail::SurfaceType current = surfaceMap_->getSurfaceType(x, y);
    if (current != Detail::SurfaceType::Water) return false;

    float cellSize = surfaceMap_->getCellSize();
    if (cellSize < 0.1f) cellSize = 2.0f;

    // Check neighbors - if any neighbor is not water, this is a shoreline
    glm::vec3 normal(0.0f);
    int nonWaterCount = 0;
    float minLandHeight = 100000.0f;

    // 4-connected neighbors
    const float offsets[4][2] = {{cellSize, 0}, {-cellSize, 0}, {0, cellSize}, {0, -cellSize}};

    for (int i = 0; i < 4; ++i) {
        float nx = x + offsets[i][0];
        float ny = y + offsets[i][1];

        Detail::SurfaceType neighbor = surfaceMap_->getSurfaceType(nx, ny);
        if (neighbor != Detail::SurfaceType::Water && neighbor != Detail::SurfaceType::Unknown) {
            // This neighbor is land - add to normal and track minimum height
            normal.x += offsets[i][0];
            normal.y += offsets[i][1];
            float landHeight = surfaceMap_->getHeight(nx, ny);
            if (landHeight < minLandHeight) {
                minLandHeight = landHeight;
            }
            nonWaterCount++;
        }
    }

    if (nonWaterCount == 0) return false;  // Not a shoreline

    // Normalize the normal (points from water toward land)
    if (glm::length(normal) > 0.01f) {
        outNormal = glm::normalize(normal);
    } else {
        outNormal = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    // Water surface is flat - use minimum adjacent land height as the water level
    // (this is where the water actually meets the land)
    outWaterSurfaceZ = minLandHeight;

    return true;
}

const ShorelineWaveEmitter::ShorelinePoint* ShorelineWaveEmitter::getRandomShorelinePoint() const {
    if (shorelinePoints_.empty()) return nullptr;

    int idx = static_cast<int>(randomFloat(0.0f, static_cast<float>(shorelinePoints_.size()) - 0.01f));
    return &shorelinePoints_[idx];
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
