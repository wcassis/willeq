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
    class ArchiveTextureSource;
    class FrustumCuller;
    class PortalSystem;
    class SoftwareOcclusionCuller;
    class RaceModelLoader;
    class EQE1Loader;
    class EQE1TextureIndex;
    class EQAnimatedMeshSceneNode;
    struct BspTree;
    struct ConstrainedRendererConfig;
}

namespace ZoneBench {

class ImpostorCache;  // B952: zone_bench_impostor.h

struct BenchConfig {
    std::string zoneName = "qeynos2";
    std::string eqPath;          // Path to EQ client directory
    std::string eqe1Path;        // Path to EQE1 texture directory (optional)
    int width = 800;
    int height = 600;
    bool useDRM = true;
    int entityCount = 0;         // Number of static entities to place in scene
    // P08f: explicit EQ race IDs to cycle entities through, overriding the
    // default hardcoded player-race set {1..12} — lets a caller place
    // specific zone-native NPC race models (e.g. BAT/ORC/RAT/ZOM) for
    // composite-vs-baseline spot-checks. Empty = default behavior (byte-
    // identical to pre-P08f invocations that don't pass --test-races).
    std::vector<uint16_t> testRaceIds;
    // B952: genders to cross with the race list (race x gender variants).
    // Empty = {0}, the pre-B952 behaviour.
    std::vector<uint8_t> genders;
    bool animate = false;        // Enable skeletal animation on entities
    bool compositeEntities = false; // Use composite textures for entities (single draw per entity)
    bool loadObjects = false;    // Load zone objects (trees, lamps, etc.)
    // B936: pose every vertex-animated (keyframe) object at keyframe
    // (N mod its frame count) before upload -- a fixed, deterministic
    // animation time for pixel comparisons. -1 = the geometry's own pose (no
    // keyframe applied), which is what the benchmark always drew before.
    int objectAnimFrame = -1;
    // H02d: shader debug mode (0=off, >0=ZoneShaderManager debug mode; see
    // zone_shader.cpp FRAGMENT_SHADER_DEBUG_SRC for the mode list). When set,
    // all per-pixel zone/object draw entries render via the debug
    // visualization program instead of their normal material — mirrors the
    // renderer's /shaderdebug swapToDebugShader for offline diagnostic capture.
    int shaderDebugMode = 0;
    // P04: log [LOAD-MEM-AUDIT] RSS + pool samples at each load step boundary
    bool loadMemAudit = false;
    // P11: run-time occluder budget override (maxOccluderRegions). -1 = prior
    // hardcoded 16 (production's default too); a large value approximates
    // "uncapped" for the raised-budget variants.
    int occluderBudget = -1;
    // P46c: benchmark-only opt-out from applyEntityShaderMaterialPromotion()
    // (P46b). When true, entity mesh buffers are left on whichever material
    // mesh_building.cpp/Irrlicht assigned by default (EMT_SOLID /
    // EMT_TRANSPARENT_ALPHA_CHANNEL_REF) instead of being promoted to the
    // custom per-pixel-lit shader family. Default false (promotion on,
    // unchanged behavior) -- exists solely to produce a lighting-neutral
    // bless pair isolating the P37/P46A alpha cutout. See
    // zone_bench_loader.cpp's applyEntityShaderMaterialPromotion() call site.
    bool noEntityShaderPromotion = false;
    // B944: override ConstrainedRendererConfig::textureMemoryBytes (the
    // texture cache's eviction ceiling) for this run only. 0 = the preset's
    // own value. A test knob for driving the cache past its ceiling; it never
    // changes the shipped preset.
    size_t textureMemoryBytesOverride = 0;
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

    // H02d: debug visualization material type (-1 = off). When >= 0, draw
    // entry collection overrides any per-pixel custom shader material
    // (MaterialType >= zoneShader->getMaterialTypeSolid()) to this type.
    irr::s32 shaderDebugMaterial = -1;

    // Q03 amendment: the player light's boosted color + attenuation, as the
    // production shader receives them (renderer_sim_bridge.cpp:745-761 —
    // diffuse color x3 boost). setupLighting() computes these once; the
    // per-frame camera update mirrors them into point-light slot 0 so the
    // LIGHTWEIGHT (per-vertex) path carries the player light exactly like
    // production. Color (0,0,0) = no player light (level 0).
    float playerLightColor[3] = {0.0f, 0.0f, 0.0f};
    float playerLightAtten[3] = {1.0f, 0.0f, 0.0f};

    // Texture systems. B944: the restore source for decode-path textures the
    // cache evicts (the zone's own archives); declared first so it outlives
    // the cache.
    std::unique_ptr<EQT::Graphics::ArchiveTextureSource> textureSource;
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
        // B952: the model variant, the key of a shared impostor.
        uint16_t raceId = 0;
        uint8_t gender = 0;
        uint8_t textureVariant = 0;
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
        // B952: distance tier. An entity beyond it is always an impostor and
        // takes no composite slot; the cap/frame-budget limit applies inside
        // it. 0 = off (the pre-B952 behaviour: the limit alone decides).
        float tierDist = 0.0f;
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

        // B952: the impostor textures (zone_bench_impostor.h). Was a
        // map<entity, dedicated RTT> at a fixed 256^2 before B952.
        std::shared_ptr<ImpostorCache> impostors;

        // B952: this frame's tier plan, computed once (planAdaptiveLod) so
        // the impostor pre-pass before beginScene() and the draw agree.
        struct PlannedEntity {
            int entityIdx;
            bool billboard;
        };
        std::vector<PlannedEntity> plan;
        bool planValid = false;

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
    // P06: zone budget mode -- mirrors
    // EQT::Graphics::ConstrainedRendererConfig::AdaptiveLightingBudgetMode so
    // benchmark numbers predict production behavior. Count (default) = flat
    // region-count budget (unchanged). Area = accumulate estimated projected
    // screen area nearest-first, switch to lightweight once cumulative area
    // exceeds zonePPAreaBudgetFactor * screen area. Entity budget is unaffected
    // (still count-based regardless of this mode).
    bool zonePPAreaMode = false;
    float zonePPAreaBudgetFactor = 0.75f;  // see P06 config header comment for rationale
    // P07: hybrid floor -- first zonePPAreaFloor regions (nearest-first) always
    // per-pixel regardless of estimated area, in area mode only. Mirrors
    // EQT::Graphics::ConstrainedRendererConfig::adaptiveLightingAreaPPFloor.
    int zonePPAreaFloor = 1;
    // P07d: radius mode -- mirrors
    // EQT::Graphics::ConstrainedRendererConfig::AdaptiveLightingBudgetMode::Radius.
    // Demotes a region iff the closest point of its world AABB to the player
    // light position is beyond the light's effective attenuation range R
    // (computed from the SAME zoneShader player-light position/attenuation
    // used to render, times zonePPRadiusMargin). Mutually exclusive with
    // zonePPAreaMode (--pp-budget-mode count|area|radius); perPixelZoneBudget's
    // numeric value is unused in this mode (only its on/off gate applies).
    bool zonePPRadiusMode = false;
    float zonePPRadiusMargin = 1.0f;
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

    // T284: name -> owning-pack index (data/eqe1_texture_index.json), loaded
    // only for --composite-entities runs. It is what replaced the composite
    // directory scan: a sheet name resolves to exactly one pack, opened on
    // demand by the resolver installed on the texture cache -- the same rule
    // production uses (IrrlichtRenderer::openArchiveContaining).
    std::unique_ptr<EQT::Graphics::EQE1TextureIndex> eqe1Index;
    // Packs opened by that resolver, and their mapped bytes (the T284
    // before/after metric). Not a subset of a scan any more, so counted here.
    size_t eqe1ResolverOpens = 0;
    size_t eqe1ResolverBytes = 0;

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
    int& outRegionsOccluded,
    // P07d: optional radius-mode reporting (see refactor/P07d_radius_demotion.md).
    // Non-null only when the caller wants the computed R + demoted-region
    // fraction for this frame (e.g. for --csv reporting). Left untouched
    // (not written) when zonePPRadiusMode is off.
    float* outLightRadius = nullptr,
    int* outRegionsDemotedRadius = nullptr,
    // P08: optional per-phase CPU timing (steady_clock, no syscalls --
    // near-zero overhead). Non-null only when the caller wants the
    // collect/cull-vs-sort split for CSV reporting; left untouched
    // (not written) when null. outCollectCullMs covers everything in this
    // function EXCEPT the final material-key sort; outSortMs covers only
    // the std::sort call (0 if materialSort is false).
    float* outCollectCullMs = nullptr,
    float* outSortMs = nullptr,
    // P08 (--draw-histogram, one-shot diagnostic, not per-frame): when
    // non-null, outDrawsPerRegion receives one entry per DRAWN region
    // (region visitation order, i.e. BEFORE the material sort reorders
    // outEntries) counting how many draw entries that region contributed;
    // outPolysPerDraw receives one entry per draw entry (poly count of
    // that entry's mesh buffer), same order as outEntries pre-sort. Both
    // left empty (not cleared/touched) when null -- zero overhead for
    // ordinary timed runs.
    std::vector<int>* outDrawsPerRegion = nullptr,
    std::vector<int>* outPolysPerDraw = nullptr);

// Place entities at evenly-spaced positions along a camera path.
// Positions are in EQ coords; entities face random directions.
void placeEntitiesAlongPath(ZoneBenchData& data,
                             const std::vector<glm::vec3>& pathPositions);

// Collect entity draw entries for visible entities near the camera.
// Appends to outEntries (call after zone draw collection).
// P08b: outEntitiesVisible counts distinct entities surviving cull (vs.
// outEntityDrawCalls, which counts mesh-buffer draw entries per entity) --
// the calibration target for placed-N -> visible-per-frame.
void collectEntityDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    bool occlusionCull,
    std::vector<DrawEntry>& outEntries,
    int& outEntityDrawCalls,
    int& outEntityPolys,
    int& outEntitiesVisible);

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
// B952: the tier decision is planAdaptiveLod(); renderAdaptiveLodEntities()
// uses a plan made earlier in the frame (the impostor pre-pass) or makes one.
void planAdaptiveLod(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    float lastFrameMs);

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

// P08 design point 6: occlusion headroom ground-truth diagnostic -- declared
// and implemented in zone_bench_histogram.h/.cpp (file-size budget; only
// needs ZoneBenchData's public members, see there for the full comment).

// B914: both portal diagnostics below read the per-region SOURCE geometry,
// which loadZone() now releases at its end exactly as the client does
// (S3DZone::releaseSourceAfterLoad). Their callers in gles2_zone_benchmark.cpp
// are commented out; reviving one means running it inside loadZone(), before
// that release, not afterwards.
//
// Analyze portals: for each portal, compute geometry contact ratio
// (fraction of portal perimeter that touches nearby region geometry).
// Outputs a report to stdout and optionally to a CSV file.
void analyzePortals(ZoneBenchData& data, const std::string& csvFile = "");

// Occluder-based portal detection: for each BSP boundary between region pairs,
// project nearby geometry onto the split plane, compute wall coverage,
// and identify gaps (low-coverage areas) as portal openings.
void detectPortalsFromOccluders(ZoneBenchData& data, const std::string& csvFile = "");

// A candidate camera-seed position: the center of a zone region (EQ coords)
// plus that region's polygon count (ranking key).
struct SurfaceCandidate {
    float x, y, z;
    int polys;
};

// T242: rank surface-level regions for camera seeding. Returns up to
// maxCandidates region centers, most polygons first, restricted to regions
// whose center Z is at or above the polygon-weighted median region Z (so
// sky/far-scenery outliers hundreds of units up cannot drag the threshold
// above the playable area, and underground regions stay excluded).
std::vector<SurfaceCandidate> findSurfaceCandidates(const ZoneBenchData& data,
                                                    size_t maxCandidates);

// Find a good surface-level position in the zone for camera seeding:
// the first (largest) findSurfaceCandidates() entry.
void findSurfacePosition(const ZoneBenchData& data,
                          float& outX, float& outY, float& outZ);

} // namespace ZoneBench
