#include "client/graphics/simulation_worker.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace EQT {
namespace Graphics {

// ============================================================================
// Boids Simulation (worker thread)
// ============================================================================

float SimulationWorker::boidsRandomFloat(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(boidsRng_);
}

glm::vec3 SimulationWorker::boidsGetRandomSpawnPosition(const glm::vec3& playerPos) {
    float angle = boidsRandomFloat(0.0f, 6.28318f);
    float dist = boidsRandomFloat(40.0f, 80.0f);
    glm::vec3 pos = playerPos;
    pos.x += std::cos(angle) * dist;
    pos.y += std::sin(angle) * dist;

    if (boidsHasBounds_) {
        pos.x = glm::clamp(pos.x, boidsBoundsMin_.x + 20.0f, boidsBoundsMax_.x - 20.0f);
        pos.y = glm::clamp(pos.y, boidsBoundsMin_.y + 20.0f, boidsBoundsMax_.y - 20.0f);
    }
    return pos;
}

std::vector<Environment::CreatureType> SimulationWorker::boidsGetTypesForBiome(int biome, bool isDay) {
    using CT = Environment::CreatureType;
    using ZB = Environment::ZoneBiome;
    std::vector<CT> types;
    auto b = static_cast<ZB>(biome);

    switch (b) {
        case ZB::Forest:
            if (isDay) { types.push_back(CT::Bird); types.push_back(CT::Butterfly); }
            else { types.push_back(CT::Bat); types.push_back(CT::Firefly); }
            break;
        case ZB::Swamp:
            if (isDay) { types.push_back(CT::Dragonfly); }
            else { types.push_back(CT::Bat); types.push_back(CT::Firefly); }
            break;
        case ZB::Desert:
            if (isDay) { types.push_back(CT::Crow); }
            else { types.push_back(CT::Bat); }
            break;
        case ZB::Plains:
            if (isDay) { types.push_back(CT::Bird); types.push_back(CT::Butterfly); }
            else { types.push_back(CT::Bat); types.push_back(CT::Firefly); }
            break;
        case ZB::Urban:
            if (isDay) { types.push_back(CT::Crow); types.push_back(CT::Bird); }
            else { types.push_back(CT::Bat); }
            break;
        case ZB::Ocean:
            if (isDay) { types.push_back(CT::Seagull); }
            break;
        case ZB::Dungeon:
        case ZB::Cave:
            types.push_back(CT::Bat);
            break;
        case ZB::Volcanic:
            if (isDay) { types.push_back(CT::Crow); }
            else { types.push_back(CT::Bat); }
            break;
        case ZB::Snow:
            if (isDay) { types.push_back(CT::Crow); }
            break;
        default:
            if (isDay) { types.push_back(CT::Bird); }
            break;
    }

    // Filter disabled types
    types.erase(
        std::remove_if(types.begin(), types.end(),
            [this](CT t) { return !boidsTypeEnabled_[static_cast<size_t>(t)]; }),
        types.end());

    return types;
}

void SimulationWorker::computeBoids(const SimulationInput& input, SimulationOutput& output) {
    const auto& bi = input.boidsInput;
    if (!bi.initialized) {
        output.boidsOutput.valid = false;
        return;
    }

    // Process commands
    for (const auto& cmd : bi.commands) {
        switch (cmd.type) {
            case BoidsCommand::ZoneEnter:
                boidsFlocks_.clear();
                boidsSpawnTimer_ = 5.0f;
                boidsZoneBiome_ = cmd.zoneBiome;
                if (cmd.hasBounds) {
                    boidsBoundsMin_ = cmd.boundsMin;
                    boidsBoundsMax_ = cmd.boundsMax;
                    boidsHasBounds_ = true;
                }
                break;
            case BoidsCommand::ZoneLeave:
                boidsFlocks_.clear();
                boidsZoneBiome_ = 0;
                boidsHasBounds_ = false;
                break;
            case BoidsCommand::SetQuality:
                boidsQuality_ = cmd.quality;
                break;
            case BoidsCommand::SetDensity:
                boidsDensity_ = cmd.density;
                break;
            case BoidsCommand::SetEnabled:
                boidsEnabled_ = cmd.enabled;
                if (!boidsEnabled_) boidsFlocks_.clear();
                break;
            case BoidsCommand::SetTypeEnabled:
                if (cmd.creatureType < static_cast<uint8_t>(Environment::CreatureType::Count))
                    boidsTypeEnabled_[cmd.creatureType] = cmd.typeEnabled;
                break;
        }
    }

    if (!boidsEnabled_ || boidsQuality_ == 0) {
        output.boidsOutput.creatures.clear();
        output.boidsOutput.activeCount = 0;
        output.boidsOutput.valid = true;
        return;
    }

    float dt = bi.deltaTime;
    glm::vec3 playerPos = bi.playerPosition;
    float timeOfDay = bi.timeOfDay;
    bool isDay = (timeOfDay >= 6.0f && timeOfDay < 20.0f);

    auto budget = Environment::BoidsBudget::fromQuality(boidsQuality_);

    // Spawn logic
    boidsSpawnTimer_ -= dt;
    if (boidsSpawnTimer_ <= 0.0f) {
        boidsSpawnTimer_ = boidsSpawnCooldown_;

        if (static_cast<int>(boidsFlocks_.size()) < budget.maxFlocks) {
            // Count total creatures
            int totalCreatures = 0;
            for (const auto& f : boidsFlocks_) totalCreatures += static_cast<int>(f.creatures.size());

            if (totalCreatures < budget.maxCreatures) {
                auto validTypes = boidsGetTypesForBiome(boidsZoneBiome_, isDay);
                if (!validTypes.empty()) {
                    std::uniform_int_distribution<size_t> typeDist(0, validTypes.size() - 1);
                    auto type = validTypes[typeDist(boidsRng_)];
                    auto config = Environment::getDefaultFlockConfig(type);

                    float effectiveDensity = boidsDensity_ * budget.densityMult;
                    config.minSize = std::max(2, static_cast<int>(config.minSize * effectiveDensity));
                    config.maxSize = std::max(config.minSize, static_cast<int>(config.maxSize * effectiveDensity));

                    glm::vec3 spawnPos = boidsGetRandomSpawnPosition(playerPos);
                    float minSpawnZ = playerPos.z + config.heightMin;
                    spawnPos.z = minSpawnZ + (config.heightMax - config.heightMin) * 0.5f;

                    // Create flock
                    WorkerFlockState flock;
                    flock.config = config;
                    flock.anchor = spawnPos;
                    flock.destination = spawnPos;
                    flock.center = spawnPos;
                    if (boidsHasBounds_) {
                        flock.boundsMin = boidsBoundsMin_;
                        flock.boundsMax = boidsBoundsMax_;
                        flock.hasBounds = true;
                    }

                    // Initialize creatures
                    std::uniform_int_distribution<int> sizeDist(config.minSize, config.maxSize);
                    int flockSize = sizeDist(boidsRng_);
                    flock.creatures.reserve(flockSize);
                    for (int i = 0; i < flockSize; ++i) {
                        WorkerCreature c;
                        c.position = spawnPos + glm::vec3(
                            boidsRandomFloat(-5.0f, 5.0f),
                            boidsRandomFloat(-5.0f, 5.0f),
                            boidsRandomFloat(-2.0f, 2.0f));
                        glm::vec3 dir = glm::normalize(glm::vec3(
                            boidsRandomFloat(-1.0f, 1.0f),
                            boidsRandomFloat(-1.0f, 1.0f),
                            boidsRandomFloat(-0.3f, 0.3f)));
                        c.speed = boidsRandomFloat(config.minSpeed, config.maxSpeed);
                        c.velocity = dir * c.speed;
                        c.size = boidsRandomFloat(0.8f, 1.2f);
                        c.textureIndex = Environment::CreatureAtlas::getBaseIndex(config.type);
                        c.animFrame = boidsRandomFloat(0.0f, 2.0f);
                        c.animSpeed = boidsRandomFloat(config.animSpeedMin, config.animSpeedMax);
                        flock.creatures.push_back(c);
                    }

                    // Set destination
                    glm::vec3 dest = boidsGetRandomSpawnPosition(playerPos);
                    dest.z = spawnPos.z;
                    flock.destination = dest;

                    boidsFlocks_.push_back(std::move(flock));
                }
            }
        }
    }

    // Update each flock
    for (auto& flock : boidsFlocks_) {
        flock.timeAlive += dt;

        // Handle scattering
        if (flock.scattering) {
            flock.scatterTimer -= dt;
            if (flock.scatterTimer <= 0.0f) flock.scattering = false;
        }

        // Update destination
        flock.destinationTimer -= dt;
        if (flock.destinationTimer <= 0.0f) {
            if (flock.hasBounds) {
                flock.destination = glm::vec3(
                    boidsRandomFloat(flock.boundsMin.x + 50.0f, flock.boundsMax.x - 50.0f),
                    boidsRandomFloat(flock.boundsMin.y + 50.0f, flock.boundsMax.y - 50.0f),
                    playerPos.z + boidsRandomFloat(flock.config.heightMin, flock.config.heightMax));
            } else {
                flock.destination = flock.anchor + glm::vec3(
                    boidsRandomFloat(-flock.config.patrolRadius, flock.config.patrolRadius),
                    boidsRandomFloat(-flock.config.patrolRadius, flock.config.patrolRadius),
                    boidsRandomFloat(-10.0f, 10.0f));
                flock.destination.z = std::max(flock.destination.z, playerPos.z + flock.config.heightMin);
            }
            float minZ = playerPos.z + flock.config.heightMin;
            float maxZ = playerPos.z + flock.config.heightMax;
            flock.destination.z = glm::clamp(flock.destination.z, minZ, maxZ);
            flock.destinationTimer = flock.destinationInterval;
        }

        // Boids per-creature update
        for (auto& creature : flock.creatures) {
            // Separation
            glm::vec3 separation{0.0f};
            int sepCount = 0;
            for (const auto& other : flock.creatures) {
                if (&other == &creature) continue;
                float d = glm::distance(creature.position, other.position);
                if (d < flock.config.separationRadius && d > 0.001f) {
                    separation += glm::normalize(creature.position - other.position) / d;
                    sepCount++;
                }
            }
            if (sepCount > 0) separation /= static_cast<float>(sepCount);

            // Alignment
            glm::vec3 alignment{0.0f};
            int alignCount = 0;
            for (const auto& other : flock.creatures) {
                if (&other == &creature) continue;
                if (glm::distance(creature.position, other.position) < flock.config.neighborRadius) {
                    alignment += other.velocity;
                    alignCount++;
                }
            }
            if (alignCount > 0) alignment = alignment / static_cast<float>(alignCount) - creature.velocity;

            // Cohesion
            glm::vec3 cohesion{0.0f};
            int cohCount = 0;
            for (const auto& other : flock.creatures) {
                if (&other == &creature) continue;
                if (glm::distance(creature.position, other.position) < flock.config.neighborRadius) {
                    cohesion += other.position;
                    cohCount++;
                }
            }
            if (cohCount > 0) {
                cohesion = cohesion / static_cast<float>(cohCount) - creature.position;
                if (glm::dot(cohesion, cohesion) > 0.01f) cohesion = glm::normalize(cohesion);
            }

            // Destination steer
            glm::vec3 destSteer{0.0f};
            glm::vec3 toDest = flock.destination - creature.position;
            if (glm::length(toDest) > 5.0f) destSteer = glm::normalize(toDest);

            // Bounds avoidance
            glm::vec3 boundsAvoid{0.0f};
            if (flock.hasBounds) {
                const float margin = 30.0f;
                if (creature.position.x < flock.boundsMin.x + margin)
                    boundsAvoid.x = (flock.boundsMin.x + margin - creature.position.x) / margin;
                else if (creature.position.x > flock.boundsMax.x - margin)
                    boundsAvoid.x = (flock.boundsMax.x - margin - creature.position.x) / margin;
                if (creature.position.y < flock.boundsMin.y + margin)
                    boundsAvoid.y = (flock.boundsMin.y + margin - creature.position.y) / margin;
                else if (creature.position.y > flock.boundsMax.y - margin)
                    boundsAvoid.y = (flock.boundsMax.y - margin - creature.position.y) / margin;
            }
            const float heightMargin = 5.0f;
            if (creature.position.z < flock.config.heightMin + heightMargin) {
                float depth = (flock.config.heightMin + heightMargin) - creature.position.z;
                boundsAvoid.z = 2.0f + depth * 0.5f;
            } else if (creature.position.z > flock.config.heightMax - heightMargin) {
                float excess = creature.position.z - (flock.config.heightMax - heightMargin);
                boundsAvoid.z = -1.0f - excess * 0.3f;
            }

            // Player avoidance
            glm::vec3 playerAvoid{0.0f};
            glm::vec3 toPlayer = creature.position - playerPos;
            float playerDist = glm::length(toPlayer);
            if (playerDist < 15.0f && playerDist > 0.001f) {
                playerAvoid = glm::normalize(toPlayer) * (15.0f - playerDist) / 15.0f;
            }

            // Terrain avoidance via HCMap
            glm::vec3 terrainAvoid{0.0f};
            if (zoneData_.hcMap && glm::length(creature.velocity) > 0.1f) {
                glm::vec3 dir = glm::normalize(creature.velocity);
                float lookAhead = 15.0f;
                glm::vec3 futurePos = creature.position + dir * lookAhead;
                glm::vec3 startAbove = futurePos;
                startAbove.z = creature.position.z + 10.0f;
                glm::vec3 groundResult;
                if (zoneData_.hcMap->FindBestZ(startAbove, &groundResult) != BEST_Z_INVALID) {
                    float groundZ = groundResult.z;
                    float clearance = creature.position.z - groundZ;
                    if (clearance < 5.0f) {
                        float urgency = 1.0f - (clearance / 5.0f);
                        terrainAvoid.z += urgency * 3.0f;
                        terrainAvoid -= dir * urgency * 1.5f;
                    }
                }
            }

            // Combine forces
            glm::vec3 accel =
                separation * flock.config.behavior.separation +
                alignment * flock.config.behavior.alignment +
                cohesion * flock.config.behavior.cohesion +
                destSteer * flock.config.behavior.destination +
                boundsAvoid * flock.config.behavior.avoidance +
                playerAvoid * flock.config.behavior.playerAvoidance +
                terrainAvoid * flock.config.behavior.avoidance;

            // Scatter force
            if (flock.scattering) {
                glm::vec3 away = creature.position - flock.scatterSource;
                if (glm::dot(away, away) > 0.01f)
                    accel += glm::normalize(away) * flock.scatterStrength * 20.0f;
            }

            // Apply acceleration and clamp velocity
            creature.velocity += accel * dt;
            float speed = glm::length(creature.velocity);
            if (speed < 0.001f) {
                creature.velocity = glm::normalize(glm::vec3(
                    boidsRandomFloat(-1.0f, 1.0f),
                    boidsRandomFloat(-1.0f, 1.0f),
                    boidsRandomFloat(-0.2f, 0.2f))) * flock.config.minSpeed;
            } else if (speed < flock.config.minSpeed) {
                creature.velocity = glm::normalize(creature.velocity) * flock.config.minSpeed;
            } else if (speed > flock.config.maxSpeed) {
                creature.velocity = glm::normalize(creature.velocity) * flock.config.maxSpeed;
            }
            float maxVert = flock.config.maxSpeed * 0.5f;
            creature.velocity.z = glm::clamp(creature.velocity.z, -maxVert, maxVert);

            // Update position
            creature.position += creature.velocity * dt;

            // Hard clamp Z
            float minZ = playerPos.z + flock.config.heightMin;
            float maxZ = playerPos.z + flock.config.heightMax;
            if (creature.position.z < minZ) {
                creature.position.z = minZ;
                if (creature.velocity.z < 0.0f) creature.velocity.z = 0.0f;
            }
            if (creature.position.z > maxZ) {
                creature.position.z = maxZ;
                if (creature.velocity.z > 0.0f) creature.velocity.z = 0.0f;
            }

            // Animation
            uint8_t baseIndex = Environment::CreatureAtlas::getBaseIndex(flock.config.type);
            if (creature.velocity.z > 1.0f) {
                creature.animFrame += creature.animSpeed * dt;
                creature.textureIndex = baseIndex + (static_cast<int>(creature.animFrame) % 2);
            } else {
                creature.animFrame = 0.0f;
                creature.textureIndex = baseIndex;
            }
        }

        // Update flock center
        if (!flock.creatures.empty()) {
            glm::vec3 sum{0.0f};
            for (const auto& c : flock.creatures) sum += c.position;
            flock.center = sum / static_cast<float>(flock.creatures.size());
        }

        // Check bounds exit
        if (flock.hasBounds) {
            const float margin = 50.0f;
            if (flock.center.x < flock.boundsMin.x - margin || flock.center.x > flock.boundsMax.x + margin ||
                flock.center.y < flock.boundsMin.y - margin || flock.center.y > flock.boundsMax.y + margin) {
                flock.exitedBounds = true;
            }
        }
    }

    // Check player proximity for scatter
    for (auto& flock : boidsFlocks_) {
        float d = glm::distance(flock.center, playerPos);
        if (d < boidsScatterRadius_) {
            float strength = 1.0f - (d / boidsScatterRadius_);
            flock.scattering = true;
            flock.scatterSource = playerPos;
            flock.scatterStrength = glm::clamp(strength, 0.0f, 1.0f);
            flock.scatterTimer = 3.0f;
        }
    }

    // Remove expired flocks
    const float maxLifetime = 300.0f;
    boidsFlocks_.erase(
        std::remove_if(boidsFlocks_.begin(), boidsFlocks_.end(),
            [maxLifetime](const WorkerFlockState& f) {
                return f.exitedBounds || f.timeAlive > maxLifetime;
            }),
        boidsFlocks_.end());

    // Build output
    output.boidsOutput.creatures.clear();
    int totalCreatures = 0;
    for (const auto& flock : boidsFlocks_) {
        if (!boidsTypeEnabled_[static_cast<size_t>(flock.config.type)]) continue;
        for (const auto& c : flock.creatures) {
            // Distance culling
            glm::vec3 diff = c.position - playerPos;
            if (glm::dot(diff, diff) > budget.cullDistance * budget.cullDistance) continue;

            SimulationOutput::BoidsOutput::CreatureRender cr;
            cr.position = c.position;
            cr.size = c.size;
            cr.textureIndex = c.textureIndex;
            cr.alpha = c.alpha;
            output.boidsOutput.creatures.push_back(cr);
            totalCreatures++;
        }
    }
    output.boidsOutput.activeCount = totalCreatures;
    output.boidsOutput.valid = true;
}

// ============================================================================
// Tumbleweed Simulation (worker thread)
// ============================================================================

float SimulationWorker::twRandomFloat(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(twRng_);
}

void SimulationWorker::computeTumbleweeds(const SimulationInput& input, SimulationOutput& output) {
    const auto& ti = input.tumbleweedInput;
    if (!ti.initialized) {
        output.tumbleweedOutput.valid = false;
        return;
    }

    // Process commands
    for (const auto& cmd : ti.commands) {
        switch (cmd.type) {
            case TumbleweedCommand::ZoneEnter:
                // Despawn all active
                for (auto& tw : twInstances_) tw.active = false;
                twZoneBiome_ = cmd.zoneBiome;
                twSpawnTimer_ = 0.0f;
                break;
            case TumbleweedCommand::ZoneLeave:
                for (auto& tw : twInstances_) tw.active = false;
                twZoneBiome_ = 0;
                break;
            case TumbleweedCommand::SetEnabled:
                twEnabled_ = cmd.enabled;
                if (!twEnabled_) {
                    for (auto& tw : twInstances_) tw.active = false;
                }
                break;
        }
    }

    output.tumbleweedOutput.spawns.clear();
    output.tumbleweedOutput.despawns.clear();

    if (!twEnabled_) {
        output.tumbleweedOutput.tumbleweeds.clear();
        output.tumbleweedOutput.activeCount = 0;
        output.tumbleweedOutput.valid = true;
        return;
    }

    // Only active in desert/plains with wind
    auto biome = static_cast<Environment::ZoneBiome>(twZoneBiome_);
    if (biome != Environment::ZoneBiome::Desert && biome != Environment::ZoneBiome::Plains) {
        output.tumbleweedOutput.tumbleweeds.clear();
        output.tumbleweedOutput.activeCount = 0;
        output.tumbleweedOutput.valid = true;
        return;
    }

    float dt = ti.deltaTime;
    glm::vec3 playerPos = ti.playerPosition;
    glm::vec3 windDir = ti.windDirection;
    float windStrength = weatherWindIntensity_;

    // Spawn logic (only if wind is blowing)
    if (windStrength >= 0.1f) {
        twSpawnTimer_ += dt;
        if (twSpawnTimer_ >= twSpawnCooldown_) {
            twSpawnTimer_ = 0.0f;

            // Count active
            int activeCount = 0;
            for (const auto& tw : twInstances_) if (tw.active) activeCount++;

            if (activeCount < twMaxActive_) {
                // Select spawn position upwind
                glm::vec3 spawnDir = -windDir;
                if (glm::length(spawnDir) < 0.01f) {
                    float angle = twRandomFloat(0.0f, 6.28f);
                    spawnDir = glm::vec3(std::cos(angle), std::sin(angle), 0.0f);
                }
                float angle = twRandomFloat(-1.05f, 1.05f);
                float cosA = std::cos(angle), sinA = std::sin(angle);
                spawnDir = glm::normalize(glm::vec3(
                    spawnDir.x * cosA - spawnDir.y * sinA,
                    spawnDir.x * sinA + spawnDir.y * cosA, 0.0f));

                glm::vec3 spawnPos = playerPos + spawnDir * twSpawnDistance_;

                // Get ground height via HCMap
                float groundZ = playerPos.z;  // Fallback
                if (zoneData_.hcMap) {
                    glm::vec3 startAbove = spawnPos;
                    startAbove.z = playerPos.z + 50.0f;
                    glm::vec3 groundResult;
                    if (zoneData_.hcMap->FindBestZ(startAbove, &groundResult) != BEST_Z_INVALID) {
                        groundZ = groundResult.z;
                    }
                }
                spawnPos.z = groundZ;

                // Validate (reasonable height)
                if (groundZ > -1000.0f && groundZ < 1000.0f) {
                    WorkerTumbleweedInstance tw;
                    tw.active = true;
                    tw.position = spawnPos;
                    tw.lifetime = 0.0f;
                    tw.bounceCount = 0;
                    tw.size = twRandomFloat(twSizeMin_, twSizeMax_);
                    tw.radius = tw.size * 0.5f;
                    tw.velocity = windDir * twRandomFloat(twMinSpeed_, twMaxSpeed_);
                    tw.velocity.z = 0.0f;
                    tw.rotation = glm::vec3(
                        twRandomFloat(0.0f, 360.0f),
                        twRandomFloat(0.0f, 360.0f),
                        twRandomFloat(0.0f, 360.0f));
                    tw.angularVelocity = glm::vec3(0.0f);
                    tw.poolIndex = twNextPoolIndex_++;

                    twInstances_.push_back(tw);

                    // Emit spawn event
                    output.tumbleweedOutput.spawns.push_back({tw.poolIndex, tw.size});
                }
            }
        }
    }

    // Update each instance
    for (auto& tw : twInstances_) {
        if (!tw.active) continue;

        // Wind force
        glm::vec3 windForce = windDir * windStrength * twWindInfluence_;
        tw.velocity += windForce * dt;

        // Drag
        float dragFactor = 1.0f - 0.3f * dt;
        tw.velocity.x *= dragFactor;
        tw.velocity.y *= dragFactor;

        // Clamp speed
        float speed = glm::length(glm::vec2(tw.velocity.x, tw.velocity.y));
        if (speed > twMaxSpeed_) {
            float scale = twMaxSpeed_ / speed;
            tw.velocity.x *= scale;
            tw.velocity.y *= scale;
            speed = twMaxSpeed_;
        }
        if (speed < twMinSpeed_ && windStrength > 0.2f) {
            if (speed > 0.01f) {
                float scale = twMinSpeed_ / speed;
                tw.velocity.x *= scale;
                tw.velocity.y *= scale;
            } else {
                tw.velocity = windDir * twMinSpeed_;
                tw.velocity.z = 0.0f;
            }
            speed = twMinSpeed_;
        }

        // Predict next position
        glm::vec3 nextPos = tw.position + tw.velocity * dt;

        // Ground following via HCMap
        float groundZ = tw.position.z;
        if (zoneData_.hcMap) {
            glm::vec3 startAbove = nextPos;
            startAbove.z = playerPos.z + 50.0f;
            glm::vec3 groundResult;
            if (zoneData_.hcMap->FindBestZ(startAbove, &groundResult) != BEST_Z_INVALID) {
                groundZ = groundResult.z;
            }
        }
        float targetZ = groundZ + tw.radius + twGroundOffset_;
        float zDiff = targetZ - nextPos.z;
        if (std::abs(zDiff) > 0.1f) {
            if (zDiff < -2.0f) {
                tw.velocity.z -= 10.0f * dt;
            } else if (zDiff > 2.0f) {
                // Hit a rise, bounce
                glm::vec3 moveDir = glm::normalize(glm::vec3(tw.velocity.x, tw.velocity.y, 0.0f));
                glm::vec3 normal = -moveDir;
                normal.z = 0.3f;
                normal = glm::normalize(normal);
                float dot = glm::dot(tw.velocity, normal);
                tw.velocity = tw.velocity - 2.0f * dot * normal;
                tw.velocity *= twBounceDecay_;
                tw.velocity.x += twRandomFloat(-0.5f, 0.5f);
                tw.velocity.y += twRandomFloat(-0.5f, 0.5f);
                tw.bounceCount++;
                continue;  // Skip position update this frame
            }
        }
        nextPos.z = targetZ;

        // Wall collision via HCMap::CheckLOS
        bool wallHit = false;
        if (zoneData_.hcMap) {
            glm::vec3 hitPoint;
            if (!zoneData_.hcMap->CheckLOSWithHit(tw.position, nextPos, &hitPoint)) {
                // LOS blocked (returns false) = wall hit
                wallHit = true;
                glm::vec3 moveDir = glm::normalize(nextPos - tw.position);
                glm::vec3 normal = -moveDir;
                normal.z = std::min(normal.z, 0.3f);
                if (glm::length(normal) > 0.01f) normal = glm::normalize(normal);
                else normal = glm::vec3(1.0f, 0.0f, 0.0f);

                float dot = glm::dot(tw.velocity, normal);
                tw.velocity = tw.velocity - 2.0f * dot * normal;
                tw.velocity *= twBounceDecay_;
                tw.velocity.x += twRandomFloat(-0.5f, 0.5f);
                tw.velocity.y += twRandomFloat(-0.5f, 0.5f);
                tw.bounceCount++;
                tw.position = hitPoint + normal * (tw.radius + 0.2f);
            }
        }

        if (!wallHit) {
            tw.position = nextPos;
        }

        // Update rotation
        if (speed > 0.1f) {
            float rollSpeed = speed / tw.radius * 57.3f;
            glm::vec2 moveDir2 = glm::normalize(glm::vec2(tw.velocity.x, tw.velocity.y));
            tw.angularVelocity = glm::vec3(
                rollSpeed * moveDir2.y,
                -rollSpeed * moveDir2.x,
                twRandomFloat(-10.0f, 10.0f));
        }
        tw.rotation += tw.angularVelocity * dt;
        tw.rotation.x = std::fmod(tw.rotation.x, 360.0f);
        tw.rotation.y = std::fmod(tw.rotation.y, 360.0f);
        tw.rotation.z = std::fmod(tw.rotation.z, 360.0f);

        // Lifetime
        tw.lifetime += dt;

        // Despawn checks
        bool shouldDespawn = false;
        if (glm::distance(tw.position, playerPos) > twDespawnDistance_) shouldDespawn = true;
        if (tw.lifetime > twMaxLifetime_) shouldDespawn = true;
        if (tw.bounceCount > static_cast<uint32_t>(twMaxBounces_)) shouldDespawn = true;

        if (shouldDespawn) {
            tw.active = false;
            output.tumbleweedOutput.despawns.push_back({tw.poolIndex});
        }
    }

    // Remove inactive from vector
    twInstances_.erase(
        std::remove_if(twInstances_.begin(), twInstances_.end(),
            [](const WorkerTumbleweedInstance& tw) { return !tw.active; }),
        twInstances_.end());

    // Build output
    output.tumbleweedOutput.tumbleweeds.clear();
    int activeCount = 0;
    for (const auto& tw : twInstances_) {
        if (!tw.active) continue;
        SimulationOutput::TumbleweedOutput::TumbleweedRender tr;
        tr.position = tw.position;
        tr.rotation = tw.rotation;
        tr.size = tw.size;
        tr.active = true;
        tr.poolIndex = tw.poolIndex;
        output.tumbleweedOutput.tumbleweeds.push_back(tr);
        activeCount++;
    }
    output.tumbleweedOutput.activeCount = activeCount;
    output.tumbleweedOutput.valid = true;
}

} // namespace Graphics
} // namespace EQT
