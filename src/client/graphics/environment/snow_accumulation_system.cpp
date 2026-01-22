#include "client/graphics/environment/snow_accumulation_system.h"
#include "client/graphics/environment/accumulation_heightmap.h"
#include "client/graphics/environment/particle_types.h"
#include "client/raycast_mesh.h"
#include "common/logging.h"
#include <irrlicht.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace EQT {
namespace Graphics {
namespace Environment {

SnowAccumulationSystem::SnowAccumulationSystem()
    : rng_(std::random_device{}())
{
}

SnowAccumulationSystem::~SnowAccumulationSystem() {
    shutdown();
}

bool SnowAccumulationSystem::initialize(irr::scene::ISceneManager* smgr,
                                         irr::video::IVideoDriver* driver,
                                         irr::video::ITexture* atlasTexture) {
    if (initialized_) return true;

    smgr_ = smgr;
    driver_ = driver;
    atlasTexture_ = atlasTexture;

    if (!smgr_ || !driver_) {
        LOG_ERROR(MOD_GRAPHICS, "SnowAccumulationSystem: Scene manager or driver is null");
        return false;
    }

    // Create heightmap
    heightmap_ = std::make_unique<AccumulationHeightmap>();

    // Initialize decal pool
    decals_.resize(settings_.maxDecals);
    for (auto& decal : decals_) {
        decal.active = false;
    }

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "SnowAccumulationSystem: Initialized with {} max decals", settings_.maxDecals);
    return true;
}

void SnowAccumulationSystem::shutdown() {
    heightmap_.reset();
    decals_.clear();
    initialized_ = false;
}

void SnowAccumulationSystem::setSettings(const SnowAccumulationSettings& settings) {
    settings_ = settings;

    // Resize decal pool if needed
    if (static_cast<int>(decals_.size()) != settings_.maxDecals) {
        decals_.resize(settings_.maxDecals);
        for (auto& decal : decals_) {
            decal.active = false;
        }
    }

    // Update heightmap max depth
    if (heightmap_) {
        heightmap_->setMaxDepth(settings_.maxDepth);
    }
}

void SnowAccumulationSystem::setEnabled(bool enabled) {
    enabled_ = enabled;
}

void SnowAccumulationSystem::update(float deltaTime, const glm::vec3& playerPos) {
    if (!initialized_ || !isEnabled() || isIndoorZone_) {
        return;
    }

    // Update heightmap center to follow player
    if (heightmap_) {
        heightmap_->updateCenter(playerPos.x, playerPos.y);
    }

    // Accumulate time for periodic updates
    updateAccumulator_ += deltaTime;

    if (updateAccumulator_ >= settings_.updateInterval) {
        updateAccumulator_ = 0.0f;

        // Update accumulation or melting
        if (snowIntensity_ > 0) {
            updateAccumulation(settings_.updateInterval);
        } else if (heightmap_ && heightmap_->getSnowCellCount() > 0) {
            updateMelting(settings_.updateInterval);
        }
    }

    // Update shelter detection less frequently
    shelterUpdateAccumulator_ += deltaTime;
    if (shelterUpdateAccumulator_ >= 1.0f && settings_.shelterDetectionEnabled) {
        shelterUpdateAccumulator_ = 0.0f;
        updateShelterDetection(playerPos);
    }

    // Update visible decals
    updateDecals(playerPos);

    lastPlayerPos_ = playerPos;
}

void SnowAccumulationSystem::updateAccumulation(float deltaTime) {
    if (!heightmap_) return;

    // Calculate accumulation amount based on intensity
    float intensityFactor = static_cast<float>(snowIntensity_) / 10.0f;
    float amount = settings_.accumulationRate * intensityFactor * deltaTime;

    heightmap_->addSnowUniform(amount);
}

void SnowAccumulationSystem::updateMelting(float deltaTime) {
    if (!heightmap_) return;

    float amount = settings_.meltRate * deltaTime;
    heightmap_->meltUniform(amount);
}

void SnowAccumulationSystem::updateShelterDetection(const glm::vec3& playerPos) {
    if (!heightmap_ || !raycastMesh_) return;

    // Sample a grid of points around the player for shelter detection
    const float sampleRadius = 30.0f;
    const int samplesPerAxis = 5;
    const float sampleSpacing = sampleRadius * 2.0f / (samplesPerAxis - 1);

    std::uniform_real_distribution<float> jitterDist(-sampleSpacing * 0.3f, sampleSpacing * 0.3f);

    for (int y = 0; y < samplesPerAxis; ++y) {
        for (int x = 0; x < samplesPerAxis; ++x) {
            float worldX = playerPos.x - sampleRadius + x * sampleSpacing + jitterDist(rng_);
            float worldY = playerPos.y - sampleRadius + y * sampleSpacing + jitterDist(rng_);

            bool sheltered = checkShelter(worldX, worldY, playerPos.z);
            heightmap_->setSheltered(worldX, worldY, sheltered);
        }
    }
}

bool SnowAccumulationSystem::checkShelter(float worldX, float worldY, float worldZ) const {
    if (!raycastMesh_) return false;

    // Cast ray upward to check for overhead cover
    // RaycastMesh uses from/to points, not direction + distance
    glm::vec3 from(worldX, worldY, worldZ + 1.0f);  // Start slightly above ground
    glm::vec3 to(worldX, worldY, worldZ + settings_.shelterRayHeight);  // End point above

    glm::vec3 hitPoint;
    glm::vec3 hitNormal;
    float hitDistance;

    // RaycastMesh::raycast takes RmReal* arrays
    bool hit = raycastMesh_->raycast(
        reinterpret_cast<const float*>(&from),
        reinterpret_cast<const float*>(&to),
        reinterpret_cast<float*>(&hitPoint),
        reinterpret_cast<float*>(&hitNormal),
        &hitDistance
    );

    if (hit) {
        // Check slope angle - steep angles don't provide shelter
        // Normal should point down (negative Z in EQ coords)
        float upDot = glm::dot(hitNormal, glm::vec3(0, 0, -1));
        return upDot > settings_.slopeThreshold;
    }

    return false;  // No overhead cover
}

void SnowAccumulationSystem::updateDecals(const glm::vec3& playerPos) {
    if (!heightmap_) return;

    // Deactivate all decals first
    for (auto& decal : decals_) {
        decal.active = false;
    }

    // Generate decals based on accumulation
    int decalIndex = 0;
    float viewDist2 = settings_.viewDistance * settings_.viewDistance;

    // Sample grid around player
    float halfView = settings_.viewDistance * 0.8f;
    float spacing = settings_.decalSpacing;

    // Start position aligned to grid
    float startX = std::floor((playerPos.x - halfView) / spacing) * spacing;
    float startY = std::floor((playerPos.y - halfView) / spacing) * spacing;

    for (float y = startY; y < playerPos.y + halfView && decalIndex < settings_.maxDecals; y += spacing) {
        for (float x = startX; x < playerPos.x + halfView && decalIndex < settings_.maxDecals; x += spacing) {
            // Check distance
            float dx = x - playerPos.x;
            float dy = y - playerPos.y;
            float dist2 = dx * dx + dy * dy;

            if (dist2 > viewDist2) continue;

            // Get snow depth at this position
            float depth = heightmap_->getSnowDepthInterpolated(x, y);

            if (depth < settings_.dustingThreshold) continue;

            // Create decal
            SnowDecal& decal = decals_[decalIndex++];
            decal.position = glm::vec3(x, y, playerPos.z);  // Use player Z as base height
            decal.depth = depth;
            decal.active = true;

            // Size scales slightly with depth
            decal.size = settings_.decalSize * (0.8f + 0.4f * (depth / settings_.maxDepth));

            // Alpha based on depth thresholds
            if (depth >= settings_.fullThreshold) {
                decal.alpha = 1.0f;
            } else if (depth >= settings_.partialThreshold) {
                float t = (depth - settings_.partialThreshold) / (settings_.fullThreshold - settings_.partialThreshold);
                decal.alpha = 0.6f + 0.4f * t;
            } else {
                float t = (depth - settings_.dustingThreshold) / (settings_.partialThreshold - settings_.dustingThreshold);
                decal.alpha = 0.2f + 0.4f * t;
            }

            // Fade with distance
            float distFade = 1.0f - std::sqrt(dist2) / settings_.viewDistance;
            decal.alpha *= distFade;
        }
    }
}

void SnowAccumulationSystem::render(const glm::vec3& cameraPos) {
    if (!initialized_ || !isEnabled() || !driver_ || !atlasTexture_) {
        return;
    }

    // Count active decals
    int activeCount = 0;
    for (const auto& decal : decals_) {
        if (decal.active) ++activeCount;
    }

    if (activeCount == 0) return;

    // Set up material for snow decals
    irr::video::SMaterial material;
    material.Lighting = false;
    material.ZWriteEnable = false;
    material.ZBuffer = irr::video::ECFN_LESSEQUAL;
    material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
    material.setTexture(0, atlasTexture_);
    material.TextureLayer[0].BilinearFilter = true;

    driver_->setMaterial(material);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // Render each active decal
    for (const auto& decal : decals_) {
        if (decal.active) {
            renderDecal(decal);
        }
    }
}

void SnowAccumulationSystem::renderDecal(const SnowDecal& decal) {
    // Convert to Irrlicht coordinates (EQ Z-up to Irrlicht Y-up)
    irr::core::vector3df pos(decal.position.x, decal.position.z, decal.position.y);

    float halfSize = decal.size * 0.5f;

    // Calculate color with alpha
    uint8_t alpha = static_cast<uint8_t>(decal.alpha * settings_.snowColorA * 255.0f);
    uint8_t r = static_cast<uint8_t>(settings_.snowColorR * 255.0f);
    uint8_t g = static_cast<uint8_t>(settings_.snowColorG * 255.0f);
    uint8_t b = static_cast<uint8_t>(settings_.snowColorB * 255.0f);
    irr::video::SColor color(alpha, r, g, b);

    // Calculate UV coordinates for SnowPatch tile (tile 11)
    const float tileU = 1.0f / ParticleAtlas::AtlasColumns;
    const float tileV = 1.0f / ParticleAtlas::AtlasRows;
    const int tileX = ParticleAtlas::SnowPatch % ParticleAtlas::AtlasColumns;
    const int tileY = ParticleAtlas::SnowPatch / ParticleAtlas::AtlasColumns;

    float u0 = tileX * tileU;
    float v0 = tileY * tileV;
    float u1 = u0 + tileU;
    float v1 = v0 + tileV;

    // Create horizontal quad (flat on ground)
    // Slight Y offset to prevent z-fighting with terrain
    float yOffset = 0.02f + decal.depth * 0.1f;  // Deeper snow sits higher

    irr::video::S3DVertex vertices[4];

    vertices[0].Pos = irr::core::vector3df(pos.X - halfSize, pos.Y + yOffset, pos.Z - halfSize);
    vertices[0].TCoords = irr::core::vector2df(u0, v0);
    vertices[0].Color = color;

    vertices[1].Pos = irr::core::vector3df(pos.X + halfSize, pos.Y + yOffset, pos.Z - halfSize);
    vertices[1].TCoords = irr::core::vector2df(u1, v0);
    vertices[1].Color = color;

    vertices[2].Pos = irr::core::vector3df(pos.X + halfSize, pos.Y + yOffset, pos.Z + halfSize);
    vertices[2].TCoords = irr::core::vector2df(u1, v1);
    vertices[2].Color = color;

    vertices[3].Pos = irr::core::vector3df(pos.X - halfSize, pos.Y + yOffset, pos.Z + halfSize);
    vertices[3].TCoords = irr::core::vector2df(u0, v1);
    vertices[3].Color = color;

    // Set normals pointing up
    for (int i = 0; i < 4; ++i) {
        vertices[i].Normal = irr::core::vector3df(0, 1, 0);
    }

    // Indices for two triangles
    irr::u16 indices[6] = { 0, 1, 2, 0, 2, 3 };

    driver_->drawIndexedTriangleList(vertices, 4, indices, 2);
}

bool SnowAccumulationSystem::hasVisibleSnow() const {
    if (!heightmap_) return false;
    return heightmap_->getSnowCellCount() > 0;
}

float SnowAccumulationSystem::getSnowDepth(float worldX, float worldY) const {
    if (!heightmap_) return 0.0f;
    return heightmap_->getSnowDepth(worldX, worldY);
}

void SnowAccumulationSystem::onZoneEnter(const std::string& zoneName, bool isIndoor) {
    currentZoneName_ = zoneName;
    isIndoorZone_ = isIndoor;

    // Reset accumulation for new zone
    if (heightmap_) {
        heightmap_->reset();
    }

    // Clear decals
    for (auto& decal : decals_) {
        decal.active = false;
    }

    updateAccumulator_ = 0.0f;
    shelterUpdateAccumulator_ = 0.0f;

    LOG_DEBUG(MOD_GRAPHICS, "SnowAccumulationSystem: Zone enter '{}', indoor={}", zoneName, isIndoor);
}

void SnowAccumulationSystem::onZoneLeave() {
    // Reset accumulation
    if (heightmap_) {
        heightmap_->reset();
    }

    // Clear decals
    for (auto& decal : decals_) {
        decal.active = false;
    }

    snowIntensity_ = 0;

    LOG_DEBUG(MOD_GRAPHICS, "SnowAccumulationSystem: Zone leave");
}

std::string SnowAccumulationSystem::getDebugInfo() const {
    std::ostringstream ss;
    ss << "SnowAccum: ";

    if (heightmap_) {
        ss << heightmap_->getSnowCellCount() << " cells";
        ss << ", avg=" << std::fixed << std::setprecision(2) << heightmap_->getAverageDepth();
    }

    // Count active decals
    int activeDecals = 0;
    for (const auto& decal : decals_) {
        if (decal.active) ++activeDecals;
    }
    ss << ", " << activeDecals << " decals";

    return ss.str();
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
