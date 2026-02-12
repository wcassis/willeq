#ifndef EQT_GRAPHICS_IRRLICHT_RENDERER_H
#define EQT_GRAPHICS_IRRLICHT_RENDERER_H

#include <irrlicht.h>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/camera_controller.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/door_manager.h"
#include "client/graphics/animated_texture_manager.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/animated_tree_manager.h"
#include "client/graphics/weather_system.h"
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/boids_manager.h"
#include "client/graphics/environment/tumbleweed_manager.h"
#include "client/input/hotkey_manager.h"

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#endif

// Forward declaration for collision map
class HCMap;

// Forward declaration for navmesh pathfinder
class PathfinderNavmesh;

// Forward declaration for zone lines
namespace EQT { struct ZoneLineBoundingBox; }

// Forward declarations for inventory UI
namespace eqt {
namespace ui { class WindowManager; }
namespace inventory { class InventoryManager; }
}

// Forward declaration for spell visual effects
namespace EQ { class SpellVisualFX; }

// Forward declaration for constrained texture cache
namespace EQT { namespace Graphics { class ConstrainedTextureCache; } }

namespace EQT {
namespace Graphics {

// Forward declarations
class SkyRenderer;
class WeatherEffectsController;

// Renderer mode: Player (gameplay), Repair (object adjustment), Admin (debug)
enum class RendererMode {
    Player,  // First-person/follow, collision, server sync, simplified HUD
    Repair,  // Object targeting and adjustment for diagnosing rendering issues
    Admin    // Free camera, debug info, no collision, all keys work
};

// Renderer input action types for event queue
enum class RendererAction : uint8_t {
    // Renderer-internal toggles
    ToggleWireframe, ToggleHUD, ToggleNameTags, ToggleZoneLights,
    CycleObjectLights, ToggleLighting, ToggleCameraMode, ToggleOldModels,
    SaveEntities, Screenshot, ToggleRendererMode,

    // Player-mode actions
    ToggleAutorun, ToggleAutoAttack, Hail, Consider,
    ToggleVendor, ToggleTrainer, ToggleCollision, ToggleCollisionDebug,
    ClearTarget, ToggleInventory, ToggleGroup, ToggleSkills,
    TogglePet, ToggleSpellbook, ToggleBuffWindow, ToggleOptions,
    DoorInteract, WorldObjectInteract,

    // Targeting
    TargetSelf, TargetGroupMember1, TargetGroupMember2, TargetGroupMember3,
    TargetGroupMember4, TargetGroupMember5, TargetNearestPC, TargetNearestNPC,
    CycleTargets, CycleTargetsReverse,

    // Debug overlays
    ToggleZoneLineVisualization, ToggleMapOverlay, RotateMapOverlay,
    MirrorXMapOverlay, ToggleNavmeshOverlay, RotateNavmeshOverlay,
    MirrorXNavmeshOverlay,

    // Helm debug
    ToggleHelmDebug, HelmUVSwap, HelmVFlip, HelmUFlip, HelmReset, HelmPrintState,

    // Frustum culling
    ToggleFrustumCulling,

    // Repair mode
    RepairFlipX, RepairFlipY, RepairFlipZ, RepairReset,
};

struct RendererEvent {
    RendererAction action;
    int8_t intData;
    RendererEvent(RendererAction a, int8_t d = -1) : action(a), intData(d) {}
};

// Vision types affecting zone light visibility
// Can be upgraded by race, items, or buffs (never downgraded)
enum class VisionType {
    Normal,      // Base vision (25% intensity) - Human, Barbarian, Erudite, Vah Shir, Froglok
    Infravision, // Heat vision (75% intensity, red-shifted) - Dwarf, Gnome, Half Elf, Ogre, Halfling
    Ultravision  // Full dark vision (100% intensity) - Dark Elf, High Elf, Wood Elf, Troll, Iksar
};

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
};

// Player position update for server synchronization
// Contains raw data; receiver derives isMoving/isRunning/isBackward from velocity and heading
struct PlayerPositionUpdate {
    float x = 0, y = 0, z = 0;      // Position in EQ coordinates
    float heading = 0;              // Heading in EQ format (0-512, where 512 = 360 degrees)
    float dx = 0, dy = 0, dz = 0;   // Velocity/delta per update interval

    // Helper to check if moving (velocity magnitude > threshold)
    bool isMoving(float threshold = 0.1f) const {
        return (dx * dx + dy * dy + dz * dz) > (threshold * threshold);
    }
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
    float nameTagLOSCheckInterval = 0.1f; // Seconds between LOS checks
    float collisionCheckHeight = 3.0f;   // Height above ground for collision raycast
    bool collisionEnabled = true;        // Toggle collision on/off for debugging
    bool collisionDebug = false;         // Print collision debug info
};

// Configuration for the renderer
struct RendererConfig {
    int width = 800;
    int height = 600;
    bool fullscreen = false;
    bool softwareRenderer = true;  // Use Burnings software renderer by default (no GPU)
    bool useDRM = false;           // Use DRM/KMS framebuffer device (no X11)
    std::string windowTitle = "WillEQ";
    std::string eqClientPath;      // Path to EQ client files

    // Rendering options
    bool wireframe = false;
    bool fog = true;
    bool lighting = false;         // Fullbright mode by default
    bool showNameTags = true;
    float ambientIntensity = 0.4f;

    // Constrained rendering mode (startup-only, cannot change at runtime)
    // When enabled, enforces memory limits for texture and framebuffer
    ConstrainedRenderingPreset constrainedPreset = ConstrainedRenderingPreset::None;
    ConstrainedRendererConfig constrainedConfig;
};

// Event receiver for keyboard/mouse input
class RendererEventReceiver : public irr::IEventReceiver {
public:
    RendererEventReceiver();

    virtual bool OnEvent(const irr::SEvent& event) override;

    bool isKeyDown(irr::EKEY_CODE keyCode) const;
    bool wasKeyPressed(irr::EKEY_CODE keyCode);

    // Mouse state
    int getMouseX() const { return mouseX_; }
    int getMouseY() const { return mouseY_; }
    int getMouseDeltaX();
    int getMouseDeltaY();
    bool isLeftButtonDown() const { return leftButtonDown_; }
    bool isRightButtonDown() const { return rightButtonDown_; }
    bool wasLeftButtonClicked();  // Returns true once per click (not hold)
    bool wasLeftButtonReleased(); // Returns true once when button is released
    int getClickMouseX() const { return clickMouseX_; }
    int getClickMouseY() const { return clickMouseY_; }

    // Input state queries
    bool quitRequested() const { return quitRequested_; }
    void setQuitRequested(bool quit) { quitRequested_ = quit; }

    // Action queue for renderer-internal actions (UI toggles, debug, camera, etc.)
    std::vector<RendererEvent> drainActions() { std::vector<RendererEvent> r; r.swap(actionQueue_); return r; }

    // Bridge queue for game actions routed through InputActionBridge
    // (targeting, combat, movement toggles)
    std::vector<RendererEvent> drainBridgeActions() { std::vector<RendererEvent> r; r.swap(bridgeQueue_); return r; }

    // Spell gem and hotbar requests (use intData in RendererEvent for new code)
    int8_t getSpellGemCastRequest() { int8_t g = spellGemCastRequest_; spellGemCastRequest_ = -1; return g; }
    int8_t getHotbarActivationRequest() { int8_t h = hotbarActivationRequest_; hotbarActivationRequest_ = -1; return h; }

    // Delta accumulators (accumulate between frames, consumed once per frame)
    float getCollisionHeightDelta() { float d = collisionHeightDelta_; collisionHeightDelta_ = 0; return d; }
    float getStepHeightDelta() { float d = stepHeightDelta_; stepHeightDelta_ = 0; return d; }
    float getHelmUOffsetDelta() { float d = helmUOffsetDelta_; helmUOffsetDelta_ = 0; return d; }
    float getHelmVOffsetDelta() { float d = helmVOffsetDelta_; helmVOffsetDelta_ = 0; return d; }
    float getHelmUScaleDelta() { float d = helmUScaleDelta_; helmUScaleDelta_ = 0; return d; }
    float getHelmVScaleDelta() { float d = helmVScaleDelta_; helmVScaleDelta_ = 0; return d; }
    float getHelmRotationDelta() { float d = helmRotationDelta_; helmRotationDelta_ = 0; return d; }
    int getHeadVariantCycleDelta() { int d = headVariantCycleDelta_; headVariantCycleDelta_ = 0; return d; }
    float getOffsetXDelta() { float d = offsetXDelta_; offsetXDelta_ = 0; return d; }
    float getOffsetYDelta() { float d = offsetYDelta_; offsetYDelta_ = 0; return d; }
    float getOffsetZDelta() { float d = offsetZDelta_; offsetZDelta_ = 0; return d; }
    float getRotationXDelta() { float d = rotationXDelta_; rotationXDelta_ = 0; return d; }
    float getRotationYDelta() { float d = rotationYDelta_; rotationYDelta_ = 0; return d; }
    float getRotationZDelta() { float d = rotationZDelta_; rotationZDelta_ = 0; return d; }
    float getAnimSpeedDelta() { float d = animSpeedDelta_; animSpeedDelta_ = 0; return d; }
    float getCameraZoomDelta() { float d = cameraZoomDelta_; cameraZoomDelta_ = 0; return d; }
    float getAmbientLightDelta() { float d = ambientLightDelta_; ambientLightDelta_ = 0; return d; }
    float getMusicVolumeDelta() { float d = musicVolumeDelta_; musicVolumeDelta_ = 0; return d; }
    float getEffectsVolumeDelta() { float d = effectsVolumeDelta_; effectsVolumeDelta_ = 0; return d; }
    float getCorpseZOffsetDelta() { float d = corpseZOffsetDelta_; corpseZOffsetDelta_ = 0; return d; }
    float getEyeHeightDelta() { float d = eyeHeightDelta_; eyeHeightDelta_ = 0; return d; }
    float getParticleMultiplierDelta() { float d = particleMultiplierDelta_; particleMultiplierDelta_ = 0; return d; }
    float getDetailDensityDelta() { float d = detailDensityDelta_; detailDensityDelta_ = 0; return d; }
    float getRepairRotateXDelta() { float d = repairRotateXDelta_; repairRotateXDelta_ = 0; return d; }
    float getRepairRotateYDelta() { float d = repairRotateYDelta_; repairRotateYDelta_ = 0; return d; }
    float getRepairRotateZDelta() { float d = repairRotateZDelta_; repairRotateZDelta_ = 0; return d; }

    // Chat input key events
    struct KeyEvent {
        irr::EKEY_CODE key;
        wchar_t character;
        bool shift;
        bool ctrl;
    };
    bool hasPendingKeyEvents() const { return !pendingKeyEvents_.empty(); }
    KeyEvent popKeyEvent() {
        if (pendingKeyEvents_.empty()) return {irr::KEY_KEY_CODES_COUNT, 0, false, false};
        KeyEvent e = pendingKeyEvents_.front();
        pendingKeyEvents_.erase(pendingKeyEvents_.begin());
        return e;
    }
    void clearPendingKeyEvents() { pendingKeyEvents_.clear(); }

    // Chat focus shortcuts
    bool enterKeyPressed() { bool r = enterKeyPressed_; enterKeyPressed_ = false; return r; }
    bool slashKeyPressed() { bool r = slashKeyPressed_; slashKeyPressed_ = false; return r; }
    bool escapeKeyPressed() { bool r = escapeKeyPressed_; escapeKeyPressed_ = false; return r; }

    // Chat focus state (set by renderer, read by GraphicsInputHandler)
    void setChatInputFocused(bool focused) { chatInputFocused_ = focused; }
    bool isChatInputFocused() const { return chatInputFocused_; }

    // Current mode (for hotkey lookups)
    void setCurrentMode(RendererMode mode) {
        switch (mode) {
            case RendererMode::Player: currentMode_ = eqt::input::HotkeyMode::Player; break;
            case RendererMode::Repair: currentMode_ = eqt::input::HotkeyMode::Repair; break;
            case RendererMode::Admin: currentMode_ = eqt::input::HotkeyMode::Admin; break;
        }
    }
    eqt::input::HotkeyMode getCurrentMode() const { return currentMode_; }

private:
    // Continuous key/mouse state
    bool keyIsDown_[irr::KEY_KEY_CODES_COUNT] = {false};
    bool keyWasPressed_[irr::KEY_KEY_CODES_COUNT] = {false};
    int mouseX_ = 0, mouseY_ = 0;
    int lastMouseX_ = 0, lastMouseY_ = 0;
    bool leftButtonDown_ = false;
    bool rightButtonDown_ = false;
    bool leftButtonClicked_ = false;
    bool leftButtonReleased_ = false;
    int clickMouseX_ = 0, clickMouseY_ = 0;
    bool quitRequested_ = false;
    bool chatInputFocused_ = false;  // Set by renderer, read by GraphicsInputHandler

    // Action queue (replaces ~47 boolean flags) — renderer-internal actions
    std::vector<RendererEvent> actionQueue_;
    // Bridge queue — game actions routed to GraphicsInputHandler → InputActionBridge
    std::vector<RendererEvent> bridgeQueue_;

    // Spell gem and hotbar (indexed values, not simple booleans)
    int8_t spellGemCastRequest_ = -1;
    int8_t hotbarActivationRequest_ = -1;

    // Delta accumulators (accumulate between frames)
    float collisionHeightDelta_ = 0.0f;
    float stepHeightDelta_ = 0.0f;
    float offsetXDelta_ = 0.0f;
    float offsetYDelta_ = 0.0f;
    float offsetZDelta_ = 0.0f;
    float rotationXDelta_ = 0.0f;
    float rotationYDelta_ = 0.0f;
    float rotationZDelta_ = 0.0f;
    float animSpeedDelta_ = 0.0f;
    float cameraZoomDelta_ = 0.0f;
    float ambientLightDelta_ = 0.0f;
    float musicVolumeDelta_ = 0.0f;
    float effectsVolumeDelta_ = 0.0f;
    float corpseZOffsetDelta_ = 0.0f;
    float eyeHeightDelta_ = 0.0f;
    float particleMultiplierDelta_ = 0.0f;
    float detailDensityDelta_ = 0.0f;
    float helmUOffsetDelta_ = 0.0f;
    float helmVOffsetDelta_ = 0.0f;
    float helmUScaleDelta_ = 0.0f;
    float helmVScaleDelta_ = 0.0f;
    float helmRotationDelta_ = 0.0f;
    int headVariantCycleDelta_ = 0;
    float repairRotateXDelta_ = 0.0f;
    float repairRotateYDelta_ = 0.0f;
    float repairRotateZDelta_ = 0.0f;

    // Chat input state
    std::vector<KeyEvent> pendingKeyEvents_;
    bool enterKeyPressed_ = false;
    bool slashKeyPressed_ = false;
    bool escapeKeyPressed_ = false;

    // Current hotkey mode for key lookups
    eqt::input::HotkeyMode currentMode_ = eqt::input::HotkeyMode::Player;
};

// Main Irrlicht renderer class
class IrrlichtRenderer {
public:
    IrrlichtRenderer();
    ~IrrlichtRenderer();

    // Initialize the renderer
    bool init(const RendererConfig& config);

    // Initialize only the loading screen (window + progress bar, no model loading)
    // Use this at startup for early progress display, then call loadGlobalAssets() later
    bool initLoadingScreen(const RendererConfig& config);

    // Load global assets (character models, equipment) - call after initLoadingScreen()
    // This is the heavy loading that was previously done in init()
    bool loadGlobalAssets();

    // Show/hide loading screen (progress bar overlay)
    void showLoadingScreen();
    void hideLoadingScreen();
    bool isLoadingScreenVisible() const { return loadingScreenVisible_; }

    // Shutdown the renderer
    void shutdown();

    // Check if renderer is running
    bool isRunning() const;

    // Request the renderer to quit (for /q, /quit commands)
    void requestQuit();

    // Load a zone for rendering
    // progressStart/progressEnd: range for progress bar (0.0-1.0), allows caller to control
    // where zone loading fits within overall loading sequence
    bool loadZone(const std::string& zoneName, float progressStart = 0.0f, float progressEnd = 1.0f);

    // Unload current zone
    void unloadZone();

    // Set zone environment parameters (sky type, fog colors) from server data
    // Call after loadZone() to apply zone-specific rendering settings
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
    void updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                      float dx = 0, float dy = 0, float dz = 0, uint32_t animation = 0);
    void removeEntity(uint16_t spawnId);
    void startCorpseDecay(uint16_t spawnId);  // Start fade-out animation for corpse
    void setEntityLight(uint16_t spawnId, uint8_t lightLevel);  // Set entity light source (lantern, lightstone)
    void clearEntities();

    // Door management
    bool createDoor(uint8_t doorId, const std::string& name,
                    float x, float y, float z, float heading,
                    uint32_t incline, uint16_t size, uint8_t opentype,
                    bool initiallyOpen);
    void setDoorState(uint8_t doorId, bool open, bool userInitiated = false);
    void clearDoors();

    // Collision setup - call after zone, objects, and doors are all loaded
    // Creates a combined collision selector including zone geometry, placeables, and doors
    void setupZoneCollision();

    // Door interaction callback (called when player clicks door or presses U key)
    using DoorInteractCallback = std::function<void(uint8_t doorId)>;
    void setDoorInteractCallback(DoorInteractCallback callback) { doorInteractCallback_ = callback; }

    // World object interaction callback (called when player clicks tradeskill container or presses O key)
    using WorldObjectInteractCallback = std::function<void(uint32_t dropId)>;
    void setWorldObjectInteractCallback(WorldObjectInteractCallback callback) { worldObjectInteractCallback_ = callback; }

    // World object management (for click detection on tradeskill containers)
    void addWorldObject(uint32_t dropId, float x, float y, float z, uint32_t objectType, const std::string& name);
    void removeWorldObject(uint32_t dropId);
    void clearWorldObjects();

    // Spell gem cast callback (called when player presses 1-8 keys)
    using SpellGemCastCallback = std::function<void(uint8_t gemSlot)>;
    void setSpellGemCastCallback(SpellGemCastCallback callback) { spellGemCastCallback_ = callback; }

    // Trigger death animation for an entity (plays death animation and holds at last frame)
    void playEntityDeathAnimation(uint16_t spawnId);

    // Play a specific animation on an entity
    // animCode: EQ animation code (e.g., "c01" for attack, "o02" for wave, etc.)
    // loop: whether to loop the animation (when false, holds on last frame)
    // playThrough: if true, animation must complete before next can start
    bool setEntityAnimation(uint16_t spawnId, const std::string& animCode, bool loop = false, bool playThrough = true);

    // Set entity pose state (sitting, standing, crouching) - prevents movement updates from overriding
    enum class EntityPoseState : uint8_t { Standing = 0, Sitting = 1, Crouching = 2, Lying = 3 };
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
    // Trigger first-person attack animation (weapon swing)
    void triggerFirstPersonAttack();

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
    void processAdminInput(const std::vector<RendererEvent>& actions);
    void processRepairInput(const std::vector<RendererEvent>& actions);
    void processInputDeltas(float deltaTime);
    void processChatInput();

    // Run the main render loop (blocking)
    void run();

    // Take a screenshot
    bool saveScreenshot(const std::string& filename);

    // Toggle rendering options
    void toggleWireframe();
    void toggleHUD();
    void toggleNameTags();
    void toggleFog();
    void toggleLighting();
    void toggleZoneLights();
    void cycleObjectLights();
    void toggleOldModels();
    bool isUsingOldModels() const;
    void setFrameTimingEnabled(bool enabled);  // Enable/disable frame timing profiler
    bool isFrameTimingEnabled() const { return frameTimingEnabled_; }
    void runSceneProfile();  // Run scene breakdown profiler (profiles next frame)
    void resetCoordOffsets();
    void adjustOffsetX(float delta);
    void adjustOffsetY(float delta);
    void adjustOffsetZ(float delta);
    float getOffsetX() const;
    float getOffsetY() const;
    float getOffsetZ() const;
    void adjustRotationX(float delta);
    void adjustRotationY(float delta);
    void adjustRotationZ(float delta);
    float getRotationX() const;
    float getRotationY() const;
    float getRotationZ() const;

    // Unified render distance (controls fog and object culling, NOT camera far plane)
    // The camera far plane must be larger to include the sky dome
    void setRenderDistance(float distance) {
        userRenderDistance_ = distance;
        // Cap effective render distance by server-provided zone max clip plane
        renderDistance_ = (zoneMaxClip_ > 0.0f) ? std::min(distance, zoneMaxClip_) : distance;
        // Sync camera far plane to render distance (must be at least render distance)
        if (camera_) {
            camera_->setFarValue(std::max(renderDistance_, SKY_FAR_PLANE));
        }
        setupFog();
        // Sync render distance to entity renderer
        if (entityRenderer_) {
            entityRenderer_->setRenderDistance(renderDistance_);
        }
        // Sync render distance to tree manager
        if (treeManager_) {
            treeManager_->setRenderDistance(renderDistance_);
        }
        // Force visibility update on next frame by invalidating cached camera position
        lastCullingCameraPos_ = irr::core::vector3df(0, 0, 0);
        lastLightCameraPos_ = irr::core::vector3df(0, 0, 0);
        // Force PVS recalculation (resets static variables in updatePvsVisibility)
        forcePvsUpdate_ = true;
    }
    float getRenderDistance() const { return renderDistance_; }

    // Sky far plane - camera far value must be at least this to render sky
    static constexpr float SKY_FAR_PLANE = 2000.0f;

    // Fog thickness (fade zone at edge of render distance)
    void setFogThickness(float thickness) {
        fogThickness_ = thickness;
        setupFog();
    }
    float getFogThickness() const { return fogThickness_; }

    // Get Irrlicht device (for advanced usage)
    irr::IrrlichtDevice* getDevice() { return device_; }

    // Get entity renderer
    EntityRenderer* getEntityRenderer() { return entityRenderer_.get(); }

    // Get event receiver (for input state queries)
    RendererEventReceiver* getEventReceiver() { return eventReceiver_.get(); }

    // HUD text update callback (for external HUD info)
    using HUDCallback = std::function<std::wstring()>;
    void setHUDCallback(HUDCallback callback) { hudCallback_ = callback; }

    // Save entities callback (called when F10 is pressed)
    using SaveEntitiesCallback = std::function<void()>;
    void setSaveEntitiesCallback(SaveEntitiesCallback callback) { saveEntitiesCallback_ = callback; }

    // Movement callback (for notifying EverQuest class of position changes in Player Mode)
    using MovementCallback = std::function<void(const PlayerPositionUpdate& update)>;
    void setMovementCallback(MovementCallback callback) { movementCallback_ = callback; }

    // Target selection callback (called when player clicks on an entity)
    using TargetCallback = std::function<void(uint16_t spawnId)>;
    void setTargetCallback(TargetCallback callback) { targetCallback_ = callback; }

    // Loot corpse callback (called when player shift+clicks on a corpse)
    using LootCorpseCallback = std::function<void(uint16_t corpseId)>;
    void setLootCorpseCallback(LootCorpseCallback callback) { lootCorpseCallback_ = callback; }


    // Vendor toggle callback (called when V key is pressed in Player Mode)
    using VendorToggleCallback = std::function<void()>;
    void setVendorToggleCallback(VendorToggleCallback callback) { vendorToggleCallback_ = callback; }

    // Banker interact callback (called when Ctrl+click on NPC in Player Mode)
    using BankerInteractCallback = std::function<void(uint16_t npcId)>;
    void setBankerInteractCallback(BankerInteractCallback callback) { bankerInteractCallback_ = callback; }

    // Trainer toggle callback (called when T key is pressed in Player Mode)
    using TrainerToggleCallback = std::function<void()>;
    void setTrainerToggleCallback(TrainerToggleCallback callback) { trainerToggleCallback_ = callback; }

    // Read item callback (called when right-clicking a readable book/note item)
    using ReadItemCallback = std::function<void(const std::string& bookText, uint8_t bookType)>;
    void setReadItemCallback(ReadItemCallback callback);

    // Chat submit callback (called when user submits chat input)
    using ChatSubmitCallback = std::function<void(const std::string& text)>;
    void setChatSubmitCallback(ChatSubmitCallback callback);

    // Zoning enabled callback (called when zone line visualization is toggled)
    using ZoningEnabledCallback = std::function<void(bool enabled)>;
    void setZoningEnabledCallback(ZoningEnabledCallback callback) { zoningEnabledCallback_ = callback; }

    // Current target management
    void setCurrentTarget(uint16_t spawnId, const std::string& name, uint8_t hpPercent = 100, uint8_t level = 0);
    void setCurrentTargetInfo(const TargetInfo& info);
    void updateCurrentTargetHP(uint8_t hpPercent);
    void clearCurrentTarget();
    uint16_t getCurrentTargetId() const { return currentTargetId_; }
    const TargetInfo& getCurrentTargetInfo() const { return currentTargetInfo_; }

    // Renderer mode (Admin vs Player)
    void setRendererMode(RendererMode mode);
    void toggleRendererMode();
    RendererMode getRendererMode() const { return rendererMode_; }
    std::string getRendererModeString() const;

    // Collision map for player mode movement
    void setCollisionMap(HCMap* map) { collisionMap_ = map; }

    // Navmesh pathfinder for navmesh overlay visualization
    void setNavmesh(PathfinderNavmesh* navmesh) { navmesh_ = navmesh; }

    // Clip distance (camera far plane) - for constrained rendering mode
    void setClipDistance(float distance);
    float getClipDistance() const;

    // Inventory UI
    void setInventoryManager(eqt::inventory::InventoryManager* manager);
    void toggleInventory();
    void openInventory();
    void closeInventory();

    // Note/Book reading UI
    void showNoteWindow(const std::string& text, uint8_t type);

    // Zone ready state - controls whether to show loading screen
    // Note: zoneReady is only true when BOTH network AND graphics are ready
    void setZoneReady(bool ready) { zoneReady_ = ready; }
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
        loadingProgress_ = progress;
        loadingText_ = text;
    }

    // Loading screen title (e.g., "Connecting...", "Loading Zone...")
    void setLoadingTitle(const std::wstring& title) {
        loadingTitle_ = title;
    }

    // Zone line debugging
    void setZoneLineDebug(bool inZoneLine, uint16_t targetZoneId = 0, const std::string& debugText = "");
    bool isInZoneLine() const { return inZoneLine_; }

    // Zone line bounding box visualization
    void setZoneLineBoundingBoxes(const std::vector<EQT::ZoneLineBoundingBox>& boxes);
    void clearZoneLineBoundingBoxes();
    void toggleZoneLineVisualization();
    bool isZoneLineVisualizationEnabled() const { return showZoneLineBoxes_; }
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
    eqt::ui::WindowManager* getWindowManager() { return windowManager_.get(); }

    // Spell visual effects access
    EQ::SpellVisualFX* getSpellVisualFX() { return spellVisualFX_.get(); }

    // Constrained texture cache access (may return nullptr if not in constrained mode)
    ConstrainedTextureCache* getConstrainedTextureCache() { return constrainedTextureCache_.get(); }

    // Check if constrained rendering mode is active
    bool isConstrainedMode() const { return config_.constrainedConfig.enabled; }

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

    // Set weather from server packet (type: 0=none, 1=rain, 2=snow; intensity: 1-10)
    void setWeather(uint8_t type, uint8_t intensity);

#ifdef WITH_RDP
    // RDP server support (alternative to Xvfb+x11vnc)

    /**
     * Initialize the native RDP server.
     *
     * @param port The port to listen on (default: 3389)
     * @return true on success, false on failure
     */
    bool initRDP(uint16_t port = 3389);

    /**
     * Start the RDP server.
     * Call after initRDP() to begin accepting connections.
     *
     * @return true on success, false on failure
     */
    bool startRDPServer();

    /**
     * Stop the RDP server.
     */
    void stopRDPServer();

    /**
     * Check if the RDP server is running.
     */
    bool isRDPRunning() const;

    /**
     * Get the RDP server for audio integration.
     * Returns nullptr if RDP is not enabled or not running.
     */
    RDPServer* getRDPServer() { return rdpServer_.get(); }

    /**
     * Get the number of connected RDP clients.
     */
    size_t getRDPClientCount() const;
#endif // WITH_RDP

private:
    void createSoftwareCursor();
    void setupCamera();
    void setupLighting();
    void updateObjectLights();  // Distance-based culling of object lights
    void updateObjectVisibility();  // Distance-based scene graph management for placeable objects
    void updateZoneLightVisibility();  // Distance-based scene graph management for zone lights
    void updateVertexAnimations(float deltaMs);  // Update vertex animated meshes
    void setupFog();
    void setupHUD();
    void updateHUD();
    void applyEnvironmentalDisplaySettings();  // Apply saved display settings to environmental systems
    void createZoneMesh();
    void createZoneMeshWithPvs();  // Create per-region meshes for PVS culling
    void updatePvsVisibility();    // Update region visibility based on camera position
    void updateFrustumCulling();   // Re-test visible nodes against frustum (for rotation-only changes)
    void createObjectMeshes();
    void createZoneLights();
    void updateZoneLightColors();  // Update zone light colors based on current vision type
    void updateObjectLightColors();  // Update object light colors based on weather

    // Loading screen
    void drawLoadingScreen(float progress, const std::wstring& stageText);

    // Player mode movement and collision
    void updatePlayerMovement(float deltaTime);
    bool checkMovementCollision(float fromX, float fromY, float fromZ,
                                float toX, float toY, float toZ);
    float findGroundZ(float x, float y, float currentZ);
    void updateNameTagsWithLOS(float deltaTime);

    // Mouse targeting
    void handleMouseTargeting(int clickX, int clickY);
    uint16_t getEntityAtScreenPos(int screenX, int screenY);
    bool checkEntityLOS(const irr::core::vector3df& cameraPos, const irr::core::vector3df& entityPos);

    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::gui::IGUIEnvironment* guienv_ = nullptr;
    irr::scene::ICameraSceneNode* camera_ = nullptr;
    irr::video::ITexture* softwareCursorTexture_ = nullptr;  // DRM mode software cursor

    std::unique_ptr<CameraController> cameraController_;
    std::unique_ptr<FrustumCuller> frustumCuller_;
    float lastFrustumFwdX_ = 0.0f, lastFrustumFwdY_ = 1.0f, lastFrustumFwdZ_ = 0.0f;
    bool frustumDebugDraw_ = false;  // Draw region bboxes colored by frustum result
    std::unique_ptr<EntityRenderer> entityRenderer_;
    std::unique_ptr<DoorManager> doorManager_;
    std::unique_ptr<RendererEventReceiver> eventReceiver_;
    std::unique_ptr<AnimatedTextureManager> animatedTextureManager_;
    std::unique_ptr<SkyRenderer> skyRenderer_;
    std::unique_ptr<ConstrainedTextureCache> constrainedTextureCache_;  // Optional, for memory-limited rendering
    std::unique_ptr<Detail::DetailManager> detailManager_;  // Grass, plants, debris
    std::unique_ptr<AnimatedTreeManager> treeManager_;  // Tree wind animation
    std::unique_ptr<WeatherSystem> weatherSystem_;  // Weather state management
    std::unique_ptr<Environment::ParticleManager> particleManager_;  // Environmental particles
    std::unique_ptr<WeatherEffectsController> weatherEffects_;  // Weather visual effects (rain, snow, lightning)
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
    irr::scene::IMeshSceneNode* zoneCollisionNode_ = nullptr;  // Hidden node for zone collision in PVS mode

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
    irr::core::vector3df lastCullingCameraPos_;  // Last camera pos when culling was updated
    irr::core::vector3df lastLightCameraPos_;    // Last camera pos when object lights were updated
    bool forcePvsUpdate_ = false;  // Force PVS visibility recalculation (set when render distance changes)
    std::vector<irr::scene::ILightSceneNode*> zoneLightNodes_;
    std::vector<irr::core::vector3df> zoneLightPositions_;  // Cached positions for distance culling
    std::vector<size_t> zoneLightRegions_;  // Cached BSP region index for each light (SIZE_MAX = no region)
    std::vector<bool> zoneLightInSceneGraph_;  // Track which lights are in scene graph
    std::vector<ObjectLight> objectLights_;  // Light-emitting objects (torches, lanterns)
    std::vector<irr::scene::IMeshSceneNode*> lightDebugMarkers_;  // Debug markers showing active light positions
    bool showLightDebugMarkers_ = false;  // Show debug markers for active lights
    std::vector<std::string> previousActiveLights_;  // Track active lights to detect changes
    std::vector<VertexAnimatedMesh> vertexAnimatedMeshes_;  // Meshes with vertex animation (flags, banners)
    irr::scene::ILightSceneNode* sunLight_ = nullptr;  // Directional sun light
    float ambientMultiplier_ = 1.0f;  // User-adjustable ambient light multiplier (Page Up/Down)

    // Time of day
    uint8_t currentHour_ = 12;
    uint8_t currentMinute_ = 0;

    // Frame phase shared state
    std::chrono::steady_clock::time_point sectionStart_;
    int64_t measureSection();
    bool chatInputFocused_ = false;
    bool runTier2_ = true;
    bool runTier3_ = true;

    // Tiered update frequencies
    uint32_t frameNumber_ = 0;
    static constexpr uint32_t kTier2Interval = 3;   // ~20Hz at 60fps
    static constexpr uint32_t kTier3Interval = 6;   // ~10Hz at 60fps
    float tier3DeltaAccum_ = 0.0f;  // Accumulated delta for Tier 3 simulation

    // Adaptive budget (constrained mode)
    float frameBudgetMs_ = 33.3f;       // Target budget (default 30fps)
    bool frameBudgetExceeded_ = false;   // True if last frame exceeded budget

    RendererConfig config_;
    bool initialized_ = false;
    bool loadingScreenVisible_ = true;  // True when loading screen is showing (default: show at start)
    bool globalAssetsLoaded_ = false;  // True when loadGlobalAssets() has completed
    bool zoneReady_ = false;  // True when zone is fully loaded and ready for player input
    bool networkReady_ = false;  // True when network packet exchange is complete
    bool entitiesLoaded_ = false;  // True when all entities have been loaded with models/textures
    size_t expectedEntityCount_ = 0;  // Expected number of entities from ZoneSpawns
    size_t loadedEntityCount_ = 0;  // Number of entities fully loaded so far
    float loadingProgress_ = 0.0f;  // Loading progress for zone transitions (0.0 - 1.0)
    std::wstring loadingText_ = L"Initializing...";  // Loading stage text
    std::wstring loadingTitle_ = L"EverQuest";  // Loading screen title
    bool wireframeMode_ = false;
    bool hudEnabled_ = true;
    bool fogEnabled_ = true;
    bool lightingEnabled_ = true;   // Lighting enabled by default
    bool zoneLightsEnabled_ = false; // Zone lights off by default
    VisionType baseVision_ = VisionType::Normal;    // Base vision from race
    VisionType currentVision_ = VisionType::Normal; // Current vision (may be upgraded by items/buffs)
    int maxObjectLights_ = 8;  // Max object lights to display (1-8), cycles with L key
    CameraMode cameraMode_ = CameraMode::Follow;  // Default to third-person follow camera

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
    irr::gui::IGUIStaticText* headingDebugText_ = nullptr;  // Heading debug info (right side)
    HUDCallback hudCallback_;
    SaveEntitiesCallback saveEntitiesCallback_;
    float hudAnimTimer_ = 0.0f;  // Timer for HUD animations (auto attack indicator)

    // Performance: HUD dirty tracking to avoid rebuilding every frame
    // Cached HUD text for comparison
    std::wstring cachedHudText_;
    std::wstring cachedHotkeysText_;
    std::wstring cachedHeadingDebugText_;
    // Cached state values to detect changes
    struct HudCachedState {
        RendererMode rendererMode = RendererMode::Player;
        int fps = 0;
        int playerX = 0, playerY = 0, playerZ = 0;
        size_t entityCount = 0;
        size_t modeledEntityCount = 0;
        uint16_t targetId = 0;
        uint8_t targetHpPercent = 0;
        float animSpeed = 1.0f;
        float corpseZ = 0.0f;
        bool wireframeMode = false;
        bool oldModels = true;
        std::string cameraMode;
        std::string zoneName;
    };
    HudCachedState hudCachedState_;

    // FPS tracking
    int currentFps_ = 0;
    int frameCount_ = 0;
    irr::u32 lastFpsTime_ = 0;

    // Polygon count tracking (for constrained mode)
    uint32_t lastPolygonCount_ = 0;
    int polygonBudgetExceededFrames_ = 0;  // Throttle warnings
    int constrainedStatsLogCounter_ = 0;   // Periodic stats logging

    // Renderer mode (Player / Repair / Admin)
    RendererMode rendererMode_ = RendererMode::Player;
    PlayerMovementState playerMovement_;
    PlayerModeConfig playerConfig_;
    MovementCallback movementCallback_;

    // Repair mode state
    irr::scene::ISceneNode* repairTargetNode_ = nullptr;
    std::string repairTargetName_;
    irr::core::vector3df repairOriginalRotation_{0, 0, 0};
    irr::core::vector3df repairRotationOffset_{0, 0, 0};
    irr::core::vector3df repairOriginalScale_{1, 1, 1};
    bool repairFlipX_ = false;
    bool repairFlipY_ = false;
    bool repairFlipZ_ = false;

    // Collision detection for player mode
    HCMap* collisionMap_ = nullptr;

    // Target selection and loot
    TargetCallback targetCallback_;
    LootCorpseCallback lootCorpseCallback_;
    VendorToggleCallback vendorToggleCallback_;
    BankerInteractCallback bankerInteractCallback_;
    TrainerToggleCallback trainerToggleCallback_;
    ChatSubmitCallback chatSubmitCallback_;
    DoorInteractCallback doorInteractCallback_;
    WorldObjectInteractCallback worldObjectInteractCallback_;
    SpellGemCastCallback spellGemCastCallback_;
    ZoningEnabledCallback zoningEnabledCallback_;
    uint16_t currentTargetId_ = 0;
    std::string currentTargetName_;
    uint8_t currentTargetHpPercent_ = 100;
    uint8_t currentTargetLevel_ = 0;
    TargetInfo currentTargetInfo_;  // Extended target info for HUD display

    // Irrlicht-based collision (using zone geometry directly)
    irr::scene::ITriangleSelector* zoneTriangleSelector_ = nullptr;  // Full selector (terrain + objects + doors)
    irr::scene::ITriangleSelector* terrainOnlySelector_ = nullptr;   // Terrain only (for detail system ground queries)
    irr::scene::ISceneCollisionManager* collisionManager_ = nullptr;
    bool useIrrlichtCollision_ = true;  // Use zone mesh for collision instead of HCMap

    // Irrlicht collision methods
    bool checkCollisionIrrlicht(const irr::core::vector3df& start, const irr::core::vector3df& end,
                                 irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle);
    // Find ground Z at position. currentZ is model center (server Z), modelYOffset is offset from center to feet.
    float findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset);

    // Repair mode methods
    irr::scene::ISceneNode* findZoneObjectAtScreenPosition(int screenX, int screenY);
    void selectRepairTarget(irr::scene::ISceneNode* node);
    void clearRepairTarget();
    void drawRepairTargetBoundingBox();  // Draw white wireframe box around repair target
    void applyRepairRotation(float deltaX, float deltaY, float deltaZ);  // Apply rotation offset
    void toggleRepairFlip(int axis);  // Toggle flip on axis (0=X, 1=Y, 2=Z)
    void resetRepairAdjustments();  // Reset all adjustments to original
    void logRepairAdjustment();  // Log current adjustment state

    // LOS checking for name tags (Player Mode)
    float lastLOSCheckTime_ = 0.0f;

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

    // Target selection box visualization
    void drawTargetSelectionBox();

    // Zone line debugging
    bool inZoneLine_ = false;
    uint16_t zoneLineTargetZoneId_ = 0;
    std::string zoneLineDebugText_;
    void drawZoneLineOverlay();

    // Zone line bounding box visualization
    struct ZoneLineBoxNode {
        irr::scene::IMeshSceneNode* node = nullptr;
        uint16_t targetZoneId = 0;
        bool isProximityBased = false;
    };
    std::vector<ZoneLineBoxNode> zoneLineBoxNodes_;
    bool showZoneLineBoxes_ = true;  // Enabled by default to help debug
    void createZoneLineBoxMesh(const EQT::ZoneLineBoundingBox& box);
    void drawZoneLineBoxLabels();

    // Map overlay visualization (Ctrl+M)
    bool showMapOverlay_ = false;
    glm::vec3 lastMapOverlayUpdatePos_ = glm::vec3(0.0f);
    float mapOverlayRadius_ = 100.0f;  // Radius around player to show
    int mapOverlayRotation_ = 0;       // Rotation index: 0=0°, 1=90°, 2=180°, 3=270° around Y axis
    bool mapOverlayMirrorX_ = false;   // Mirror placeables across YZ plane (negate X)
    struct MapOverlayTriangle {
        irr::core::vector3df v1, v2, v3;
        irr::video::SColor color;  // Based on height and normal
        bool isPlaceable;          // True if from placeable object (rotation applies)
    };
    std::vector<MapOverlayTriangle> mapOverlayTriangles_;
    void toggleMapOverlay();
    void updateMapOverlay(const glm::vec3& playerPos);
    void drawMapOverlay();
    irr::video::SColor getMapOverlayColor(float z, float minZ, float maxZ, const glm::vec3& normal) const;

    // Navmesh overlay visualization (Ctrl+N)
    PathfinderNavmesh* navmesh_ = nullptr;
    bool showNavmeshOverlay_ = false;
    glm::vec3 lastNavmeshOverlayUpdatePos_ = glm::vec3(0.0f);
    float navmeshOverlayRadius_ = 100.0f;  // Radius around player to show
    int navmeshOverlayRotation_ = 0;       // Rotation index: 0=0°, 1=90°, 2=180°, 3=270° around Y axis
    bool navmeshOverlayMirrorX_ = false;   // Mirror across YZ plane (negate X)
    struct NavmeshOverlayTriangle {
        irr::core::vector3df v1, v2, v3;
        irr::video::SColor color;  // Based on area type
    };
    std::vector<NavmeshOverlayTriangle> navmeshOverlayTriangles_;
    void toggleNavmeshOverlay();
    void updateNavmeshOverlay(const glm::vec3& playerPos);
    void drawNavmeshOverlay();
    irr::video::SColor getNavmeshAreaColor(uint8_t areaType) const;

    // FPS counter (centered at top of screen)
    void drawFPSCounter();

    // Inventory UI
    std::unique_ptr<eqt::ui::WindowManager> windowManager_;
    eqt::inventory::InventoryManager* inventoryManager_ = nullptr;
    bool windowManagerCapture_ = false;  // True when window manager has mouse capture (dragging/resizing)

    // Spell visual effects
    std::unique_ptr<EQ::SpellVisualFX> spellVisualFX_;

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
        int64_t objectVisibility = 0;
        int64_t pvsVisibility = 0;
        int64_t objectLights = 0;
        int64_t hudUpdate = 0;
        int64_t sceneDrawAll = 0;
        int64_t targetBox = 0;
        int64_t castingBars = 0;
        int64_t guiDrawAll = 0;
        int64_t windowManager = 0;
        int64_t zoneLineOverlay = 0;
        int64_t endScene = 0;
        int64_t totalFrame = 0;
    };
    FrameTimings frameTimings_;
    FrameTimings frameTimingsAccum_;  // Accumulated over multiple frames
    int frameTimingsSampleCount_ = 0;
    bool frameTimingEnabled_ = false;  // Enable with /frametiming command
    void logFrameTimings();  // Log accumulated frame timings

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
    void profileSceneBreakdown();  // Run once to profile scene categories

#ifdef WITH_RDP
    // RDP server for native remote desktop streaming
    std::unique_ptr<RDPServer> rdpServer_;

    // Capture framebuffer and send to RDP clients
    void captureFrameForRDP();

    // Handle RDP keyboard input
    void handleRDPKeyboard(uint16_t flags, uint8_t scancode);

    // Handle RDP mouse input
    void handleRDPMouse(uint16_t flags, uint16_t x, uint16_t y);
#endif // WITH_RDP
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_IRRLICHT_RENDERER_H
