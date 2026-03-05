# Batch L -- Loading Thread Separation

## Goal

Move all zone loading work (progress bar rendering + asset building) onto a
dedicated loading thread so the main thread is never blocked during zone load.
The main thread continues processing network packets throughout. No additional
threads are spawned by the loading process -- the loading thread does all GL
and asset work sequentially.

This is a prerequisite for the full game-state/renderer decoupling (Batch D).
It establishes the pattern of network and GL operating on separate threads and
introduces guards on renderer calls from the network path.

## Problem Statement

Today, the main loop in `Application::mainLoop()` runs sequentially:

```
while running:
    processNetworkEvents()    // <-- blocked when GL is busy
    updateGameState()
    render()                  // <-- calls processFrame() which runs zone loading
```

Zone loading happens inside `processFrame()` via `advanceBackgroundZoneLoad()`.
While the loading state machine advances (S3D parse, atlas upload, mesh building,
etc.), the main thread is occupied. Network packets are only processed when the
renderer explicitly calls `networkTickCallback_()` between heavy loading phases
(3 call sites in irrlicht_renderer.cpp: lines 776, 4752, 4766).

This causes:
- **Packet loss risk**: Server sends 40+ packets during zone load; manual network
  pumping is fragile and insufficient for long loading phases.
- **Debugging difficulty**: Loading is a sequential process but is interleaved with
  network ticks, background thread pools, frame budget governors, and poll-based
  completion detection -- making the actual execution order hard to reason about.
- **Tight coupling**: 343 `m_renderer->` call sites in eq.cpp mean network handlers
  directly mutate renderer state, which is unsafe when GL is owned by another thread.

## Design

### Thread Ownership

```
Main Thread (always running):
    - Network event loop (processNetworkEvents)
    - Game state updates (entity tracking, movement, combat, chat)
    - Never touches GL/Irrlicht objects during loading

Loading Thread (temporary, exists only during zone load):
    - Owns GL context (eglMakeCurrent / glXMakeCurrent)
    - Renders loading screen (progress bar + text)
    - Executes zone asset loading sequentially (the 13 ZoneLoadStep phases)
    - Joins main thread when loading completes
    - Main thread reacquires GL context and runs normal render loop
```

### Data Flow

```
                    Loading Thread                     Main Thread
                    ==============                     ===========

Phase 1 (0-45%):   Render progress bar    <---read--- {phase, percent, text}
                    (pure display loop)                Game state loading
                                                       (network packets)

Handoff (45%):      Game state signals                 Stops mutating fields
                    "ready for graphics"               that loading will read

Phase 2 (45-100%): Read game state         <---read--- Entity list, door list,
                    Build zone meshes                   zone info, positions
                    Build texture atlases               (main thread continues
                    Create entity models                 processing network but
                    Setup sky, lighting                  defers renderer calls)
                    ...sequential steps...

Complete:           Release GL context     --signal-->  Acquire GL context
                    Thread joins                        Resume render loop
```

### Lifecycle (Initial Load and Re-zone)

The same flow applies to initial login and zone-to-zone transitions:

1. **Main thread** detects zone transition (login complete, or zone change approved)
2. **Main thread** releases GL context (`eglMakeCurrent(EGL_NO_CONTEXT)` / `glXMakeCurrent(None)`)
3. **Main thread** spawns loading thread, passing it the GL context handle
4. **Loading thread** acquires GL context, shows loading screen
5. **Loading thread** enters passive display loop (renders progress bar from shared state)
6. **Main thread** continues processing network, updating game state, writing to
   shared `LoadingStatus` struct (atomic phase/percent, mutex-protected text string)
7. When game state reaches 45% (`ZONE_AWAITING_CONFIRM`), main thread sets a
   `graphicsLoadReady` flag
8. **Loading thread** sees the flag, transitions to active mode
9. **Loading thread** reads game state (entities, doors, zone info) and executes
   the 13 ZoneLoadStep phases sequentially -- no background threads, no frame
   budget governor, no poll loops
10. **Loading thread** finishes, releases GL context, sets `loadingComplete` flag
11. **Main thread** joins loading thread, acquires GL context, resumes render loop

### Shared State: LoadingStatus

```cpp
struct LoadingStatus {
    std::atomic<int> phase{0};
    std::atomic<int> percent{0};
    std::atomic<bool> graphicsLoadReady{false};  // main -> loading: "go"
    std::atomic<bool> loadingComplete{false};     // loading -> main: "done"
    std::atomic<bool> quitRequested{false};       // main -> loading: "abort"

    std::mutex textMutex;
    std::string text;  // protected by textMutex
};
```

### Renderer Call Guards

During loading, the main thread must not call any renderer methods that touch GL
or Irrlicht objects. The 343 `m_renderer->` call sites in eq.cpp fall into categories:

| Category | Count | During Loading | Action |
|----------|-------|----------------|--------|
| UI windows (inventory, chat, vendor, etc.) | ~145 | Windows don't exist | Skip (guard) |
| Entity management (register, update, remove) | ~28 | Entities are dynamic | Queue for post-join |
| Player state (position, appearance) | ~21 | Position updates continue | Write to game state only |
| Animation/combat | ~17 | Not visible | Skip (guard) |
| Environment (weather, particles) | ~14 | Not initialized yet | Skip (guard) |
| Loading/progress | ~11 | Handled via LoadingStatus | Replace with atomic writes |
| Collision/navigation | ~7 | Set once at handoff | Already done before handoff |
| Doors | ~5 | Registered at handoff | Queue state changes |
| Callbacks/setup | ~11 | One-time setup | Done before/after loading |
| Debug/camera | ~8 | Not active | Skip (guard) |

**Implementation approach**: A single `isLoading()` check at the top of renderer
entry points. Most calls become no-ops during loading. Entity spawns/despawns that
arrive during loading are tracked in game state (m_entities) and will be picked up
by the loading thread or reconciled after the join.

The loading thread reads `m_entities` and `m_doors` at the start of its active phase.
New entities that arrive after that point are handled post-join by the existing
`OnSpawnAddedGraphics()` path, which already handles late spawns during gameplay.

### Sequential Loading (No Background Threads)

The current loading uses `BackgroundThreadPool` for CPU work (S3D parse, BSP
computation, atlas building, sky loading, equipment index) with a Submit/Poll/Install
pattern. The loading thread replaces this with direct sequential execution:

| Current (main thread + pool) | New (loading thread only) |
|------------------------------|--------------------------|
| Submit S3D parse to pool | Parse S3D directly |
| Poll for completion | (already done) |
| Install results | Install results |
| Submit BSP compute to pool | Compute BSP directly |
| Poll for completion | (already done) |
| ... | ... |

This eliminates:
- `BackgroundWorkQueue` submit/poll/install state machine
- `PhaseState` (Ready/Working/Done) tracking
- Frame budget governor gating (GREEN/YELLOW/RED)
- `networkTickCallback_` (main thread handles network independently)

The `BackgroundThreadPool` continues to exist for runtime use (entity prep for
late spawns, texture decode, etc.) but is not used during zone loading.

### GL Context Transfer

**X11/GLX**:
```cpp
// Main thread releases
glXMakeCurrent(display, None, nullptr);

// Loading thread acquires
glXMakeCurrent(display, drawable, glContext);

// Loading thread releases
glXMakeCurrent(display, None, nullptr);

// Main thread reacquires
glXMakeCurrent(display, drawable, glContext);
```

**DRM/EGL (Orange Pi)**:
```cpp
// Main thread releases
eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

// Loading thread acquires
eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);

// Same pattern for release/reacquire
```

The GL context handle, display, and surface are stored in the Irrlicht device and
accessible to both threads. Only one thread holds the context at a time.

### Input During Loading

The loading thread checks `quitRequested` (set by main thread) between loading
steps. The main thread can detect quit input via:

- **X11**: `XPending()` / `XNextEvent()` on the main thread (X11 is thread-safe
  for event reading when the display connection is not shared, or with `XInitThreads()`)
- **DRM**: Direct evdev read from `/dev/input/event*` (already implemented in
  `CIrrDeviceFB.cpp`)
- **Simple approach**: Main thread reads raw keyboard input (ESC key) outside of
  Irrlicht and sets the atomic flag

---

## Current Loading Flow (Reference)

### Application Main Loop

`Application::mainLoop()` (`src/client/application.cpp:382`):
```
while running:
    processNetworkEvents()     // eq.cpp: TickNetwork()
    updateGameState()          // movement, combat, spells
    render()                   // calls eq.cpp: UpdateGraphics() -> processFrame()
```

### Loading Phase Progression

`LoadingPhase` enum (`include/client/eq.h:519-543`):

| Phase | Value | Progress | Owner |
|-------|-------|----------|-------|
| DISCONNECTED | 0 | 0% | -- |
| LOGIN_CONNECTING | 1 | 2% | Network |
| LOGIN_AUTHENTICATING | 2 | 5% | Network |
| WORLD_CONNECTING | 3 | 10% | Network |
| WORLD_CHARACTER_SELECT | 4 | 15% | Network |
| ZONE_CONNECTING | 5 | 20% | Network |
| ZONE_RECEIVING_PROFILE | 6 | 25% | Network |
| ZONE_RECEIVING_SPAWNS | 7 | 30% | Network |
| ZONE_REQUEST_PHASE | 8 | 35% | Network |
| ZONE_PLAYER_READY | 9 | 40% | Network |
| ZONE_AWAITING_CONFIRM | 10 | 45% | Network |
| GRAPHICS_LOADING_ZONE | 11 | 50% | Graphics |
| GRAPHICS_LOADING_MODELS | 12 | 65% | Graphics |
| GRAPHICS_CREATING_ENTITIES | 13 | 80% | Graphics |
| GRAPHICS_FINALIZING | 14 | 95% | Graphics |
| COMPLETE | 15 | 100% | -- |

**Handoff point**: `ZONE_AWAITING_CONFIRM` (45%) -> `GRAPHICS_LOADING_ZONE` (50%)

Triggered by `OnGameStateComplete()` (`eq.cpp:1103`) which calls `LoadZoneGraphics()`
(`eq.cpp:19160`). This registers entities/doors with the renderer and starts
`beginZoneAssetLoad()`.

### Zone Asset Loading State Machine

`ZoneLoadPhase` enum (`include/client/graphics/irrlicht_renderer.h:241-325`):

13 steps, each with sub-phases (Submit/Poll/Install pattern):

1. **P01_S3d** -- Parse zone .s3d archive (background thread)
2. **P02_Bsp** -- Compute BSP tree, region BBs, portals, light regions
3. **P03_Atlas** -- Build and upload texture atlases (one page per frame)
4. **P04_Regions** -- Create zone region meshes (VBO upload)
5. **P05_Assets** -- Build indexes, create entity renderer, load equipment
6. **P06_Objects** -- Install zone objects (placeables)
7. **P07_Doors** -- Create door meshes
8. **P08_Entities** -- Create EntityPrepWorker, build character meshes
9. **P09_Collision** -- Setup zone collision
10. **P10_Sky** -- Create sky dome, upload textures, configure fog/weather
11. **P11_Env** -- Trees, detail, particles, boids, simulation worker
12. **P12_Lights** -- Zone lighting
13. **P13_Cleanup** -- Release temporary data, finalize

Called from `advanceBackgroundZoneLoad()` (`irrlicht_renderer.cpp:3819`), which
is called from `processFrame()` every frame when loading is active.

### Network Tick Callback Sites

`networkTickCallback_` is called at 3 points during loading:
- `irrlicht_renderer.cpp:776` -- During `loadGlobalAssets()`
- `irrlicht_renderer.cpp:4752` -- During P05_Assets_Equipment
- `irrlicht_renderer.cpp:4766` -- During P05_Assets_GlobalAssets

These manual pumps are eliminated by the new design.

---

## Implementation Plan

### Phase L1 -- Loading Thread Infrastructure (L01-L02)

Build the loading thread scaffolding without changing existing behavior.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| L01 | LoadingStatus struct, LoadingThread class, GL context transfer | pending | |
| L02 | Renderer `isLoading()` guard on all public entry points | pending | |

#### L01: LoadingThread Class

Create `include/client/graphics/loading_thread.h` and
`src/client/graphics/loading_thread.cpp`.

`LoadingThread` is a simple wrapper around `std::thread` that:

1. Takes a reference to the Irrlicht device (for GL context), renderer config,
   and a shared `LoadingStatus` struct
2. On `start()`: spawns a thread that acquires GL, enters the loading loop
3. Passive loop: renders loading screen from `LoadingStatus` at ~30fps
4. Active loop: when `graphicsLoadReady` is set, executes zone loading steps
5. On completion: releases GL, sets `loadingComplete`, thread function returns
6. `join()`: called by main thread to join + reacquire GL

The loading screen rendering is extracted from `IrrlichtRenderer::drawLoadingScreen()`
into a standalone function that only needs the driver, font, and screen dimensions.
No dependency on the full renderer state.

GL context transfer requires access to the underlying display/context handles.
For X11, these are available via `irr::video::COpenGLDriver::getExposedData()`.
For DRM/EGL, they're stored in `CIrrDeviceFB`. Add accessor methods if needed.

Acceptance criteria:
- `LoadingThread` compiles and links
- Can start/join without zone loading (just renders progress bar)
- GL context correctly transfers between threads (verified by draw call on each)
- No existing behavior changed

#### L02: Renderer Entry Point Guards

Add an `std::atomic<bool> loading_` flag to `IrrlichtRenderer`. When true, all
public methods called from the network/game thread become no-ops or queue their
effects.

Approach:
1. Identify all public `IrrlichtRenderer` methods called from `eq.cpp`
2. Add `if (loading_.load()) return;` (or appropriate default) at the top of each
3. Methods that write to game-state-only data (no GL) can remain active
4. Methods that the loading thread calls internally are NOT guarded (they check
   `loading_` only when called externally via the public API)

Special cases:
- `setLoadingProgress()` -- replaced by `LoadingStatus` atomic writes
- `registerEntity()` / `removeEntity()` -- entities arriving during loading are
  tracked in `m_entities` (game state); reconciled post-join
- `setDoorState()` -- door state changes queued or deferred

Acceptance criteria:
- All 343 call sites in eq.cpp are safe to call during loading (no crash, no GL)
- Loading flag can be set/cleared from main thread
- No behavioral change when flag is false (default)

### Phase L2 -- Sequential Zone Loading (L03-L04)

Convert the loading state machine from async poll-based to sequential.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| L03 | Sequential zone loading function (replaces advanceBackgroundZoneLoad) | pending | |
| L04 | Loading screen progress updates from sequential loader | pending | |

#### L03: Sequential Zone Loading

Create a new method `IrrlichtRenderer::loadZoneSequential()` that executes all
13 ZoneLoadStep phases in order, synchronously, on the calling thread (the loading
thread).

For each current phase:
- **Submit**: Execute the work inline instead of submitting to BackgroundThreadPool
- **Poll**: Eliminated (work already complete)
- **Install**: Execute inline (GL calls are valid -- loading thread owns GL)

The method signature:
```cpp
void loadZoneSequential(const std::string& eqClientPath,
                        LoadingStatus& status,
                        /* read-only game state references */);
```

It updates `status.percent` and `status.text` as it progresses through steps,
and checks `status.quitRequested` between steps for early abort.

The existing `advanceBackgroundZoneLoad()` and its sub-phase state machine remain
for now (dual-path). A flag controls which path is used. This allows incremental
testing.

Acceptance criteria:
- Zone loads correctly using sequential path on loading thread
- All 13 steps execute in order
- No BackgroundThreadPool work items submitted during sequential load
- Progress bar updates smoothly from 45% to 100%
- Can abort via quit flag between steps

#### L04: Progress Bar Updates

The loading thread renders the loading screen between each major step and at
sub-step boundaries (e.g., per-region mesh build, per-atlas-page upload).

Add progress callbacks to long-running steps:
- Atlas upload: progress per page
- Region mesh build: progress per region (or batch)
- Entity prep: progress per entity
- Door rebuild: progress per door

These callbacks update `LoadingStatus` and call the loading screen draw function.
Target: progress bar updates at least every 100ms.

Acceptance criteria:
- Progress bar updates visually during all long-running steps
- No stalls >200ms without progress update on Orange Pi
- Percentage monotonically increases from 45% to 100%

### Phase L3 -- Integration (L05-L06)

Wire the loading thread into the application lifecycle.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| L05 | Initial zone load via loading thread | pending | |
| L06 | Re-zone via loading thread | pending | |

#### L05: Initial Zone Load

Modify `Application::mainLoop()` / `EverQuest::OnGameStateComplete()` flow:

1. When `OnGameStateComplete()` fires (game state reaches 45%):
   - Instead of calling `LoadZoneGraphics()` which calls renderer methods directly,
     write entity/door/zone data into a shared struct or set the `graphicsLoadReady` flag
   - The loading thread (already running and displaying progress bar) picks up the
     signal and begins active loading
2. Main loop continues `processNetworkEvents()` without calling `render()` while
   loading thread is active
3. When loading thread sets `loadingComplete`:
   - Main thread joins loading thread
   - Main thread reacquires GL context
   - Main thread calls `OnGraphicsComplete()`
   - Normal render loop resumes

The `LoadZoneGraphics()` function's responsibilities are split:
- **Game state prep** (entity registration data, door data, zone info): stays on
  main thread, written to shared state before signaling `graphicsLoadReady`
- **Renderer calls** (registerEntity, registerDoor, setupInstantScene, camera setup):
  moved to loading thread's active phase

Acceptance criteria:
- Login -> zone load -> gameplay works end-to-end
- Network never stalls during loading
- No packet loss during zone load (verified by zone load success rate)
- Loading screen displays throughout
- `networkTickCallback_` is not called (verify by removing the 3 call sites)

#### L06: Re-zone

Modify the zone transition flow:

1. When zone change is approved:
   - Main thread calls `unloadZone()` on renderer (still owns GL at this point)
   - Main thread releases GL context
   - Main thread spawns loading thread (or re-uses from a pool/re-creates)
   - Loading thread acquires GL, shows loading screen
   - Flow continues as in L05

Handle edge cases:
- Zone change while previous zone is still loading (abort + restart)
- Network disconnect during loading (abort loading thread, reconnect flow)
- Server-initiated zone change (same as client-initiated, triggered by packet)

Acceptance criteria:
- Zone-to-zone transitions work (verified by zoning between 3+ zones)
- No GL context leaks (context always owned by exactly one thread)
- Abort during loading works cleanly (no zombie threads, no GL state corruption)
- Memory cleanup between zones is correct (unloadZone + new load)

### Phase L4 -- Cleanup (L07-L08)

Remove the old loading path and clean up.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| L07 | Remove old loading state machine (advanceBackgroundZoneLoad) | pending | |
| L08 | Remove networkTickCallback_, update docs | pending | |

#### L07: Remove Old Loading Path

Once the loading thread path is verified:
- Remove `advanceBackgroundZoneLoad()` and all ZoneLoadPhase sub-phases
- Remove `PhaseState` enum and tracking
- Remove frame budget governor involvement in loading
- Remove `zoneLoadQueue_` (BackgroundWorkQueue for zone loading)
- Simplify `processFrame()` to only handle the render loop (no loading branches)

Acceptance criteria:
- `processFrame()` no longer contains zone loading logic
- ZoneLoadPhase reduced to Idle/Loading/Complete (or removed entirely)
- All loading happens in `loadZoneSequential()` on loading thread
- Build compiles, all tests pass

#### L08: Remove networkTickCallback_

- Remove `networkTickCallback_` member from IrrlichtRenderer
- Remove `setNetworkTickCallback()` method
- Remove the 3 call sites in irrlicht_renderer.cpp
- Remove the callback setup in eq.cpp `InitGraphics()`
- Update CLAUDE.md Architecture section to document loading thread design

Acceptance criteria:
- No references to `networkTickCallback_` anywhere in codebase
- Zone loading works without manual network pumping
- CLAUDE.md updated with new loading architecture

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| GL context transfer fails on specific driver | Test on both X11 (desktop) and DRM/EGL (Orange Pi); fall back to old path if transfer fails |
| Race condition on shared game state during active loading | Loading thread reads entity/door lists once at handoff; late arrivals handled post-join via existing `OnSpawnAddedGraphics()` path |
| Irrlicht internal state assumes single thread | All Irrlicht calls from loading thread only; main thread does zero Irrlicht calls during loading; context transfer is clean |
| Loading takes longer without thread pool parallelism | S3D parse and BSP compute are the only CPU-heavy steps; these are I/O-bound (disk read) and compute-bound respectively. Sequential execution may be slower but is deterministic and debuggable. Can re-add parallelism later if needed. |
| Entity count changes between handoff and active load | Loading thread takes a snapshot of entity/door data; delta reconciled post-join. Same as current behavior for late spawns. |
| Deadlock from mutex on LoadingStatus text | Text mutex is only held briefly for string copy; no nested locks; loading thread reads, main thread writes |

---

## Dependencies

- No new external libraries
- Uses `std::thread`, `std::atomic`, `std::mutex` (already in codebase)
- Requires GL context transfer APIs (platform-specific, already available via Irrlicht device)
- Builds on Batch A infrastructure (BackgroundThreadPool remains for runtime use)

## Estimated Scope

- ~500 lines for LoadingThread class (L01)
- ~200 lines for renderer guards (L02)
- ~800 lines for sequential loading function (L03, extracted from existing state machine)
- ~100 lines for progress callbacks (L04)
- ~300 lines for integration (L05, L06)
- Net reduction: ~1000+ lines removed from state machine, poll loops, callbacks (L07, L08)
