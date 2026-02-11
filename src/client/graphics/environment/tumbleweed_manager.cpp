#include "client/graphics/environment/tumbleweed_manager.h"
#include "client/graphics/environment/environment_config.h"
#include "common/logging.h"
#include <cmath>
#include <random>
#include <sstream>

namespace EQT {
namespace Graphics {
namespace Environment {

// Random number generation
static std::random_device rd;
static std::mt19937 gen(rd());

TumbleweedManager::TumbleweedManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
{
}

TumbleweedManager::~TumbleweedManager() {
    // Clean up scene nodes
    for (auto& tw : tumbleweeds_) {
        if (tw.node) {
            tw.node->remove();
            tw.node = nullptr;
        }
    }

    // Drop mesh
    if (tumbleweedMesh_) {
        tumbleweedMesh_->drop();
        tumbleweedMesh_ = nullptr;
    }

    // Texture is managed by driver, don't drop manually
    tumbleweedTexture_ = nullptr;
}

bool TumbleweedManager::init() {
    if (initialized_) {
        return true;
    }

    // Load settings
    reloadSettings();

    // Load tumbleweed texture from file
    std::string texPath = "data/textures/tumbleweed.png";
    tumbleweedTexture_ = driver_->getTexture(texPath.c_str());
    if (!tumbleweedTexture_) {
        LOG_WARN(MOD_GRAPHICS, "TumbleweedManager: Texture not found at {}, disabling tumbleweeds. Run generate_textures tool.", texPath);
        initialized_ = false;
        return false;
    }

    // Create tumbleweed mesh
    tumbleweedMesh_ = createTumbleweedMesh();
    if (!tumbleweedMesh_) {
        LOG_ERROR(MOD_GRAPHICS, "TumbleweedManager: Failed to create tumbleweed mesh");
        return false;
    }

    // Pre-allocate instance pool
    tumbleweeds_.resize(settings_.maxActive);
    for (auto& tw : tumbleweeds_) {
        tw.active = false;
        tw.node = nullptr;
    }

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "TumbleweedManager initialized (max={}, spawnRate={})",
             settings_.maxActive, settings_.spawnRate);
    return true;
}

void TumbleweedManager::reloadSettings() {
    // Copy settings from config (different struct type, so copy field-by-field)
    const auto& cfg = EnvironmentEffectsConfig::instance().getTumbleweeds();
    settings_.enabled = cfg.enabled;
    settings_.maxActive = cfg.maxActive;
    settings_.spawnRate = cfg.spawnRate;
    settings_.spawnDistance = cfg.spawnDistance;
    settings_.despawnDistance = cfg.despawnDistance;
    settings_.minSpeed = cfg.minSpeed;
    settings_.maxSpeed = cfg.maxSpeed;
    settings_.windInfluence = cfg.windInfluence;
    settings_.bounceDecay = cfg.bounceDecay;
    settings_.maxLifetime = cfg.maxLifetime;
    settings_.groundOffset = cfg.groundOffset;
    settings_.sizeMin = cfg.sizeMin;
    settings_.sizeMax = cfg.sizeMax;
    settings_.maxBounces = cfg.maxBounces;

    spawnCooldown_ = settings_.spawnRate > 0.0f ? (1.0f / settings_.spawnRate) : 10.0f;
}

void TumbleweedManager::update(float deltaTime) {
    if (!initialized_ || !isEnabled()) {
        return;
    }

    // Only active in desert/plains biomes with wind
    if (currentBiome_ != ZoneBiome::Desert && currentBiome_ != ZoneBiome::Plains) {
        return;
    }

    if (envState_.windStrength < 0.1f) {
        // No wind, tumbleweeds don't move much
        // Still update existing ones but don't spawn new
    } else {
        // Try to spawn new tumbleweeds
        spawnTimer_ += deltaTime;
        if (spawnTimer_ >= spawnCooldown_) {
            spawnTimer_ = 0.0f;
            trySpawn();
        }
    }

    // Update all active tumbleweeds
    for (auto& tw : tumbleweeds_) {
        if (!tw.active) continue;

        // Update physics
        updateTumbleweed(tw, deltaTime);

        // Update lifetime
        tw.lifetime += deltaTime;

        // Check despawn conditions
        bool shouldDespawn = false;

        // Distance from player
        float distToPlayer = glm::distance(tw.position, envState_.playerPosition);
        if (distToPlayer > settings_.despawnDistance) {
            shouldDespawn = true;
        }

        // Lifetime exceeded
        if (tw.lifetime > settings_.maxLifetime) {
            shouldDespawn = true;
        }

        // Too many bounces
        if (tw.bounceCount > static_cast<uint32_t>(settings_.maxBounces)) {
            shouldDespawn = true;
        }

        if (shouldDespawn) {
            despawnTumbleweed(tw);
        } else {
            // Update visuals
            updateVisuals(tw, deltaTime);
        }
    }
}

void TumbleweedManager::trySpawn() {
    // Check if we can spawn more
    if (getActiveCount() >= settings_.maxActive) {
        return;
    }

    // Select spawn position
    glm::vec3 spawnPos = selectSpawnPosition();
    if (spawnPos == glm::vec3(0.0f)) {
        return;  // Invalid position
    }

    spawnTumbleweed(spawnPos);
}

bool TumbleweedManager::spawnTumbleweed(const glm::vec3& position) {
    TumbleweedInstance* tw = getInactiveInstance();
    if (!tw) {
        return false;
    }

    // Initialize instance
    tw->active = true;
    tw->position = position;
    tw->lifetime = 0.0f;
    tw->distanceTraveled = 0.0f;
    tw->bounceCount = 0;

    // Random size
    tw->size = randomFloat(settings_.sizeMin, settings_.sizeMax);
    tw->radius = tw->size * 0.5f;

    // Initial velocity - start rolling with the wind
    tw->velocity = envState_.windDirection * randomFloat(settings_.minSpeed, settings_.maxSpeed);
    tw->velocity.z = 0.0f;  // No vertical velocity initially

    // Random initial rotation
    tw->rotation = glm::vec3(
        randomFloat(0.0f, 360.0f),
        randomFloat(0.0f, 360.0f),
        randomFloat(0.0f, 360.0f)
    );
    tw->angularVelocity = glm::vec3(0.0f);

    // Create scene node if needed
    if (!tw->node && tumbleweedMesh_) {
        tw->node = smgr_->addMeshSceneNode(tumbleweedMesh_);
        if (tw->node) {
            tw->node->setMaterialFlag(irr::video::EMF_LIGHTING, true);
            tw->node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
        }
    }

    if (tw->node) {
        tw->node->setVisible(true);
        tw->node->setScale(irr::core::vector3df(tw->size, tw->size, tw->size));
        // Set initial position
        tw->node->setPosition(irr::core::vector3df(
            tw->position.x, tw->position.z, tw->position.y
        ));
        LOG_DEBUG(MOD_GRAPHICS, "TumbleweedManager: Created node at Irr({:.1f}, {:.1f}, {:.1f})",
                  tw->position.x, tw->position.z, tw->position.y);
    } else {
        LOG_WARN(MOD_GRAPHICS, "TumbleweedManager: Failed to create scene node!");
    }

    LOG_DEBUG(MOD_GRAPHICS, "TumbleweedManager: Spawned at ({:.1f}, {:.1f}, {:.1f}) size={:.2f}",
              position.x, position.y, position.z, tw->size);

    return true;
}

glm::vec3 TumbleweedManager::selectSpawnPosition() {
    // Spawn upwind from player at edge of view distance
    glm::vec3 spawnDir = -envState_.windDirection;  // Opposite to wind direction
    if (glm::length(spawnDir) < 0.01f) {
        // No wind direction, pick random
        float angle = randomFloat(0.0f, 6.28f);
        spawnDir = glm::vec3(std::cos(angle), std::sin(angle), 0.0f);
    }

    // Add randomness to spawn angle (±60 degrees)
    float angle = randomFloat(-1.05f, 1.05f);
    float cosA = std::cos(angle), sinA = std::sin(angle);
    spawnDir = glm::vec3(
        spawnDir.x * cosA - spawnDir.y * sinA,
        spawnDir.x * sinA + spawnDir.y * cosA,
        0.0f
    );
    spawnDir = glm::normalize(spawnDir);

    // Position at spawn distance from player
    glm::vec3 spawnPos = envState_.playerPosition + spawnDir * settings_.spawnDistance;

    // Get ground height
    float groundZ = getGroundHeight(spawnPos.x, spawnPos.y);
    spawnPos.z = groundZ;

    // Validate spawn position
    if (!isValidSpawnPosition(spawnPos)) {
        // Try a few more positions
        for (int i = 0; i < 5; ++i) {
            angle = randomFloat(-3.14f, 3.14f);
            cosA = std::cos(angle);
            sinA = std::sin(angle);
            spawnDir = glm::vec3(cosA, sinA, 0.0f);
            spawnPos = envState_.playerPosition + spawnDir * settings_.spawnDistance;
            groundZ = getGroundHeight(spawnPos.x, spawnPos.y);
            spawnPos.z = groundZ;

            if (isValidSpawnPosition(spawnPos)) {
                return spawnPos;
            }
        }
        return glm::vec3(0.0f);  // Failed to find valid position
    }

    return spawnPos;
}

bool TumbleweedManager::isValidSpawnPosition(const glm::vec3& pos) {
    // Check surface type
    if (surfaceMap_) {
        auto type = surfaceMap_->getSurfaceType(pos.x, pos.y);
        if (type == Detail::SurfaceType::Water ||
            type == Detail::SurfaceType::Lava ||
            type == Detail::SurfaceType::Unknown) {
            return false;
        }
    }

    // Check ground height is reasonable (not underground or sky-high)
    float groundZ = getGroundHeight(pos.x, pos.y);
    if (groundZ < -1000.0f || groundZ > 1000.0f) {
        return false;  // Likely invalid
    }

    return true;
}

void TumbleweedManager::updateTumbleweed(TumbleweedInstance& tw, float deltaTime) {
    // 1. Apply wind force
    glm::vec3 windForce = envState_.windDirection * envState_.windStrength * settings_.windInfluence;
    tw.velocity += windForce * deltaTime;

    // 2. Apply friction/drag
    float dragFactor = 1.0f - 0.3f * deltaTime;
    tw.velocity.x *= dragFactor;
    tw.velocity.y *= dragFactor;

    // 3. Clamp speed
    float speed = glm::length(glm::vec2(tw.velocity.x, tw.velocity.y));
    if (speed > settings_.maxSpeed) {
        float scale = settings_.maxSpeed / speed;
        tw.velocity.x *= scale;
        tw.velocity.y *= scale;
        speed = settings_.maxSpeed;
    }

    // Minimum speed when wind is blowing
    if (speed < settings_.minSpeed && envState_.windStrength > 0.2f) {
        if (speed > 0.01f) {
            float scale = settings_.minSpeed / speed;
            tw.velocity.x *= scale;
            tw.velocity.y *= scale;
        } else {
            // No velocity, start with wind direction
            tw.velocity = envState_.windDirection * settings_.minSpeed;
            tw.velocity.z = 0.0f;
        }
        speed = settings_.minSpeed;
    }

    // 4. Predict next position
    glm::vec3 nextPos = tw.position + tw.velocity * deltaTime;

    // 5. Terrain following - get ground height at next position
    float groundZ = getGroundHeight(nextPos.x, nextPos.y);
    float targetZ = groundZ + tw.radius + settings_.groundOffset;

    // Smoothly adjust height to follow terrain
    float zDiff = targetZ - nextPos.z;
    if (std::abs(zDiff) > 0.1f) {
        // Steep change - could be a cliff or obstacle
        if (zDiff < -2.0f) {
            // Falling off something - add some downward velocity
            tw.velocity.z -= 10.0f * deltaTime;
        } else if (zDiff > 2.0f) {
            // Hit a rise - treat as collision
            TumbleweedCollision collision;
            collision.hit = true;
            collision.type = TumbleweedCollisionType::Geometry;
            collision.normal = glm::normalize(glm::vec3(
                tw.position.x - nextPos.x,
                tw.position.y - nextPos.y,
                0.5f
            ));
            handleCollision(tw, collision);
            return;
        }
    }
    nextPos.z = targetZ;

    // 6. Collision detection with obstacles
    TumbleweedCollision collision = checkCollisions(tw.position, nextPos, tw.radius);
    if (collision.hit) {
        handleCollision(tw, collision);
    } else {
        tw.position = nextPos;
    }

    // 7. Update visual rotation (roll effect)
    if (speed > 0.1f) {
        float rollSpeed = speed / tw.radius * 57.3f;  // Convert to degrees/sec
        glm::vec2 moveDir = glm::normalize(glm::vec2(tw.velocity.x, tw.velocity.y));

        // Roll perpendicular to movement direction
        tw.angularVelocity = glm::vec3(
            rollSpeed * moveDir.y,   // Roll around X based on Y movement
            -rollSpeed * moveDir.x,  // Roll around Y based on X movement
            randomFloat(-10.0f, 10.0f)  // Some random Z spin
        );
    }

    // 8. Update distance traveled
    tw.distanceTraveled += speed * deltaTime;
}

TumbleweedCollision TumbleweedManager::checkCollisions(const glm::vec3& from, const glm::vec3& to, float radius) {
    TumbleweedCollision result;

    // 1. Check zone geometry collision
    if (zoneCollisionSelector_ && smgr_) {
        // Convert to Irrlicht coords (EQ: x,y,z -> Irr: x,z,y)
        irr::core::vector3df irrFrom(from.x, from.z, from.y);
        irr::core::vector3df irrTo(to.x, to.z, to.y);
        irr::core::line3df ray(irrFrom, irrTo);

        irr::core::vector3df hitPoint;
        irr::core::triangle3df hitTri;
        irr::scene::ISceneNode* hitNode = nullptr;

        if (smgr_->getSceneCollisionManager()->getCollisionPoint(
                ray, zoneCollisionSelector_, hitPoint, hitTri, hitNode)) {
            // Check if hit point is close enough to matter
            float hitDist = irrFrom.getDistanceFrom(hitPoint);
            float moveDist = irrFrom.getDistanceFrom(irrTo);

            if (hitDist < moveDist + radius) {
                result.hit = true;
                result.point = glm::vec3(hitPoint.X, hitPoint.Z, hitPoint.Y);  // Back to EQ coords
                // Calculate normal from triangle
                irr::core::vector3df triNormal = hitTri.getNormal();
                result.normal = glm::vec3(triNormal.X, triNormal.Z, triNormal.Y);
                result.normal = glm::normalize(result.normal);
                result.type = TumbleweedCollisionType::Geometry;
                return result;
            }
        }
    }

    // 2. Check placeable object collisions
    for (const auto& obj : placeableObjects_) {
        // Simple AABB vs sphere test
        glm::vec3 closest;
        closest.x = std::max(obj.min.x, std::min(to.x, obj.max.x));
        closest.y = std::max(obj.min.y, std::min(to.y, obj.max.y));
        closest.z = std::max(obj.min.z, std::min(to.z, obj.max.z));

        float distSq = glm::dot(to - closest, to - closest);
        if (distSq < radius * radius) {
            result.hit = true;
            result.point = closest;
            // Normal points from object to tumbleweed
            glm::vec3 objCenter = (obj.min + obj.max) * 0.5f;
            result.normal = to - objCenter;
            result.normal.z = 0.0f;  // Keep bounce horizontal
            if (glm::length(result.normal) > 0.01f) {
                result.normal = glm::normalize(result.normal);
            } else {
                result.normal = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            result.type = TumbleweedCollisionType::Object;
            return result;
        }
    }

    // 3. Check water
    if (surfaceMap_) {
        auto surfType = surfaceMap_->getSurfaceType(to.x, to.y);
        if (surfType == Detail::SurfaceType::Water) {
            result.hit = true;
            result.type = TumbleweedCollisionType::Water;
            return result;
        }
    }

    return result;
}

void TumbleweedManager::handleCollision(TumbleweedInstance& tw, const TumbleweedCollision& collision) {
    if (collision.type == TumbleweedCollisionType::Water) {
        // Tumbleweed sinks in water - despawn
        LOG_DEBUG(MOD_GRAPHICS, "TumbleweedManager: Tumbleweed hit water, despawning");
        tw.lifetime = settings_.maxLifetime + 1.0f;  // Mark for despawn
        return;
    }

    // Reflect velocity off collision normal
    glm::vec3 normal = collision.normal;
    if (glm::length(normal) < 0.01f) {
        normal = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    // Ensure normal is mostly horizontal for bouncing
    normal.z = std::min(normal.z, 0.3f);
    normal = glm::normalize(normal);

    // Reflect: v' = v - 2(v.n)n
    float dot = glm::dot(tw.velocity, normal);
    tw.velocity = tw.velocity - 2.0f * dot * normal;

    // Apply bounce decay
    tw.velocity *= settings_.bounceDecay;

    // Add some randomness
    tw.velocity.x += randomFloat(-0.5f, 0.5f);
    tw.velocity.y += randomFloat(-0.5f, 0.5f);

    // Increment bounce count
    tw.bounceCount++;

    // Push slightly away from collision
    if (collision.point != glm::vec3(0.0f)) {
        tw.position = collision.point + normal * (tw.radius + 0.2f);
    }

    LOG_DEBUG(MOD_GRAPHICS, "TumbleweedManager: Bounce #{} type={}",
              tw.bounceCount, static_cast<int>(collision.type));
}

float TumbleweedManager::getGroundHeight(float x, float y) {
    // Use collision ray cast starting from player's approximate height
    // Surface map heights can be unreliable (may return ceiling heights)
    if (zoneCollisionSelector_ && smgr_) {
        // Start raycast from well above player's current Z position
        float startZ = envState_.playerPosition.z + 50.0f;

        // Irrlicht coords: (EQ_x, EQ_z, EQ_y)
        irr::core::vector3df start(x, startZ, y);
        irr::core::vector3df end(x, startZ - 200.0f, y);  // Cast down 200 units
        irr::core::line3df ray(start, end);

        irr::core::vector3df hitPoint;
        irr::core::triangle3df hitTri;
        irr::scene::ISceneNode* hitNode = nullptr;

        if (smgr_->getSceneCollisionManager()->getCollisionPoint(
                ray, zoneCollisionSelector_, hitPoint, hitTri, hitNode)) {
            return hitPoint.Y;  // Irrlicht Y is EQ Z
        }
    }

    // Fallback to player's Z if no collision found
    return envState_.playerPosition.z;
}

void TumbleweedManager::updateVisuals(TumbleweedInstance& tw, float deltaTime) {
    if (!tw.node) return;

    // Update position (EQ to Irrlicht coords)
    tw.node->setPosition(irr::core::vector3df(
        tw.position.x, tw.position.z, tw.position.y
    ));

    // Update rotation (accumulate angular velocity)
    tw.rotation += tw.angularVelocity * deltaTime;

    // Wrap rotation to 0-360
    tw.rotation.x = std::fmod(tw.rotation.x, 360.0f);
    tw.rotation.y = std::fmod(tw.rotation.y, 360.0f);
    tw.rotation.z = std::fmod(tw.rotation.z, 360.0f);

    tw.node->setRotation(irr::core::vector3df(
        tw.rotation.x, tw.rotation.y, tw.rotation.z
    ));
}

void TumbleweedManager::despawnTumbleweed(TumbleweedInstance& tw) {
    tw.active = false;
    if (tw.node) {
        tw.node->setVisible(false);
    }
    LOG_DEBUG(MOD_GRAPHICS, "TumbleweedManager: Despawned (lifetime={:.1f}s, dist={:.1f}, bounces={})",
              tw.lifetime, tw.distanceTraveled, tw.bounceCount);
}

irr::scene::IMesh* TumbleweedManager::createTumbleweedMesh() {
    // Create a simple tumbleweed mesh using crossed planes
    // Tan/brown color to look like dried plant material

    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    // Tumbleweed colors - tan/brown with variation
    irr::video::SColor colorLight(255, 180, 150, 100);   // Lighter tan
    irr::video::SColor colorDark(255, 140, 110, 70);     // Darker brown

    // Create 3 crossed quads for a sphere-like appearance
    const float size = 1.0f;  // Base size (scaled by instance size)
    const int numPlanes = 3;

    for (int p = 0; p < numPlanes; ++p) {
        float angle = (float)p * 3.14159f / numPlanes;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        // Quad vertices (rotated around Y axis)
        irr::core::vector3df v0(-size * cosA, -size, -size * sinA);
        irr::core::vector3df v1(size * cosA, -size, size * sinA);
        irr::core::vector3df v2(size * cosA, size, size * sinA);
        irr::core::vector3df v3(-size * cosA, size, -size * sinA);

        irr::core::vector3df normal(sinA, 0.0f, -cosA);

        irr::u32 baseIdx = buffer->Vertices.size();

        // Alternate colors between planes for visual variety
        irr::video::SColor c0 = (p % 2 == 0) ? colorLight : colorDark;
        irr::video::SColor c1 = (p % 2 == 0) ? colorDark : colorLight;

        buffer->Vertices.push_back(irr::video::S3DVertex(v0, normal, c0, irr::core::vector2df(0, 1)));
        buffer->Vertices.push_back(irr::video::S3DVertex(v1, normal, c1, irr::core::vector2df(1, 1)));
        buffer->Vertices.push_back(irr::video::S3DVertex(v2, normal, c0, irr::core::vector2df(1, 0)));
        buffer->Vertices.push_back(irr::video::S3DVertex(v3, normal, c1, irr::core::vector2df(0, 0)));

        // Two triangles for the quad (front face)
        buffer->Indices.push_back(baseIdx + 0);
        buffer->Indices.push_back(baseIdx + 1);
        buffer->Indices.push_back(baseIdx + 2);
        buffer->Indices.push_back(baseIdx + 0);
        buffer->Indices.push_back(baseIdx + 2);
        buffer->Indices.push_back(baseIdx + 3);

        // Back face
        buffer->Indices.push_back(baseIdx + 0);
        buffer->Indices.push_back(baseIdx + 2);
        buffer->Indices.push_back(baseIdx + 1);
        buffer->Indices.push_back(baseIdx + 0);
        buffer->Indices.push_back(baseIdx + 3);
        buffer->Indices.push_back(baseIdx + 2);
    }

    buffer->recalculateBoundingBox();

    // Set material
    buffer->Material.Lighting = false;  // Unlit to ensure visibility
    buffer->Material.BackfaceCulling = false;
    buffer->Material.AmbientColor = colorLight;
    buffer->Material.DiffuseColor = colorLight;

    // Apply texture if available
    if (tumbleweedTexture_) {
        buffer->Material.setTexture(0, tumbleweedTexture_);
        buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
        buffer->Material.MaterialTypeParam = 0.1f;  // Alpha ref threshold
    }

    // Create mesh
    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    mesh->addMeshBuffer(buffer);
    mesh->recalculateBoundingBox();
    buffer->drop();

    return mesh;
}

TumbleweedInstance* TumbleweedManager::getInactiveInstance() {
    for (auto& tw : tumbleweeds_) {
        if (!tw.active) {
            return &tw;
        }
    }
    return nullptr;
}

int TumbleweedManager::getActiveCount() const {
    int count = 0;
    for (const auto& tw : tumbleweeds_) {
        if (tw.active) {
            count++;
        }
    }
    return count;
}

std::string TumbleweedManager::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Tumbleweeds: " << getActiveCount() << "/" << settings_.maxActive;
    ss << " | Wind: " << envState_.windStrength;
    return ss.str();
}

void TumbleweedManager::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentZoneName_ = zoneName;
    currentBiome_ = biome;

    // Clear existing tumbleweeds
    for (auto& tw : tumbleweeds_) {
        if (tw.active) {
            despawnTumbleweed(tw);
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "TumbleweedManager: Zone '{}' biome={}", zoneName, static_cast<int>(biome));
}

void TumbleweedManager::onZoneLeave() {
    // Despawn all tumbleweeds
    for (auto& tw : tumbleweeds_) {
        if (tw.active) {
            despawnTumbleweed(tw);
        }
    }

    currentZoneName_.clear();
    currentBiome_ = ZoneBiome::Unknown;
}

void TumbleweedManager::addPlaceableBounds(const glm::vec3& min, const glm::vec3& max) {
    PlaceableBounds bounds;
    bounds.min = min;
    bounds.max = max;
    placeableObjects_.push_back(bounds);
}

float TumbleweedManager::randomFloat(float min, float max) const {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
