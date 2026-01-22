#include "client/graphics/detail/footprint_manager.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Detail {

FootprintManager::FootprintManager(irr::scene::ISceneManager* smgr,
                                   irr::video::IVideoDriver* driver)
    : smgr_(smgr), driver_(driver) {

    // Setup material for transparent footprint rendering
    // Use texture alpha for shape, texture color for appearance
    material_.Lighting = false;
    material_.ZWriteEnable = false;  // Don't write to depth buffer
    material_.ZBuffer = irr::video::ECFN_LESSEQUAL;
    material_.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
    material_.BackfaceCulling = false;  // Visible from both sides
}

FootprintManager::~FootprintManager() {
    clear();
}

void FootprintManager::setConfig(const FootprintConfig& config) {
    config_ = config;
}

void FootprintManager::setSurfaceMap(const SurfaceMap* surfaceMap) {
    surfaceMap_ = surfaceMap;
}

void FootprintManager::setAtlasTexture(irr::video::ITexture* atlas) {
    atlasTexture_ = atlas;
    material_.setTexture(0, atlas);
}

void FootprintManager::setCollisionSelector(irr::scene::ITriangleSelector* selector) {
    collisionSelector_ = selector;
}

void FootprintManager::update(float deltaTime,
                               const irr::core::vector3df& playerPos,
                               float playerHeading,
                               bool playerMoving) {
    if (!config_.enabled) return;

    // Age existing footprints
    for (auto& fp : footprints_) {
        fp.age += deltaTime;
    }

    // Remove expired footprints
    removeExpiredFootprints();

    // Try to place new footprint if player is moving
    if (playerMoving) {
        if (!hasLastPos_) {
            lastFootprintPos_ = playerPos;
            hasLastPos_ = true;
        }

        float dist = playerPos.getDistanceFrom(lastFootprintPos_);
        if (dist >= config_.placementInterval) {
            tryPlaceFootprint(playerPos, playerHeading);
        }
    }
}

void FootprintManager::tryPlaceFootprint(const irr::core::vector3df& pos, float heading) {
    // Check surface type
    if (!surfaceMap_ || !surfaceMap_->isLoaded()) return;

    SurfaceType surface = surfaceMap_->getSurfaceType(pos.X, pos.Z);
    if (!shouldShowOnSurface(surface)) return;

    // Check footprint limit
    if (footprints_.size() >= kMaxFootprints) {
        // Remove oldest
        footprints_.erase(footprints_.begin());
    }

    // Create new footprint
    Footprint fp;
    fp.isLeftFoot = nextFootIsLeft_;
    nextFootIsLeft_ = !nextFootIsLeft_;  // Alternate for next footprint

    // Calculate lateral offset based on which foot (perpendicular to heading)
    // In Irrlicht: X is right, Z is forward based on heading
    float perpAngle = heading + (fp.isLeftFoot ? 1.5708f : -1.5708f);  // +/- 90 degrees
    float lateralX = std::sin(perpAngle) * kStrideWidth;
    float lateralZ = std::cos(perpAngle) * kStrideWidth;

    fp.position = pos;
    fp.position.X += lateralX;
    fp.position.Z += lateralZ;
    fp.normal = irr::core::vector3df(0, 1, 0);  // Default to up

    // Try to get actual ground height and normal via raycast
    float groundHeight;
    irr::core::vector3df groundNormal;
    if (getGroundHeightAndNormal(fp.position.X, fp.position.Z, pos.Y, groundHeight, groundNormal)) {
        // Use raycast result
        fp.position.Y = groundHeight + config_.groundOffset;
        fp.normal = groundNormal;
    } else {
        // Fallback: subtract player height offset to estimate ground level
        fp.position.Y = pos.Y - config_.playerHeightOffset + config_.groundOffset;
    }

    fp.rotation = heading;
    fp.surfaceType = surface;
    fp.age = 0.0f;
    fp.maxAge = config_.fadeDuration;

    footprints_.push_back(fp);
    lastFootprintPos_ = pos;
}

void FootprintManager::removeExpiredFootprints() {
    footprints_.erase(
        std::remove_if(footprints_.begin(), footprints_.end(),
            [](const Footprint& fp) { return fp.age >= fp.maxAge; }),
        footprints_.end());
}

bool FootprintManager::shouldShowOnSurface(SurfaceType type) const {
    switch (type) {
        case SurfaceType::Grass:
            return config_.showOnGrass;
        case SurfaceType::Dirt:
            return config_.showOnDirt;
        case SurfaceType::Sand:
            return config_.showOnSand;
        case SurfaceType::Snow:
            return config_.showOnSnow;
        case SurfaceType::Swamp:
            return config_.showOnSwamp;
        case SurfaceType::Jungle:
            return config_.showOnJungle;
        // Hard surfaces - no footprints
        case SurfaceType::Stone:
        case SurfaceType::Water:
        case SurfaceType::Lava:
        case SurfaceType::Wood:
        case SurfaceType::Unknown:
        default:
            return false;
    }
}

irr::video::SColor FootprintManager::getFootprintColor(SurfaceType type, float alpha) const {
    // Color comes from texture, vertex color is white
    // Alpha controls fade based on age (but EMT_TRANSPARENT_ALPHA_CHANNEL ignores vertex alpha)
    (void)type;  // Will be used when we add per-biome atlas tiles
    irr::u32 a = static_cast<irr::u32>(alpha * 255.0f);
    return irr::video::SColor(a, 255, 255, 255);
}

AtlasTile FootprintManager::getFootprintTile(bool isLeftFoot, SurfaceType surfaceType) const {
    switch (surfaceType) {
        case SurfaceType::Grass:
            return isLeftFoot ? AtlasTile::FootprintLeftGrass : AtlasTile::FootprintRightGrass;
        case SurfaceType::Dirt:
            return isLeftFoot ? AtlasTile::FootprintLeftDirt : AtlasTile::FootprintRightDirt;
        case SurfaceType::Sand:
            return isLeftFoot ? AtlasTile::FootprintLeftSand : AtlasTile::FootprintRightSand;
        case SurfaceType::Snow:
            return isLeftFoot ? AtlasTile::FootprintLeftSnow : AtlasTile::FootprintRightSnow;
        case SurfaceType::Swamp:
            return isLeftFoot ? AtlasTile::FootprintLeftSwamp : AtlasTile::FootprintRightSwamp;
        case SurfaceType::Jungle:
            return isLeftFoot ? AtlasTile::FootprintLeftJungle : AtlasTile::FootprintRightJungle;
        default:
            // Fallback to grass for unknown surfaces
            return isLeftFoot ? AtlasTile::FootprintLeftGrass : AtlasTile::FootprintRightGrass;
    }
}

void FootprintManager::render() {
    if (!config_.enabled || footprints_.empty() || !driver_ || !atlasTexture_) return;

    driver_->setMaterial(material_);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    for (const auto& fp : footprints_) {
        // Calculate alpha based on age (fade out over time)
        float alpha = 1.0f - (fp.age / fp.maxAge);
        alpha = std::max(0.0f, std::min(1.0f, alpha));

        // Get color with alpha
        irr::video::SColor color = getFootprintColor(fp.surfaceType, alpha);

        // Get UV coordinates for biome-specific left or right foot texture
        AtlasTile tile = getFootprintTile(fp.isLeftFoot, fp.surfaceType);
        float u0, v0, u1, v1;
        getTileUV(tile, u0, v0, u1, v1);

        // Build quad vertices rotated to match player heading and aligned to terrain slope
        float halfSize = config_.footprintSize * 0.5f;
        float cosH = std::cos(fp.rotation);
        float sinH = std::sin(fp.rotation);

        // Create a coordinate frame aligned to the terrain normal
        // normal = up direction for the footprint
        // tangent = forward direction (based on heading)
        // bitangent = right direction
        irr::core::vector3df normal = fp.normal;
        normal.normalize();

        // Forward direction in world XZ plane based on heading
        irr::core::vector3df forward(sinH, 0, cosH);

        // Project forward onto the plane defined by normal to get tangent
        // tangent = forward - (forward . normal) * normal
        float dot = forward.dotProduct(normal);
        irr::core::vector3df tangent = forward - normal * dot;
        tangent.normalize();

        // Bitangent is perpendicular to both (right direction)
        irr::core::vector3df bitangent = tangent.crossProduct(normal);
        bitangent.normalize();

        // Quad corners relative to center, transformed by our coordinate frame
        irr::video::S3DVertex vertices[4];

        // Corner offsets: (-halfSize, -halfSize), (halfSize, -halfSize), (halfSize, halfSize), (-halfSize, halfSize)
        // In our frame: X = bitangent direction, Z = tangent direction
        irr::core::vector3df offsets[4] = {
            bitangent * (-halfSize) + tangent * (-halfSize),
            bitangent * ( halfSize) + tangent * (-halfSize),
            bitangent * ( halfSize) + tangent * ( halfSize),
            bitangent * (-halfSize) + tangent * ( halfSize)
        };

        for (int i = 0; i < 4; ++i) {
            vertices[i].Pos = fp.position + offsets[i];
            vertices[i].Color = color;
            vertices[i].Normal = normal;
        }

        vertices[0].TCoords = irr::core::vector2df(u0, v1);
        vertices[1].TCoords = irr::core::vector2df(u1, v1);
        vertices[2].TCoords = irr::core::vector2df(u1, v0);
        vertices[3].TCoords = irr::core::vector2df(u0, v0);

        irr::u16 indices[6] = {0, 1, 2, 0, 2, 3};
        driver_->drawIndexedTriangleList(vertices, 4, indices, 2);
    }
}

void FootprintManager::clear() {
    footprints_.clear();
    hasLastPos_ = false;
}

bool FootprintManager::getGroundHeightAndNormal(float x, float z, float startY,
                                                  float& outHeight, irr::core::vector3df& outNormal) {
    if (!collisionSelector_ || !smgr_) {
        return false;
    }

    // Raycast from above the player position down to find ground
    // Irrlicht coords: X=EQ_X, Y=EQ_Z (up), Z=EQ_Y
    float rayStartY = startY + 50.0f;  // Start well above player
    irr::core::vector3df start(x, rayStartY, z);
    irr::core::vector3df end(x, rayStartY - 200.0f, z);  // Cast down 200 units
    irr::core::line3df ray(start, end);

    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTri;
    irr::scene::ISceneNode* hitNode = nullptr;

    if (smgr_->getSceneCollisionManager()->getCollisionPoint(
            ray, collisionSelector_, hitPoint, hitTri, hitNode)) {
        outHeight = hitPoint.Y;
        // Calculate normal from hit triangle
        outNormal = hitTri.getNormal();
        outNormal.normalize();
        // Make sure normal points up (not down)
        if (outNormal.Y < 0) {
            outNormal = -outNormal;
        }
        return true;
    }

    return false;
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
