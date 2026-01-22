#ifndef EQT_GRAPHICS_DETAIL_FOOTPRINT_MANAGER_H
#define EQT_GRAPHICS_DETAIL_FOOTPRINT_MANAGER_H

#include <irrlicht.h>
#include <vector>
#include "client/graphics/detail/footprint_config.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/surface_map.h"
#include "client/graphics/detail/detail_texture_atlas.h"

namespace EQT {
namespace Graphics {
namespace Detail {

// Individual footprint data
struct Footprint {
    irr::core::vector3df position;
    irr::core::vector3df normal;  // Terrain normal for slope alignment
    float rotation;           // Heading in radians
    SurfaceType surfaceType;
    float age;                // Seconds since placed
    float maxAge;             // Fade duration (from config)
    bool isLeftFoot;          // True for left foot, false for right
};

// Manages player footprints with biome-aware textures
class FootprintManager {
public:
    FootprintManager(irr::scene::ISceneManager* smgr,
                     irr::video::IVideoDriver* driver);
    ~FootprintManager();

    // Configuration
    void setConfig(const FootprintConfig& config);
    void setSurfaceMap(const SurfaceMap* surfaceMap);
    void setAtlasTexture(irr::video::ITexture* atlas);
    void setCollisionSelector(irr::scene::ITriangleSelector* selector);

    // Called each frame with player position and movement state
    void update(float deltaTime,
                const irr::core::vector3df& playerPos,
                float playerHeading,
                bool playerMoving);

    // Render all active footprints
    void render();

    // Clear all footprints (e.g., on zone change)
    void clear();

    bool isEnabled() const { return config_.enabled; }
    size_t getActiveCount() const { return footprints_.size(); }

private:
    void tryPlaceFootprint(const irr::core::vector3df& pos, float heading);
    void removeExpiredFootprints();
    bool shouldShowOnSurface(SurfaceType type) const;
    irr::video::SColor getFootprintColor(SurfaceType type, float alpha) const;
    AtlasTile getFootprintTile(bool isLeftFoot, SurfaceType surfaceType) const;
    bool getGroundHeightAndNormal(float x, float z, float startY,
                                   float& outHeight, irr::core::vector3df& outNormal);

    FootprintConfig config_;
    const SurfaceMap* surfaceMap_ = nullptr;

    std::vector<Footprint> footprints_;
    irr::core::vector3df lastFootprintPos_;
    bool hasLastPos_ = false;
    bool nextFootIsLeft_ = true;  // Alternates between left and right foot

    static constexpr float kStrideWidth = 0.4f;  // Lateral offset for foot placement

    // Rendering
    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::video::ITexture* atlasTexture_ = nullptr;
    irr::video::SMaterial material_;
    irr::scene::ITriangleSelector* collisionSelector_ = nullptr;

    static constexpr size_t kMaxFootprints = 200;  // Limit for performance
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_FOOTPRINT_MANAGER_H
