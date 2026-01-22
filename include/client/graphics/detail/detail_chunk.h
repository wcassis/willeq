#ifndef EQT_GRAPHICS_DETAIL_CHUNK_H
#define EQT_GRAPHICS_DETAIL_CHUNK_H

#include <irrlicht.h>
#include <vector>
#include "client/graphics/detail/detail_types.h"

namespace EQT {
namespace Graphics {
namespace Detail {

class WindController;
class FoliageDisturbanceManager;

class DetailChunk {
public:
    DetailChunk(const ChunkKey& key, float size,
                irr::scene::ISceneManager* smgr,
                irr::video::IVideoDriver* driver);
    ~DetailChunk();

    // Generate placements for this chunk (done once at creation)
    void generatePlacements(const ZoneDetailConfig& config,
                            GroundQueryFunc groundQuery,
                            ExclusionCheckFunc exclusionCheck,
                            uint32_t seed);

    // Rebuild mesh with current density and category mask
    void rebuildMesh(float density, uint32_t categoryMask,
                     const ZoneDetailConfig& config,
                     irr::video::ITexture* atlas);

    // Apply wind animation to vertices (call each frame)
    void applyWind(const WindController& wind, const ZoneDetailConfig& config);

    // Apply wind and foliage disturbance together (call each frame when disturbance is enabled)
    void applyWindAndDisturbance(const WindController& wind,
                                  const FoliageDisturbanceManager& disturbance,
                                  const ZoneDetailConfig& config);

    // Scene attachment
    void attach();
    void detach();
    bool isAttached() const { return attached_; }

    const ChunkKey& getKey() const { return key_; }
    const irr::core::aabbox3df& getBounds() const { return bounds_; }
    size_t getPlacementCount() const { return placements_.size(); }
    size_t getVisibleCount() const { return visibleCount_; }

    // Check if placements have been generated
    bool hasGeneratedPlacements() const { return placementsGenerated_; }

private:
    void buildQuadGeometry(const DetailPlacement& p,
                           const DetailType& type,
                           std::vector<irr::video::S3DVertex>& verts,
                           std::vector<irr::u16>& indices,
                           bool useTestColor,
                           const irr::video::SColor& seasonTint);

    void setupMaterial(irr::video::ITexture* atlas);

    ChunkKey key_;
    float size_;
    irr::core::aabbox3df bounds_;

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;

    // Full placement list (generated once)
    std::vector<DetailPlacement> placements_;
    bool placementsGenerated_ = false;

    // Current mesh
    irr::scene::SMesh* mesh_ = nullptr;
    irr::scene::SMeshBuffer* buffer_ = nullptr;
    irr::scene::IMeshSceneNode* sceneNode_ = nullptr;
    bool attached_ = false;

    // Tracking
    size_t visibleCount_ = 0;
    float lastDensity_ = -1.0f;
    uint32_t lastCategoryMask_ = 0;

    // Stored atlas texture for scene node material
    irr::video::ITexture* atlasTexture_ = nullptr;

    // Wind animation data - stored during rebuildMesh
    std::vector<irr::core::vector3df> basePositions_;  // Original vertex positions
    std::vector<float> windInfluence_;                 // Per-vertex wind multiplier (0 at base, windResponse at top)
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_CHUNK_H
