#ifndef EQT_GRAPHICS_SIMULATION_WORKER_H
#define EQT_GRAPHICS_SIMULATION_WORKER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <string>
#include <chrono>
#include <random>
#include <irrlicht.h>
#include <glm/glm.hpp>
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/boids_types.h"
#include "client/graphics/environment/particle_types.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/weather_system.h"

// Forward declaration (global namespace)
class HCMap;

namespace EQT {
namespace Graphics {

// Forward declarations
struct BspTree;
class PortalSystem;

// Work priority tiers for simulation scheduling
enum class WorkPriority : uint8_t {
    Critical,    // Every frame: visibility, light selection, player light
    Normal,      // Every frame (skippable if behind): portals, fire flicker, trees, vertex anims
    Background   // Every N frames: weather, sky, weather effects, light animations
};

// ============================================================================
// Particle Command Types — main thread → worker event queue
// ============================================================================

enum class ParticleCommand : uint8_t {
    CreateFireEmitters,        // positions + radii → fire emitters from FirePresets
    ClearUnifiedEmitters,      // kill all particles/emitters/spells
    ActivateWeather,           // type + intensity → weather emitter from WeatherPresets
    DeactivateWeather,         // remove weather emitter
    CreateSpellEffect,         // SpellEffectDef + caster/target/duration
    CreateSpellEffectAtPos,    // SpellEffectDef + world position + duration
    RemoveSpellEffect,         // by effectID
    RemoveSpellEffectsEntity,  // by entityID
    ClearAllSpellEffects,
    ToggleFire,
    SetZoneEnter,              // zone name + biome
    ZoneLeave,
};

// Light source for weather particle illumination (Irrlicht Y-up coords)
struct ParticleLight {
    glm::vec3 position;
    float radius;
    glm::vec3 color;
};

struct ParticleCommandData {
    ParticleCommand type;

    // CreateFireEmitters
    std::vector<glm::vec3> firePositions;    // EQ Z-up
    std::vector<float> fireRadii;

    // ActivateWeather
    uint8_t weatherType = 0;
    uint8_t weatherIntensity = 0;

    // CreateSpellEffect / CreateSpellEffectAtPos
    Environment::SpellEffectDef spellDef;
    uint16_t casterID = 0;
    uint16_t targetID = 0;
    float duration = 0.0f;
    bool useDynamicDir = false;
    float projectileTravelDuration = 0.0f;
    uint32_t preAssignedEffectID = 0;       // Pre-assigned by main thread
    glm::vec3 worldPos{0.0f};               // For CreateSpellEffectAtPos

    // RemoveSpellEffect
    uint32_t effectID = 0;

    // RemoveSpellEffectsEntity
    uint16_t entityID = 0;

    // SetZoneEnter
    std::string zoneName;
    int zoneBiome = 0;

    // Initial entity positions (resolved on main thread when command is created)
    std::unordered_map<uint16_t, glm::vec3> initialEntityPositions;
};

// ============================================================================
// Boids Command Types — main thread → worker event queue
// ============================================================================

enum class BoidsCommand : uint8_t {
    ZoneEnter, ZoneLeave, SetQuality, SetDensity, SetEnabled, SetTypeEnabled
};

struct BoidsCommandData {
    BoidsCommand type;
    std::string zoneName;
    int zoneBiome = 0;
    glm::vec3 boundsMin{-1000.0f};
    glm::vec3 boundsMax{1000.0f};
    bool hasBounds = false;
    int quality = 2;
    float density = 1.0f;
    bool enabled = true;
    uint8_t creatureType = 0;
    bool typeEnabled = true;
};

// ============================================================================
// Tumbleweed Command Types — main thread → worker event queue
// ============================================================================

enum class TumbleweedCommand : uint8_t {
    ZoneEnter, ZoneLeave, SetEnabled
};

struct TumbleweedCommandData {
    TumbleweedCommand type;
    std::string zoneName;
    int zoneBiome = 0;
    bool enabled = true;
};

// ============================================================================
// Weather Command Types — main thread → worker event queue
// ============================================================================

enum class WeatherCommand : uint8_t {
    SetZoneConfig,
    SetWeatherFromZone,
    SetWeatherImmediate,
    TransitionToWeather,
    SetSimulationEnabled,
};

struct WeatherCommandData {
    WeatherCommand type;
    ZoneWeatherConfig zoneConfig;        // for SetZoneConfig
    std::string zoneName;                // for SetWeatherFromZone
    uint8_t weatherType = 0;             // WeatherType as uint8_t
    float transitionTime = 5.0f;         // for TransitionToWeather
    bool simulationEnabled = true;       // for SetSimulationEnabled
};

// ============================================================================
// SpellVFX Command Types — main thread → worker event queue (desktop GL path)
// ============================================================================

enum class SpellVFXCommand : uint8_t {
    CreateEffect,
    RemoveCastGlow,
    RemoveBuffAura,
    RemoveAllForEntity,
    ClearAll,
};

struct SpellVFXCommandData {
    SpellVFXCommand type;
    uint32_t effectId = 0;
    uint8_t fxType = 0;           // SpellFXType cast to uint8_t
    uint32_t spellId = 0;
    uint16_t sourceEntity = 0;
    uint16_t targetEntity = 0;
    float lifetime = 0;
    float scale = 1.0f;
    uint8_t colorA = 255, colorR = 255, colorG = 255, colorB = 255;
    float posX = 0, posY = 0, posZ = 0;       // Irrlicht Y-up
    float targetPosX = 0, targetPosY = 0, targetPosZ = 0;  // Irrlicht Y-up
};

// ============================================================================
// Detail Command Types — main thread → worker event queue
// ============================================================================

enum class DetailCommand : uint8_t { AddChunk, RemoveChunk, ClearAll };

struct DetailCommandData {
    DetailCommand type;
    int32_t chunkKeyX = 0, chunkKeyZ = 0;
    std::vector<irr::core::vector3df> basePositions;  // AddChunk only
    std::vector<float> windInfluence;                   // AddChunk only
};

// ============================================================================
// SimulationInput — snapshot of main thread state sent to worker each frame
// ============================================================================

struct SimulationInput {
    // Camera (Irrlicht Y-up)
    irr::core::vector3df cameraPos;
    irr::core::vector3df cameraTarget;

    // Frustum planes copied from FrustumCuller (6 planes × 4 floats each)
    // Each plane: (nx, ny, nz, d) where nx*x + ny*y + nz*z + d >= 0 means inside
    // Coordinates are in EQ space (Z-up)
    float frustumPlanes[6][4];
    bool frustumValid = false;
    float renderDistance = 300.0f;

    // Camera position in EQ Z-up coordinates
    float camEqX = 0, camEqY = 0, camEqZ = 0;

    // Player (EQ Z-up)
    float playerX = 0, playerY = 0, playerZ = 0;
    float playerHeading = 0;

    // Timing
    float deltaTime = 0;
    uint32_t frameNumber = 0;

    // Environment (changes rarely)
    float timeOfDay = 12.0f;
    uint8_t currentHour = 12;
    uint8_t currentMinute = 0;

    // Player light
    uint8_t playerLightLevel = 0;

    // Vision and weather modifiers (for zone light animation colors)
    uint8_t visionType = 0;               // 0=Normal, 1=Ultravision, 2=Infravision
    float weatherAmbientModifier = 1.0f;  // From WeatherEffectsController

    // Tree wind controller state (snapshotted from AnimatedTreeManager)
    struct TreeWindState {
        float time = 0;
        float baseFrequency = 0.4f;
        float baseStrength = 0.3f;
        float gustFrequency = 0.1f;
        float gustStrength = 0.5f;
        float turbulence = 0.2f;
        float influenceStartHeight = 0.3f;
        float influenceExponent = 2.0f;
        float weatherMultiplier = 1.0f;
        float windDirX = 1.0f, windDirY = 0.0f;
        bool enabled = false;
    } treeWind;

    // Sky state snapshot
    float skyCloudScrollOffset = 0.0f;
    bool skyEnabled = false;
    bool skyInitialized = false;

    // Weather effects state snapshot
    float weatherTransitionProgress = 1.0f;
    float weatherTransitionDuration = 5.0f;
    float weatherCurrentDarkening = 0.0f;
    float weatherTargetDarkening = 0.0f;
    float weatherLightningFlashTimer = 0.0f;
    float weatherLightningBoltTimer = 0.0f;
    float weatherLightningTimer = 0.0f;
    bool weatherLightningActive = false;
    bool weatherLightningEnabled = false;
    uint8_t weatherType = 0;
    uint8_t weatherIntensity = 0;
    bool weatherEnabled = false;

    // Occlusion camera state (EQ Z-up basis vectors from FrustumCuller)
    struct OcclusionCameraState {
        float fwdX = 0, fwdY = 1, fwdZ = 0;
        float rightX = 1, rightY = 0, rightZ = 0;  // Only X,Y needed (2D right)
        float upX = 0, upY = 0, upZ = 1;
        float fovRadV = 1.0f;
        float aspect = 1.33f;
        bool enabled = false;
    } occlusionCamera;

    // Entity snapshots for worker reconciliation
    struct EntitySnapshot {
        uint16_t spawnId;
        float lastX, lastY, lastZ;
        float velocityX, velocityY, velocityZ;
        float serverX, serverY, serverZ;
        float serverHeading;
        float timeSinceUpdate, lastUpdateInterval;
        float collisionZOffset, modelYOffset;
        int32_t serverAnimation;
        uint32_t lastNonZeroAnimation;
        size_t cachedBspRegion;
        bool bspRegionDirty, isNPC, isPlayer, isCorpse, isFading;
        bool inSceneGraph, hasVelocity;
    };
    std::vector<EntitySnapshot> entitySnapshots;

    struct EntityPendingUpdate {
        uint16_t spawnId;
        float x, y, z, heading, dx, dy, dz;
        int32_t animation;
    };
    std::vector<EntityPendingUpdate> entityPendingUpdates;

    float entityRenderDistance = 200.0f;
    int maxVisibleEntities = 50;
    bool entityCullingEnabled = false;

    // Name tag visibility
    float nameTagDistance = 200.0f;
    bool nameTagsVisible = true;

    // Vertex animation time advance
    float vertAnimDeltaMs = 0;  // deltaTime * 1000

    // Particle system input
    struct ParticleInput {
        float deltaTime = 0;
        glm::vec3 cameraPos{0.0f};           // Irrlicht Y-up
        glm::vec3 ambientColor{0.1f};        // Zone ambient for weather tinting
        glm::vec3 windDirection{1.0f, 0.0f, 0.0f};  // EQ Z-up
        float windStrength = 0.0f;
        std::vector<ParticleLight> weatherLights;
        std::unordered_map<uint16_t, glm::vec3> entityPositions;   // Irrlicht Y-up
        std::unordered_map<uint16_t, glm::vec3> entityDirections;  // Irrlicht Y-up, normalized
        std::vector<ParticleCommandData> commands;
        bool fireEnabled = true;
        bool unifiedRendererInitialized = false;
        int poolSize = 1024;  // For deferred initialization
    } particleInput;

    // Boids system input
    struct BoidsInput {
        float deltaTime = 0;
        glm::vec3 playerPosition{0.0f};
        float playerHeading = 0;
        float timeOfDay = 12.0f;
        glm::vec3 windDirection{1.0f, 0.0f, 0.0f};
        float windStrength = 0;
        std::vector<BoidsCommandData> commands;
        bool initialized = false;
    } boidsInput;

    // Tumbleweed system input
    struct TumbleweedInput {
        float deltaTime = 0;
        glm::vec3 playerPosition{0.0f};
        glm::vec3 windDirection{1.0f, 0.0f, 0.0f};
        float windStrength = 0;
        std::vector<TumbleweedCommandData> commands;
        bool initialized = false;
    } tumbleweedInput;

    // Weather system input
    struct WeatherInput {
        float deltaTime = 0;
        std::vector<WeatherCommandData> commands;
        bool initialized = false;
    } weatherInput;

    // Spell VFX input (desktop GL worker-driven path)
    struct SpellVFXInput {
        float deltaTime = 0;
        std::vector<SpellVFXCommandData> commands;
        bool initialized = false;
    } spellVfxInput;

    // Detail wind/disturbance input
    struct DetailInput {
        float deltaTime = 0;
        // Wind params (from WindController/ZoneDetailConfig)
        float windStrength = 1.0f;
        float windFrequency = 0.5f;
        float gustFrequency = 0.1f;
        float gustStrength = 0.3f;
        float windDirX = 1.0f, windDirY = 0.0f;
        // Disturbance params (from FoliageDisturbanceConfig)
        bool disturbanceEnabled = false;
        float playerRadius = 2.5f, playerStrength = 1.0f;
        float maxDisplacement = 0.5f, verticalDipFactor = 0.1f;
        float velocityInfluence = 0.5f, heightExponent = 2.0f;
        float recoveryRate = 0.7f;
        // Player state for disturbance (Irrlicht Y-up)
        bool playerMoving = false;
        float playerPosX = 0, playerPosY = 0, playerPosZ = 0;
        float playerVelX = 0, playerVelY = 0, playerVelZ = 0;
        std::vector<DetailCommandData> commands;
        bool initialized = false;
    } detailInput;
};

// ============================================================================
// SimulationOutput — results computed by worker, applied by main thread
// ============================================================================

struct SimulationOutput {
    // Visibility results (pre-allocated at zone load, one byte per region/object/light)
    std::vector<uint8_t> regionVisible;     // 1=visible, 0=hidden
    std::vector<uint8_t> objectVisible;     // 1=visible, 0=hidden
    std::vector<uint8_t> lightVisible;      // 1=visible, 0=hidden
    std::vector<uint8_t> objectLightVisible; // 1=visible, 0=hidden (object lights)
    size_t currentPvsRegion = SIZE_MAX;     // Camera's current BSP region

    // Portal-visible regions (BFS walk from camera room through portal graph)
    std::unordered_set<size_t> portalVisibleRegions;

    // Region → portal BFS depth (0 = camera region, 1+ = adjacent through portals)
    // Regions not in this map are unreachable via portals (depth = 255 fallback)
    std::unordered_map<size_t, uint8_t> regionPvsDepth;

    // Sorted region entry (shared by draw list and mesh load queue)
    struct SortedRegion {
        size_t regionIdx;
        float distanceSq;
    };

    // Regions needing lazy mesh loading (for constrained mesh cache)
    std::vector<SortedRegion> meshLoadQueue;
    // Protected regions (visible + buffer ring, skip during eviction)
    std::vector<size_t> protectedRegions;

    // Light selection (top 8)
    struct SelectedLight {
        irr::core::vector3df position;     // Irrlicht Y-up
        irr::video::SColorf diffuseColor;  // Current color (including flicker)
        irr::video::SColorf originalColor; // Original color (pre-flicker)
        float radius = 0;
        float attConstant = 0, attLinear = 0, attQuadratic = 0;
        size_t sourceIndex = SIZE_MAX;     // Index into zoneLightData_ or objectLights_
        bool isZoneLight = false;
        bool isPlayerLight = false;
        bool valid = false;
    };
    std::array<SelectedLight, 8> selectedLights;
    int activeLightCount = 0;

    // Object light flicker colors (one per object light)
    struct LightColor {
        float r = 0, g = 0, b = 0;
    };
    std::vector<LightColor> objectLightColors;

    // Sorted region draw list for front-to-back rendering
    std::vector<SortedRegion> sortedRegions;

    // Shadow vertex buffers for tree wind animation
    // Indexed by [treeIdx][bufferIdx], contains positions for each vertex
    struct TreeBufferShadow {
        std::vector<irr::core::vector3df> positions;
        bool dirty = false;
    };
    std::vector<std::vector<TreeBufferShadow>> treeShadows;

    // Vertex animation results (frame index per mesh)
    struct VertexAnimResult {
        int currentFrame = 0;
        bool frameChanged = false;
    };
    std::vector<VertexAnimResult> vertexAnims;

    // Zone light animation colors (one per animated zone light)
    // Only lights with frameCount > 1 have entries here
    struct ZoneLightAnimColor {
        size_t lightIndex;  // Index into zoneLightData_
        float r, g, b;
        bool updated = false;  // True if frame changed this tick
    };
    std::vector<ZoneLightAnimColor> zoneLightAnimColors;

    // Sky state (computed by worker, applied by render thread)
    struct SkyStateResult {
        float cloudScrollOffset = 0.0f;
        bool valid = false;
    } skyState;

    // Weather effects state (computed by worker, applied by render thread)
    struct WeatherEffectsResult {
        float transitionProgress = 1.0f;
        float currentDarkening = 0.0f;
        float lightningFlashTimer = 0.0f;
        float lightningBoltTimer = 0.0f;
        bool lightningActive = false;
        bool triggerLightningFlash = false;
        bool valid = false;
    } weatherEffectsState;

    // Particle system output
    struct ParticleOutput {
        std::vector<Environment::UnifiedParticle> renderBuffer;  // Alive particles for GPU upload
        int activeCount = 0;
        std::unordered_set<uint16_t> positionRequestEntities;    // Entity IDs worker needs next frame
        std::unordered_set<uint16_t> directionRequestEntities;   // Entity IDs for direction callbacks
        bool valid = false;
    } particleOutput;

    // Boids system output
    struct BoidsOutput {
        struct CreatureRender {
            glm::vec3 position;     // EQ Z-up
            float size;
            uint8_t textureIndex;
            float alpha;
        };
        std::vector<CreatureRender> creatures;
        int activeCount = 0;
        bool valid = false;
    } boidsOutput;

    // Tumbleweed system output
    struct TumbleweedOutput {
        struct TumbleweedRender {
            glm::vec3 position;     // EQ Z-up
            glm::vec3 rotation;     // degrees
            float size;
            bool active;
            int poolIndex;
        };
        struct SpawnEvent { int poolIndex; float size; };
        struct DespawnEvent { int poolIndex; };
        std::vector<TumbleweedRender> tumbleweeds;
        std::vector<SpawnEvent> spawns;
        std::vector<DespawnEvent> despawns;
        int activeCount = 0;
        bool valid = false;
    } tumbleweedOutput;

    // Weather system output
    struct WeatherOutput {
        uint8_t currentWeather = 1;      // WeatherType::Normal
        uint8_t targetWeather = 1;
        float transitionProgress = 1.0f;
        float windIntensity = 0.6f;
        bool weatherChanged = false;     // Main thread should fire listeners
        uint8_t newWeatherType = 0;      // WeatherType to notify about
        bool valid = false;
    } weatherOutput;

    // Detail wind/disturbance output (shadow vertex buffers per chunk)
    struct DetailOutput {
        struct ChunkShadow {
            int32_t keyX = 0, keyZ = 0;
            std::vector<irr::core::vector3df> positions;
            bool dirty = false;
        };
        std::vector<ChunkShadow> chunkShadows;
        bool valid = false;
    } detailOutput;

    // Spell VFX output (desktop GL worker-driven path)
    struct SpellVFXOutput {
        struct EffectUpdate {
            uint32_t effectId;
            float posX, posY, posZ;                     // Billboard position (Irrlicht Y-up)
            float billboardWidth, billboardHeight;       // Size (0 = no change)
            uint8_t colorA, colorR, colorG, colorB;
            float particlePosX, particlePosY, particlePosZ;
            bool hasBillboardUpdate = false;
            bool hasParticleUpdate = false;
            bool hasColorUpdate = false;
        };
        struct CreateEvent {
            uint32_t effectId;
            uint8_t fxType;
            uint32_t spellId;
            uint16_t sourceEntity, targetEntity;
            float posX, posY, posZ;
            float targetPosX, targetPosY, targetPosZ;
            float lifetime, scale;
            uint8_t colorA, colorR, colorG, colorB;
        };
        struct RemoveEvent { uint32_t effectId; };
        struct ImpactEvent { uint16_t targetEntity; uint32_t spellId; };

        std::vector<EffectUpdate> effectUpdates;
        std::vector<CreateEvent> createEvents;
        std::vector<RemoveEvent> removeEvents;
        std::vector<ImpactEvent> impactEvents;
        bool valid = false;
    } spellVfxOutput;

    // Occlusion-culled regions (populated by computeSoftwareOcclusion)
    std::unordered_set<size_t> occlusionCulledRegions;

    // Entity interpolation and visibility results
    struct EntityResult {
        uint16_t spawnId;
        float posX, posY, posZ;
        size_t cachedBspRegion;
        bool bspRegionDirty;
        bool wasInterpolated;    // Position changed
        bool shouldBeVisible;    // Culling result
        bool shouldDeactivate;   // Became stationary
        bool nameTagVisible = false;  // Name tag visibility (distance + PVS + portal + occlusion)
    };
    std::vector<EntityResult> entityResults;
    int entityVisibleCount = 0;

    // Whether this output has been computed at least once (valid data)
    bool valid = false;
};

// ============================================================================
// Zone data registration — immutable pointers set once after zone load
// ============================================================================

struct SimulationZoneData {
    // BSP tree (shared ownership with renderer)
    std::shared_ptr<BspTree> bspTree;
    bool usePvsCulling = false;

    // Region bounding boxes in EQ Z-up coordinates (indexed by region ID)
    // These are copies of the renderer's regionBoundingBoxes_ map
    struct RegionBounds {
        size_t regionIdx;
        float minX, minY, minZ;
        float maxX, maxY, maxZ;
    };
    std::vector<RegionBounds> regionBounds;

    // Object data for visibility culling
    struct ObjectData {
        irr::core::aabbox3df boundingBox;  // Irrlicht Y-up
        irr::core::vector3df position;     // Irrlicht Y-up
        size_t bspRegion;                  // BSP region index (SIZE_MAX if unknown)
        bool hasNode;                      // Whether a scene node exists for this object
    };
    std::vector<ObjectData> objects;

    // Zone light data for visibility culling
    struct ZoneLightData {
        irr::core::vector3df position;     // Irrlicht Y-up
        size_t bspRegion;                  // BSP region index (SIZE_MAX if unknown)
    };
    std::vector<ZoneLightData> zoneLights;

    // Object light data for light selection
    struct ObjectLightData {
        irr::core::vector3df position;     // Irrlicht Y-up
        irr::video::SColorf originalColor;
        float radius;
        float attConstant, attLinear, attQuadratic;
        bool isFireSource;
        float flickerSpeed;
        std::string objectName;            // For debugging
        size_t bspRegion = SIZE_MAX;       // BSP region for PVS culling
    };
    std::vector<ObjectLightData> objectLights;

    // Zone light node data for light selection
    struct ZoneLightNodeData {
        irr::core::vector3df position;     // Irrlicht Y-up
        irr::video::SColorf diffuseColor;
        float radius;
        float attConstant, attLinear, attQuadratic;
    };
    std::vector<ZoneLightNodeData> zoneLightNodes;

    // Portal system (non-owning, immutable after zone load)
    const PortalSystem* portalSystem = nullptr;

    // Collision map for ground snapping (non-owning, immutable after zone load)
    const ::HCMap* hcMap = nullptr;

    // Region occluder triangles for software occlusion culling
    // Keyed by region index → vector of wall triangles
    std::unordered_map<size_t, std::vector<OccluderTriangle>> regionOccluders;

    // Occlusion culler config
    OcclusionCullerConfig occlusionConfig;

    // Animated tree data for wind computation
    struct AnimatedTreeData {
        irr::core::vector3df worldPosition;
        float meshSeed;
        struct BufferData {
            std::vector<irr::core::vector3df> basePositions;
            std::vector<float> vertexHeights;
        };
        std::vector<BufferData> buffers;
    };
    std::vector<AnimatedTreeData> trees;

    // Zone light animation data (animated torches from WLD Fragment 0x1B)
    struct ZoneLightAnimData {
        size_t lightIndex;                  // Index into zoneLightData_
        uint32_t frameCount = 1;
        uint32_t sleepMs = 100;             // Delay between frames
        // Per-frame colors (RGB, size == frameCount)
        std::vector<std::tuple<float,float,float>> frameColors;
        // Per-frame light levels (size == frameCount, used if frameColors empty)
        std::vector<float> lightLevels;
        float baseR = 0, baseG = 0, baseB = 0;  // Base color for lightLevel scaling
    };
    std::vector<ZoneLightAnimData> zoneLightAnims;

    // Vertex-animated mesh data (flags, banners)
    struct VertexAnimData {
        int delayMs = 100;
        size_t frameCount = 0;
        // Frame positions: frames[frameIdx] = flat array of float x,y,z triples
        // positions[vertIdx*3+0] = x, [vertIdx*3+1] = y, [vertIdx*3+2] = z (EQ coords)
        std::vector<std::vector<float>> framePositions;
        // Per-buffer vertex mappings: vertexMapping[bufIdx][vertIdx] = animVertIdx
        std::vector<std::vector<size_t>> vertexMapping;
        float centerOffsetX = 0, centerOffsetY = 0, centerOffsetZ = 0;
        // Per-buffer vertex counts (for output shadow sizing)
        std::vector<size_t> bufferVertexCounts;
    };
    std::vector<VertexAnimData> vertexAnims;
};

// ============================================================================
// SimulationWorker — dedicated thread for computing simulation operations
// ============================================================================

class SimulationWorker {
public:
    SimulationWorker();
    ~SimulationWorker();

    // Thread lifecycle
    void start();
    void stop();
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

    // Zone data registration (call after zone load, before start())
    void setZoneData(const SimulationZoneData& data);
    void clearZoneData();

    // Incremental zone data updates (call while worker is running, between frames)
    // Safe to call after swapAndGetResults() and before postInput() — worker is sleeping.
    void updateTreeData(std::vector<SimulationZoneData::AnimatedTreeData>&& trees);
    void updateVertexAnimData(std::vector<SimulationZoneData::VertexAnimData>&& vertAnims);

    // Per-frame protocol (called from main thread)

    // Post input and signal worker to begin computation
    void postInput(const SimulationInput& input);

    // Try to swap buffers and return the front buffer for applying.
    // Returns pointer to front buffer if new results are ready, nullptr if worker hasn't finished.
    // The returned pointer is valid until the next call to swapAndGetResults().
    const SimulationOutput* swapAndGetResults();

    // Debug info
    struct DebugInfo {
        uint64_t framesComputed = 0;
        uint64_t framesSkipped = 0;      // Worker wasn't done when main thread needed results
        float lastComputeTimeMs = 0;
        float avgComputeTimeMs = 0;
        bool workerBusy = false;
    };
    DebugInfo getDebugInfo() const;

private:
    void workerLoop();

    // Compute functions (called on worker thread)
    void computeAll(const SimulationInput& input, SimulationOutput& output);
    void computeVisibility(const SimulationInput& input, SimulationOutput& output);
    void computeObjectVisibility(const SimulationInput& input, SimulationOutput& output);
    void computeLightVisibility(const SimulationInput& input, SimulationOutput& output);
    void computeObjectLightVisibility(const SimulationInput& input, SimulationOutput& output);
    void computeLightSelection(const SimulationInput& input, SimulationOutput& output);
    void computeFireFlicker(const SimulationInput& input, SimulationOutput& output);
    void computePortalVisibility(const SimulationInput& input, SimulationOutput& output);
    void computeSoftwareOcclusion(const SimulationInput& input, SimulationOutput& output);
    void computeEntitySync(const SimulationInput& input);
    void computeEntityPendingUpdates(const SimulationInput& input);
    void computeEntityInterpolation(const SimulationInput& input, SimulationOutput& output);
    void computeEntityVisibility(const SimulationInput& input, SimulationOutput& output);
    void computeNameTagVisibility(const SimulationInput& input, SimulationOutput& output);
    void computeTreeAnimation(const SimulationInput& input, SimulationOutput& output);
    void computeVertexAnimations(const SimulationInput& input, SimulationOutput& output);
    void computeLightAnimations(const SimulationInput& input, SimulationOutput& output);
    void computeSkyState(const SimulationInput& input, SimulationOutput& output);
    void computeWeatherEffectsState(const SimulationInput& input, SimulationOutput& output);
    void computeParticles(const SimulationInput& input, SimulationOutput& output);
    void computeBoids(const SimulationInput& input, SimulationOutput& output);
    void computeTumbleweeds(const SimulationInput& input, SimulationOutput& output);
    void computeWeather(const SimulationInput& input, SimulationOutput& output);
    void computeDetailAnimation(const SimulationInput& input, SimulationOutput& output);
    void computeSpellVFX(const SimulationInput& input, SimulationOutput& output);

    // Tree wind displacement (replicates TreeWindController::getDisplacement)
    irr::core::vector3df computeTreeWindDisplacement(
        const irr::core::vector3df& worldPos, float normalizedHeight,
        float meshSeed, const SimulationInput::TreeWindState& wind) const;

    // Frustum test helper (AABB vs 6 planes, EQ Z-up coordinates)
    bool testFrustumAABB(const float planes[6][4],
                         float minX, float minY, float minZ,
                         float maxX, float maxY, float maxZ) const;

    // Thread state
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> workReady_{false};
    std::atomic<bool> resultReady_{false};

    // Double-buffered I/O
    SimulationInput input_;                 // Written by main thread, read by worker
    SimulationOutput output_[2];            // Double buffer
    int frontIdx_ = 0;                      // Main thread reads this
    int backIdx_ = 1;                       // Worker writes to this

    // Zone data (read-only after setZoneData, thread-safe)
    SimulationZoneData zoneData_;
    bool zoneDataValid_ = false;

    // Worker-owned software occlusion culler
    std::unique_ptr<SoftwareOcclusionCuller> workerOcclusionCuller_;

    // Worker entity state for interpolation
    struct WorkerEntityState {
        float lastX, lastY, lastZ;
        float velocityX, velocityY, velocityZ;
        float serverX, serverY, serverZ, serverHeading;
        float timeSinceUpdate, lastUpdateInterval;
        float collisionZOffset, modelYOffset;
        int32_t serverAnimation;
        uint32_t lastNonZeroAnimation;
        size_t cachedBspRegion;
        bool bspRegionDirty, isNPC, isPlayer, isCorpse, isFading;
        bool active;  // Has velocity (in active set)
    };
    std::unordered_map<uint16_t, WorkerEntityState> workerEntities_;

    // Fire flicker state (owned by worker thread)
    std::vector<float> flickerPhases_;      // Per object light

    // Vertex animation state (owned by worker thread, persists across frames)
    struct VertAnimState {
        float elapsedMs = 0;
        int currentFrame = 0;
    };
    std::vector<VertAnimState> vertAnimStates_;

    // Zone light animation state (owned by worker thread, persists across frames)
    struct LightAnimState {
        float elapsedMs = 0;
        uint32_t currentFrame = 0;
    };
    std::vector<LightAnimState> lightAnimStates_;

    // Original zone light base colors (captured at setZoneData time, never modified)
    std::vector<irr::video::SColorf> zoneLightBaseColors_;

    // Cached vision/weather state for change detection (zone light color updates)
    uint8_t cachedVisionType_ = 255;            // Force initial apply (255 = invalid)
    float cachedWeatherAmbientModifier_ = -1.0f; // Force initial apply

    // Apply vision/weather modifiers to all non-animated zone light colors in zoneData_
    void applyVisionWeatherToZoneLights(uint8_t visionType, float weatherAmbientModifier);

    // --- Particle system state (owned by worker thread) ---
    // Pool and allocation
    std::vector<Environment::UnifiedParticle> particlePool_;
    std::vector<uint16_t> particleFreeList_;
    int particleActiveCount_ = 0;
    bool particlePoolInitialized_ = false;

    // Emitters
    uint16_t particleNextEmitterID_ = 1;
    std::unordered_map<uint16_t, Environment::ActiveEmitter> particleEmitters_;
    bool particleFireEnabled_ = true;

    // Weather
    uint16_t particleWeatherEmitterID_ = 0;

    // Spell effects
    std::vector<Environment::SpellEffectInstance> particleSpellEffects_;
    uint32_t particleNextSpellEffectID_ = 1;

    // RNG
    std::mt19937 particleRng_;

    // Particle helpers (called on worker thread)
    int allocateParticle();
    void freeParticle(int index);
    void spawnWeatherParticle(const Environment::EmitterConfig& cfg, uint16_t emitterID,
                              const glm::vec3& cameraPos, float transitionAlpha,
                              const glm::vec3& windDir, float windStrength);
    void spawnSpellParticle(const Environment::EmitterConfig& cfg, uint16_t emitterID,
                            const glm::vec3& emitterPos, const glm::vec3* dynamicDir = nullptr);
    float particleRandomFloat(float minVal, float maxVal);
    int particleRandomInt(int minVal, int maxVal);

    // --- Boids system state (owned by worker thread) ---
    struct WorkerCreature {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float speed = 10.0f;
        float size = 1.0f;
        uint8_t textureIndex = 0;
        float animFrame = 0.0f;
        float animSpeed = 1.0f;
        float alpha = 1.0f;
    };
    struct WorkerFlockState {
        Environment::FlockConfig config;
        std::vector<WorkerCreature> creatures;
        glm::vec3 center{0.0f};
        glm::vec3 anchor{0.0f};
        glm::vec3 destination{0.0f};
        glm::vec3 boundsMin{-1000.0f};
        glm::vec3 boundsMax{1000.0f};
        bool hasBounds = false;
        float timeAlive = 0.0f;
        float destinationTimer = 0.0f;
        float destinationInterval = 15.0f;
        bool exitedBounds = false;
        bool scattering = false;
        glm::vec3 scatterSource{0.0f};
        float scatterStrength = 0.0f;
        float scatterTimer = 0.0f;
    };
    std::vector<WorkerFlockState> boidsFlocks_;
    float boidsSpawnTimer_ = 0.0f;
    float boidsSpawnCooldown_ = 30.0f;
    float boidsScatterRadius_ = 20.0f;
    int boidsZoneBiome_ = 0;
    int boidsQuality_ = 2;
    float boidsDensity_ = 1.0f;
    bool boidsEnabled_ = true;
    bool boidsTypeEnabled_[static_cast<size_t>(Environment::CreatureType::Count)];
    glm::vec3 boidsBoundsMin_{-1000.0f};
    glm::vec3 boidsBoundsMax_{1000.0f};
    bool boidsHasBounds_ = false;
    std::mt19937 boidsRng_;

    // Boids helpers
    float boidsRandomFloat(float minVal, float maxVal);
    glm::vec3 boidsGetRandomSpawnPosition(const glm::vec3& playerPos);
    std::vector<Environment::CreatureType> boidsGetTypesForBiome(int biome, bool isDay);

    // --- Tumbleweed system state (owned by worker thread) ---
    struct WorkerTumbleweedInstance {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 angularVelocity{0.0f};
        float radius = 0.5f;
        float size = 1.0f;
        float lifetime = 0.0f;
        uint32_t bounceCount = 0;
        bool active = false;
        int poolIndex = -1;
    };
    std::vector<WorkerTumbleweedInstance> twInstances_;
    float twSpawnTimer_ = 0.0f;
    float twSpawnCooldown_ = 10.0f;
    int twZoneBiome_ = 0;
    bool twEnabled_ = true;
    int twMaxActive_ = 10;
    float twSpawnDistance_ = 80.0f;
    float twDespawnDistance_ = 120.0f;
    float twMinSpeed_ = 2.0f;
    float twMaxSpeed_ = 8.0f;
    float twWindInfluence_ = 1.5f;
    float twBounceDecay_ = 0.6f;
    float twMaxLifetime_ = 60.0f;
    float twGroundOffset_ = 0.3f;
    float twSizeMin_ = 0.6f;
    float twSizeMax_ = 1.4f;
    int twMaxBounces_ = 20;
    int twNextPoolIndex_ = 0;
    std::mt19937 twRng_;

    // Tumbleweed helpers
    float twRandomFloat(float minVal, float maxVal);

    // --- Weather system state (owned by worker thread) ---
    uint8_t weatherCurrentWeather_ = 1;  // WeatherType::Normal
    uint8_t weatherTargetWeather_ = 1;
    float weatherTransitionProgress_ = 1.0f;
    float weatherTransitionDuration_ = 5.0f;
    float weatherTimeSinceLastCheck_ = 0.0f;
    float weatherCurrentDuration_ = 0.0f;
    float weatherCurrentElapsed_ = 0.0f;
    bool weatherSimulationEnabled_ = true;
    ZoneWeatherConfig weatherZoneConfig_;
    std::mt19937 weatherRng_;
    float weatherWindIntensity_ = 0.6f;  // Boids/tumbleweeds read this directly

    void weatherCheckChange();
    uint8_t weatherRollForWeather();
    static float weatherGetWindIntensity(uint8_t type);

    // --- Spell VFX state (owned by worker thread, desktop GL path) ---
    struct WorkerSpellEffect {
        uint32_t effectId;
        uint8_t type;    // SpellFXType cast to uint8_t
        uint32_t spellId;
        uint16_t sourceEntity, targetEntity;
        float elapsed = 0, lifetime = 0, scale = 1.0f;
        uint8_t colorA, colorR, colorG, colorB;
        float posX = 0, posY = 0, posZ = 0;           // Irrlicht Y-up
        float targetPosX = 0, targetPosY = 0, targetPosZ = 0;  // Irrlicht Y-up
        bool active = true;
    };
    std::vector<WorkerSpellEffect> spellVfxEffects_;

    // --- Detail wind/disturbance state (owned by worker thread) ---
    struct WorkerDetailChunk {
        int32_t keyX, keyZ;
        std::vector<irr::core::vector3df> basePositions;
        std::vector<float> windInfluence;
    };
    std::vector<WorkerDetailChunk> detailChunks_;
    float detailWindTime_ = 0;
    struct WorkerResidualDisturbance {
        float posX, posY, posZ;   // Irrlicht Y-up position
        float dirX, dirZ;          // Push direction (XZ plane)
        float intensity;
    };
    std::unordered_map<int64_t, WorkerResidualDisturbance> detailResiduals_;

    // PVS depth map cache (BFS adjacency, no frustum checks)
    size_t cachedDepthMapRegion_ = SIZE_MAX;
    std::unordered_map<size_t, uint8_t> cachedDepthMap_;
    void computeRegionDepthMap(const SimulationInput& input, SimulationOutput& output);

    // Priority tier scheduling
    static constexpr uint32_t kBackgroundInterval = 3;  // Background tier runs every N frames
    uint32_t workerFrameCount_ = 0;  // Worker-side frame counter for tier scheduling

    // Debug stats
    mutable std::mutex debugMutex_;
    uint64_t framesComputed_ = 0;
    uint64_t framesSkipped_ = 0;
    float lastComputeTimeMs_ = 0;
    float avgComputeTimeMs_ = 0;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SIMULATION_WORKER_H
