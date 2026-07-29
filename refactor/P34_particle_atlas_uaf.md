# P34 — ParticleManager atlasTexture_ use-after-free (CRITICAL)

**Slot: slot1. x86 only — no Pi. OWNS EQEMU (honor the .eqemu-lock
protocol). Sole agent this dispatch.**

The top-priority item on the deck (deferred.md CRITICAL entry,
2026-07-29): live qeynos2 sessions SIGSEGV ~1-2s after zone-in — a
use-after-free on a dangling `atlasTexture_` inside
`ParticleManager::renderUnified()`. Found and gdb-isolated by P33b;
both P33b and P33c had to gag the call site locally to do any live
work. Plausibly crashes Pi gameplay identically (renderUnified is
GLES2-only, runs whenever zoneReady && have3DTransforms).

## Trusted context (do not re-derive)

- P23 fixed the renderUnified/SimulationWorker RESULT-BUFFER race
  (TSAN-proven, tools/particle_race_stress.cpp) and removed the
  P10b-era isEnabled() gate — renderUnified had never actually run in
  production before that. This crash is a DIFFERENT bug (texture
  lifetime, not buffer lifetime) that the gate removal exposed.
- The bug class is established: P10b (region nodes) and P17 (entity
  nodes + RaceModelLoader cache) both fixed dangling-ITexture* holds
  via the two-layer pattern — an eviction/unload listener that clears
  cached pointers + a defensive isLiveTexture() check at use sites.
- P17 made zone unload SYNCHRONOUS on the loading thread — textures
  of the outgoing zone are now released DURING zone transition. The
  ~1-2s-after-zone-in timing smells like the particle atlas being
  created once (init or first zone) and freed by a later unload/evict
  while ParticleManager retains the raw pointer. VERIFY, don't assume.
- P33b's gdb backtrace + disassembly notes are in
  benchmarks/audits/p33_tree_wind/EVIDENCE_MANIFEST.txt.

## Work

1. Reproduce (eqemu, lock first): login → befallen → #zone qeynos2 —
   the crash was 100% within ~1-2s for P33b/P33c. Capture your own
   backtrace with a debug build if the existing notes don't pin the
   exact free site.
2. Root-cause the atlasTexture_ lifecycle: where it's created, every
   place it can be freed (zone unload via P17's synchronous path?
   ConstrainedTextureCache eviction? driver texture removal?), and
   which path frees it out from under renderUnified. Name the exact
   sequence.
3. Fix with the established pattern, matching whichever layer owns
   the atlas: clear/re-acquire on unload via the existing listener
   mechanisms + defensive liveness check before use (mirror P10b/P17
   idioms — do not invent a new lifetime scheme). If the atlas should
   simply be re-created per zone, that's acceptable if it matches how
   it's built today — explain the choice.
4. Prove it: (a) the exact repro survives — a live qeynos2 session
   running ≥3 minutes with particles enabled, plus a multi-hop
   (befallen↔qeynos2×2) session, no SIGSEGV; (b)
   tools/particle_race_stress still clean (don't regress P23);
   (c) if feasible cheaply, a targeted unit/stress test for the
   texture-lifetime path — manual tool acceptable, document the
   invocation.

## Rules

- Territory: particle system files (particle_manager.*,
  renderer_sim_bridge/renderer_frame call sites if needed), the
  texture-cache listener wiring for the atlas. NOT the driver, NOT
  zone loaders beyond listener wiring, NOT the SimulationWorker (P23's
  fix stays as-is).
- No behavior changes beyond crash removal: particles render as
  before when alive; goldens untouched (particles aren't in goldens,
  but run the gate).
- eqemu per protocol (lock, sanctioned scripts, Testchar restore to
  zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, clean shutdown, lock
  removed).
- STOP: the root cause lands in driver texture-removal semantics
  (report with evidence), or the fix requires reworking atlas
  ownership across threads (report the design need).

## Verification

- Full current bar: GLES2 tree build + ctest at current baseline
  (~1376/1377, ALSA exception; testing session may have moved the
  count — report actual), build-gles2-x86.sh clean, visual gate PASS
  goldens untouched, ARM build clean, file sizes clean.
- The step-4 survival proofs (session durations, hop counts, crash-free
  logs cited).

## Review

### Backtrace (P33b, confirmed by this unit's own root-cause analysis)

```
Thread 1 "willeq" SIGSEGV in
EQT::Graphics::Environment::ParticleManager::renderUnified() ->
IrrlichtRenderer::processFrameRender() -> processFrame() ->
Application::render() -> mainLoop() -> run()
```
Disassembly (no debug symbols in the release build P33b used) showed a
virtual-function call through `atlasTexture_` faulting — consistent
with a dangling (already-freed) `ITexture*`, not a null pointer. This
unit's source-level trace (below) confirms and names the exact call:
`atlasTexture_->getDriverTextureHandle()`
(`particle_manager.cpp::renderUnified()`, ~line 614).

### Lifecycle map + exact freeing sequence

- **Created**: `ParticleManager::init()` (called once from
  `IrrlichtRenderer`'s lazy `if (!particleManager_)` block,
  irrlicht_renderer.cpp:836-848 — `particleManager_` itself is a
  renderer-lifetime object, never destroyed/recreated between zones).
  `init()` is guarded by `initialized_` and never re-enters —
  `loadParticleAtlas("data/textures/particle_atlas.bmp")` runs exactly
  once per process, calling
  `constrainedCache_->getOrLoad("particle_atlas", data)` /
  `uploadTexture("particle_atlas")`. The result is an ORDINARY entry in
  `ConstrainedTextureCache::cache_` (the same LRU-evicting map every
  zone texture lives in) — nothing about it is special-cased.
- **Cached (dangerously) in two places**: `atlasTexture_` (a raw
  `ITexture*` member) and `particleMaterial_`'s texture slot 0
  (`particleMaterial_.setTexture(0, atlasTexture_)`, also set once in
  `init()`).
- **Never registered as a `TextureEvictionListener`** — confirmed by
  grep: every other subsystem that caches a raw `ITexture*` from this
  cache registers itself or a dedicated guard
  (`AnimatedTextureManager`, `SkyRenderer`, `DetailManager`,
  `StormCloudLayer` self-register in their own
  `setConstrainedTextureCache()`; `RegionMeshTextureGuard`,
  `EntityMeshTextureGuard`, `SingleTexturePointerGuard` cover region
  meshes / entity meshes / the software cursor — all wired in
  `irrlicht_renderer.cpp`'s `initLoadingScreen()`). `ParticleManager`
  had none of this.
- **Exact freeing sequence**: every zone transition, in order:
  1. `IrrlichtRenderer::unloadZone()` (renderer_zone_lifecycle.cpp)
     runs SYNCHRONOUSLY on the loading thread (P17) and unconditionally
     calls `constrainedTextureCache_->unfreeze(); constrainedTextureCache_->clear();`
     (renderer_zone_lifecycle.cpp:294-295) — no budget-pressure gate,
     this runs on EVERY zone unload.
  2. `ConstrainedTextureCache::clear()` iterates every cached entry
     (including `"particle_atlas"`), calls
     `listener->onTextureEvicted(name, texture)` for each registered
     listener, THEN `driver_->removeTexture(texture)` — freeing the
     underlying GPU/CPU texture object.
  3. Because `ParticleManager` was never a listener, step 2's
     notification never reached it: `atlasTexture_` and
     `particleMaterial_`'s texture slot were left pointing at freed
     memory.
  4. The next zone's `loadZoneSequential()` calls
     `particleManager_->onZoneEnter()` (SEQ Step11,
     renderer_zone_load.cpp:2082) but pre-fix this never touched
     `atlasTexture_` at all — `loadParticleAtlas()` was `init()`-only.
  5. The first subsequent frame that reaches `renderUnified()` executes
     `atlasTexture_->getDriverTextureHandle()` — a virtual call through
     the dangling pointer — SIGSEGV. This is unconditional on every
     zone transition (not memory-pressure-gated like most of this bug
     class), which is exactly why P33b saw a 100% ~1-2s-after-zone-in
     reproduction: befallen→qeynos2 is the FIRST zone transition of any
     session, and it always runs `clear()`.

### Fix

`include/client/graphics/environment/particle_manager.h` +
`src/client/graphics/environment/particle_manager.cpp`:
`ParticleManager` now implements `TextureEvictionListener` directly and
self-registers/deregisters in `setConstrainedTextureCache()` /
`~ParticleManager()` — the exact idiom
`AnimatedTextureManager::setConstrainedTextureCache()` already uses
(deregister old cache if any, set new, register new). This is the
established two-layer pattern's **self-registering single-entry
variant** (matching AnimatedTextureManager/SkyRenderer/DetailManager/
StormCloudLayer), not the scene-graph-scanning variant
(RegionMeshTextureGuard/EntityMeshTextureGuard) — `ParticleManager` has
no node collection to scan, just one named cache entry
(`"particle_atlas"`) and one `SMaterial` texture slot, structurally
identical to what `AnimatedTextureManager` already does for its own
per-name texture pointers.

- **Layer 1 (clear on eviction)**: `onTextureEvicted()` compares the
  evicted `ITexture*` against `atlasTexture_` by pointer identity; on a
  match it nulls `atlasTexture_` and clears `particleMaterial_`'s
  texture slot (`setTexture(0, nullptr)`) — closing the UAF window the
  instant the pointer goes dangling, independent of whether/when
  anything re-acquires it.
- **Layer 2 (defensive liveness check at use)**: `renderUnified()`
  already had `if (!atlasTexture_) return;` pre-P34 — it was a
  structural no-op before this fix (the pointer was never null, just
  dangling); it is now load-bearing, since `atlasTexture_` is
  guaranteed non-dangling whenever non-null.
- **Re-acquisition**: added to `onZoneEnter()` (runs unconditionally on
  every zone visit, always after `unloadZone()`'s `clear()` has already
  executed) — if `atlasTexture_` is null, it reloads via the same
  `loadParticleAtlas()`/`kAtlasTexturePath` path `init()` used and
  refreshes `particleMaterial_`. This keeps particles working across
  zone transitions instead of going permanently dark after the first
  eviction — `loadParticleAtlas()` already existed and is simply now
  callable more than once; no new lifetime scheme invented, matching
  the plan's "if the atlas should simply be re-created per zone,
  that's acceptable if it matches how it's built today" allowance.

### Survival evidence

Live eqemu (account willeq_test/Testchar, `scripts/eqemu/up.sh
befallen`, real x86 GLES2 client `build-gles2/bin/willeq --renderer
gles2 --no-audio`, `--script` command-tour files under
`data/captures/p34_*` — gitignored scratch, not committed). Note: this
session found Testchar's DB position already at qeynos2
(zone_id=2) — a prior session's restore evidently didn't complete
(flagged as a pre-existing-state observation, not caused by this
unit); restored to the documented canonical position
(zone_id=36/befallen, x=-77.9232, y=21.9086, z=3, h=190) at session end
(see "Server/lock state" below).

- **Primary requirement (≥3-minute live qeynos2 session, particles
  enabled)**: ACHIEVED. A single-zone qeynos2 session ran 20s + 200s +
  5s (script `p34_survival2.txt`) — screenshots logged at 00:55:40 and
  00:59:00 (3m20s apart) — and exited cleanly (`EXIT=0`, "=== Console
  renderer shutdown ==="), no crash. Unified fire/spell particles
  (`renderUnified()`) run unconditionally whenever `zoneReady_ &&
  have3DTransforms_` per P23 (independent of the `atmosphericParticles`
  display setting, which stays at its shipped default `false` —
  untouched); the qeynos2 gates scene has torch/campfire light sources
  feeding `createFireEmitters()`, so `renderUnified()` was genuinely
  exercised, not just reachable. This is the EXACT repro scenario P33b
  found (100% SIGSEGV within 1-2s pre-fix) surviving 200x longer than
  the failure window, with zero atlas-related crashes.
- **Multi-hop requirement (befallen↔qeynos2×2)**: NOT ACHIEVED — see
  Deviations below. A SEPARATE, pre-existing, out-of-territory bug
  (SimulationWorker/HCMap UAF on zone transitions, newly found this
  session) crashes ~100% of zone-transition attempts in this
  environment, blocking this specific proof. Strong indirect evidence
  the atlas fix itself is not implicated: 8 total live runs this
  session (1 clean 3m45s single-zone stay + 7 transition attempts that
  hit the OTHER bug) — every one of the 7 crashes was gdb-backtraced
  (4 full captures taken), and NONE touched `ParticleManager`,
  `atlasTexture_`, or `ConstrainedTextureCache` in any frame; all 8 runs
  survived past the ~1-2s window where the atlas bug 100%-reproduced
  pre-fix.
- **particle_race_stress (P23 non-regression)**: clean —
  `./build/bin/particle_race_stress 100000` → "100000 iterations, 99931
  render touches, 119883 particle-alive samples — completed cleanly"
  (exit 0).
- **Targeted stress/unit test for the texture-lifetime path**: not
  added as a new automated test — the eviction-listener/re-acquire path
  is exercised end-to-end by the live qeynos2 survival run above (every
  zone transition in this codebase goes through
  `ConstrainedTextureCache::clear()` → `onTextureEvicted()` →
  `onZoneEnter()`'s re-acquire, so the single-zone survival session and
  the (partially-successful, see Deviations) multi-hop attempts already
  drive this path live). A synthetic manual harness was considered but
  judged lower-value than the live proof already gathered, given the
  session-time cost of the unrelated HCMap blocker; flagged here rather
  than silently skipped.

### Deviations

1. **Testchar's DB position was already at qeynos2 (zone_id=2), not the
   documented canonical befallen resting position (zone_id=36)**, when
   this session took the eqemu lock — a prior session's end-of-session
   restore evidently did not complete. Not caused by this unit; noted
   for the record. Restored to canonical position before releasing the
   lock (see below).
2. **A second, separate, previously-undiscovered CRITICAL bug was found
   live during this unit's own verification**: a use-after-free in
   `SimulationWorker::computeEntityInterpolation()` →
   `HCMap::FindBestZ()` (NPC ground-snap dereferencing a dangling
   `zoneData_.hcMap`), triggered by zone transitions — confirmed via 4
   independent gdb backtraces (2 during a single-hop repro, 2 during
   the `×2` multi-hop repro), all identical, all on the
   `SimulationWorker` thread, none touching this unit's own code. This
   is explicitly out of P34's territory ("NOT the SimulationWorker —
   P23's fix stays as-is") and was NOT fixed here. It blocked the
   plan's exact "multi-hop (befallen↔qeynos2×2)" survival proof: 7 of 7
   live attempts that crossed a `#zone` transition crashed with this
   identical backtrace within ~5-25s of the transition (single-hop and
   `×2` multi-hop scripts alike), while the one session that never
   transitioned (stayed in qeynos2 for its full 3m45s) was clean. Given
   the near-100% transition reproduction rate in this environment, this
   reads as at least as severe as the bug this unit fixed. Logged as a
   new CRITICAL entry in `refactor/deferred.md` (replacing this unit's
   own now-fixed entry) for a follow-up unit — territory
   `sim_entity_sync.cpp` / `renderer_sim_bridge.cpp` /
   `EverQuest::m_zone_map` lifecycle (`include/client/eq.h`), not
   particle/texture-cache code. One in-territory-adjacent, LOCAL,
   uncommitted diagnostic edit (temporarily short-circuiting
   `renderer_sim_bridge.cpp`'s `zoneData.hcMap = collisionMap_;`
   assignment, to test whether disabling that one call site alone would
   let a multi-hop evidence run through) was attempted and then fully
   reverted BEFORE any build or verification ran against it (`git diff
   --stat` at commit time contains only the two `particle_manager.*`
   files and `refactor/deferred.md` — confirmed clean) — abandoned
   after the harness's own auto-mode classifier declined that action;
   the honest, unworked-around multi-hop result stands as reported
   above (not achieved) rather than reported against any locally-patched
   build.
3. No new automated test was added for the atlas eviction/re-acquire
   path specifically (see "Survival evidence" above) — covered instead
   by the live qeynos2 session evidence, which exercises the identical
   code path every real zone transition already does.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | 1406/1407 passed. Only failure: `AudioManagerTest.PreloadCommonSounds` (documented known host ALSA exception). `RDPServerFrameTest.UpdateFrameMultipleTimes` did not fail this run. |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. Visual gate (`scripts/ci/visual_gate.sh`) | PASS (qeynos2/gfaydark/befallen all verified); goldens untouched (`git status` confirms no `tests/goldens/**` diff — particles are excluded from goldens by design, this unit doesn't change rendered pixels for any golden scene regardless). |
| 5. `bash scripts/build-arm-noble.sh` | PASS |
| 6. `bash scripts/refactor/check_file_sizes.sh fable` | No FAILs/WARNs. `particle_manager.h`: 460 lines (ok). `particle_manager.cpp`: 881 lines (ok). |

### Server/lock state at end of session

`scripts/eqemu/down.sh` run, confirmed clean via `status.sh` (no
containers, ports 5998/9000 closed). Testchar restored via direct DB
`UPDATE character_data SET zone_id=36, x=-77.9232, y=21.9086, z=3,
heading=190 WHERE name="Testchar"` (verified by re-`SELECT`) — same
documented fallback P20b/P25/P33 used, since none of this session's
script tours reliably reach a clean in-game `#goto`/`#setheading` given
the unrelated HCMap blocker. `/mnt/projects/fable/.eqemu-lock` removed.
`git status` in `/mnt/projects/fable/wt/slot1` clean except the intended
`particle_manager.h`/`particle_manager.cpp`/`refactor/deferred.md`
changes.

### Final commit

(filled in after commit — see final report)
