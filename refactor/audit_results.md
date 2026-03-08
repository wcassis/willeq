# willeq Code Audit Results

Audit conducted against the architecture defined in `arch_design.md` and `arch_design_update.md`.
Branch: `orangepi` (compared against `main`).

---

## Table of Contents

1. [Thread Inventory](#1-thread-inventory)
2. [Work Scheduling](#2-work-scheduling)
3. [Budget Tracking](#3-budget-tracking)
4. [Loading Code Paths](#4-loading-code-paths)
5. [Placeholder Coverage](#5-placeholder-coverage)
6. [Dead Code](#6-dead-code)
7. [Governor Compliance](#7-governor-compliance)
8. [Summary of Issues](#8-summary-of-issues)

---

## 1. Thread Inventory

The codebase creates **13 distinct threads** (including 1 third-party). No `std::async`,
`std::future`, `.detach()`, or thread pools exist. All threads are joined before
destruction.

### Sanctioned Threads

| # | Name | File | Lifetime | Conditional |
|---|------|------|----------|-------------|
| 1 | Game Update | `eq.cpp:15421` | Per zone-in | No |
| 2 | Console Input | `console_input_handler.cpp:24` | App lifetime | Headless mode |
| 3 | Zone Load | `irrlicht_renderer.cpp:3508` | Single-shot per zone | Graphics mode |
| 4 | Deferred Work | `irrlicht_renderer.cpp:3833` | Single-shot per zone | Manual load mode |
| 5 | BSP Preload | `irrlicht_renderer.cpp:3135` | Single-shot per zone | Graphics mode |
| 6 | Entity Prep Worker | `entity_prep_worker.cpp:25` | Zone lifetime | Graphics mode |
| 7 | GPU Upload | `gpu_upload_thread.cpp:108` | Renderer lifetime | GLES2 only |
| 8 | Simulation Worker | `simulation_worker.cpp:47` | Zone lifetime | Graphics mode |
| 9 | Item Icon Loader | `item_icon_loader.cpp:44` | Renderer lifetime | Graphics mode |
| 10 | RDP Listener | `rdp_server.cpp:319` | RDP lifetime | RDP enabled |
| 11 | RDP Peer(s) | `rdp_server.cpp:752` | Per connection | RDP enabled |
| 12 | RDP Audio Pump | `audio_backend.cpp:98` | RDP+audio lifetime | RDP+audio |
| 13 | Miniaudio Device | `miniaudio.h:17674` (pthread) | Audio lifetime | Audio enabled |

### Thread Architecture vs. Design Document

The design document (`arch_design_update.md`) defines 4 thread roles: Render, GPU Upload,
Simulation, Background. The current codebase maps to this as follows:

| Design Role | Current Threads | Notes |
|-------------|----------------|-------|
| **Render** | Main thread | Correct — single instance |
| **GPU Upload** | GPU Upload Thread (#7) | Correct — single instance, GLES2 only |
| **Simulation** | Simulation Worker (#8) | Correct — single instance |
| **Background** | Zone Load (#3), Deferred Work (#4), BSP Preload (#5), Entity Prep Worker (#6), Item Icon Loader (#9) | **5 separate threads instead of 1 unified background thread** |

### Issues Found

**ISSUE 1.1 — Multiple independent background threads instead of unified work queue.**
The design calls for a single background thread (or configurable pool) consuming from a
unified priority queue. Currently there are 5 independent background threads, each with
their own lifecycle and work queues:

- Zone Load Thread — single-shot lambda, created/destroyed per zone
- Deferred Work Thread — single-shot lambda, manual mode only
- BSP Preload Thread — single-shot lambda, created/destroyed per zone
- Entity Prep Worker — long-lived, own condition variable + dispatch model
- Item Icon Loader — long-lived, own condition variable + request/result queues

None of these share a work queue or respect a unified priority ordering. Each makes
independent decisions about what to work on next.

**ISSUE 1.2 — No configurable thread counts.**
Thread count is hardcoded. The design calls for configurable simulation and background
thread counts per hardware tier.

**ISSUE 1.3 — No thread priority management.**
Only the BSP Preload Thread sets thread priority (`SCHED_IDLE`/`nice(19)`). All other
threads run at default priority. The design specifies a priority hierarchy
(Render > GPU Upload > Simulation > Background).

**ISSUE 1.4 — RDP peer thread accumulation.**
`RDPServer::peerThreads_` pushes new threads for each connection but only joins them at
`stop()`. Disconnected peers leave completed-but-unjoined `std::thread` objects in the
vector. Not a resource leak (OS reclaims thread resources), but the vector grows unbounded.

**ISSUE 1.5 — Maximum concurrent thread count.**
A GLES2+RDP+audio session can have up to ~13 threads. On the Orange Pi's quad-core
Cortex-A7 this means significant context switching overhead.

---

## 2. Work Scheduling

### Scheduling Mechanisms Inventory

| Mechanism | Location | Queue Type | Priority | Thread-Safe |
|-----------|----------|-----------|----------|-------------|
| EntityPrepWorker | `entity_prep_worker.h:113` | `std::deque<PrepRequest>` | Sort by raceId for cache locality | Yes (mutex) |
| GPU Upload Thread | `gpu_upload_thread.h:146` | `std::vector<UploadRequest>` | FIFO | Yes (mutex) |
| SimulationWorker | `simulation_worker.h:804` | Double-buffered I/O structs | Internal tier priority | Yes (mutex+cv) |
| Item Icon Loader | `item_icon_loader.h:151` | `std::deque<SheetRequest>` | FIFO (sorted by sheet) | Yes (mutex) |
| Background Zone Load | `irrlicht_renderer.h:1368` | State machine (enum) | Sequential phases | Atomic flags |
| Frame Budget Governor | `frame_budget_governor.h` | State machine (Green/Yellow/Red) | Weight classes | Main thread only |
| Mesh Load Queue | `irrlicht_renderer.h:1346` | `std::vector<MeshLoadEntry>` | Distance-sorted | Main thread only |
| Renderer Event Queue | `irrlicht_renderer.h:501` | `std::vector<RendererEvent>` | FIFO | Main thread only |
| Entity Pending Updates | `entity_renderer.h:617` | `std::unordered_map` | Dedup by spawnId | Main thread only |
| Async Packet Queue | `daybreak_connection.h:365` | `std::vector<PendingAsyncPacket>` | FIFO | Yes (mutex) |
| Console Input Queues | `console_input_handler.h:94` | 7x `std::queue` by type | FIFO per type | Yes (mutex) |
| Chat Message Buffer | `chat_message_buffer.h:101` | `std::deque<ChatMessage>` | FIFO | Yes (mutex) |

### Issues Found

**ISSUE 2.1 — No unified priority queue.**
The design calls for a single priority ordering across all threads. Currently, each
subsystem has its own queue with its own (or no) priority logic:

- Entity Prep Worker: sorted by raceId for cache locality (not distance)
- GPU Upload Thread: strict FIFO (no priority)
- Mesh Load Queue: distance-sorted (closest to design intent)
- Item Icon Loader: sorted by sheet grouping (not distance)
- Background Zone Load: fixed sequential phases (not priority-based)

Only the mesh load queue respects the distance-from-player priority axis.

**ISSUE 2.2 — GPU Upload Thread has no priority ordering.**
Upload requests are processed FIFO. A depth=5 texture uploaded before a depth=1 texture
will be processed first. The design requires all threads to respect distance priority.

**ISSUE 2.3 — Entity Prep Worker sorts by race, not by distance.**
`sortPendingByModel()` groups by `(raceId << 8 | gender)` for S3D cache locality. This
is a reasonable optimization but conflicts with the design's distance-first priority. A
distant entity of the same race as a nearby one will be prepped before a nearby entity of
a different race.

**ISSUE 2.4 — SimulationWorker has its own internal priority tiers.**
The worker defines Critical/Normal/Background tiers (Background runs every 3 frames).
This is independent of the unified priority system and works well for its purpose, but
should be documented as a sanctioned exception.

**ISSUE 2.5 — No re-prioritization on player movement.**
When the player moves to a new PVS region, the mesh load queue is re-populated by the
SimulationWorker, but the Entity Prep Worker's pending queue is not re-sorted. An entity
that was depth=1 when queued may now be depth=5, but it retains its position in the queue.

---

## 3. Budget Tracking

### Budget Systems Inventory

| System | Resource | Default (OrangePi) | Eviction | Thread-Safe |
|--------|----------|-------------------|----------|-------------|
| ConstrainedTextureCache | GPU texture memory | 64 MB | LRU, scene-graph scan | No (main only) |
| ConstrainedMeshCache | Region mesh memory | 24 MB | LRU, skip protected | No (main only) |
| FrameBudgetGovernor | Frame time | 30 FPS target | Green/Yellow/Red states | No (main only) |
| AudioManager sound cache | Decoded PCM buffers | 8 MB | LRU | Yes (mutex) |
| RaceModelLoader chr cache | Archive count | 4 entries | LRU by count | Yes (mutex) |
| ParticleBudget | Particle count | 100-500 by quality | Count cap | N/A (config) |
| BoidsBudget | Creature count | 30-80 by quality | Count cap | N/A (config) |
| Network data budget | Outgoing KB/s | Connection-defined | Rate limiting | No (single thread) |
| Entity visibility | Visible count | 40 | Distance-sorted cap | No (main only) |
| Polygon budget | Polys/frame | Config-defined | **Warning only** | No (main only) |

### Issues Found

**ISSUE 3.1 — ConstrainedMeshCache::onLoaded() has no budget guard.**
`onLoaded()` unconditionally adds `sizeBytes` to `currentUsage_`. The caller is expected
to have pre-evicted, but there is no assertion or check in `onLoaded()` itself. A buggy
caller could silently exceed the budget.

**ISSUE 3.2 — Equipment textures bypass the constrained texture cache.**
`equipment_model_loader.cpp:588` calls `driver_->addTexture()` directly. Equipment
textures are not tracked by the constrained texture cache, have no LRU eviction, and
are only indirectly visible via `gles2GetGpuTextureMemoryUsage()` in `/pmem`.

**ISSUE 3.3 — Polygon budget is advisory only.**
The polygon budget logs a warning after 60 frames of exceeding the limit but takes no
corrective action (no LOD reduction, no entity hiding, no clip distance adjustment).

**ISSUE 3.4 — RaceModelLoader uses count-based eviction, not byte-based.**
`maxChrCacheEntries_` limits the number of cached `_chr.s3d` archives (default 4), but
archives vary significantly in size (1-20MB). Memory stats are tracked in
`getMemoryStats()` but not used for eviction decisions. Four large archives could consume
80MB while four small ones use 4MB.

**ISSUE 3.5 — Several `driver_->addTexture()` calls bypass budget tracking.**
The following allocations are not tracked by any budget system:

| File | What | Risk |
|------|------|------|
| `animated_texture_manager.cpp:62,76` | Animated textures | Low — few, zone load only |
| `sky_renderer.cpp:341` | Sky textures | Low — 1-2 textures |
| `animated_tree_manager.cpp:512` | Tree textures | Low — static, created once |
| `spell_visual_fx.cpp:184,225` | Spell particle textures | Low — small |
| `storm_cloud_layer.cpp:49,434` | Storm cloud textures | Low — 1-2 textures |
| `detail_texture_atlas.cpp:161` | Detail atlas | Low — one texture |

Individually low risk, but collectively they represent untracked GPU memory that could
contribute to budget overruns on the Orange Pi.

**ISSUE 3.6 — Budget checks are not thread-safe for future multi-worker expansion.**
`ConstrainedTextureCache` and `ConstrainedMeshCache` have no mutexes. This is correct
for the current single-thread design, but the design document calls for configurable
background thread counts. If multiple workers can trigger cache operations, thread-safe
accounting will be needed.

---

## 4. Loading Code Paths

### State Machine Hierarchy

The loading system uses 5 independent state machines:

1. **LoadingPhase** (16 states) — top-level game connection + loading state
2. **BackgroundZoneLoadPhase** (~30+ states) — zone asset loading pipeline
3. **ManualLoadStep** (12 states) — maps `/load` commands to phase groups
4. **DeferredInitStep** (12 states) — post-load environment initialization
5. **EntityBuildPhase** (9 states) — per-entity multi-frame build pipeline

### Automatic Loading Flow

```
LoadingPhase::GRAPHICS_LOADING_ZONE
  -> LoadZoneGraphics()
    -> setupInstantScene() (HCMap terrain + placeholder cubes)
    -> registerEntity() x N (placeholder cubes)
    -> registerDoor() x N (placeholder boxes)
    -> OnGraphicsComplete() -> LoadingPhase::COMPLETE
  -> beginZoneAssetLoad()
    -> startBackgroundZoneLoad() [background thread]
      -> S3D parse, BSP, portals, atlas preload
    -> advanceBackgroundZoneLoad() [1 state per GREEN frame]
      -> 30+ phases, each doing one unit of work
    -> processFrameProgressiveLoad() [ongoing]
      -> entity prep, mesh builds, texture uploads
```

### Manual Loading Flow

```
/load <step>
  -> advanceManualLoadStep()
    -> sets pauseAtPhase_ target
    -> same advanceBackgroundZoneLoad() state machine
    -> pauses at target phase until next /load command
```

### Lazy Loading Flow (In-Game)

```
processFrameProgressiveLoad() [every frame, GREEN-gated]
  P1: Player's region mesh (bypasses GREEN)
  P2: Entity prep dispatch
  P3: Entity poll + build step
  P4: Texture-ready region rebuilds
  P5: PVS neighbor region meshes
  P6: Door rebuilds
  P7: Object rebuilds
  P8: Item icon loading

processFrameLazyLoad() [every frame, GREEN-gated]
  - 1 region mesh build from meshLoadQueue_ (distance-sorted)
  - texture rebuild queue processing
  - mesh cache over-budget eviction
```

### Issues Found

**ISSUE 4.1 — EntityRenderer creation duplicated in 3 locations.**
`EntityRenderer` is created with the same configuration in:

1. `loadGlobalAssets()` (~line 951)
2. `setupInstantScene()` (~line 2816)
3. `advanceBackgroundZoneLoad()` DataReady_EntityRenderer (~line 4034)

All three are guarded by `if (!entityRenderer_)` but duplicate the model loader config,
client path, archive index, old models flag, and ground finder callback setup.

**ISSUE 4.2 — Synchronous entity creation for post-load spawns.**
During zone load, entities use the multi-frame `EntityBuildPhase` pipeline (9 phases
spread across GREEN frames). But entities that spawn after loading (`OnSpawnAddedGraphics`
in `eq.cpp:19260`) call `buildEntityMesh()` synchronously — all 9 phases in one frame.
This can cause frame hitches for complex entities.

**ISSUE 4.3 — Model search order differs between main thread and background thread.**
`getMeshForRace()` (main thread): JSON S3D → archive index → zone chr → global chr → numbered globals.
`preloadModelData()` (background thread): zoneCharacters → otherChrCaches → JSON S3D from disk → global chr from disk → numbered globals from disk.

The archive index path is missing from `preloadModelData()`. The search order divergence
means different code paths may find the same model in different archives, potentially
causing subtle differences.

**ISSUE 4.4 — Equipment index has 3 initialization paths.**
1. Eager: `loadEquipmentArchives()` scans all gequip archives synchronously
2. Lazy: `ensureIndexLoaded()` scans on first use (double-checked locking)
3. Background: `buildEquipmentIndex()` off-thread, then `adoptIndex()` on main thread

**ISSUE 4.5 — GraphicsArchiveIndex has 2 initialization paths.**
1. Eager in `loadGlobalAssets()` on main thread
2. Background in `startBackgroundZoneLoad()`, adopted in `DataReady_ArchiveIndex`

**ISSUE 4.6 — Automatic and manual modes use different background threads.**
Automatic mode uses `zoneLoadThread_` (single-shot lambda). Manual mode triggers
`deferredWorkThread_` (separate single-shot lambda) for phases 6-10. Both do similar
work (archive index, sky, weather, atlas) but in different thread contexts. This is
the duplication identified in `arch_design.md` issue #1.

---

## 5. Placeholder Coverage

### Placeholder Fallback Table

| Asset Type | Placeholder | Created In | Replaced By | Permanent Fallback |
|------------|------------|-----------|------------|-------------------|
| Entity mesh | Con-colored cube (6*scale) | `EntityRenderer::registerEntity()` | `buildEntityMesh()` or `processOneEntityBuildStep()` | Yes — stays as cube if no model found |
| Door mesh | Name-hash colored box | `DoorManager::registerDoor()` | `rebuildSingleDoor()` | Yes — stays as box if no S3D mesh |
| Zone mesh | HCMap wireframe geometry | `setupInstantScene()` | S3D region meshes | Yes — HCMap remains if mesh budget full |
| Zone texture | 8x8 solid color | `ConstrainedTextureCache::getPlaceholderTexture()` | Real texture from cache | Yes — placeholder used on eviction |
| Zone mesh (no texture) | Vertex-colored mesh | `buildColoredMesh()` in `rebuildRegionMesh()` | N/A (final fallback) | Yes |
| Sky | No sky rendered | N/A | Sky textures loaded | Yes — sky simply absent |
| Lights | No light contribution | N/A | Zone light nodes created | Yes — scene unlit |
| Effects/particles | Not rendered | N/A | Particle system init | Yes — no particles |

### Issues Found

**ISSUE 5.1 — No placeholder for equipment textures.**
If an equipment texture fails to load, the equipment model renders with whatever texture
state Irrlicht defaults to (likely white or black). There is no explicit fallback texture
assigned to equipment materials.

**ISSUE 5.2 — Texture eviction scans the entire scene graph.**
`ConstrainedTextureCache::clearTextureReferences()` recursively scans all scene nodes to
replace an evicted texture pointer with the placeholder texture. This is O(N) in scene
graph size and happens synchronously on the render thread during eviction. With large
scenes this could cause frame hitches.

---

## 6. Dead Code

### Uncalled Functions

| Function | File | Severity |
|----------|------|----------|
| `EntityPrepWorker::isPending(raceId, gender)` | `entity_prep_worker.cpp:119` | Low |
| `EntityPrepWorker::getPendingCount()` | `entity_prep_worker.cpp:142` | Low |
| `EntityPrepWorker::cancelPrep(spawnId)` | `entity_prep_worker.cpp:94` | Medium — intended for despawn cleanup but never used |
| `GPUUploadThread::getCompletedCount()` | `gpu_upload_thread.cpp:159` | Low |
| `SimulationWorker::getFrontBuffer()` | `simulation_worker.cpp:246` | Low |
| `IrrlichtRenderer::isRegionPvsVisibleDebug()` | `irrlicht_renderer.cpp:1756` | Low |
| `S3DLoader::buildBoneTransforms()` | `s3d_loader.cpp:929` | Low — empty stub, explicitly deprecated |

### Unused Member Variables (Written but Never Read)

| Variable | File | Severity | Notes |
|----------|------|----------|-------|
| `zoneLightNames_` | `irrlicht_renderer.h:1447` | Medium | Populated during zone load, never queried |
| `previousActiveLights_` | `irrlicht_renderer.h:1452` | Medium | Only cleared, never compared |
| `lightDebugMarkers_` | `irrlicht_renderer.h:1450` | Low | Not written or read in any .cpp |
| `showLightDebugMarkers_` | `irrlicht_renderer.h:1451` | Low | Not written or read in any .cpp |
| `lastLightPlayerPos_` | `irrlicht_renderer.h:1433` | Medium | Assigned in ~8 places, never read — vestigial from old light update code moved to SimulationWorker |
| `portalCacheDirty_` | `irrlicht_renderer.h:1323` | Low | Set once, never checked — portal visibility moved to SimulationWorker |
| `EntityVisual::meshBuildQueued` | `entity_renderer.h:102` | Low | Declared, never set or checked |

### Superseded Code

The old `updateObjectVisibility()`, `updateZoneLightVisibility()`, `updateObjectLights()`,
`updateZoneLightColors()`, and `updateNameTagsWithLOS()` methods have been removed from
the implementation (moved to SimulationWorker) but left behind the vestigial member
variables listed above.

No large commented-out blocks, `#if 0` blocks, or "TODO: remove" markers were found.

---

## 7. Governor Compliance

### Compliance Summary

The render thread demonstrates **strong governor compliance**. All progressive/lazy loading
paths check governor state before doing work, and all enforce the 1-frame-per-GREEN rule
via `break` statements, `didWork` flags, or single state machine transitions.

### Compliant Patterns

- `processFrameLazyLoad()`: GREEN check at entry (line 5893), max 1 region per frame
- `processFrameProgressiveLoad()`: GREEN check at line 11786, `didWork` mutual exclusion
- `advanceBackgroundZoneLoad()`: GREEN-gated at line 9030, one state transition per call
- Entity prep dispatch: GREEN-gated at line 8270
- Icon loading: GREEN-gated at line 8307, one icon per call
- P1 Critical (player region): intentionally bypasses GREEN — documented exception

### Violations Found

| # | Rule | Location | Description | Severity |
|---|------|----------|-------------|----------|
| V1 | File I/O | `irrlicht_renderer.cpp` lines 4179, 4222 | `loadDisplaySettingsFromFile()` reads `config/display_settings.json` via `std::ifstream` on render thread | Low — small file, once per zone |
| V2 | File I/O | `irrlicht_renderer.cpp` lines 4086, 4092 | `loadGlobalCharacters()` / `loadNumberedGlobals()` open S3D archives on render thread | Low on GLES2 (skipped when `deferredAssetLoading=true`), Medium on desktop GL |
| V3 | File I/O | `irrlicht_renderer.cpp` line 9256 | `fopen("/proc/self/statm")` for memory stats | Negligible — procfs, no disk I/O |
| V4 | GPU alloc | `constrained_texture_cache.cpp` lines 129, 675; `zone_geometry.cpp:320` | `driver_->addTexture()` fallback when GPU upload thread unavailable | Low — only fires if gpuUploadThread is null on GLES2 |
| V5 | Blocking wait | `irrlicht_renderer.cpp` line 1275 | `eglClientWaitSyncKHR(..., EGL_FOREVER_KHR)` — unbounded GPU fence wait | Low — typically near-instant, but unbounded |
| V6 | Blocking wait | `irrlicht_renderer.cpp` lines 2231-2237 | `zoneLoadThread_->join()` during zone cleanup without completion guard | Low — threads should be finished, but no pre-check |

---

## 8. Summary of Issues

### Architecture-Level Issues (Require Refactoring)

| ID | Category | Description | Impact |
|----|----------|-------------|--------|
| 1.1 | Threads | 5 independent background threads instead of unified work queue | Prevents unified priority, complicates thread count configuration |
| 1.2 | Threads | No configurable thread counts | Can't scale to hardware tier |
| 2.1 | Scheduling | No unified priority queue across threads | Each subsystem makes independent scheduling decisions |
| 2.2 | Scheduling | GPU Upload Thread is FIFO, no distance priority | May upload far assets before near ones |
| 2.3 | Scheduling | Entity Prep sorts by race, not distance | May prep distant entities before nearby ones |
| 2.5 | Scheduling | No re-prioritization of Entity Prep queue on movement | Stale priorities after player moves |
| 4.1 | Loading | EntityRenderer creation duplicated in 3 locations | Maintenance risk, potential inconsistency |
| 4.6 | Loading | Automatic/manual modes use different background threads | Duplication, inconsistent behavior |

### Budget/Safety Issues (Should Fix)

| ID | Category | Description | Impact |
|----|----------|-------------|--------|
| 3.1 | Budget | ConstrainedMeshCache::onLoaded() has no budget guard | Silent budget overrun possible |
| 3.2 | Budget | Equipment textures bypass constrained texture cache | Untracked GPU memory |
| 3.4 | Budget | RaceModelLoader uses count-based eviction, not byte-based | Memory usage unpredictable |
| 3.5 | Budget | Multiple addTexture() calls bypass budget tracking | Cumulative untracked GPU memory |
| 3.6 | Budget | Budget checks not thread-safe for future multi-worker | Blocks planned expansion |
| 4.2 | Loading | Post-load spawns build synchronously in 1 frame | Frame hitches on complex entities |
| 4.3 | Loading | Model search order differs between main/background thread | Potential inconsistency |
| 5.2 | Placeholders | Texture eviction scans entire scene graph | O(N) per eviction on render thread |

### Low-Priority Cleanup

| ID | Category | Description |
|----|----------|-------------|
| 1.3 | Threads | No thread priority management (only BSP preload sets priority) |
| 1.4 | Threads | RDP peer thread accumulation |
| 3.3 | Budget | Polygon budget is advisory only |
| 5.1 | Placeholders | No placeholder for equipment textures |
| 6.x | Dead code | 7 uncalled functions, 7 unused member variables |
| 7.V1-V6 | Governor | 6 minor governor violations (file I/O, fallback GPU alloc, fence wait) |
| 4.4 | Loading | Equipment index has 3 initialization paths |
| 4.5 | Loading | GraphicsArchiveIndex has 2 initialization paths |

### Refactoring Priority Order

Based on the design document goals, the recommended refactoring order is:

1. **Unify background threads into a shared work queue** (issues 1.1, 1.2, 2.1)
   — This is the foundational change. Entity Prep Worker, Item Icon Loader, Zone Load,
   Deferred Work, and BSP Preload should all submit work items to a shared priority
   queue consumed by configurable worker threads.

2. **Implement unified priority system** (issues 2.1, 2.2, 2.3, 2.5)
   — Distance-from-player primary sort, asset-type secondary sort. All work queues
   (including GPU Upload) should respect this ordering. Re-prioritize on player movement.

3. **Unify loading code paths** (issues 4.1, 4.6, 4.4, 4.5)
   — Merge automatic/manual modes into one code path with a mode flag. Deduplicate
   EntityRenderer creation. Consolidate equipment/archive index initialization.

4. **Fix budget gaps** (issues 3.1, 3.2, 3.4, 3.5)
   — Add budget guard to mesh cache onLoaded(). Route equipment textures through
   constrained cache. Consider byte-based chr cache eviction. Audit untracked
   addTexture() calls.

5. **Address post-load entity creation** (issue 4.2)
   — Route post-load entity spawns through the same multi-frame EntityBuildPhase pipeline
   used during zone loading, instead of synchronous buildEntityMesh().

6. **Remove dead code** (section 6)
   — Clean sweep of unused functions and member variables.

7. **Fix minor governor violations** (section 7)
   — Move file I/O off render thread, add timeout to EGL fence wait, add completion
   guard to zone cleanup join.
