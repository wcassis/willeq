#include "client/graphics/environment/flock_controller.h"
#include "client/graphics/detail/surface_map.h"
#include <irrlicht.h>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <sstream>

namespace EQT {
namespace Graphics {
namespace Environment {

FlockController::FlockController(const FlockConfig& config, const glm::vec3& spawnPosition, std::mt19937& rng)
    : config_(config)
    , rng_(rng)
{
    initializeCreatures(spawnPosition);
    anchor_ = spawnPosition;      // Store spawn position as anchor for patrolling
    destination_ = spawnPosition;
    center_ = spawnPosition;
}

void FlockController::initializeCreatures(const glm::vec3& spawnPosition) {
    // Determine flock size
    std::uniform_int_distribution<int> sizeDist(config_.minSize, config_.maxSize);
    int flockSize = sizeDist(rng_);

    creatures_.reserve(flockSize);

    for (int i = 0; i < flockSize; ++i) {
        Creature c;

        // Spread initial positions around spawn point
        c.position = spawnPosition + glm::vec3(
            randomFloat(-5.0f, 5.0f),
            randomFloat(-5.0f, 5.0f),
            randomFloat(-2.0f, 2.0f)
        );

        // Random initial velocity
        glm::vec3 dir = glm::normalize(glm::vec3(
            randomFloat(-1.0f, 1.0f),
            randomFloat(-1.0f, 1.0f),
            randomFloat(-0.3f, 0.3f)
        ));
        c.speed = randomFloat(config_.minSpeed, config_.maxSpeed);
        c.velocity = dir * c.speed;

        // Visual properties
        c.size = randomFloat(0.8f, 1.2f);  // Slight size variation
        c.textureIndex = CreatureAtlas::getBaseIndex(config_.type);
        c.animFrame = randomFloat(0.0f, 2.0f);  // Desync animations
        c.animSpeed = randomFloat(config_.animSpeedMin, config_.animSpeedMax);
        c.color = glm::vec4(1.0f);
        c.alpha = 1.0f;

        creatures_.push_back(c);
    }
}

void FlockController::update(float deltaTime, const EnvironmentState& env) {
    timeAlive_ += deltaTime;

    // Handle scattering
    if (scattering_) {
        scatterTimer_ -= deltaTime;
        if (scatterTimer_ <= 0.0f) {
            scattering_ = false;
        }
    }

    // Update destination waypoint (needs player Z for height calculation)
    updateDestination(deltaTime, env.playerPosition.z);

    // Apply Boids algorithm to each creature
    for (auto& creature : creatures_) {
        // Calculate steering forces
        glm::vec3 separation = calculateSeparation(creature);
        glm::vec3 alignment = calculateAlignment(creature);
        glm::vec3 cohesion = calculateCohesion(creature);
        glm::vec3 destination = calculateDestinationSteer(creature);
        glm::vec3 avoidance = calculateBoundsAvoidance(creature);
        glm::vec3 playerAvoid = calculatePlayerAvoidance(creature, env.playerPosition);
        glm::vec3 terrainAvoid = calculateTerrainAvoidance(creature);

        // Combine forces with weights
        // Terrain avoidance uses strong weight (same as bounds avoidance)
        glm::vec3 acceleration =
            separation * config_.behavior.separation +
            alignment * config_.behavior.alignment +
            cohesion * config_.behavior.cohesion +
            destination * config_.behavior.destination +
            avoidance * config_.behavior.avoidance +
            playerAvoid * config_.behavior.playerAvoidance +
            terrainAvoid * config_.behavior.avoidance;

        // If scattering, add extra force away from scatter source
        if (scattering_) {
            glm::vec3 awayFromScatter = creature.position - scatterSource_;
            if (glm::length2(awayFromScatter) > 0.01f) {
                awayFromScatter = glm::normalize(awayFromScatter) * scatterStrength_ * 20.0f;
                acceleration += awayFromScatter;
            }
        }

        // Apply acceleration
        creature.velocity += acceleration * deltaTime;

        // Clamp velocity
        clampVelocity(creature);

        // Update position
        creature.position += creature.velocity * deltaTime;

        // Hard clamp Z position - creatures should stay above player
        // heightMin/heightMax are offsets above player position
        float minZ = env.playerPosition.z + config_.heightMin;
        float maxZ = env.playerPosition.z + config_.heightMax;
        if (creature.position.z < minZ) {
            creature.position.z = minZ;
            // Also zero out downward velocity
            if (creature.velocity.z < 0.0f) {
                creature.velocity.z = 0.0f;
            }
        }
        if (creature.position.z > maxZ) {
            creature.position.z = maxZ;
            if (creature.velocity.z > 0.0f) {
                creature.velocity.z = 0.0f;
            }
        }

        // Update animation - flap when climbing, glide when level or descending
        // This creates more natural bird movement
        const float climbThreshold = 1.0f;  // Minimum upward velocity to trigger flapping
        uint8_t baseIndex = CreatureAtlas::getBaseIndex(config_.type);

        if (creature.velocity.z > climbThreshold) {
            // Climbing - flap wings (animate between frames 0 and 1)
            creature.animFrame += creature.animSpeed * deltaTime;
            creature.textureIndex = baseIndex + creature.getCurrentFrame();
        } else {
            // Level or descending - glide (wings spread, frame 0)
            creature.animFrame = 0.0f;  // Reset animation
            creature.textureIndex = baseIndex;  // Wings-up/spread pose
        }
    }

    // Update flock center
    updateCenter();

    // Check if flock has exited bounds
    if (hasBounds_) {
        const float margin = 50.0f;
        if (center_.x < boundsMin_.x - margin || center_.x > boundsMax_.x + margin ||
            center_.y < boundsMin_.y - margin || center_.y > boundsMax_.y + margin) {
            exitedBounds_ = true;
        }
    }
}

glm::vec3 FlockController::calculateSeparation(const Creature& creature) const {
    glm::vec3 steer{0.0f};
    int count = 0;

    for (const auto& other : creatures_) {
        if (&other == &creature) continue;

        float dist = glm::distance(creature.position, other.position);
        if (dist < config_.separationRadius && dist > 0.001f) {
            // Steer away from neighbor
            glm::vec3 diff = creature.position - other.position;
            diff = glm::normalize(diff) / dist;  // Weight by inverse distance
            steer += diff;
            count++;
        }
    }

    if (count > 0) {
        steer /= static_cast<float>(count);
    }

    return steer;
}

glm::vec3 FlockController::calculateAlignment(const Creature& creature) const {
    glm::vec3 avgVelocity{0.0f};
    int count = 0;

    for (const auto& other : creatures_) {
        if (&other == &creature) continue;

        float dist = glm::distance(creature.position, other.position);
        if (dist < config_.neighborRadius) {
            avgVelocity += other.velocity;
            count++;
        }
    }

    if (count > 0) {
        avgVelocity /= static_cast<float>(count);
        // Steer toward average velocity
        glm::vec3 steer = avgVelocity - creature.velocity;
        return steer;
    }

    return glm::vec3{0.0f};
}

glm::vec3 FlockController::calculateCohesion(const Creature& creature) const {
    glm::vec3 avgPosition{0.0f};
    int count = 0;

    for (const auto& other : creatures_) {
        if (&other == &creature) continue;

        float dist = glm::distance(creature.position, other.position);
        if (dist < config_.neighborRadius) {
            avgPosition += other.position;
            count++;
        }
    }

    if (count > 0) {
        avgPosition /= static_cast<float>(count);
        // Steer toward average position
        glm::vec3 desired = avgPosition - creature.position;
        if (glm::length2(desired) > 0.01f) {
            desired = glm::normalize(desired);
        }
        return desired;
    }

    return glm::vec3{0.0f};
}

glm::vec3 FlockController::calculateDestinationSteer(const Creature& creature) const {
    glm::vec3 toDestination = destination_ - creature.position;
    float dist = glm::length(toDestination);

    if (dist < 5.0f) {
        // Close enough, no need to steer
        return glm::vec3{0.0f};
    }

    // Steer toward destination
    glm::vec3 desired = glm::normalize(toDestination);
    return desired;
}

glm::vec3 FlockController::calculateBoundsAvoidance(const Creature& creature) const {
    glm::vec3 steer{0.0f};

    // X/Y bounds (only if bounds are set)
    if (hasBounds_) {
        const float margin = 30.0f;

        if (creature.position.x < boundsMin_.x + margin) {
            steer.x = (boundsMin_.x + margin - creature.position.x) / margin;
        } else if (creature.position.x > boundsMax_.x - margin) {
            steer.x = (boundsMax_.x - margin - creature.position.x) / margin;
        }

        if (creature.position.y < boundsMin_.y + margin) {
            steer.y = (boundsMin_.y + margin - creature.position.y) / margin;
        } else if (creature.position.y > boundsMax_.y - margin) {
            steer.y = (boundsMax_.y - margin - creature.position.y) / margin;
        }
    }

    // Z bounds (height limits) - ALWAYS enforce, more aggressively
    // Strong upward force when getting too low
    const float heightMargin = 5.0f;
    if (creature.position.z < config_.heightMin + heightMargin) {
        // Proportional force - stronger the lower we go
        float depth = (config_.heightMin + heightMargin) - creature.position.z;
        steer.z = 2.0f + depth * 0.5f;  // Strong upward force
    } else if (creature.position.z > config_.heightMax - heightMargin) {
        float excess = creature.position.z - (config_.heightMax - heightMargin);
        steer.z = -1.0f - excess * 0.3f;  // Downward force
    }

    return steer;
}

glm::vec3 FlockController::calculatePlayerAvoidance(const Creature& creature, const glm::vec3& playerPos) const {
    glm::vec3 toPlayer = creature.position - playerPos;
    float dist = glm::length(toPlayer);

    const float avoidDist = 15.0f;
    if (dist < avoidDist && dist > 0.001f) {
        // Steer away from player
        return glm::normalize(toPlayer) * (avoidDist - dist) / avoidDist;
    }

    return glm::vec3{0.0f};
}

void FlockController::updateCenter() {
    if (creatures_.empty()) {
        center_ = glm::vec3{0.0f};
        return;
    }

    glm::vec3 sum{0.0f};
    for (const auto& c : creatures_) {
        sum += c.position;
    }
    center_ = sum / static_cast<float>(creatures_.size());
}

void FlockController::updateDestination(float deltaTime, float playerZ) {
    destinationTimer_ -= deltaTime;

    if (destinationTimer_ <= 0.0f) {
        // Pick a new destination within patrol area around anchor point
        if (hasBounds_) {
            destination_ = glm::vec3(
                randomFloat(boundsMin_.x + 50.0f, boundsMax_.x - 50.0f),
                randomFloat(boundsMin_.y + 50.0f, boundsMax_.y - 50.0f),
                playerZ + randomFloat(config_.heightMin, config_.heightMax)
            );
        } else {
            // Patrol around anchor point (where flock was spawned)
            // This keeps flocks in the area rather than flying off forever
            destination_ = anchor_ + glm::vec3(
                randomFloat(-config_.patrolRadius, config_.patrolRadius),
                randomFloat(-config_.patrolRadius, config_.patrolRadius),
                randomFloat(-10.0f, 10.0f)
            );
            // Also ensure destination Z is above player
            destination_.z = std::max(destination_.z, playerZ + config_.heightMin);
        }

        // Clamp height relative to player
        float minZ = playerZ + config_.heightMin;
        float maxZ = playerZ + config_.heightMax;
        destination_.z = glm::clamp(destination_.z, minZ, maxZ);

        destinationTimer_ = destinationInterval_;
    }
}

void FlockController::clampVelocity(Creature& creature) {
    float speed = glm::length(creature.velocity);

    if (speed < 0.001f) {
        // Give some minimum velocity in a random direction
        creature.velocity = glm::vec3(
            randomFloat(-1.0f, 1.0f),
            randomFloat(-1.0f, 1.0f),
            randomFloat(-0.2f, 0.2f)
        );
        creature.velocity = glm::normalize(creature.velocity) * config_.minSpeed;
        return;
    }

    // Clamp to min/max speed
    if (speed < config_.minSpeed) {
        creature.velocity = glm::normalize(creature.velocity) * config_.minSpeed;
    } else if (speed > config_.maxSpeed) {
        creature.velocity = glm::normalize(creature.velocity) * config_.maxSpeed;
    }

    // Also clamp vertical velocity to prevent too steep climbs/dives
    const float maxVertical = config_.maxSpeed * 0.5f;
    creature.velocity.z = glm::clamp(creature.velocity.z, -maxVertical, maxVertical);
}

float FlockController::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
}

void FlockController::setZoneBounds(const glm::vec3& min, const glm::vec3& max) {
    boundsMin_ = min;
    boundsMax_ = max;
    hasBounds_ = true;
}

void FlockController::setDestination(const glm::vec3& destination) {
    destination_ = destination;
    destinationTimer_ = destinationInterval_;
}

void FlockController::scatter(const glm::vec3& sourcePosition, float strength) {
    scattering_ = true;
    scatterSource_ = sourcePosition;
    scatterStrength_ = glm::clamp(strength, 0.0f, 1.0f);
    scatterTimer_ = 3.0f;  // Scatter for 3 seconds
}

void FlockController::setCollisionDetection(irr::scene::ISceneManager* smgr,
                                             irr::scene::ITriangleSelector* selector,
                                             const Detail::SurfaceMap* surfaceMap,
                                             const std::vector<PlaceableBounds>* placeables) {
    smgr_ = smgr;
    collisionSelector_ = selector;
    surfaceMap_ = surfaceMap;
    placeableObjects_ = placeables;
}

glm::vec3 FlockController::calculateTerrainAvoidance(const Creature& creature) const {
    glm::vec3 steer{0.0f};

    // Skip if no collision detection is set up
    if (!smgr_ || !collisionSelector_) {
        return steer;
    }

    // Look ahead in the direction of movement
    float speed = glm::length(creature.velocity);
    if (speed < 0.1f) {
        return steer;
    }

    glm::vec3 direction = glm::normalize(creature.velocity);
    float lookAhead = 15.0f;  // How far ahead to check for obstacles

    // Cast multiple rays: forward, forward-down, forward-left, forward-right
    struct RayCheck {
        glm::vec3 offset;
        float weight;
    };

    std::vector<RayCheck> rays = {
        {{0.0f, 0.0f, 0.0f}, 1.0f},        // Straight ahead
        {{0.0f, 0.0f, -5.0f}, 0.8f},       // Forward-down
        {{-3.0f, 0.0f, 0.0f}, 0.5f},       // Forward-left (in XY plane)
        {{3.0f, 0.0f, 0.0f}, 0.5f},        // Forward-right
    };

    for (const auto& ray : rays) {
        // Calculate ray end point
        glm::vec3 from = creature.position;
        glm::vec3 to = creature.position + direction * lookAhead + ray.offset;

        // Convert to Irrlicht coordinates (EQ: x,y,z -> Irr: x,z,y)
        irr::core::vector3df irrFrom(from.x, from.z, from.y);
        irr::core::vector3df irrTo(to.x, to.z, to.y);
        irr::core::line3df rayLine(irrFrom, irrTo);

        irr::core::vector3df hitPoint;
        irr::core::triangle3df hitTri;
        irr::scene::ISceneNode* hitNode = nullptr;

        if (smgr_->getSceneCollisionManager()->getCollisionPoint(
                rayLine, collisionSelector_, hitPoint, hitTri, hitNode)) {

            // Calculate distance to hit
            float hitDist = irrFrom.getDistanceFrom(hitPoint);
            float rayDist = irrFrom.getDistanceFrom(irrTo);

            if (hitDist < rayDist) {
                // Obstacle detected - steer away
                // Get normal in EQ coords
                irr::core::vector3df triNormal = hitTri.getNormal();
                glm::vec3 normal(triNormal.X, triNormal.Z, triNormal.Y);

                if (glm::length(normal) > 0.01f) {
                    normal = glm::normalize(normal);
                } else {
                    // Fallback: steer up and away from obstacle
                    normal = glm::vec3(0.0f, 0.0f, 1.0f);
                }

                // Stronger avoidance when closer
                float urgency = 1.0f - (hitDist / rayDist);
                steer += normal * urgency * ray.weight * 3.0f;

                // Also add upward component to fly over obstacles
                steer.z += urgency * ray.weight * 2.0f;
            }
        }
    }

    // Check placeable objects (AABB collision)
    if (placeableObjects_) {
        glm::vec3 futurePos = creature.position + direction * lookAhead;

        for (const auto& obj : *placeableObjects_) {
            // Expand AABB by a margin for early detection
            float margin = 5.0f;
            glm::vec3 expandedMin = obj.min - glm::vec3(margin);
            glm::vec3 expandedMax = obj.max + glm::vec3(margin);

            // Check if future position would be inside expanded AABB
            if (futurePos.x >= expandedMin.x && futurePos.x <= expandedMax.x &&
                futurePos.y >= expandedMin.y && futurePos.y <= expandedMax.y &&
                futurePos.z >= expandedMin.z && futurePos.z <= expandedMax.z) {

                // Calculate center and steer away
                glm::vec3 objCenter = (obj.min + obj.max) * 0.5f;
                glm::vec3 away = creature.position - objCenter;

                if (glm::length(away) > 0.01f) {
                    away = glm::normalize(away);
                } else {
                    away = glm::vec3(0.0f, 0.0f, 1.0f);
                }

                // Calculate how deep into the expanded area
                float penetration = 1.0f;
                steer += away * penetration * 2.0f;

                // Add upward component
                steer.z += 1.5f;
            }
        }
    }

    // Limit the magnitude
    float steerMag = glm::length(steer);
    if (steerMag > 5.0f) {
        steer = glm::normalize(steer) * 5.0f;
    }

    return steer;
}

std::string FlockController::getDebugInfo() const {
    std::ostringstream ss;
    ss << getCreatureTypeName(config_.type)
       << " flock: " << creatures_.size() << " creatures"
       << ", center: (" << center_.x << ", " << center_.y << ", " << center_.z << ")"
       << ", alive: " << timeAlive_ << "s";
    if (scattering_) {
        ss << " [SCATTERING]";
    }
    return ss.str();
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
