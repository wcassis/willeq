#ifndef EQT_GRAPHICS_IRRLICHT_RENDERER_H
#define EQT_GRAPHICS_IRRLICHT_RENDERER_H

#include <irrlicht.h>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/camera_controller.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/door_manager.h"
#include "client/graphics/animated_texture_manager.h"

// Forward declaration for collision map
class HCMap;

// Forward declaration for zone lines
namespace EQT { struct ZoneLineBoundingBox; }

// Forward declarations for inventory UI
namespace eqt {
namespace ui { class WindowManager; }
namespace inventory { class InventoryManager; }
}

// Forward declaration for spell visual effects
namespace EQ { class SpellVisualFX; }

namespace EQT {
namespace Graphics {

// Renderer mode: Player (gameplay), Repair (object adjustment), Admin (debug)
enum class RendererMode {
    Player,  // First-person/follow, collision, server sync, simplified HUD
    Repair,  // Object targeting and adjustment for diagnosing rendering issues
    Admin    // Free camera, debug info, no collision, all keys work
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
    std::string windowTitle = "WillEQ";
    std::string eqClientPath;      // Path to EQ client files

    // Rendering options
    bool wireframe = false;
    bool fog = true;
    bool lighting = false;         // Fullbright mode by default
    bool showNameTags = true;
    float ambientIntensity = 0.4f;
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
    bool screenshotRequested() { bool r = screenshotRequested_; screenshotRequested_ = false; return r; }
    bool wireframeToggleRequested() { bool r = wireframeToggleRequested_; wireframeToggleRequested_ = false; return r; }
    bool hudToggleRequested() { bool r = hudToggleRequested_; hudToggleRequested_ = false; return r; }
    bool nameTagToggleRequested() { bool r = nameTagToggleRequested_; nameTagToggleRequested_ = false; return r; }
    bool zoneLightsToggleRequested() { bool r = zoneLightsToggleRequested_; zoneLightsToggleRequested_ = false; return r; }
    bool cameraModeToggleRequested() { bool r = cameraModeToggleRequested_; cameraModeToggleRequested_ = false; return r; }
    bool oldModelsToggleRequested() { bool r = oldModelsToggleRequested_; oldModelsToggleRequested_ = false; return r; }
    bool saveEntitiesRequested() { bool r = saveEntitiesRequested_; saveEntitiesRequested_ = false; return r; }
    bool helmDebugToggleRequested() { bool r = helmDebugToggleRequested_; helmDebugToggleRequested_ = false; return r; }
    bool rendererModeToggleRequested() { bool r = rendererModeToggleRequested_; rendererModeToggleRequested_ = false; return r; }
    bool autorunToggleRequested() { bool r = autorunToggleRequested_; autorunToggleRequested_ = false; return r; }
    bool collisionToggleRequested() { bool r = collisionToggleRequested_; collisionToggleRequested_ = false; return r; }
    bool collisionDebugToggleRequested() { bool r = collisionDebugToggleRequested_; collisionDebugToggleRequested_ = false; return r; }
    bool lightingToggleRequested() { bool r = lightingToggleRequested_; lightingToggleRequested_ = false; return r; }
    bool clearTargetRequested() { bool r = clearTargetRequested_; clearTargetRequested_ = false; return r; }
    bool autoAttackToggleRequested() { bool r = autoAttackToggleRequested_; autoAttackToggleRequested_ = false; return r; }
    bool inventoryToggleRequested() { bool r = inventoryToggleRequested_; inventoryToggleRequested_ = false; return r; }
    bool groupToggleRequested() { bool r = groupToggleRequested_; groupToggleRequested_ = false; return r; }
    bool doorInteractRequested() { bool r = doorInteractRequested_; doorInteractRequested_ = false; return r; }
    bool worldObjectInteractRequested() { bool r = worldObjectInteractRequested_; worldObjectInteractRequested_ = false; return r; }
    bool hailRequested() { bool r = hailRequested_; hailRequested_ = false; return r; }
    bool vendorToggleRequested() { bool r = vendorToggleRequested_; vendorToggleRequested_ = false; return r; }
    bool trainerToggleRequested() { bool r = trainerToggleRequested_; trainerToggleRequested_ = false; return r; }
    bool skillsToggleRequested() { bool r = skillsToggleRequested_; skillsToggleRequested_ = false; return r; }
    bool zoneLineVisualizationToggleRequested() { bool r = zoneLineVisualizationToggleRequested_; zoneLineVisualizationToggleRequested_ = false; return r; }
    int8_t getSpellGemCastRequest() { int8_t g = spellGemCastRequest_; spellGemCastRequest_ = -1; return g; }
    int8_t getHotbarActivationRequest() { int8_t h = hotbarActivationRequest_; hotbarActivationRequest_ = -1; return h; }
    float getCollisionHeightDelta() { float d = collisionHeightDelta_; collisionHeightDelta_ = 0; return d; }
    float getStepHeightDelta() { float d = stepHeightDelta_; stepHeightDelta_ = 0; return d; }

    // Helm UV adjustment requests (for debugging)
    float getHelmUOffsetDelta() { float d = helmUOffsetDelta_; helmUOffsetDelta_ = 0; return d; }
    float getHelmVOffsetDelta() { float d = helmVOffsetDelta_; helmVOffsetDelta_ = 0; return d; }
    float getHelmUScaleDelta() { float d = helmUScaleDelta_; helmUScaleDelta_ = 0; return d; }
    float getHelmVScaleDelta() { float d = helmVScaleDelta_; helmVScaleDelta_ = 0; return d; }
    float getHelmRotationDelta() { float d = helmRotationDelta_; helmRotationDelta_ = 0; return d; }
    bool helmUVSwapRequested() { bool r = helmUVSwapRequested_; helmUVSwapRequested_ = false; return r; }
    bool helmVFlipRequested() { bool r = helmVFlipRequested_; helmVFlipRequested_ = false; return r; }
    bool helmUFlipRequested() { bool r = helmUFlipRequested_; helmUFlipRequested_ = false; return r; }
    bool helmResetRequested() { bool r = helmResetRequested_; helmResetRequested_ = false; return r; }
    bool helmPrintStateRequested() { bool r = helmPrintStateRequested_; helmPrintStateRequested_ = false; return r; }
    int getHeadVariantCycleDelta() { int d = headVariantCycleDelta_; headVariantCycleDelta_ = 0; return d; }

    // Offset adjustment requests (returns delta value, 0 if no change)
    float getOffsetXDelta() { float d = offsetXDelta_; offsetXDelta_ = 0; return d; }
    float getOffsetYDelta() { float d = offsetYDelta_; offsetYDelta_ = 0; return d; }
    float getOffsetZDelta() { float d = offsetZDelta_; offsetZDelta_ = 0; return d; }

    // Rotation adjustment requests (returns delta value, 0 if no change)
    float getRotationXDelta() { float d = rotationXDelta_; rotationXDelta_ = 0; return d; }
    float getRotationYDelta() { float d = rotationYDelta_; rotationYDelta_ = 0; return d; }
    float getRotationZDelta() { float d = rotationZDelta_; rotationZDelta_ = 0; return d; }

    // Animation speed adjustment (returns delta value, 0 if no change)
    float getAnimSpeedDelta() { float d = animSpeedDelta_; animSpeedDelta_ = 0; return d; }

    // Camera zoom adjustment for Player mode (+ to zoom in, - to zoom out)
    float getCameraZoomDelta() { float d = cameraZoomDelta_; cameraZoomDelta_ = 0; return d; }

    // Ambient light adjustment (returns delta value, 0 if no change)
    float getAmbientLightDelta() { float d = ambientLightDelta_; ambientLightDelta_ = 0; return d; }

    // Corpse Z offset adjustment (returns delta value, 0 if no change)
    float getCorpseZOffsetDelta() { float d = corpseZOffsetDelta_; corpseZOffsetDelta_ = 0; return d; }

    // Eye height adjustment for first-person camera (returns delta value, 0 if no change)
    float getEyeHeightDelta() { float d = eyeHeightDelta_; eyeHeightDelta_ = 0; return d; }

    // Spell particle multiplier adjustment (- to decrease, = to increase, Shift for fine control)
    float getParticleMultiplierDelta() { float d = particleMultiplierDelta_; particleMultiplierDelta_ = 0; return d; }

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

    // Repair mode control requests
    float getRepairRotateXDelta() { float d = repairRotateXDelta_; repairRotateXDelta_ = 0; return d; }
    float getRepairRotateYDelta() { float d = repairRotateYDelta_; repairRotateYDelta_ = 0; return d; }
    float getRepairRotateZDelta() { float d = repairRotateZDelta_; repairRotateZDelta_ = 0; return d; }
    bool repairFlipXRequested() { bool r = repairFlipXRequested_; repairFlipXRequested_ = false; return r; }
    bool repairFlipYRequested() { bool r = repairFlipYRequested_; repairFlipYRequested_ = false; return r; }
    bool repairFlipZRequested() { bool r = repairFlipZRequested_; repairFlipZRequested_ = false; return r; }
    bool repairResetRequested() { bool r = repairResetRequested_; repairResetRequested_ = false; return r; }

private:
    bool keyIsDown_[irr::KEY_KEY_CODES_COUNT] = {false};
    bool keyWasPressed_[irr::KEY_KEY_CODES_COUNT] = {false};
    int mouseX_ = 0, mouseY_ = 0;
    int lastMouseX_ = 0, lastMouseY_ = 0;
    bool leftButtonDown_ = false;
    bool rightButtonDown_ = false;
    bool leftButtonClicked_ = false;  // Set on click, consumed by wasLeftButtonClicked()
    bool leftButtonReleased_ = false; // Set on release, consumed by wasLeftButtonReleased()
    int clickMouseX_ = 0, clickMouseY_ = 0;  // Position where click occurred
    bool screenshotRequested_ = false;
    bool wireframeToggleRequested_ = false;
    bool hudToggleRequested_ = false;
    bool nameTagToggleRequested_ = false;
    bool zoneLightsToggleRequested_ = false;
    bool cameraModeToggleRequested_ = false;
    bool oldModelsToggleRequested_ = false;
    bool saveEntitiesRequested_ = false;
    bool helmDebugToggleRequested_ = false;
    bool rendererModeToggleRequested_ = false;
    bool autorunToggleRequested_ = false;
    bool collisionToggleRequested_ = false;
    bool collisionDebugToggleRequested_ = false;
    bool lightingToggleRequested_ = false;
    bool clearTargetRequested_ = false;
    bool autoAttackToggleRequested_ = false;
    bool inventoryToggleRequested_ = false;
    bool groupToggleRequested_ = false;
    bool doorInteractRequested_ = false;
    bool worldObjectInteractRequested_ = false;
    bool hailRequested_ = false;
    bool vendorToggleRequested_ = false;
    bool trainerToggleRequested_ = false;
    bool skillsToggleRequested_ = false;
    bool zoneLineVisualizationToggleRequested_ = false;
    int8_t spellGemCastRequest_ = -1;  // -1 = no request, 0-7 = gem slot
    int8_t hotbarActivationRequest_ = -1;  // -1 = no request, 0-9 = hotbar button
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
    float corpseZOffsetDelta_ = 0.0f;
    float eyeHeightDelta_ = 0.0f;
    float particleMultiplierDelta_ = 0.0f;
    bool quitRequested_ = false;

    // Helm UV debugging
    float helmUOffsetDelta_ = 0.0f;
    float helmVOffsetDelta_ = 0.0f;
    float helmUScaleDelta_ = 0.0f;
    float helmVScaleDelta_ = 0.0f;
    float helmRotationDelta_ = 0.0f;
    bool helmUVSwapRequested_ = false;
    bool helmVFlipRequested_ = false;
    bool helmUFlipRequested_ = false;
    bool helmResetRequested_ = false;
    bool helmPrintStateRequested_ = false;
    int headVariantCycleDelta_ = 0;

    // Chat input state
    std::vector<KeyEvent> pendingKeyEvents_;
    bool enterKeyPressed_ = false;
    bool slashKeyPressed_ = false;
    bool escapeKeyPressed_ = false;

    // Repair mode controls
    float repairRotateXDelta_ = 0.0f;
    float repairRotateYDelta_ = 0.0f;
    float repairRotateZDelta_ = 0.0f;
    bool repairFlipXRequested_ = false;
    bool repairFlipYRequested_ = false;
    bool repairFlipZRequested_ = false;
    bool repairResetRequested_ = false;
};

// Main Irrlicht renderer class
class IrrlichtRenderer {
public:
    IrrlichtRenderer();
    ~IrrlichtRenderer();

    // Initialize the renderer
    bool init(const RendererConfig& config);

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

    // Get current zone name
    const std::string& getCurrentZoneName() const { return currentZoneName_; }

    // Entity management
    // isPlayer: true if this is our own player character
    // isNPC: true if this is an NPC (npc_type=1), false for other player characters
    // isCorpse: true if this is a corpse (npc_type=2 or 3), starts with death animation
    bool createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                     float x, float y, float z, float heading, bool isPlayer = false,
                     uint8_t gender = 0, const EntityAppearance& appearance = EntityAppearance(),
                     bool isNPC = true, bool isCorpse = false);
    void updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                      float dx = 0, float dy = 0, float dz = 0, uint32_t animation = 0);
    void removeEntity(uint16_t spawnId);
    void startCorpseDecay(uint16_t spawnId);  // Start fade-out animation for corpse
    void clearEntities();

    // Door management
    bool createDoor(uint8_t doorId, const std::string& name,
                    float x, float y, float z, float heading,
                    uint32_t incline, uint16_t size, uint8_t opentype,
                    bool initiallyOpen);
    void setDoorState(uint8_t doorId, bool open, bool userInitiated = false);
    void clearDoors();

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

    // First-person mode methods
    // Trigger first-person attack animation (weapon swing)
    void triggerFirstPersonAttack();

    // Check if in first-person mode
    bool isFirstPersonMode() const { return cameraMode_ == CameraMode::FirstPerson; }

    // Set the player's spawn ID (marks that entity as the player and handles visibility)
    void setPlayerSpawnId(uint16_t spawnId);

    // Set player position for camera following
    void setPlayerPosition(float x, float y, float z, float heading);

    // Get player's current Z (may differ from set value if snapped to ground)
    float getPlayerZ() const { return playerZ_; }

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

    // Process input and render a single frame
    // Returns false if the renderer should stop
    bool processFrame(float deltaTime);

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
    void toggleOldModels();
    bool isUsingOldModels() const;
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

    // Get Irrlicht device (for advanced usage)
    irr::IrrlichtDevice* getDevice() { return device_; }

    // Get entity renderer
    EntityRenderer* getEntityRenderer() { return entityRenderer_.get(); }

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

    // Clear target callback (called when player clears target via ESC key)
    using ClearTargetCallback = std::function<void()>;
    void setClearTargetCallback(ClearTargetCallback callback) { clearTargetCallback_ = callback; }

    // Loot corpse callback (called when player shift+clicks on a corpse)
    using LootCorpseCallback = std::function<void(uint16_t corpseId)>;
    void setLootCorpseCallback(LootCorpseCallback callback) { lootCorpseCallback_ = callback; }

    // Auto attack toggle callback (called when ` key is pressed in Player Mode)
    using AutoAttackCallback = std::function<void()>;
    void setAutoAttackCallback(AutoAttackCallback callback) { autoAttackCallback_ = callback; }

    // Auto attack status callback (returns true if auto attack is currently enabled)
    using AutoAttackStatusCallback = std::function<bool()>;
    void setAutoAttackStatusCallback(AutoAttackStatusCallback callback) { autoAttackStatusCallback_ = callback; }

    // Hail callback (called when H key is pressed in Player Mode)
    using HailCallback = std::function<void()>;
    void setHailCallback(HailCallback callback) { hailCallback_ = callback; }

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

    // Inventory UI
    void setInventoryManager(eqt::inventory::InventoryManager* manager);
    void toggleInventory();
    void openInventory();
    void closeInventory();

    // Note/Book reading UI
    void showNoteWindow(const std::string& text, uint8_t type);

    // Zone ready state - controls whether to show loading screen
    void setZoneReady(bool ready) { zoneReady_ = ready; }
    bool isZoneReady() const { return zoneReady_; }

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

private:
    void setupCamera();
    void setupLighting();
    void updateObjectLights();  // Distance-based culling of object lights
    void updateVertexAnimations(float deltaMs);  // Update vertex animated meshes
    void setupFog();
    void setupHUD();
    void updateHUD();
    void createZoneMesh();
    void createObjectMeshes();
    void createZoneLights();

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

    std::unique_ptr<CameraController> cameraController_;
    std::unique_ptr<EntityRenderer> entityRenderer_;
    std::unique_ptr<DoorManager> doorManager_;
    std::unique_ptr<RendererEventReceiver> eventReceiver_;
    std::unique_ptr<AnimatedTextureManager> animatedTextureManager_;

    std::shared_ptr<S3DZone> currentZone_;
    std::string currentZoneName_;
    irr::scene::IMeshSceneNode* zoneMeshNode_ = nullptr;
    std::vector<irr::scene::IMeshSceneNode*> objectNodes_;
    std::vector<irr::scene::ILightSceneNode*> zoneLightNodes_;
    std::vector<ObjectLight> objectLights_;  // Light-emitting objects (torches, lanterns)
    std::vector<VertexAnimatedMesh> vertexAnimatedMeshes_;  // Meshes with vertex animation (flags, banners)
    irr::scene::ILightSceneNode* sunLight_ = nullptr;  // Directional sun light
    float ambientMultiplier_ = 1.0f;  // User-adjustable ambient light multiplier (Page Up/Down)

    // Time of day
    uint8_t currentHour_ = 12;
    uint8_t currentMinute_ = 0;

    RendererConfig config_;
    bool initialized_ = false;
    bool zoneReady_ = false;  // True when zone is fully loaded and ready for player input
    float loadingProgress_ = 0.0f;  // Loading progress for zone transitions (0.0 - 1.0)
    std::wstring loadingText_ = L"Initializing...";  // Loading stage text
    std::wstring loadingTitle_ = L"EverQuest";  // Loading screen title
    bool wireframeMode_ = false;
    bool hudEnabled_ = true;
    bool fogEnabled_ = true;
    bool lightingEnabled_ = false;
    bool zoneLightsEnabled_ = false;
    CameraMode cameraMode_ = CameraMode::Follow;  // Default to third-person follow camera

    // Player position (for Follow and FirstPerson modes)
    float playerX_ = 0, playerY_ = 0, playerZ_ = 0, playerHeading_ = 0;
    float playerPitch_ = 0;  // Vertical look angle in degrees (-89 to 89)
    uint16_t playerSpawnId_ = 0;  // Spawn ID of the player entity

    // HUD elements
    irr::gui::IGUIStaticText* hudText_ = nullptr;
    irr::gui::IGUIStaticText* hotkeysText_ = nullptr;  // Hotkey hints in upper right
    irr::gui::IGUIStaticText* headingDebugText_ = nullptr;  // Heading debug info (right side)
    HUDCallback hudCallback_;
    SaveEntitiesCallback saveEntitiesCallback_;
    float hudAnimTimer_ = 0.0f;  // Timer for HUD animations (auto attack indicator)

    // FPS tracking
    int currentFps_ = 0;
    int frameCount_ = 0;
    irr::u32 lastFpsTime_ = 0;

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
    ClearTargetCallback clearTargetCallback_;
    LootCorpseCallback lootCorpseCallback_;
    AutoAttackCallback autoAttackCallback_;
    AutoAttackStatusCallback autoAttackStatusCallback_;
    HailCallback hailCallback_;
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
    irr::scene::ITriangleSelector* zoneTriangleSelector_ = nullptr;
    irr::scene::ISceneCollisionManager* collisionManager_ = nullptr;
    bool useIrrlichtCollision_ = true;  // Use zone mesh for collision instead of HCMap

    // Irrlicht collision methods
    bool checkCollisionIrrlicht(const irr::core::vector3df& start, const irr::core::vector3df& end,
                                 irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle);
    // Find ground Z at position. currentZ is model center (server Z), modelYOffset is offset from center to feet.
    float findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset);
    void setupZoneCollision();  // Create triangle selector for zone mesh

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
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_IRRLICHT_RENDERER_H
