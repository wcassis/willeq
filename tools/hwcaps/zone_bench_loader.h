#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <irrlicht.h>

// Forward declarations
namespace EQT::Graphics {
    class S3DLoader;
    struct S3DZone;
    class WldLoader;
    struct ZoneGeometry;
    class ZoneShaderManager;
    class TextureAtlas;
    class ConstrainedTextureCache;
    class FrustumCuller;
    class PortalSystem;
    class SoftwareOcclusionCuller;
    class RaceModelLoader;
    struct BspTree;
    struct ConstrainedRendererConfig;
}

namespace ZoneBench {

struct BenchConfig {
    std::string zoneName = "qeynos2";
    std::string eqPath;          // Path to EQ client directory
    std::string atlasPath;       // Path to atlas directory (optional)
    int width = 800;
    int height = 600;
    bool useDRM = true;
    bool useAtlas = true;        // Use texture atlas (can be toggled for A/B)
    int entityCount = 0;         // Number of static entities to place in scene
};

// Per-region data stored for rendering
struct RegionData {
    irr::scene::IMesh* mesh = nullptr;
    irr::core::vector3df position;  // Irrlicht coords (EQ x, EQ z, EQ y)
    // Bounding box in EQ coords (for frustum/PVS culling)
    float eqMinX, eqMinY, eqMinZ;
    float eqMaxX, eqMaxY, eqMaxZ;
};

// Draw entry for material-sorted rendering
struct DrawEntry {
    uint32_t materialKey;
    float distanceSq;
    irr::scene::IMeshBuffer* buffer;
    irr::core::matrix4 worldMat;
};

// All loaded zone data needed for benchmarking
struct ZoneBenchData {
    // Irrlicht device/driver (owned by us)
    irr::IrrlichtDevice* device = nullptr;
    irr::video::IVideoDriver* driver = nullptr;
    irr::scene::ISceneManager* smgr = nullptr;

    // Zone data
    std::shared_ptr<EQT::Graphics::S3DZone> zone;
    std::shared_ptr<EQT::Graphics::BspTree> bspTree;

    // Per-region meshes
    std::map<size_t, RegionData> regions;

    // Shader system
    std::unique_ptr<EQT::Graphics::ZoneShaderManager> zoneShader;

    // Texture systems
    std::shared_ptr<EQT::Graphics::TextureAtlas> zoneAtlas;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache;

    // Entity models
    std::unique_ptr<EQT::Graphics::RaceModelLoader> raceModelLoader;

    // Placed entities (static, for benchmarking)
    // Entity priority tiers (higher = more important to keep as full mesh)
    enum class EntityPriority : uint8_t {
        Idle = 0,           // Ambient NPCs, shopkeepers
        Default = 1,        // Everything else
        RecentInteract = 2, // NPCs you've interacted with recently
        Friend = 3,         // Friends list (future)
        Guild = 4,          // Guild members
        Raid = 5,           // Raid members
        Group = 6,          // Group members
        Target = 7,         // Current target / aggro'd mobs
    };

    struct PlacedEntity {
        irr::scene::IMesh* mesh = nullptr;
        irr::core::vector3df position;  // Irrlicht coords
        float eqX, eqY, eqZ;           // EQ coords (for culling)
        EntityPriority priority = EntityPriority::Default;
    };
    std::vector<PlacedEntity> entities;

    // Adaptive LOD config
    int compositeEntityBudget = 55;        // Max composite entities per frame (configurable)
    float aggressiveFactor = 1.5f;         // Overshoot: billboard 50% more than needed
    int recoveryFrames = 0;                // Frames under budget before recovering one entity
    int recoveryThreshold = 15;            // Consecutive under-budget frames to start recovery

    // Visibility
    std::unique_ptr<EQT::Graphics::FrustumCuller> frustumCuller;
    std::unique_ptr<EQT::Graphics::PortalSystem> portalSystem;
    std::unique_ptr<EQT::Graphics::SoftwareOcclusionCuller> occlusionCuller;

    // Zone bounds (EQ coords)
    float boundsMinX = 0, boundsMinY = 0, boundsMinZ = 0;
    float boundsMaxX = 0, boundsMaxY = 0, boundsMaxZ = 0;

    // Cleanup
    ~ZoneBenchData();
};

// Load zone and prepare all GPU resources.
// Returns null on failure.
std::unique_ptr<ZoneBenchData> loadZone(const BenchConfig& config);

// Collect visible draw entries for a given camera position (EQ coords).
// Returns draw entries sorted by material key, with distance as secondary sort.
// Camera direction needed for occlusion culling
struct CameraState {
    float eqX, eqY, eqZ;
    float dirX, dirY, dirZ;
    float fovRadV, aspect;
};

void collectDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    bool materialSort,
    bool portalCull,
    bool frustumCull,
    bool occlusionCull,
    bool frontToBack,
    int maxDraws,
    int maxPolys,
    std::vector<DrawEntry>& outEntries,
    int& outRegionsVisible,
    int& outRegionsDrawn,
    int& outRegionsOccluded);

// Place entities at evenly-spaced positions along a camera path.
// Positions are in EQ coords; entities face random directions.
void placeEntitiesAlongPath(ZoneBenchData& data,
                             const std::vector<glm::vec3>& pathPositions);

// Collect entity draw entries for visible entities near the camera.
// Appends to outEntries (call after zone draw collection).
void collectEntityDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    std::vector<DrawEntry>& outEntries,
    int& outEntityDrawCalls,
    int& outEntityPolys);

// Batched entity rendering: merge same-material entity parts into single draws.
// Bakes world transforms into vertex data, groups by material, one draw per bucket.
// Returns draw call count and poly count.
int renderBatchedEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys);

// Composite entity rendering: simulate 1-draw-per-entity by merging all body
// parts into a single mesh buffer per entity. Uses the first texture as the
// "composite" stand-in. This models what pre-baked composite textures achieve.
// Returns draw call count and poly count.
int renderCompositeEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys);

// Direct GL entity rendering: bypasses Irrlicht's setMaterial/drawMeshBuffer
// and calls glBindTexture/glDrawElements directly with minimal state tracking.
// Per-body-part mode (same draws as unbatched, less overhead per draw).
int renderDirectGLEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys);

// Direct GL composite: 1 draw per entity via direct GL, bypassing Irrlicht.
// Merges body parts per entity, uses drawVertexPrimitiveList equivalent via raw GL.
int renderDirectGLCompositeEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys);

// LOD entity rendering: composite meshes for near entities, batched billboard
// quads for far entities. One draw for all billboards.
// Returns draw call count and poly count.
int renderLodEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    float billboardDist,
    bool frustumCull,
    int& outEntityPolys,
    int& outTier1Count,
    int& outTier2Count);

// Place entities in a tight cluster at a fixed position along the path
// (simulates an EQ "train" running toward the camera).
void placeEntitiesTrain(ZoneBenchData& data,
                         const glm::vec3& centerPos);

// Adaptive LOD entity rendering: dynamically swaps entities between composite
// mesh and billboard based on frame budget. Tracks rolling frame time and
// adjusts billboard count to maintain target FPS. Prioritizes keeping
// important entities (target, group) as full meshes.
// lastFrameMs: previous frame's total time (for budget tracking)
// Returns draw call count and poly count.
int renderAdaptiveLodEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    float lastFrameMs,
    int& outEntityPolys,
    int& outTier1Count,
    int& outTier2Count);

// Analyze portals: for each portal, compute geometry contact ratio
// (fraction of portal perimeter that touches nearby region geometry).
// Outputs a report to stdout and optionally to a CSV file.
void analyzePortals(ZoneBenchData& data, const std::string& csvFile = "");

// Occluder-based portal detection: for each BSP boundary between region pairs,
// project nearby geometry onto the split plane, compute wall coverage,
// and identify gaps (low-coverage areas) as portal openings.
void detectPortalsFromOccluders(ZoneBenchData& data, const std::string& csvFile = "");

// Find a good surface-level position in the zone for camera seeding.
// Returns the center of the region with the most polygons in the top
// quartile of Z heights (avoids underground regions).
void findSurfacePosition(const ZoneBenchData& data,
                          float& outX, float& outY, float& outZ);

} // namespace ZoneBench
