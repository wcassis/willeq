# P08 — Draw-call/CPU submission investigation (befallen tail poses)

Approved by Will 2026-07-25 ("go ahead with P08").
Slot: wt/slot1, branch task/P08-draw-calls, cut from fable (f502298).
Investigation unit (P05 style): root-cause and quantify, prototype at most
ONE lever behind a flag, REPORT — no default/architecture changes.

## Motivation

With the lighting arc closed (docs/decisions.md 2026-07-24 + 2026-07-25
addendum), the 30fps KPI gap is CPU-bound draw submission. Current blessed
tier-2 baseline (Q03: count mode + GLS level 9, benchmarks/pi_baseline/):
qeynos2 32.4/36.6 fps p95 (ABOVE KPI), gfaydark 25.7 (rgba) / 30.3 (eqe1),
befallen 25.3/26.5 — befallen is the worst and gfaydark-rgba second.
P06-era evidence: befallen tail frames run ~368 draws with tiny
polys-per-draw at ~26.5ms CPU, and the software occlusion culler is
near-blind in dungeons (single-digit occlusions out of ~156 visible
regions). Nothing has ever decomposed WHERE the CPU milliseconds go per
draw — that is this unit.

Everything here runs under the production config (manifest
productionConfig: count mode, budget 20, --player-light-level 9) and, for
moving runs, REPLAYS the canonical befallen path
(benchmarks/paths/befallen_seed42_1000f.path, --path-file) — first perf
unit with drift-free cross-unit-comparable moving data. Do NOT re-record
paths.

## Design

### A. Identify and pin the tail poses (new capability, use it)

1. x86 + Pi replay of the canonical befallen path (production config).
   From the Pi CSV, take the p95-and-worse frames (by total_ms), map frame
   index → path-file line → pose, and pin the 4-6 recurring worst poses as
   named statics (`bf<frameidx>` convention, like fr291/fr627). Record the
   pose list in the review — these become the standing befallen reference
   poses for all future units. Sanity: confirm draws/regions_vis at those
   poses roughly match the P06-era picture (~350+ draws).

### B. Decompose the frame (the core question: where do 26.5ms go?)

2. Instrument the benchmark's manual draw path with per-phase CPU timers,
   new CSV columns (always present; near-zero overhead — steady_clock
   reads, no syscalls): collect/cull phase (draw-entry collection incl.
   adaptive-lighting decisions), sort phase, submission phase
   (setMaterial + drawMeshBuffer loop), and everything-else (frame total
   minus phases). If the driver swallows time asynchronously, note what
   the submission phase does and does not capture (glFlush/eglSwapBuffers
   accounting — put swap/present in its own column).
3. Draw-composition histograms at the pinned poses (one-shot `--dump`-
   gated report, not per-frame): draws per region, polys per draw
   (distribution, not just mean), state changes, and how many draws are
   "tiny" (<32 polys). This answers WHY befallen is draw-heavy: BSP leaf
   granularity x per-material buffers is the hypothesis — verify it.
4. Per-draw fixed cost on the Pi (the number every lever's ROI hangs on):
   measure empirically with a controlled sweep — same total geometry
   split into N draws for several N (e.g. a `--split-draws N` test flag on
   one big region, or an extension of the existing micro-benchmark
   family). Deliverable: ms per 100 draws on Mali400/Lima at 800x600, and
   the same sweep on x86 llvmpipe for contrast (CPU-side driver cost vs
   GPU cost — use cpu_ms/gpu_ms columns to separate).

### C. Quantify the levers (measure ceilings, do not build them yet)

5. **Batching ceiling**: from the pose histograms, compute the draw count
   if same-material buffers were merged (i) within each region, (ii)
   across visible regions. Pure arithmetic from the histogram data — no
   implementation. Note the interactions a real batching lever must
   respect: per-region mesh cache/lazy loading, per-region adaptive-
   lighting material overrides, per-region occlusion/PVS culling
   granularity, front-to-back order within material groups.
6. **Occlusion headroom**: at the pinned poses, regions_occluded vs
   regions_vis (already CSV columns), plus a manual ground-truth estimate
   at 2-3 poses (how many visible regions contribute ZERO visible pixels —
   e.g. render with per-region color IDs at `--dump` time and count
   regions with no surviving pixels). Headroom = draws attributable to
   fully-hidden regions. Quantify only; occlusion redesign is its own
   future unit.
7. **Prototype AT MOST ONE lever**, and only if B4's fixed-cost number
   makes the ROI case decisive (>=20% predicted p95 improvement at the
   tail poses). Candidate order by expected cost/benefit: (a) submission-
   time index-merge of consecutive same-material draw entries from the
   same region (no VBO rebuild — check whether region buffers share VBOs
   or can use glDrawElements over concatenated index ranges; if buffers
   are separate VBOs this candidate dies cheaply, say so), (b) load-time
   merge of tiny same-material buffers within a region. Behind a flag
   (`--merge-draws` / config field, DEFAULT OFF everywhere). Measured on
   the Pi at the pinned poses + full canonical replay; byte-identical
   pixel check vs unmerged at the pinned poses (draw order within a
   material group changes are acceptable ONLY if pixels are identical;
   any pixel diff = report, do not rationalize).
8. STOP conditions: if the decomposition shows the CPU time is NOT in
   submission (e.g. it's in collection/sort/driver-swap), stop lever work
   and report — the ranked-levers list then targets whatever actually
   dominates. If prototype results are ambiguous, report without tuning.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  **visual gate PASS with goldens UNTOUCHED** (instrumentation must not
  change rendered output; new CSV columns are fine); scripts/
  build-arm-noble.sh; scripts/refactor/check_file_sizes.sh fable
  (gles2_zone_benchmark.cpp WARN is pre-existing; must not FAIL — if your
  additions push it near a limit, put the instrumentation report code in a
  new TU).
- Replay discipline: all moving runs via --path-file with the canonical
  befallen path; pose columns byte-identical to the path file (programmatic
  check); geometry columns byte-identical across instrumented vs
  uninstrumented builds (instrumentation changes nothing).
- Pi rules: P02 protocol via scripts/pi/pi_common.py, ALL timed-run I/O to
  Pi-local /tmp, stripped staged binary from slot1 (ARM_BUILD_BIN_DIR trap
  — env.sh defaults to main), drop_caches never reboot, <=12 invocations/
  session (2 sessions OK), leave Pi pristine, report blocked if
  unreachable.
- Review must contain: pinned pose list (frame idx + pose + why), the
  phase-decomposition table (x86 + Pi, tail poses + whole-run), draw
  histograms, ms-per-100-draws numbers, batching-ceiling arithmetic,
  occlusion-headroom counts, ranked lever list with predicted savings,
  prototype data if built (with the pixel-identity verdict), and a
  recommendation for the next unit.

## Out of scope

- Any default/preset/architecture change (report only; a landing unit
  follows if the data says so).
- Occlusion/PVS redesign (quantify headroom only).
- Entity/object draw paths (zone geometry only — entities are a separate
  submission path; note their draw counts in passing if free).
- Re-recording camera paths; touching the goldens; lighting work.

## Review

### Setup

All moving runs replay the canonical befallen path
(`benchmarks/paths/befallen_seed42_1000f.path`, `--path-file`,
`--entities 200 --seed 42 --frames 1000`, matching the path's own recording
command) under the production config (`--adaptive-lighting
--zone-pp-budget 20 --pp-budget-mode count --player-light-level 9`, the
manifest's `productionConfig` block), 800x600. x86 = llvmpipe via
`build-gles2/bin/gles2_zone_benchmark`; Pi = Orange Pi One (Mali400/Lima)
via a stripped binary built from this worktree's `build-arm-noble/`, P02
protocol (`refactor/scratch/p08_pi_session.py`, ad hoc script reusing
`scripts/pi/pi_common.py` primitives per the P05/P06/Q03 precedent, deleted
after the session). One Pi session, 2 invocations (well under the 12 cap),
no `SessionAbort`, CmaFree stayed in [62224, 78020] kB throughout (well
above the 40 MB floor), governor restored to `ondemand`, staged binary/path
file/scratch CSVs removed from `/tmp` after — Pi left pristine. Data:
`benchmarks/pi_runs/p08_draw_calls/`.

### A. Pinned tail poses

From the x86 canonical replay's 1000-frame CSV, the p95-and-worse frames
(total_ms >= 44.03ms) cluster into 19 distinct recurring pose groups (frame
proximity <=15 apart). Six were pinned, chosen for a mix of severity,
recurrence (cluster size), and structural diversity (dense city-like
dungeon corridor vs. sparse/entity-dominated poses) — these become the
standing befallen reference poses for future units, named `bf<frameidx>`
per the fr291/fr627 convention:

| pose | path line | pos (x,y,z) | heading | cluster size | why pinned |
|---|---|---|---|---|---|
| bf355 | 355 | (-663.9,-297.1,5.0) | 118.2 | 7 frames (350-397) | largest sustained cluster after bf791; mid-density corridor |
| bf472 | 472 | (-308.0,-235.2,5.0) | 41.1 | 4 frames (469-473) | single worst frame in the whole run (58.998ms total_ms x86); 704 visible regions, the densest pose measured |
| bf626 | 626 | (-379.8,207.8,5.0) | 299.5 | 3 frames (621-626) | anomalous: only 2 zone regions visible yet 47.29ms total_ms — turned out to be almost entirely entity-crowd cost, not zone geometry (see B's entity-contribution finding) |
| bf791 | 791 | (-808.9,13.5,5.0) | 190.8 | 8 frames (766-827) | largest cluster in the whole run; sustained mid-density room |
| bf917 | 917 | (-615.2,-318.2,5.0) | 107.7 | 4 frames (902-917) | moderate density with high occlusion activity (30 regions occluded) |
| bf987 | 987 | (-388.9,-298.8,5.0) | 61.5 | 5 frames (963-987) | second-densest pose (396 regions), second-highest zone draw count |

Sanity check (design point A1): draws/regions_vis at the pinned poses range
505-1470 combined (zone+entity) draws, 2-704 regions_vis — comfortably
above the P06-era "~368 draws" figure cited in the motivation (that number
predates this unit's entity-inclusive canonical-path config; see B's
entity-contribution finding for why direct comparison isn't apples-to-apples).
`regions_vis == regions_drawn` at every pinned pose (no budget cap was hit —
`--max-draws`/`--max-polys` are unset by default), confirming the region
counts above are the true PVS+frustum+occlusion-survived visible set, not an
artifact of an early budget cutoff.

Replay-discipline checks (verification requirement, full-file programmatic,
not spot checks):
- Pose columns vs the path file: **0/1000 mismatches** (x86 and Pi).
- Geometry columns (draws/state_changes/polys/regions_vis/regions_drawn/
  regions_occluded) instrumented vs. a from-scratch uninstrumented build of
  the pre-P08 commit (`git worktree add` at `fable~0` i.e. this branch's
  parent, same command/path): **0 diffs across all 1000 frames** —
  instrumentation changes nothing about rendered/collected geometry.
- Geometry columns x86 vs. Pi (cross-machine): **0 diffs across all 1000
  frames** — consistent with the Q01/Q03/P07d precedent that these columns
  are machine-independent.

### B. Frame decomposition — where do the milliseconds go?

**Headline: 87-96% of CPU draw time is in the submission phase
(`setMaterial`+`drawMeshBuffer` loop), on both machines, at every pinned
pose and in the whole-run average. Collection/culling, sorting, GPU sync,
and swap/present are all minor.** The STOP condition (design point 8) does
**not** trigger — CPU time is squarely in submission, so lever work is
justified.

**Whole-run phase decomposition (1000 frames, avg / p95 / max, ms):**

| phase | x86 llvmpipe avg | x86 p95 | x86 max | Pi Mali400/Lima avg | Pi p95 | Pi max |
|---|---|---|---|---|---|---|
| collect/cull | 2.715 | 4.437 | 10.599 | 2.800 | 4.384 | 5.707 |
| sort | 0.137 | 0.595 | 1.856 | 0.150 | 0.610 | 1.126 |
| **submission** | **20.822** | **36.668** | **45.854** | **65.638** | **192.247** | **357.840** |
| other (residual) | 0.068 | 0.114 | 4.092 | 0.268 | 0.688 | 4.691 |
| CPU draw total | 23.742 | 41.015 | 56.258 | 68.855 | 197.741 | 365.308 |
| GPU sync (glFinish) | 4.002 | 5.688 | 9.204 | 33.935 | 81.534 | 90.735 |
| swap/present (endScene) | 0.023 | 0.038 | 0.074 | 0.255 | 0.290 | 1.131 |
| **total frame** | **27.746** | **44.783** | **60.732** | **102.799** | **285.552** | **452.148** |
| avg FPS | 36.0 | | | 9.7 | | |

Submission is 87.7% of CPU-draw time (x86) / 95.3% (Pi), and 75.0% / 63.8%
of TOTAL frame time respectively (GPU sync is nontrivial on the Pi too, but
still smaller than submission at every pose). The Pi's p95 total_ms
(285.6ms, 3.5 fps) is far worse than x86's (44.8ms, 22 fps) — the same
Mali400/Lima CPU-side driver overhead per `drawMeshBuffer` call that shows
up as a steep material-switch penalty in the synthetic sweep (below) is the
mechanism.

**Per-pose phase decomposition (ms):**

| pose | x86 total | x86 submit | x86 collect | Pi total | Pi submit | Pi collect | draws (combined) | state_ch |
|---|---|---|---|---|---|---|---|---|
| bf355 | 24.496 | 17.827 | 1.843 | 69.059 | 47.483 | 2.517 | 582 | 495 |
| bf472 | 27.515 | 14.038 | 3.377 | 333.453 | 240.055 | 5.355 | 1470 | 186 |
| bf626 | 24.543 | 17.678 | 1.951 | 51.495 | 29.796 | 2.138 | 505 | 498 |
| bf791 | 25.914 | 19.149 | 2.102 | 98.058 | 58.635 | 2.654 | 654 | 495 |
| bf917 | 28.947 | 20.493 | 2.354 | 76.735 | 48.842 | 2.929 | 606 | 480 |
| bf987 | 43.145 | 27.856 | 4.912 | 191.288 | 128.350 | 4.114 | 1041 | 286 |

**Entity draws dominate the "draws" column at this config.** A parallel
`--entities 0` isolation run (same path/config, zone-only) shows combined
avg draws 699.3 vs. zone-only avg draws 253.0 — **entities are 63.8% of
combined draws** and **86.9% of combined submission time** (avg submission
20.822ms combined vs. 2.730ms zone-only, x86). bf626's anomaly (2 zone
regions but 505 combined draws, 47.29ms) is now explained: it's almost
entirely an entity-crowd pose, not a zone-geometry problem. Entity draw
paths are out of scope per the plan; noted in passing as instructed. All
histogram/batching/occlusion work below uses the zone-only isolation data
so the levers target zone geometry specifically.

### B3. Draw-composition histogram (zone-only, at the 6 pinned poses)

Confirms the BSP-leaf-granularity x per-material-buffer hypothesis
(design point 3): regions are tiny (1-5 draws each, mean 1.46-1.93 —
i.e. most regions reference only 1-2 distinct materials), draws are
overwhelmingly tiny (<32 polys), and per-draw poly counts are minuscule
(mean 3.0-6.4 polys/draw, p95 8-17, max 21-48).

| pose | regions_drawn | zone draws | draws/region (min/mean/p50/p95/max) | polys/draw (min/mean/p50/p95/max) | tiny draws (<32 polys) |
|---|---|---|---|---|---|
| bf355 | 60 | 92 | 1 / 1.53 / 1 / 3 / 3 | 1 / 6.04 / 4 / 17 / 48 | 91/92 (98.9%) |
| bf472 | 704 | 1297 | 1 / 1.84 / 2 / 3 / 4 | 1 / 3.42 / 3 / 8 / 21 | 1297/1297 (100.0%) |
| bf626 | 2 | 6 | 3 / 3.00 / 3 / 3 / 3 | 2 / 3.00 / 3 / 4 / 4 | 6/6 (100.0%) |
| bf791 | 100 | 165 | 1 / 1.65 / 1 / 3 / 5 | 1 / 5.57 / 4 / 14 / 36 | 164/165 (99.4%) |
| bf917 | 89 | 130 | 1 / 1.46 / 1 / 3 / 3 | 1 / 6.37 / 4 / 17 / 48 | 129/130 (99.2%) |
| bf987 | 396 | 763 | 1 / 1.93 / 2 / 3 / 4 | 1 / 4.05 / 3 / 10 / 48 | 762/763 (99.9%) |

Poly-count buckets are almost entirely `[0-7]` (72-1222 of each pose's
draws), a small tail in `[8-15]`/`[16-31]`, essentially nothing >=32 polys.
Full histograms: `benchmarks/pi_runs/p08_draw_calls/befallen_{x86,pi}_histogram.csv`
(byte-identical between machines, confirming this is a geometry property,
not a driver artifact).

### B4. Empirical per-draw fixed cost (`--synthetic-draws`)

Marginal cost (100->1000-draw slope, ms per 100 draws):

| | same-material (no state change) | material switch per draw |
|---|---|---|
| x86 llvmpipe | 0.141 ms/100 draws | 0.349 ms/100 draws |
| Pi Mali400/Lima | 2.146 ms/100 draws | 12.548 ms/100 draws |

The Pi's per-draw cost is 15-36x x86's, and material-switch overhead alone
(the delta between the two rows) is ~10.4ms/100 switches on the Pi — this
is the CPU-side driver/state-validation cost the "Mali 400 Shader
Optimization Rules" section of `src/client/graphics/CLAUDE.md` documents
for FS branches; the same driver-overhead pattern shows up for draw
submission generally, not just shader branching.

**Caveat**: the synthetic test uses one tiny (13-vertex, 7-poly) buffer
repeated N times — real zone mesh buffers carry actual VBO/texture bind
state the synthetic test doesn't isolate. The REAL effective rate,
computed directly from measured frames (`submission_ms / draws * 100`),
runs higher on the Pi at several pinned poses:

| pose | x86 effective ms/100 draws | Pi effective ms/100 draws |
|---|---|---|
| bf355 | 3.063 | 8.159 |
| bf472 | 0.955 | 16.330 |
| bf626 | 3.501 | 5.900 |
| bf791 | 2.928 | 8.966 |
| bf917 | 3.382 | 8.060 |
| bf987 | 2.676 | 12.329 |

(bf472's real rate is *lower* than most other poses despite being the
worst-total pose — its draws are almost all same-material runs, i.e. very
few of the 186 state changes are spread across 1470 draws, so it pays
mostly the same-material rate, not the switch rate; its total cost is high
purely from sheer draw *count*, not per-draw cost.) The synthetic sweep's
"same-material" row (0.141-2.146 ms/100) is a reasonable floor; real-pose
rates land nearer the synthetic "material switch" row or above it,
confirming real driver overhead (texture/VBO bind validation) exceeds the
isolated synthetic worst case. Both numbers are used below: the synthetic
number for a conservative floor, the measured effective rate for a
representative ROI estimate.

### C5. Batching ceiling arithmetic (zone-only, pure arithmetic, not implemented)

**Within-region same-material merge**: structurally near-zero ceiling.
`draws_per_region` (B3 table) is already 1.46-1.93 — since one
`IMeshBuffer` is built per *distinct material* within a region (confirmed
architecturally: `zone_bench_loader.cpp`'s per-region mesh build calls
`buildTexturedMesh`/`buildColoredMesh` once per region, producing one
`IMeshBuffer` per material the region references), `draws_per_region`
already approximates "distinct materials per region," not incidental
duplication. A *naive upper bound* ignoring material identity
(`draws_total - regions_drawn`, i.e. assuming every draw in a region could
merge) gives 32-593 draws (34.8-48.1% reduction) at the pinned poses — but
this bound is almost certainly not achievable, since it would require two
`IMeshBuffer`s of the *same* material in one region, which the mesh-build
convention doesn't produce. Realistic within-region ceiling: **near zero**.

**Cross-region same-material merge**: the real opportunity. `state_changes`
(zone-only, material-sorted draw order — the count of distinct material
*groups* in the frame) is the exact ceiling: if every same-material buffer
across ALL visible regions could concatenate into one draw call (index-range
merge over a shared VBO), draws would drop to `state_changes`:

| pose | zone draws | cross-region ceiling (=state_changes) | reduction |
|---|---|---|---|
| bf355 | 92 | 8 | 91.3% |
| bf472 | 1297 | 14 | 98.9% |
| bf626 | 6 | 3 | 50.0% |
| bf791 | 165 | 10 | 93.9% |
| bf917 | 130 | 8 | 93.8% |
| bf987 | 763 | 11 | 98.6% |
| **aggregate** | **2453** | **54** | **97.8%** |

This is a real, large ceiling — but is **architecturally out of proportion
for a flagged submission-time or load-time-within-region prototype** (the
two candidates design point 7 names). Interactions a real cross-region
batching lever must respect, confirmed from the codebase (per the plan's
own request):
- **Region mesh buffers are separate static VBOs, not shared/pooled**
  (confirmed: `uploadMeshHWBuffers()` calls `gles2CreateStaticHWBuffer()`
  once per `IMeshBuffer`, one independent `GL_STATIC_DRAW` buffer object
  per region x material — `zone_bench_loader.cpp`). A real merge needs a
  **load-time mesh-merge/rebuild pass** producing new shared VBOs, not a
  submission-time trick — a genuine GPU-resource-layout change.
- **Per-region mesh cache / lazy loading** (`ConstrainedMeshCache`,
  production-side): merging buffers across regions breaks per-region
  eviction — a merged buffer can't be partially evicted when only one of
  its constituent regions falls out of the cache's budget.
- **Per-region adaptive-lighting material overrides** (count/area/radius
  modes all decide per-region per-pixel-vs-lightweight): a cross-region
  merged buffer can't have two different regions in different lighting
  states within one draw call — the merge would have to be re-computed (or
  fall back to unmerged) whenever a region's lighting decision differs from
  its merge-mates', which happens constantly as the camera moves.
- **Per-region occlusion/PVS granularity**: today occlusion drops whole
  regions from the visible set before collection; a merged cross-region
  buffer would draw ALL its constituent regions' geometry even if some are
  individually occluded, unless occlusion culling is re-done at
  sub-buffer granularity (defeats the point of occlusion).
- **Front-to-back order within material groups**: today draws within a
  material group still preserve distance order (secondary sort key) for
  early-Z; a single merged draw call loses per-sub-region ordering
  entirely.

**Candidate (a) (submission-time index-merge of consecutive same-region
same-material entries, no VBO rebuild) dies cheap, exactly as the plan
anticipated**: region buffers are confirmed separate VBOs (no
`glDrawElements` over a concatenated range is possible without a real
merge), AND `draws_per_region` is already near 1, so there's essentially
nothing to merge within a region in the first place.

**Candidate (b) (load-time merge of tiny same-material buffers WITHIN a
region) also has near-zero ceiling**, for the same reason: one
`IMeshBuffer` per distinct material per region already IS the merged state
at region granularity — there's no redundant same-material duplication
within a single region left to merge.

**Conclusion: neither of the plan's two named candidates clears the 20%
p95 bar (both have ~0% realistic ceiling in this zone).** The lever with
real ceiling (cross-region merge) is out of scope for a flag-gated
prototype in an investigation unit — see the recommendation below.

### C6. Occlusion headroom

`regions_occluded` vs `regions_vis` (existing CSV columns, production-capped
occlusion pass, `maxOccluderRegions=16`):

| pose | regions_vis (post-PVS+frustum, pre-occlusion) | regions_occluded (capped) |
|---|---|---|
| bf355 | 89 | 29 |
| bf472 | 766 | 62 |
| bf626 | 2 | 0 |
| bf791 | 100 | 0 |
| bf917 | 119 | 30 |
| bf987 | 480 | 84 |

Manual ground-truth estimate (design point 6): rather than a GPU per-region
color-ID render (the built-in GLES2 debug shader modes — `zone_shader.cpp`
`FRAGMENT_SHADER_DEBUG_SRC`, 12 numbered modes — have no flat/arbitrary
per-draw-color mode to hijack safely without touching vertex data/adding
shader code, judged too risky/out-of-proportion to build for a 2-3-pose
diagnostic), a CPU-side ground truth was computed instead
(`computeOcclusionHeadroomUncapped`, `zone_bench_histogram.cpp`): re-runs
the exact same PVS+frustum+occlusion algorithm but with **no
`maxOccluderRegions` cap** (occluders from every nearer region, not just
the nearest 16), giving a legitimate "how many more regions would a bigger
occluder budget catch" ground truth, reusing the tested
`SoftwareOcclusionCuller` API directly (same depth-buffer rasterize/testAABB
calls production uses, just uncapped):

| pose | regions_vis | occluded (capped, production) | occluded (uncapped) | headroom |
|---|---|---|---|---|
| bf355 | 89 | 29 | 29 | 0 |
| bf472 | 766 | 62 | 287 | **225** |
| bf626 | 2 | 0 | 0 | 0 |
| bf791 | 100 | 0 | 4 | 4 |
| bf917 | 119 | 30 | 47 | 17 |
| bf987 | 480 | 84 | 152 | 68 |

bf472 (the densest pose) has substantial occlusion headroom — 225
additional regions could be culled with a larger occluder budget, worth
~225*1.84≈414 fewer zone draws (using bf472's draws/region mean) *before
submission even runs* (i.e. this is a collect-phase saving, complementary
to the cross-region merge lever, not overlapping with it). Occlusion
redesign is explicitly out of scope for this unit (quantify only, per the
plan) — flagged as a candidate for a future unit, separate from the
draw-merge thread.

### C7/C8. Prototype lever

**No prototype was built.** Per design point 7's candidate order ((a)
submission-time same-region merge, (b) load-time within-region merge),
both candidates were evaluated against the >=20% predicted p95 improvement
bar and **both have ~0% realistic ceiling** in befallen (C5 above) — the
region granularity here is already fine enough (BSP splits into hundreds
of small regions, each touching only 1-2 of a shared ~8-14-material
palette) that there is essentially no same-region duplication left to
merge. Building a token prototype of either candidate to "check the box"
would be dishonest given the arithmetic says it can't move the needle;
the plan's own STOP-condition spirit ("if prototype results are ambiguous,
report without tuning") is extended here to "if no in-scope candidate
clears the bar, don't force one."

### Ranked lever list (predicted savings, C5/C6 arithmetic)

1. **Cross-region same-material draw merge** (load-time mesh/VBO
   restructuring) — 87-99% zone-draw reduction at the pinned poses (97.8%
   aggregate), the dominant lever by a wide margin. NOT buildable as a
   small flag; needs a proper design unit (new shared-VBO layout,
   reconciled with mesh cache eviction granularity, adaptive-lighting
   per-region overrides, and occlusion/PVS granularity — C5's interaction
   list). **Recommended as the next unit** (a dedicated design/prototype
   unit, not a report-and-flag investigation).
2. **Occlusion culling budget increase** (raise/adaptive-size
   `maxOccluderRegions`) — up to +225 regions at the densest pinned pose,
   0 at several others (pose-dependent, works best in dense-region poses
   like bf472/bf987). Explicitly out of scope to prototype here (occlusion
   redesign is its own future unit per the plan) but cheap and
   low-architectural-risk relative to lever #1 — a natural companion/
   interim unit if #1's timeline is long.
3. **Entity draw-path reduction** (out of scope for this unit, noted in
   passing per the plan) — entities are 63.8% of combined draws and 86.9%
   of combined submission time in this canonical-path config
   (`--entities 200`). The benchmark already has `--batch-entities`/
   `--composite-entities`/`--lod-entities` variants built for exactly this
   (P05/prior units); worth revisiting whether any of those are production-
   applicable, since the entity submission cost now dwarfs the zone-geometry
   cost this unit investigated. Flagging for orchestrator visibility, not
   claiming it as this unit's finding.
4. **Within-region merge (candidates a/b as scoped)** — dead, ~0%
   realistic ceiling, do not pursue as separately scoped work.

### Recommendation for the next unit

A dedicated **cross-region same-material draw-merge design unit**: design a
load-time mesh-merge pass that concatenates same-material index/vertex data
across visible regions into shared VBOs, explicitly reconciling the four
interactions listed in C5 (mesh cache eviction granularity, adaptive-
lighting per-region overrides, occlusion/PVS granularity, front-to-back
ordering within material groups) — likely requiring either (a) a
coarser-than-BSP-region "merge cell" abstraction the mesh cache/adaptive-
lighting/occlusion systems all key off of instead of raw regions, or (b)
accepting some of the ceiling's upside (e.g. merge only within
statically-grouped clusters of nearby regions, capping the interaction
blast radius). This is real design work, not a flag — budget it as such.
Companion/interim candidate: an occlusion-culling-budget unit (lever #2)
if the merge unit's timeline is long, since it is architecturally much
smaller and has real (if pose-dependent) headroom.

### Verification

- `cmake --build build -j24`: clean (both mid-unit and final).
- `ctest --test-dir build --output-on-failure`: 1223/1225 passed (final
  run). Failures: `AudioManagerTest.PreloadCommonSounds` (documented host
  exception) and `RDPServerFrameTest.UpdateFrameMultipleTimes` (SEGFAULT,
  the other documented host exception — intermittent per prior units'
  reviews). `visual_gate` (test #1225) **PASSED**.
- `scripts/build-gles2-x86.sh`: clean.
- Visual gate (`scripts/ci/visual_gate.py`, no `--bless`): **PASS**,
  goldens untouched (this unit adds CSV columns and a new `--dump-frames`-
  gated report only, no rendering-path or default changes).
- `scripts/build-arm-noble.sh`: clean, final binary used for the Pi session.
- `scripts/refactor/check_file_sizes.sh fable`: exit 0.
  `tools/hwcaps/gles2_zone_benchmark.cpp` 2673 lines (WARN, pre-existing).
  `tools/hwcaps/zone_bench_loader.cpp` 2996 lines (WARN — this unit's
  additions pushed it from 2973 to a peak of 3081/FAIL; relocated the
  occlusion-headroom diagnostic to the new `zone_bench_histogram.cpp` TU to
  bring it back under the 3000 FAIL threshold with margin, verified
  byte-identical output before/after the move). New TU
  `zone_bench_histogram.cpp`/`.h` (206/67 lines) holds all P08 report/
  diagnostic code, per the plan's own instruction.
- Replay discipline: pose columns 0/1000 mismatches vs. the path file
  (x86 and Pi); geometry columns 0 diffs instrumented vs. uninstrumented
  build (full-file, all 1000 frames); geometry columns 0 diffs x86 vs. Pi
  (full-file, all 1000 frames).
- Pi rules: P02 protocol via `scripts/pi/pi_common.py` primitives, all
  timed-run I/O to Pi-local `/tmp`, stripped staged binary from this
  worktree's `build-arm-noble/bin` (`ARM_BUILD_BIN_DIR` explicitly
  overridden — the documented trap), `drop_caches` not needed (CmaFree
  never approached the floor), 2 invocations in 1 session (well under the
  12/session, 2-sessions-OK budget), Pi left pristine (staged binary/path
  file/scratch CSVs removed, governor restored to `ondemand`).

### Deviations from the plan

- **Occlusion ground truth (design point 6)**: the plan's suggested method
  ("render with per-region color IDs at `--dump` time") was not built — the
  GLES2 driver's built-in shader debug modes have no flat/arbitrary
  per-draw-color mode to safely repurpose without adding new shader/vertex-
  data plumbing, judged out of proportion for a 2-3-pose diagnostic. Used
  a CPU-side uncapped-occluder-budget re-run instead (reusing the tested
  `SoftwareOcclusionCuller` API), which answers the same underlying
  question ("how many more regions contribute zero visible pixels than the
  production pass finds") with less implementation risk. Documented as a
  deviation per the plan's instruction to flag it, not silently substitute.
- **No prototype lever built**: per C7/C8, neither of the plan's two named
  candidates clears the 20% p95 bar in this zone (both ~0% realistic
  ceiling) — reported per the plan's "only if the ROI case is decisive"
  gate and STOP-condition spirit, rather than forcing a token prototype.
- **Whole-run avg/p95 numbers differ from the motivation's cited P06-era
  figure** ("befallen ~368 draws / 26.5ms CPU"): this unit's canonical-path
  replay (`--entities 200`, matching the path file's own recording command)
  shows materially worse numbers (avg 699 combined draws, x86 p95 total
  44.8ms, Pi p95 total 285.6ms) — traced to entities now contributing 63.8%
  of combined draws and 86.9% of combined submission time in this specific
  config, which the P06-era number's measurement context likely didn't
  include at the same entity density. Not chased further (would need the
  P06-era run's exact config, not recorded in that unit's review) — flagged
  as a data-provenance note, not a contradiction of this unit's findings
  (the zone-only isolation run's numbers are what the batching-ceiling
  arithmetic actually uses).
- `refactor/scratch/p08_pi_session.py` (the ad hoc Pi driver script) was
  deleted after the session per the P05/P06/Q03 precedent of not committing
  single-unit investigation drivers into `pi_common.py`/committed scripts.
