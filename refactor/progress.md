# Refactoring Progress

Master tracker for the willeq architecture refactoring effort.
Reference: `arch_design_update.md` (design), `audit_results.md` (findings).
Post-refactoring audit: `audit_results_post.md`.

## Process

Each unit follows: **Plan -> Implement -> Review -> Commit**

1. Write plan with numbered steps and acceptance criteria to unit file
2. Implement, re-reading the plan file during work
3. Review: re-read plan, check each step, note deviations in review section
4. Commit only after review passes
5. Update this table

## Batch A — Architecture (Sequential)

| Unit | Description | Status | Commit | Post-Audit Verdict |
|------|-------------|--------|--------|--------------------|
| A01 | Create BackgroundWorkQueue abstraction | done | `2d3345f` | Verified — template in use by all 5 subsystems |
| A02 | Migrate EntityPrepWorker to BackgroundWorkQueue | done | `e41e900` | Verified |
| A03 | Migrate ItemIconLoader to BackgroundWorkQueue | done | `faf6873` | Verified |
| A04 | Migrate zone load / deferred work / BSP preload to BackgroundWorkQueue | done | `eb89d48` | Verified — but still 5 separate instances, not unified pool |
| A05 | Unified priority system (PVS depth + asset type + GPU upload + entity prep + re-prioritization) | done | `ab5a030` | Verified — WorkPriorityKey wired into GPU upload + entity prep; BackgroundWorkQueue instances still FIFO |
| A06 | *(merged into A05)* | done | `ab5a030` | — |
| A07 | *(merged into A05)* | done | `ab5a030` | — |
| A08 | Unify automatic/manual loading code paths | done | `1b04ba3` | Verified — phases 6-10 shared via preloadDeferredAssets() |
| A09 | Deduplicate EntityRenderer creation | done | `49870a9` | Verified — single createEntityRenderer() call site |
| A10 | Route post-load entity spawns through multi-frame pipeline | done | `0df4ecf` | Verified — both pre-load and lazy-load use EntityBuildPhase pipeline |

## Batch B — Budget/Safety/Cleanup (Sequential)

| Unit | Description | Status | Commit | Post-Audit Verdict |
|------|-------------|--------|--------|--------------------|
| B01 | Remove dead code (uncalled functions + unused members) | done | `ebca7d7` | Verified — all 7 functions and 7 variables removed. New dead code found in zone_lines.cpp (see below) |
| B02 | Add budget guard to ConstrainedMeshCache::onLoaded() | done | `e24bd3a` | Verified — evictUntilAvailable() called at call site before onLoaded() |
| B03 | Route equipment textures through constrained texture cache | done | `feb3c90` | Partially verified — textures registered on success path, but allocate-then-register order-of-ops risk remains |
| B04 | Fix minor governor violations (file I/O off render thread) | done | `5d77e4b` | Partially verified — S3D loads moved off render thread, but one loadDisplaySettingsFromFile() call site missed at line 3454 |

## Remaining Issues (from post-refactoring audit)

Issues discovered by the post-refactoring audit that were not covered by the original
refactoring plan, or were incompletely addressed.

### Critical

*(None — C01 was a false positive. The call at line 4937 is inside `advanceBackgroundZoneLoad()`, which is already behind the outer GREEN gate at `processFrame()` lines 8790-8792. The post-load path at line 8811 has its own inner gate because it's called from a different location. Both paths are governor-gated.)*

### High

| ID | Description | Source |
|----|-------------|--------|
| C02 | ~~5 BackgroundWorkQueue instances still separate threads~~ **DONE**: Unified into shared BackgroundThreadPool | `5154357` |
| C03 | ~~`glGenTextures` on render thread in TextureAtlas fallback~~ **DONE**: Removed sync fallback from `uploadPreloadedPageAsync()`, simplified GLES2 calling code in renderer, removed 2 dead `load()` methods (~235 lines) | `c822a8f` |
| C04 | ~~Entity/equipment texture allocate-then-register pattern — GPU alloc before budget check~~ **DONE**: Unified zone + entity texture uploads into single decodedQueue_ with budget-check-first via processUploadQueue(). Removed uploadDecodedTexture(), submitAsyncEntityTexture(), setGPUUploadThread() from EntityRenderer. Eliminated all #ifdef EQT_HAS_GLES2 from entity texture upload code. | `89c0709` |

### Medium

| ID | Description | Source |
|----|-------------|--------|
| C05 | ~~Unbudgeted addTexture() calls: sky, animated textures, trees, storm clouds, detail atlas~~ **DONE**: Added TextureEvictionListener interface to ConstrainedTextureCache. All 5 subsystems (SkyRenderer, AnimatedTextureManager, AnimatedTreeManager, StormCloudLayer, DetailManager) now register textures into the shared LRU cache and handle eviction callbacks. ~4-10 MB previously untracked textures are now evictable under memory pressure. | `b444583` |
| C06 | ~~Thread safety of budget accounting (no mutex on caches)~~ **DONE**: Added `std::mutex` to both `ConstrainedTextureCache` and `ConstrainedMeshCache`. All public methods acquire `mutex_` via `lock_guard`. Lock ordering invariant: `decodedQueueMutex_` (queue only) is never held simultaneously with `mutex_` (all other state). Internal `touchInternal()` and `getPlaceholderTextureInternal()` avoid recursive locking. 16 inline methods in texture cache and 9 in mesh cache moved out-of-line. `getOrLoad()` releases `mutex_` before sync decode path to prevent lock-order inversion with `processUploadQueue()`. | `a8b7001` |
| C07 | ~~DDS decode duplication (ConstrainedTextureCache + DDSDecoder)~~ **DONE**: Inlined `DDSDecoder::decode()` at call site in `processTextureData()`, removed trivial `decodeDDS()` wrapper method (declaration + implementation). | `7100bc9` |
| C08 | ~~One loadDisplaySettingsFromFile() call at line 3484 missed by B04~~ **DONE**: Removed dead code block in `preloadDeferredAssets()` that populated `computations->displaySettings` (never read). Removed `DisplaySettingsData` struct and member from `PendingZoneComputations`. | `3a64788` |

### Low

| ID | Description | Source |
|----|-------------|--------|
| C09 | ~~Dead code in zone_lines.cpp: 3 #if 0 blocks + hasBspZoneLines() + debugTestCoordinateMappings()~~ **DONE**: Removed 3 `#if 0` blocks (~130 lines), 2 dead functions (`hasBspZoneLines`, `debugTestCoordinateMappings`), `resolveZoneLine` helper, unused members (`bspTree_`, zone bounds, `wldZonePoints_`), `wld_loader.h` include, and debug call in eq.cpp. 267 lines deleted. | `1b1bc53` |
| C10 | ~~Configurable thread counts not implemented~~ **DONE**: Added `--threads N` CLI flag to override per-preset `backgroundThreadCount` at launch time. Follows preset → JSON → CLI override chain. | `147115f` |
| C11 | ~~RDP peer thread accumulation~~ **DONE**: Added `PeerThreadEntry` struct with `std::atomic<bool> finished` flag. `addPeerThread()` sweeps and joins completed threads before adding new ones via `cleanupFinishedPeerThreads()`. `peerThreadImpl()` sets `finished` flag at exit. `peerThreads_` changed from `vector<thread>` to `vector<unique_ptr<PeerThreadEntry>>`. | `938e947` |

## Batch L — Loading Thread Separation (prerequisite for Batch D)

Move zone loading onto a dedicated thread so the main thread is never blocked
during zone load. Full plan: `refactor/L_loading_thread.md`.

### Phase L1 — Infrastructure

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| L01 | LoadingStatus struct, LoadingThread class, GL context transfer | done | `58f0791` |
| L02 | Renderer `isLoading()` guard on all public entry points | done | `58f0791` |

### Phase L2 — Sequential Zone Loading

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| L03 | Sequential zone loading function (replaces advanceBackgroundZoneLoad) | done | `58f0791` |
| L04 | Loading screen progress updates from sequential loader | done | `58f0791` |

### Phase L3 — Integration

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| L05 | Initial zone load via loading thread | done | `58f0791` |
| L06 | Re-zone via loading thread | done | `58f0791` |

### Phase L4 — Cleanup

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| L07 | Remove old loading state machine (advanceBackgroundZoneLoad) | done | `58f0791` |
| L08 | Remove networkTickCallback_, update docs | done | `58f0791` |

---

## Batch S — Startup Validation & Pre-allocation (prerequisite for Batch D)

Enforce strict resource control at application startup. All settings validated,
all buffers pre-allocated, fail-fast on any error. Full plan: `refactor/S_startup_validation.md`.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| S01 | Mandatory config validation (credentials, eqClientPath, preset) | done | `58f0791` |
| S02 | Preset value validation (framebuffer budget, all field bounds) | done | `58f0791` |
| S03 | Global file validation (race_models.json, item_models.json, spell DB) | done | `58f0791` |
| S04 | Pre-allocate game state managers (inventory, buff, spell DB, commands) | done | `58f0791` |
| S05 | Pre-allocate renderer-lifetime subsystems (entity renderer, sky, sim, etc.) | done | `58f0791` |
| S06 | Door state separation (DoorStateManager game-state class) | done | `58f0791` |
| S07 | File I/O failure enforcement (FATAL on missing gameplay files) | done | `58f0791` |

---

## Batch D — Game State Threading (after Batch S)

Decouple game state from rendering into separate threads via event/intent bridge.
Full plan: `refactor/D_game_state_threading.md`.

### Phase 1 — Event Infrastructure

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D01 | Extend existing EventBus with missing event types + consolidate door state | done | `ed6ae49` |
| D02 | Define renderer intent types (reconcile with ActionDispatcher) | done | `4cc784d` |
| D03 | Create GameStateBridge interface and IrrlichtBridge skeleton | done | `e398f9d` |

### Phase 2 — Dual-Path Event Publishing

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D04 | Publish entity events alongside existing calls | done | `393a617` |
| D05 | Publish chat, combat, and player stat events | done | `a5d3418` |
| D06a | Publish inventory + currency events | done | `ee0d95d` |
| D06b | Publish loot + vendor events | done | `bcef15a` |
| D06c | Publish bank + trade events | done | `f953f89` |
| D07 | Publish door, group, pet, spell, skill events | done | `99c89ee` |
| D08 | Publish world/environment + zone lifecycle events | pending | |

### Phase 3 — Bridge Consumes Events

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D09 | IrrlichtBridge handles entity events | pending | |
| D10 | IrrlichtBridge handles chat, combat, player stat events | pending | |
| D11a | IrrlichtBridge handles inventory + currency events | pending | |
| D11b | IrrlichtBridge handles loot + vendor events | pending | |
| D11c | IrrlichtBridge handles bank + trade events | pending | |
| D12 | IrrlichtBridge handles door, group, pet, spell, skill events | pending | |
| D13 | IrrlichtBridge handles world/environment + zone lifecycle events | pending | |

### Phase 4 — Intent Handling

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D14 | Movement intent: renderer posts PlayerPositionChanged, game thread consumes | pending | |
| D15 | Interaction intents: target, door, loot, vendor, banker, trainer, world object, zoning, chat, read item | pending | |
| D16 | UI intents: spell, buff, skill, loot actions, vendor buy/sell, bank, trade, pet, group, camp/quit, hotbar | pending | |

### Phase 5 — Remove Direct Coupling

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D17 | Remove direct renderer calls from entity packet handlers | pending | |
| D18a | Remove direct renderer calls from inventory/loot/vendor packet handlers | pending | |
| D18b | Remove direct renderer calls from bank/trade/trainer packet handlers | pending | |
| D19 | Remove direct renderer calls from remaining handlers | pending | |
| D20a | Remove callback lambdas from InitGraphics() | pending | |
| D20b | Remove raw pointer coupling from InitGraphics() | pending | |
| D20c | Remove m_renderer from EverQuest | pending | |
| D20d | Remove debug/toggle slash commands from EverQuest | pending | |

### Phase 6 — Thread Separation

*D23 (Remove networkTickCallback_) deleted — already completed by Batch L (`58f0791`).*

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D21 | Move EverQuest tick loop to dedicated game thread | pending | |
| D22 | Synchronize bridge queues with proper threading | pending | |

### Phase 7 — Cleanup and Optimization

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D24 | Optimize high-frequency events (entity position batching) | pending | |
| D25 | Add console bridge implementation | pending | |
| D26 | Update documentation and CLAUDE.md | pending | |
