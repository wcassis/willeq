#ifndef EQT_GRAPHICS_ENTITY_RENDERER_H
#define EQT_GRAPHICS_ENTITY_RENDERER_H

#include <irrlicht.h>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/wld_loader.h"

namespace EQT {
namespace Graphics {

// Forward declarations
class RaceModelLoader;
struct ConstrainedRendererConfig;

// Appearance data for entity rendering
struct EntityAppearance {
    uint8_t face = 0;           // Face/head variant (for players)
    uint8_t haircolor = 0;      // Hair color
    uint8_t hairstyle = 0;      // Hair style
    uint8_t beardcolor = 0;     // Beard color
    uint8_t beard = 0;          // Beard style
    uint8_t texture = 0;        // Body texture variant (equip_chest2: 0=naked, 1-3=armor, 10+=robes)
    uint8_t helm = 0;           // Head/helm variant (for NPCs: selects head mesh HE01, HE02, etc.)

    // Equipment textures (9 slots: head, chest, arms, wrist, hands, legs, feet, primary, secondary)
    uint32_t equipment[9] = {0};
    uint32_t equipment_tint[9] = {0};
};

// Entity visual representation
struct EntityVisual {
    irr::scene::ISceneNode* sceneNode = nullptr;              // Base scene node (can be mesh or animated)
    irr::scene::IMeshSceneNode* meshNode = nullptr;           // Static mesh node (for placeholders)
    EQAnimatedMeshSceneNode* animatedNode = nullptr;          // Animated node (for animated models)
    irr::scene::ITextSceneNode* nameNode = nullptr;
    uint16_t spawnId = 0;
    uint16_t raceId = 0;
    uint8_t gender = 0;
    std::string name;
    // Current interpolated position (may differ from server position between updates)
    float lastX = 0, lastY = 0, lastZ = 0;
    float lastHeading = 0;
    // Server's last reported position (for calculating velocity)
    float serverX = 0, serverY = 0, serverZ = 0;
    float serverHeading = 0;
    // Calculated velocity for interpolation between server updates
    float velocityX = 0, velocityY = 0, velocityZ = 0;
    float velocityHeading = 0;
    // Time tracking for interpolation
    float timeSinceUpdate = 0;     // Seconds since last server update
    float lastUpdateInterval = 0.2f;  // Estimated time between server updates
    int32_t serverAnimation = 0;   // Animation ID from server (negative = reverse playback)
    uint32_t lastNonZeroAnimation = 0;  // Last non-zero animation from server (for maintaining state)
    bool isPlayer = false;         // True if this is our own player character
    bool isNPC = false;            // True if this is an NPC (npc_type=1), false for other players
    bool isCorpse = false;         // True if this is a corpse (keeps death animation, no updates)
    bool corpsePositionAdjusted = false; // True if corpse Y position was adjusted for death animation
    float corpseYOffset = 0.0f;    // Manual Y offset applied to corpse (for debugging/tuning)
    float corpseTime = 0.0f;       // Time since entity became a corpse (for death animation timing)
    bool usesPlaceholder = false;  // True if using placeholder cube instead of model
    bool isAnimated = false;       // True if using animated mesh
    EntityAppearance appearance;   // Appearance data for model/texture selection
    std::string currentAnimation;  // Current animation being played
    float modelYOffset = 0;        // Height offset to adjust for model origin (center vs base)
    float collisionZOffset = 0;    // Offset from server Z (model center) to feet for collision detection

    // Boat/object collision - allows entities to act as physical obstructions
    bool hasCollision = false;     // True if this entity has collision (boats, etc.)
    float collisionRadius = 0;     // Horizontal collision radius
    float collisionHeight = 0;     // Vertical collision height (deck height for boats)
    float deckZ = 0;               // Z coordinate of the deck surface (for standing on boats)

    // Pose state (sitting, standing, etc.) - set via SpawnAppearance, not movement updates
    // This prevents movement updates from overriding sitting/crouching poses
    enum class PoseState : uint8_t { Standing = 0, Sitting = 1, Crouching = 2, Lying = 3 };
    PoseState poseState = PoseState::Standing;

    // Equipment model attachments
    irr::scene::IMeshSceneNode* primaryEquipNode = nullptr;   // Right hand weapon
    irr::scene::IMeshSceneNode* secondaryEquipNode = nullptr; // Left hand weapon/shield
    uint32_t currentPrimaryId = 0;    // Current primary equipment item ID
    uint32_t currentSecondaryId = 0;  // Current secondary equipment item ID

    // Weapon skill types for combat animations (255 = unknown/none, 7 = hand-to-hand)
    uint8_t primaryWeaponSkill = 255;    // Weapon skill type of primary weapon
    uint8_t secondaryWeaponSkill = 255;  // Weapon skill type of secondary weapon
    float weaponDelayMs = 3000.0f;       // Primary weapon delay in ms (for attack animation speed)

    // Casting state (for other entities - shows casting bar above head)
    bool isCasting = false;                   // True if entity is currently casting
    uint32_t castSpellId = 0;                 // Spell being cast
    std::string castSpellName;                // Name of spell being cast
    uint32_t castDurationMs = 0;              // Total cast time in milliseconds
    std::chrono::steady_clock::time_point castStartTime;  // When cast started
    irr::scene::IBillboardSceneNode* castBarBillboard = nullptr;  // Casting bar billboard

    // Corpse decay/fade state
    bool isFading = false;                    // True if corpse is fading out
    float fadeAlpha = 1.0f;                   // Current opacity (1.0 = visible, 0.0 = invisible)
    float fadeTimer = 0.0f;                   // Time since fade started
    static constexpr float FADE_DURATION = 3.0f;  // Duration of fade-out in seconds

    // First-person view state (for player entity only)
    bool isFirstPersonMode = false;           // True if player is in first-person view
    float fpAttackTimer = 0.0f;               // Timer for first-person attack animation
    float fpAttackDuration = 0.5f;            // Duration of attack animation in seconds
    bool fpIsAttacking = false;               // True if attack animation is playing

    // Light source (lantern, lightstone, etc.)
    irr::scene::ILightSceneNode* lightNode = nullptr;  // Point light attached to entity
    uint8_t lightLevel = 0;                            // Current light level (0=none, 1-255=intensity)

    // Scene graph management (for constrained mode optimization)
    // When false, the entity is removed from scene graph to skip traversal overhead
    bool inSceneGraph = true;
};

// Manages rendering of game entities (NPCs, players, mobs)
class EntityRenderer {
public:
    EntityRenderer(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                   irr::io::IFileSystem* fileSystem);
    ~EntityRenderer();

    // Set the base path for EQ client files (S3D archives)
    void setClientPath(const std::string& path);

    // Load global character models (global_chr.s3d)
    bool loadGlobalCharacters();

    // Create a visual for an entity
    // isPlayer: true if this is our own player character
    // isNPC: true if this is an NPC (npc_type=1), false for other player characters
    // isCorpse: true if this is a corpse (npc_type=2 or 3), starts with death animation
    // serverSize: size value from server (0 or 1 = default, >1 = larger, <1 = smaller)
    bool createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                     float x, float y, float z, float heading, bool isPlayer = false,
                     uint8_t gender = 0, const EntityAppearance& appearance = EntityAppearance(),
                     bool isNPC = true, bool isCorpse = false, float serverSize = 0.0f);

    // Update entity position/heading with velocity for interpolation
    void updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                      float dx = 0, float dy = 0, float dz = 0, int32_t animation = 0);

    // Interpolate entity positions based on velocity (call each frame)
    void updateInterpolation(float deltaTime);

    // Remove an entity
    void removeEntity(uint16_t spawnId);

    // Update entity appearance (for illusion spells, etc.)
    // Recreates the entity model with new race/gender/appearance
    void updateEntityAppearance(uint16_t spawnId, uint16_t raceId, uint8_t gender,
                                const EntityAppearance& appearance);

    // Remove all entities
    void clearEntities();

    // Check if entity exists
    bool hasEntity(uint16_t spawnId) const;

    // Get entity count
    size_t getEntityCount() const { return entities_.size(); }

    // Set visibility of all entity nodes (for profiling)
    void setAllEntitiesVisible(bool visible);

    // Get count of entities with actual models (not placeholders)
    size_t getModeledEntityCount() const;

    // Update name tag positions (call each frame)
    void updateNameTags(irr::scene::ICameraSceneNode* camera);

    // Show/hide name tags
    void setNameTagsVisible(bool visible);

    // Enable/disable lighting on entity materials
    void setLightingEnabled(bool enabled);

    // Get race model loader (for preloading)
    RaceModelLoader* getRaceModelLoader() { return raceModelLoader_.get(); }

    // Set current zone name (for zone-specific model loading)
    void setCurrentZone(const std::string& zoneName);

    // Load numbered global model files (global2-7_chr.s3d)
    void loadNumberedGlobals();

    // Load equipment models from gequip.s3d archives
    bool loadEquipmentModels();

    // Get equipment model loader
    EquipmentModelLoader* getEquipmentModelLoader() { return equipmentModelLoader_.get(); }

    // Animation control for entities
    // playThrough: if true, animation must complete before next can start (for jumps, attacks, emotes)
    // When loop=false, animation holds on last frame automatically
    bool setEntityAnimation(uint16_t spawnId, const std::string& animCode, bool loop = true, bool playThrough = false);
    void stopEntityAnimation(uint16_t spawnId);

    // Mark an entity as a corpse (plays death animation and prevents further animation updates)
    void markEntityAsCorpse(uint16_t spawnId);

    // Start corpse decay animation (fade out over a few seconds, then remove)
    void startCorpseDecay(uint16_t spawnId);

    // Set entity pose state (sitting, standing, etc.) - prevents movement updates from overriding pose
    void setEntityPoseState(uint16_t spawnId, EntityVisual::PoseState pose);
    EntityVisual::PoseState getEntityPoseState(uint16_t spawnId) const;

    // Set/get entity weapon skill types for combat animation selection
    void setEntityWeaponSkills(uint16_t spawnId, uint8_t primaryWeaponSkill, uint8_t secondaryWeaponSkill);
    uint8_t getEntityPrimaryWeaponSkill(uint16_t spawnId) const;
    uint8_t getEntitySecondaryWeaponSkill(uint16_t spawnId) const;

    // Set weapon delay for attack animation speed matching (Phase 6.2)
    // delayMs: weapon delay in milliseconds (EQ delay * 100, e.g., delay 30 = 3000ms)
    void setEntityWeaponDelay(uint16_t spawnId, float delayMs);

    // Set entity light level (from equipped light sources like lanterns, lightstones)
    // lightLevel: 0=no light, higher values=brighter light (max 255)
    void setEntityLight(uint16_t spawnId, uint8_t lightLevel);

    // Check if entity is playing a playThrough animation
    bool isEntityPlayingThrough(uint16_t spawnId) const;
    std::string getEntityAnimation(uint16_t spawnId) const;
    bool hasEntityAnimation(uint16_t spawnId, const std::string& animCode) const;
    std::vector<std::string> getEntityAnimationList(uint16_t spawnId) const;

    // Global animation speed control
    void setGlobalAnimationSpeed(float speed);
    float getGlobalAnimationSpeed() const { return globalAnimationSpeed_; }
    void adjustGlobalAnimationSpeed(float delta);  // Adjust by delta amount

    // Corpse Z offset control (for tuning corpse vertical position)
    void adjustCorpseZOffset(float delta);
    float getCorpseZOffset() const { return corpseZOffset_; }

    // Get entities map for LOS checking (read-only access)
    const std::map<uint16_t, EntityVisual>& getEntities() const { return entities_; }

    // Check for boat collision at a position
    // Returns the deck Z height if standing on a boat, or BEST_Z_INVALID if not on a boat
    // x, y: EQ coordinates to check
    // currentZ: current player Z position (to check if we're at boat level)
    float findBoatDeckZ(float x, float y, float currentZ) const;

    // Debug: Set target ID for animation debugging output
    void setDebugTargetId(uint16_t spawnId);
    uint16_t getDebugTargetId() const { return debugTargetId_; }

    // Show/hide the player entity (used in first-person mode)
    // In first-person mode (visible=false), weapons are shown but body is hidden
    void setPlayerEntityVisible(bool visible);

    // Debug: log player visibility status (call before drawAll to diagnose render issues)
    void debugLogPlayerVisibility() const;

    // Set first-person mode for the player (shows only weapons)
    // When enabled, weapons are positioned relative to camera instead of skeleton
    void setPlayerFirstPersonMode(bool enabled);

    // Update first-person weapon positions relative to camera
    // Call this each frame when in first-person mode
    // cameraPos/cameraTarget: Irrlicht world coordinates
    void updateFirstPersonWeapons(const irr::core::vector3df& cameraPos,
                                   const irr::core::vector3df& cameraTarget,
                                   float deltaTime);

    // Trigger a first-person attack animation (weapon swing)
    void triggerFirstPersonAttack();

    // Check if player is in first-person mode
    bool isPlayerInFirstPersonMode() const;

    // Update the player entity's position (used in player mode for third-person view)
    void updatePlayerEntityPosition(float x, float y, float z, float heading);

    // Set the player entity's animation (used in player mode)
    // movementSpeed is used to scale walk/run animation speed to match actual movement
    // playThrough animations (like jump, combat) must complete before other animations can play
    void setPlayerEntityAnimation(const std::string& animCode, bool loop = true, float movementSpeed = 0.0f, bool playThrough = false);

    // Set the player's spawn ID (marks that entity as the player)
    void setPlayerSpawnId(uint16_t spawnId);

    // Get the player's head bone position in EQ coordinates for first-person camera
    // Returns true if head bone was found and position was set, false otherwise
    // If false, fallback to player entity position + eye height offset
    bool getPlayerHeadBonePosition(float& eqX, float& eqY, float& eqZ) const;

    // Get the player's model Y offset (height offset from server Z to feet)
    // Server Z represents the CENTER of the model, so feet are at serverZ + modelYOffset
    // Returns 0.0f if player entity not found
    float getPlayerModelYOffset() const;

    // Get the player's collision Z offset (distance from server Z to feet for collision)
    // Server Z is model center, feet are at serverZ - collisionZOffset
    // Returns 0.0f if player entity not found
    float getPlayerCollisionZOffset() const;

    // Get the player's eye height from feet (for first-person camera positioning)
    // Returns the height from ground level to approximate eye position
    // Returns 0.0f if player entity not found
    float getPlayerEyeHeightFromFeet() const;

    // Entity casting management (for showing casting bars above other entities)
    void startEntityCast(uint16_t spawnId, uint32_t spellId, const std::string& spellName, uint32_t castTimeMs);
    void cancelEntityCast(uint16_t spawnId);
    void completeEntityCast(uint16_t spawnId);
    bool isEntityCasting(uint16_t spawnId) const;
    float getEntityCastProgress(uint16_t spawnId) const;

    // Update casting bars (call each frame)
    void updateEntityCastingBars(float deltaTime, irr::scene::ICameraSceneNode* camera);

    // Render 2D casting bars over entities (call during 2D render pass)
    void renderEntityCastingBars(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                                  irr::scene::ICameraSceneNode* camera);

    // Set BSP tree for PVS-based entity visibility culling
    // When set, entities in regions not visible from the camera's region will be hidden
    void setBspTree(std::shared_ptr<BspTree> bspTree);

    // Clear BSP tree (call when changing zones)
    void clearBspTree();

    // Constrained rendering support
    // Set the constrained renderer config for entity visibility limits
    // When set, limits the number of visible entities and their render distance
    void setConstrainedConfig(const ConstrainedRendererConfig* config);

    // Update entity visibility based on constrained mode limits
    // Call this each frame after updating entity positions
    // cameraPos: camera position in Irrlicht coordinates
    void updateConstrainedVisibility(const irr::core::vector3df& cameraPos);

    // Get number of entities currently visible (for debug HUD)
    int getVisibleEntityCount() const { return visibleEntityCount_; }

private:
    irr::scene::IMesh* getMeshForRace(uint16_t raceId, uint8_t gender = 0,
                                       const EntityAppearance& appearance = EntityAppearance());
    irr::scene::IMesh* createPlaceholderMesh(float size, irr::video::SColor color);
    float getScaleForRace(uint16_t raceId) const;
    irr::video::SColor getColorForRace(uint16_t raceId) const;

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;

    std::string clientPath_;
    std::map<uint16_t, EntityVisual> entities_;

    // Race model loader for actual 3D models
    std::unique_ptr<RaceModelLoader> raceModelLoader_;

    // Equipment model loader for weapons/items
    std::unique_ptr<EquipmentModelLoader> equipmentModelLoader_;

    // Attach equipment models to an entity's bone attachment points
    void attachEquipment(EntityVisual& visual);

    // Update equipment transforms to follow animated bones
    void updateEquipmentTransforms(EntityVisual& visual);

    // Placeholder mesh cache (for races without models)
    std::map<uint16_t, irr::scene::IMesh*> placeholderMeshCache_;

    bool nameTagsVisible_ = true;
    bool lightingEnabled_ = false;
    irr::gui::IGUIFont* nameFont_ = nullptr;

    // Visibility settings
    float renderDistance_ = 300.0f;   // Max distance to render entity models (synced from main renderer)
    float nameTagDistance_ = 200.0f;  // Max distance to show name tags

    // PVS-based visibility culling
    std::shared_ptr<BspTree> bspTree_;                  // BSP tree for region lookups
    size_t currentCameraRegionIdx_ = SIZE_MAX;          // Camera's current region index
    std::shared_ptr<BspRegion> currentCameraRegion_;    // Camera's current region (has visibleRegions)

    // Debug: Target ID for animation debugging
    uint16_t debugTargetId_ = 0;

    // Pending update for batched processing
    struct PendingUpdate {
        uint16_t spawnId;
        float x, y, z, heading;
        float dx, dy, dz;
        int32_t animation;  // Signed: negative = reverse playback
    };

    // Performance optimization: use map instead of vector for pending updates
    // This automatically deduplicates updates - only the latest update per entity is kept
    std::unordered_map<uint16_t, PendingUpdate> pendingUpdates_;

    // Process a single update (internal, called by flushPendingUpdates)
    void processUpdate(const PendingUpdate& update);

    // Flush all pending updates in reverse order
    void flushPendingUpdates();

    // Performance optimization: track entities that need interpolation updates
    // Entities are added when they receive position updates with non-zero velocity,
    // and removed when they become stationary
    std::unordered_set<uint16_t> activeEntities_;

    // Performance optimization: spatial grid for O(1) visibility queries
    // Grid cells are ~500 EQ units, covering typical render distances
    static constexpr float GRID_CELL_SIZE = 500.0f;

    // Convert EQ position to grid cell key
    int64_t positionToGridKey(float x, float y) const {
        int32_t cellX = static_cast<int32_t>(std::floor(x / GRID_CELL_SIZE));
        int32_t cellY = static_cast<int32_t>(std::floor(y / GRID_CELL_SIZE));
        return (static_cast<int64_t>(cellX) << 32) | static_cast<uint32_t>(cellY);
    }

    // Spatial grid: maps cell key -> set of entity spawn IDs in that cell
    std::unordered_map<int64_t, std::unordered_set<uint16_t>> spatialGrid_;

    // Track which cell each entity is currently in
    std::unordered_map<uint16_t, int64_t> entityGridCell_;

    // Update entity's position in the spatial grid
    void updateEntityGridPosition(uint16_t spawnId, float x, float y);

    // Remove entity from spatial grid
    void removeEntityFromGrid(uint16_t spawnId);

    // Get entities within distance of a point (uses spatial grid)
    void getEntitiesInRange(float centerX, float centerY, float range,
                            std::vector<uint16_t>& outEntities) const;

public:
    // Set render distance for entities
    void setRenderDistance(float distance) { renderDistance_ = distance; }
    float getRenderDistance() const { return renderDistance_; }

    // Set name tag visibility distance
    void setNameTagDistance(float distance) { nameTagDistance_ = distance; }
    float getNameTagDistance() const { return nameTagDistance_; }

    // Coordinate offset adjustments
    void adjustOffsetX(float delta);
    void adjustOffsetY(float delta);
    void adjustOffsetZ(float delta);
    void resetOffsets();
    float getOffsetX() const { return offsetX_; }
    float getOffsetY() const { return offsetY_; }
    float getOffsetZ() const { return offsetZ_; }

    // Coordinate rotation adjustments (degrees)
    void adjustRotationX(float delta);
    void adjustRotationY(float delta);
    void adjustRotationZ(float delta);
    float getRotationX() const { return rotationX_; }
    float getRotationY() const { return rotationY_; }
    float getRotationZ() const { return rotationZ_; }

    // Helm texture debugging (for race 71 QCM NPCs)
    void setHelmDebugEnabled(bool enabled);
    bool isHelmDebugEnabled() const { return helmDebugEnabled_; }
    void adjustHelmUOffset(float delta);
    void adjustHelmVOffset(float delta);
    void adjustHelmUScale(float delta);
    void adjustHelmVScale(float delta);
    void adjustHelmRotation(float delta);  // UV rotation in degrees
    void toggleHelmUVSwap();               // Swap U and V coordinates
    void toggleHelmVFlip();                // Toggle V coordinate flip
    void toggleHelmUFlip();                // Toggle U coordinate flip
    void resetHelmUVParams();
    void printHelmDebugState() const;
    void applyHelmUVTransform();           // Apply current UV transform to helm meshes

    // Get current helm UV parameters
    float getHelmUOffset() const { return helmUOffset_; }
    float getHelmVOffset() const { return helmVOffset_; }
    float getHelmUScale() const { return helmUScale_; }
    float getHelmVScale() const { return helmVScale_; }
    float getHelmRotation() const { return helmRotation_; }
    bool getHelmUVSwap() const { return helmUVSwap_; }
    bool getHelmVFlip() const { return helmVFlip_; }
    bool getHelmUFlip() const { return helmUFlip_; }

    // Cycle head variant for QCM entities (for debugging)
    void cycleHeadVariant(int direction);
    int getCurrentHeadVariant() const { return debugHeadVariant_; }

private:
    float offsetX_ = 0.0f;  // X offset for all entities
    float offsetY_ = 0.0f;  // Y offset (height) for all entities
    float offsetZ_ = 0.0f;  // Z offset for all entities
    float rotationX_ = 0.0f;  // Rotation around X axis (degrees)
    float rotationY_ = 0.0f;  // Rotation around Y axis (degrees)
    float rotationZ_ = 0.0f;  // Rotation around Z axis (degrees)
    float globalAnimationSpeed_ = 1.0f;  // Global animation speed multiplier (1.0 = normal speed)
    float corpseZOffset_ = 0.0f;         // Global Z offset for corpse positioning (debug/tuning)

    // Helm texture debugging parameters
    bool helmDebugEnabled_ = false;
    float helmUOffset_ = 0.0f;
    float helmVOffset_ = 0.0f;
    float helmUScale_ = 1.0f;
    float helmVScale_ = 1.0f;
    float helmRotation_ = 0.0f;  // Rotation in degrees
    bool helmUVSwap_ = false;    // Swap U and V
    bool helmVFlip_ = false;     // Flip V coordinate
    bool helmUFlip_ = false;     // Flip U coordinate

    // Store original UV coords for helm meshes (for reverting)
    struct HelmUVData {
        uint16_t spawnId;
        int bufferIndex;
        std::vector<irr::core::vector2df> originalUVs;
    };
    std::vector<HelmUVData> helmOriginalUVs_;

    // Debug head variant cycling
    int debugHeadVariant_ = -1;  // -1 = use default, 0+ = override variant

    // Player spawn ID (for filtering player from entity casting bars)
    uint16_t playerSpawnId_ = 0;

    // Constrained rendering state
    const ConstrainedRendererConfig* constrainedConfig_ = nullptr;
    int visibleEntityCount_ = 0;  // Number of entities currently visible (for debug HUD)
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ENTITY_RENDERER_H
