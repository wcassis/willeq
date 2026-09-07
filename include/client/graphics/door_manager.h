#ifndef EQT_GRAPHICS_DOOR_MANAGER_H
#define EQT_GRAPHICS_DOOR_MANAGER_H

#include <irrlicht.h>
#include "client/graphics/door_collision.h"
#include "client/graphics/door_motion.h"
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace EQT {
namespace Graphics {

// Forward declarations
struct S3DZone;
struct BspTree;
class ConstrainedTextureCache;
class FrustumCuller;

// Visual representation of a door in the scene
struct DoorVisual {
    irr::scene::ISceneNode* pivotNode = nullptr;      // Pivot point for rotation (at hinge)
    irr::scene::IMeshSceneNode* sceneNode = nullptr;  // Door mesh (offset from pivot)
    uint8_t doorId = 0;
    std::string modelName;

    // EQ coordinates (for interaction checks)
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    // Rotation state
    float closedHeading = 0.0f;    // Heading when closed (degrees)
    float openHeading = 0.0f;      // Heading when open (degrees)
    uint16_t size = 100;           // Scale (100 = 1.0)
    uint8_t opentype = 0;          // Door behavior type

    // T438 (board B422): the wire door_param, which is a LIFT's travel distance
    // in units. It was parsed into DoorState and read by nothing.
    uint32_t doorParam = 0;
    bool isLift = false;           // opentype 59 with doorParam > 0
    LiftCycle lift;                // the measured travel/dwell/return cycle

    // Animation state
    bool isOpen = false;
    bool isAnimating = false;
    bool usePlaceholder = false;   // True if using placeholder cube (zone data wasn't loaded yet)
    float animProgress = 0.0f;     // 0.0 = closed, 1.0 = open

    // Spinning animation (for opentype 100/105)
    bool isSpinning = false;       // True for spinning objects
    float spinAngle = 0.0f;        // Current spin angle (degrees)

    // T438: the official client closes a door BY ITSELF -- for an ordinary
    // door eqemu transmits no close at all (doors.cpp:138). Seconds left until
    // this door closes on its own; negative means disarmed.
    float autoCloseTimer = -1.0f;

    // Bounding box for interaction
    irr::core::aabbox3df boundingBox;

    // BSP region for occlusion culling
    size_t bspRegion = SIZE_MAX;

    // Scene graph membership (grab/remove pattern like entities)
    bool inSceneGraph = false;

    // Deferred mesh building (progressive loading)
    bool meshBuilt = false;          // false = registered only, true = mesh built
    // Stored creation parameters for deferred building
    std::string name_raw;
    float heading_raw = 0.0f;
    uint32_t incline_raw = 0;
    bool initiallyOpen_raw = false;
};

// Manages door rendering and interaction
class DoorManager {
public:
    DoorManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~DoorManager();

    // Set the current zone data (for finding door meshes)
    void setZone(const std::shared_ptr<S3DZone>& zone);

    // Set constrained texture cache (for memory-managed texture loading after pixel data release)
    void setConstrainedTextureCache(ConstrainedTextureCache* cache) { constrainedCache_ = cache; }

    // Set custom shader material types for door mesh building.
    // P21: added alphaTestRGBAType (H02d Option A), matching
    // ZoneMeshBuilder::setShaderMaterialTypes (zone_geometry.h) — DoorManager
    // delegates all door mesh building to a per-call ZoneMeshBuilder instance
    // (findDoorMesh()), which already classifies masked materials per-texture
    // (ETC1-companion vs direct RGBA/DDS alpha). Before this fix, doors only
    // ever forwarded the 2-arg (solid, alphaTest) pair, so masked RGBA door
    // textures fell back to the ETC1-companion variant with nothing bound on
    // unit 1 — GLES2's opaque-white fallback never discards, so masked door
    // materials rendered fully solid (the same class P20b fixed for tree
    // foliage). Defaults to -1 for callers that don't set it.
    void setShaderMaterialTypes(irr::s32 solid, irr::s32 alphaTest, irr::s32 alphaTestRGBA = -1) {
        shaderMaterialSolid_ = solid;
        shaderMaterialAlphaTest_ = alphaTest;
        shaderMaterialAlphaTestRGBA_ = alphaTestRGBA;
    }

    // Set BSP tree for occlusion region lookup
    void setBspTree(const BspTree* tree) { bspTree_ = tree; }

    // Set occlusion-culled regions (pass nullptr when no occlusion data)
    void setOcclusionCulledRegions(const std::unordered_set<size_t>* regions) { occlusionCulledRegions_ = regions; }

    // Rebuild doors that were created with placeholder meshes (zone data wasn't loaded yet)
    void rebuildPlaceholderDoors();

    // Rebuild a single placeholder door (for progressive one-door-per-frame loading)
    // Returns true if the door was rebuilt, false if skipped or failed
    bool rebuildSingleDoor(uint8_t doorId);

    // Set frustum culler for directional door visibility culling
    void setFrustumCuller(FrustumCuller* culler) { frustumCuller_ = culler; }

    // Set current PVS region for BSP-based door culling
    void setPvsRegion(size_t cameraRegion) { currentPvsRegion_ = cameraRegion; }

    // Retroactively compute BSP regions for doors registered before BSP was available.
    // Called after setBspTree() when BSP arrives via advanceBspPreload().
    void recomputeAllBspRegions();

    // Set region neighbor map for 1-depth PVS expansion (prevents pop-in at boundaries)
    void setRegionNeighbors(const std::unordered_map<size_t, std::vector<size_t>>* neighbors) {
        regionNeighbors_ = neighbors;
    }

    // Create a door visual from server data
    // Returns true if door was created successfully (or skipped for invisible types)
    bool createDoor(uint8_t doorId, const std::string& name,
                    float x, float y, float z, float heading,
                    uint32_t incline, uint16_t size, uint8_t opentype,
                    bool initiallyOpen, uint32_t doorParam = 0);

    // Update door state (open/close animation)
    // userInitiated: true if triggered by user click/keypress, false if from server broadcast
    void setDoorState(uint8_t doorId, bool open, bool userInitiated = false);

    // Animation update (call each frame)
    void update(float deltaTime);

    // T438 (board B423): every lift that moved during the last update(), with
    // the z displacement it applied. PlayerCameraComponent uses this to carry a
    // player standing on the platform -- measured: the official client carries
    // the rider exactly.
    const std::vector<std::pair<uint8_t, float>>& liftMovers() const { return liftMovers_; }

    // Find door at screen position (for click targeting)
    // Returns door_id or 0 if none found
    uint8_t getDoorAtScreenPos(int screenX, int screenY,
                               irr::scene::ICameraSceneNode* camera,
                               irr::scene::ISceneCollisionManager* collisionMgr) const;

    // Find nearest interactable door (for U-key)
    // Returns door_id or 0 if none in range
    uint8_t getNearestDoor(float playerX, float playerY, float playerZ,
                           float playerHeading, float maxDistance = 15.0f) const;

    // Deferred/progressive mesh building
    // Register door metadata without building mesh (for deferred loading)
    bool registerDoor(uint8_t doorId, const std::string& name,
                      float x, float y, float z, float heading,
                      uint32_t incline, uint16_t size, uint8_t opentype,
                      bool initiallyOpen, uint32_t doorParam = 0);
    // Build the mesh for a previously registered door
    bool buildDoorMesh(uint8_t doorId);
    // Check if door mesh is built
    bool isDoorMeshBuilt(uint8_t doorId) const;
    // Get all unbuilt door IDs
    void getUnbuiltDoors(std::vector<uint8_t>& out) const;

    // Check if a specific door exists
    bool hasDoor(uint8_t doorId) const;

    // Get door info (for debugging)
    const DoorVisual* getDoor(uint8_t doorId) const;

    // Get textures that were missing (async pending) during the last findDoorMesh() call
    const std::vector<std::string>& getLastMissingTextures() const { return lastMissingTextures_; }

    // Invalidate cached mesh for a door model (forces rebuild on next findDoorMesh)
    void invalidateMeshCache(const std::string& doorName);

    // Remove all doors (zone change)
    void clearDoors();

    // Get door count
    size_t getDoorCount() const { return doors_.size(); }

    // Set visibility of all door nodes (for profiling)
    void setAllDoorsVisible(bool visible);

    // Every built door's mesh node. T433 found the old "for collision
    // detection" claim on this accessor to be false -- collision does not go
    // through scene nodes at all (see computeDoorCollision above), and the one
    // production caller is diagnostics_debug.cpp's shader-material swap.
    std::vector<irr::scene::IMeshSceneNode*> getDoorSceneNodes() const;

    // --- Collision (T435, board B425) ---------------------------------
    // Before T435 nothing in the tree ever wrote ctx_.doorCollisionData, so
    // every door in every zone was walk-through and non-standable. These three
    // are that missing writer. computeDoorCollision() bakes the door's CURRENT
    // world-space geometry in both settled poses; drainCollisionUpdates()
    // reports the ids that changed (created, mesh built, moved) and the ids
    // that went away since the last drain, so the renderer's map is kept in
    // step without walking every door every frame.
    bool computeDoorCollision(uint8_t doorId, DoorCollisionData& out) const;
    void drainCollisionUpdates(std::vector<uint8_t>& changed,
                               std::vector<uint8_t>& removed);
    // Re-offer every door on the next drain. The renderer clears its own map at
    // three lifecycle points; each of them calls this so the two cannot desync.
    void markAllCollisionDirty();

    // T124: exposes doors_ directly for DoorTextureGuard registration
    // (renderer_zone_merge.h) -- mirrors EntityRenderer::getEntities()'s
    // existing precedent for the same purpose (EntityMeshTextureGuard).
    const std::map<uint8_t, DoorVisual>& getDoors() const { return doors_; }

private:
    // Find matching mesh in zone objects by name
    irr::scene::IMesh* findDoorMesh(const std::string& doorName) const;

    // Calculate open heading from closed heading + incline
    // Uses default 90-degree rotation for standard doors (opentype 0, 5, 56) when incline=0
    float calculateOpenHeading(float closedHeading, uint32_t incline, uint8_t opentype) const;

    // Create a placeholder mesh for doors without models
    // Shape and color are derived from the door name (DOOR→slab, CRATE→cube, BARREL→cylinder)
    irr::scene::IMesh* createPlaceholderMesh(const std::string& doorName = "") const;

    // Check if a BSP region is PVS-visible from the current camera region
    // (includes 1-depth portal neighbor expansion)
    bool isRegionPvsVisible(size_t regionIdx) const;
    bool isRegionPvsVisibleDebug(size_t regionIdx, uint8_t doorId) const;

    // T435: mark a door's collision geometry as needing a re-bake.
    void markCollisionDirty(uint8_t doorId);

    std::map<uint8_t, DoorVisual> doors_;
    std::set<uint8_t> collisionDirty_;    // T435: ids to re-bake on the next drain
    std::set<uint8_t> collisionRemoved_;  // T435: ids the renderer must erase
    std::set<uint8_t> invisibleDoors_;  // Track invisible doors to suppress state update warnings
    std::unordered_map<std::string, irr::scene::IMesh*> doorMeshCache_;  // Cached meshes by uppercase door name
    mutable std::vector<std::string> lastMissingTextures_;  // Textures pending async upload from last findDoorMesh
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::s32 shaderMaterialSolid_ = -1;
    irr::s32 shaderMaterialAlphaTest_ = -1;
    irr::s32 shaderMaterialAlphaTestRGBA_ = -1;  // P21: H02d Option-A main-texture-alpha variant
    std::shared_ptr<S3DZone> currentZone_;
    ConstrainedTextureCache* constrainedCache_ = nullptr;
    const BspTree* bspTree_ = nullptr;
    FrustumCuller* frustumCuller_ = nullptr;
    const std::unordered_set<size_t>* occlusionCulledRegions_ = nullptr;
    size_t currentPvsRegion_ = SIZE_MAX;
    const std::unordered_map<size_t, std::vector<size_t>>* regionNeighbors_ = nullptr;
    // T438: the animation constants moved to door_motion.h, where each one
    // carries the oracle session that produced it. The two that stood here --
    // ANIM_SPEED 2.0 ("~0.5 seconds") and SPIN_SPEED 42.35 ("~180 degrees per
    // 4.25 seconds") -- were both invented, and both wrong: the rotation is
    // 0.95 s opening / 0.60 s closing and the spin is a FULL turn in 4.267 s.
    std::vector<std::pair<uint8_t, float>> liftMovers_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DOOR_MANAGER_H
