#ifndef EQT_GRAPHICS_ENTITY_RENDERER_H
#define EQT_GRAPHICS_ENTITY_RENDERER_H

#include <irrlicht.h>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "client/graphics/actor_scale.h"
#include "client/spell/spell_action_anim.h"  // B1174: ActionCastAnimInputs
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/entity_build_scheduler.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/impostor/impostor_policy.h"

namespace EQT {
namespace Graphics {

// THE Z CONVENTION (T310 Leg B, established from the EQEmu server source at
// /mnt/projects/akk-stack/code, not from this file's former comments).
//
// The server applies NO transform to the z in a position packet: a client's z
// is relayed exactly as the official client sent it (Client::Handle_OP_ClientUpdate,
// zone/client_packet.cpp:4831 -- m_Position is assigned the packet's z_pos verbatim,
// and Mob::FixZ, zone/waypoints.cpp:831-834, returns immediately for a Client
// unless fix_client_z is passed, which NO caller in the tree ever does). Players
// and NPCs therefore carry the SAME quantity on the wire, and it is encoded
// identically for every client patch (Mob::FillSpawnStruct, zone/mob.cpp:1292;
// Mob::MakeSpawnUpdate, zone/mob.cpp:1721 -- both just FloatToEQ19(z)).
//
// What that quantity is: NOT the feet. The server's own model of it is
// "the collision ground plus a per-mob, size-proportional offset" --
// Mob::GetFixedZ (zone/waypoints.cpp:796-810) computes ground z by raycast and
// then adds Mob::GetZOffset() (zone/waypoints.cpp:881-1006, = 0.2 * size * 3.125,
// i.e. 3.75 for a default size-6 humanoid), and Mob::Summon (zone/mob.cpp:4866-4868)
// converts one entity's z into ANOTHER's by subtracting the difference of their
// two GetZOffset() values -- applied to Clients as well as NPCs.
//
// For RENDERING, the operative fact is narrower and is a property of the shipped
// meshes: fable places the scene node's ORIGIN at the server z (modelYOffset is 0
// everywhere), and the EQ player meshes are authored with their origin inside the
// body. A Human male base mesh spans Y = [-3.05, +3.65] about its origin. So the
// origin-to-feet distance is |bbox.MinEdge.Y| * scale -- 3.05 -- and it is NOT
// half the bbox height (3.35). Using half-height was the class-A defect T310 Leg B
// fixed: it stood every entity ~0.30 above its own collision ground.
//
// "Model CENTER" was never true and the phrase has been removed from this
// subsystem. The node origin is simply the mesh's own origin.
//
// T311 ADDENDUM -- WHAT THE OFFICIAL CLIENT ACTUALLY PUTS THERE, MEASURED.
//
// Everything above is about the server and about the meshes. It said nothing
// about the third party: what the official Titanium CLIENT transmits. T311
// Leg A measured it on the wire against a live server (server-side hexdump of
// the C->S OP_ClientUpdate; the Titanium DECODE for that opcode is IN()-only,
// so the dump is the transmitted value):
//
//     wireZ = groundZ + 0.625f * size          // == 0.2 * size * 3.125
//
// Three sizes (3/6/12), two races, two ground heights; size is exactly linear,
// and there is NO race term -- under a Dwarf illusion the server's own Fixed Z
// moved to the EQEmu Dwarf constant while the client went on sending the 3.125
// value. So the client behaves as if EVERY model used 3.125, and the agreement
// with Mob::GetZOffset() above is a coincidence of EQEmu's DEFAULT. Do not
// reimplement GetZOffset() and do not add a per-race table.
//
// THE CONSEQUENCE FOR THIS FILE: 0.625 * size is NOT a landmark of the shipped
// mesh. For a Human male it is 3.75, while the origin-to-feet distance measured
// below is 3.004, half the bbox height is 3.339 and the crown is 3.674. The
// WIRE z and the RENDER node origin are therefore two different quantities and
// must stay two different variables:
//
//     nodeOriginZ = groundZ + collisionZOffset      // this file; unchanged
//     wireZ       = groundZ + 0.625f * size         // the packet edges only
//
// collisionZOffsetFromBounds() and getPlayerCollisionZOffset() are the RENDER
// frame and are correct as they are -- T310 Leg B measured them off the meshes.
// The conversion between the two frames lives in exactly one place,
// include/client/position_frames.h, and is applied only where a packet is built
// or parsed (eq_movement.cpp's SendPositionUpdate, eq_entities.cpp's two
// self-spawn paths). Do not fold either quantity into the other.

// Distance from a mesh's node ORIGIN down to its lowest point (its feet), in
// world units, for a node scaled uniformly by `scale`. Positive when the origin
// sits inside the body above the feet (every EQ player/NPC mesh); zero for a
// mesh authored with its origin on its base (the placeholder cube). Placing the
// node at groundZ + this value puts the mesh's lowest point exactly on groundZ.
inline float collisionZOffsetFromBounds(const irr::core::aabbox3df& bbox, float scale) {
    return -bbox.MinEdge.Y * scale;
}

// B1172 (#491): the scale an entity's node is given -- the official client's
// actor scale (actor_scale.h) for every spawn, and 1.0 for the LOCAL player.
// The 1.0 is a DEVIATION from the client, owned by B1176 (#495): the client
// has no local-player branch -- both EQPlayer build paths run FUN_0047fa9e
// and then FUN_0043c84b (review/B1141/ghidra/out/eqgame.exe.c:106152 ->
// :106154 and :106406 -> :106416), and 0x43c84b hands actor+0xC4 to the
// graphics actor (0x43d3e8 `call [edx+0x50]`) for every player, skipping it
// only for a null player (0x43c87b) or a null graphics actor (0x43d3d0). So a
// human player is 1.2 in the client and 1.0 here (a dwarf 0.8 vs 1.0; a dark
// elf, default 5, is 1.0 on both sides). fable draws its own player with a
// translation-only world matrix (EQAnimatedMeshSceneNode::render,
// isPlayerNode_), so its node scale -- and the collisionZOffset measured from
// it -- stays 1.0 to match what is drawn until B1176 moves the drawing, the
// T431 first-person eye and the third-person camera together.
inline float entityNodeScale(bool isLocalPlayer, uint16_t raceId, uint8_t gender, float serverSize) {
    return isLocalPlayer ? 1.0f : clientActorScale(raceId, gender, serverSize);
}

// Forward declarations
class RaceModelLoader;
class EntityPrepWorker;
class ConstrainedTextureCache;
struct ConstrainedRendererConfig;
struct SimulationInput;
class ImpostorCache;            // B964: include/client/graphics/impostor/impostor_cache.h
class NameTagBatchNode;         // B975: include/client/graphics/name_tag_batch.h
class FrameBudgetGovernor;

// B964: one spawn's impostor state (entity_impostors.cpp). `key` is the cell
// it draws: its own (unique) or its race/gender/texture key (shared).
struct ImpostorSpawnState {
    uint64_t key = 0;
    bool hasKey = false;
    bool shared = false;            // drawing (or asking for) a shared cell
    bool drawnAsImpostor = false;   // withheld from the render list this frame
    bool wantsImpostor = false;     // the tier policy's verdict this frame
    bool needsCapture = true;       // no current capture for the state it is in
    bool capHidden = false;         // not drawn: past the full-model cap (setVisible(false))
    enum class Reason : uint8_t { None, Create, Flip, Appearance, Angle } reason = Reason::Create;
    uint32_t lastSeenFrame = 0;
};

// B964: what the impostor pass did, for logs, tests and the rigs.
struct ImpostorFrameStats {
    int candidates = 0;
    int tierFull = 0;
    int tierImpostor = 0;      // drawn as an impostor this frame
    int wantImpostor = 0;      // the policy's verdict, impostor ready or not
    int fullLimit = 0;
    int unitsThisFrame = 0;
    float unitMsThisFrame = 0.0f;
    int drawCalls = 0;         // billboard draws in the previous render
    int capHidden = 0;         // not drawn this frame: past the full-model cap
};

// Entity pending update for worker drain (matches SimulationInput::EntityPendingUpdate layout)
struct DrainedEntityUpdate {
    uint16_t spawnId;
    float x, y, z, heading, dx, dy, dz;
    int32_t animation;
};

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

// Multi-frame entity build pipeline phases
enum class EntityBuildPhase : uint8_t {
    Placeholder,              // Colored cube visible, waiting for background prep
    TextureUploading,         // Uploading pre-decoded base race textures to GPU, one per frame
    VariantTextureUploading,  // Uploading pre-decoded variant/body-part textures, one per frame
    SceneNodeCreation,        // Frame 1/3: Create node + set position/rotation/scale
    NodeSetup,                // Frame 2/3: Set materials, animation speed, player highlight
    MeshFinalize,             // Frame 3/3: Name tag, corpse anim, light, race ref, decide equip phases
    EquipTextureUploading,    // Uploading pre-decoded equipment textures, one per frame
    EquipmentAttach,          // Build equipment Irrlicht meshes + attach to skeleton
    Built                     // Complete
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

    // Deferred mesh building (progressive loading)
    bool meshBuilt = false;          // false = registered only, true = scene node built
    EntityBuildPhase buildPhase = EntityBuildPhase::Placeholder;

    // Equipment staging (from background prep)
    struct EquipmentStaging {
        int modelId = 0;
        uint32_t equipmentId = 0;
        bool isPrimary = true;
        std::shared_ptr<ZoneGeometry> geometry;
        std::map<std::string, std::shared_ptr<TextureInfo>> rawTextures;
    };
    std::vector<EquipmentStaging> equipmentStaging;

    bool entityPrepComplete = false;  // Background prep finished for this entity

    EntityAppearance appearance;   // Appearance data for model/texture selection
    std::string currentAnimation;  // Current animation being played
    float modelYOffset = 0;        // Height offset to adjust for model origin (center vs base)
    float collisionZOffset = 0;    // Distance from the node origin (== server Z) down to the mesh's feet; see collisionZOffsetFromBounds()

    // T318: the server's size for this entity (Spawn_Struct::size at spawn,
    // OP_SpawnAppearance AT_SIZE / OP_Illusion afterwards). A REAL field: it
    // used to be smuggled through collisionHeight between registerEntity() and
    // buildEntityMesh(), which destroyed it as soon as the mesh was built and
    // made a later size change impossible to apply. See setEntitySize().
    float serverSize = 0;

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
    // B1065: the point bones the two held items hang from, resolved once per
    // (skeleton, secondary item) by EntityRenderer::updateEquipmentTransforms()
    // -- the skeleton's OWN model code, not the race row's (race 71 QCM drawn
    // with the HUM fallback skeleton has no "QCM..." bones).
    const void* attachBoneSkeleton = nullptr;
    uint32_t attachBoneSecondaryId = 0;
    int primaryAttachBone = -1;
    int secondaryAttachBone = -1;

    // Weapon skill types for combat animations (255 = unknown/none, 7 = hand-to-hand)
    uint8_t primaryWeaponSkill = 255;    // Weapon skill type of primary weapon
    uint8_t secondaryWeaponSkill = 255;  // Weapon skill type of secondary weapon
    // B1022 (#342): the `weaponDelayMs` field is deleted. The swing plays at
    // the model's native animation length (speed byte 10 = 1.0x, constant for
    // every swing); the swing RATE is the server's OP_Animation cadence
    // (100 x item delay ms), which never reaches the renderer through a
    // per-entity delay. See the tombstone on setEntityWeaponDelay below.

    // Casting state (for other entities - shows casting bar above head)
    // BSP region cache - avoids tree traversal every frame
    // Invalidated when server sends a position update (processUpdate sets bspRegionDirty)
    size_t cachedBspRegion = SIZE_MAX;         // Cached BSP region index
    bool bspRegionDirty = true;                // True = needs re-lookup on next visibility check

    // Occlusion culling cache - avoids retesting every frame
    bool occlusionHidden = false;              // Cached: entity is behind wall geometry
    uint32_t occlusionFrame = 0;               // Frame counter when occlusion was last tested
    float occlusionTestX = 0, occlusionTestY = 0, occlusionTestZ = 0; // Position when last tested

    bool isCasting = false;                   // True if entity is currently casting
    uint32_t castSpellId = 0;                 // Spell being cast
    std::string castSpellName;                // Name of spell being cast
    uint32_t castDurationMs = 0;              // Total cast time in milliseconds
    std::chrono::steady_clock::time_point castStartTime;  // When cast started
    // B1157 (#476): the model track the spell's CastingAnim id names on this
    // model (spell_cast_animation.h playCastAnim; "" = the model has no such
    // track and its current animation stands). The cast pose is held for the
    // channel and ended at OP_Action / interrupt (completeEntityCast /
    // cancelEntityCast), and the per-frame state (entity_updates.cpp,
    // setPlayerEntityAnimation) must not pull it back to walk/idle.
    std::string castAnimCode;
    // B1174 (#493): the CastingAnim track an OP_Action started on a source
    // that had no cast in progress (a /disc; actionEntityCastAnim, eqgame
    // 0x47f4af). It plays once and the per-frame walk returns the model to
    // the standing idle when it stops (the observer's caster is back in its
    // idle pose 4.36 s after the packet); "" = none.
    std::string actionAnimCode;
    irr::scene::IBillboardSceneNode* castBarBillboard = nullptr;  // Casting bar billboard

    // B1148 (#468): the corpse-fade list (corpse_fade.h). isFading = this
    // spawn's OP_DeleteSpawn came and its actor is on the list: the spawn is
    // gone (no name, no target, no click), the model fades from 1.3 s and is
    // removed past 3499 ms.
    bool isFading = false;
    float fadeAlpha = 1.0f;                   // the alpha corpse_fade.h gives (1 = opaque)
    float fadeTimer = 0.0f;                   // seconds since OP_DeleteSpawn

    // First-person view state (for player entity only)
    bool isFirstPersonMode = false;           // True if player is in first-person view
    float fpAttackTimer = 0.0f;               // Timer for first-person attack animation
    float fpAttackDuration = 0.5f;            // Duration of attack animation in seconds
    bool fpIsAttacking = false;               // True if attack animation is playing

    // Combat animation buffering (for double/triple attack and dual wield detection)
    struct PendingCombatAnim {
        uint8_t weaponSkill;     // Weapon skill type (from damage packet)
        int32_t damage;          // Damage amount (0 = miss)
        float damagePercent;     // Damage as % of target's max HP
        uint16_t targetId;       // Target of this attack
        std::chrono::steady_clock::time_point timestamp;  // When packet received
    };
    std::vector<PendingCombatAnim> pendingCombatAnims;
    std::chrono::steady_clock::time_point combatAnimBufferStart;  // Start of current buffer window
    bool combatBufferActive = false;  // True if we're collecting combat anims in buffer
    bool receivedDamageInBuffer = false;  // True if entity received damage during buffer window
    std::string queuedSkillAnim;  // Skill animation queued during buffer (bash, kick, etc.) - plays first

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

    // Debug: skip texture uploads during entity build (isolate glTexImage2D).
    // Fed from ConstrainedRendererConfig::skipEntityTextureUpload (config/debug.json)
    // by IrrlichtRenderer::initDeferredGraphics; consumed by
    // uploadPendingEntityTextures() below (T412, board B291 -- before that the
    // value was stored and never read, so the knob did nothing).
    void setSkipTextureUpload(bool skip) { skipTextureUpload_ = skip; }
    bool skipTextureUpload() const { return skipTextureUpload_; }

    // Promote every texture the entity build path decoded into the constrained
    // cache to the GPU, and report how many were uploaded. Returns 0 without
    // touching the cache when setSkipTextureUpload(true) is in effect -- that is
    // the whole meaning of the knob. Defined in entity_build_step.cpp.
    int uploadPendingEntityTextures();

    // Set composite entity texture path (enables 1-draw-call entity rendering)
    void setCompositeEntityPath(const std::string& path);

    // Set constrained texture cache for LRU tracking of entity/equipment textures
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    // Get mesh builder from race model loader (for async texture registration)
    ZoneMeshBuilder* getMeshBuilder() const;

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

    // Main-thread entity state updates (corpse fading, animation timing, equipment)
    // Called after worker results are applied. Handles things that must stay on main thread.
    void updateMainThreadEntityState(float deltaTime);

    // Drain pending updates into a vector for the simulation worker.
    // Returns the contents (pendingUpdates_ cleared after flushPendingUpdatesForAnimations).
    std::vector<DrainedEntityUpdate> drainPendingUpdates();

    // Flush pending updates processing only animation/spatial-grid side effects.
    // Position/velocity math is handled by the worker.
    void flushPendingUpdatesForAnimations();

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

    // Show/hide name tags
    void setNameTagsVisible(bool visible);
    bool areNameTagsVisible() const { return nameTagsVisible_; }

    // B431: the Options window's ODP_PCNamesCheckbox / ODP_NPCNamesCheckbox
    // (eqclient.ini PCNames= / NPCNames=). They narrow the master switch
    // above (Ctrl+F3) by KIND, so one never moves the other: an entity's tag
    // shows only when the master is on AND its kind's flag is on. NPC =
    // `isNPC`; everything else (other players, the player, corpses) is PC.
    void setNameTagKindsVisible(bool pcNames, bool npcNames);
    bool nameTagKindVisible(const EntityVisual& visual) const {
        if (visual.isFading) return false;  // B1148: the spawn is deleted, only its actor fades
        return visual.isNPC ? npcNameTagsVisible_ : pcNameTagsVisible_;
    }

    // B975: the ONE name-tag factory (entity_name_tags.cpp). A NameTagNode
    // under `parent` at `offset`, drawn by the shared NameTagBatchNode; with
    // WILLEQ_NAMETAG_BATCH=0 the pre-B975 CTextSceneNode (the A/B path).
    irr::scene::ITextSceneNode* createNameTagNode(irr::scene::ISceneNode* parent, const std::wstring& text,
                                                  irr::video::SColor color, const irr::core::vector3df& offset);
    void releaseNameTagBatch();  // destructor only: every tag holds its own reference

    // Enable/disable lighting on entity materials
    void setLightingEnabled(bool enabled);

    // Set GLSL shader material type IDs for entity meshes (-1 = not available).
    // P21: added alphaTestRGBAType (H02d Option A), mirroring
    // DoorManager::setShaderMaterialTypes / ZoneMeshBuilder — same bug class
    // P20b fixed for tree foliage: masked RGBA/DDS textures must read their
    // own alpha (uTexture.a) rather than land on the ETC1-companion variant
    // with nothing bound on unit 1. Defaults to -1 for callers that don't set it.
    void setShaderMaterialTypes(irr::s32 solidType, irr::s32 alphaTestType, irr::s32 alphaTestRGBAType = -1) {
        shaderMaterialSolid_ = solidType;
        shaderMaterialAlphaTest_ = alphaTestType;
        shaderMaterialAlphaTestRGBA_ = alphaTestRGBAType;
    }

    // Swap shader materials on all existing entity mesh nodes (old → new mapping).
    // P21: added oldAlphaRGBA/newAlphaRGBA for the RGBA alpha-test variant.
    void swapShaderMaterials(irr::s32 oldSolid, irr::s32 newSolid,
                             irr::s32 oldAlpha, irr::s32 newAlpha,
                             irr::s32 oldAlphaRGBA = -1, irr::s32 newAlphaRGBA = -1);

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

    // CorpseDecayStarted (the bridge, just before EntityDespawned): a corpse
    // goes on the fade list (beginCorpseFade); any other entity is removed.
    void startCorpseDecay(uint16_t spawnId);

    // B1148 (#468): OP_DeleteSpawn of `spawnId`. A corpse whose race is not
    // 216/472 goes on the client's corpse-fade list (corpse_fade.h): true,
    // and the caller must NOT remove it (the per-frame walk in
    // updateMainThreadEntityState() fades and removes it). False for every
    // other entity, which the caller removes at the packet.
    bool beginCorpseFade(uint16_t spawnId);

    // B1148: the blended twins of the entity materials (ZoneShaderManager's
    // getMaterialTypeFade*; -1 = none, the built-in path).
    void setFadeShaderMaterialTypes(irr::s32 solid, irr::s32 alphaTest, irr::s32 alphaTestRGBA) {
        fadeMaterialSolid_ = solid;
        fadeMaterialAlphaTest_ = alphaTest;
        fadeMaterialAlphaTestRGBA_ = alphaTestRGBA;
    }

    // Set entity pose state (sitting, standing, etc.) - prevents movement updates from overriding pose
    void setEntityPoseState(uint16_t spawnId, EntityVisual::PoseState pose);
    EntityVisual::PoseState getEntityPoseState(uint16_t spawnId) const;

    // Set/get entity weapon skill types for combat animation selection
    void setEntityWeaponSkills(uint16_t spawnId, uint8_t primaryWeaponSkill, uint8_t secondaryWeaponSkill);
    uint8_t getEntityPrimaryWeaponSkill(uint16_t spawnId) const;
    uint8_t getEntitySecondaryWeaponSkill(uint16_t spawnId) const;

    // B1022 (#342): setEntityWeaponDelay() is deleted. It was the only writer
    // of the (now-removed) weaponDelayMs field, and it had zero callers (B494
    // census). The swing plays at the model's native animation length; the
    // swing RATE is the server's OP_Animation cadence (100 x item delay ms),
    // which never reaches the renderer through a per-entity weapon delay. If a
    // future unit re-introduces delay-based swing timing, it must source the
    // delay from the equipped item (the inventory row carries `delay`), never
    // from a render-site lookup.

    // Combat animation buffering (for double/triple attack and dual wield detection)
    // Call this for each damage packet received - buffers packets within 50ms window
    // sourceId: entity dealing damage (plays attack animation)
    // targetId: entity receiving damage (plays damage reaction)
    // weaponSkill: weapon skill type from damage packet (0-79 for melee)
    // damage: damage amount (0 = miss)
    // damagePercent: damage as % of target's max HP
    void queueCombatAnimation(uint16_t sourceId, uint16_t targetId,
                              uint8_t weaponSkill, int32_t damage, float damagePercent);

    // Process expired combat animation buffers (call each frame from update loop)
    // Checks all entities with pending combat anims and processes buffers where window expired
    void processExpiredCombatBuffers();

    // Check if entity has pending combat animations in buffer
    bool hasEntityPendingCombatAnims(uint16_t spawnId) const;

    // Queue received damage animation into combat buffer (from emote packets)
    // This integrates with the combat buffer so attack vs damage priority can be decided
    void queueReceivedDamageAnimation(uint16_t spawnId);

    // Queue combat skill animation into combat buffer (bash, kick, etc.)
    // Skill animations take priority and play before normal weapon attacks
    void queueSkillAnimation(uint16_t spawnId, const std::string& animCode);

    // Set entity light level (from equipped light sources like lanterns, lightstones)
    // lightLevel: 0=no light, higher values=brighter light (max 255)
    void setEntityLight(uint16_t spawnId, uint8_t lightLevel);

    // T318: apply a server size change (shrink/grow/illusion) to an already
    // registered entity. Rescales the built node in place; if the mesh has not
    // been built yet the new size is simply what the build will use. Returns
    // false only when spawnId is not registered.
    bool setEntitySize(uint16_t spawnId, float serverSize);

    // Check if entity is playing a playThrough animation
    bool isEntityPlayingThrough(uint16_t spawnId) const;
    std::string getEntityAnimation(uint16_t spawnId) const;
    bool hasEntityAnimation(uint16_t spawnId, const std::string& animCode) const;
    std::vector<std::string> getEntityAnimationList(uint16_t spawnId) const;

    // Global animation speed control
    void setGlobalAnimationSpeed(float speed);
    float getGlobalAnimationSpeed() const { return globalAnimationSpeed_; }

    // Get entities map for LOS checking (read-only access)
    const std::map<uint16_t, EntityVisual>& getEntities() const { return entities_; }

    // Check for boat collision at a position (early-out if no boats in zone)
    // Returns the deck Z height if standing on a boat, or BEST_Z_INVALID if not on a boat
    // x, y: EQ coordinates to check
    // currentZ: current player Z position (to check if we're at boat level)
    float findBoatDeckZ(float x, float y, float currentZ) const;

    // Returns true if any boats exist in the current zone (cheap bool check)
    bool hasBoatsInZone() const { return !boatSpawnIds_.empty(); }

    // Debug: Set target ID for animation debugging output
    void setDebugTargetId(uint16_t spawnId);
    uint16_t getDebugTargetId() const { return debugTargetId_; }

    // Show/hide the player entity (used in first-person mode)
    // In first-person mode (visible=false), weapons are shown but body is hidden
    void setPlayerEntityVisible(bool visible);

    // Debug: log player visibility status (call before drawAll to diagnose render issues)
    void debugLogPlayerVisibility() const;

    // T300: first person draws the player's ORDINARY model, unfiltered, with
    // its equipment. There is no geometry filter and no viewmodel pass.
    //
    // T309: what HIDES it is the measured DRAW RULE, not the down-pitch clamp
    // -- the model is withheld from the render list except while an ATTACK
    // animation is playing (first_person_body_draw.h). This call records the
    // mode, hides the floating name tag, and applies the rule immediately so
    // entering first person mid-swing is not a frame late; thereafter
    // updateFirstPersonBodyDrawRule() re-evaluates it every frame.
    void setPlayerFirstPersonMode(bool enabled);

    // Check if player is in first-person mode
    bool isPlayerInFirstPersonMode() const;

    // T309: re-evaluate the first-person draw rule for the local player.
    // Called once per frame from updateMainThreadEntityState(), before
    // smgr_->drawAll() registers the scene. A no-op outside first person
    // (and it restores the node to the render list on the way out).
    // Public so a test can drive exactly what the frame loop drives.
    void updateFirstPersonBodyDrawRule();

    // T309 diagnostics/tests: is the local player's model currently withheld
    // from the render list by the draw rule?
    bool isPlayerBodyWithheldFromRenderList() const;

    // Update the player entity's position (used in player mode for third-person view)
    void updatePlayerEntityPosition(float x, float y, float z, float heading);
    // T472 (B587): make the player's node wear the RENDERER's heading (0..512,
    // the one the camera orbits). The zone-in build and the self echo carry
    // the self row's raw server heading; see the definition for why that
    // turned a stationary player round. Called once per frame by
    // FrameComponent::processFrameSimulation().
    void syncLocalPlayerHeading(float heading512);

    // Set the player entity's animation (used in player mode)
    // movementSpeed is used to scale walk/run animation speed to match actual movement
    // playThrough animations (like jump, combat) must complete before other animations can play
    void setPlayerEntityAnimation(const std::string& animCode, bool loop = true, float movementSpeed = 0.0f, bool playThrough = false);

    // Set the player's spawn ID (marks that entity as the player)
    void setPlayerSpawnId(uint16_t spawnId);
    uint16_t getPlayerSpawnId() const { return playerSpawnId_; }  // B1163

    // Get the player's model Y offset (rendering nudge applied on top of server Z;
    // 0 everywhere today, because the node origin IS the server Z)
    // Returns 0.0f if player entity not found
    float getPlayerModelYOffset() const;

    // Get the player's collision Z offset: the distance from the node origin
    // (== server Z) down to the mesh's feet, so feet are at serverZ - collisionZOffset
    // Returns 0.0f if player entity not found
    float getPlayerCollisionZOffset() const;

    // T301: the offset from the player NODE ORIGIN to the EYE, in Irrlicht
    // world axes.
    //
    // The node origin is the server's Z and sits INSIDE the body
    // (the mesh's own origin -- NOT its centre; see the Z-convention note at the
    // top of this header); the Human
    // male base mesh spans Y = [-3.05, +3.65] about it.
    //
    // T300 returned a single number here, bbox.MaxEdge.Y == 3.65, the CROWN
    // OF THE SKULL. It put the camera on top of the head: the player's own
    // face sat below and forward of it (51 deg depression, inside any
    // plausible frustum) and the run cycle lifted the ANIMATED head above the
    // STATIC crown, so the head bobbed through frame at level pitch. Both are
    // eye-placement defects and no clamp value fixes either.
    //
    // The eye is now derived from the HEAD BONE's own vertex cluster, per
    // race, from the mesh -- see include/client/graphics/first_person_eye.h.
    // It carries a horizontal component too, because a hunched race's head
    // genuinely is forward of its origin (Iksar 0.84, Froglok 1.48 EQ units).
    // derivedFromHead is false when the model has no head anchor and the
    // legacy bbox crown was used; that is a FALLBACK, not the answer.
    struct FirstPersonEyeOffset {
        float irrX = 0.0f;
        float irrY = 0.0f;
        float irrZ = 0.0f;
        bool derivedFromHead = false;
    };
    FirstPersonEyeOffset getPlayerFirstPersonEyeOffset() const;

    // Entity casting management (for showing casting bars above other entities)
    // B1157 (#476): castAnimId is the spell's CastingAnim (field 120); the
    // model plays the track it names on this model (spell_cast_animation.h).
    void startEntityCast(uint16_t spawnId, uint32_t spellId, const std::string& spellName, uint32_t castTimeMs,
                         int castAnimId);
    void cancelEntityCast(uint16_t spawnId);
    void completeEntityCast(uint16_t spawnId);
    // B1174 (#493): OP_Action (effect_flag 0) plays the spell's CastingAnim on
    // its source when the client's conditions hold (eqgame 0x47f4af,
    // EQ::ActionPlaysSourceCastAnim: id, record, target type, stand byte,
    // field 123, Bard/instrument_mod);
    // a source still casting keeps its BeginCast track and B1157's end rule.
    // Returns the track started ("" = none).
    std::string actionEntityCastAnim(uint16_t spawnId, int castAnimId, const EQ::ActionCastAnimInputs& in);
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

    // Set camera's BSP region (computed by IrrlichtRenderer::updatePvsVisibility)
    // Eliminates duplicate BSP lookup — entity renderer uses this for PVS entity culling
    // T407: the regionIdx parameter is accepted and discarded -- only the
    // region pointer is read (currentCameraRegion_->pvs()). The
    // index member it used to cache was written here and by
    // setBspTree()/clearBspTree() and read nowhere.
    void setCameraRegion(size_t /*regionIdx*/, std::shared_ptr<BspRegion> region) {
        currentCameraRegion_ = region;
    }

    // Set frustum culler for directional entity visibility culling
    void setFrustumCuller(FrustumCuller* culler) { frustumCuller_ = culler; }

    // Set occlusion-culled regions (non-owning pointer, cleared each frame)
    void setOcclusionCulledRegions(const std::unordered_set<size_t>* regions) { occlusionCulledRegions_ = regions; }

    // Set software occlusion culler for per-entity depth buffer testing
    void setOcclusionCuller(SoftwareOcclusionCuller* culler) { occlusionCuller_ = culler; }

    // Notify that the occlusion depth buffer was rebuilt (forces retest of all entities)
    void invalidateOcclusionCache();

    // Clear BSP tree (call when changing zones)
    void clearBspTree();

    // Con color computation (EQ Titanium rules)
    // Returns consideration color based on level difference
    static irr::video::SColor getConColor(int playerLevel, int entityLevel);

    // Set player level (for con-colored placeholders during deferred loading)
    void setPlayerLevel(int level) { playerLevel_ = level; }
    int getConPlayerLevel() const { return playerLevel_; }  // B994: the target ring's con

    // Deferred/progressive mesh building
    // Register entity metadata without building mesh (for deferred loading)
    // When playerLevel_ is set and entityLevel > 0, creates a con-colored placeholder cube
    bool registerEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                        float x, float y, float z, float heading, bool isPlayer = false,
                        uint8_t gender = 0, const EntityAppearance& appearance = EntityAppearance(),
                        bool isNPC = true, bool isCorpse = false, float serverSize = 0.0f,
                        uint8_t entityLevel = 0);
    // Build the mesh for a previously registered entity (synchronous, full build)
    bool buildEntityMesh(uint16_t spawnId);
    // Advance one entity through the multi-frame build pipeline.
    // Returns true if work was done this frame.
    // pvsRegion: only build entities in this BSP region (SIZE_MAX = no filter).
    // B973: priorityTargetId (the player's target) is built before the
    // nearest-first order, after the player and anything already in progress.
    // With a scheduler, the step first asks it whether a unit of the chosen
    // entity's kind may start (returns false when not) and records the unit's
    // measured cost after it ran; without one (loading screen, tests) every
    // call may run a unit, as before.
    bool processOneEntityBuildStep(size_t pvsRegion = SIZE_MAX, uint16_t priorityTargetId = 0,
                                   EntityBuildScheduler* scheduler = nullptr);
    // B973: the build work still pending (O(entities); the [entity-build] line).
    EntityBuildBacklog countBuildBacklog() const;
    // B973: textures the prep worker decoded wait for their GPU upload; the
    // create unit used to upload ALL of them in one step (up to 68 textures,
    // 485 ms measured on x86). Uploads now run as their own units, one texture
    // each, before any create unit. False when nothing is pending, the
    // scheduler declines, the skipEntityTextureUpload knob is set, or the
    // upload made no progress (the create unit's uploadAll then handles it).
    bool hasPendingTextureUploads() const;
    bool processOnePendingTextureUpload(EntityBuildScheduler* scheduler);
    // B973: the post-load build scheduler (frame slice + accounting).
    EntityBuildScheduler& buildScheduler() { return buildScheduler_; }
    // Check if entity mesh is built
    bool isEntityMeshBuilt(uint16_t spawnId) const;
    // Get spawn IDs of all entities with meshBuilt == false
    void getUnbuiltEntities(std::vector<uint16_t>& out) const;

    // Set background prep worker for deferred entity model loading
    void setEntityPrepWorker(EntityPrepWorker* worker) { entityPrepWorker_ = worker; }

    // Poll completed prep results from background thread and distribute
    // variant/equipment data to the corresponding EntityVisual structs.
    // Call this once per frame before processOneEntityBuildStep().
    // Returns true if any results were distributed (consumes governor budget).
    bool pollAndDistributePrepResults();

    // Constrained rendering support
    // Set the constrained renderer config for entity visibility limits
    // When set, limits the number of visible entities and their render distance
    void setConstrainedConfig(const ConstrainedRendererConfig* config);

    // Get number of entities currently visible (for debug HUD)
    int getVisibleEntityCount() const { return visibleEntityCount_; }

    // Set visible entity count (from SimulationWorker results)
    void setVisibleEntityCount(int count) { visibleEntityCount_ = count; }

    // Get per-instance animated mesh memory (instanceMesh_ + animator state per entity)
    size_t getPerInstanceMemoryBytes() const;

    // ---- B964: impostor billboards (entity_impostors.cpp) ----
    // Per-frame inputs from the frame loop, set before updateMainThreadEntityState():
    // the governor (null = treat as Green), the target and group members
    // (always full models), and whether the loading screen is up (no work).
    // Post-load progressive streaming is NOT loading: a GM spawn re-arms it.
    void setImpostorFrameInputs(const FrameBudgetGovernor* governor, uint16_t targetId,
                                std::vector<std::string> groupMemberNames, bool loading);
    // True when the preset gives impostors a budget and WILLEQ_BASELINE is not set.
    bool impostorsEnabled() const;
    // How many more entities the simulation worker may mark visible because
    // they can be drawn as impostors (0 when impostors are off).
    int impostorVisibilityAllowance() const;
    // Tools/tests: 0 = the policy decides, 1 = every candidate wants its
    // impostor, 2 = every candidate stays a full model.
    void forceImpostorTier(int mode) { impostorForceMode_ = mode; }
    // Tools/tests: turn impostors on with this budget whatever the preset says.
    void enableImpostorsForTesting(size_t budgetBytes, float tierDistance);
    const ImpostorFrameStats& impostorFrameStats() const { return impostorFrameStats_; }
    const ImpostorCache* impostorCache() const { return impostors_.get(); }
    bool isEntityDrawnAsImpostor(uint16_t spawnId) const;
    // B979: create the impostor cache (if the preset budgets it) and its
    // one-time GPU resources (all pages + scratch FBO + a priming FBO draw).
    // Called by the loading thread at the end of a zone load, under the
    // loading screen, while the loading thread owns the GL context -- the
    // first gameplay frame must never carry a page GEM allocation, the
    // scratch FBO or the FBO's first raster (refactor/B979). A no-op with
    // nothing to do (impostors off, already prepared).
    void prepareImpostorCache();

private:
    // B964 (entity_impostors.cpp)
    void updateImpostors();
    void impostorOnEntityRemoved(uint16_t spawnId);
    void impostorRestoreFull(EntityVisual& visual, ImpostorSpawnState& st);
    std::unique_ptr<ImpostorCache> impostors_;
    NameTagBatchNode* nameTagBatch_ = nullptr;  // B975: grabbed; entity_name_tags.cpp
    std::unordered_map<uint16_t, ImpostorSpawnState> impostorSpawns_;
    ImpostorFrameStats impostorFrameStats_;
    const FrameBudgetGovernor* impostorGovernor_ = nullptr;
    uint16_t impostorTargetId_ = 0;
    std::vector<std::string> impostorGroupNames_;
    // B1071 (GitLab #391): the last logged state of the target's draw path,
    // for the transition-only [impostor] target line. nullptr = nothing
    // targeted (or not yet logged).
    const char* lastImpostorTargetState_ = nullptr;
    uint16_t lastImpostorTargetId_ = 0;
    bool impostorLoading_ = false;
    bool impostorRetainOnRemove_ = false;   // updateEntityAppearance's rebuild keeps the cell
    int impostorForceMode_ = 0;
    size_t impostorTestBudget_ = 0;
    float impostorTestTierDistance_ = -1.0f;
    uint32_t impostorFrame_ = 0;
    std::chrono::steady_clock::time_point impostorLastLog_{};
    std::chrono::steady_clock::time_point impostorLastFrameT_{};
    std::vector<float> impostorFrameMs_;   // frame intervals since the last [impostor] line
    impostor::TierPolicy impostorPolicy_;
    std::vector<impostor::TierCandidate> impostorCands_;   // reused per frame
    std::vector<impostor::Tier> impostorTiers_;

    irr::scene::IMesh* getMeshForRace(uint16_t raceId, uint8_t gender = 0,
                                       const EntityAppearance& appearance = EntityAppearance());
    irr::scene::IMesh* createPlaceholderMesh(float size, irr::video::SColor color);
    // P21: H02d Option-A classification for an entity/equipment material that
    // has already been assigned EMT_TRANSPARENT_ALPHA_CHANNEL_REF (fixed-
    // function alpha-test) upstream. Picks the ETC1-companion shader variant
    // (with unit-1 binding) when the material's bound texture came through
    // the ETC1/EQE1 pipeline, else the RGBA main-texture-alpha variant.
    // No-op (leaves mat.MaterialType alone) if neither shader variant is set.
    void applyAlphaTestMaterialType(irr::video::SMaterial& mat) const;
    float getScaleForRace(uint16_t raceId) const;
    irr::video::SColor getColorForRace(uint16_t raceId) const;

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;

    std::string clientPath_;
    std::map<uint16_t, EntityVisual> entities_;

    // Race model loader for actual 3D models
    std::unique_ptr<RaceModelLoader> raceModelLoader_;

    // P08d: composite-entity adoption tally for the CURRENT zone (since the last
    // clearEntities() call). Logged as one INFO line and reset in clearEntities().
    size_t compositeEligibleCount_ = 0;
    size_t compositeTotalCount_ = 0;

    // Equipment model loader for weapons/items
    std::unique_ptr<EquipmentModelLoader> equipmentModelLoader_;

    // Attach equipment models to an entity's bone attachment points
    void attachEquipment(EntityVisual& visual);

    // Update equipment transforms to follow animated bones
    void updateEquipmentTransforms(EntityVisual& visual);

    // Placeholder mesh cache (for races without models)
    std::map<uint16_t, irr::scene::IMesh*> placeholderMeshCache_;

    int playerLevel_ = 0;  // Player's level for con-color computation
    bool nameTagsVisible_ = true;
    bool pcNameTagsVisible_ = true;   // B431
    bool npcNameTagsVisible_ = true;  // B431
    bool lightingEnabled_ = false;
    irr::s32 shaderMaterialSolid_ = -1;
    irr::s32 fadeMaterialSolid_ = -1;          // B1148: corpse-fade twins
    irr::s32 fadeMaterialAlphaTest_ = -1;
    irr::s32 fadeMaterialAlphaTestRGBA_ = -1;
    irr::s32 shaderMaterialAlphaTest_ = -1;
    irr::s32 shaderMaterialAlphaTestRGBA_ = -1;  // P21: H02d Option-A main-texture-alpha variant

    // Visibility settings
    float renderDistance_ = 300.0f;   // Max distance to render entity models (synced from main renderer)
    float nameTagDistance_ = 200.0f;  // Max distance to show name tags

    // PVS-based visibility culling
    std::shared_ptr<BspTree> bspTree_;                  // BSP tree for region lookups
    std::shared_ptr<BspRegion> currentCameraRegion_;    // Camera's current region (has PVS)

    // Frustum culling (non-owning pointer, owned by IrrlichtRenderer)
    FrustumCuller* frustumCuller_ = nullptr;

    // Occlusion-culled regions (non-owning pointer, owned by IrrlichtRenderer)
    const std::unordered_set<size_t>* occlusionCulledRegions_ = nullptr;

    // Per-entity occlusion culler (non-owning pointer, owned by IrrlichtRenderer)
    SoftwareOcclusionCuller* occlusionCuller_ = nullptr;
    uint32_t occlusionFrameCounter_ = 0;  // Incremented when depth buffer is rebuilt
    uint32_t occlusionTestedCount_ = 0;   // Debug: entities tested this invalidation cycle
    uint32_t occlusionHiddenCount_ = 0;   // Debug: entities hidden by testPoint
    uint32_t frustumCulledCount_ = 0;     // Debug: entities culled by frustum test

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

    // Process combat animation buffer for a specific entity (internal helper)
    void processCombatAnimationBufferForEntity(uint16_t sourceId, uint16_t targetId);

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

    // Ground finder callback for NPC terrain snapping during interpolation
    // Signature: groundZ = callback(x, y, currentZ)
    // Returns the ground Z at (x, y) considering currentZ, or currentZ if no ground found
    using GroundFinderCallback = std::function<float(float x, float y, float currentZ)>;
    void setGroundFinderCallback(GroundFinderCallback callback) { groundFinderCallback_ = std::move(callback); }

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

private:
    float offsetX_ = 0.0f;  // X offset for all entities
    float offsetY_ = 0.0f;  // Y offset (height) for all entities
    float offsetZ_ = 0.0f;  // Z offset for all entities
    float rotationX_ = 0.0f;  // Rotation around X axis (degrees)
    float rotationY_ = 0.0f;  // Rotation around Y axis (degrees)
    float rotationZ_ = 0.0f;  // Rotation around Z axis (degrees)
    float globalAnimationSpeed_ = 1.0f;  // Global animation speed multiplier (1.0 = normal speed)

    // Player spawn ID (for filtering player from entity casting bars)
    uint16_t playerSpawnId_ = 0;

    // Debug flags
    bool skipTextureUpload_ = false;

    // B973: post-load build scheduling (WILLEQ_ENTITY_BUILD_LEGACY read once here).
    EntityBuildScheduler buildScheduler_;

    // Constrained rendering state
    const ConstrainedRendererConfig* constrainedConfig_ = nullptr;
    int visibleEntityCount_ = 0;  // Number of entities currently visible (for debug HUD)

    // Ground finder callback for NPC terrain snapping
    GroundFinderCallback groundFinderCallback_;

    // Background entity prep worker (non-owning, owned by IrrlichtRenderer)
    EntityPrepWorker* entityPrepWorker_ = nullptr;

    // Constrained texture cache for LRU tracking (non-owning, owned by IrrlichtRenderer)
    ConstrainedTextureCache* constrainedTextureCache_ = nullptr;

    // Boat tracking — avoid per-frame boat collision checks when no boats exist
    // Populated at entity build time (both sequential and runtime paths)
    std::vector<uint16_t> boatSpawnIds_;  // Spawn IDs of boat entities (race 72/73)

    // Register/unregister a boat entity for collision tracking
    void registerBoat(uint16_t spawnId);
    void unregisterBoat(uint16_t spawnId);
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ENTITY_RENDERER_H
