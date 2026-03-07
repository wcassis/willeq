#ifndef EQT_GRAPHICS_DOOR_MANAGER_H
#define EQT_GRAPHICS_DOOR_MANAGER_H

#include <irrlicht.h>
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

    // Animation state
    bool isOpen = false;
    bool isAnimating = false;
    bool usePlaceholder = false;   // True if using placeholder cube (zone data wasn't loaded yet)
    float animProgress = 0.0f;     // 0.0 = closed, 1.0 = open

    // Spinning animation (for opentype 100/105)
    bool isSpinning = false;       // True for spinning objects
    float spinAngle = 0.0f;        // Current spin angle (degrees)

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

    // Set custom shader material types for door mesh building
    void setShaderMaterialTypes(irr::s32 solid, irr::s32 alphaTest) {
        shaderMaterialSolid_ = solid;
        shaderMaterialAlphaTest_ = alphaTest;
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
                    bool initiallyOpen);

    // Update door state (open/close animation)
    // userInitiated: true if triggered by user click/keypress, false if from server broadcast
    void setDoorState(uint8_t doorId, bool open, bool userInitiated = false);

    // Animation update (call each frame)
    void update(float deltaTime);

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
                      bool initiallyOpen);
    // Build the mesh for a previously registered door
    bool buildDoorMesh(uint8_t doorId);
    // Check if door mesh is built
    bool isDoorMeshBuilt(uint8_t doorId) const;
    // Get door IDs in given BSP regions where meshBuilt == false
    void getDoorsInRegions(const std::unordered_set<size_t>& regions, std::vector<uint8_t>& out) const;
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

    // Get all door scene nodes for collision detection
    std::vector<irr::scene::IMeshSceneNode*> getDoorSceneNodes() const;

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

    std::map<uint8_t, DoorVisual> doors_;
    std::set<uint8_t> invisibleDoors_;  // Track invisible doors to suppress state update warnings
    std::unordered_map<std::string, irr::scene::IMesh*> doorMeshCache_;  // Cached meshes by uppercase door name
    mutable std::vector<std::string> lastMissingTextures_;  // Textures pending async upload from last findDoorMesh
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::s32 shaderMaterialSolid_ = -1;
    irr::s32 shaderMaterialAlphaTest_ = -1;
    std::shared_ptr<S3DZone> currentZone_;
    ConstrainedTextureCache* constrainedCache_ = nullptr;
    const BspTree* bspTree_ = nullptr;
    FrustumCuller* frustumCuller_ = nullptr;
    const std::unordered_set<size_t>* occlusionCulledRegions_ = nullptr;
    size_t currentPvsRegion_ = SIZE_MAX;
    const std::unordered_map<size_t, std::vector<size_t>>* regionNeighbors_ = nullptr;
    // Animation speed (complete animation in ~0.5 seconds)
    static constexpr float ANIM_SPEED = 2.0f;

    // Spinning speed (~180 degrees per 4.25 seconds = 42.35 deg/sec)
    static constexpr float SPIN_SPEED = 42.35f;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DOOR_MANAGER_H
