# WillEQ Frame-Budget Loading System — Implementation Handoff

## Purpose

This document describes the design and implementation of a frame-budget-aware asset loading system for WillEQ. The goal is to guarantee a minimum FPS at all times by controlling when and how much loading work happens per frame, using progressive placeholder rendering so the player always has visual context even while assets are still loading.

**Problem**: Zone loading (S3D parsing, WLD fragment processing, mesh construction, texture decoding, VBO upload) currently happens synchronously and can cause multi-second frame hitches. Entity model loading from `_chr.s3d` files similarly blocks the main thread when new spawns arrive.

Zone and entity load operations cause huge spikes that drop fps from 40+ to sub 10fps and or zero fps (whole app hangs for 1-2 seconds). I'm not sure 100% what is causing these spikes but I know from watching the loading indicator that zone and entity/character loads take a long time. when the 3d scene is first rendered, it takes roughly 10-15 seconds for it to "settle" and become playable. Also, there appear to be some culling/efficiency operations that only occur after movement (rotation works, doesn't require forward/backward). From testing, it appears that there are three distinct fps improvements that happen related to initial movement. Upon first zoning in, fps is usually sub 10. Moving once will bump this up to ~20-30fps. Rotating a second time causes some lights to enable (player/object) and fps goes up to 40-50. From initially rendering the scene through the fps bumps there are usually a number of complete app hangs and or massive fps drops,

The existing tiered rendering system is important but we need the absolute cut-off that the governor system provides so we can police rendering and guarantee minimum 30fps.

REMEMBER: Once the game state has been loaded via the network and the basic Irrlicht renderer has been created, willeq runs at 50-60fps easily while processing game events. We should have a consistent 20-30fps budget to work with. YOU MUST GUARANTEE MINIMUM FPS WITH THIS IMPLEMENTATION. The minimum fps needs to be adjustable via configuration file (add to the constrained presets) and while willeq is running via a slash command.

**Solution**: Decompose all loading into small resumable tasks, meter those tasks against a per-frame time budget, and render lightweight placeholders (HCMap wireframe for zones, con-colored cubes for entities) until real geometry is ready.

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                        Main Render Loop                          │
│                                                                  │
│  1. Process input                                                │
│  2. Update game state                                            │
│  3. FrameBudgetGovernor::beginFrame()                            │
│  4. While governor.hasTimeBudget():                              │
│       LoadTaskQueue::processNextTask()                           │
│  5. Render scene (placeholders + completed geometry)             │
│  6. FrameBudgetGovernor::endFrame()                              │
└──────────────────────────────────────────────────────────────────┘

┌─────────────────┐  ┌───────────────────┐  ┌──────────────────────┐
│ FrameBudget     │  │ LoadTaskQueue     │  │ PlaceholderRenderer  │
│ Governor        │  │                   │  │                      │
│                 │  │ - Zone region     │  │ - HCMap wireframe    │
│ - Tracks frame  │  │   mesh tasks      │  │   (Tier 0 zone)     │
│   time rolling  │  │ - Texture decode  │  │ - Con-colored cubes  │
│   average       │  │   tasks           │  │   (Tier 0 entity)   │
│ - Green/Yellow/ │  │ - Entity model    │  │ - Per-region fade-  │
│   Red states    │  │   load tasks      │  │   out as real geom   │
│ - Reports time  │  │ - VBO upload      │  │   replaces it       │
│   budget per    │  │   tasks           │  │                      │
│   frame         │  │ - Priority sorted │  │                      │
│                 │  │   by PVS+distance │  │                      │
└─────────────────┘  └───────────────────┘  └──────────────────────┘
```

---

## Component 1: FrameBudgetGovernor

### Location
- Header: `include/client/graphics/frame_budget_governor.h`
- Source: `src/client/graphics/frame_budget_governor.cpp`

### Responsibility
Tracks frame timing, determines how much time is available for loading work each frame, and manages a traffic-light state system (Green/Yellow/Red) that controls loading aggressiveness.

### Interface

```cpp
#pragma once
#include <chrono>
#include <cstdint>

class FrameBudgetGovernor {
public:
    enum class State { Green, Yellow, Red };

    // targetFps: the minimum FPS to maintain (e.g. 30 for OrangePi, 60 for desktop)
    explicit FrameBudgetGovernor(float targetFps = 30.0f);

    // Call at the very start of each frame
    void beginFrame();

    // Call at the very end of each frame (after present/
    void endFrame();

    // Returns how many microseconds are available for loading work THIS frame.
    // Returns 0 if in Red state or no budget remains.
    int64_t getAvailableBudgetMicros() const;

    // Returns true if there is still time budget remaining for loading work.
    // Call this in a loop: while (governor.hasTimeBudget()) { processTask(); }
    // Re-checks elapsed time each call so it stays accurate.
    bool hasTimeBudget() const;

    // Consume some of the budget (call after completing a loading task)
    // This is informational — hasTimeBudget() does its own elapsed-time check —
    // but helps the governor track per-task cost for future estimates.
    void consumeBudget(int64_t microsUsed);

    // Current state
    State getState() const;

    // Rolling average frame time in milliseconds (for debug overlay / /frametiming)
    float getAverageFrameTimeMs() const;

    // How many loading tasks were processed this frame (for debug)
    int getTasksProcessedThisFrame() const;

    // Change target FPS at runtime (e.g. when switching constrained presets)
    void setTargetFps(float fps);

private:
    float targetFps_;
    int64_t targetFrameTimeMicros_; // 1e6 / targetFps

    // Rolling average (last N frames)
    static constexpr int HISTORY_SIZE = 30;
    int64_t frameTimeHistory_[HISTORY_SIZE] = {};
    int historyIndex_ = 0;
    int historyCount_ = 0;
    int64_t rollingSum_ = 0;

    // Current frame tracking
    std::chrono::steady_clock::time_point frameStartTime_;
    int64_t lastFrameTimeMicros_ = 0;

    // Budget for current frame
    int64_t frameBudgetMicros_ = 0; // total available for loading
    int tasksProcessedThisFrame_ = 0;

    State currentState_ = State::Green;

    void updateState();
    int64_t computeBudgetForCurrentFrame() const;
};
```

### Behavior

**State transitions** (based on rolling average frame time, NOT single-frame spikes):

| State | Condition | Loading Behavior |
|-------|-----------|-----------------|
| Green | avgFrameTime < 80% of target | Budget = `targetFrameTime - avgFrameTime`. Process multiple tasks. |
| Yellow | avgFrameTime is 80%-100% of target | Budget = `min(2000µs, targetFrameTime - avgFrameTime)`. Max 1 task per frame. |
| Red | avgFrameTime > 100% of target | Budget = 0. No loading. If sustained 60+ frames, consider forcing distant regions to placeholder. |

**Budget computation** each frame:
```
frameBudget = targetFrameTimeMicros - rollingAverageFrameTimeMicros
if (state == Yellow) frameBudget = min(frameBudget, 2000)  // cap at 2ms
if (state == Red)    frameBudget = 0
```

The `hasTimeBudget()` method checks `(elapsed since beginFrame) < (targetFrameTimeMicros - safetyMarginMicros)` in real time, where safety margin is ~2ms. This prevents overrunning even if a single task takes longer than estimated.

### Integration Point

The governor is owned by `IrrlichtRenderer` and called from the main render loop. The `/frametiming` debug command should display the governor's state, average frame time, and tasks-per-frame count alongside existing profiler data.

### Constrained Preset Targets

The target FPS should come from the active `ConstrainedRendererConfig` preset:

| Preset | Target FPS |
|--------|-----------|
| OrangePi | 30 |
| Voodoo1 | 30 |
| Voodoo2 | 30 |
| TNT | 60 |
| Default (desktop) | 60 |

---

## Component 2: LoadTaskQueue

### Location
- Header: `include/client/graphics/load_task_queue.h`
- Source: `src/client/graphics/load_task_queue.cpp`

### Responsibility
Maintains a priority-sorted queue of small, discrete loading tasks. Each task represents one atomic unit of loading work (parse one WLD fragment batch, decode one texture, build one region mesh, upload one VBO, load one entity model, etc.).

### Task Types

```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
namespace irr { namespace scene { class IMeshBuffer; class ISceneNode; } }

enum class LoadTaskType {
    // Zone loading pipeline (in dependency order):
    ZoneParsePFS,          // Extract files from S3D/PFS archive
    ZoneParseWLD,          // Parse WLD fragment table
    ZoneDecodeTexture,     // DXT→RGB decode for one texture (or load ETC1 atlas page)
    ZoneBuildRegionMesh,   // Build SMeshBuffer for one BSP region from WLD data
    ZoneUploadRegion,      // Create Irrlicht scene node + material for one region
    ZoneBuildRegionBatch,  // Build a batch of N regions in one task (for small regions)

    // Entity loading pipeline:
    EntityLoadModel,       // Parse _chr.s3d, extract WLD, build mesh data
    EntityUploadModel,     // Create Irrlicht scene node, bind textures, swap placeholder

    // Texture upgrades:
    TextureUpgrade,        // Replace placeholder solid-color texture with real texture
};

enum class LoadTaskPriority : uint8_t {
    Critical = 0,   // Player's current BSP region, targeted entity
    High     = 1,   // In PVS and in front of camera
    Medium   = 2,   // In PVS but behind camera
    Low      = 3,   // Not in PVS but within render distance
    Deferred = 4,   // Beyond render distance, preload
};

struct LoadTask {
    LoadTaskType type;
    LoadTaskPriority priority;

    // Sorting keys (lower = higher priority)
    float distanceSquared;      // From camera/player position
    float dotWithViewDir;       // -1.0 (behind) to 1.0 (in front), negated for sort
    bool inPVS;                 // Is this region/entity in the current PVS?

    // Payload — what to load
    int regionIndex;            // BSP region index (for zone tasks)
    uint16_t spawnId;           // Entity spawn ID (for entity tasks)
    int textureIndex;           // Texture index (for texture tasks)
    std::string archivePath;    // S3D file path if needed

    // Completion callback — called on main thread after task finishes.
    // For zone regions: removes wireframe placeholder, makes real node visible.
    // For entities: swaps placeholder cube with loaded model.
    std::function<void()> onComplete;

    // For sorting: lower sort key = processed first
    uint64_t sortKey() const;
};

class LoadTaskQueue {
public:
    // Add a task to the queue
    void enqueue(LoadTask task);

    // Process the next highest-priority task.
    // Returns the time in microseconds the task took, or 0 if queue is empty.
    // Only call from the main thread.
    int64_t processNextTask();

    // Re-sort the queue based on updated camera position and PVS.
    // Call when the player moves to a new BSP region.
    void reprioritize(const irr::core::vector3df& cameraPos,
                      const irr::core::vector3df& cameraDir,
                      const std::vector<int>& visibleRegions);

    // Number of pending tasks
    size_t pendingCount() const;

    // Clear all tasks (e.g. on zone change)
    void clear();

    // Check if a specific region is already queued or completed
    bool isRegionQueued(int regionIndex) const;
    bool isRegionComplete(int regionIndex) const;

    // Check if a specific entity is queued or loaded
    bool isEntityQueued(uint16_t spawnId) const;
    bool isEntityComplete(uint16_t spawnId) const;

private:
    // Priority queue — sorted by sortKey() ascending
    // Using a vector + partial_sort rather than std::priority_queue
    // because we need to re-sort when camera moves.
    std::vector<LoadTask> tasks_;

    // Tracking sets
    std::set<int> queuedRegions_;
    std::set<int> completedRegions_;
    std::set<uint16_t> queuedEntities_;
    std::set<uint16_t> completedEntities_;

    bool needsSort_ = false;
    void sortIfNeeded();
};
```

### Sort Key Computation

```cpp
uint64_t LoadTask::sortKey() const {
    // Bits 63-56: priority (0=Critical, 4=Deferred)
    // Bit 55:     NOT inPVS (0=in PVS, 1=not)
    // Bits 54-48: view direction bucket (0=directly ahead, 7=behind)
    // Bits 47-0:  distance (fixed-point, closer = lower)

    uint64_t key = 0;
    key |= (static_cast<uint64_t>(priority) << 56);
    key |= (inPVS ? 0ULL : (1ULL << 55));

    // View direction: map [-1,1] dot product to [7,0] bucket
    int viewBucket = static_cast<int>((1.0f - dotWithViewDir) * 3.5f);
    viewBucket = std::clamp(viewBucket, 0, 7);
    key |= (static_cast<uint64_t>(viewBucket) << 48);

    // Distance: clamp and convert to fixed-point
    uint64_t distKey = static_cast<uint64_t>(std::min(distanceSquared, 1e12f));
    key |= (distKey & 0x0000FFFFFFFFFFFFULL);

    return key;
}
```

This means: Critical tasks always first, then PVS membership, then "in front of camera" beats "behind camera", then closer beats farther.

### Zone Loading Decomposition

The current synchronous zone loading pipeline needs to be split into discrete tasks. Here is the current flow (approximate, based on the architecture described in CLAUDE.md) and how to decompose it:

**Current flow (blocking):**
```
s3d_loader loads zone:
  1. Open S3D archive (PFS)               → ~5ms
  2. Extract zone.wld                      → ~10ms
  3. Parse all WLD fragments               → ~50-200ms
  4. For each BSP region:                  → ~100-500ms TOTAL
       Build SMeshBuffer (vertices, indices, UVs, normals)
       Decode DXT textures for this region's materials
       Create Irrlicht scene node
  5. Upload everything to scene graph      → varies
```

**Decomposed into tasks:**
```
Task 1 (ZoneParsePFS):     Open S3D, extract file list     [~5ms, Critical]
Task 2 (ZoneParseWLD):     Parse WLD fragment table         [~50-200ms — may need sub-splitting]
Tasks 3..N (ZoneDecodeTexture):    One per unique texture    [~1-5ms each]
Tasks N+1..M (ZoneBuildRegionMesh): One per BSP region      [~1-10ms each]
Tasks M+1..P (ZoneUploadRegion):   One per BSP region       [~0.5-2ms each]
```

**Important**: `ZoneParseWLD` may itself be too large for a single task in large zones. If so, split it into batches of N fragments per task (e.g., 50 fragments per task). The WLD fragment table is sequential, so this is straightforward: parse fragments 0-49, then 50-99, etc. Store partial results in a shared `WLDParseState` struct.

**Dependency ordering**: Tasks have implicit dependencies:
- `ZoneDecodeTexture` depends on `ZoneParseWLD` (needs fragment data)
- `ZoneBuildRegionMesh` depends on `ZoneParseWLD` + the textures that region uses
- `ZoneUploadRegion` depends on `ZoneBuildRegionMesh`

The simplest approach: use a phased pipeline. Complete all PFS/WLD parsing first (these are fast enough to run in one or a few frames), then enqueue all texture decode tasks and region mesh build tasks. The queue naturally handles ordering since higher-priority regions get their textures decoded first.

### Entity Loading Decomposition

When a spawn packet arrives and creates an entity:

```
Current flow (blocking):
  RaceModelLoader::getMeshForRace()
    1. Try race-specific S3D           → may involve PFS open + WLD parse
    2. Try zone _chr.s3d               → same
    3. Try global_chr.s3d files        → same
    4. Build animated mesh
    5. Apply equipment/tint
    6. Fallback: colored cube           ← THIS IS THE KEY

Decomposed:
  1. Immediately create con-colored placeholder cube (see PlaceholderRenderer)
  2. Enqueue EntityLoadModel task (priority based on distance)
  3. When EntityLoadModel completes, enqueue EntityUploadModel
  4. EntityUploadModel swaps placeholder cube for real model
```

The placeholder cube is already the existing fallback path in `RaceModelLoader`. The change is to make the fallback the *default initial state* and make the real model load asynchronous.

---

## Component 3: PlaceholderRenderer

### Location
- Header: `include/client/graphics/placeholder_renderer.h`
- Source: `src/client/graphics/placeholder_renderer.cpp`

### Responsibility
Creates and manages lightweight placeholder geometry displayed while real assets load. Two types: HCMap wireframe for zone terrain, and con-colored cubes for entities.

### 3A: Zone Placeholder (HCMap Wireframe)

#### Data Source

`HCMap` loads zone `.map` files and stores geometry in a `RaycastMesh` (AABB tree of triangles). The mesh is always loaded for collision/LOS regardless of graphics state. The data is stored in Y-up format internally (matching Irrlicht's coordinate system after the Y↔Z swap during loading).

**Key**: The HCMap triangles ARE in Irrlicht-compatible coordinates already. `GetTrianglesInRadius()` returns triangles in mesh-internal coordinates (Y-up). No additional coordinate transform is needed for rendering.

#### Implementation

```cpp
#pragma once
#include <irrlicht.h>
#include <vector>
#include <unordered_set>

class HCMap;  // Forward declaration

class ZonePlaceholder {
public:
    // Build placeholder mesh from HCMap collision data.
    // Call once at zone entry, before any S3D loading begins.
    //
    // hcMap: the loaded HCMap for this zone (already in Y-up coords internally)
    // smgr: Irrlicht scene manager
    // fogColor: zone fog color (from server) — used to tint the wireframe
    void build(const HCMap* hcMap,
               irr::scene::ISceneManager* smgr,
               irr::video::SColor fogColor);

    // Mark a BSP region as loaded (real geometry is now in scene).
    // The placeholder geometry for that region's area will be hidden.
    //
    // regionBBox: bounding box of the loaded region (in Irrlicht Y-up coords)
    void markRegionLoaded(int regionIndex);

    // Remove all placeholder geometry from the scene.
    // Call when zone loading is fully complete or on zone exit.
    void destroy();

    // Returns true if any placeholder geometry is still visible
    bool isActive() const;

    // For debug: number of placeholder triangles still visible
    int visibleTriangleCount() const;

private:
    irr::scene::IMeshSceneNode* placeholderNode_ = nullptr;
    irr::scene::SMesh* placeholderMesh_ = nullptr;
    int totalTriangles_ = 0;
    bool active_ = false;

    // Track which regions have been replaced by real geometry
    std::unordered_set<int> loadedRegions_;
};
```

#### Build Process

1. Access the HCMap's internal triangle data. The `RaycastMesh` stores vertices and indices — you need a way to iterate them. If `RaycastMesh` doesn't expose a public iterator, add a `getTriangles()` method that returns a `const` reference to the vertex/index arrays. (The data is already there for `GetTrianglesInRadius()` — we just need broader access.)

2. Build a single `SMeshBuffer` with `EMT_SOLID` material:
   - Vertex format: `S3DVertex` with position, normal (face normal from triangle), and color
   - Color: derived from zone fog color but lighter/desaturated. Something like:
     ```cpp
     // Make it look like a ghostly wireframe version of the zone
     irr::video::SColor baseColor(
         180,  // alpha (slightly transparent if blending available, otherwise opaque)
         std::min(255, fogColor.getRed() + 60),
         std::min(255, fogColor.getGreen() + 60),
         std::min(255, fogColor.getBlue() + 80)
     );
     ```
   - Alternatively, use height-based gradient: low areas are darker, high areas are lighter. This gives immediate terrain readability:
     ```cpp
     // Map vertex Y (height in Irrlicht) to a brightness value
     float heightNorm = (vertex.Y - minY) / (maxY - minY);  // 0..1
     uint8_t brightness = static_cast<uint8_t>(80 + heightNorm * 120); // 80..200
     vertex.Color = irr::video::SColor(255, brightness, brightness, brightness + 30);
     ```

3. Create the scene node with wireframe rendering:
   ```cpp
   placeholderNode_ = smgr->addMeshSceneNode(placeholderMesh_);
   placeholderNode_->setMaterialFlag(irr::video::EMF_WIREFRAME, true);
   placeholderNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
   placeholderNode_->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
   // Render order: make sure placeholder is behind real geometry
   placeholderNode_->setMaterialType(irr::video::EMT_SOLID);
   ```

4. **Important for GLES2 path**: Wireframe rendering may not be supported on Mali 400 / Lima driver (`glPolygonMode` is desktop-only). For GLES2, use `GL_LINES` draw mode instead. You'll need to convert the triangle list to a line list (3 lines per triangle: AB, BC, CA, deduplicated). Alternatively, render as solid triangles with the desaturated height-gradient color — this actually gives better visual context than wireframe on a small screen. Choose based on platform:
   - Software renderer (Burnings): wireframe works
   - Desktop GL 2.1: wireframe works (`glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)`)
   - GLES2: render as solid flat-shaded triangles (no wireframe available)

#### Teardown

As each BSP region's real geometry finishes loading (ZoneUploadRegion task completes), mark that region via `markRegionLoaded()`. Two strategies for removal:

**Simple approach (recommended for first implementation)**: Keep the entire HCMap placeholder visible until ALL regions in the PVS are loaded, then remove it in one shot. This avoids the complexity of per-region mesh editing. The real geometry renders on top of the placeholder due to proper Z-buffering anyway, so having both visible temporarily is fine — the placeholder just peeks through where real geometry hasn't loaded yet.

**Advanced approach (later optimization)**: Build the placeholder as multiple mesh buffers grouped by spatial region (e.g., one per quadtree cell), and hide each buffer as its corresponding zone regions load. This gives a nicer progressive reveal but is more complex.

### 3B: Entity Placeholder (Con-Colored Cubes)

#### Con Color Computation (No Server Communication)

Since the GLES renderer doesn't have GL_LINES support, render the placeholders as flat-shaded solid triangles with height-gradient coloring,

The con color is computed purely from the level difference between the player and the target entity. The client already has both pieces of data locally:
- Player level: stored in the client's player state (received during zone-in, kept current)
- Entity level: received in the `Spawn_Struct` packet when the entity spawns (field: `level`)

**EQ Titanium con color rules** (level difference = entity_level - player_level):

```cpp
// Compute con color from level difference alone.
// No server communication needed — both levels are in local client state.
//
// These colors match the original EQ Titanium client's consider system.
// The consider MESSAGE (indifferent, threatens, scowls, etc.) requires
// server faction data, but the COLOR is purely level-based.

irr::video::SColor getConColor(int playerLevel, int entityLevel) {
    int diff = entityLevel - playerLevel;

    if (entityLevel <= 0) {
        // Unknown/invalid level — use white
        return irr::video::SColor(255, 255, 255, 255);
    }

    // Gray (trivial, no experience)
    // Rule: green threshold varies by player level
    int greenCutoff;
    if (playerLevel < 8)        greenCutoff = -4;
    else if (playerLevel < 13)  greenCutoff = -6;
    else if (playerLevel < 21)  greenCutoff = -8;
    else if (playerLevel < 26)  greenCutoff = -10;
    else if (playerLevel < 31)  greenCutoff = -12;
    else if (playerLevel < 36)  greenCutoff = -14;
    else if (playerLevel < 41)  greenCutoff = -16;
    else if (playerLevel < 46)  greenCutoff = -18;
    else if (playerLevel < 51)  greenCutoff = -20;
    else if (playerLevel < 56)  greenCutoff = -22;
    else if (playerLevel < 61)  greenCutoff = -24;
    else                        greenCutoff = -26;

    if (diff <= greenCutoff)
        return irr::video::SColor(255, 128, 128, 128);  // Gray

    // Green (easy)
    if (diff < -5)
        return irr::video::SColor(255, 0, 200, 0);      // Green

    // Light blue (slightly below)
    if (diff < -1)
        return irr::video::SColor(255, 0, 160, 255);    // Light blue

    // White (even match)
    if (diff < 3)
        return irr::video::SColor(255, 255, 255, 255);  // White

    // Yellow (somewhat dangerous)
    if (diff < 6)
        return irr::video::SColor(255, 255, 255, 0);    // Yellow

    // Red (very dangerous)
    return irr::video::SColor(255, 255, 0, 0);          // Red
}
```

**Note**: The exact green/gray cutoff thresholds above are approximate and based on common community knowledge of the Titanium-era con system. The actual thresholds should be verified against the EQEmu server source (`GetConsiderDifficulty()` or similar) or the `OP_Consider` response packet. The COLORS themselves are standard EQ con colors and correct. If precise cutoffs matter, a simpler version that uses just the diff is fine for placeholder purposes:

```cpp
// Simplified version — good enough for placeholders
irr::video::SColor getConColorSimple(int playerLevel, int entityLevel) {
    int diff = entityLevel - playerLevel;
    if (diff < -10)     return {255, 128, 128, 128}; // Gray
    if (diff < -5)      return {255, 0, 200, 0};     // Green
    if (diff < -1)      return {255, 0, 160, 255};   // Light blue
    if (diff < 3)       return {255, 255, 255, 255}; // White
    if (diff < 6)       return {255, 255, 255, 0};   // Yellow
    return              {255, 255, 0, 0};             // Red
}
```

#### Cube Placeholder Rendering

`RaceModelLoader` already creates colored cubes as a fallback when model files can't be found. The change is to make this the *default initial state* for all entities, then upgrade to the real model when loading completes.

```cpp
class EntityPlaceholder {
public:
    // Create a con-colored cube for an entity.
    // playerLevel: local player's level
    // entityLevel: from spawn packet
    // position: entity position in Irrlicht coords (x, z, y swap already done)
    // height: approximate entity height (use race default or 6.0f as fallback)
    irr::scene::ISceneNode* createPlaceholder(
        irr::scene::ISceneManager* smgr,
        int playerLevel,
        int entityLevel,
        const irr::core::vector3df& position,
        float height = 6.0f);

    // Swap a placeholder with a loaded model.
    // Handles: removing placeholder node, inserting model node at same position.
    void swapWithModel(uint16_t spawnId,
                       irr::scene::ISceneNode* placeholderNode,
                       irr::scene::IAnimatedMeshSceneNode* modelNode);

    // Remove a placeholder (entity despawned before model loaded)
    void removePlaceholder(uint16_t spawnId);
};
```

The cube should be:
- Sized approximately to the entity's expected dimensions (use race-based height if available, otherwise ~6 units for humanoids, ~3 for small creatures, ~12 for large)
- Colored with the con color (all faces same color)
- Unlit (`EMF_LIGHTING = false`) so it's always visible regardless of zone lighting state
- Y-positioned so the base sits on the ground, not centered at the entity origin

**Enhancement**: For entities with `bodytype` or other type info in the spawn packet, vary the placeholder shape slightly — taller/thinner for humanoids, flatter/wider for animals. But plain cubes are fine for the initial implementation.

---

## Component 4: Integration with IrrlichtRenderer

### Zone Load Flow (Modified)

Current zone load (simplified pseudocode from the architecture):
```cpp
void IrrlichtRenderer::loadZone(const std::string& zoneName) {
    // BLOCKING: loads everything synchronously
    auto s3dData = s3dLoader_.loadZone(eqClientPath_, zoneName);
    auto meshes = zoneGeometry_.buildAllMeshes(s3dData);
    for (auto& mesh : meshes) {
        smgr_->addMeshSceneNode(mesh);
    }
}
```

New zone load flow:
```cpp
void IrrlichtRenderer::loadZone(const std::string& zoneName) {
    // 1. Load HCMap (already happens — used for collision).
    //    Build zone placeholder from HCMap data immediately.
    zonePlaceholder_.build(hcMap_, smgr_, currentFogColor_);

    // 2. Quick synchronous parse: open S3D, parse WLD fragment table.
    //    This is relatively fast (~50-200ms) and gives us the BSP region
    //    list, texture list, and PVS data we need to plan the loading.
    //    
    //    ALTERNATIVE: If even this is too slow for large zones, make
    //    ZoneParsePFS and ZoneParseWLD into tasks too. But try synchronous first.
    auto parseResult = s3dLoader_.parseZoneMetadata(eqClientPath_, zoneName);

    // 3. Enqueue loading tasks for each BSP region, prioritized by PVS.
    auto visibleRegions = getVisibleRegionsFromPVS(parseResult, playerPosition_);

    for (int regionIdx = 0; regionIdx < parseResult.regionCount; regionIdx++) {
        LoadTask task;
        task.type = LoadTaskType::ZoneBuildRegionMesh;
        task.regionIndex = regionIdx;
        task.inPVS = visibleRegions.count(regionIdx) > 0;
        task.distanceSquared = computeDistanceSq(playerPosition_,
                                                  parseResult.regionBBox[regionIdx]);
        task.priority = task.inPVS ? LoadTaskPriority::High : LoadTaskPriority::Low;

        task.onComplete = [this, regionIdx]() {
            zonePlaceholder_.markRegionLoaded(regionIdx);
        };

        loadTaskQueue_.enqueue(std::move(task));
    }

    // 4. Enqueue texture decode tasks
    for (int texIdx = 0; texIdx < parseResult.textureCount; texIdx++) {
        LoadTask task;
        task.type = LoadTaskType::ZoneDecodeTexture;
        task.textureIndex = texIdx;
        task.priority = LoadTaskPriority::High; // Textures needed by many regions
        loadTaskQueue_.enqueue(std::move(task));
    }

    // 5. The main render loop (already running) will process tasks
    //    via the FrameBudgetGovernor. Zone progressively appears.
}
```

### Entity Spawn Flow (Modified)

Current entity spawn handling (in eq.cpp → renderer):
```cpp
void IrrlichtRenderer::spawnEntity(const Entity& entity) {
    // BLOCKING: loads model synchronously
    auto mesh = raceModelLoader_.getMeshForRace(entity.race, entity.gender, ...);
    auto node = smgr_->addAnimatedMeshSceneNode(mesh);
    node->setPosition(toIrrlichtCoords(entity.x, entity.y, entity.z));
    entityNodes_[entity.spawn_id] = node;
}
```

New entity spawn flow:
```cpp
void IrrlichtRenderer::spawnEntity(const Entity& entity) {
    // 1. Immediately create con-colored placeholder
    irr::video::SColor conColor = getConColor(playerLevel_, entity.level);
    auto placeholder = entityPlaceholder_.createPlaceholder(
        smgr_, playerLevel_, entity.level,
        toIrrlichtCoords(entity.x, entity.y, entity.z));
    entityNodes_[entity.spawn_id] = placeholder;

    // 2. Enqueue model load task
    LoadTask task;
    task.type = LoadTaskType::EntityLoadModel;
    task.spawnId = entity.spawn_id;
    task.distanceSquared = computeDistanceSq(
        playerPosition_, {entity.x, entity.y, entity.z});
    task.priority = (entity.spawn_id == targetSpawnId_)
        ? LoadTaskPriority::Critical
        : LoadTaskPriority::Medium;

    task.onComplete = [this, spawnId = entity.spawn_id]() {
        // Swap placeholder with loaded model
        // (details handled in EntityUploadModel task)
    };

    loadTaskQueue_.enqueue(std::move(task));
}
```

### Main Loop Integration

The render loop in `IrrlichtRenderer` (or wherever `driver_->beginScene()` / `endScene()` happens) needs the governor and task processing inserted:

```cpp
void IrrlichtRenderer::renderFrame() {
    governor_.beginFrame();

    // --- Existing: process input, update camera, update entities ---
    updateScene();

    // --- NEW: process loading tasks within frame budget ---
    while (governor_.hasTimeBudget()) {
        int64_t taskTime = loadTaskQueue_.processNextTask();
        if (taskTime == 0) break;  // Queue empty
        governor_.consumeBudget(taskTime);
    }

    // --- Existing: render scene ---
    driver_->beginScene(true, true, clearColor_);
    smgr_->drawAll();      // Draws both placeholders and loaded geometry
    // ... UI, overlays, etc ...
    driver_->endScene();

    governor_.endFrame();
}
```

---

## Component 5: Zone Loading State Machine

To coordinate the phased loading, introduce a simple state enum:

```cpp
enum class ZoneLoadPhase {
    Idle,           // No zone loading in progress
    ParsePending,   // S3D/WLD parsing (synchronous or tasked)
    ParseComplete,  // Metadata ready, tasks being enqueued
    Loading,        // Tasks being processed by governor
    Complete,       // All regions loaded, placeholder removed
};
```

This lives in `IrrlichtRenderer` (or a new `ZoneLoadManager` class if you prefer separation). The phase drives behavior:

- **ParsePending**: Show only HCMap placeholder. Enqueue parse tasks if async, or do sync parse.
- **ParseComplete**: PVS data available. Enqueue region build tasks. Start processing.
- **Loading**: Governor meters task processing. Placeholder fades out region by region. The existing PVS culling, frustum culling, and front-to-back sorting all work as normal — they just operate on whatever subset of regions is loaded so far.
- **Complete**: Placeholder destroyed. Normal rendering.

---

## Debug Commands and Observability

Add or extend these commands to monitor the system:

| Command | Behavior |
|---------|----------|
| `/frametiming` | (existing) Add governor state, budget remaining, tasks/frame to the overlay |
| `/loadstatus` | New: Show zone load phase, regions loaded/total, entities loaded/total, queue depth |
| `/placeholder` | New: Toggle zone placeholder visibility (for debugging — see wireframe even when geometry loaded) |
| `/loadpause` | New: Pause/resume the loading task queue (useful for testing placeholder rendering) |

### Frame Timing Overlay Extension

The existing `/frametiming` profiler overlay should show additional fields:

```
Frame: 16.2ms (avg 15.8ms)  [GREEN]
Budget: 4.2ms  Tasks: 3  Queue: 47
Zone: Loading [82/126 regions]  Entities: [14/19]
```

---

## Task Execution Details

### ZoneBuildRegionMesh Task

This is the heaviest individual task type. It takes the pre-parsed WLD fragment data for one BSP region and builds an Irrlicht `SMeshBuffer`. Key steps:

1. Look up the region's 0x22 fragment → get mesh reference (0x36)
2. Gather vertices, indices, UV coords, normals from the WLD data
3. Apply coordinate transform: `EQ(x,y,z) → Irrlicht(x,z,y)` (swap Y↔Z)
4. Look up texture references → use already-decoded textures or solid-color placeholders
5. Build `SMeshBuffer` with `S3DVertex` format
6. If GLES2: create VBO via `glBufferData(GL_STATIC_DRAW)`
7. Create Irrlicht scene node, set material, set position

**Critical detail**: Steps 1-5 are pure computation and could theoretically run off the main thread. Step 6-7 MUST run on the main thread (OpenGL context is thread-bound). If you want to split this further, the task can do 1-5 and produce a "ready package" (vertex array + index array + material info), then a separate `ZoneUploadRegion` task does 6-7. This keeps the main-thread cost per region to just the VBO upload + scene node creation (~0.5-2ms).

### ZoneDecodeTexture Task

Decodes one DXT texture from the WLD data:
1. Get compressed texture data from parsed WLD
2. Run DXT1/DXT3/DXT5 decode via `dds_decoder.cpp`
3. Create Irrlicht texture object from decoded pixels
4. Store in a texture cache indexed by WLD fragment reference

On GLES2 with ETC1: if using the offline `zone_atlas_builder`, the ETC1 atlas pages may already be on disk. The task then becomes: load ETC1 data from file → `glCompressedTexImage2D` upload. This is much faster than runtime DXT decode.

### EntityLoadModel Task

Loads one character/NPC model:
1. Determine S3D archive path(s) to search (race-specific → zone chr → global chr)
2. Open S3D, parse WLD for mesh and skeleton
3. Build animated mesh data (vertices, bones, animation frames)
4. Decode textures for this model
5. Produce a "ready package" (mesh + textures + animation data)

Then `EntityUploadModel` (separate task, main thread):
1. Create `IAnimatedMeshSceneNode` from ready package
2. Apply equipment/tint from entity data
3. Swap with placeholder cube
4. Set position, rotation, scale, current animation

---

## Reprioritization

When the player moves to a new BSP region, call `LoadTaskQueue::reprioritize()` with the updated PVS. This re-sorts pending tasks so that newly visible regions jump to the front of the queue. Regions that were high priority but are now behind the player (or out of PVS) get demoted.

Also reprioritize when:
- The player changes target (targeted entity → Critical priority)
- The camera rotates significantly (changes which regions are "in front")
- An entity moves close to the player (distance changed)

Don't reprioritize every frame — it's a full sort of the queue. Every 500ms or on BSP region change is sufficient.

---

## Implementation Order

Recommended order to implement and test incrementally:

### Phase 1: FrameBudgetGovernor (standalone, testable immediately)
1. Implement `FrameBudgetGovernor` class
2. Integrate into render loop (beginFrame/endFrame only, no tasks yet)
3. Add governor state to `/frametiming` overlay
4. Verify it correctly tracks frame times and transitions between Green/Yellow/Red
5. **Test**: Run the client normally. Governor should report Green with full budget.

### Phase 2: HCMap Zone Placeholder
1. Add method to `RaycastMesh` / `HCMap` to expose raw triangle data for rendering
2. Implement `ZonePlaceholder::build()` — create wireframe/flat mesh from HCMap
3. Wire it in: at zone load start, build placeholder. At zone load end, destroy it.
4. **Test**: Add a deliberate `sleep()` in zone loading. Verify the wireframe appears instantly while zone loads in the background. Verify Z-buffer: real geometry renders on top of placeholder.

### Phase 3: Entity Con-Colored Placeholders
1. Implement `getConColor()` function
2. Implement `EntityPlaceholder::createPlaceholder()` — con-colored cube
3. Modify entity spawn flow: create placeholder immediately, load model later
4. For now, load the model synchronously right after — the placeholder just flashes briefly. This proves the swap mechanism works.
5. **Test**: Spawn near entities. Verify cubes appear in correct con colors. Verify swap to real model is seamless (no position jump, no flicker).

### Phase 4: LoadTaskQueue + Zone Decomposition
1. Implement `LoadTaskQueue` with priority sorting
2. Decompose `zone_geometry.cpp` mesh building into per-region tasks
3. Wire zone loading to enqueue tasks instead of building synchronously
4. Governor meters task execution
5. **Test**: Load a zone. Verify progressive region appearance. Verify FPS stays at target. Use `/loadstatus` to monitor progress. Try a large zone (e.g., Everfrost, Commonlands) and a small one (e.g., an indoor dungeon).

### Phase 5: Entity Model Deferred Loading
1. Make `RaceModelLoader` produce "ready packages" instead of scene nodes
2. Entity spawn enqueues load task, creates placeholder
3. Task completion swaps placeholder for model
4. **Test**: Zone into a populated area. Verify entities appear as colored cubes first, then models pop in. Verify targeted entity loads with Critical priority (loads first).

### Phase 6: Reprioritization + Polish
1. Implement PVS-based reprioritization on BSP region change
2. Camera-direction-based sorting
3. Target-entity priority boost
4. Tune budget thresholds for each constrained preset
5. **Test**: Walk through a zone. Verify geometry appears smoothly in front of you. Turn around — verify behind-you regions load after in-front regions.

---

## Risks and Edge Cases

**Zone transitions**: On zone change, `LoadTaskQueue::clear()` must be called immediately to discard all pending tasks for the old zone. The old zone's placeholder must be destroyed. The new zone starts fresh.

**Entity despawn during loading**: If an entity despawns (leaves zone, dies, etc.) while its model load task is still queued, the task must be cancelled or its completion callback must safely handle the entity being gone. Use spawn_id lookup — if entity no longer exists in `m_entities`, the completion callback is a no-op.

**Memory pressure**: On constrained hardware (OrangePi with 512MB RAM), having both the HCMap placeholder mesh AND progressively loading zone geometry could spike memory usage. Monitor this. The placeholder mesh is small (HCMap data is typically <1MB), but watch for accumulation during the loading phase.

**WLD parse atomicity**: The WLD fragment parser may have internal state that makes it hard to pause mid-parse. If so, let `ZoneParseWLD` remain a single (possibly large) task and accept that it may cause one frame hitch. It's a one-time cost per zone load and typically <200ms — a single frame skip at 30fps. This is acceptable as a v1 trade-off. Optimize later if needed.

**Software renderer (Burnings)**: The Burnings software renderer doesn't have VBOs — it reads vertices from CPU memory each frame. The "upload" step is essentially free (just adding a scene node). But the per-frame render cost of having many scene nodes is higher. The governor's budget calculation works the same way — frame time is frame time regardless of renderer.

**GLES2 VBO upload timing**: `glBufferData(GL_STATIC_DRAW)` on Mali 400 can stall the pipeline if the GPU is busy. Keep VBO uploads small (one region at a time, not multiple) and don't upload in the same frame as a heavy draw call. The governor's time-checking in `hasTimeBudget()` naturally handles this — if a VBO upload takes 5ms, the governor will stop processing tasks for that frame.

---

## File Summary

New files to create:

| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `include/client/graphics/frame_budget_governor.h` | ~60 | Governor interface |
| `src/client/graphics/frame_budget_governor.cpp` | ~120 | Governor implementation |
| `include/client/graphics/load_task_queue.h` | ~80 | Task queue interface |
| `src/client/graphics/load_task_queue.cpp` | ~200 | Task queue, sorting, execution |
| `include/client/graphics/placeholder_renderer.h` | ~60 | Zone + entity placeholder interfaces |
| `src/client/graphics/placeholder_renderer.cpp` | ~250 | HCMap wireframe, con cubes, swap logic |

Files to modify:

| File | Changes |
|------|---------|
| `src/client/graphics/irrlicht_renderer.cpp` | Add governor + task queue to render loop; modify zone load and entity spawn flows |
| `include/client/graphics/irrlicht_renderer.h` | Add governor, task queue, placeholder members |
| `src/client/graphics/eq/zone_geometry.cpp` | Refactor into per-region build functions callable from tasks |
| `src/client/graphics/entity_renderer.cpp` | Support placeholder→model swap |
| `src/client/graphics/eq/s3d_loader.cpp` | Add `parseZoneMetadata()` that parses without building meshes |
| `include/client/hc_map.h` | Expose triangle data for placeholder rendering |
| `src/client/action/command_processor.cpp` | Add `/loadstatus`, `/placeholder`, `/loadpause` commands |
| `CMakeLists.txt` | Add new source files |

---

## References

- CLAUDE.md sections: Architecture, Core Components, Zone Rendering Optimizations, Renderer Tiers, Coordinate Systems
- README.md sections: Rendering Pipeline, Key Components, Constrained HW presets
- Existing debug commands: `/frametiming`, `/sort`, `/portal`, Ctrl+M (map overlay), Ctrl+N (navmesh overlay)
- Existing patterns: `RaceModelLoader` fallback cubes, `ConstrainedRendererConfig` presets, `ZoneShader` GLES2 path
- EQ con color system: level-difference based, no server round-trip needed for color
