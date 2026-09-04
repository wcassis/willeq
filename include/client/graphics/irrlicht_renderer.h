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
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/slash/slash_commands_component.h"
#include "client/graphics/sim/sim_bridge_component.h"
#include "client/graphics/zone_stream/zone_stream_component.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
#include "client/graphics/zone_load/zone_load_component.h"
#include "client/graphics/frame/frame_component.h"
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
#include "client/graphics/zone_load_surface.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/renderer_draw_types.h"
#include "client/graphics/render_pass_timer.h"  // L13: RenderPassTimer, hoisted out of this class

namespace EQT {
namespace Graphics {

// Main Irrlicht renderer class. K16 (B006): implements IZoneLoadSurface, the
// two-method zone-load surface the game side calls through.
class IrrlichtRenderer : public IZoneLoadSurface {
public:
    IrrlichtRenderer();
    ~IrrlichtRenderer();

    // Initialize the renderer (window, GPU, subsystems, background thread pool).
    // Use this at startup for early progress display, then call loadGlobalAssets() later.
    bool initLoadingScreen(const RendererConfig& config) override;

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

    // D20d: renderer-specific slash commands forwarded from the game thread.
    // L06: SlashCommandsComponent implements it; forwarder kept for the bridge.
    void processSlashCommand(const std::string& command) { slash_->processSlashCommand(command); }

    // Shutdown the renderer
    void shutdown();

    // Check if renderer is running
    bool isRunning() const;

    // Request the renderer to quit (for /q, /quit commands)
    void requestQuit();

    // L09 (Batch L): zone lifecycle/draw/upload live on
    // ZoneLifecycleComponent. These six stay as forwarders, each justified by
    // a NAMED caller listed in zone_lifecycle_component.h (the K11 rule).
    void unloadZone() { zoneLifecycle_->unloadZone(); }
    void setupInstantScene(const std::string& zoneName, float playerX = 0, float playerY = 0, float playerZ = 0) {
        zoneLifecycle_->setupInstantScene(zoneName, playerX, playerY, playerZ);
    }
    void storeZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                              const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                              const float fogMinClip[4], const float fogMaxClip[4]) {
        zoneLifecycle_->storeZoneEnvironment(skyType, zoneType, fogRed, fogGreen, fogBlue, fogMinClip, fogMaxClip);
    }
    void setExpectedEntityCount(size_t count) { zoneLifecycle_->setExpectedEntityCount(count); }
    void setNetworkReady(bool ready) { zoneLifecycle_->setNetworkReady(ready); }
    void setWeather(uint8_t type, uint8_t intensity) { zoneLifecycle_->setWeather(type, intensity); }

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

    // L08 (Batch L): deferred/progressive asset loading lives on
    // ZoneStreamComponent -- reach it through zoneStream_-> in the renderer,
    // owner_.ZoneStream() from another component.

    // Create and configure EntityRenderer (idempotent — skips if already created)
    void createEntityRenderer();

    // L08 (Batch L): the three T124 guard-registration helpers live on
    // ZoneStreamComponent.

    // L09 (Batch L): instant-scene bring-up and the zone-environment trio are
    // on ZoneLifecycleComponent; two of them forward from above.
    // L08 (Batch L): progressive-load completion, the T111 blocking
    // predicate, the entity-prep request queue and the load timing/progress
    // reporting all live on ZoneStreamComponent. The T111 predicate stays a
    // public static there, and tests/test_progressive_load_rearm.cpp calls it
    // as ZoneStreamComponent::isEntityBlockingProgressiveLoad(...).
    // Check if progressive loading is active
    bool isProgressiveLoadingActive() const { return progressiveLoadingActive_; }
    // L10 (Batch L): the zone-load step sequence lives on ZoneLoadComponent;
    // these two forward for application_zone_loading.cpp:275,277 (K11 rule).
    void loadZoneSequential(const std::string& p, LoadingStatus& s) { zoneLoad_->loadZoneSequential(p, s); }
    bool hasLoadFailed() const { return zoneLoad_->hasLoadFailed(); }

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
    bool isFirstPersonMode() const { return PlayerCamera().cameraMode() == CameraMode::FirstPerson; }

    // L05 (Batch L): the player family -- movement, collision, ground height
    // and the camera -- lives in PlayerCameraComponent. The four methods below
    // stay as one-line forwarders ONLY because these callers hold an
    // IrrlichtRenderer and must not learn a component exists (the K11 rule):
    //   setPlayerSpawnId  -- application_zone_loading.cpp:249,359;
    //                        bridge/irrlicht_bridge.cpp:69,173
    //   setVisionType     -- bridge/irrlicht_bridge.cpp:582
    //   setPlayerPosition -- application_zone_loading.cpp:270,380;
    //                        bridge/irrlicht_bridge.cpp:24
    //   setSwimmingState  -- bridge/irrlicht_bridge.cpp:629
    // setPlayerRace(), resetVisionToBase(), getBaseVision() and isSwimming()
    // got NO forwarder: each has zero callers on an IrrlichtRenderer receiver,
    // and a forwarder for surface nothing calls is exactly what this batch
    // removes. Reach them through PlayerCamera(). T334 (B046) DELETED
    // setSwimUp(), setSwimDown() and setLevitating(): the first two were never
    // the sole writers of playerMovement_.swimUp/.swimDown -- the G06 input
    // poll assigns both every frame from the [ and ] bindings -- and the third
    // duplicated setSwimmingState()'s levitating parameter. resetVisionToBase()
    // is no longer a dead end either: setVisionType() calls it (B046).
    void setPlayerSpawnId(uint16_t spawnId) { playerCamera_->setPlayerSpawnId(spawnId); }
    void setVisionType(VisionType vision) { playerCamera_->setVisionType(vision); }
    void setPlayerPosition(float x, float y, float z, float heading) { playerCamera_->setPlayerPosition(x, y, z, heading); }
    void setSwimmingState(bool swimming, float swimSpeed = 20.0f, bool levitating = false) { playerCamera_->setSwimmingState(swimming, swimSpeed, levitating); }

    // Get current vision type
    VisionType getVisionType() const { return currentVision_; }

    // Get player's current Z (may differ from set value if snapped to ground)
    float getPlayerZ() const { return playerZ_; }

    // The player family is otherwise reached through this accessor; it is also
    // what the component's tests bind against.
    PlayerCameraComponent& PlayerCamera() const { return *playerCamera_; }

    // L07 (Batch L): the SimulationWorker bridge. In-renderer callers reach
    // the family through simBridge_->; this accessor is for the T116 lifetime
    // fixtures and the component's own test.
    SimBridgeComponent& SimBridge() const { return *simBridge_; }

    // L08 (Batch L): zone streaming -- progressive load, PVS neighbourhood,
    // deferred objects and the T124 texture guards.
    ZoneStreamComponent& ZoneStream() const { return *zoneStream_; }

    // L09 (Batch L): unload/environment/ready state, zone draw, GPU upload.
    ZoneLifecycleComponent& ZoneLifecycle() const { return *zoneLifecycle_; }

    // L10 (Batch L): loadZoneSequential() and its twenty step methods.
    ZoneLoadComponent& ZoneLoad() const { return *zoneLoad_; }

    // L11 (Batch L): the per-frame loop, the input drain and the static UI
    // panel state. Reached by the input tests, by the bridge (the UI panel
    // members are public on the component, as they were here) and by the
    // sibling components that toggle UI state.
    FrameComponent& Frame() const { return *frame_; }

    // Get BSP tree for water region detection
    std::shared_ptr<EQT::Graphics::BspTree> getZoneBspTree() const { return zoneBspTree_; }

    // Update time of day lighting (hour 0-23, minute 0-59)
    void updateTimeOfDay(uint8_t hour, uint8_t minute);

    // Camera control modes.
    // L05 (Batch L): the enum was nested here. PlayerCameraComponent's
    // setCameraMode(CameraMode) needs it, and the component header cannot
    // include this one, so the enum moved to client/graphics/renderer_types.h
    // beside VisionType. This alias keeps every existing spelling working --
    // EQT::Graphics::IrrlichtRenderer::CameraMode::Follow in
    // application_zone_loading.cpp:268,378, renderer_slash_commands.cpp:126-130
    // and tests/test_irrlicht_camera_command.cpp still resolve, unchanged.
    using CameraMode = EQT::Graphics::CameraMode;
    // Forwarders, each with a real caller on an IrrlichtRenderer receiver:
    //   setCameraMode       -- application_zone_loading.cpp:268,378;
    //                          tests/test_zoning_graphics_integration.cpp:729
    //   getCameraModeString -- tests/test_irrlicht_camera_command.cpp:68,106
    // cycleCameraMode() is NOT here: L05 measured zero callers on this class.
    // GraphicalMode calls it through std::unique_ptr<output::IOutputRenderer>
    // and reaches output_renderer.h's empty default body -- IrrlichtRenderer
    // has NO base class (L03 correction 13). Its one real caller is
    // renderer_slash_commands.cpp, in the renderer, which reaches the
    // component. getCameraTransform() was gone from this header for the same
    // reason and T334 (B045) deleted it outright: the audio listener it was
    // written for has had a producer since D20c2, and that producer uses the
    // PLAYER position by design (BridgeComponent::UpdateAudioListener).
    void setCameraMode(CameraMode mode) { playerCamera_->setCameraMode(mode); }
    CameraMode getCameraMode() const { return PlayerCamera().cameraMode(); }
    std::string getCameraModeString() const { return playerCamera_->getCameraModeString(); }

    // Process input and render a single frame; false means stop.
    // L11 (Batch L): FrameComponent owns the loop and all four phases. This
    // forwarder stays for application_frame.cpp:181, run() below and two
    // integration tests -- Application must not learn a component exists
    // (the K11 rule). The phase methods are reached through Frame().
    bool processFrame(float deltaTime) { return frame_->processFrame(deltaTime); }

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
    bool isAllUIHidden() const { return frame_->allUIHidden_; }

    // Front-to-back sorting and portal occlusion controls
    bool isManualZoneDrawEnabled() const { return manualZoneDrawEnabled_; }

    // P10: cross-region zone draw merge (see renderer_zone_merge.h). Runtime
    // toggle mirrors /sort's pattern -- a renderer-owned bool separate from
    // the config-loaded mergeZoneDraws, which only supplies the initial value
    // at zone load.
    bool isMergeZoneDrawsEnabled() const { return mergeZoneDrawsEnabled_; }

    // L09 (Batch L): the GPU upload thread's renderer-side driver is on
    // ZoneLifecycleComponent; SlashCommandsComponent is its only caller.

    // Debug lighting toggles (for isolating performance regressions) --
    // the toggles themselves are on DiagnosticsComponent (L04); these read
    // the resulting state.
    bool isPlayerLightEnabled() const { return Diagnostics().debugPlayerLightEnabled(); }
    bool isObjectLightsEnabled() const { return Diagnostics().debugObjectLightsEnabled(); }
    bool isDirectionalLightEnabled() const { return Diagnostics().debugDirectionalLightEnabled(); }
    bool isPortalOcclusionEnabled() const { return portalOcclusionEnabled_; }
    bool isPortalDebugDrawEnabled() const { return Diagnostics().portalDebugDraw(); }
    bool isStencilDebugDrawEnabled() const { return Diagnostics().stencilDebugDraw(); }
    void setFrameTimingEnabled(bool enabled);  // Enable/disable frame timing profiler
    bool isFrameTimingEnabled() const { return Frame().frameTimingEnabled(); }
    // L04 (Batch L): implemented by DiagnosticsComponent. These three stay as
    // one-line forwarders because irrlicht_bridge.cpp (dumpScene,
    // getMemoryReport) and application.cpp (runSceneProfile) call them and
    // must not learn a component exists -- the K11 rule.
    void dumpScene() const { diagnostics_->dumpScene(); }  // /dumpscene debug command
    void runSceneProfile() { diagnostics_->runSceneProfile(); }  // Run scene breakdown profiler (profiles next frame)
    DiagnosticsComponent& Diagnostics() const { return *diagnostics_; }
    // K15 (B033): resetCoordOffsets / adjustOffset{X,Y,Z} / getOffset{X,Y,Z} /
    // adjustRotation{X,Y,Z} / getRotation{X,Y,Z} and stepToString (above) were
    // 14 declarations with no definition anywhere -- the real offset/rotation
    // pair lives on EntityRenderer. Deleted; the linker never saw them.

    // Unified render distance (controls fog and object culling, NOT camera far plane)
    // The camera far plane must be larger to include the sky dome
    void setRenderDistance(float distance) {
        if (isLoading()) return;
        ZoneLifecycle().userRenderDistance() = distance;
        // Cap effective render distance by constrained preset's clip distance
        if (config_.constrainedConfig.clipDistance > 0.0f) {
            distance = std::min(distance, config_.constrainedConfig.clipDistance);
        }
        // Cap effective render distance by server-provided zone max clip plane
        renderDistance_ = (ZoneLifecycle().zoneMaxClip() > 0.0f) ? std::min(distance, ZoneLifecycle().zoneMaxClip()) : distance;
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
    OverlaysComponent& Overlays() const { return *overlays_; }
    irr::gui::IGUIEnvironment* getGUIEnvironment() { return Overlays().guienv(); }

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
    uint16_t getCurrentTargetId() const { return Overlays().currentTargetId(); }

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
    void initDeferredGraphics() override;
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
    // L09 (Batch L): on ZoneLifecycleComponent, forwarded above.
    // notifyEntityLoaded(), checkAndSetZoneReady() and areEntitiesLoaded() had
    // ZERO callers and were deleted (§Mechanism 5) -- see the component header.
    bool isNetworkReady() const { return ZoneLifecycle().networkReady(); }

    // Loading progress for zone transitions
    void setLoadingProgress(float progress, const std::wstring& text) {
        if (isLoading()) return;
        Frame().loadingProgress() = progress;
        Frame().loadingText() = text;
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
    // isZoneLineVisualizationEnabled() had zero callers tree-wide (Mechanism 5);
    // T354 then deleted the debug border those wrote for (B042).
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
    EQ::SpellVisualFX* getSpellVisualFX() { return SimBridge().spellVisualFX().get(); }

    // U01: Batched text renderer
    TextBatch* getTextBatch() { return textBatch_.get(); }

    // U02: UI sprite atlas
    UIAtlas* getUIAtlas() { return uiAtlas_.get(); }

    // U03: Static layout UI
    UIRenderer* getUIRenderer() { return frame_->uiRenderer_.get(); }
    bool isNewUIEnabled() const { return frame_->newUIEnabled_; }
    void setNewUIEnabled(bool v) { frame_->newUIEnabled_ = v; }

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

    // L09 (Batch L): setWeather() is on ZoneLifecycleComponent, forwarded above.

#ifdef WITH_RDP
    // RDP server support (alternative to Xvfb+x11vnc). L01 (Batch L) moved
    // the implementation to RdpComponent; these five stay as one-line
    // forwarders because Application calls them and must not learn a
    // component exists (the K11 rule). getRDPClientCount() had zero callers
    // tree-wide and was deleted.
    RdpComponent& Rdp() const { return *rdp_; }
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
    Eqe1ArchiveComponent& Eqe1() const { return *eqe1_; }

private:
    void createSoftwareCursor();
    void setupCamera();
    void setupLighting();
    // L07 (Batch L): the nine SimulationWorker bridge methods live on
    // SimBridgeComponent -- including the T116 lifetime split between
    // applySimulationOutput() (value-only) and bindFrameSimulationBuffers()
    // (the only pointer-storing half), whose contract is stated in full in
    // include/client/graphics/sim/sim_bridge_component.h. Nothing outside the
    // renderer ever called any of them, so there is no forwarder.

    // L10 (Batch L): the step helpers and ZoneLoadContext moved to
    // ZoneLoadComponent; L12 folded in the T109 load gate too (L10 dev 1).

    // advanceDeferredInit() folded into P11_Env_* phases of advanceBackgroundZoneLoad()
    // L09 (Batch L): createZoneMesh()/rebuildRegionMesh() moved.
    // updateZoneLightColors() removed — vision/weather modifiers now applied in SimulationWorker
    void updateObjectLightColors(float deltaTime = 0.0f);  // Update object light colors based on weather + fire flicker

    // PVS visibility check for a BSP region (used at insertion time for objects/lights)
    bool isRegionPvsVisible(size_t regionIdx) const;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::scene::ICameraSceneNode* camera_ = nullptr;

    std::unique_ptr<CameraController> cameraController_;
    std::unique_ptr<FrustumCuller> frustumCuller_;
    std::unique_ptr<SoftwareOcclusionCuller> occlusionCuller_;
    std::unordered_set<size_t> occlusionCulledRegions_;
    // K15 (B034): the occlusion camera-movement gating members
    // (lastOccCam{X,Y,Z}, lastOccFwd{X,Y,Z}) had zero references tree-wide --
    // the gate was never implemented. Deleted.
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
    // L13: portalVisibleRegions_ STAYS. ZoneLifecycleComponent has 4 of its 5
    // reaches and the pairwise rule would move it, but it is one of the FOUR
    // SimulationWorker-output pointers whose shared cross-thread lifetime
    // invariant tests/test_simulation_output_lifetime.cpp guards as a group
    // (T115/T116) -- the other three are context members or stay here, and
    // splitting the group is exactly what that fixture exists to catch.
    const std::unordered_set<size_t>* portalVisibleRegions_ = nullptr;
    const SimulationOutput* currentFrameSimResults_ = nullptr;
#ifdef EQT_HAS_GLES2
    std::unique_ptr<GPUUploadThread> gpuUploadThread_;   // Async GPU upload thread (shared EGL context)
    std::unordered_set<size_t> pendingVBOUploads_;       // Region indices with VBO uploads in flight
    // L09 (Batch L): gpuUploadEnabled_ + the four driver methods moved.
    // Split drain/process-one for GREEN-gated post-loading work
    std::vector<UploadResult> pendingGPUResults_;  // Drained from GPU upload thread
#endif

    // L07: the particle I/O state for SimulationWorker moved to
    // SimBridgeComponent (single-family).
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

    // L09 (Batch L): gles2Helper_ moved to ZoneLifecycleComponent.
    std::unique_ptr<Environment::BoidsManager> boidsManager_;  // Ambient creatures (boids)
    std::unique_ptr<Environment::TumbleweedManager> tumbleweedManager_;  // Tumbleweeds (desert/plains)

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
    // L09 (Batch L): drawZoneGeometrySorted() is on ZoneLifecycleComponent.

    // P10: cross-region zone draw merge state (see renderer_zone_merge.h).
    // mergeZoneDrawsEnabled_ is initialized from config_.constrainedConfig.mergeZoneDraws
    // at zone load and independently runtime-toggleable via /mergedraws.
    // zoneMergeBuffers_ is built lazily (once the region set is fully resident
    // with zero mesh-cache evictions so far) and destroyed + permanently
    // disabled for the rest of the zone visit the instant an eviction occurs
    // (mergeZoneDrawsDisabledForZone_) -- see renderer_zone_merge.h's header
    // comment for the reduced-landing rationale.
    bool mergeZoneDrawsEnabled_ = false;

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
    // L13: softwareCursorTexture_ STAYS. FrameComponent reads it twice, but
    // IrrlichtRenderer::createSoftwareCursor() creates it and the guard below
    // binds a REFERENCE to it that the texture cache holds as an eviction
    // listener -- moving the referent into a component would make its lifetime
    // depend on component teardown order. Correction 10's init/core terminal
    // owner, and a lifetime question a structural unit must not open.
    irr::video::ITexture* softwareCursorTexture_ = nullptr;  // DRM mode software cursor
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

    // L08: doorTextureGuard_ moved to ZoneStreamComponent (single-family).

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
    // L08: boidsTextureGuard_ moved to ZoneStreamComponent (single-family).

    // L09 (Batch L): the VBO/EBO helpers moved.

    // Portal occlusion system
    std::unique_ptr<PortalSystem> portalSystem_;
    bool portalOcclusionEnabled_ = false;
    bool portalOcclusionEligible_ = false;

    // Per-region indoor classification (loaded from data/region_maps/<zone>.json)
    std::unordered_set<size_t> indoorRegions_;

    // Region neighbor map (built from portal system for 1-depth PVS expansion)
    std::unordered_map<size_t, std::vector<size_t>> regionNeighbors_;
    // L09 (Batch L): buildRegionNeighborMap() is on ZoneLifecycleComponent.

    // Portal-based entity culling — pointer to SimulationOutput front buffer
    // (no copy). T116: set ONLY by bindFrameSimulationBuffers().

    // PVS depth map — pointer to SimulationOutput front buffer (no copy).
    // T116: set ONLY by bindFrameSimulationBuffers().
    const std::unordered_map<size_t, uint8_t>* regionPvsDepthMap_ = nullptr;
    // L08: getRegionPvsDepth()/onPvsRegionChanged() are on ZoneStreamComponent.

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
    // L08: rebuildPvsNeighborhood()/isInPvsNeighborhood() are on
    // ZoneStreamComponent; the two members above stay here (zone_lifecycle
    // clears them on unload).

    // L09 (Batch L): the portal/stencil draw recursion moved.

    // Debug visualization for portals/stencil

    // Zone placeholder mesh (HCMap collision mesh as visual context during loading)
    irr::scene::IMeshSceneNode* zonePlaceholderNode_ = nullptr;
    // L09 (Batch L): the placeholder build/destroy pair + its cache moved.

    // T292 (2026-08-26): protectedRegions_ removed. It was the
    // ConstrainedMeshCache's eviction-protection bitvector; T67 deleted the
    // cache and with it the only writer, leaving the vector permanently
    // all-false, and the progressive door/PVS-object build paths that read
    // it were therefore permanently no-ops. See
    // src/client/graphics/CLAUDE.md's "Constrained Memory Management"
    // section for the removed cache's reference pointer.

    // L09 (Batch L): pendingTextureRegions_ is on ZoneLifecycleComponent.


    // Doors waiting for async textures to complete GPU upload.
    // Maps texture name → set of door IDs that were built without that texture.
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

    // L10 (Batch L): pendingZoneComputations_ and loadFailed_ moved.
    bool sequentialLoadComplete_ = false;  // True after loadZoneSequential() finishes


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
    float fogThickness_ = 50.0f;       // Thickness of fog fade zone at edge
    bool forcePvsUpdate_ = false;  // Force PVS visibility recalculation (set when render distance changes)
    // L13: these five types were nested here; they now live at namespace scope
    // (renderer_types.h; render_pass_timer.h for the timer class) so the
    // context and the component headers can name them. See those headers.
    using ZoneLightData = ::EQT::Graphics::RendererZoneLightData;
    using FireGlowLight = ::EQT::Graphics::FireGlowLight;
    using DoorCollisionData = ::EQT::Graphics::DoorCollisionData;
    using FrameTimings = ::EQT::Graphics::FrameTimings;
    using RenderPassTimer = ::EQT::Graphics::RenderPassTimer;
    friend class ::EQT::Graphics::RenderPassTimer;  // was nested; kept its access
    std::vector<ZoneLightData> zoneLightData_;
    std::vector<irr::core::vector3df> zoneLightPositions_;  // Cached positions for distance culling
    std::vector<ObjectLight> objectLights_;  // Light-emitting objects (torches, lanterns)
    std::vector<bool> objectLightInSceneGraph_;  // Track which object lights are in scene graph
    std::vector<VertexAnimatedMesh> vertexAnimatedMeshes_;  // Meshes with vertex animation (flags, banners)
    float ambientMultiplier_ = 1.0f;  // User-adjustable ambient light multiplier (Page Up/Down)

    // Time of day
    uint8_t currentHour_ = 12;
    uint8_t currentMinute_ = 0;

    // Frame phase shared state -- L12 moved measureSection()/sectionStart_ to
    // FrameComponent and deleted the write-only frameStart_ (see that header).
    // L11: pendingUIClick_, runTier2_/runTier3_, the two tier intervals,
    // tier3DeltaAccum_ and windTime_ are on FrameComponent. frameNumber_ stays
    // (renderer_sim_bridge.cpp and renderer_zone_load_env.cpp read it).
    uint32_t frameNumber_ = 0;

    // Frame budget governor (Green/Yellow/Red state machine for load throttling)
    std::unique_ptr<FrameBudgetGovernor> governor_;

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
    // L09 (Batch L): entitiesLoaded_/expectedEntityCount_ moved out.
    bool wireframeMode_ = false;
    bool fogEnabled_ = true;
    bool lightingEnabled_ = true;   // Lighting enabled by default
    // L05: baseVision_ moved to PlayerCameraComponent (single-family; its only
    // other user was the inline getBaseVision(), which moved with it).
    // currentVision_ stays -- renderer_sim_bridge.cpp and
    // renderer_zone_load_env.cpp read it.
    VisionType currentVision_ = VisionType::Normal; // Current vision (may be upgraded by items/buffs)
    bool fireEffectsEnabled_ = true;      // Fire light flickering and ember/smoke particles
    bool fireGlowLightingEnabled_ = true;  // Fire glow lighting (per-vertex warm glow on zone geometry)
    bool fireGlowIcospheresEnabled_ = false; // Fire glow icospheres (animated translucent volumes)
    int maxFireGlowLights_ = 4;            // Max active fire glow lights (1-8, from config)

    std::vector<FireGlowLight> fireGlowLights_;

    // Icosphere rendering (animated fire glow volumes)
#ifdef EQT_HAS_DRM
    uint32_t icosphereProgram_ = 0;
#endif


    // Zone geometry bounds cache (survives geometry vector release)
    float zoneBoundsMinX_ = 0, zoneBoundsMaxX_ = 0;
    float zoneBoundsMinY_ = 0, zoneBoundsMaxY_ = 0;
    bool zoneBoundsValid_ = false;

    // Player position (for Follow and FirstPerson modes)
    float playerX_ = 0, playerY_ = 0, playerZ_ = 0, playerHeading_ = 0;
    // L05: playerPitch_ moved to PlayerCameraComponent (single-family).



    // Performance: HUD dirty tracking to avoid rebuilding every frame
    // K15 (B034): cachedHudText_ / cachedHotkeysText_ / cachedHeadingDebugText_
    // deleted -- zero references; the dirty tracking below is what is live.
    // L03: HudCachedState and hudCachedState_ moved to OverlaysComponent.



    eqt::bridge::GameStateBridge* bridge_ = nullptr;  // For pushing intents (D14+)

    // Collision detection for player mode (shared ownership — P35)
    std::shared_ptr<const HCMap> collisionMap_;

    // L01 (Batch L, refactor/L_renderer_component_extraction.md): the
    // shared-state bundle every extracted renderer component binds by
    // reference. Declared after every member it references; built in the
    // ctor initialiser list (renderer_init.cpp). See renderer_context.h.
    RendererContext ctx_;


    // Irrlicht-based collision (using zone geometry directly)
    // Legacy meta selectors — only used by HCMap fallback/debug path (no BSP)
    irr::scene::ISceneCollisionManager* collisionManager_ = nullptr;

    // BSP-filtered collision: pre-transformed world-space triangles — no matrix math per query
    // All collision paths (movement, detail, footprints, LOS) use direct Möller–Trumbore intersection
    std::map<size_t, std::vector<irr::core::triangle3df>> regionWorldTriangles_;  // Zone geometry per BSP region
    std::map<size_t, std::vector<irr::core::triangle3df>> objectWorldTriangles_;  // Placeable objects per BSP region

    // Pre-baked door collision: open + closed world-space triangles per door
    // During animation (opening/closing), door has no collision (walk-through, matching original EQ behavior)
    // L13: the DoorCollisionData type is now at namespace scope (renderer_types.h);
    // the alias above keeps IrrlichtRenderer::DoorCollisionData spelling valid.
    std::map<uint8_t, DoorCollisionData> doorCollisionData_;

    // L05 (Batch L): the whole collision/raycast/ground-height layer, the
    // collision debug lines and their nested CollisionDebugLine type, and
    // useIrrlichtCollision_, moved to PlayerCameraComponent. doorCollisionData_
    // and its DoorCollisionData type stay -- renderer_zone_lifecycle.cpp and
    // renderer_zone_load_objects.cpp touch them, so the component names the
    // type as IrrlichtRenderer::DoorCollisionData (K05 j).

    // L03 (Batch L): the target highlight, the zone-line debug/bounding-box
    // visualization, the map overlay and the navmesh overlay -- their draw
    // methods, their nested triangle/box types and their single-family state
    // -- moved to OverlaysComponent. Only the members the frame family also
    // touches stayed here (Batch K Mechanism 3); the overlay TUs reach them
    // as owner_.X_.

    // U07c3: WindowManager deleted — inventoryManager remains for bridge data access

    // Spell visual effects
    std::unique_ptr<TextBatch> textBatch_;  // U01
    std::unique_ptr<UIAtlas> uiAtlas_;     // U02
    // L11: uiRenderer_, uiLayout_ and newUIEnabled_ are on FrameComponent.

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

    // T319: see handleActionResult()/requestChatPrefill()/takeChatPrefill()
    // above. Written on the game thread, read on the render thread; every
    // access goes through those accessors.
    mutable std::mutex chatPrefillMutex_;
    std::optional<std::string> pendingChatPrefill_;
public:
    // L11: the sixteen static-UI panel members that used to sit here are on
    // FrameComponent, still public and still written directly by
    // IrrlichtBridge -- reached now as Frame().cachedPlayerStats_ and so on.

    // T60: called by IrrlichtBridge on InventorySlotChanged (equip slots
    // 0-21) — an equip-slot display change usually means the entity's
    // appearance changed too, so the doll should re-render next time it's
    // visible. Safe to call even if the doll doesn't exist yet or the
    // popup is closed (no-op in both cases). L03: a one-line forwarder to
    // OverlaysComponent, which owns the body (the K11 rule).
    void markPaperdollDirty() { overlays_->markPaperdollDirty(); }
private:

    // L05 (Batch L): repositionFirstPersonCamera(),
    // applyCameraProjectionForMode(), applyFirstPersonPitchClamp(),
    // getFirstPersonVerticalFov() and buildFirstPersonCameraDumpLines() moved
    // to PlayerCameraComponent, keeping their comments and their TU basename
    // (src/client/graphics/player/renderer_camera.cpp -- the basename is a
    // string literal in tests/test_first_person_camera_single_formula.cpp).

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


    RenderPassTimer* renderPassTimer_ = nullptr;  // Owned by Irrlicht (ref counted)

    // 3D camera transforms captured during SOLID pass (before any 2D drawing overwrites them)
    irr::core::matrix4 captured3DView_;
    irr::core::matrix4 captured3DProj_;

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

    // L05 (Batch L): the player family -- player-mode movement, the collision
    // and raycast layer, ground-height queries, the collision debug lines and
    // the camera. Constructed in the IrrlichtRenderer ctor body; see
    // renderer_init.cpp. It reaches 20 members that are shared with other
    // families but sit below the context's 5-family core threshold, plus the
    // private nested type DoorCollisionData, so it needs friendship on both
    // axes of K13 correction (ii).
    friend class PlayerCameraComponent;
    std::unique_ptr<PlayerCameraComponent> playerCamera_;

    // L06 (Batch L): the slash family -- registry 3, the renderer's slash-
    // command if-chain. Ctor body, renderer_init.cpp; friendship on axis 1.
    // L12: no friend line -- SlashCommandsComponent is private-clean on both
    // census axes (see slash/slash_commands_component.h).
    std::unique_ptr<SlashCommandsComponent> slash_;
    // L07 (Batch L): the SimulationWorker bridge. It reaches 28 members that
    // are shared with other families but sit below the context's 5-family
    // core threshold, plus three private methods, so it needs friendship on
    // axis 1 of K13 correction (ii). simulationWorker_ itself stays here --
    // L07 changed no thread ownership.
    friend class SimBridgeComponent;
    std::unique_ptr<SimBridgeComponent> simBridge_;

    // L08 (Batch L): the zone streaming family. It reaches 23 members that are
    // shared with other families but sit below the context's 5-family core
    // threshold, plus two private methods, so it needs friendship on axis 1 of
    // K13 correction (ii). No thread ownership moved -- see
    // include/client/graphics/zone_stream/zone_stream_component.h.
    friend class ZoneStreamComponent;
    std::unique_ptr<ZoneStreamComponent> zoneStream_;

    // L09 (Batch L): three families in one component (batch correction 26).
    // Friendship on axis 1 of K13 (ii); no thread ownership moved -- see
    // include/client/graphics/zone_lifecycle/zone_lifecycle_component.h.
    friend class ZoneLifecycleComponent;
    std::unique_ptr<ZoneLifecycleComponent> zoneLifecycle_;

    // L10 (Batch L): the loading thread's zone build. Friendship on BOTH axes
    // of K13 (ii); no thread ownership moved -- see zone_load_component.h.
    friend class ZoneLoadComponent;
    std::unique_ptr<ZoneLoadComponent> zoneLoad_;

    // L11 (Batch L): FrameComponent needs friendship on both axes of K13
    // correction (ii) -- it reaches ~60 private members through owner_
    // (frameTimings_ above all) and names IrrlichtRenderer::FrameTimings when
    // it resets the accumulator. Thread ownership is unchanged: see the
    // threading note in frame/frame_component.h.
    friend class FrameComponent;
    std::unique_ptr<FrameComponent> frame_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_IRRLICHT_RENDERER_H
