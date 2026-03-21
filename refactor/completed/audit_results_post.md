# WillEQ Architecture Audit Results (Post-Refactoring)

Audit performed against `arch_design.md` and `arch_design_update.md` as the reference design.
Covers: thread inventory, work scheduling, budget tracking, loading code paths, placeholder
fallbacks, dead code, and governor compliance.

Cross-referenced against `audit_results.md` (pre-refactoring findings) and `refactor/progress.md`
(A01-A10, B01-B04 refactoring work). All refactoring commits verified in git history.

---

## 1. Thread Inventory

### 1.1 Sanctioned Threads

| Thread | File | Line | Role |
|--------|------|------|------|
| Game Update | `src/client/eq.cpp` | 15421 | Network/simulation loop (per zone-in) |
| Simulation Worker | `src/client/graphics/simulation_worker.cpp` | 47 | Visibility, lights, particles, animation |
| GPU Upload | `src/client/graphics/gpu_upload_thread.cpp` | 109 | Texture/VBO upload (GLES2 only, shared EGL ctx) |
| Console Input | `src/client/input/console_input_handler.cpp` | 24 | Stdin reading (headless mode) |
| RDP Listener | `src/client/graphics/rdp/rdp_server.cpp` | 319 | RDP connection acceptance |
| RDP Peer(s) | `src/client/graphics/rdp/rdp_server.cpp` | 752 | Per-client RDP session |
| RDP Audio Pump | `src/client/audio/audio_backend.cpp` | 98 | Audio delivery to RDP clients |
| Miniaudio Device | `third_party/miniaudio/miniaudio.h` | ~17674 | Hardware audio I/O (third-party pthread) |

### 1.2 Background Thread Architecture

A01-A04 migrated all 5 ad-hoc background threads (zone load, deferred work, BSP preload,
entity prep, icon loader) from raw `std::thread`/lambdas to the common
`BackgroundWorkQueue<Request, Result>` template (`include/client/graphics/background_work_queue.h`).
This standardized lifecycle management, submit/poll APIs, and synchronization patterns.

**UPDATE (C02 resolved):** All 5 `BackgroundWorkQueue` instances are now backed by a shared
`BackgroundThreadPool` (`background_thread_pool.h/.cpp`). `BackgroundWorkQueue` gained a
pool-backed constructor that submits work to the shared pool with priority ordering via
`WorkPriorityKey`. Thread count is configurable per hardware tier via
`ConstrainedRendererConfig::backgroundThreadCount` (OrangePi=1, TNT=2).

Priority assignments: zone load=0, BSP preload=0, deferred work=1,
entity prep=WorkPriorityKey(pvsDepth, EntityMesh), icon sheets=WorkPriorityKey::makeNonSpatial(Icon).

**Status vs old audit:** Issue 1.1 (ad-hoc threads) **RESOLVED**. Issue 1.2 (configurable
thread counts) **RESOLVED**.

### 1.3 Thread Lifecycle

All threads are properly joined before destruction. No leaked threads found.

**Minor issue:** RDP peer threads accumulate in `peerThreads_` vector
(`rdp_server.h:217`) and are only joined at `RDPServer::stop()`. Disconnected clients leave
completed threads in the vector until server shutdown.

---

## 2. Work Scheduling & Priority System

### 2.1 Unified Priority System — Partially Implemented

A05 introduced `WorkPriorityKey` in `include/client/graphics/work_priority.h` — a packed
32-bit key encoding PVS depth (bits 31-24), asset type (bits 23-20), and distance tiebreaker
(bits 19-0).

**Where it IS applied (added by A05):**
- `GPUUploadThread` — requests carry a `priority` field; `reprioritize()` re-sorts on PVS change
- `EntityPrepWorker` — `sortPendingByPriority()` sorts by PVS depth; `updateDepths()`
  re-prioritizes on player movement
- `SimulationWorker` — outputs `regionPvsDepth` map and `meshLoadQueue` sorted by distance

**Where it is NOT applied:**
- `BackgroundWorkQueue` instances (zone load, deferred work, BSP preload) — strict FIFO
- `SimulationWorker` internal tiers — own Critical/Normal/Background scheme (acceptable —
  these are frame-frequency compute tasks, not asset loads)
- Mesh load queue — sorted by distance only, not by the full `WorkPriorityKey`
- Texture rebuild queue — sorted by distance only

**Status vs old audit:** Issues 2.2 (GPU upload FIFO), 2.3 (entity prep race-only sort),
and 2.5 (no re-prioritization on movement) are **fixed**. Issue 2.1 (no unified priority
queue) is **partially fixed** — key subsystems use `WorkPriorityKey`, but
`BackgroundWorkQueue` instances remain FIFO.

### 2.2 Frame Budget Governor — Well Implemented

`FrameBudgetGovernor` (`include/client/graphics/frame_budget_governor.h`) is solid:

- **States:** Green (avg < 80% target), Yellow (80-100%), Red (>100%)
- **Task weights:** Critical (always), Heavy (Green only, >=5ms), Light (Green/Yellow, >=2ms),
  Trivial (Green/Yellow, >=0.5ms)
- **Rolling average:** 30-frame ring buffer
- **Hysteresis:** 10 consecutive good frames to upgrade; immediate downgrade on violation
- **Stall watchdog:** Resets after 90 non-Green frames (~3s at 30fps) with pending work

Governor is checked at 5+ locations in `processFrame()` before dispatching work.

### 2.3 Background Zone Load State Machine

`BackgroundZoneLoadPhase` (`irrlicht_renderer.h:223-270`) is a comprehensive 20+ phase
state machine. A08 unified automatic and manual modes into shared `preloadDeferredAssets()`
for phases 6-10.

---

## 3. Budget Tracking

### 3.1 Subsystems With Budget Tracking

| Subsystem | Budget Source | Tracking Variable | LRU Eviction | Thread-Safe |
|-----------|-------------|-------------------|--------------|-------------|
| ConstrainedTextureCache | `config_.textureMemoryBytes` | `currentUsage_` | Yes (std::list) | **NO** |
| ConstrainedMeshCache | Constructor `budgetBytes` | `currentUsage_` | Yes (std::list) | **NO** |
| SfxManager (Audio) | `setCacheMaxBytes()` | `cacheSizeBytes_` | Yes | N/A (single thread) |
| AudioManager | `soundBufferCacheMaxBytes_` | `soundBufferCacheBytes_` | Yes | N/A (single thread) |
| FrameBudgetGovernor | Target FPS | Rolling 30-frame avg | N/A | N/A (single thread) |

### 3.2 Hardware Tier Presets

`ConstrainedRendererConfig` (`include/client/graphics/constrained_renderer_config.h`)
defines 4 presets:

| Preset | FB Memory | Texture Memory | Total RAM | Max Tex Size |
|--------|-----------|----------------|-----------|-------------|
| Voodoo1 | 2 MB | 2 MB | 32 MB | 64px |
| Voodoo2 | 4 MB | 8 MB | 64 MB | 128px |
| TNT | 8 MB | 16 MB | 128 MB | 512px |
| OrangePi | 10 MB | 64 MB | 128 MB | 512px |

Derived limits: `meshMemoryBytes = totalBudget / 5`, `soundBufferCacheBytes = min(8MB, totalBudget / 16)`.

### 3.3 ConstrainedMeshCache Budget Guard — Fixed

B02 (`e24bd3a`) added a pre-eviction call at the `rebuildRegionMesh()` call site
(`irrlicht_renderer.cpp:5600`): `evictUntilAvailable(meshSize, protectedRegions_)` runs
immediately before `onLoaded()` at line 5614. The `onLoaded()` method itself still does
an unconditional add + warn as a safety net, but the caller ensures space is available
before calling it.

**Status vs old audit:** Issue 3.1 is **fixed**.

### 3.4 Equipment/Entity Textures — Partially Fixed

B03 (`feb3c90`) routed entity and equipment textures through `ConstrainedTextureCache`.
`uploadDecodedTexture()` now passes `constrainedTextureCache_` and calls
`registerTexture()` after GPU upload. This is a significant improvement — before B03 these
textures were completely untracked.

**Remaining gap:** The allocate-then-register pattern persists — `driver_->addTexture()`
at line 893 happens before `cache->registerTexture()` at line 901. If cache registration
fails due to budget exhaustion, the GPU texture exists but is untracked. The improvement is
that textures ARE registered on the success path; the risk is the failure path.

**Status vs old audit:** Issue 3.2 is **mostly fixed** (textures registered on success).
Order-of-operations risk on failure path remains.

### 3.5 Remaining Unbudgeted Texture Allocations

The following `driver_->addTexture()` calls still bypass budget tracking (unchanged from
old audit Issue 3.5):

| Location | File | Context | Risk |
|----------|------|---------|------|
| Sky renderer | `sky_renderer.cpp:341` | No cache integration | Low — 1-2 textures |
| Animated textures | `animated_texture_manager.cpp:62,76` | Local cache, no budget | Low — few, zone load only |
| Animated trees | `animated_tree_manager.cpp:512` | Local cache, no budget | Low — static, created once |
| Zone geometry fallback | `zone_geometry.cpp:320` | When `constrainedCache_` is NULL | Low — fallback path only |
| Software cursor | `irrlicht_renderer.cpp:1407` | One-time, no check | Negligible |

Individually low risk, but collectively represent untracked GPU memory on constrained devices.

### 3.6 Thread Safety of Budget Accounting

Neither `ConstrainedTextureCache` nor `ConstrainedMeshCache` has mutex or atomic protection.
Currently safe (single-thread access). Would need protection if multi-worker background
threads are ever implemented.

### 3.7 GPU Memory — Not Explicitly Tracked

No global GPU memory budget. Texture cache estimates CPU-side sizes. VBO/index buffer memory
not tracked. Framebuffer budget used only for resolution clamping.

---

## 4. Loading Code Paths

### 4.1 Zone Entry Sequence

```
1. ZoneProcessPlayerProfile() -> m_zone_* properties
2. ZoneProcessZoneSpawns() -> populates m_entities
3. ZoneProcessClientUpdate() -> OnGameStateComplete()
   +-- LoadZoneGraphics() (eq.cpp:19141-19252)
      +-- setupInstantScene()
      |   +-- createEntityRenderer()        [A09: single creation point]
      |   +-- createDoorManager()
      |   +-- buildZonePlaceholder() (HCMap collision)
      |   +-- setupHCMapCollision()
      |   +-- startBspPreload() [background thread]
      +-- Loop m_entities -> registerEntity() [metadata + placeholder cube]
      +-- Loop m_doors -> registerDoor() [metadata only, deferred mesh]
      +-- OnGraphicsComplete() -> SetLoadingPhase(COMPLETE)

4. Each render frame (COMPLETE phase):
   +-- processFrameProgressiveLoad()
       +-- promotePreparedModels()
       +-- Build player region [critical]
       +-- Poll entity prep results -> GL upload
       +-- Build one entity mesh     [A10: multi-frame pipeline]
       +-- Build one PVS neighbor region
       +-- Build one door mesh
       +-- Build one deferred object mesh
```

### 4.2 Shared vs Duplicated Code Paths

**Shared (good):**
- Entity registration: Both pre-load and lazy-load call identical `registerEntity()`
- Region mesh building: All paths use `rebuildRegionMesh()`
- Entity mesh building: All paths use `processOneEntityBuildStep()` (A10 unified this)
- Door mesh building: All paths use `buildDoorMesh()`
- Auto/manual loading: Phases 6-10 unified via `preloadDeferredAssets()` (A08)

**Minor duplication:**
- Door registration: `registerDoor()` (pre-load) vs `createDoor()` (in-game) — nearly
  identical but separate methods in DoorManager.

**Status vs old audit:** Issues 4.1 (EntityRenderer duplication) **fixed** by A09.
Issue 4.2 (synchronous post-load entity creation) **fixed** by A10. Issue 4.6
(auto/manual different threads) **fixed** by A08.

### 4.3 DDS Texture Decode Duplication

Two separate DDS decoders still exist:
1. `ConstrainedTextureCache` (`constrained_texture_cache.cpp:505`)
2. `TextureDecoder` (header-only)

Both implement identical DXT1/DXT3/DXT5 decoding logic.

### 4.4 File I/O on Render Thread

B04 (`5d77e4b`) cached display settings at startup and moved `loadGlobalCharacters`/
`loadNumberedGlobals`/`loadEquipmentModels` off the render thread. However, one call site
was missed:

- `irrlicht_renderer.cpp:3454`: `loadDisplaySettingsFromFile()` still called directly during
  `EnvironmentInit` phase instead of using the cached `getDisplaySettings()`.

Procfs memory stats (`fopen("/proc/self/statm")`) remain — acceptable, documented exception.

**Status vs old audit:** Issues V1 (display settings) **partially fixed** — one call site
remains. V2 (S3D loads on render thread) **fixed**. V3 (procfs) **unchanged** (acceptable).

---

## 5. Placeholder & Fallback Coverage

### 5.1 Coverage Summary

| Asset Type | Placeholder | Fallback Mechanism | Null-Safe |
|------------|-------------|-------------------|-----------|
| Entity Mesh | Con-colored cube | `createPlaceholderMesh()` (entity_renderer.cpp:548) | Yes |
| Entity Texture | Async retry | Returns nullptr, retries next frame | Yes |
| Zone Mesh | HCMap collision mesh | `buildZonePlaceholder()` (irrlicht_renderer.cpp:2465) | Yes |
| Zone Texture | 8x8 magenta checkerboard | LRU eviction replaces with placeholder | Yes |
| Door Model | Deferred/skipped | Skip if model missing, null check | Yes |
| Light | Omitted | null check after `addLightSceneNode()` | Yes |
| Spell Effects | White 32x32 circle | `createFallbackTexture()`, multi-level chain | Yes |
| Sound | Silent | Returns -1 (invalid handle), caller checks | Yes |

### 5.2 Assessment

**No dangerous code paths found.** All asset accesses are guarded by null checks. No
blocking waits for assets on the render thread. The entity build pipeline
(`EntityBuildPhase` enum with 9 phases) provides a well-structured progressive build
with placeholder cubes visible at every intermediate state.

---

## 6. Dead Code

### 6.1 Previously Identified Dead Code — Removed

B01 (`ebca7d7`) removed all dead code from the original audit:
- 7 uncalled functions (cancelPrep, isPending, getPendingCount, getCompletedCount,
  getFrontBuffer, isRegionPvsVisibleDebug, buildBoneTransforms)
- 7 unused member variables (zoneLightNames_, previousActiveLights_, lastLightPlayerPos_,
  lightDebugMarkers_, showLightDebugMarkers_, portalCacheDirty_, meshBuildQueued)

Verified: none of these appear in the current codebase.

### 6.2 Newly Identified Dead Code

Items not caught by the original audit:

**Disabled code blocks (`#if 0`) in `src/client/zone_lines.cpp`:**

| Lines | Description |
|-------|-------------|
| 104-134 | BSP tree loading (disabled, coordinate issues) |
| 529-597 | BSP zone line detection with 8 coordinate variants |
| 716-765 | BSP-based bounding box computation |

**Dead functions in `src/client/zone_lines.cpp`:**

| Function | Line | Status |
|----------|------|--------|
| `ZoneLines::hasBspZoneLines()` | 499 | Always returns false |
| `ZoneLines::debugTestCoordinateMappings()` | 361 | Called only from disabled debug block (eq.cpp:14059) |

### 6.3 Static Debug Counters

20+ `static int` throttled-logging counters across major files. Benign debug
instrumentation, not logic bugs.

---

## 7. Governor Compliance

### 7.1 Governor GREEN Enforcement Points

The governor is checked at 5+ locations in `processFrame()`:

- Entity prep heavy ops: `governor_->getState() == BudgetState::Green`
- Lazy icon loading: Same check
- Deferred init (post-load): Same check
- Placeholder collision rebuild: Same check
- Progressive/lazy mesh loading: `governor_->canStartTask(weight)`

### 7.2 Remaining Violations

**~~VIOLATION 1 (CRITICAL):~~ FALSE POSITIVE — `advanceDeferredInit()` during zone load is governor-gated**

- Location: `irrlicht_renderer.cpp:4937` (within `advanceBackgroundZoneLoad()`)
- Phase: `BackgroundZoneLoadPhase::EnvironmentInit`
- **Resolution:** This is a false positive. The call at line 4937 IS governor-gated by the
  outer check in `processFrame()` (lines 8790-8792): since `EnvironmentInit != Loading`,
  the outer condition requires `governor_->getState() == BudgetState::Green` before
  `advanceBackgroundZoneLoad()` is entered. The post-load path at line 8811 has its own
  inner GREEN gate because it is called from a different `processFrame()` location outside
  the outer gate. Both paths are GREEN-gated — the gate is at different levels in the call stack.

**VIOLATION 2 (HIGH): `glGenTextures` on render thread as fallback path**

- Location: `texture_atlas.cpp:187` (`uploadPreloadedPage()`) and `texture_atlas.cpp:357`
  (`load()`)
- Issue: When GPU upload thread is unavailable, the fallback path calls `glGenTextures` +
  `glCompressedTexImage2D` directly on the render thread. The preferred async path
  (`uploadPreloadedPageAsync()`) correctly uses the GPU upload thread.
- Context: Called from `advanceBackgroundZoneLoad()` lines 4181-4187. Desktop GL always
  takes the direct path.

**VIOLATION 3 (MODERATE): One remaining `loadDisplaySettingsFromFile()` call site**

- Location: `irrlicht_renderer.cpp:3454`
- Issue: B04 cached settings at startup (line 485) but this call site still reads from disk.
- Mitigation: Runs once per zone load during `EnvironmentInit`.

**VIOLATION 4 (LOW): Procfs I/O during render**

- Location: `irrlicht_renderer.cpp:9015-9029`
- `fopen("/proc/self/statm")` — kernel memory-mapped (~1us), only during `/pmem` diagnostics.
- Acceptable — documented governor exception.

### 7.3 Violations Fixed by B04

The following violations from the original audit are now resolved:
- V2: `loadGlobalCharacters()` / `loadNumberedGlobals()` S3D opens on render thread — **fixed**,
  moved to background thread with adoption via DataReady phases.

---

## 8. Cross-Cutting Issues

### 8.1 Background Threads — Common Abstraction, Not Unified Pool

A01-A04 migrated all 5 background subsystems to the common `BackgroundWorkQueue` template.
This is a real improvement in code quality and maintainability — standardized lifecycle,
submit/poll, synchronization. But the design's goal of a single thread pool consuming from
a unified priority queue is not achieved. Each subsystem still has its own thread.

The practical impact: global priority ordering cannot be enforced across subsystems (e.g.,
entity prep cannot yield to a higher-priority zone load request). Configurable thread counts
per hardware tier remain unimplemented.

### 8.2 Automatic vs Manual Loading — Well Unified

A08 merged phases 6-10 into shared `preloadDeferredAssets()`. Both modes use
`processFrameProgressiveLoad()` for asset building. `BackgroundZoneLoadPhase` state machine
is shared. Minor remaining duplication in door registration only.

---

## 9. Summary of Findings by Severity

### Critical

*(None — C01 was a false positive; see section 7.2 for details.)*

### High

2. ~~**No unified background thread pool**~~ **RESOLVED (C02)** — All 5 queues now backed by
   shared `BackgroundThreadPool` with priority ordering and configurable thread count per tier.
3. **`glGenTextures` on render thread** — atlas upload fallback path does GPU allocation
   directly instead of through GPU upload thread
4. **Entity/equipment texture allocate-then-register** — GPU memory allocated before budget
   check. B03 improved (textures now registered on success path) but failure path risk remains.

### Medium

5. **Remaining unbudgeted `addTexture()` calls** — sky, animated textures, trees still bypass
   constrained cache (old Issue 3.5, not addressed)
6. **Thread safety of budget accounting** — no mutex protection on caches (safe today,
   blocks future multi-worker expansion)
7. **DDS decode duplication** — identical DXT logic in `ConstrainedTextureCache` and
   `TextureDecoder`
8. **One missed `loadDisplaySettingsFromFile()` call** at line 3454 (B04 incomplete)

### Low

9. **Dead code in `zone_lines.cpp`** — 3 `#if 0` blocks (~130 lines) + 2 dead functions
   (`hasBspZoneLines`, `debugTestCoordinateMappings`)
10. **Procfs I/O during render** — acceptable, documented exception
11. **RDP peer thread accumulation** — completed threads not cleaned until shutdown
12. ~~**Configurable thread counts**~~ **RESOLVED (C02)** — `backgroundThreadCount` per preset

### Strengths

- Placeholder fallback coverage is **complete** for all asset types
- GPU upload thread architecture is **correctly designed** with mutex protection, EGL fence
  sync, and non-blocking render thread integration
- Frame budget governor is **well implemented** with hysteresis and stall watchdog
- `WorkPriorityKey` is **correctly designed and wired** into GPU upload and entity prep
- Progressive entity build pipeline (`EntityBuildPhase`, 9 phases) is **well structured**
  and used by both pre-load and lazy-load paths (A10)
- Zone entry loading path is **clean** — metadata registration and placeholder cubes only
- Automatic/manual loading modes are **well unified** (A08)
- EntityRenderer creation is **deduplicated** (A09)
- Dead code from original audit is **fully removed** (B01)
- Mesh cache pre-eviction guard is **in place** (B02)
- Equipment/entity textures are **routed through constrained cache** on success path (B03)
- S3D and equipment loading **moved off render thread** (B04)
- All threads are **properly joined** on shutdown
