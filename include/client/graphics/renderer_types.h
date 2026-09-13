// Free-standing renderer enums, structs and forward declarations.
// Extracted verbatim from the umbrella renderer header by
// refactor/J17_irrlicht_renderer_header_split.md. Move-only: no line of the
// extracted block was edited. The three timer-query free-function
// declarations in that header's preamble deliberately did not travel; see
// that plan for why.

#ifndef EQT_GRAPHICS_RENDERER_TYPES_H
#define EQT_GRAPHICS_RENDERER_TYPES_H


#include <irrlicht.h>
#include <cstdint>
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
#include "client/graphics/vision_type.h"
#include "client/state/entity_pose_state.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/camera_controller.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/door_collision.h"
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
#include "client/graphics/gpu_upload_thread.h"
#include "client/graphics/work_priority.h"
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
// T461: the camera set (CameraMode) -- see the note where the enum used to be.
#include "client/graphics/player/camera_mode.h"

namespace EQT {
namespace Graphics {
// Forward declarations
class SkyRenderer;
class SkyLoader;
class SkyConfig;
class WeatherEffectsController;

// Renderer input action types for event queue
enum class RendererAction : uint8_t {
    // Renderer-internal toggles
    ToggleWireframe, ToggleHUD, ToggleNameTags, ToggleZoneLights,
    CycleObjectLights, ToggleCameraMode, ToggleOldModels,
    Screenshot, ToggleAllUI,
    ScrollUIUp, ScrollUIDown,   // T313: static-UI scroll (PageUp/PageDown)
    // T461: keyboard orbit of the active user camera (HotkeyAction::CameraOrbit*)
    CameraOrbitLeft, CameraOrbitRight, CameraPitchUp, CameraPitchDown,

    // Player-mode actions
    ToggleAutorun, ToggleAutoAttack, Hail, Consider,
    ToggleVendor, ToggleTrainer, ToggleCollision, ToggleCollisionDebug,
    ClearTarget, ToggleInventory, ToggleGroup, ToggleSkills,
    TogglePet, ToggleSpellbook, ToggleBuffWindow, ToggleOptions,
    ToggleCombatAbilities,      // T415 (B320): Alt+C
    DoorInteract, WorldObjectInteract,
    ReplyToTell,                // T319: R key -> ActionDispatcher::openReplyToTell()

    // Targeting
    TargetSelf, TargetGroupMember1, TargetGroupMember2, TargetGroupMember3,
    TargetGroupMember4, TargetGroupMember5, TargetNearestPC, TargetNearestNPC,
    CycleTargets, CycleTargetsReverse,

    // Debug overlays
    ToggleZoneLineVisualization, ToggleMapOverlay, RotateMapOverlay,
    MirrorXMapOverlay, ToggleNavmeshOverlay, RotateNavmeshOverlay,
    MirrorXNavmeshOverlay,

    // Frustum culling
    ToggleFrustumCulling,
};

struct RendererEvent {
    RendererAction action;
    int8_t intData;
    RendererEvent(RendererAction a, int8_t d = -1) : action(a), intData(d) {}
};

// Zone load step enum — each step is functionally self-contained
// Used by loadZoneSequential() for progress tracking
enum class ZoneLoadStep : uint8_t {
    None,
    S3d,        // 1: Parse zone archive
    Bsp,        // 2: Compute spatial data (region BBs, portals, light regions, object index)
    Atlas,      // 3: Texture atlas upload
    Regions,    // 4: Zone meshes
    Assets,     // 5: Build indexes, create subsystems
    Objects,    // 6: Zone objects
    Doors,      // 7: Door meshes
    Entities,   // 8: Entity prep worker
    Collision,  // 9: Zone collision
    Sky,        // 10: Sky, fog, weather
    Env,        // 11: Environment subsystems
    Lights,     // 12: Zone lighting
    Cleanup,    // 13: Release data
    All         // Run everything remaining
};

// Deferred/progressive object for lazy mesh building
struct DeferredObject {
    size_t objectIndex;              // Index into currentZone_->objects
    size_t bspRegion = SIZE_MAX;     // BSP region for priority
    irr::core::aabbox3df worldBounds;
    bool meshBuilt = false;
    size_t nodeIndex = SIZE_MAX;     // Index into objectNodes_ (set after first build)
};

// CPU-only computations performed on background thread after S3D parse
struct PendingZoneComputations {
    std::map<size_t, irr::core::aabbox3df> regionBoundingBoxes;
    std::vector<std::pair<size_t, size_t>> deferredObjectEntries; // (objectIndex, bspRegion) pairs
    std::unique_ptr<PortalSystem> portalSystem;
    bool portalOcclusionEligible = false;
    std::vector<size_t> zoneLightRegions;  // BSP region per light (SIZE_MAX = no region)

    // Archive index built on background thread (deferred asset loading only)
    std::unique_ptr<GraphicsArchiveIndex> archiveIndex;

    // Sky data loaded on background thread (sky.s3d + sky.ini — no GL)
    struct SkyLoadData {
        std::unique_ptr<SkyLoader> skyLoader;
        std::unique_ptr<SkyConfig> skyConfig;
        // Pre-decoded + upscaled sky textures from background thread (BMP only)
        struct PreDecodedTexture {
            std::string name;
            std::vector<uint8_t> pixels; // A8R8G8B8 (BGRA byte order for Irrlicht)
            uint32_t width, height;
        };
        std::vector<PreDecodedTexture> preDecodedTextures;
        // Pre-computed dome mesh from background thread (CPU trig, no GL)
        struct PrecomputedSkyDome {
            std::vector<irr::video::S3DVertex> vertices;  // 693 verts
            std::vector<irr::u16> indices;                 // 3840 indices
        };
        std::unique_ptr<PrecomputedSkyDome> precomputedDome;
        // Pre-computed WLD dome mesh from sky.s3d (lower poly, ~145 verts)
        struct PrecomputedWldDome {
            std::vector<irr::video::S3DVertex> vertices;
            std::vector<irr::u16> indices;
        };
        std::unique_ptr<PrecomputedWldDome> precomputedWldDome;
    };
    std::unique_ptr<SkyLoadData> skyLoadData;

    // P04: zoneAtlasPreload/objAtlasPreload (TextureAtlas::PreloadData) and
    // prebuiltMeshCache (PrebuiltMeshCacheData) removed — dead fields, never
    // populated or read anywhere since the E10 atlas removal / G01 rework.

    // Weather config pre-loaded on background thread (file I/O + JSON parse)
    struct WeatherConfigData {
        ZoneWeatherConfig config;
        bool loaded = false;
    };
    std::unique_ptr<WeatherConfigData> weatherConfig;

    // Pre-built deferred objects (tree-filtered + world bounds on background thread)
    std::vector<DeferredObject> prebuiltDeferredObjects;

    // Pre-built equipment index (S3D archive headers — no GL)
    struct EquipmentIndexData {
        std::map<int, EquipmentModelLoader::EquipmentModelRef> modelIndex;
        std::map<std::string, EquipmentModelLoader::EquipmentTextureRef> textureIndex;
        bool loaded = false;
    };
    std::unique_ptr<EquipmentIndexData> equipmentIndex;
};

// Camera control modes.
//
// L05 (Batch L, refactor/L_renderer_component_extraction.md) hoisted this enum
// here out of class IrrlichtRenderer (a component header cannot include
// irrlicht_renderer.h), and IrrlichtRenderer keeps
// `using CameraMode = EQT::Graphics::CameraMode;`. T461 moved the definition
// once more, into the device-free client/graphics/player/camera_mode.h
// (included at the top of this file), and replaced Free/Follow/FirstPerson
// with the official client's six cameras. Every spelling still resolves;
// Follow is now CameraMode::Chase and Free is gone.

// VisionType moved to client/graphics/vision_type.h (T360, BOARD B066) so
// simulation_worker.h -- which this header INCLUDES, so the dependency cannot
// run the other way -- can name it and use the single visionLightModifiers()
// mapping instead of a hand-written, swapped switch. The spelling
// EQT::Graphics::VisionType is unchanged; the include below re-exports it.

// Extended target information for HUD display
struct TargetInfo {
    uint16_t spawnId = 0;
    std::string name;
    uint8_t level = 0;
    uint8_t hpPercent = 100;
    uint16_t raceId = 0;
    uint8_t gender = 0;
    uint8_t classId = 0;
    uint8_t bodyType = 0;
    uint8_t helm = 0;
    uint8_t showHelm = 0;
    uint8_t texture = 0;  // equip_chest2 / body texture variant
    uint8_t npcType = 0;  // 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
    uint32_t equipment[9] = {0};      // Equipment material IDs
    uint32_t equipmentTint[9] = {0};  // Equipment tint colors (ARGB)
    // Position and heading for debug display
    float x = 0, y = 0, z = 0;        // Entity position (EQ coords)
    float heading = 0;                 // Entity heading from server (degrees 0-360)
};

// Object light source (torch, lantern, etc.) for distance-based culling
struct ObjectLight {
    irr::scene::ILightSceneNode* node = nullptr;
    irr::core::vector3df position;
    std::string objectName;  // For debugging
    irr::video::SColorf originalColor;  // Original color for weather modification
    bool isFireSource = false;    // True for torches, fires, braziers, flames, candles
    float flickerPhase = 0.0f;   // Randomized phase for fire flickering
    float flickerSpeed = 1.0f;   // Randomized speed multiplier for flickering
    size_t bspRegion = SIZE_MAX; // BSP region for PVS culling
};

// Vertex animated mesh (flags, banners, etc.)
struct VertexAnimatedMesh {
    irr::scene::IMeshSceneNode* node = nullptr;
    irr::scene::IMesh* mesh = nullptr;
    std::shared_ptr<MeshAnimatedVertices> animData;
    float elapsedMs = 0;
    int currentFrame = 0;
    std::string objectName;  // For debugging
    // Mapping from mesh buffer vertex index to animation vertex index
    // Indexed by [bufferIndex][vertexIndex] -> animationVertexIndex
    std::vector<std::vector<size_t>> vertexMapping;
    // Center offset (EQ coords) - animation frames are relative to center,
    // but mesh vertices have center baked in. Add this to animation positions.
    float centerOffsetX = 0.0f;
    float centerOffsetY = 0.0f;
    float centerOffsetZ = 0.0f;
};

// Player movement state for EQ-style controls
struct PlayerMovementState {
    // Movement input flags
    bool moveForward = false;
    bool moveBackward = false;
    bool strafeLeft = false;
    bool strafeRight = false;
    bool turnLeft = false;
    bool turnRight = false;
    bool autorun = false;

    // Movement speeds (units per second) - EQ defaults
    float runSpeed = 48.5f;
    float walkSpeed = 24.0f;
    float backwardSpeed = 24.0f;
    float strafeSpeed = 24.0f;
    float turnSpeed = 90.0f;  // Degrees per second

    // Running vs walking
    bool isRunning = true;

    // Jump state
    bool isJumping = false;        // Currently in air from jump
    float verticalVelocity = 0.0f; // Current vertical velocity (positive = up)
    float jumpVelocity = 40.0f;    // Initial upward velocity when jumping
    float gravity = 80.0f;         // Gravity acceleration (units/sec^2)

    // Swimming state
    bool isSwimming = false;       // Currently in water
    bool isLevitating = false;     // Has levitation effect (flymode 2)
    bool swimUp = false;           // Pressing swim up key
    bool swimDown = false;         // Pressing swim down key
    float swimSpeed = 20.0f;       // Base swim speed (units per second)
    float swimBackwardSpeed = 10.0f;  // Backward swim speed
    float swimVerticalSpeed = 15.0f;  // Vertical swim speed (up/down)
    float sinkRate = 5.0f;         // Rate of sinking when idle (units per second)
};

// Configuration for player mode
struct PlayerModeConfig {
    float eyeHeight = 0.0f;              // Eye height offset from head bone position (Y/Shift+Y to adjust)
    float collisionRadius = 2.0f;        // Character collision radius
    float collisionStepHeight = 4.0f;    // Max step-up height for stairs
    float collisionCheckHeight = 3.0f;   // Height above ground for collision raycast
    bool collisionEnabled = true;        // Toggle collision on/off for debugging
    bool collisionDebug = false;         // Print collision debug info
};

// Configuration for the renderer
struct RendererConfig {
    int width = 800;
    int height = 600;
    bool fullscreen = false;
    std::string windowTitle = "WillEQ";
    std::string eqClientPath;      // Path to EQ client files
    std::string regionMapsPath = "data/region_maps";  // Per-zone indoor region classification

    // Rendering options
    bool lighting = false;         // Fullbright mode by default
    float ambientIntensity = 0.4f;

    // Constrained rendering config (always active, default OrangePi preset)
    ConstrainedRenderingPreset constrainedPreset = ConstrainedRenderingPreset::OrangePi;
    ConstrainedRendererConfig constrainedConfig;
};


// ----------------------------------------------------------------------
// L13 (refactor/L13_pairwise_ownership.md): four types hoisted out of
// class IrrlichtRenderer. They typed members that three or more components
// reach, so those members could not be promoted while renderer_context.h
// would have had to name IrrlichtRenderer::<Type> and re-acquire the ~60
// transitive includes the context exists to avoid (batch correction 10).
// Each keeps a `using` alias in class IrrlichtRenderer (correction 22's
// shape), so every existing spelling still compiles.
//
// RendererZoneLightData is DELIBERATELY not named ZoneLightData: two other
// structs of that name exist (EQT::ZoneLightData in client/zone_object_data.h
// and the nested SimulationZoneData::ZoneLightData in simulation_worker.h).
// A namespace-scope EQT::Graphics::ZoneLightData would not be ambiguous --
// it would SHADOW EQT::ZoneLightData for every unqualified use inside
// namespace EQT::Graphics, which is worse than ambiguity because it is
// silent. The rename removes the question; IrrlichtRenderer::ZoneLightData
// still resolves through the alias.
// ----------------------------------------------------------------------

// Zone light data -- no scene nodes, just base data for worker
struct RendererZoneLightData {
    float radius;
    float attConstant, attLinear, attQuadratic;
    irr::video::SColorf baseDiffuseColor;  // Original color from WLD
};

// Fire glow light data (collected during zone load)
struct FireGlowLight {
    irr::core::vector3df position;  // Irrlicht Y-up
    float r, g, b;                 // Fire color
    float radius;                  // Glow radius
    float flickerPhase;            // Random phase offset for animation
    float flickerSpeed;            // Random speed multiplier
};

// DoorCollisionData moved to client/graphics/door_collision.h (T435, board
// B425): it now has a writer -- DoorManager::computeDoorCollision() -- and the
// ray test that reads it is a free function there, so both are reachable
// without an IrrlichtRenderer. Included above via door_manager.h.

// Frame timing profiler for performance analysis
struct FrameTimings {
    int64_t inputHandling = 0;
    int64_t cameraUpdate = 0;
    int64_t entityUpdate = 0;
    int64_t doorUpdate = 0;
    int64_t spellVfxUpdate = 0;
    int64_t animatedTextures = 0;
    int64_t vertexAnimations = 0;
    int64_t fireFlicker = 0;
    int64_t objectVisibility = 0;
    int64_t pvsVisibility = 0;
    int64_t meshLoading = 0;
    int64_t objectLights = 0;
    int64_t tier2Update = 0;     // Detail objects, trees
    int64_t tier3Update = 0;     // Weather, sky, particles, boids
    int64_t hudUpdate = 0;
    int64_t sceneDrawAll = 0;
    // Scene Draw All sub-timings (from render pass timer)
    int64_t sceneAnimate = 0;       // OnAnimate + OnRegisterSceneNode
    int64_t sceneSolid = 0;         // ESNRP_SOLID pass
    int64_t sceneTransparent = 0;   // ESNRP_TRANSPARENT + TRANSPARENT_EFFECT passes
    int64_t sceneSkybox = 0;        // ESNRP_SKY_BOX pass
    int64_t sceneOther = 0;         // Camera + light + shadow passes
    int sceneNodeCount = 0;         // Nodes rendered in drawAll
    int64_t manualZoneDraw = 0;
    int64_t targetBox = 0;
    int64_t particles = 0;
    int64_t boids = 0;
    int64_t weatherRender = 0;
    int64_t debugOverlays = 0;
    int64_t castingBars = 0;
    int64_t guiDrawAll = 0;
    int64_t staticUI = 0;           // Static UI panels (player status, chat, hotbar, etc.)
    int64_t zoneLineOverlay = 0;
    int64_t glFinish = 0;            // glFinish() before endScene (GPU pipeline drain)
    int64_t endScene = 0;
    int64_t totalFrame = 0;
    int manualZoneRegionsDrawn = 0;  // Regions actually drawn in manual zone draw
    int manualZoneDrawCalls = 0;     // Individual drawMeshBuffer calls
    // Fine-grained fields (Phase 2 timing gaps)
    int64_t playerMovement = 0;      // updatePlayerMovement() - collision during movement
    int64_t occlusionCulling = 0;    // Software occlusion rasterize + test (inside PVS)
    int64_t zoneLightVisibility = 0; // updateZoneLightVisibility() (split from objectVisibility)
    // U07c3: windowManagerUpdate removed
    int64_t weatherSystemUpdate = 0; // weatherSystem_->update() (every frame, outside Tier 3)
    int64_t footprintRender = 0;     // detailManager_->renderFootprints()
    int64_t postRender = 0;          // RDP capture + cursor + screenshot
    int64_t simWorkerApply = 0;      // SimulationWorker result application
    int64_t drainQueues = 0;         // GLES2 upload drain + texture cache drain
    int64_t visibility = 0;          // processFrameVisibility() - frustum update
    int64_t postSimInput = 0;        // postSimulationInput() - worker input posting
    int64_t postLoadWork = 0;        // Post-loading priority work (GREEN-gated)
    int64_t entityHousekeeping = 0;  // Entity prep promote/scan/dispatch
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_TYPES_H
