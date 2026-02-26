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
};

// ============================================================================
// SimulationOutput — results computed by worker, applied by main thread
// ============================================================================

struct SimulationOutput {
    // Visibility results (pre-allocated at zone load, one byte per region/object/light)
    std::vector<uint8_t> regionVisible;     // 1=visible, 0=hidden
    std::vector<uint8_t> objectVisible;     // 1=visible, 0=hidden
    std::vector<uint8_t> lightVisible;      // 1=visible, 0=hidden
    size_t currentPvsRegion = SIZE_MAX;     // Camera's current BSP region

    // Portal-visible regions (BFS walk from camera room through portal graph)
    std::unordered_set<size_t> portalVisibleRegions;

    // Regions needing lazy mesh loading (for constrained mesh cache)
    std::vector<size_t> meshLoadQueue;
    // Protected regions (visible + buffer ring, skip during eviction)
    std::vector<size_t> protectedRegions;

    // Light selection (top 8)
    struct SelectedLight {
        irr::core::vector3df position;     // Irrlicht Y-up
        irr::video::SColorf diffuseColor;  // Current color (including flicker)
        irr::video::SColorf originalColor; // Original color (pre-flicker)
        float radius = 0;
        float attConstant = 0, attLinear = 0, attQuadratic = 0;
        size_t sourceIndex = SIZE_MAX;     // Index into zoneLightNodes_ or objectLights_
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
    struct SortedRegion {
        size_t regionIdx;
        float distanceSq;
    };
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
        size_t lightIndex;  // Index into zoneLightNodes_
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
        size_t lightIndex;                  // Index into zoneLightNodes_
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

    // Get the current front buffer (last successfully swapped results).
    // Returns nullptr if no results have ever been produced.
    const SimulationOutput* getFrontBuffer() const;

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
    void computeLightSelection(const SimulationInput& input, SimulationOutput& output);
    void computeFireFlicker(const SimulationInput& input, SimulationOutput& output);
    void computePortalVisibility(const SimulationInput& input, SimulationOutput& output);
    void computeTreeAnimation(const SimulationInput& input, SimulationOutput& output);
    void computeVertexAnimations(const SimulationInput& input, SimulationOutput& output);
    void computeLightAnimations(const SimulationInput& input, SimulationOutput& output);
    void computeSkyState(const SimulationInput& input, SimulationOutput& output);
    void computeWeatherEffectsState(const SimulationInput& input, SimulationOutput& output);
    void computeParticles(const SimulationInput& input, SimulationOutput& output);

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
