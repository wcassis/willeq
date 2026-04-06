#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
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
    class ConstrainedTextureCache;
    class FrustumCuller;
    class PortalSystem;
    class SoftwareOcclusionCuller;
    class RaceModelLoader;
    class EQE1Loader;
    class EQAnimatedMeshSceneNode;
    struct BspTree;
    struct ConstrainedRendererConfig;
}

namespace ZoneBench {

struct BenchConfig {
    std::string zoneName = "qeynos2";
    std::string eqPath;          // Path to EQ client directory
    std::string eqe1Path;        // Path to EQE1 texture directory (optional)
    int width = 800;
    int height = 600;
    bool useDRM = true;
    int entityCount = 0;         // Number of static entities to place in scene
    bool animate = false;        // Enable skeletal animation on entities
    bool compositeEntities = false; // Use composite textures for entities (single draw per entity)
    bool loadObjects = false;    // Load zone objects (trees, lamps, etc.)
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
    irr::s32 materialOverride = -1;  // If >= 0, override buffer's MaterialType at draw time
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

    // Animated entity scene nodes (one per entity when --animate is enabled)
    // These own per-instance SkeletalAnimator + vertex buffers
    std::vector<EQT::Graphics::EQAnimatedMeshSceneNode*> animatedNodes;

    // Adaptive LOD state and config
    struct AdaptiveLodConfig {
        int maxEntitiesPerScene = 55;      // Hard cap: never render more composites than this
        float frameBudgetMs = 33.3f;       // Target frame time (30 FPS)
        int hysteresisFrames = 10;         // Stay billboarded for N frames after switching
        int rollingWindowSize = 30;        // Frames for rolling average
        float safetyMargin = 0.85f;        // Use 85% of budget to leave headroom
    } adaptiveLodConfig;

    struct AdaptiveLodState {
        // Rolling average tracking
        std::deque<float> recentFrameMs;       // Recent total frame times
        std::deque<float> recentEntityMs;      // Recent entity render times
        std::deque<int>   recentEntityCounts;  // Recent composite entity counts
        float avgPerEntityMs = 0.5f;           // Rolling average ms per composite entity
        float avgBaseZoneMs = 10.0f;           // Rolling average non-entity frame cost

        // Per-entity hysteresis: entityIndex → frames remaining as billboard
        std::map<int, int> billboardTimers;

        void updateRollingAverage(float totalMs, float entityMs, int entityCount, int windowSize) {
            recentFrameMs.push_back(totalMs);
            recentEntityMs.push_back(entityMs);
            recentEntityCounts.push_back(entityCount);
            while (static_cast<int>(recentFrameMs.size()) > windowSize) {
                recentFrameMs.pop_front();
                recentEntityMs.pop_front();
                recentEntityCounts.pop_front();
            }
            // Compute rolling averages
            float sumEntityMs = 0, sumBaseMs = 0;
            int sumEntities = 0;
            for (size_t i = 0; i < recentFrameMs.size(); ++i) {
                sumEntityMs += recentEntityMs[i];
                sumBaseMs += (recentFrameMs[i] - recentEntityMs[i]);
                sumEntities += recentEntityCounts[i];
            }
            int n = static_cast<int>(recentFrameMs.size());
            if (n > 0) avgBaseZoneMs = sumBaseMs / n;
            if (sumEntities > 0) avgPerEntityMs = sumEntityMs / sumEntities;
        }

        void tickHysteresis() {
            for (auto it = billboardTimers.begin(); it != billboardTimers.end(); ) {
                if (--it->second <= 0)
                    it = billboardTimers.erase(it);
                else
                    ++it;
            }
        }

        bool isForcedBillboard(int entityIdx) const {
            return billboardTimers.count(entityIdx) > 0;
        }

        void markBillboard(int entityIdx, int frames) {
            billboardTimers[entityIdx] = frames;
        }

        // RTT impostor cache: entityIndex → dedicated FBO texture (both render target and billboard)
        std::map<int, irr::video::ITexture*> impostorTextures;
        int impostorSize = 256;  // Width/height of impostor textures

        // Rate-limited composite limit (smoothed to prevent oscillation)
        int smoothedCompositeLimit = -1;  // -1 = uninitialized
        static constexpr int maxLimitChangePerFrame = 2;
    } adaptiveLodState;

    // Placed zone objects (trees, lamps, fences, etc.)
    struct PlacedObject {
        irr::scene::IMesh* mesh = nullptr;
        irr::core::matrix4 worldTransform;
        float eqX, eqY, eqZ;  // EQ coords for culling
        std::string name;      // Object definition name (e.g., "TREE6")
        // World-space AABB in EQ coords (for frustum/PVS culling)
        float eqMinX, eqMinY, eqMinZ;
        float eqMaxX, eqMaxY, eqMaxZ;
        // BSP regions this object overlaps (precomputed at load time)
        std::vector<size_t> bspRegions;
    };
    std::vector<PlacedObject> objects;

    // Adaptive lighting config
    bool adaptiveLighting = false;
    int perPixelEntityBudget = 15;         // Max entities with per-pixel lighting
    int perPixelZoneBudget = 20;           // Max zone regions with per-pixel lighting
    irr::s32 materialPerPixelSolid = -1;   // Per-pixel shader material type
    irr::s32 materialPerPixelAlpha = -1;   // Per-pixel alpha test material type
    irr::s32 materialLWSolid = -1;         // Lightweight (per-vertex) shader material type
    irr::s32 materialLWAlpha = -1;         // Lightweight alpha test material type

    // Visibility
    std::unique_ptr<EQT::Graphics::FrustumCuller> frustumCuller;
    std::unique_ptr<EQT::Graphics::PortalSystem> portalSystem;
    std::unique_ptr<EQT::Graphics::SoftwareOcclusionCuller> occlusionCuller;

    // Zone bounds (EQ coords)
    float boundsMinX = 0, boundsMinY = 0, boundsMinZ = 0;
    float boundsMaxX = 0, boundsMaxY = 0, boundsMaxZ = 0;

    // EQE1 loaders (owned, kept alive for texture cache references)
    std::vector<std::unique_ptr<EQT::Graphics::EQE1Loader>> eqe1Loaders;

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
    bool pvsCull,
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
    bool occlusionCull,
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
    int& outTier2Count,
    float& outEntityMs);

// Collect object draw entries for visible zone objects near the camera.
// Uses PVS culling (precomputed region overlap) + AABB frustum culling +
// software occlusion culling (tests against occlusion buffer built by zone pass).
// Appends to outEntries (call after zone draw collection).
void collectObjectDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool pvsCull,
    bool frustumCull,
    bool occlusionCull,
    std::vector<DrawEntry>& outEntries,
    int& outObjectDrawCalls,
    int& outObjectPolys);

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
