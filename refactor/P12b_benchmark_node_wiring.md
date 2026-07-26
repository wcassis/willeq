# P12b — Find the benchmark's animation node-wiring delta, fix the crumple, restore animated evidence

Approved by Will 2026-07-27 ("go ahead with both in parallel").
Slot: wt/slot2, branch task/P12b-node-wiring, cut from fable (e3ba677).
Runs in parallel with P10b (slot1, production merge crash — disjoint
files). Pi allowed ONLY for the optional final phase.

## Motivation — the search space just collapsed

Will's gates-video verdict (2026-07-27): live production NPCs animate
CORRECTLY (multibuffer AND composite runs). So the P12 crumple is
benchmark-context-only after all — despite P12's finding that the
driving idiom (OnAnimate/forceAnimationUpdate) is identical. Production
animates through per-instance state: each EQAnimatedMeshSceneNode owns
its own SkeletalAnimator AND its own instanceMesh_ (per-instance
skinned vertex buffer, createInstanceMesh()/applyAnimation() —
"Per-instance animation state (NOT shared between entities)",
animated_mesh_scene_node.h). The delta is therefore almost certainly in
NODE CONSTRUCTION/WIRING: how the benchmark creates its animated nodes
vs production's createAnimatedNode() path (instanceMesh creation,
material/buffer hookup, skeleton/clip setup). P12's crumple-at-frame-0
finding fits a construction-time difference exactly.

## Design

1. **Diff the construction paths line-by-line**: production
   RaceModelLoader::createAnimatedNode / createAnimatedNodeWithAppearance
   (animated_mesh_creation.cpp:237+) vs the benchmark's animated-node
   creation (gles2_zone_benchmark.cpp ~1704-1724 + zone_bench_loader).
   Enumerate every divergence: constructor arguments, createInstanceMesh
   invocation, mesh source (shared base vs instance), material list
   handling, skeleton/clip initialization, scene-manager registration.
2. Convert the diff into the fix: make the benchmark construct/drive
   nodes the production-faithful way (prefer calling the SAME production
   entry points rather than replicating them). Minimize
   gles2_zone_benchmark.cpp churn where possible (P10b is in slot1;
   file overlap is acceptable here since P10b targets production TUs —
   but keep changes tight anyway).
3. **Validate per the P08d lesson** (garbled-evidence verification is
   void):
   - animated renders show COHERENT walk/idle cycles across 4-6 races ×
     several phases (human-judgeable captures, compare against the
     gates videos' look);
   - static renders byte-identical before/after (the fix must not
     perturb the static path);
   - full suite + gate untouched.
4. **The deferred Z35 verification**: composite vs multi-buffer UNDER
   ANIMATION, aligned frames, quantified — contact sheet →
   benchmarks/contact_sheets/p12b_animated/. (Expected: identical or
   near; any structural diff = report.)
5. Re-evaluate the parked VertexMapping patch
   (refactor/scratch/P08d_vertexmapping_one_to_many.patch) against NOW-
   VALID animated renders: does it change anything visible? Land or
   discard it with evidence; close its loop.
6. **Un-skip/repair ModelAnimatesProperly**
   (tests/test_inventory_model_view.cpp) — the P12 finding that the one
   production-adjacent animation test is unconditionally skipped. Make
   it run meaningfully in CI if feasible (it may need DISPLAY/server —
   if genuinely un-runnable in CI, replace the blanket skip with a
   documented conditional skip + a runnable variant of the assertion at
   whatever level IS testable headlessly).
7. **OPTIONAL final phase (Pi, only if everything above is green and
   budget remains)**: the TRUE composite perf magnitude — canonical
   befallen replay, ANIMATED entities at calibrated load, composite on
   vs off (and +merge if trivially cheap), P02 protocol, <=8
   invocations. This replaces P08c's unreliable number and P11's
   wrong-route ~0% with the real one. If skipped, say so — it becomes
   P11b.

## Verification

- Standard bar: build, ctest (known host exceptions; note if
  ModelAnimatesProperly now runs), gles2-x86 build, visual gate PASS
  goldens untouched, ARM build, file-size check.
- Static byte-identity; the animated evidence set; replay discipline if
  the Pi phase runs.

## Out of scope

- Production animation code changes EXCEPT what step 5/6 justify with
  valid evidence (the crumple itself is benchmark-side by Will's
  verdict; if the diff-hunt unexpectedly implicates production code
  anyway, STOP and report before changing it).
- Impostors; the merge crash (P10b's); foliage alpha; pop-in.

## STOP

- Construction diff yields no candidate delta after a thorough
  enumeration → stop with the enumerated diff table (do not thrash).

## Review

### Setup

Branch `task/P12b-node-wiring`, cut from `fable` at `9d18713` (this plan's own commit)
per the checkout-first instruction. Slot `wt/slot2` (was on a capture branch; switched
cleanly, per instructions, without deleting it). Read in full before starting:
`refactor/P12_animate_root_cause.md` (the repro, the exhaustive prime-suspect +
bisection history, the frame-0 finding, the bind-pose-bbox lead), plus
`animated_mesh_creation.cpp`, `animated_mesh_scene_node.cpp/.h`,
`tools/hwcaps/gles2_zone_benchmark.cpp`, `tools/hwcaps/zone_bench_loader.cpp`,
`refactor/scratch/P08d_vertexmapping_one_to_many.patch`, `tests/test_inventory_model_view.cpp`.

### 1. Construction diff — production vs benchmark

Reproduced first (`build-gles2/bin/gles2_zone_benchmark --zone qeynos2 --eq-path
/mnt/projects/EverQuestP1999 --entities 6 --seed 42 --animate --frames 60 --warmup 5
--res 1024 768 --dump-frames 15,25 --orbit-radius 15`, Xvfb `:97`): confirmed
immediately — entities render as multi-colored, flattened/splayed blobs, both at close
range and (with `--orbit-radius 100` to separate entities) individually per-entity, not
merely a multi-entity screen-space overlap illusion.

**Key differential test**: `gles2_zone_benchmark --composite-entity-test --test-race 1
--animate` (a SECOND, independent animated-entity code path in the same benchmark
binary, using the SAME `RaceModelLoader::createAnimatedNode()` production entry point)
renders **perfectly** — clean, coherent walk-cycle poses for both HUM and HUF. This
proved the shared production machinery (`EQAnimatedMesh`, `EQAnimatedMeshSceneNode`,
`SkeletalAnimator`, `applyBoneTransformsToMeshImpl`) is NOT the bug — matching Will's
verdict — and narrowed the search to whatever differs between the benchmark's two
entity-setup code paths. A temporary diagnostic (`P12B_DIAG` env-gated printf in
`applyAnimation()`, added then fully reverted) confirmed both paths produce IDENTICAL,
healthy per-entity bounding-box extents (~2-6 unit human envelope) — the CPU-side
skinned vertex data was always correct; the bug was downstream, at the draw layer.

| Aspect | Production (`animated_mesh_creation.cpp`, `entity_renderer.cpp`) | Benchmark — `--composite-entity-test` (works) | Benchmark — default entity path (crumpled) |
|---|---|---|---|
| Node creation entry point | `RaceModelLoader::createAnimatedNode`/`createAnimatedNodeWithEquipment` | Same `createAnimatedNode()` | Same `createAnimatedNode()` — **no divergence here** |
| `EQAnimatedMeshSceneNode` constructor args | `(animMesh, parentNode, targetSmgr, id)` | identical | identical |
| `createInstanceMesh()` | called once in the constructor; copies `baseMesh_` (raw/unposed, per-bone-local) buffers into `instanceMesh_` | identical | identical |
| Skeleton/clip init | ctor sets `animator_`'s skeleton; `createAnimatedNode()` calls `playAnimation("o01"/"l01")` (no `forceAnimationUpdate()` yet) | identical, plus an explicit extra `playAnimation(animCode)` | identical, plus `node->getAnimator().update(offsetMs); node->forceAnimationUpdate();` (stagger) |
| Per-frame driving | `smgr_->drawAll()` → `OnAnimate(realElapsedMs)` every frame | `node->OnAnimate(simTimeMs)` every frame, `data->entities[i].mesh = node->getInstanceMesh()` reassigned every frame | same idiom (`gles2_zone_benchmark.cpp:2446`/`2455`) |
| **GPU buffer registration for the per-instance animated mesh** | **never registered** — `drawMeshBuffer()` always falls through `COpenGLES2Driver`'s "client-side vertex arrays" path, which reads live CPU vertex data every draw | **never registered** — same fallback path | **DIVERGENCE.** `zone_bench_loader.cpp` (`config.animate` block) called `node->getInstanceMesh()->setHardwareMappingHint(EHM_STREAM, EBT_VERTEX)` then `uploadMeshHWBuffers()` → `gles2CreateStaticHWBuffer()` → `COpenGLES2Driver::createStaticHardwareBuffer()`, registering `instanceMesh_`'s buffers in the driver's `HWBufferMap` **before the first real pose was ever applied** (at that point `createAnimatedNode()` had only called `playAnimation()`, not `forceAnimationUpdate()`, so `instanceMesh_` still held the raw/unposed, per-bone-local copy from `createInstanceMesh()`) |
| Effect of that registration | n/a | n/a | `COpenGLES2Driver::drawMeshBuffer()`'s fast path (`docker/irrlicht-drm/COpenGLES2Driver.cpp:1002-1060`) draws straight from the registered `HWBufferMap` VBO **for the rest of the process's life** whenever the buffer is found there — it never checks `IMeshBuffer::setDirty()`/`ChangedID`, so it never re-uploads. Every subsequent frame's correct, freshly-computed `OnAnimate()`/`applyAnimation()`/`applyBoneTransformsToMeshImpl()` CPU vertex update was silently discarded; the GPU rendered the SAME frozen, raw, per-bone-local (unposed) snapshot every frame — the "crumpled blob" (each body piece stacked near its own bone-local origin instead of being assembled into a coherent pose) |

**The `EHM_STREAM` hint was aspirational but inert**: the code's own comment ("vertices
change every frame via skeletal skinning") shows the intent was a per-frame-updating
buffer, but `gles2CreateStaticHWBuffer`'s name and implementation are unconditionally
static — the hint is never consulted by the upload path actually used.
`gles2CreateStaticHWBuffer` IS the correct call for genuinely static geometry (and is
used that way, correctly, by production's own `renderer_zone_draw.cpp` for ZONE
geometry, and by the benchmark's own static race-mesh preload loop) — it was simply the
wrong call for a per-frame-mutating animated instance mesh. `grep`-confirmed production
(`src/client/graphics/*.cpp`, `src/client/graphics/eq/*.cpp`) never calls
`gles2CreateStaticHWBuffer`/`createStaticHardwareBuffer` for any entity mesh — only for
zone geometry — so this was never a production code path; it is 100% benchmark-only
construction/wiring, exactly the plan's premise.

### 2. Fix

`tools/hwcaps/zone_bench_loader.cpp`: removed the `setHardwareMappingHint(EHM_STREAM/
EHM_STATIC, ...)` + `uploadMeshHWBuffers()` block for the per-instance animated
`instanceMesh_` in the `config.animate` entity-setup loop. No production code was
touched for this fix (per the plan's "prefer calling production entry points" — the
benchmark already called the right entry points; the only change needed was to STOP
doing something extra and harmful after them). Left a detailed comment explaining the
mechanism so a future perf-minded contributor doesn't reintroduce the same freeze while
trying to speed up entity draws — a real per-frame-updating dynamic buffer is possible
but was not needed here and is out of this unit's scope.

Also (small, additive, evidence-oriented): wired `--dump-png` into
`--composite-entity-test` mode in `gles2_zone_benchmark.cpp` (it previously had no
screenshot capability at all), used to capture the differential-test evidence above and
now available for future units.

### 3. Validation (per the P08d lesson)

- **Coherent poses across races/phases**: captured HUM, BAM, ELF/HAF-ish (half-elf),
  and a barbarian at close range across frames 15/25/35/45 — all show readable
  walk/kick/idle poses with correct proportions and silhouette, matching the quality of
  the gates videos' look. Evidence: `benchmarks/contact_sheets/p12b_animated/
  animate_fixed_f15.png`, `_f25.png`, `_f35_barbarian_closeup.png`, `_f45.png`,
  `composite_entity_test_hum_huf_animated.png`; `BEFORE_crumpled_f15.png` and
  `static_control_f15.png` kept for contrast.
- **Static byte-identity**: `--entities 6 --seed 42 --frames 60 --warmup 5
  --orbit-radius 15` (no `--animate`), frames 15/25, before vs after the fix:
  `PIL.ImageChops.difference` bbox = `None` at both frames (0 differing pixels). The fix
  does not touch the static path at all (it only removes a call inside the
  `config.animate` block).
- **Full suite**: `cmake --build build -j24` clean. `ctest --output-on-failure -j4`
  (`build/`): 1234/1235 passed. One known host exception
  (`AudioManagerTest.PreloadCommonSounds`, ALSA/no audio device in this sandbox — matches
  P12's own documented exception). `HotkeyJsonLoadTest.WarnsOnInvalidKeyName` failed once
  under `-j4` parallel execution in an earlier run of this same suite, passed cleanly
  when re-run in isolation and did not recur on the final full run — the same
  pre-existing parallel-execution flake class P12 already documented for a sibling
  Hotkey test, zero source changes, not investigated further.
- **`ModelAnimatesProperly` now runs**: see section 6 below — it runs and passes as
  `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless` (ctest #824).
- `scripts/build-gles2-x86.sh`: clean. `visual_gate` (`build/`, `DISPLAY=:97`): **PASSED**
  (unlike P12's run, which hit a foliage-geometry diff the P12 review already
  characterized as a pre-existing, zero-source-diff environment condition unrelated to
  entities — not reproduced here).
- ARM cross-build (`scripts/build-arm-noble.sh`): clean, `gles2_zone_benchmark` and
  `willeq` both produce valid ARM ELF binaries (`build-arm-noble/bin/`).
- File-size check: `build/bin/willeq` 72.9MB, `build-gles2/bin/gles2_zone_benchmark`
  14.2MB, `build-arm-noble/bin/willeq` 353MB (unstripped, debug symbols retained per the
  ARM build's own default) — no anomalies (no near-empty or runaway-bloat binaries).

### 4. Z35 verification: composite vs multi-buffer UNDER ANIMATION

The deferred P08d/P12 deliverable, finally attemptable now that animation is
trustworthy. Method: `--dump-path` on a baseline (multi-buffer) animated run to record
the exact camera trajectory, then `--path-file` replay on a
`--composite-entities --composite-scenegraph --eqe1-path /mnt/projects/EverQuest_ETC1`
animated run — byte-identical camera trajectory AND (since entity placement samples the
same resolved per-frame trace) byte-identical entity positions; only the mesh-building
path (multi-buffer vs composite-atlas) differs. Same `--entities 6 --seed 42 --frames 60
--warmup 5 --orbit-radius 15` setup as the coherence evidence above.

**Result: NOT identical — a bounded, low-magnitude structural diff.**

| frame | differing px % | mean abs Δ (0-255) | max channel Δ |
|---|---|---|---|
| f15 | 17.206% | 0.108 | 128 |
| f25 | 29.305% | 1.463 | 215 |
| f35 | 25.973% | 2.151 | 227 |

This is reported, not rationalized, per the plan's instruction. Visual inspection
(close crops of both figures at f25, `benchmarks/contact_sheets/p12b_animated/
p12b_z35_{baseline,composite}_f25.png`, diff at `p12b_z35_diff_f25.png`) shows: **pose,
position, and silhouette are identical** between multi-buffer and composite — same
stride, same limb placement, same framing. The differing pixels are concentrated at
material/lighting edges and one visible thin dark seam near a body-part texture
boundary on the composite render, consistent with the composite texture atlas
introducing minor UV-boundary/mip-filtering discontinuities (an expected, previously
undocumented-under-animation cost of "same texels, repacked" — P08d's own static
comparison found byte-identical composite-vs-baseline, so this seam-level effect is
either animation-specific — per-vertex normals recomputed every frame interact with the
atlas's different UV seams differently than the static bind pose — or was always
present and only became visible once animation stopped being unusable-as-evidence). Not
further root-caused within this unit's budget (this is a texture-atlas-fidelity question
closer to P08d's domain than P12b's node-wiring mandate); flagged for a follow-up unit.
Draw-call count dropped as expected (avg 47 → 22 per frame), confirming the composite
path's core value proposition is unaffected.

### 5. VertexMapping patch disposition: LANDED

`refactor/scratch/P08d_vertexmapping_one_to_many.patch` applies cleanly on top of the
P12b tree. Re-tested against the now-valid animated evidence (same 4 frames/races as
section 3): **byte-identical output with and without the patch** (0 differing pixels,
every frame) — the same "zero visible effect" verdict P12 found, now on a
correctly-matched coherent-vs-coherent comparison instead of P12's own
garbled-vs-garbled one. Static byte-identity and `visual_gate` PASS were re-confirmed
with the patch applied; full `ctest` re-run clean (1234/1235, same known exception);
`build-arm-noble.sh` cross-compiles clean with the patch applied (it touches 4 shared
production files: `animated_mesh_scene_node.h/.cpp`, `race_model_loader.h`,
`mesh_building.cpp`).

Given zero regressions across every check, and a real (if narrow — UV-seam duplicate
vertices are a minority of any mesh) correctness fix underneath that has now been
independently re-derived as sound by two separate units (P12, P12b), this unit **landed**
the patch rather than leaving it parked a third time. `refactor/P08d_npc_composite_landing.md`
and `refactor/P12_animate_root_cause.md` both annotated with a closing pointer to this
disposition.

### 6. `ModelAnimatesProperly` test repair

All six `InventoryModelViewTest` fixtures need a live EQEmu server + DISPLAY + the
paperdoll `WindowManager`/`CharacterModelView` UI classes, which have been deleted —
genuinely un-runnable in CI without a UI rewrite (explicitly out of scope). Rather than
touch those (left as documented, self-skipping tests — `ModelAnimatesProperly`'s skip
message now also points at its headless replacement), added a NEW fixture,
`RaceModelAnimationHeadlessTest::ModelAnimatesProperly_Headless`
(`tests/test_inventory_model_view.cpp`), that exercises the same underlying mechanism
the paperdoll view renders through — `RaceModelLoader::createAnimatedNode()` and
`EQAnimatedMeshSceneNode::OnAnimate()`/`applyAnimation()` — via an `EDT_NULL` Irrlicht
device (no window, no GPU, no `DISPLAY`, no server; same pattern as
`test_door_loading.cpp`'s `DoorManagerGraphicsTest`). It plays an animation on HUM and
BAM, steps `OnAnimate()` across 4 synthetic frames, and asserts (a) the bounding box
stays within a plausible humanoid envelope every frame (guards the collapsed/exploded
class of bug) and (b) a tracked vertex actually moves both from its bind pose and
between consecutive frames (guards the exact "frozen at one pose" class of bug this unit
fixed). **Sanity-checked the test's own validity**: temporarily short-circuited
`applyAnimation()` to a no-op (simulating a frozen node) — the new test correctly FAILED
with the expected "model appears frozen" messages; reverted before rebuilding (confirmed
zero net diff via `git diff` on that file before continuing). Also flipped
`tests/CMakeLists.txt` to `GTEST_DISCOVER_TESTS(test_inventory_model_view)` (previously
not discovered at all, so NOTHING in this file — including the always-skipped fixtures
— ever ran in CI); the six server-dependent tests report SKIPPED, the new one runs and
PASSES (`ctest -R RaceModelAnimationHeadlessTest`, confirmed both standalone and inside
the full suite run, `DISPLAY` unset).

### 7. Pi phase: SKIPPED

Not attempted. Steps 1-6 consumed this unit's budget on a genuinely deep,
multi-hypothesis investigation (the construction diff was not obvious from static
reading alone — confirmed empirically via a differential test between the benchmark's
two independent animated-entity code paths, then traced to a GLES2-driver-specific
static-hardware-buffer registration bug). Per the plan's explicit allowance ("If
skipped, state so — becomes P11b"): skipped, stated here. The true composite
under-animation performance magnitude (P08c's unreliable number, P11's wrong-route ~0%)
remains open for a follow-up unit (P11b).

### Deviations from the plan

- Landed the parked `VertexMapping` patch (production code:
  `animated_mesh_scene_node.h/.cpp`, `race_model_loader.h`, `mesh_building.cpp`) — the
  plan's "out of scope" section permits this explicitly when step 5 evidence justifies
  it, which it does (zero regressions, independently re-derived correctness fix, closes
  a loop three units have now carried forward).
- Added `RaceModelAnimationHeadlessTest` to `tests/test_inventory_model_view.cpp` and
  changed that binary from "never discovered by ctest" to discovered — a slightly larger
  test-infrastructure change than a minimal patch, but directly what plan step 6 asked
  for ("a runnable variant of the assertion at whatever level IS testable headlessly").
- Small additive change to `gles2_zone_benchmark.cpp` (`--dump-png` support inside
  `--composite-entity-test` mode) — used as the differential-test evidence tool in
  section 1 and left in place since it has no effect on any other mode and restores
  evidence-capture capability the plan's motivation section cares about.
- Z35 verification did not come back "identical or near" as hoped — reported as a
  bounded, likely-cosmetic (texture-atlas-seam-level) structural diff rather than
  rationalized away; flagged for a follow-up unit closer to P08d's texture-atlas domain.
- Pi phase skipped per the plan's own optionality — becomes P11b.
- No STOP fired: a candidate delta was found (the construction diff table above), so the
  plan's STOP condition (no candidate delta after thorough enumeration) never applied.
