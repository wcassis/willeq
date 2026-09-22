// Draw-ordering entries, texture-guard forward declarations and memory-report structs.
// Extracted verbatim from the umbrella renderer header by
// refactor/J17_irrlicht_renderer_header_split.md. Move-only: no line of the
// extracted block was edited. The three timer-query free-function
// declarations in that header's preamble deliberately did not travel; see
// that plan for why.

#ifndef EQT_GRAPHICS_RENDERER_DRAW_TYPES_H
#define EQT_GRAPHICS_RENDERER_DRAW_TYPES_H


#include <irrlicht.h>
#include <atomic>
#include <chrono>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <utility>
#include <optional>
#include <glm/glm.hpp>
#include "client/state/entity_pose_state.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/camera_controller.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/door_manager.h"
#include "client/graphics/animated_texture_manager.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/graphics_archive_index.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/tree_wind_controller.h"
#include "client/graphics/tree_identifier.h"
#include "client/graphics/weather_system.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/texture_atlas.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_texture_index.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/boids_manager.h"
#include "client/graphics/environment/tumbleweed_manager.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/display_settings.h"  // U07c2: extracted from deleted options_window.h
#include "client/graphics/background_work_queue.h"
#include "client/graphics/background_thread_pool.h"
#include "client/graphics/simulation_worker.h"
#include "client/input/hotkey_manager.h"

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"
#endif

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
#include "client/graphics/gles2_egl_helper.h"
#endif


// Collision map (included for HCMap::Triangle in member variable)
#include "client/hc_map.h"

// Forward declaration for navmesh pathfinder
class PathfinderNavmesh;

// Forward declaration for zone lines
namespace EQT { struct ZoneLineBoundingBox; }

// Forward declarations for inventory UI and bridge
namespace eqt {
// U07c3: WindowManager deleted
namespace inventory { class InventoryManager; }
namespace bridge { class GameStateBridge; }
}

// Forward declaration for spell visual effects
namespace EQ { class SpellVisualFX; }
namespace eqt { namespace ui { class ChatMessageBuffer; class CharacterModelView; } }
namespace EQT { namespace Graphics { class TextBatch; class UIAtlas; class UIRenderer; class StaticUIInput; } }
// U03: included directly for layout struct and panel data structs (small, no deps)
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"

// Forward declaration for constrained texture cache
namespace EQT { namespace Graphics { class ConstrainedTextureCache; } }
// T300: the first-person camera's constants and its pure placement math.
#include "client/graphics/first_person_camera.h"

namespace EQT {
namespace Graphics {

// Entry for front-to-back sorted zone drawing
struct SortedRegionEntry {
    size_t regionIdx;
    float distanceSq;
    irr::scene::IMeshSceneNode* node;
};

// Forward declaration -- full definition + implementation in
// renderer_zone_merge.h/.cpp (P10 cross-region zone draw merge).
class ZoneMergeBuffers;
// Forward declaration -- full definition + implementation in
// renderer_zone_merge.h/.cpp (P10b root-cause fix: keeps region mesh
// materials' texture pointers valid across ConstrainedTextureCache
// evictions even when the owning node is detached from the scene graph).
class RegionMeshTextureGuard;
// Forward declaration -- full definition in renderer_zone_merge.h (P10b:
// generic single-raw-pointer eviction guard; used here for
// softwareCursorTexture_, a separate pre-existing dangling-pointer bug
// found while verifying P10b's fix).
class SingleTexturePointerGuard;
// Forward declaration -- full definition in renderer_zone_merge.h (P17:
// entity-mesh sibling of RegionMeshTextureGuard, see its own doc comment).
class EntityMeshTextureGuard;
// Forward declaration -- full definition in renderer_zone_merge.h (T123:
// object-node sibling of RegionMeshTextureGuard/EntityMeshTextureGuard, see
// its own doc comment).
class ObjectNodeTextureGuard;
// Forward declarations -- full definitions in renderer_zone_merge.h (T124:
// doors_ sibling of ObjectNodeTextureGuard, and the generic forwarding
// guard used for the singleton-subsystem holders -- see their own doc
// comments).
class DoorTextureGuard;
class ForwardingTextureGuard;

// Entry for material-sorted zone draw list (mesh buffer level)
struct ZoneDrawEntry {
    uint32_t materialKey;   // (materialType << 16) | textureIdHash
    float distanceSq;
    irr::scene::IMeshBuffer* buffer;
    irr::core::matrix4 worldMat;
    irr::s32 materialOverride = -1;  // If >= 0, override buffer's MaterialType at draw time
    // T504: POOLED zone draw -- when set, `buffer` is null and the geometry and
    // material come from ZoneMergeBuffers via this slice.
    const struct ZoneSlice* slice = nullptr;
};

// External subsystem memory data passed into getMemoryReport()
struct MemoryReportInput {
    // Audio
    bool audioAvailable = false;
    size_t sfxCacheBytes = 0;
    size_t soundBufferCacheBytes = 0;
    size_t soundBufferCacheMaxBytes = 0;
    size_t soundFontEstimateBytes = 0;
    size_t musicDecodedBytes = 0;
    size_t zoneEmitterCount = 0;
    size_t activeEmitterCount = 0;
    // T130: real-time audio callback timing -- see event_bus.h's
    // DiagnosticsMemoryReportData field comments.
    uint64_t audioCallbackCount = 0;
    uint64_t audioLateCallbackCount = 0;
    int64_t audioWorstCallbackDeltaUs = 0;
    // T131: peak concurrent TSF voices / dropped-note count -- see
    // event_bus.h's DiagnosticsMemoryReportData field comments.
    int audioPeakVoices = 0;
    uint64_t audioNoteOnFailures = 0;

    // Network
    struct ConnectionInfo {
        std::string name;
        uint64_t recvBytes = 0;
        uint64_t sentBytes = 0;
        uint64_t avgPing = 0;
    };
    std::vector<ConnectionInfo> connections;

    // Game Data
    size_t entityCount = 0;
    size_t entityEstimateBytes = 0;
    size_t doorCount = 0;
    size_t doorEstimateBytes = 0;
    size_t spellDbCount = 0;
    size_t spellDbEstimateBytes = 0;

    // Audio PFS archive cache
    size_t audioPfsArchiveBytes = 0;

    // Process (from /proc/self/statm)
    size_t processRssBytes = 0;
    size_t processVmBytes = 0;

    // Process memory breakdown (from /proc/self/smaps_rollup)
    size_t sharedLibBytes = 0;     // Shared clean + shared dirty mapped files
    size_t anonBytes = 0;          // Anonymous (heap + mmap anon)
    size_t stackBytes = 0;         // Thread stacks

    // T127: glibc mallinfo2() allocator bookkeeping -- see event_bus.h's
    // DiagnosticsMemoryReportData field comments for what these measure and
    // why they were added (T106 named this exact gap: allocator
    // fragmentation is real and large but had "no dedicated counter").
    size_t mallocArenaBytes = 0;   // Total committed arena (brk + mmap'd arena blocks)
    size_t mallocInUseBytes = 0;   // Subset actually holding live allocations
};

// T69: structured per-subsystem memory accounting — the SAME tally
// getMemoryReport() formats into human-readable lines, exposed as named
// fields instead of parsed strings. Added so CI (scripts/ci/visual_gate.py,
// via tools/hwcaps/gles2_zone_benchmark.cpp) can consume the real accounting
// directly rather than duplicating it or scraping formatted log lines —
// two independent tallies of the same memory is exactly the drift this
// project keeps re-discovering (T57 slot geometry, T60 bone logic).
// getMemoryReport() now computes this struct once and formats lines from
// it; there is exactly one place that adds up bytes per category.
struct MemoryReportBreakdown {
    // --- Graphics (computed internally; renderer state, not ext input) ---
    size_t textureCacheBytesUsed = 0;    // 0 if unconstrained (no ConstrainedTextureCache)
    size_t textureCacheBytesLimit = 0;   // budget: ConstrainedRendererConfig::textureMemoryBytes-derived
    size_t textureCacheCount = 0;
    bool textureCacheActive = false;     // false in unconstrained mode (driver-managed, no budget)

    size_t eqe1MappedBytes = 0;          // EQE1 flat packs, mmap/page-cache backed. MAPPED size, not
                                          // resident (T105: typically 1.6-10.6% resident) -- deliberately
                                          // NOT included in totalEstimate (T108); reported separately.
    size_t eqe1ArchiveCount = 0;

    size_t zoneGeometryBytes = 0;        // single mesh OR PVS region meshes, whichever active
    size_t placeableObjectsBytes = 0;

    size_t entityInstanceBytes = 0;      // per-instance render data (EntityRenderer)
    size_t characterModelsBytes = 0;     // RaceModelLoader total (tex+geom+skel+char+animMesh+irrMesh)
    size_t equipmentModelsBytes = 0;     // EquipmentModelLoader total

    size_t s3dZoneSourceBytes = 0;       // CPU-side S3D geometry/BSP/animation retained for mesh rebuilds

    size_t gpuTextureBytes = 0;          // actual GPU-side texture allocations (shared RAM)

    size_t hcmapBytes = 0;               // HCMap/collision mesh

    size_t framebufferBytes = 0;         // actual usage for current resolution
    size_t framebufferBudgetBytes = 0;   // ConstrainedRendererConfig::framebufferMemoryBytes (the real budget)

    size_t vboEboBytes = 0;              // GPU hardware buffers

    // --- Audio (from MemoryReportInput ext; zero/unpopulated when ext.audioAvailable is false) ---
    size_t sfxCacheBytes = 0;
    size_t soundBufferCacheBytes = 0;
    size_t soundFontEstimateBytes = 0;
    size_t musicDecodedBytes = 0;
    size_t audioPfsArchiveBytes = 0;

    // --- Game Data (from ext; zero when the caller has no game-state to report, e.g. benchmark tools) ---
    size_t entityEstimateBytes = 0;
    size_t doorEstimateBytes = 0;
    size_t spellDbEstimateBytes = 0;

    // --- Total ---
    size_t totalEstimate = 0;            // sum of every category above that has a defined estimate,
                                          // EXCEPT eqe1MappedBytes (T108: that field is mapped/address-
                                          // space size, not resident, and would inflate this beyond RSS)
    size_t processRssBytes = 0;          // passthrough of ext.processRssBytes, for convenience
    size_t untrackedBytes = 0;           // processRssBytes - totalEstimate (0 if RSS unavailable or <= total)
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_DRAW_TYPES_H
