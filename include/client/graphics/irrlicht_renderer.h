#ifndef EQT_GRAPHICS_IRRLICHT_RENDERER_H
#define EQT_GRAPHICS_IRRLICHT_RENDERER_H

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
#include "client/graphics/gpu_upload_thread.h"
#include "client/graphics/work_priority.h"
#include "client/input/hotkey_manager.h"

#include "client/graphics/renderer_context.h"
#include "client/graphics/eqe1/eqe1_archive_component.h"
#include "client/graphics/overlays/overlays_component.h"
#include "client/graphics/diagnostics/diagnostics_component.h"
#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"
#include "client/graphics/rdp/rdp_component.h"
#endif

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
#include "client/graphics/gles2_egl_helper.h"
#endif

// P52: GPU-sync per-phase timer-query hooks (docker/irrlicht-drm/COpenGLES2Driver.cpp
// / gpu_sync_probe.h). Plain-typed free functions, declared extern locally rather
// than via that header (which lives outside this target's include path — only the
// benchmark tool adds docker/irrlicht-drm to its include dirs), matching the existing
// gles2CreateStaticHWBuffer() extern-declaration precedent. No-ops when the probe is
// disabled (see gpu_sync_probe.h's zero-cost-when-off design).
extern void eqtGPUPhaseBegin(int phase);
extern void eqtGPUPhaseEnd(int phase);
extern void eqtGPUPhaseCollectResults();

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

// J17: the free-standing types that used to sit below this preamble now live in
// three sibling headers, included here so that every existing includer of this
// header sees exactly the same declarations, in the same order, as before.
#include "client/graphics/renderer_types.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/renderer_draw_types.h"

namespace EQT {
namespace Graphics {

// Main Irrlicht renderer class
class IrrlichtRenderer {
public:
    IrrlichtRenderer();
    ~IrrlichtRenderer();

    // Initialize the renderer (window, GPU, subsystems, background thread pool).
    // Use this at startup for early progress display, then call loadGlobalAssets() later.
    bool initLoadingScreen(const RendererConfig& config);

    // Load global assets (character models, equipment) - call after initLoadingScreen()
    // This is the heavy loading that was previously done in init()
    bool loadGlobalAssets();

    // Loading thread owns GL context — main thread must not call GL-mutating methods.
    void setLoading(bool loading) { loading_.store(loading, std::memory_order_release); }
    bool isLoading() const { return loading_.load(std::memory_order_acquire); }

    // Show/hide loading screen (progress bar overlay)
    void showLoadingScreen();
    void hideLoadingScreen();
    bool isLoadingScreenVisible() const { return loadingScreenVisible_; }

    // D20d: Process renderer-specific slash commands forwarded from game thread
    void processSlashCommand(const std::string& command);

    // Shutdown the renderer
    void shutdown();

    // Check if renderer is running
    bool isRunning() const;

    // Request the renderer to quit (for /q, /quit commands)
    void requestQuit();

    // Unload current zone
    void unloadZone();

    // Set zone environment parameters (sky type, fog colors) from server data
    // Call after zone loading to apply zone-specific rendering settings
    // skyType: sky type from NewZone_Struct (0-255)
    // zoneType: zone type (0=outdoor, 1=dungeon, etc. - indoor zones disable sky)
    // fogRed/Green/Blue: fog color arrays (4 values for different fog ranges)
    // fogMinClip/MaxClip: fog distance arrays (4 values)
    void setZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                            const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                            const float fogMinClip[4], const float fogMaxClip[4]);

    // Get current zone name
    const std::string& getCurrentZoneName() const { return currentZoneName_; }

    // Sky control for debugging
    // Toggle sky rendering on/off
    void toggleSky();

    // Force a specific sky type (for testing)
    // skyTypeId: sky type ID (0=default, 6=luclin, 10=thegrey, 11=pofire, etc.)
    void forceSkyType(uint8_t skyTypeId);

    // Check if sky is enabled
    bool isSkyEnabled() const;

    // Get current sky info string for debug HUD
    std::string getSkyDebugInfo() const;

    // Entity management
    // isPlayer: true if this is our own player character
    // isNPC: true if this is an NPC (npc_type=1), false for other player characters
    // isCorpse: true if this is a corpse (npc_type=2 or 3), starts with death animation
    // serverSize: size value from server (0 or 1 = default, >1 = larger, <1 = smaller)
    bool createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                     float x, float y, float z, float heading, bool isPlayer = false,
                     uint8_t gender = 0, const EntityAppearance& appearance = EntityAppearance(),
                     bool isNPC = true, bool isCorpse = false, float serverSize = 0.0f);
    // Register entity metadata only (no mesh building, for deferred loading)
    bool registerEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                        float x, float y, float z, float heading, bool isPlayer = false,
                        uint8_t gender = 0, const EntityAppearance& appearance = EntityAppearance(),
                        bool isNPC = true, bool isCorpse = false, float serverSize = 0.0f,
                        uint8_t entityLevel = 0);
    void updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                      float dx = 0, float dy = 0, float dz = 0, uint32_t animation = 0);
    void removeEntity(uint16_t spawnId);
    void startCorpseDecay(uint16_t spawnId);  // Start fade-out animation for corpse
    void setEntityLight(uint16_t spawnId, uint8_t lightLevel);  // Set entity light source (lantern, lightstone)
    void setEntitySize(uint16_t spawnId, float serverSize);     // T318: shrink/grow/illusion resize (server-authoritative)
    void clearEntities();

    // Door management
    bool createDoor(uint8_t doorId, const std::string& name,
                    float x, float y, float z, float heading,
                    uint32_t incline, uint16_t size, uint8_t opentype,
                    bool initiallyOpen);
    // Register door metadata only (no mesh building, for deferred loading)
    bool registerDoor(uint8_t doorId, const std::string& name,
                      float x, float y, float z, float heading,
                      uint32_t incline, uint16_t size, uint8_t opentype,
                      bool initiallyOpen);
    void setDoorState(uint8_t doorId, bool open, bool userInitiated = false);
    void clearDoors();

    // Deferred/progressive asset loading
    // Index objects without building meshes (deferred mode)
    void indexObjectMeshes();
    // Build a single deferred object mesh by index
    void buildDeferredObject(size_t idx);
    // Minimal collision using only player's region mesh + HCMap fallback
    void setupMinimalZoneCollision();
    // Progressive per-frame asset building (priority-ordered, GREEN-only single-step)
    void processFrameProgressiveLoad();

    // Create and configure EntityRenderer (idempotent — skips if already created)
    void createEntityRenderer();

    // T124: idempotent guard-registration helpers, defined in
    // renderer_zone_merge.cpp (not here -- irrlicht_renderer.cpp was at the
    // check_file_sizes.sh budget when this was written). ensureBoidsTextureGuard() is safe to
    // call from either of boidsManager_'s two lazy-construction sites. See
    // DoorTextureGuard/BoidsManager::swapEvictedTexture()'s doc comments
    // for the gaps these close.
    void ensureDoorTextureGuard();
    void ensureBoidsTextureGuard();
    void unregisterRemainingT124Guards();

    // Background zone loading — instant scene + deferred S3D
    // Build HCMap placeholder + collision for immediate gameplay (<5ms)
    void setupInstantScene(const std::string& zoneName, float playerX = 0, float playerY = 0, float playerZ = 0);
    // Create collision from HCMap data only (no S3D mesh needed)
    void setupHCMapCollision();
    // Store zone environment data for deferred application (after sky renderer init)
    void storeZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                              const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                              const float fogMinClip[4], const float fogMaxClip[4]);
    // Apply stored zone environment (called when sky renderer is ready)
    void applyStoredZoneEnvironment();
    // Check if all deferred assets are built
    void checkProgressiveLoadingComplete();
    // T111: pure predicate for whether one entity blocks progressive-load
    // completion. Extracted out of checkProgressiveLoadingComplete() so it's
    // unit-testable without a scene graph/device, and so it can be kept
    // deliberately symmetric with queueEntityPrepRequests()'s own gate
    // (entityPrepMaxPvsDepth) rather than the currentPvsRegion_-equality
    // check T109 shipped, which is why deferred entities outside the
    // player's landing region were never picked up (see
    // refactor/T111_progressive_loading_rearm.md's Review). An entity this
    // predicate says blocks MUST be one queueEntityPrepRequests() will
    // actually queue, or completion would wait forever; one it excludes
    // must not silently vanish from the count either, or the T109 bug
    // recurs.
    static bool isEntityBlockingProgressiveLoad(bool meshBuilt, bool inSceneGraph,
                                                 bool isPlayerEntity, bool pvsValid,
                                                 uint8_t regionPvsDepth,
                                                 uint8_t entityPrepMaxPvsDepth);
    // Queue background prep requests for unbuilt entities
    void queueEntityPrepRequests();
    // Log build time and warn if over budget
    void logAssetBuildTime(const char* type, size_t id, std::chrono::steady_clock::time_point start);
    // Send loading progress message to chat window
    void sendLoadProgress(const std::string& msg);
    // Incremental collision helpers
    void addRegionToCollision(size_t regionIdx);
    void addDoorToCollision(uint8_t doorId);
    void addObjectToCollision(size_t objIdx);
    // Find BSP region for a point (EQ coordinates)
    size_t findBspRegionForPoint(float x, float y, float z);
    // Check if progressive loading is active
    bool isProgressiveLoadingActive() const { return progressiveLoadingActive_; }
    // Sequential zone loading — executes all 13 steps inline on the calling thread.
    // Called from the loading thread (L05/L06).
    // Updates status.percent/text between steps, checks status.quitRequested.
    void loadZoneSequential(const std::string& eqClientPath, struct LoadingStatus& status);
    bool hasLoadFailed() const { return loadFailed_; }

    // T241: door / world-object interaction callbacks deleted (no setter
    // caller anywhere); DoorInteractIntent / WorldObjectInteractIntent are
    // the live routes.

    // World object management (for click detection on tradeskill containers)
    void addWorldObject(uint32_t dropId, float x, float y, float z, uint32_t objectType, const std::string& name);
    void removeWorldObject(uint32_t dropId);
    void clearWorldObjects();

    // Trigger death animation for an entity (plays death animation and holds at last frame)
    void playEntityDeathAnimation(uint16_t spawnId);

    // Play a specific animation on an entity
    // animCode: EQ animation code (e.g., "c01" for attack, "o02" for wave, etc.)
    // loop: whether to loop the animation (when false, holds on last frame)
    // playThrough: if true, animation must complete before next can start
    bool setEntityAnimation(uint16_t spawnId, const std::string& animCode, bool loop = false, bool playThrough = true);

    // Set entity pose state (sitting, standing, crouching) - prevents movement updates from overriding
    // S2: enum owned by the state layer (client/state/entity_pose_state.h); alias kept for renderer call sites
    using EntityPoseState = eqt::state::EntityPoseState;
    void setEntityPoseState(uint16_t spawnId, EntityPoseState pose);

    // Set entity weapon skill types for combat animation selection
    void setEntityWeaponSkills(uint16_t spawnId, uint8_t primaryWeaponSkill, uint8_t secondaryWeaponSkill);

    // Get entity weapon skill type (for animation selection)
    uint8_t getEntityPrimaryWeaponSkill(uint16_t spawnId) const;
    uint8_t getEntitySecondaryWeaponSkill(uint16_t spawnId) const;

    // Combat animation buffering (for double/triple attack and dual wield detection)
    // Buffers damage packets within 50ms window to detect multi-hit scenarios
    void queueCombatAnimation(uint16_t sourceId, uint16_t targetId,
                              uint8_t weaponSkill, int32_t damage, float damagePercent);

    // Check if entity has pending combat animations in buffer
    bool hasEntityPendingCombatAnims(uint16_t spawnId) const;

    // Queue received damage animation into combat buffer (from emote packets)
    void queueReceivedDamageAnimation(uint16_t spawnId);

    // Queue combat skill animation into combat buffer (bash, kick, etc.)
    void queueSkillAnimation(uint16_t spawnId, const std::string& animCode);

    // First-person mode methods
    // Check if in first-person mode
    bool isFirstPersonMode() const { return cameraMode_ == CameraMode::FirstPerson; }

    // Set the player's spawn ID (marks that entity as the player and handles visibility)
    void setPlayerSpawnId(uint16_t spawnId);

    // Set player's race (determines base vision type)
    void setPlayerRace(uint16_t raceId);

    // Set vision type (for buffs/items that upgrade vision)
    // Only upgrades vision - cannot downgrade below base race vision
    void setVisionType(VisionType vision);

    // Reset vision to base race vision (call when vision buffs fade)
    void resetVisionToBase();

    // Get current vision type
    VisionType getVisionType() const { return currentVision_; }

    // Get base vision type (from race)
    VisionType getBaseVision() const { return baseVision_; }

    // Set player position for camera following
    void setPlayerPosition(float x, float y, float z, float heading);

    // Get player's current Z (may differ from set value if snapped to ground)
    float getPlayerZ() const { return playerZ_; }

    // Swimming state
    void setSwimmingState(bool swimming, float swimSpeed = 20.0f, bool levitating = false);
    bool isSwimming() const { return playerMovement_.isSwimming; }
    void setSwimUp(bool up) { playerMovement_.swimUp = up; }
    void setSwimDown(bool down) { playerMovement_.swimDown = down; }
    void setLevitating(bool levitating) { playerMovement_.isLevitating = levitating; }

    // Get BSP tree for water region detection
    std::shared_ptr<EQT::Graphics::BspTree> getZoneBspTree() const { return zoneBspTree_; }

    // Update time of day lighting (hour 0-23, minute 0-59)
    void updateTimeOfDay(uint8_t hour, uint8_t minute);

    // Camera control modes
    enum class CameraMode {
        Free,       // Free-fly camera
        Follow,     // Follow player character
        FirstPerson // First-person from player position
    };
    void setCameraMode(CameraMode mode);
    void cycleCameraMode();
    CameraMode getCameraMode() const { return cameraMode_; }
    std::string getCameraModeString() const;

    // Get camera position and orientation for audio listener
    void getCameraTransform(float& posX, float& posY, float& posZ,
                            float& forwardX, float& forwardY, float& forwardZ,
                            float& upX, float& upY, float& upZ) const;

    // Process input and render a single frame
    // Returns false if the renderer should stop
    bool processFrame(float deltaTime);

    // Frame phase methods (called by processFrame)
    void processFrameInput(float deltaTime);
    void processFrameVisibility();
    void processFrameSimulation(float deltaTime);
    bool processFrameRender(float deltaTime);  // Returns false for loading screen early-return
    void processCommonInput(const std::vector<RendererEvent>& actions);
    void processPlayerInput(const std::vector<RendererEvent>& actions);
    void processInputDeltas(float deltaTime);
    void processChatInput();

    // ---- T319: chat-input prefill (game thread -> render thread) ----
    // The R key (HotkeyAction::ReplyToTell) decodes on the render thread,
    // crosses to the game thread through bridgeQueue_ ->
    // GraphicsInputHandler -> InputActionBridge ->
    // ActionDispatcher::openReplyToTell(), which produces the text the
    // chat input is meant to open with ("/tell <Name> "). That text has to
    // come BACK to the render thread, where chatPanelState_ lives.
    //
    // handleActionResult() is the body of the InputActionBridge action
    // callback Application installs (application.cpp); it runs on the GAME
    // thread. requestChatPrefill() parks the text under
    // chatPrefillMutex_; processChatInput() takes it on the render thread.
    // Keep every access to pendingChatPrefill_ inside these two -- same
    // no-raw-access rule as bridgeQueue_ (T312).
    void handleActionResult(const std::string& actionName, bool success,
                            const std::string& message);
    void requestChatPrefill(const std::string& text) {
        std::lock_guard<std::mutex> lock(chatPrefillMutex_);
        pendingChatPrefill_ = text;
    }
    std::optional<std::string> takeChatPrefill() {
        std::lock_guard<std::mutex> lock(chatPrefillMutex_);
        std::optional<std::string> taken;
        taken.swap(pendingChatPrefill_);
        return taken;
    }

    // Run the main render loop (blocking)
    void run();

    // Take a screenshot
    bool saveScreenshot(const std::string& filename);

    // Toggle rendering options. L04 (Batch L) moved every toggle body to
    // DiagnosticsComponent; call them through Diagnostics(). The readable
    // state accessors stay here -- they read members other families share.
    // T301 Leg D: readable so /hideui can echo the resulting state and so the
    // toggle can be asserted headlessly (tests/test_ui_hide_command.cpp).
    bool isAllUIHidden() const { return allUIHidden_; }

    // Front-to-back sorting and portal occlusion controls
    bool isManualZoneDrawEnabled() const { return manualZoneDrawEnabled_; }

    // P10: cross-region zone draw merge (see renderer_zone_merge.h). Runtime
    // toggle mirrors /sort's pattern -- a renderer-owned bool separate from
    // the config-loaded mergeZoneDraws, which only supplies the initial value
    // at zone load.
    bool isMergeZoneDrawsEnabled() const { return mergeZoneDrawsEnabled_; }

    // Toggle GPU upload thread on/off at runtime (for A/B frame timing comparison)
    void toggleGPUUploadThread();
#ifdef EQT_HAS_GLES2
    bool isGPUUploadThreadEnabled() const { return gpuUploadEnabled_; }
#else
    bool isGPUUploadThreadEnabled() const { return false; }
#endif

    // Debug lighting toggles (for isolating performance regressions) --
    // the toggles themselves are on DiagnosticsComponent (L04); these read
    // the resulting state.
    bool isPlayerLightEnabled() const { return debugPlayerLightEnabled_; }
    bool isObjectLightsEnabled() const { return debugObjectLightsEnabled_; }
    bool isDirectionalLightEnabled() const { return debugDirectionalLightEnabled_; }
    bool isPortalOcclusionEnabled() const { return portalOcclusionEnabled_; }
    bool isPortalDebugDrawEnabled() const { return portalDebugDraw_; }
    bool isStencilDebugDrawEnabled() const { return stencilDebugDraw_; }
    void setFrameTimingEnabled(bool enabled);  // Enable/disable frame timing profiler
    bool isFrameTimingEnabled() const { return frameTimingEnabled_; }
    std::vector<std::string> getSimWorkerDebugInfo() const;  // /simworker debug command
    // L04 (Batch L): implemented by DiagnosticsComponent. These three stay as
    // one-line forwarders because irrlicht_bridge.cpp (dumpScene,
    // getMemoryReport) and application.cpp (runSceneProfile) call them and
    // must not learn a component exists -- the K11 rule.
    void dumpScene() const { diagnostics_->dumpScene(); }  // /dumpscene debug command
    void runSceneProfile() { diagnostics_->runSceneProfile(); }  // Run scene breakdown profiler (profiles next frame)
    DiagnosticsComponent& Diagnostics() { return *diagnostics_; }
    // K15 (B033): resetCoordOffsets / adjustOffset{X,Y,Z} / getOffset{X,Y,Z} /
    // adjustRotation{X,Y,Z} / getRotation{X,Y,Z} and stepToString (above) were
    // 14 declarations with no definition anywhere -- the real offset/rotation
    // pair lives on EntityRenderer. Deleted; the linker never saw them.

    // Unified render distance (controls fog and object culling, NOT camera far plane)
    // The camera far plane must be larger to include the sky dome
    void setRenderDistance(float distance) {
        if (isLoading()) return;
        userRenderDistance_ = distance;
        // Cap effective render distance by constrained preset's clip distance
        if (config_.constrainedConfig.clipDistance > 0.0f) {
            distance = std::min(distance, config_.constrainedConfig.clipDistance);
        }
        // Cap effective render distance by server-provided zone max clip plane
        renderDistance_ = (zoneMaxClip_ > 0.0f) ? std::min(distance, zoneMaxClip_) : distance;
        // Sync camera far plane to render distance (must be at least render distance)
        if (camera_) {
            camera_->setFarValue(std::max(renderDistance_, SKY_FAR_PLANE));
        }
        overlays_->setupFog();
        // Sync render distance to entity renderer
        if (entityRenderer_) {
            entityRenderer_->setRenderDistance(renderDistance_);
        }
        // P32: the tree-manager render-distance sync was removed along with
        // AnimatedTreeManager — it only fed the CPU-instancing distance
        // culling (dead on GLES2; that code path was reachable only via
        // the removed EDT_BURNINGSVIDEO branch).
        // Force visibility update on next frame
        forcePvsUpdate_ = true;
    }
    float getRenderDistance() const { return renderDistance_; }

    // Sky far plane - camera far value must be at least this to render sky
    static constexpr float SKY_FAR_PLANE = 2000.0f;

    // Fog thickness (fade zone at edge of render distance)
    void setFogThickness(float thickness) {
        if (isLoading()) return;
        fogThickness_ = thickness;
        overlays_->setupFog();
    }
    float getFogThickness() const { return fogThickness_; }

    // Get Irrlicht device/driver/GUI (for loading thread GL context transfer)
    irr::IrrlichtDevice* getDevice() { return device_; }
    irr::video::IVideoDriver* getDriver() { return driver_; }

    // L01 (Batch L): the shared-state bundle the extracted renderer
    // components bind by reference. See renderer_context.h.
    RendererContext& Context() { return ctx_; }

    // L03 (Batch L): the overlays component. In-renderer callers reach the
    // family through overlays_->; this accessor is for tests.
    OverlaysComponent& Overlays() { return *overlays_; }
    irr::gui::IGUIEnvironment* getGUIEnvironment() { return guienv_; }

    // Get entity renderer
    EntityRenderer* getEntityRenderer() { return entityRenderer_.get(); }

    // Get event receiver (for input state queries)
    RendererEventReceiver* getEventReceiver() { return eventReceiver_.get(); }

    // K15 (B034): the HUD text update callback (HUDCallback, setHUDCallback,
    // hudCallback_) is deleted -- the member's only reference was the setter,
    // and the setter had zero callers tree-wide. HUD text is built in
    // renderer_hud.cpp, not by an external callback.

    // S2: movement callback removed — player position flows via PlayerPositionChanged intent only

    // Bridge for cross-thread game state ↔ renderer communication (D14+)
    void setBridge(eqt::bridge::GameStateBridge* bridge) { bridge_ = bridge; }

    // T241: the target / loot-corpse / vendor-toggle / banker-interact /
    // trainer-toggle / read-item / chat-submit / zoning-enabled callback
    // setters were deleted. No caller ever set one (grep, tests included);
    // each site that invoked them already pushes the equivalent bridge
    // intent (TargetIntent, LootCorpseIntent, VendorToggleIntent, ...).

    // Current target management. L03 (Batch L): the bodies moved to
    // OverlaysComponent; these three stay as one-line forwarders because
    // irrlicht_bridge.cpp calls them and must not learn a component exists
    // (the K11 rule) -- setCurrentTarget/clearCurrentTarget are also the
    // IOutputRenderer-shaped public surface ConsoleRenderer/NullRenderer
    // implement. getCurrentTargetInfo() had zero callers tree-wide and moved
    // with currentTargetInfo_, so it was deleted (Mechanism 5).
    void setCurrentTarget(uint16_t spawnId, const std::string& name, uint8_t hpPercent = 100, uint8_t level = 0) { overlays_->setCurrentTarget(spawnId, name, hpPercent, level); }
    void setCurrentTargetInfo(const TargetInfo& info) { overlays_->setCurrentTargetInfo(info); }
    void clearCurrentTarget() { overlays_->clearCurrentTarget(); }
    uint16_t getCurrentTargetId() const { return currentTargetId_; }

    // Collision map for player mode movement. shared_ptr<const HCMap>
    // (P35): shares ownership with the game thread's m_zone_map and
    // SimulationWorker's zoneData_.hcMap so the object outlives every
    // holder instead of dangling when the game thread resets/replaces
    // its own copy on the next zone change.
    void setCollisionMap(std::shared_ptr<const HCMap> map) { if (isLoading()) return; collisionMap_ = std::move(map); }

    // Navmesh pathfinder for navmesh overlay visualization
    void setNavmesh(PathfinderNavmesh* navmesh) { overlays_->setNavmesh(navmesh); }

    // Clip distance (camera far plane) - for constrained rendering mode
    void setClipDistance(float distance);
    float getClipDistance() const;

    // Deferred graphics init (render distance, spell VFX) — S2: was setInventoryManager(InventoryManager*),
    // renamed; the stored pointer was dead and no game-thread object crosses the boundary now
    void initDeferredGraphics();
    void toggleInventory();
    void openInventory();
    void closeInventory();

    // Note/Book reading UI
    void showNoteWindow(const std::string& text, uint8_t type);

    // Zone ready state - controls whether to show loading screen
    // Note: zoneReady is only true when BOTH network AND graphics are ready
    void setZoneReady(bool ready) { if (isLoading()) return; zoneReady_ = ready; }
    bool isZoneReady() const { return zoneReady_; }

    // Entity loading state - tracks when all entities have been fully loaded
    void setExpectedEntityCount(size_t count);  // Set expected number of entities from ZoneSpawns
    void notifyEntityLoaded();  // Called when an entity has finished loading (model/texture/animation)
    bool areEntitiesLoaded() const { return entitiesLoaded_; }
    void setNetworkReady(bool ready);  // Called when network packet exchange is complete
    bool isNetworkReady() const { return networkReady_; }
    void checkAndSetZoneReady();  // Check if both network and graphics are ready

    // Loading progress for zone transitions
    void setLoadingProgress(float progress, const std::wstring& text) {
        if (isLoading()) return;
        loadingProgress_ = progress;
        loadingText_ = text;
        // Immediately redraw loading screen so progress updates are visible
        // during blocking operations like LoadZoneGraphics()
        if (loadingScreenVisible_) {
            overlays_->drawLoadingScreen(progress, text);
        }
    }

    // Loading screen title (e.g., "Connecting...", "Loading Zone...")
    void setLoadingTitle(const std::wstring& title) { overlays_->setLoadingTitle(title); }

    // Zone line bounding box visualization. L03: implemented by
    // OverlaysComponent; this one stays as a forwarder because
    // application_zone_loading.cpp calls it (the K11 rule). The renderer's
    // own callers of clearZoneLineBoundingBoxes()/toggleZoneLineVisualization()
    // were retargeted to overlays_->. setZoneLineDebug(), isInZoneLine() and
    // isZoneLineVisualizationEnabled() had zero callers tree-wide (Mechanism 5).
    void setZoneLineBoundingBoxes(const std::vector<EQT::ZoneLineBoundingBox>& boxes) { overlays_->setZoneLineBoundingBoxes(boxes); }
    bool isInventoryOpen() const;
    void setCharacterInfo(const std::wstring& name, int level, const std::wstring& className);
    void setCharacterDeity(const std::wstring& deity);
    void setExpProgress(float progress);  // 0.0 to 1.0
    void updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                              uint32_t curMana, uint32_t maxMana,
                              uint32_t curEnd, uint32_t maxEnd,
                              int ac, int atk,
                              int str, int sta, int agi, int dex, int wis, int intel, int cha,
                              int pr, int mr, int dr, int fr, int cr,
                              float weight, float maxWeight,
                              uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);

    // Character model view (3D preview in inventory)
    void updatePlayerAppearance(uint16_t raceId, uint8_t gender,
                                const EntityAppearance& appearance);

    // Update entity appearance (for illusion spells, etc.)
    void updateEntityAppearance(uint16_t spawnId, uint16_t raceId, uint8_t gender,
                                const EntityAppearance& appearance);

    // Mouse position (from event receiver)
    int getMouseX() const { return eventReceiver_ ? eventReceiver_->getMouseX() : 0; }
    int getMouseY() const { return eventReceiver_ ? eventReceiver_->getMouseY() : 0; }

    // Loot window access
    // U07c3: getWindowManager() removed — old UI deleted

    // Spell visual effects access
    EQ::SpellVisualFX* getSpellVisualFX() { return spellVisualFX_.get(); }

    // U01: Batched text renderer
    TextBatch* getTextBatch() { return textBatch_.get(); }

    // U02: UI sprite atlas
    UIAtlas* getUIAtlas() { return uiAtlas_.get(); }

    // U03: Static layout UI
    UIRenderer* getUIRenderer() { return uiRenderer_.get(); }
    bool isNewUIEnabled() const { return newUIEnabled_; }
    void setNewUIEnabled(bool v) { newUIEnabled_ = v; }

    // Constrained texture cache — owned by Application, set before initLoadingScreen()
    void setConstrainedTextureCache(ConstrainedTextureCache* cache) { constrainedTextureCache_ = cache; }
    ConstrainedTextureCache* getConstrainedTextureCache() { return constrainedTextureCache_; }

    // Frame budget governor access (always available after init)
    FrameBudgetGovernor* getGovernor() { return governor_.get(); }

    // Check if deferred asset loading is enabled
    bool isDeferredAssetLoading() const { return config_.constrainedConfig.deferredAssetLoading; }

    // Zone shader access (returns nullptr if shaders not available/enabled)
    ZoneShaderManager* getZoneShader() { return zoneShader_.get(); }

    // Detail system access (grass, plants, debris)
    Detail::DetailManager* getDetailManager() { return detailManager_.get(); }

    // Environmental particle system access
    Environment::ParticleManager* getParticleManager() { return particleManager_.get(); }

    // Ambient creatures (boids) system access
    Environment::BoidsManager* getBoidsManager() { return boidsManager_.get(); }

    // Tumbleweed system access (desert/plains rolling objects)
    Environment::TumbleweedManager* getTumbleweedManager() { return tumbleweedManager_.get(); }

    // Weather effects system access (rain, snow, lightning)
    WeatherEffectsController* getWeatherEffects() { return weatherEffects_.get(); }

    // Memory usage report for /pmem command (forwarder, see dumpScene above).
    // The T69 structured accessor getMemoryReportBreakdown() had no caller
    // outside the family and lives on DiagnosticsComponent only.
    std::vector<std::string> getMemoryReport(const MemoryReportInput& ext) const {
        return diagnostics_->getMemoryReport(ext);
    }

    // Set weather from server packet (type: 0=none, 1=rain, 2=snow; intensity: 1-10)
    void setWeather(uint8_t type, uint8_t intensity);

#ifdef WITH_RDP
    // RDP server support (alternative to Xvfb+x11vnc). L01 (Batch L) moved
    // the implementation to RdpComponent; these five stay as one-line
    // forwarders because Application calls them and must not learn a
    // component exists (the K11 rule). getRDPClientCount() had zero callers
    // tree-wide and was deleted.
    RdpComponent& Rdp() { return *rdp_; }
    bool initRDP(uint16_t port = 3389) { return rdp_->initRDP(port); }
    bool startRDPServer() { return rdp_->startRDPServer(); }
    void stopRDPServer() { rdp_->stopRDPServer(); }
    bool isRDPRunning() const { return rdp_->isRDPRunning(); }
    RDPServer* getRDPServer() { return rdp_->getRDPServer(); }
#endif // WITH_RDP

    // L02 (Batch L): the EQE1 archive family (open/resolve/report) and the
    // fire-glow icospheres live in Eqe1ArchiveComponent. Nothing outside
    // IrrlichtRenderer ever called any of them, so there are no forwarders --
    // every call site was retargeted to eqe1_-> (Mechanism 5).
    Eqe1ArchiveComponent& Eqe1() { return *eqe1_; }

private:
    void createSoftwareCursor();
    void setupCamera();
    void setupLighting();
    // SimulationWorker helpers
    void startSimulationWorkerEarly();     // Build core zone data (BSP/regions/objects/lights) and start worker
    // registerSimulationWorkerTreeData() removed (P32): it fed
    // AnimatedTreeManager's CPU-instancing tree buffers into the
    // SimulationWorker, but was never called from anywhere in the tree —
    // dead code, confirmed uncalled before deletion. Went with the class.
    void registerSimulationWorkerVertexAnimData();  // Add vertex anim data after objects are installed
    void stopSimulationWorker();     // Stop worker (zone transition)
    void postSimulationInput(float deltaTime);  // Snapshot main thread state → worker input
    // T116: applySimulationOutput()/bindFrameSimulationBuffers() are split by
    // lifetime contract -- see the two functions' own comments
    // (renderer_sim_bridge.cpp) and refactor/T116_simulationoutput_lifetime.md.
    // applySimulationResults() (the runtime path, called every frame from a
    // persistent SimulationWorker buffer) is the ONLY caller of the bind step;
    // loadZoneSequential()'s Step12b (renderer_zone_load.cpp) calls
    // applySimulationOutput() alone, with a local SimulationOutput, and must
    // never be given a reason to call the bind step too.
    void applySimulationResults();   // Apply worker output → scene graph, then bind frame pointers
    void applySimulationOutput(const SimulationOutput& results);  // Value-only: no pointers stored. Safe with ANY lifetime, including a stack local.
    // Stores pointers into `persistent`'s containers on four members (see
    // their declarations below). `persistent` MUST outlive every frame the
    // pointers are read on -- call this ONLY with a pointer into
    // SimulationWorker's persistent, double-buffered output_[2]. Passing a
    // stack-local SimulationOutput here reopens the exact dangling-pointer
    // hazard T111 and T115 each patched (see the plan). This is why the
    // load path (Step12b) does not call it.
    void bindFrameSimulationBuffers(const SimulationOutput* persistent);
    SimulationZoneData buildSimulationZoneData();  // Build zone data snapshot for SimulationWorker

    // loadZoneSequential() per-step helpers (G01 decomposition).
    // ZoneLoadContext is forward-declared here and defined in renderer_zone_load.cpp;
    // it holds every local that crosses a step boundary. Steps that can hit an
    // unrecoverable error return false; the driver stops and returns immediately,
    // matching the original early-return control flow.
    struct ZoneLoadContext;
    void loadZoneStep0_EQE1(ZoneLoadContext& ctx);
    bool loadZoneStep1_S3D(ZoneLoadContext& ctx);
    void loadZoneStep2_BSP(ZoneLoadContext& ctx);
    void loadZoneStep4_Regions(ZoneLoadContext& ctx);
    void loadZoneStep5_Assets(ZoneLoadContext& ctx);
    void loadZoneStep6_Objects(ZoneLoadContext& ctx);
    void loadZoneStep7_Doors(ZoneLoadContext& ctx);
    bool loadZoneStep8_Entities(ZoneLoadContext& ctx);
    // T109 load-distance gate — out-of-line in renderer_entity_load_gate.cpp
    // (keeps renderer_zone_load.cpp under the file-size gate; see that file
    // for why ensureEntityPrepWorkerStarted needs to exist at all).
    void ensureEntityPrepWorkerStarted(class RaceModelLoader* modelLoader, class EquipmentModelLoader* equipLoader);
    // Returns true (and records the deferral) if distFromPlayer exceeds
    // entityLoadRadius — the caller should skip the entity's prep+build
    // chain and let background streaming pick it up post-load.
    bool deferEntityForLoadGate(uint16_t spawnId, const std::string& name, uint16_t raceId, uint8_t gender,
                                 float distFromPlayer, size_t& deferredCount,
                                 std::set<std::pair<uint16_t, uint8_t>>& deferredPairs) const;
    bool loadZoneStep8a_BaseModelPrep(ZoneLoadContext& ctx);
    void loadZoneStep8b_VariantModelPrep(ZoneLoadContext& ctx);
    void loadZoneStep8c_VariantTextures(ZoneLoadContext& ctx);
    bool loadZoneStep8d_EquipmentPrep(ZoneLoadContext& ctx);
    void loadZoneStep8e_DistributePrepResults(ZoneLoadContext& ctx);
    void loadZoneStep8f_BuildEntityMesh(ZoneLoadContext& ctx);
    void loadZoneStep9_Collision(ZoneLoadContext& ctx);
    bool loadZoneStep10_Sky(ZoneLoadContext& ctx);
    void loadZoneStep11_Env(ZoneLoadContext& ctx);
    void loadZoneStep12_Lights(ZoneLoadContext& ctx);
    void loadZoneStep12b_Simulation(ZoneLoadContext& ctx);
    void loadZoneStep13_Cleanup(ZoneLoadContext& ctx);

    // advanceDeferredInit() folded into P11_Env_* phases of advanceBackgroundZoneLoad()
    void createZoneMesh();
    bool rebuildRegionMesh(size_t regionIdx);  // Build mesh for a single region
    // updateZoneLightColors() removed — vision/weather modifiers now applied in SimulationWorker
    void updateObjectLightColors(float deltaTime = 0.0f);  // Update object light colors based on weather + fire flicker

    // Player mode movement and collision
    void updatePlayerMovement(float deltaTime);
    bool checkMovementCollision(float fromX, float fromY, float fromZ,
                                float toX, float toY, float toZ);
    float findGroundZ(float x, float y, float currentZ);
    // PVS visibility check for a BSP region (used at insertion time for objects/lights)
    bool isRegionPvsVisible(size_t regionIdx) const;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::gui::IGUIEnvironment* guienv_ = nullptr;
    irr::scene::ICameraSceneNode* camera_ = nullptr;
    irr::video::ITexture* softwareCursorTexture_ = nullptr;  // DRM mode software cursor

    std::unique_ptr<CameraController> cameraController_;
    std::unique_ptr<FrustumCuller> frustumCuller_;
    std::unique_ptr<SoftwareOcclusionCuller> occlusionCuller_;
    std::unordered_set<size_t> occlusionCulledRegions_;
    // K15 (B034): the occlusion camera-movement gating members
    // (lastOccCam{X,Y,Z}, lastOccFwd{X,Y,Z}) had zero references tree-wide --
    // the gate was never implemented. Deleted.
    bool frustumDebugDraw_ = false;  // Draw region bboxes colored by frustum result
    std::unique_ptr<EntityRenderer> entityRenderer_;
    std::unique_ptr<class EntityPrepWorker> entityPrepWorker_;  // Background entity model preparation
    bool entityPrepReady_ = false;  // True after global archives loaded, safe to queue entity prep
    int entityPrepScanCounter_ = 0;  // Throttle queueEntityPrepRequests to every ~10 frames
    std::unique_ptr<SimulationWorker> simulationWorker_;  // Background simulation (visibility, lighting, animation)
    // T116: set ONLY by bindFrameSimulationBuffers(), from the runtime path's
    // persistent worker buffer. Previously commented "Valid during
    // processFrame, null otherwise" -- that was FALSE: nothing ever nulled
    // it, so it survived zone transitions pointing at recycled (not freed)
    // storage. It was safe only by three coincidences (simulationWorker_ is
    // constructed once and never reset; clearZoneData() replaces
    // output_[i] with a fresh empty SimulationOutput rather than freeing it;
    // both read sites guard on !empty()). The split doesn't change those
    // coincidences, but see renderer_zone_lifecycle.cpp for where it's now
    // also nulled at zone teardown, for consistency with its three siblings.
    const SimulationOutput* currentFrameSimResults_ = nullptr;
#ifdef EQT_HAS_GLES2
    std::unique_ptr<GPUUploadThread> gpuUploadThread_;   // Async GPU upload thread (shared EGL context)
    std::unordered_set<size_t> pendingVBOUploads_;       // Region indices with VBO uploads in flight
    bool gpuUploadEnabled_ = true;                       // Runtime toggle for /upload command
    void initGPUUploadThread();
    void processCompletedUploads();   // Batch mode (loading screen)

    // Split drain/process-one for GREEN-gated post-loading work
    std::vector<UploadResult> pendingGPUResults_;  // Drained from GPU upload thread
    void drainGPUResults();       // Cheap queue drain, no GL
    bool processOneGPUResult();   // Process 1 result (fence wait, register). GREEN-gated.
#endif

    // Particle I/O state for SimulationWorker
    std::unordered_map<uint16_t, glm::vec3> pendingParticleEntityPositions_;   // Resolved positions for next frame
    std::unordered_map<uint16_t, glm::vec3> pendingParticleEntityDirections_;  // Resolved directions for next frame
    // T116: set ONLY by bindFrameSimulationBuffers() -- see that function's
    // comment. Points into SimulationOutput's persistent storage.
    const std::vector<Environment::UnifiedParticle>* particleRenderBuffer_ = nullptr;

    std::unique_ptr<DoorManager> doorManager_;
    std::unique_ptr<RendererEventReceiver> eventReceiver_;
    std::unique_ptr<AnimatedTextureManager> animatedTextureManager_;
    std::unique_ptr<SkyRenderer> skyRenderer_;
    ConstrainedTextureCache* constrainedTextureCache_ = nullptr;  // Owned by Application, mandatory
    std::unique_ptr<Detail::DetailManager> detailManager_;  // Grass, plants, debris
    // P32: AnimatedTreeManager (CPU-instancing tree wind facade) deleted —
    // its live GLES2 consumers used only wind config/classification, which
    // now live directly here via TreeWindController + TreeIdentifier (both
    // were already separate TUs; AnimatedTreeManager was just a wrapper).
    TreeWindController treeWindController_;  // Wind config, weather multiplier
    TreeIdentifier treeIdentifier_;          // Tree mesh classification (isTreeMesh)
    std::unique_ptr<WeatherSystem> weatherSystem_;  // Weather state management
    std::unique_ptr<Environment::ParticleManager> particleManager_;  // Environmental particles
    std::unique_ptr<WeatherEffectsController> weatherEffects_;  // Weather visual effects (rain, snow, lightning)
    std::unique_ptr<ZoneShaderManager> zoneShader_;  // GLSL fog/lighting/tint shader
    // Zone texture atlas removed (E10) — EQE1 flat packs replace atlas system

    // EQE1 flat pack loaders — lazy-opened on demand, keyed by archive base name
    // (e.g., "qeynos2", "global_chr", "gequip2"). Thread-safe via eqe1Mutex_.
    std::unordered_map<std::string, std::unique_ptr<EQE1Loader>> eqe1Loaders_;
    mutable std::mutex eqe1Mutex_;
    // T207: name -> owning-archive index (data/eqe1_texture_index.json). Loaded
    // once and reused across zones; the candidate set is per-zone, not this.
    EQE1TextureIndex eqe1Index_;

    // P04: global archives resolved for the current zone but not yet opened.
    // loadZoneStep0_EQE1 populates this (instead of eagerly opening every
    // global archive); openArchiveContaining() opens the single owning one, only
    // as ConstrainedTextureCache's lookup actually misses on what's open so
    // far. Reset alongside eqe1Loaders_ on zone teardown.
    // T207: this is now a candidate SET, not a probe order -- the index decides
    // which one holds a given name, this decides which are eligible at all.
    // T266: no longer frozen at zone load. queueEntityPrepRequests() extends it
    // for every race that spawns AFTER load (addSpawnedRaceEQE1Candidate), and
    // openArchiveContaining() extends it with a late-open when a name is in no
    // candidate at all (rule R2). Written on the loading thread (Step0) and the
    // render thread (late spawns), read from whichever thread the cache's
    // lookup runs on -- so it is guarded by eqe1CandidatesMutex_ (never held
    // across getOrOpenEQE1, which takes eqe1Mutex_).
    std::vector<std::string> pendingGlobalEQE1Archives_;
    mutable std::mutex eqe1CandidatesMutex_;
    // Races whose archive has already been queued (dedups the per-frame call
    // from queueEntityPrepRequests()); reset with the candidate set.
    std::set<uint16_t> eqe1CandidateRaces_;
    // Size of the candidate set when zone-load Step0 finished. Anything
    // opened from beyond it is a late-open (an archive the load-time set did
    // not name) and is tagged as such in the request log.
    size_t eqe1LoadTimeCandidateCount_ = 0;

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
    std::unique_ptr<GLES2EGLHelper> gles2Helper_;  // GLES2 context for ETC1 EGL image sharing
#endif
    std::unique_ptr<Environment::BoidsManager> boidsManager_;  // Ambient creatures (boids)
    std::unique_ptr<Environment::TumbleweedManager> tumbleweedManager_;  // Tumbleweeds (desert/plains)
    std::unique_ptr<GraphicsArchiveIndex> graphicsArchiveIndex_;  // Race-to-archive index for deferred loading

    std::shared_ptr<S3DZone> currentZone_;
    std::string currentZoneName_;
    bool isIndoorZone_ = false;
    irr::scene::IMeshSceneNode* zoneMeshNode_ = nullptr;

    // PVS (Potentially Visible Set) culling state
    bool usePvsCulling_ = false;  // Whether PVS culling is active for this zone
    std::map<size_t, irr::scene::IMeshSceneNode*> regionMeshNodes_;  // Per-region mesh nodes
    std::map<size_t, irr::core::aabbox3df> regionBoundingBoxes_;  // World-space bounding boxes in EQ coords for distance culling
    std::shared_ptr<EQT::Graphics::BspTree> zoneBspTree_;  // BSP tree for region queries
    size_t currentPvsRegion_ = SIZE_MAX;  // Current camera region (SIZE_MAX = unknown)
    irr::scene::IMeshSceneNode* fallbackMeshNode_ = nullptr;  // Mesh for geometry not in any region

    // Manual zone draw (front-to-back sorting and portal occlusion)
    bool manualZoneDrawEnabled_ = false;
    std::vector<SortedRegionEntry> sortedZoneDrawList_;
    std::vector<ZoneDrawEntry> sortedDrawEntries_;  // Material-sorted mesh buffer draw list
    void drawZoneGeometrySorted();

    // P10: cross-region zone draw merge state (see renderer_zone_merge.h).
    // mergeZoneDrawsEnabled_ is initialized from config_.constrainedConfig.mergeZoneDraws
    // at zone load and independently runtime-toggleable via /mergedraws.
    // zoneMergeBuffers_ is built lazily (once the region set is fully resident
    // with zero mesh-cache evictions so far) and destroyed + permanently
    // disabled for the rest of the zone visit the instant an eviction occurs
    // (mergeZoneDrawsDisabledForZone_) -- see renderer_zone_merge.h's header
    // comment for the reduced-landing rationale.
    bool mergeZoneDrawsEnabled_ = false;
    bool mergeZoneDrawsDisabledForZone_ = false;
    size_t mergeZoneDrawsBuildEvictionCount_ = 0;
    std::unique_ptr<ZoneMergeBuffers> zoneMergeBuffers_;

    // P10b: root-cause fix for the qeynos2 mergeZoneDraws SIGSEGV -- keeps
    // regionMeshNodes_'s materials (including nodes detached from the scene
    // graph for front-to-back manual draw) from ever holding a dangling
    // ITexture* after a ConstrainedTextureCache eviction. Constructed once
    // in initLoadingScreen() (regionMeshNodes_ is a persistent member, safe
    // to bind a reference to for the renderer's lifetime) and registered
    // with constrainedTextureCache_ as a TextureEvictionListener. See
    // renderer_zone_merge.h for the full mechanism.
    std::unique_ptr<RegionMeshTextureGuard> regionMeshTextureGuard_;

    // P10b: guards softwareCursorTexture_ (declared below) against the same
    // class of dangling-pointer bug -- see renderer_zone_merge.h's
    // SingleTexturePointerGuard doc comment for the full story (found while
    // verifying P10b's own fix, pre-existing, unrelated to mergeZoneDraws).
    std::unique_ptr<SingleTexturePointerGuard> softwareCursorTextureGuard_;

    // P17: entity-mesh sibling of regionMeshTextureGuard_ above -- closes
    // the gap RegionMeshTextureGuard's own doc comment flagged (region
    // meshes protected since P10b, entity meshes were not). Constructed
    // once right after createEntityRenderer() (needs entityRenderer_ to
    // exist first, unlike the other two guards constructed earlier in
    // initLoadingScreen()) and registered the same way. See
    // renderer_zone_merge.h's EntityMeshTextureGuard doc comment.
    std::unique_ptr<EntityMeshTextureGuard> entityMeshTextureGuard_;

    // T123: object-node sibling of regionMeshTextureGuard_/
    // entityMeshTextureGuard_ above -- closes the detached-object-node gap
    // (applySimulationOutput() detaches invisible objectNodes_ entries from
    // the scene graph, making them unreachable to
    // ConstrainedTextureCache::clearTextureReferences()'s scan). Constructed
    // once in initLoadingScreen() alongside regionMeshTextureGuard_
    // (objectNodes_ is a persistent member, safe to bind a reference to for
    // the renderer's lifetime) and registered the same way. See
    // renderer_zone_merge.h's ObjectNodeTextureGuard doc comment.
    std::unique_ptr<ObjectNodeTextureGuard> objectNodeTextureGuard_;

    // T124: doors_ sibling of objectNodeTextureGuard_ above -- closes the
    // identical detached-pivotNode gap for doors (DoorManager's PVS-driven
    // visibility remove()/addChild() cycle, same shape as
    // applySimulationOutput()'s objectNodes_ handling). Constructed once in
    // initLoadingScreen() alongside the placeholder-resolving guards above
    // ONLY if doorManager_ already exists at that point; otherwise
    // constructed lazily the first time doorManager_ itself is (see
    // renderer_init.cpp and renderer_zone_load_objects.cpp, both sites). See renderer_zone_merge.h's
    // DoorTextureGuard doc comment.
    std::unique_ptr<DoorTextureGuard> doorTextureGuard_;

    // T124: forwarding guards for the singleton-subsystem holders that
    // actually need one -- SpellVisualFX and BoidsManager (see their own
    // swapEvictedTexture() doc comments). RainOverlay/SnowOverlay were
    // investigated and found NOT to need a guard: WeatherEffectsController
    // only ever constructs rainOverlay_/snowOverlay_ inside
    // `#ifndef EQT_HAS_GLES2` (weather_effects_controller.cpp), and
    // EQT_HAS_GLES2 is unconditionally defined whenever EQT_GLES2=ON, the
    // only supported renderer configuration since P32
    // (docs/decisions.md) -- so both stay permanently null in every
    // shipped build; there is nothing for a guard to protect. Flagged in
    // refactor/T124_remaining_texture_holders.md Review, not silently
    // skipped. TumbleweedManager was also investigated and found already
    // covered by the ordinary scene-graph scan (pool nodes are hidden via
    // setVisible(false), never detached -- see the Review for the full
    // argument); it needs no guard either.
    std::unique_ptr<ForwardingTextureGuard> spellVisualFXTextureGuard_;
    std::unique_ptr<ForwardingTextureGuard> boidsTextureGuard_;

    // Hardware buffer (VBO/EBO) helpers for static zone geometry
    void uploadMeshHardwareBuffers(irr::scene::IMeshSceneNode* node);
    void deleteMeshHardwareBuffers(irr::scene::IMeshSceneNode* node);

    // Portal occlusion system
    std::unique_ptr<PortalSystem> portalSystem_;
    bool portalOcclusionEnabled_ = false;
    bool portalOcclusionEligible_ = false;

    // Per-region indoor classification (loaded from data/region_maps/<zone>.json)
    std::unordered_set<size_t> indoorRegions_;

    // Region neighbor map (built from portal system for 1-depth PVS expansion)
    std::unordered_map<size_t, std::vector<size_t>> regionNeighbors_;
    void buildRegionNeighborMap();

    // Portal-based entity culling — pointer to SimulationOutput front buffer
    // (no copy). T116: set ONLY by bindFrameSimulationBuffers().
    const std::unordered_set<size_t>* portalVisibleRegions_ = nullptr;

    // PVS depth map — pointer to SimulationOutput front buffer (no copy).
    // T116: set ONLY by bindFrameSimulationBuffers().
    const std::unordered_map<size_t, uint8_t>* regionPvsDepthMap_ = nullptr;
    uint8_t getRegionPvsDepth(size_t regionIdx) const;
    void onPvsRegionChanged();

    // T116 (refactor/T116_simulationoutput_lifetime.md): test-only access to
    // applySimulationOutput()/bindFrameSimulationBuffers() and the four
    // pointer members they split, asserting the real production functions
    // (not a hand-constructed fake) uphold the lifetime split. Hand-expanded
    // from gtest's FRIEND_TEST(SimulationOutputLifetimeTest, X) macro
    // (-> `friend class SimulationOutputLifetimeTest_X_Test;`) rather than
    // including <gtest/gtest_prod.h>: the ARM cross-compile toolchain
    // (docker/Dockerfile.arm-noble) has no gtest headers at all (gtest is
    // fetched only for the x86 CMake tree's EQT_BUILD_TESTS targets), so an
    // unconditional gtest include in a production header that ships to ARM
    // is a hard build break there -- caught by this unit's own ARM
    // cross-compile verification step.
    // L02: Eqe1ArchiveComponent reaches 17 IrrlichtRenderer members that are
    // shared with other families and so stay here (Mechanism 3).
    friend class Eqe1ArchiveComponent;

    friend class SimulationOutputLifetimeTest_ApplyOutputAloneLeavesAllFourPointersUntouched_Test;
    friend class SimulationOutputLifetimeTest_ApplyOutputAloneLeavesAllFourPointersUntouched_PvsCullingOff_Test;
    friend class SimulationOutputLifetimeTest_BindBindsAllFourFromThePassedPersistentPointer_Test;
    friend class SimulationOutputLifetimeTest_BindOnlyBindsPvsMembersWhenPvsCullingConditionsHold_Test;
    friend class SimulationOutputLifetimeTest_BindOnlyBindsParticleBufferWhenParticleOutputValid_Test;
    friend class SimulationOutputLifetimeTest_RepeatedApplyOutputWithFreshLocalsNeverDangles_Test;

    // T235 (refactor/T235_spell_gem_key_race.md): test-only access to
    // processChatInput() and eventReceiver_ so the real render-thread
    // frame step can be driven against a real RendererEventReceiver with
    // no device, asserting a spell-gem key request SURVIVES it for the
    // game thread's GraphicsInputHandler. Same hand-expanded FRIEND_TEST
    // idiom as T116 above (no gtest include in a production header).
    friend class SpellGemKeyRaceTest;  // fixture installs eventReceiver_
    friend class SpellGemKeyRaceTest_GemKeySurvivesRenderFrameForGameThread_Test;
    friend class SpellGemKeyRaceTest_RenderFrameWithChatUnfocusedLeavesRequestUntouched_Test;

    // T319 (refactor/T319_reachability_cleanup.md): test-only access to
    // eventReceiver_ so tests/test_reply_to_tell_key.cpp can install a real
    // RendererEventReceiver on a bare, never-Init()'d renderer and drive the
    // R key from OnEvent all the way to chatPanelState_. Same hand-expanded
    // FRIEND_TEST idiom as T235 above; only the fixture needs it (the TEST_F
    // bodies use public members only, and friendship is not inherited).
    friend class ReplyToTellKeyTest;

    // L03 (Batch L): OverlaysComponent reaches thirty IrrlichtRenderer
    // members that are shared with another family but outside the
    // RendererContext core (owner_.X_), and six IrrlichtRenderer methods
    // that stayed behind (owner_.Foo()). It names no IrrlichtRenderer
    // static, so only the first axis of K13 correction (ii) is live.
    friend class OverlaysComponent;

    // PVS neighborhood cache: suppress onPvsRegionChanged() when moving within
    // a cluster of adjacent regions. Only fire when leaving the neighborhood.
    std::unordered_set<size_t> pvsNeighborhood_;  // regions within K portal hops of anchor
    size_t pvsNeighborhoodAnchor_ = SIZE_MAX;
    void rebuildPvsNeighborhood(size_t anchorRegion);
    bool isInPvsNeighborhood(size_t regionIdx) const;

    void drawZoneGeometryWithPortals();
    void drawRegionMesh(size_t regionIdx);
    void drawPortalQuad(const Portal& portal);
    void drawPortalRecursive(size_t fromRegion, int stencilLevel, int maxDepth,
                             std::unordered_set<size_t>& drawn);

    // Debug visualization for portals/stencil
    bool portalDebugDraw_ = false;
    bool stencilDebugDraw_ = false;

    // Zone placeholder mesh (HCMap collision mesh as visual context during loading)
    irr::scene::IMeshSceneNode* zonePlaceholderNode_ = nullptr;
    std::vector<HCMap::Triangle> cachedPlaceholderTriangles_;  // Cached terrain triangles for rebuild
    irr::core::vector3df lastPlaceholderBuildPos_;  // Irrlicht coords of last placeholder build
    void buildZonePlaceholder(float playerIrrX = 0, float playerIrrY = 0, float playerIrrZ = 0);
    void destroyZonePlaceholder();

    // T292 (2026-08-26): protectedRegions_ removed. It was the
    // ConstrainedMeshCache's eviction-protection bitvector; T67 deleted the
    // cache and with it the only writer, leaving the vector permanently
    // all-false, and the progressive door/PVS-object build paths that read
    // it were therefore permanently no-ops. See
    // src/client/graphics/CLAUDE.md's "Constrained Memory Management"
    // section for the removed cache's reference pointer.
    irr::scene::IMeshSceneNode* zoneCollisionNode_ = nullptr;  // Hidden node for zone collision in PVS mode

    // Regions waiting for async fallback textures to complete GPU upload.
    // Maps texture name → set of region indices that were built without that texture.
    std::unordered_map<std::string, std::unordered_set<size_t>> pendingTextureRegions_;

    // Regions queued for texture-ready rebuild (sorted: player region first, then by distance)
    struct TextureRebuildEntry {
        size_t regionIdx;
        float distanceSq;
    };
    std::vector<TextureRebuildEntry> textureRebuildQueue_;

    // Doors waiting for async textures to complete GPU upload.
    // Maps texture name → set of door IDs that were built without that texture.
    std::unordered_map<std::string, std::unordered_set<uint8_t>> pendingTextureDoors_;
    std::vector<uint8_t> doorTextureRebuildQueue_;

    // PVS objects waiting for async textures to complete GPU upload.
    // Maps texture name → set of deferred object indices.
    std::unordered_map<std::string, std::unordered_set<size_t>> pendingTextureObjects_;
    std::vector<size_t> objectTextureRebuildQueue_;

    // Deferred/progressive asset loading state (DeferredObject defined above PendingZoneComputations)
    std::vector<DeferredObject> deferredObjects_;
    bool progressiveLoadingActive_ = false;
    bool portalBuildPending_ = false;  // Deferred portal extraction from loadZone
    std::chrono::steady_clock::time_point progressiveLoadStartTime_;  // When progressive loading began

    // Shared background thread pool (replaces per-queue dedicated threads)
    std::unique_ptr<BackgroundThreadPool> backgroundThreadPool_;

    // Zone loading computations (used by loadZoneSequential)
    std::unique_ptr<PendingZoneComputations> pendingZoneComputations_;
    bool sequentialLoadComplete_ = false;  // True after loadZoneSequential() finishes
    bool loadFailed_ = false;              // True if loadZoneSequential() hit a FATAL error

    // Stored zone environment from NewZone packet (applied after global assets load)
    struct StoredZoneEnvironment {
        uint8_t skyType = 0;
        uint8_t zoneType = 0;
        uint8_t fogR[4] = {0}, fogG[4] = {0}, fogB[4] = {0};
        float fogMinClip[4] = {0}, fogMaxClip[4] = {0};
        bool pending = false;
    } storedZoneEnvironment_;

    std::vector<irr::scene::IMeshSceneNode*> objectNodes_;

    std::vector<irr::core::vector3df> objectPositions_;  // Cached positions for distance culling
    std::vector<irr::core::aabbox3df> objectBoundingBoxes_;  // Cached world-space bounding boxes for distance-to-edge culling
    std::vector<bool> objectInSceneGraph_;  // Track which objects are in scene graph
    std::vector<size_t> objectRegions_;  // BSP region per object (SIZE_MAX = unknown)

    // Unified render distance system
    // renderDistance_ is the effective limit - nothing renders beyond this
    // It is capped by zoneMaxClip_ (server-provided maximum for the current zone)
    // fogEnd = renderDistance_, fogStart = renderDistance_ - fogThickness_
    float renderDistance_ = 300.0f;    // Effective render limit (sphere around player)
    float userRenderDistance_ = 300.0f; // User's desired render distance (slider value)
    float zoneMaxClip_ = 99999.0f;     // Server-provided max clip plane for current zone (0 = no limit)
    float fogThickness_ = 50.0f;       // Thickness of fog fade zone at edge
    bool forcePvsUpdate_ = false;  // Force PVS visibility recalculation (set when render distance changes)
    // Zone light data — no scene nodes, just base data for worker
    struct ZoneLightData {
        float radius;
        float attConstant, attLinear, attQuadratic;
        irr::video::SColorf baseDiffuseColor;  // Original color from WLD
    };
    std::vector<ZoneLightData> zoneLightData_;
    std::vector<irr::core::vector3df> zoneLightPositions_;  // Cached positions for distance culling
    std::vector<size_t> zoneLightRegions_;  // Cached BSP region index for each light (SIZE_MAX = no region)
    std::vector<ObjectLight> objectLights_;  // Light-emitting objects (torches, lanterns)
    std::vector<bool> objectLightInSceneGraph_;  // Track which object lights are in scene graph
    int activeLightCount_ = 0;  // Number of active shader lights from last SimulationOutput
    std::vector<VertexAnimatedMesh> vertexAnimatedMeshes_;  // Meshes with vertex animation (flags, banners)
    irr::scene::ILightSceneNode* sunLight_ = nullptr;  // Directional sun light
    float ambientMultiplier_ = 1.0f;  // User-adjustable ambient light multiplier (Page Up/Down)

    // Time of day
    uint8_t currentHour_ = 12;
    uint8_t currentMinute_ = 0;

    // Frame phase shared state
    std::chrono::steady_clock::time_point sectionStart_;
    std::chrono::steady_clock::time_point frameStart_;  // Start of current frame (for lazy load budgeting)
    int64_t measureSection();
    bool chatInputFocused_ = false;
    bool pendingUIClick_ = false;  // Click consumed in Phase 1, applied to UI in render phase
    bool runTier2_ = true;
    bool runTier3_ = true;

    // Tiered update frequencies
    uint32_t frameNumber_ = 0;
    static constexpr uint32_t kTier2Interval = 3;   // ~20Hz at 60fps
    static constexpr uint32_t kTier3Interval = 6;   // ~10Hz at 60fps
    float tier3DeltaAccum_ = 0.0f;  // Accumulated delta for Tier 3 simulation
    float windTime_ = 0.0f;  // Monotonic wind time for GPU wind shader (seconds)

    // Frame budget governor (Green/Yellow/Red state machine for load throttling)
    std::unique_ptr<FrameBudgetGovernor> governor_;
    float renderCostAvgUs_ = 15000.0f;  // EMA of render phase cost (us), init ~15ms
    float essentialSimCostAvgUs_ = 3000.0f;  // EMA of essential sim cost (us), init ~3ms

    RendererConfig config_;

    // Cached display settings (loaded once at app startup, before render loop)
    eqt::ui::DisplaySettings cachedDisplaySettings_;
    bool displaySettingsCached_ = false;
    eqt::ui::DisplaySettings getDisplaySettings();

    // Pre-loaded item-to-model mapping (tiny JSON, loaded once at app startup)
    std::map<uint32_t, int> itemToModelMap_;

    bool initialized_ = false;
    bool globalAssetsLoaded_ = false;  // S05: loadGlobalAssets() idempotency guard
    std::atomic<bool> loading_{false};   // True while loading thread owns GL context
    bool loadingScreenVisible_ = true;  // True when loading screen is showing (default: show at start)
    bool zoneReady_ = false;  // True when zone is fully loaded and ready for player input
    bool networkReady_ = false;  // True when network packet exchange is complete
    bool entitiesLoaded_ = false;  // True when all entities have been loaded with models/textures
    size_t expectedEntityCount_ = 0;  // Expected number of entities from ZoneSpawns
    size_t loadedEntityCount_ = 0;  // Number of entities fully loaded so far
    float loadingProgress_ = 0.0f;  // Loading progress for zone transitions (0.0 - 1.0)
    std::wstring loadingText_ = L"Initializing...";  // Loading stage text
    bool wireframeMode_ = false;
    bool pendingScreenshot_ = false;  // Deferred to after render, before swap
    bool hudEnabled_ = true;
    bool allUIHidden_ = false;
    bool fogEnabled_ = true;
    bool lightingEnabled_ = true;   // Lighting enabled by default
    bool zoneLightsEnabled_ = false; // Zone lights off by default
    VisionType baseVision_ = VisionType::Normal;    // Base vision from race
    VisionType currentVision_ = VisionType::Normal; // Current vision (may be upgraded by items/buffs)
    int maxObjectLights_ = 8;  // Max object lights to display (1-8), cycles with L key
    bool debugPlayerLightEnabled_ = true;    // /plight toggle
    bool debugObjectLightsEnabled_ = true;   // /olight toggle
    bool debugDirectionalLightEnabled_ = true; // /zlight toggle
    bool fireEffectsEnabled_ = true;      // Fire light flickering and ember/smoke particles
    bool fireGlowLightingEnabled_ = true;  // Fire glow lighting (per-vertex warm glow on zone geometry)
    bool fireGlowIcospheresEnabled_ = false; // Fire glow icospheres (animated translucent volumes)
    int maxFireGlowLights_ = 4;            // Max active fire glow lights (1-8, from config)

    // Fire glow light data (collected during zone load)
    struct FireGlowLight {
        irr::core::vector3df position;  // Irrlicht Y-up
        float r, g, b;                 // Fire color
        float radius;                  // Glow radius
        float flickerPhase;            // Random phase offset for animation
        float flickerSpeed;            // Random speed multiplier
    };
    std::vector<FireGlowLight> fireGlowLights_;
    float fireGlowTime_ = 0.0f;       // Animation time accumulator

    // Icosphere rendering (animated fire glow volumes)
    std::vector<float> icosphereVertices_;   // 3 floats per vertex (unit sphere positions)
    std::vector<uint16_t> icosphereIndices_;
#ifdef EQT_HAS_DRM
    uint32_t icosphereVBO_ = 0;
    uint32_t icosphereIBO_ = 0;
    uint32_t icosphereProgram_ = 0;
#endif

    CameraMode cameraMode_ = CameraMode::Follow;  // Default to third-person follow camera

    // Zone geometry bounds cache (survives geometry vector release)
    float zoneBoundsMinX_ = 0, zoneBoundsMaxX_ = 0;
    float zoneBoundsMinY_ = 0, zoneBoundsMaxY_ = 0;
    bool zoneBoundsValid_ = false;

    // Player position (for Follow and FirstPerson modes)
    float playerX_ = 0, playerY_ = 0, playerZ_ = 0, playerHeading_ = 0;
    float playerPitch_ = 0;  // Vertical look angle in degrees (-89 to 89)
    uint16_t playerSpawnId_ = 0;  // Spawn ID of the player entity

    // Player light source (lantern, lightstone, etc.) - always highest priority in light pool
    irr::scene::ILightSceneNode* playerLightNode_ = nullptr;
    uint8_t playerLightLevel_ = 0;

    // HUD elements
    irr::gui::IGUIStaticText* hudText_ = nullptr;
    irr::gui::IGUIStaticText* hotkeysText_ = nullptr;  // Hotkey hints in upper right
    float hudAnimTimer_ = 0.0f;  // Timer for HUD animations (auto attack indicator)

    // Performance: HUD dirty tracking to avoid rebuilding every frame
    // K15 (B034): cachedHudText_ / cachedHotkeysText_ / cachedHeadingDebugText_
    // deleted -- zero references; the dirty tracking below is what is live.
    // L03: HudCachedState and hudCachedState_ moved to OverlaysComponent.

    // FPS tracking
    int currentFps_ = 0;
    int frameCount_ = 0;
    irr::u32 lastFpsTime_ = 0;

    // Polygon count tracking (for constrained mode)
    uint32_t lastPolygonCount_ = 0;
    int polygonBudgetExceededFrames_ = 0;  // Throttle warnings
    int constrainedStatsLogCounter_ = 0;   // Periodic stats logging

    PlayerMovementState playerMovement_;
    PlayerModeConfig playerConfig_;
    eqt::bridge::GameStateBridge* bridge_ = nullptr;  // For pushing intents (D14+)

    // Collision detection for player mode (shared ownership — P35)
    std::shared_ptr<const HCMap> collisionMap_;

    // L01 (Batch L, refactor/L_renderer_component_extraction.md): the
    // shared-state bundle every extracted renderer component binds by
    // reference. Declared after every member it references; built in the
    // ctor initialiser list (renderer_init.cpp). See renderer_context.h.
    RendererContext ctx_;

    // Target selection
    uint16_t currentTargetId_ = 0;
    std::string currentTargetName_;

    // Irrlicht-based collision (using zone geometry directly)
    // Legacy meta selectors — only used by HCMap fallback/debug path (no BSP)
    irr::scene::ITriangleSelector* zoneTriangleSelector_ = nullptr;
    irr::scene::ITriangleSelector* terrainOnlySelector_ = nullptr;
    irr::scene::ISceneCollisionManager* collisionManager_ = nullptr;

    // BSP-filtered collision: pre-transformed world-space triangles — no matrix math per query
    // All collision paths (movement, detail, footprints, LOS) use direct Möller–Trumbore intersection
    std::map<size_t, std::vector<irr::core::triangle3df>> regionWorldTriangles_;  // Zone geometry per BSP region
    std::map<size_t, std::vector<irr::core::triangle3df>> objectWorldTriangles_;  // Placeable objects per BSP region

    // Pre-baked door collision: open + closed world-space triangles per door
    // During animation (opening/closing), door has no collision (walk-through, matching original EQ behavior)
    struct DoorCollisionData {
        std::vector<irr::core::triangle3df> closedTriangles;
        std::vector<irr::core::triangle3df> openTriangles;
        size_t bspRegion = SIZE_MAX;
        bool hasCollision = true;  // false for invisible, teleporter, spinning, lifts, hazardous
    };
    std::map<uint8_t, DoorCollisionData> doorCollisionData_;
    bool useIrrlichtCollision_ = true;  // Use zone mesh for collision instead of HCMap

    // Irrlicht collision methods (full meta selector — cold path: LOS, detail)
    bool checkCollisionIrrlicht(const irr::core::vector3df& start, const irr::core::vector3df& end,
                                 irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle);
    // Find ground Z at position. currentZ is the node origin (server Z); modelYOffset is the origin-to-feet distance.
    float findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset);

    // BSP-filtered collision methods (hot path: movement) — tests only source+dest region + doors/objects
    bool checkCollisionBspFiltered(const irr::core::vector3df& start, const irr::core::vector3df& end,
                                   irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle);
    float findGroundZBspFiltered(float x, float y, float currentZ, float modelYOffset);
    // Direct ray-triangle intersection on pre-transformed world-space triangles (no matrix math)
    static bool rayIntersectTriangles(const irr::core::vector3df& rayOrigin,
                                      const irr::core::vector3df& rayDir, float rayLen,
                                      const std::vector<irr::core::triangle3df>& triangles,
                                      irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle,
                                      float& hitDistSq);
    // Helper: test ray against all doors+objects in a BSP region (direct Möller–Trumbore)
    bool rayTestDoorsAndObjects(const irr::core::vector3df& rayOrigin,
                                const irr::core::vector3df& rayDir, float rayLen,
                                size_t bspRegion,
                                irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle,
                                float& hitDistSq);
    // Helper: extract world-space triangles from a mesh node (called once at build time)
    static std::vector<irr::core::triangle3df> extractWorldTriangles(
        irr::scene::ISceneManager* smgr, irr::scene::IMeshSceneNode* node);

    // Debug visualization for collision rays
    struct CollisionDebugLine {
        irr::core::vector3df start;
        irr::core::vector3df end;
        irr::video::SColor color;
        float timeRemaining;  // How long to display (seconds)
    };
    std::vector<CollisionDebugLine> collisionDebugLines_;
    void addCollisionDebugLine(const irr::core::vector3df& start, const irr::core::vector3df& end,
                               const irr::video::SColor& color, float duration = 0.5f);
    void drawCollisionDebugLines(float deltaTime);
    void clearCollisionDebugLines();

    // L03 (Batch L): the target highlight, the zone-line debug/bounding-box
    // visualization, the map overlay and the navmesh overlay -- their draw
    // methods, their nested triangle/box types and their single-family state
    // -- moved to OverlaysComponent. Only the members the frame family also
    // touches stayed here (Batch K Mechanism 3); the overlay TUs reach them
    // as owner_.X_.
    bool showMapOverlay_ = false;
    int mapOverlayRotation_ = 0;       // Rotation index: 0=0°, 1=90°, 2=180°, 3=270° around Y axis
    bool mapOverlayMirrorX_ = false;   // Mirror placeables across YZ plane (negate X)
    bool showNavmeshOverlay_ = false;
    int navmeshOverlayRotation_ = 0;   // Rotation index: 0=0°, 1=90°, 2=180°, 3=270° around Y axis
    bool navmeshOverlayMirrorX_ = false;   // Mirror across YZ plane (negate X)

    // U07c3: WindowManager deleted — inventoryManager remains for bridge data access

    // Spell visual effects
    std::unique_ptr<EQ::SpellVisualFX> spellVisualFX_;
    std::unique_ptr<TextBatch> textBatch_;  // U01
    std::unique_ptr<UIAtlas> uiAtlas_;     // U02
    std::unique_ptr<UIRenderer> uiRenderer_;  // U03b
    UILayout uiLayout_;                       // U03a
    bool newUIEnabled_ = true;                // U07c1: new UI on by default

    // T60: inventory paperdoll. Constructed lazily on first popup open
    // (ensurePaperdollInitialized()) — never at renderer init, so a
    // player who never opens the inventory never pays the RTT's memory
    // cost. Updated (appearance + RTT re-render) only while the popup is
    // open (updatePaperdollForFrame(), called from the frame loop) and
    // only on markDirty() triggers, never per frame — see
    // refactor/T60_paperdoll_restore.md's Review.
    // L03: the doll itself stays (renderer_entities and the frame family
    // reach it); its bookkeeping members and both methods moved to
    // OverlaysComponent.
    std::unique_ptr<eqt::ui::CharacterModelView> characterModelView_;

    // T319: see handleActionResult()/requestChatPrefill()/takeChatPrefill()
    // above. Written on the game thread, read on the render thread; every
    // access goes through those accessors.
    mutable std::mutex chatPrefillMutex_;
    std::optional<std::string> pendingChatPrefill_;
public:
    // U03d+: Cached data from bridge events for new static UI panels.
    // Public so IrrlichtBridge can update them directly.
    PlayerStatsData cachedPlayerStats_;
    TargetInfoData cachedTargetInfo_;
    ChatPanelState chatPanelState_;        // U04
    std::unique_ptr<eqt::ui::ChatMessageBuffer> newUIChatBuffer_;  // U04
    InventoryPanelState inventoryState_;  // U05
    HotbarPanelState hotbarState_;       // U06a
    SpellGemPanelState spellGemState_;  // U06b
    BuffBarState buffBarState_;         // U06c
    CastingBarState castingBarState_;  // U06d
    GroupPanelState groupPanelState_;  // U06e
    PetPanelState petPanelState_;       // U06f
    SpellbookPopupState spellbookState_; // U06g
    SkillsPopupState skillsPopupState_; // U06h
    // T290: renderer-side corpse contents, restored. Fed only by the four
    // loot bridge arms (applyLoot*), read by renderLootPopup() and
    // StaticUIInput's row/button hit tests.
    LootPanelState lootState_;          // T290
    XPBarState xpBarState_;             // U06i
    std::unique_ptr<StaticUIInput> staticUIInput_;  // U06j

    // T60: called by IrrlichtBridge on InventorySlotChanged (equip slots
    // 0-21) — an equip-slot display change usually means the entity's
    // appearance changed too, so the doll should re-render next time it's
    // visible. Safe to call even if the doll doesn't exist yet or the
    // popup is closed (no-op in both cases). L03: a one-line forwarder to
    // OverlaysComponent, which owns the body (the K11 rule).
    void markPaperdollDirty() { overlays_->markPaperdollDirty(); }
private:

    // T74: snap camera_ to the player's own eye position/facing. The only
    // thing that makes a camera transform actually "first person" --
    // setCameraMode()/cycleCameraMode() previously only flipped cameraMode_
    // and left the camera transform wherever the Follow camera last put it
    // (T74 stage-1 diagnostic finding: a stationary player toggling into
    // first person kept looking through the stale ~20-unit-back Follow
    // shot until the next server position update happened to call
    // setPlayerPosition(), which is the only other place this math lived).
    // No-op if camera_ is null or the player model isn't loaded yet
    // (eyeHeight <= 0); safe to call unconditionally on entering
    // FirstPerson mode.
    void repositionFirstPersonCamera();

    // T300: first person is the ORDINARY player model seen from the eyes,
    // hidden by a large near clip plane -- not a separate pass. This applies
    // the projection that mechanism needs (near plane + the official client's
    // horizontal FOV) on entering first person, and restores the world
    // defaults on leaving it. Safe to call unconditionally; reads
    // cameraMode_.
    void applyCameraProjectionForMode();

    // T300: THE HIDING SEAM. Everything that hides the player's own body in
    // first person is in this one body. It clamps the stored mouse-look pitch
    // so the frustum's lower edge never passes the nadir -- the body hangs
    // directly below the eye, so a view that cannot look past straight down
    // cannot contain it. The clamp angle is DERIVED from the camera's vertical
    // FOV (90 - vFOV/2), which makes it scale-, race- and shrink/grow-
    // invariant with no size term of any kind.
    //
    // Called every frame before the scene draw, and on mode changes (entering
    // first person from a third-person view that was pitched further down than
    // the clamp allows has to be corrected immediately). A no-op outside first
    // person, and a no-op in first person unless the pitch was out of range.
    //
    // Leg A REFUTED the near-plane hypothesis this seam previously held: the
    // official client clips nothing, and the size sweep killed both the fixed
    // and the size-derived plane. If the mechanism moves again, this body and
    // firstPersonMaxDownPitchDegrees() are what get replaced.
    void applyFirstPersonPitchClamp();

    // The vertical FOV the first-person camera is actually using, in radians.
    // One answer, so the clamp derivation and the projection cannot disagree.
    float getFirstPersonVerticalFov() const;

    // T310 Leg A: `/fpcam dump` -- the RESOLVED first-person pose as parseable
    // [FPCAM-DUMP] key=value lines, each stating its units and frame. This is
    // what Leg C's sweep driver reads back; it must never be asserted on via
    // the dispatcher's `outcome=`, which registry 3 always reports as
    // "queued".
    std::vector<std::string> buildFirstPersonCameraDumpLines() const;

    // World objects for click detection (tradeskill containers, etc.)
    struct WorldObjectVisual {
        uint32_t dropId;
        float x, y, z;           // EQ coordinates
        uint32_t objectType;
        std::string name;
        irr::core::aabbox3df boundingBox;  // For click detection
    };
    std::map<uint32_t, WorldObjectVisual> worldObjects_;
    uint32_t getWorldObjectAtScreenPos(int screenX, int screenY) const;
    uint32_t getNearestWorldObject(float playerX, float playerY, float playerZ, float maxDistance = 50.0f) const;

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
    FrameTimings frameTimings_;
    FrameTimings frameTimingsAccum_;  // Accumulated over multiple frames
    int frameTimingsSampleCount_ = 0;
    bool frameTimingEnabled_ = false;  // Enable with /frametiming command

    // Render pass timer - hooks into Irrlicht's ILightManager to time individual render passes
    class RenderPassTimer : public irr::scene::ILightManager {
    public:
        // Per-pass accumulated times (reset each frame via reset())
        int64_t solidTime = 0;
        int64_t transparentTime = 0;  // TRANSPARENT + TRANSPARENT_EFFECT
        int64_t skyboxTime = 0;
        int64_t otherTime = 0;        // CAMERA + LIGHT + SHADOW
        int nodeCount = 0;

        void reset() {
            solidTime = 0; transparentTime = 0; skyboxTime = 0; otherTime = 0;
            nodeCount = 0; firstPassSeen_ = false;
        }

        void OnPreRender(irr::core::array<irr::scene::ISceneNode*>& lightList) override {
            preRenderTime_ = std::chrono::steady_clock::now();
        }
        void OnPostRender() override {}
        void OnRenderPassPreRender(irr::scene::E_SCENE_NODE_RENDER_PASS renderPass) override;
        // Implementation in renderer_zone_draw.cpp (needs access to IrrlichtRenderer)
        void OnRenderPassPostRender(irr::scene::E_SCENE_NODE_RENDER_PASS renderPass) override {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - passStart_).count();
            switch (renderPass) {
                case irr::scene::ESNRP_SOLID:
                    solidTime += elapsed;
                    // P52: close the entity-pass GPU query if OnRenderPassPreRender opened
                    // one (only happens when manualZoneDrawEnabled_ split zone from entity
                    // draw this frame — see OnRenderPassPreRender in renderer_zone_draw.cpp).
                    if (entityGPUQueryActive_) {
                        eqtGPUPhaseEnd(1 /* GPUSyncPhase::EntityPass */);
                        entityGPUQueryActive_ = false;
                    }
                    break;
                case irr::scene::ESNRP_TRANSPARENT:
                case irr::scene::ESNRP_TRANSPARENT_EFFECT:
                    transparentTime += elapsed; break;
                case irr::scene::ESNRP_SKY_BOX:
                    skyboxTime += elapsed; break;
                default:
                    otherTime += elapsed; break;
            }
        }
        void OnNodePreRender(irr::scene::ISceneNode* node) override { nodeCount++; }
        void OnNodePostRender(irr::scene::ISceneNode* node) override {}

        // Time between OnPreRender and first pass = animate+register overhead
        int64_t getAnimateTime() const {
            if (!firstPassSeen_) return 0;
            return std::chrono::duration_cast<std::chrono::microseconds>(
                firstPassStart_ - preRenderTime_).count();
        }

        // Set renderer for manual zone draw hook
        void setRenderer(IrrlichtRenderer* r) { renderer_ = r; }

    private:
        IrrlichtRenderer* renderer_ = nullptr;
        std::chrono::steady_clock::time_point passStart_;
        std::chrono::steady_clock::time_point firstPassStart_;
        std::chrono::steady_clock::time_point preRenderTime_;
        bool firstPassSeen_ = false;
        irr::scene::E_SCENE_NODE_RENDER_PASS currentPass_ = irr::scene::ESNRP_NONE;
        // P52: tracks whether a GPU timer query for the entity pass (GPUSyncPhase::EntityPass)
        // was opened this SOLID pass, so OnRenderPassPostRender only closes it when it was
        // actually opened (manualZoneDrawEnabled_ gates whether zone/entity are split at all).
        bool entityGPUQueryActive_ = false;
    };
    RenderPassTimer* renderPassTimer_ = nullptr;  // Owned by Irrlicht (ref counted)

    // 3D camera transforms captured during SOLID pass (before any 2D drawing overwrites them)
    irr::core::matrix4 captured3DView_;
    irr::core::matrix4 captured3DProj_;
    bool have3DTransforms_ = false;

    // Scene breakdown profiler - profiles drawAll() by node category
    struct SceneBreakdown {
        int64_t totalDrawAll = 0;      // Full scene render time
        int64_t withoutZone = 0;       // Time without zone mesh
        int64_t withoutEntities = 0;   // Time without entities
        int64_t withoutObjects = 0;    // Time without placeable objects
        int64_t withoutDoors = 0;      // Time without doors
        // Derived (calculated from differences)
        int64_t zoneTime = 0;
        int64_t entityTime = 0;
        int64_t objectTime = 0;
        int64_t doorTime = 0;
        int64_t otherTime = 0;
        // Counts
        int zonePolys = 0;
        int entityCount = 0;
        int objectCount = 0;
        int doorCount = 0;
    };
    bool sceneProfileEnabled_ = false;
    int sceneProfileFrameCount_ = 0;

    // L04 (Batch L): the diagnostics family -- scene dump, profiling, memory
    // report and the runtime debug toggles -- now lives in
    // DiagnosticsComponent. Constructed in the IrrlichtRenderer ctor body;
    // see renderer_init.cpp. It reads 39 members that are shared with other
    // families but sit below the context's 5-family core threshold, plus the
    // private nested type SceneBreakdown, so it needs friendship (both axes of
    // K13 correction ii).
    friend class DiagnosticsComponent;
    std::unique_ptr<DiagnosticsComponent> diagnostics_;

    // L02 (Batch L): the EQE1 archive family and the fire-glow icospheres.
    // Constructed in the IrrlichtRenderer ctor body; see renderer_init.cpp.
    std::unique_ptr<Eqe1ArchiveComponent> eqe1_;

#ifdef WITH_RDP
    // L01 (Batch L): the rdp family -- the server handle, the caps-lock
    // state and the four handlers -- now lives in RdpComponent. Constructed
    // in the IrrlichtRenderer ctor body; see renderer_init.cpp.
    std::unique_ptr<RdpComponent> rdp_;
#endif // WITH_RDP

    // L03 (Batch L): the overlays family -- HUD, map/navmesh/zone-line
    // overlays, mouse targeting, the loading screen, fog and the paperdoll.
    // Constructed in the IrrlichtRenderer ctor body; see renderer_init.cpp.
    std::unique_ptr<OverlaysComponent> overlays_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_IRRLICHT_RENDERER_H
