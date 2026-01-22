#ifndef EQT_GRAPHICS_DETAIL_MANAGER_H
#define EQT_GRAPHICS_DETAIL_MANAGER_H

#include <irrlicht.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/wind_controller.h"
#include "client/graphics/detail/seasonal_controller.h"
#include "client/graphics/detail/surface_map.h"
#include "client/graphics/detail/foliage_disturbance.h"
#include "client/graphics/detail/footprint_config.h"
#include "client/graphics/detail/footprint_manager.h"

namespace EQT {
namespace Graphics {

// Forward declarations
class WldLoader;
struct BspTree;
struct ZoneGeometry;

namespace Detail {

class DetailChunk;

class DetailManager {
public:
    DetailManager(irr::scene::ISceneManager* smgr,
                  irr::video::IVideoDriver* driver);
    ~DetailManager();

    // Frame update - call from IrrlichtRenderer::processFrame
    // playerPos/playerVelocity: for foliage disturbance (grass bending when walking)
    // playerHeading: player facing direction in radians (for footprint rotation)
    // playerMoving: true if player is currently moving
    void update(const irr::core::vector3df& cameraPos, float deltaTimeMs,
                const irr::core::vector3df& playerPos = irr::core::vector3df(0, 0, 0),
                const irr::core::vector3df& playerVelocity = irr::core::vector3df(0, 0, 0),
                float playerHeading = 0.0f,
                bool playerMoving = false);

    // Density control (0.0 = off, 1.0 = maximum)
    void setDensity(float density);
    float getDensity() const { return density_; }
    void adjustDensity(float delta);  // For keyboard +/- controls

    // Zone lifecycle
    void onZoneEnter(const std::string& zoneName,
                     irr::scene::ITriangleSelector* zoneSelector,
                     irr::scene::IMeshSceneNode* zoneMeshNode,
                     const std::shared_ptr<WldLoader>& wldLoader = nullptr,
                     const std::shared_ptr<ZoneGeometry>& zoneGeometry = nullptr);
    void onZoneExit();

    // Surface map configuration (pre-computed surface type data)
    void setSurfaceMapsPath(const std::string& path);
    bool hasSurfaceMap() const { return surfaceMap_.isLoaded(); }
    const SurfaceMap* getSurfaceMap() const { return surfaceMap_.isLoaded() ? &surfaceMap_ : nullptr; }

    // Add additional mesh nodes for texture lookup (for PVS mode with multiple region meshes)
    void addMeshNodeForTextureLookup(irr::scene::IMeshSceneNode* node);

    // Category toggles
    void setCategoryEnabled(DetailCategory cat, bool enabled);
    bool isCategoryEnabled(DetailCategory cat) const;

    // Season control
    Season getCurrentSeason() const { return currentSeason_; }
    void setSeasonOverride(Season season);
    void clearSeasonOverride();

    // Debug
    std::string getDebugInfo() const;
    bool isEnabled() const { return enabled_ && density_ > 0.01f; }
    void setEnabled(bool enabled);

    // Foliage disturbance control
    void setFoliageDisturbanceConfig(const FoliageDisturbanceConfig& config);
    const FoliageDisturbanceConfig& getFoliageDisturbanceConfig() const;
    bool isFoliageDisturbanceEnabled() const;

    // Footprint system control
    void setFootprintConfig(const FootprintConfig& config);
    const FootprintConfig& getFootprintConfig() const;
    bool isFootprintEnabled() const;

    // Render footprints (called after terrain rendering)
    void renderFootprints();

    // Access for UI/commands
    size_t getActiveChunkCount() const { return activeChunks_.size(); }
    size_t getTotalPlacementCount() const;
    size_t getVisiblePlacementCount() const;

private:
    void updateVisibleChunks(const irr::core::vector3df& cameraPos);
    void loadChunk(const ChunkKey& key);
    void unloadDistantChunks(const irr::core::vector3df& cameraPos);
    void rebuildAllChunkMeshes();  // Called when density changes

    // Ground height and surface type query using triangle selector
    bool getGroundInfo(float x, float z, float& outY,
                       irr::core::vector3df& outNormal,
                       SurfaceType& outSurfaceType) const;

    // Classify texture name into surface type
    SurfaceType classifyTexture(const std::string& textureName) const;

    // Find surface type at a world position by sampling zone mesh textures
    SurfaceType getSurfaceTypeAtPosition(float x, float z) const;

    // Get surface type by looking up texture from mesh node materials
    SurfaceType getTextureFromMeshNode(const irr::core::triangle3df& hitTriangle) const;

    // Check if point is in exclusion zone (AABB boxes only)
    bool isExcluded(const irr::core::vector3df& pos) const;

    // Check if point is in BSP-defined exclusion region (water, lava, zone lines)
    bool isExcludedByBsp(const irr::core::vector3df& pos) const;

    // Create default test config (hardcoded for Phase 1)
    ZoneDetailConfig createDefaultConfig(const std::string& zoneName) const;

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;

    // State
    bool enabled_ = true;
    float density_ = 0.5f;         // Default 50%
    uint32_t categoryMask_ = static_cast<uint32_t>(DetailCategory::All);

    // Zone data
    ZoneDetailConfig config_;
    std::string currentZone_;
    irr::scene::ITriangleSelector* zoneSelector_ = nullptr;
    irr::scene::IMeshSceneNode* zoneMeshNode_ = nullptr;  // For texture lookups
    std::vector<irr::scene::IMeshSceneNode*> additionalMeshNodes_;  // Additional mesh nodes for PVS mode
    irr::scene::ISceneCollisionManager* collisionManager_ = nullptr;

    // Pre-computed surface map for fast surface type lookups
    std::string surfaceMapsPath_;  // Directory containing <zonename>_surface.map files
    SurfaceMap surfaceMap_;

    // BSP tree for water/lava/zoneline exclusion
    std::shared_ptr<BspTree> bspTree_;

    // Zone geometry for texture lookups (triangles with texture indices)
    std::shared_ptr<ZoneGeometry> zoneGeometry_;
    std::vector<std::string> zoneTextureNames_;

    // Texture atlas (nullptr for Phase 1 - uses colored quads)
    irr::video::ITexture* atlasTexture_ = nullptr;

    // Chunk management
    std::unordered_map<ChunkKey, std::unique_ptr<DetailChunk>, ChunkKeyHash> chunks_;
    std::vector<DetailChunk*> activeChunks_;
    ChunkKey lastCameraChunk_{INT32_MAX, INT32_MAX};

    // View distance in chunks
    int viewDistanceChunks_ = 2;  // 2 chunks in each direction = 5x5 grid

    // Wind animation
    WindController windController_;

    // Foliage disturbance (player movement bending grass)
    std::unique_ptr<FoliageDisturbanceManager> disturbanceManager_;
    FoliageDisturbanceConfig disturbanceConfig_;

    // Footprint system (fading footprints on soft surfaces)
    std::unique_ptr<FootprintManager> footprintManager_;
    FootprintConfig footprintConfig_;

    // Seasonal effects
    SeasonalController seasonalController_;
    Season currentSeason_ = Season::Default;
    irr::video::SColor seasonTint_{255, 255, 255, 255};
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_MANAGER_H
