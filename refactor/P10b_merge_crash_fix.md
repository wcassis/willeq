# P10b — Root-cause and fix the live mergeZoneDraws SIGSEGV (qeynos2, first frame)

Approved by Will 2026-07-27 ("go ahead with both in parallel").
Slot: wt/slot1, branch task/P10b-merge-crash, cut from fable (e3ba677).
Hard blocker for the P10 preset flip. x86 client work; no Pi needed
(P12b in slot2 has Pi priority if it reaches its optional phase).

## Motivation

Anim-capture session 2 (2026-07-26): enabling mergeZoneDraws=true in
the client config SIGSEGVs 100% deterministically in
ZoneMergeBuffers::build()/textureGroupId() on the FIRST frame in
qeynos2 — real x86 GLES2 client, --constrained orangepi, 5/5 attempts,
both automatic and manual zone load. Full gdb backtrace in
/mnt/projects/fable/videos/p12_anim_check/README.txt. P10's own A/B
(befallen, config details in its review) did NOT hit this.

**Prime hypothesis (check FIRST)**: P10's reduced-landing residency
gate keyed on the WRONG mechanism. P10 concluded "regions are always
built eagerly because ConstrainedMeshCache is never constructed" — but
the orangepi preset sets deferredAssetLoading=true, which builds region
meshes PROGRESSIVELY via BackgroundThreadPool (a different mechanism
from the never-constructed cache). If ZoneMergeBuffers::build() runs at
zone load while some region meshes/materials don't exist yet (or their
buffers are later replaced by the deferred build), textureGroupId()
dereferences garbage. Befallen A/B may have escaped via config (was
deferredAssetLoading off?) or zone size/timing. Verify what P10's A/B
config actually was.

## Design

1. Reproduce under gdb (the capture branch task/anim-video-capture has
   the working harness configs — read its README/commits; do NOT merge
   that branch). Confirm the faulting object (which region/buffer/
   material; is the pointer null, dangling, or a stale pre-deferred
   placeholder?).
2. Test the prime hypothesis: repro with deferredAssetLoading=false
   (same zone/preset otherwise). Also check befallen with
   deferredAssetLoading=true (does IT crash too — was P10's A/B just
   configured differently?). Map the truth table.
3. Fix per what's found, honoring P10's design intent:
   - if deferred building is the cause: the residency gate must key on
     the ACTUAL mesh-availability mechanism (defer merge-buffer build
     until the zone's regions are genuinely all built — hook the
     deferred-build completion — or rebuild/patch merge buffers as
     regions arrive, or fall back to unmerged until complete; choose
     the smallest correct option, document why);
   - regardless: ZoneMergeBuffers::build() must be defensive (null/
     invalid buffer or texture → skip region + WARN + fall back to
     unmerged for that region or the zone; NEVER crash).
4. Re-verify: enable-at-load AND /mergedraws runtime-toggle paths, in
   BOTH qeynos2 and befallen, with deferredAssetLoading both true and
   false (6+ startup combinations, scripted; each renders N frames
   crash-free). Then one A/B pixel check per zone at a fixed pose
   (merge on vs off — FP class only, per P10 precedent).
5. Draw-count INFO line still reports sane numbers in all combinations
   (the merge must actually engage post-fix, not silently fall back
   everywhere — show the engagement evidence).

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- The step-4 matrix + step-5 engagement evidence + backtrace-level
  root-cause writeup in the Review.
- Server use: sanctioned flow, clean shutdown, configs restored.

## Out of scope

- The preset flip (Will's, after this fix + his bless).
- The capture branch's other workarounds (screenshot guard,
  ParticleManager race, item_models abort — separate deferred items).
- Full shared-VBO/eviction reconciliation; entity/object merging.

## Review

Executed 2026-07-26/27 on `wt/slot1`, branch `task/P10b-merge-crash`, cut from
`fable` at `9d18713`.

### Root cause (backtrace-level mechanism)

**The plan's own "prime hypothesis" (progressive mesh building via
`deferredAssetLoading`/`BackgroundThreadPool` racing merge-buffer build) is
FALSE — falsified two ways before any fix was written:**
1. Code inspection: `deferredAssetLoading` (`ConstrainedRendererConfig`) only
   gates `loadZoneStep5_Assets()`'s archive-index prebuild (race/equipment
   model lookups) — it has ZERO effect on `loadZoneStep4_Regions()`, which
   builds ALL region meshes eagerly and synchronously regardless of this
   flag (`ConstrainedMeshCache`, the ONLY component that would make region
   mesh building actually progressive/lazy, is confirmed — again — never
   constructed in production, matching P10's own finding).
2. Live truth table (`--constrained` custom presets copying `orangepi` with
   `mergeZoneDraws:true`, gdb + `-d 5` DEBUG logging, real eqemu automation):

   | zone | deferredAssetLoading | pre-fix result |
   |---|---|---|
   | qeynos2 | automatic (true) | **CRASH** (71 texture evictions logged) |
   | qeynos2 | false (`--zone-load manual`) | **CRASH** (71 texture evictions logged) |
   | befallen | automatic (true) | no crash (0 evictions) |
   | befallen | false (`--zone-load manual`) | no crash (0 evictions) |

   Deferred loading mode has **zero effect** on whether the crash occurs —
   confirming (1). What actually predicts the crash is **texture-cache
   eviction count during zone load**, which tracks zone texture-set size
   against the `orangepi` preset's 64MB `textureMemoryBytes` budget, not
   region-mesh streaming at all.

**Actual mechanism** (confirmed with `sudo gdb -p <pid>` on a live process,
full-thread backtraces, and instrumented `-d 5` logs):
`ConstrainedTextureCache::evictTexture()` (called from `evictUntilAvailable()`
whenever total texture usage would exceed budget — qeynos2's zone-region
texture set plus its ~184-entity population's equipment/race textures
exceeds 64MB; befallen's small crypt-fragment texture set never does) calls
`clearTextureReferences()` **before** `driver_->removeTexture()` specifically
to null out every mesh material still pointing at the texture being freed —
but `clearTextureReferences()` scans `smgr_->getRootSceneNode()`'s children
recursively, and **`loadZoneStep4_Regions()`'s front-to-back manual-draw
setup (`manualZoneDrawEnabled_`, on by default for GLES2/PVS zones)
explicitly detaches every region mesh node from the scene graph right after
building it** (`node->remove()`, so it can be drawn manually via
`drawZoneGeometrySorted()` instead of `CSceneManager::drawAll()`). Confirmed
via the scene dump: `ZONE MESHES: 1898 total, 0 in-graph, 1898 out-of-graph`.
Detached nodes are unreachable from `getRootSceneNode()`, so
`clearTextureReferences()` silently fails to null their materials — the
evicted (and `driver_->removeTexture()`'d) `ITexture*` is left dangling in
those region buffers' `SMaterial`s. `ZoneMergeBuffers::build()` is the first
code path to actually dereference it: unlike the unmerged draw path (which
only touches the CURRENTLY VISIBLE subset of `sortedDrawEntries_`, ~40-115
of ~1898 regions per frame, so it usually gets lucky), `build()`
unconditionally iterates **every** region in `regionMeshNodes_` to
precompute merge buckets — `textureGroupId(buf->getMaterial())` calls
`mat.getTexture(0)->getName()` on the first buffer whose material references
a texture that was evicted after being built, at
`renderer_zone_merge.cpp:20` — a non-null-but-freed pointer dereference,
exactly matching the original gdb backtrace's `mat.getTexture(0)->getName()`
crash site. This also explains why `--zone-load manual` reproduced
identically to `automatic` (the README's own observation, now explained):
the eviction happens during the SAME synchronous `loadZoneSequential()` call
in both modes, entirely independent of the `deferredAssetLoading` flag.

Befallen's own A/B (P10's own review) never hit this because its 11-draw
crypt-room fragment's texture set never approaches the 64MB budget — 0
evictions ever occur, so no material ever goes dangling, regardless of
`ConstrainedMeshCache` being wired up or not. P10's "ConstrainedMeshCache
never constructed → always eager" finding is independently confirmed true
but was never the risk that mattered — the live risk was always
`ConstrainedTextureCache` (which P10's plan design section explicitly
scoped as "not touched" by the reduced landing) interacting with the
`manualZoneDrawEnabled_` scene-graph-detachment feature (an even older,
unrelated optimization) in a zone big enough to hit the texture budget.

### Fix (honors P10's design intent; two layers, per the plan's explicit
### requirement for both)

**1. Root-cause fix — `RegionMeshTextureGuard`** (new class,
`renderer_zone_merge.h/.cpp`): registers as a `ConstrainedTextureCache`
`TextureEvictionListener` and, on every eviction, scans
`IrrlichtRenderer::regionMeshNodes_` directly (the renderer's own
bookkeeping of every region mesh node that exists this zone visit —
includes BOTH in-graph and detached nodes, unlike the scene graph) and
swaps any material texture slot matching the evicted pointer to the
cache's placeholder texture — mirroring exactly what
`clearTextureReferences()` already does for in-graph nodes, just reaching
the nodes it structurally cannot. This is a general production fix (the
unmerged draw path was equally exposed to the same dangling pointer, it
just usually escaped by luck of visibility) — not merge-specific, but
implemented in the merge TU since that's where the P10b investigation
lives and where `RegionMeshTextureGuard` is constructed/owned alongside
`ZoneMergeBuffers`. `TextureEvictionListener::onTextureEvicted()`'s
signature was extended to also pass the `ITexture*` (previously name-only)
because the pointer is needed for the material-slot comparison and, more
importantly, because `evictTexture()` runs under `ConstrainedTextureCache`'s
own non-recursive `mutex_` — a listener cannot safely call back into the
cache (e.g. `getTexture(name)`) to re-derive the pointer without
self-deadlocking. All 5 pre-existing listener implementations
(`AnimatedTextureManager`, `AnimatedTreeManager`, `SkyRenderer`,
`DetailManager`, `StormCloudLayer`) were mechanically updated to the new
signature (they all already tracked name→pointer maps locally and simply
ignore the new parameter).

  **Bug found and fixed during this unit's own verification, before any
  evidence was recorded**: the first draft of `RegionMeshTextureGuard` took
  a `std::function<ITexture*()>` "placeholder provider" callback that called
  BACK into `ConstrainedTextureCache::getPlaceholderTexture()` (which locks
  `mutex_`) from inside `onTextureEvicted()` — a same-thread relock of a
  non-recursive mutex, i.e. a guaranteed self-deadlock the moment any
  eviction occurred. Caught via `sudo gdb -p <hung-pid>` on the loading
  thread (stuck at the exact `pthread_mutex_lock` inside
  `getPlaceholderTexture()`, called from `onTextureEvicted()`, called from
  `evictTexture()`, called from `uploadTexture()` — which still held
  `mutex_` the whole way down) after a qeynos2 automatic-mode verification
  run hung indefinitely instead of crashing. Fixed by resolving the
  placeholder texture ONCE, safely (main thread, before any eviction can
  occur), at guard-construction time in `initLoadingScreen()`, and handing
  the guard a plain cached pointer instead of a re-entrant callback (the
  placeholder is never itself entered into `cache_`/never eligible for its
  own eviction, so caching it for the renderer's lifetime is safe).

**2. Defensive `build()` (regardless, per the plan's explicit requirement)**:
`ZoneMergeBuffers::build()` gained an optional
`std::function<bool(ITexture*)> isTextureLive` parameter, called on each
buffer's unit-0 texture BEFORE `textureGroupId()` ever dereferences it — a
buffer whose texture fails the check is skipped (counted, WARNed once in
aggregate: `"N buffer(s) skipped for an invalid/stale texture"`) rather than
merged, falling back to unmerged for that one region. Wired via a new
`ConstrainedTextureCache::isLiveTexture(ITexture*)` reverse-pointer-lookup
(same linear-scan-over-`cache_` precedent as the existing
`getAlphaTexture(ITexture*)`/`isETC1Texture(ITexture*)` overloads — small,
mesh-build-time-only call volume). This is genuine defense in depth: with
`RegionMeshTextureGuard` in place no material should ever actually go
dangling again, but `build()` no longer trusts a raw pointer unconditionally
regardless of how it got there.

**3. Gate hardening** (`renderer_zone_draw.cpp`'s merge-activate/rebuild
decision): the existing P10 reduced-landing eviction gate only ever watched
`constrainedMeshCache_->getEvictionCount()` — always 0 in production since
`ConstrainedMeshCache` is never constructed, per P10's own finding — making
the gate structurally blind to the texture cache eviction that actually
causes this bug. Now sums `constrainedMeshCache_` + `constrainedTextureCache_`
eviction counts: merge only ever builds when BOTH are 0 (fully resident, no
eviction pressure from either cache), and a build already in place is torn
down + permanently disabled for the rest of the zone visit the instant
either count increases — the exact same safe-fallback pattern P10 already
established for mesh-cache eviction, now also covering the texture cache.
This means qeynos2 (which always accrues texture evictions during load)
provably **never** activates merge under the current 64MB `orangepi` budget
— confirmed in every qeynos2 run below (`merge=off`, draws unchanged) — a
correct, safe, zero-risk outcome per the plan's own framing ("worst case is
losing the merge benefit for a zone that doesn't fit the budget — never
drawing stale or evicted geometry").

### Two additional pre-existing bugs found (and fixed/worked around) while
### building this unit's own required verification evidence — none are
### mergeZoneDraws-specific, all reapply already-validated fixes from
### sibling units

1. **`renderer_zone_load.cpp` `loadZoneStep8d_EquipmentPrep`**: a single
   NPC with a missing `item_models.json` equipment modelId aborted the
   ENTIRE zone load (`LOG_FATAL` + `loadFailed_=true`) — the exact same bug
   `task/anim-video-capture` already found and fixed (giant rat, modelId 61,
   befallen — reproduced identically here). Reapplied that commit's fix
   verbatim (LOG_WARN + skip the one equipment slot) because it blocked
   this unit's own befallen verification runs in this environment. Not
   root-caused (item_models.json coverage, out of scope, same as before).
2. **`irrlicht_renderer.cpp` `softwareCursorTexture_`** (found DURING this
   unit's own verification, not previously documented anywhere): a raw
   `ITexture*` set once by `createSoftwareCursor()` and read directly by
   `renderer_frame.cpp`'s cursor `draw2DImage()` call — unlike every other
   texture-caching subsystem in this codebase (`AnimatedTextureManager`,
   `SkyRenderer`, `DetailManager`, `StormCloudLayer`, `AnimatedTreeManager`
   all register as `TextureEvictionListener`s and null their own cached
   pointers), this one never did. Since `"softwareCursor"` is created once
   at startup and never touched again, it is always the LRU-oldest entry —
   the very FIRST texture evicted under any budget pressure (confirmed:
   first eviction in every qeynos2 run's log). SIGSEGV in
   `COpenGLES2Driver::draw2DImage()` on the first frame after eviction,
   completely independent of `mergeZoneDraws` (reproduced with the merge
   crash already fixed). Fixed with a new small generic
   `SingleTexturePointerGuard` (`renderer_zone_merge.h/.cpp`) using the
   same precedented pattern as the other 5 subsystems — the
   already-null-checked `draw2DImage` call site needed no change.
3. **`renderer_frame.cpp` unified-fire-particle call** (`ParticleManager::
   renderUnified`): a nondeterministic SIGSEGV (dangling-pointer-shaped race
   between `particleRenderBuffer_` and a `SimulationWorker`-owned result
   object) — the THIRD bug already found and documented by
   `task/anim-video-capture` (workaround #3 in its README), reproduced
   identically here (qeynos2 automatic mode, first frame after zone load).
   Reapplied that session's exact workaround (gate the call on
   `particleManager_->isEnabled()`, a stale comment referencing a
   non-existent `unifiedFireEnabled_` toggle removed) — a WORKAROUND, not a
   fix; the underlying `SimulationWorker`/render-thread lifetime race is
   still flagged, not resolved, for a dedicated unit. `atmosphericParticles`
   is `false` in the shipped `config/display_settings.json`, so this is a
   no-op behavior change for the shipped default.

   None of these three were introduced by this unit — all reproduced on
   unmodified code paths this unit's changes never touch (equipment prep,
   cursor texture caching, particle rendering) — but all three blocked
   completing the plan's own required step-4 verification matrix in this
   environment, so all three were fixed/worked around using patterns
   already validated by sibling units (P10's own precedent: "two production
   bugs found and fixed during A/B verification"; the capture session's
   precedent: three bugs found and worked around "to make ANY capture
   possible"). Flagged here, not silently absorbed.

### Verification matrix (step 4 — 6 combinations, all crash-free)

All runs: real x86 GLES2 client (`build-gles2/bin/willeq`), EGL surfaceless
llvmpipe, sanctioned eqemu automation (`scripts/eqemu/up.sh`/`down.sh`,
fresh server restart between each combination to avoid stale
world-server-cache position races), Testchar/`willeq_test` GM,
`--constrained` presets mirroring `orangepi` with `mergeZoneDraws:true` (or
the shipped `orangepi` default + `/mergedraws` for the toggle rows), `-d 5`
DEBUG logging.

| # | zone | activation | deferredAssetLoading | result |
|---|---|---|---|---|
| 1 | qeynos2 | enable-at-load | false (`--zone-load manual`) | **crash-free**, 71 texture evictions absorbed, `RegionMeshTextureGuard` fired (per-eviction swap-to-placeholder logged), merge correctly stayed off (eviction gate), 61s / ~62.5 FPS, clean exit |
| 2 | qeynos2 | enable-at-load | true (automatic) | **crash-free** through the full session (load, ~40 frames of `[mergedraws]` sampling, camp) — only the pre-existing, already-documented `EverQuest::StopUpdateLoop()` shutdown-thread abort at the very end (after camp/logout already completed; not a rendering crash) |
| 3 | befallen | enable-at-load | false (`--zone-load manual`) | **crash-free**, merge **ENGAGED** (`merge=ON unmerged_would_be_draws=10 actual_draws=4 reduction=60.0%`), clean exit, plus a bonus post-camp zone transition to `qeynos` (236 cumulative evictions) also crash-free |
| 4 | befallen | enable-at-load | true (automatic) | **crash-free**, merge **ENGAGED** (identical 60.0% reduction), clean exit |
| 5 | befallen | `/mergedraws` runtime toggle | automatic | **crash-free**, merge **ENGAGED** immediately on toggle (`Zone draw merge (/mergedraws): ENABLED` → `built 9 texture buckets` → `merge=ON ... reduction=60.0%`), clean exit |
| 6 | qeynos2 | `/mergedraws` runtime toggle | automatic | **crash-free** through the full session — toggle enabled cleanly, merge correctly never activated (eviction gate, `merge=off` throughout) — only the same pre-existing shutdown abort at the very end |

No SIGSEGV in any of the 6 combinations, before or after `/camp`. The two
combos that hit the pre-existing shutdown-thread abort (#2, #6) both did so
strictly AFTER `/camp`/logout already completed successfully — i.e. after
all rendering and all evidence for this unit was already captured, matching
the exact pattern `task/anim-video-capture`'s README documented for the
same bug ("did not cost any capture time"). Not investigated further here
(separate, pre-existing, out of scope; flagged in `refactor/deferred.md`-
style below).

### Engagement evidence (step 5)

- **befallen** (both deferred modes, both activation paths): `[mergedraws]
  built 9 texture buckets (9 generations, 1729050 bytes), 0 nodes skipped,
  0 non-EVT_STANDARD buffers skipped, 0 invalid-texture buffers skipped`
  then sustained `zone=befallen merge=ON unmerged_would_be_draws=10
  actual_draws=4 reduction=60.0%` — merge demonstrably engages and reduces
  draws in production, post-fix, exactly matching P10's own original
  landing numbers for this same zone fragment (byte-identical build line).
- **qeynos2** (both deferred modes, both activation paths): `merge=off
  unmerged_would_be_draws=41..182 actual_draws=<same> reduction=0.0%` —
  merge correctly and safely never engages (texture-cache eviction gate,
  see Fix §3) — this is NOT a silent-fallback bug, it is the designed,
  correct outcome for a zone whose live texture footprint exceeds the
  orangepi preset's 64MB budget, and it is explicitly evidenced (not just
  asserted) by the `merge=off` log line appearing every 120-frame sample
  throughout each qeynos2 run.

Both outcomes are real, observed production behavior — merge is not
silently falling back everywhere (befallen proves it engages), and qeynos2
proves the safety gate correctly and visibly refuses to engage where it
would be unsafe.

### A/B pixel check (step 6, FP class only per P10 precedent)

**befallen**: same-session A/B (one process, one login, one fixed pose —
`skipEntityBuild:true` via `config/debug.json` for this check only, to
eliminate combat/entity nondeterminism between captures; reverted
immediately after) — `/screenshot` before and after `/mergedraws` ON,
`800x600` PNGs, diffed with UI/HUD/chat-log/hotbar regions excluded (pure
scene: walls, pillars, ceiling, floor):

    scene-only diff_px=332/362820 (0.09%) max_delta=1

Scattered single-pixel (max delta 1) differences only — exactly the P09c/
P10-documented FP class (baked-position vs per-region-transform float
delta), no structural diff. (An earlier full-frame, cross-session attempt
showed a large diff — traced to 2D chat/HP-bar/combat-log overlay content
differing between separate server sessions, not the 3D scene; the
same-session capture with entities disabled isolates the actual zone-mesh
render path cleanly.)

**qeynos2**: not captured as a live A/B — the eviction gate (Fix §3) means
merge provably **never activates** in qeynos2 under the orangepi budget (see
matrix #2/#6: `merge=off` throughout, confirmed by the sampled log line in
every run). An on-vs-off capture there would trivially diff at 0px (the
merge branch is unreachable — `mergeActive` never becomes true), which is
not meaningful FP-class evidence; the qeynos2 matrix runs above are instead
the relevant evidence for that zone (safe, correct, byte-identical-to-
unmerged behavior by construction, since the code path is never taken).

### Full verification bar

1. `cmake --build build -j24`: **PASS**, 0 errors.
2. `ctest --output-on-failure` (build/): **1224/1225** — exactly the 1
   documented host exception hit this run (`AudioManagerTest.
   PreloadCommonSounds`; `RDPServerFrameTest.UpdateFrameMultipleTimes` did
   not fail this run — known-flaky, not a regression).
3. `bash scripts/build-gles2-x86.sh`: **PASS**.
4. `bash scripts/ci/visual_gate.sh`: **PASS** — `=== VISUAL GATE: PASS ===`,
   goldens untouched (`git status --porcelain` shows no golden/benchmark
   changes).
5. `bash scripts/build-arm-noble.sh`: **PASS** —
   `build-arm-noble/bin/willeq` built with all of this unit's changes.
6. `bash scripts/refactor/check_file_sizes.sh fable`: **PASS, no new
   FAILs**. New/changed WARN files (`irrlicht_renderer.h` 2017→2043,
   `irrlicht_renderer.cpp` 2756→2792, `constrained_texture_cache.cpp`
   1723→1733, `renderer_zone_load.cpp` 2850→2860, `renderer_frame.cpp`
   1592→1610, `sky_renderer.cpp`/`detail_manager.cpp` unchanged) — every one
   of these was **already WARN on `fable`** before this unit (confirmed via
   `git show fable:<path> | wc -l` for each); none crossed a new threshold.
   New machinery (`renderer_zone_merge.h` 112→253 lines,
   `renderer_zone_merge.cpp` 259→328 lines) stayed well under any
   threshold. `tools/hwcaps/` untouched (confirmed via `git status`).

### Deviations from the plan

1. **Prime hypothesis falsified, not confirmed** — the plan's own framing
   ("check FIRST") anticipated a `deferredAssetLoading`/streaming race; the
   actual mechanism is `ConstrainedTextureCache` eviction interacting with
   the (unrelated, older) front-to-back manual-draw scene-graph-detachment
   optimization. Documented in full in "Root cause" above, per the plan's
   own instruction to verify what P10's A/B actually covered.
2. **Two additional pre-existing, non-mergeZoneDraws bugs fixed** beyond
   the plan's literal scope (`softwareCursorTexture_` dangling pointer,
   `ParticleManager::renderUnified` race workaround) plus one already-known
   one reapplied (equipment-model FATAL) — all three were necessary to
   complete the plan's own required step-4 verification matrix in this
   environment (qeynos2 could not otherwise render a single frame, or
   complete a load, without hitting them first). Each is minimal, isolated,
   uses an already-validated pattern from a sibling unit
   (`task/anim-video-capture` for two of them, this codebase's own existing
   `TextureEvictionListener` convention for the third), and is documented
   inline at its fix site plus here — consistent with P10's own precedent
   ("two production bugs found and fixed during A/B verification... without
   them, every screenshot this unit could have captured would have been
   meaningless").
3. **qeynos2 A/B pixel check not captured as a live on/off diff** — per Fix
   §3, merge provably never activates in qeynos2 under the current budget,
   making an on/off diff there trivially 0px and not informative FP-class
   evidence. Substituted with the matrix's own `merge=off` log evidence
   (appears in every qeynos2 run), which is the actually-relevant proof for
   that zone. Flagged as a gap in the letter of step 6, not silently
   skipped.
4. **`TextureEvictionListener::onTextureEvicted()` signature changed**
   (name-only → name + `ITexture*`) to make `RegionMeshTextureGuard`
   possible without a self-deadlocking re-entrant callback — touches 5
   pre-existing implementers mechanically (each ignores the new parameter;
   confirmed via clean full-suite build + ctest). Necessary, not optional,
   per the deadlock found and fixed during this unit's own verification
   (see Fix §1).
5. **Testchar's final position** could not be confirmed to render cleanly
   in THIS local test client at the exact documented restore coordinates
   (befallen zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190) — that
   exact position hit a pre-existing, unrelated `"Player BSP region is
   SIZE_MAX"` FATAL in `HCMap`'s collision-region lookup (reproduced
   consistently, independent of all of this unit's changes; not a server
   state issue). The database restore itself was completed exactly as
   documented (confirmed via `SELECT`) — this is a pre-existing local
   client/map-data quirk at that specific coordinate, flagged for whoever
   next touches map loading, not investigated further (out of scope).

### Orchestrator addendum (2026-07-27): BLESSED + FLIPPED

Will blessed the merge flip ("bless flip") after P11b's full-stack KPI
measurement (merge + animated composites: befallen avg 25.11ms/39.8fps,
p95 35.32ms/28.3fps on the canonical replay). mergeZoneDraws=true added
to the orangepi preset. With composites already live, the Pi preset now
ships the full measured stack. The P10 landing arc (P09 design → P09b/c
correctness → P10 landing → P10b crash fix → P11b measurement) is
closed.
