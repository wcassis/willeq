#include "client/graphics/environment/water_ripple_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "common/logging.h"
#include <irrlicht.h>
#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {

WaterRippleManager::WaterRippleManager()
    : rng_(std::random_device{}())
{
}

bool WaterRippleManager::initialize(irr::video::IVideoDriver* driver,
                                     irr::scene::ISceneManager* smgr,
                                     irr::video::ITexture* atlasTexture) {
    if (initialized_) return true;

    driver_ = driver;
    smgr_ = smgr;
    atlasTexture_ = atlasTexture;

    if (!driver_ || !smgr_) {
        LOG_ERROR(MOD_GRAPHICS, "WaterRippleManager: Driver or scene manager is null");
        return false;
    }

    // Initialize ripple pool
    ripples_.resize(settings_.maxRipples);
    for (auto& ripple : ripples_) {
        ripple.active = false;
    }

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "WaterRippleManager: Initialized with {} ripple pool", settings_.maxRipples);
    return true;
}

void WaterRippleManager::setSettings(const WaterRippleSettings& settings) {
    settings_ = settings;

    // Resize pool if needed
    if (static_cast<int>(ripples_.size()) != settings_.maxRipples) {
        ripples_.resize(settings_.maxRipples);
        for (auto& ripple : ripples_) {
            ripple.active = false;
        }
        LOG_DEBUG(MOD_GRAPHICS, "WaterRippleManager: Resized pool to {} ripples", settings_.maxRipples);
    }
}

void WaterRippleManager::update(float deltaTime, const glm::vec3& playerPos, const glm::vec3& cameraPos) {
    if (!initialized_ || !isEnabled() || rainIntensity_ == 0) {
        return;
    }

    // Update existing ripples
    for (auto& ripple : ripples_) {
        if (!ripple.active) continue;

        ripple.age += deltaTime;

        if (ripple.age >= ripple.maxAge) {
            // Ripple expired
            ripple.active = false;
            continue;
        }

        // Calculate progress (0 to 1)
        float progress = ripple.age / ripple.maxAge;

        // Expand size over lifetime
        ripple.size = settings_.minSize + (ripple.maxSize - settings_.minSize) * progress;

        // Fade out alpha (faster fade at end)
        ripple.alpha = 1.0f - std::pow(progress, 0.7f);
    }

    // Spawn new ripples based on rain intensity
    if (surfaceMap_ && zoneHasWater_) {
        // Spawn rate scales with intensity (0-10)
        float spawnRate = settings_.spawnRate * (rainIntensity_ / 10.0f);
        spawnAccumulator_ += spawnRate * deltaTime;

        while (spawnAccumulator_ >= 1.0f) {
            spawnRipple(playerPos);
            spawnAccumulator_ -= 1.0f;
        }
    }
}

void WaterRippleManager::spawnRipple(const glm::vec3& playerPos) {
    WaterRipple* ripple = getInactiveRipple();
    if (!ripple) {
        return;  // Pool exhausted
    }

    glm::vec3 spawnPos;
    if (!findWaterPosition(playerPos, settings_.spawnRadius, spawnPos)) {
        return;  // No valid water position found
    }

    // Initialize ripple
    ripple->position = spawnPos;
    ripple->age = 0.0f;
    ripple->maxAge = settings_.lifetime;
    ripple->size = settings_.minSize;
    ripple->maxSize = settings_.maxSize;
    ripple->alpha = 1.0f;
    ripple->active = true;
}

bool WaterRippleManager::findWaterPosition(const glm::vec3& center, float radius, glm::vec3& outPos) {
    if (!surfaceMap_ || !surfaceMap_->isLoaded()) {
        return false;
    }

    // Try random positions to find water
    std::uniform_real_distribution<float> angleDist(0.0f, 6.283185f);
    std::uniform_real_distribution<float> radiusDist(0.0f, radius);

    const int maxAttempts = 10;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        float angle = angleDist(rng_);
        float dist = radiusDist(rng_);

        float x = center.x + std::cos(angle) * dist;
        float y = center.y + std::sin(angle) * dist;

        // Check if this position is water
        Detail::SurfaceType surface = surfaceMap_->getSurfaceType(x, y);
        if (surface == Detail::SurfaceType::Water) {
            // Get water height
            float height = surfaceMap_->getHeight(x, y);
            if (height > -9000.0f) {
                outPos.x = x;
                outPos.y = y;
                outPos.z = height + settings_.heightOffset;
                return true;
            }
        }
    }

    return false;
}

WaterRipple* WaterRippleManager::getInactiveRipple() {
    for (auto& ripple : ripples_) {
        if (!ripple.active) {
            return &ripple;
        }
    }
    return nullptr;
}

void WaterRippleManager::render(const glm::vec3& cameraPos, const glm::vec3& cameraUp) {
    if (!initialized_ || !isEnabled() || !driver_ || !atlasTexture_) {
        return;
    }

    // Set up material for ripples (alpha blended, no lighting)
    irr::video::SMaterial material;
    material.Lighting = false;
    material.ZWriteEnable = false;
    material.ZBuffer = irr::video::ECFN_LESSEQUAL;
    material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
    material.setTexture(0, atlasTexture_);
    material.TextureLayer[0].BilinearFilter = true;

    driver_->setMaterial(material);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // Render each active ripple
    for (const auto& ripple : ripples_) {
        if (ripple.active) {
            renderRipple(ripple);
        }
    }
}

void WaterRippleManager::renderRipple(const WaterRipple& ripple) {
    // Convert to Irrlicht coordinates (EQ Z-up to Irrlicht Y-up)
    irr::core::vector3df pos(ripple.position.x, ripple.position.z, ripple.position.y);

    float halfSize = ripple.size * 0.5f;

    // Calculate color with alpha
    uint8_t alpha = static_cast<uint8_t>(ripple.alpha * settings_.colorA * 255.0f);
    uint8_t r = static_cast<uint8_t>(settings_.colorR * 255.0f);
    uint8_t g = static_cast<uint8_t>(settings_.colorG * 255.0f);
    uint8_t b = static_cast<uint8_t>(settings_.colorB * 255.0f);
    irr::video::SColor color(alpha, r, g, b);

    // Calculate UV coordinates for RippleRing tile (tile 10)
    // Atlas is 4x3, tile 10 is at position (2, 2)
    const float tileU = 1.0f / ParticleAtlas::AtlasColumns;
    const float tileV = 1.0f / ParticleAtlas::AtlasRows;
    const int tileX = ParticleAtlas::RippleRing % ParticleAtlas::AtlasColumns;
    const int tileY = ParticleAtlas::RippleRing / ParticleAtlas::AtlasColumns;

    float u0 = tileX * tileU;
    float v0 = tileY * tileV;
    float u1 = u0 + tileU;
    float v1 = v0 + tileV;

    // Create horizontal billboard (flat on water surface)
    // Vertices in XZ plane (Irrlicht Y is up)
    irr::video::S3DVertex vertices[4];

    vertices[0].Pos = irr::core::vector3df(pos.X - halfSize, pos.Y, pos.Z - halfSize);
    vertices[0].TCoords = irr::core::vector2df(u0, v0);
    vertices[0].Color = color;

    vertices[1].Pos = irr::core::vector3df(pos.X + halfSize, pos.Y, pos.Z - halfSize);
    vertices[1].TCoords = irr::core::vector2df(u1, v0);
    vertices[1].Color = color;

    vertices[2].Pos = irr::core::vector3df(pos.X + halfSize, pos.Y, pos.Z + halfSize);
    vertices[2].TCoords = irr::core::vector2df(u1, v1);
    vertices[2].Color = color;

    vertices[3].Pos = irr::core::vector3df(pos.X - halfSize, pos.Y, pos.Z + halfSize);
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

void WaterRippleManager::onZoneEnter(const std::string& zoneName) {
    currentZoneName_ = zoneName;

    // Assume zones might have water - actual detection happens via SurfaceMap
    zoneHasWater_ = true;

    // Clear all ripples
    for (auto& ripple : ripples_) {
        ripple.active = false;
    }
    spawnAccumulator_ = 0.0f;

    LOG_DEBUG(MOD_GRAPHICS, "WaterRippleManager: Zone enter '{}'", zoneName);
}

void WaterRippleManager::onZoneLeave() {
    // Clear all ripples
    for (auto& ripple : ripples_) {
        ripple.active = false;
    }
    spawnAccumulator_ = 0.0f;
    zoneHasWater_ = false;

    LOG_DEBUG(MOD_GRAPHICS, "WaterRippleManager: Zone leave");
}

int WaterRippleManager::getActiveRippleCount() const {
    int count = 0;
    for (const auto& ripple : ripples_) {
        if (ripple.active) {
            ++count;
        }
    }
    return count;
}

std::string WaterRippleManager::getDebugInfo() const {
    return "Ripples: " + std::to_string(getActiveRippleCount()) + "/" +
           std::to_string(settings_.maxRipples) +
           " (intensity: " + std::to_string(rainIntensity_) + ")";
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
