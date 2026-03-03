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
| C06 | Thread safety of budget accounting (no mutex on caches) | Old Issue 3.6 — unchanged, blocks future multi-worker expansion |
| C07 | DDS decode duplication (ConstrainedTextureCache + TextureDecoder) | NEW |
| C08 | One loadDisplaySettingsFromFile() call at line 3454 missed by B04 | Residual from B04 |

### Low

| ID | Description | Source |
|----|-------------|--------|
| C09 | Dead code in zone_lines.cpp: 3 #if 0 blocks + hasBspZoneLines() + debugTestCoordinateMappings() | NEW — not in original audit's dead code list |
| C10 | Configurable thread counts not implemented | Old Issue 1.2 — unchanged |
| C11 | RDP peer thread accumulation | Old Issue 1.4 — unchanged |
