# P07d — Radius-based per-pixel demotion (lossless by construction)

Approved by Will 2026-07-24. Slot: wt/slot2, branch task/P07d-radius.
Supersedes the P07c flip (branch task/P07c-default-flip parked UNMERGED —
its harness-alignment and baseline work will be reused when the flip is
redone on top of this unit).

## Motivation (P07c orchestrator review)

The area+floor mode blacks out frame-dominant geometry at ordinary
viewpoints (see benchmarks/contact_sheets/p07c_flip/ on the parked branch:
qeynos2 f0 building, f2 pool room) — the floor protects the nearest region
by DISTANCE, but the structure dominating the screen can be region #2-3,
by which point region 1's full-screen fallback has spent the budget.
P07b's city savings were partly earned by unlighting visible geometry.

The correct criterion falls out of the shader architecture: the per-pixel
and lightweight zone shaders differ ONLY in the player light (light[0] is
the sole per-pixel term; the other 7 point lights are per-vertex in both
paths). The player light has finite attenuation. A region lying entirely
beyond the light's effective range renders (near-)identically under either
shader — so demoting exactly those regions is visually lossless BY
CONSTRUCTION, with no screen-area estimate, no k, no floor, no near-plane
fallback.

## Design

1. **New mode**: `AdaptiveLightingBudgetMode::Radius` (third enum value;
   JSON string "radius"; benchmark `--pp-budget-mode radius`).
2. **Demotion rule**: region → lightweight iff the closest point of its
   world-space AABB to the PLAYER LIGHT position is beyond the light's
   effective range R. Compute R once per frame from the actual light[0]
   attenuation parameters as the distance where the maximum possible
   contribution falls below ~1/255 (account for the wrap-lighting term's
   direction-independent floor — wrap `(NdotL+0.5)/1.5` has a minimum of
   1/3, so use max contribution = attenuation * 1.0, conservative).
   Config: `adaptiveLightingRadiusMargin` (float, default 1.0) multiplies
   the computed R for headroom/tuning.
3. **Correctness-critical**: use CLOSEST-POINT-of-AABB distance, NOT
   region-center distance — the huge outskirts region's center is far
   away while its geometry is underfoot; center-distance would wrongly
   demote it and reintroduce the black-ground artifact. Check what
   `sortedZoneDrawList_` entries' `distanceSq` actually is (computed in
   sim_visibility.cpp) before reusing it; compute closest-point distance
   from the AABB if it is center-based.
4. **Light position/params source**: find where the renderer sets the
   per-pixel player light (zoneShader_ / light[0] uniforms) and read the
   same position + attenuation values the shader will use this frame.
   In the benchmark, mirror from its own player-light setup
   (zone_bench_loader hard-codes per-pixel lighting; find its light[0]).
5. `adaptiveLightingZoneBudget > 0` continues to gate adaptive lighting
   on/off; in radius mode the count value itself is unused (document in
   the field comment). Count and area modes untouched. Defaults stay
   `count` everywhere — no preset flips in this unit.
6. **Report the computed R and the demoted-region fraction** per pose in
   the review. If R turns out to cover the whole scene (tiny attenuation
   → no savings) or nearly nothing, STOP and report for orchestrator
   review rather than tuning unilaterally.

## Verification

- Build + ctest (known host exceptions) + x86 GLES2 build + visual gate
  PASS unchanged (defaults untouched) + ARM build + file-size check.
- **Losslessness check (the headline claim), x86 llvmpipe**: render the 9
  golden-manifest views with count-mode (production today, budget 20) vs
  radius-mode. Target: pixel-identical or near-identical (report max
  per-view pixel delta; small differences confined to the range boundary
  are acceptable — quantify them). Also render the P06/P07 contact-sheet
  poses (fr291/509/627/634 static poses in refactor/P07_pp_hybrid_floor.md)
  count vs radius. Contact sheets → benchmarks/contact_sheets/p07d_radius/
  (git add -f).
- **Pi measurement (P02 protocol, same qeynos2 config lineage:
  --entities 200 --seed 42, 1280x720)**: count vs radius vs all-LW.
  Moving-path runs for whole-run averages + static-pose captures for the
  P07 plan's 6 poses (path drift expected — 5 prior occurrences; static
  fallback is standard). Acceptance:
  - Anomaly poses (fr291/292): radius within noise of count (~17-18ms —
    the huge region is in light range and must stay per-pixel).
  - City poses (fr627/631/634): radius captures a majority of the
    area-floor1 saving measured in P07b (-19 to -24% vs count); report
    the fraction. If it captures little (most regions within R), that is
    a finding, not a failure — report for review.
  - Whole-run avg: radius <= count.
  - draws/regions_vis/regions_drawn/polys byte-identical count vs radius
    (full-file programmatic check).
  - CSVs → benchmarks/pi_runs/p07d_radius/ (git add -f).
- Pi rules: P02 protocol via scripts/pi/pi_common.py, /tmp-only I/O,
  stripped staged binary (ARM_BUILD_BIN_DIR trap — stage the slot2-built
  binary), drop_caches not reboot, leave Pi pristine, report blocked if
  the Pi goes unreachable.

## Out of scope

- Preset flips / golden re-bless / tier-2 re-capture (redone on top of
  this unit by reviving the parked P07c work if the numbers hold).
- Removing area/floor modes (they stay as flag-gated experiments).
- Any shader changes.

## Review

### Implementation

Landed as designed, on top of P06/P07's `AdaptiveLightingBudgetMode` enum
(Count/Area untouched by this unit).

- **`distanceSq` semantics, verified before reuse (design point 3)**: confirmed
  by inspection of `sim_visibility.cpp:121-129`
  (`SimulationWorker::computeVisibility`) that `sortedZoneDrawList_`'s
  `distanceSq` is already a CLOSEST-POINT-of-AABB distance (clamp-to-box
  formula from camera position), not center distance -- good, but it is
  computed from the CAMERA position, not the player light position (light
  sits at `player pos + (0, 0, 3.0)` in EQ-Z terms, see
  `renderer_entities.cpp:120-122`), so this unit computes its OWN closest-point
  distance against the light position rather than reusing `entry.distanceSq`.
- **Config**: `include/client/graphics/constrained_renderer_config.h` --
  `AdaptiveLightingBudgetMode::Radius` (third enum value) and
  `float adaptiveLightingRadiusMargin = 1.0f`. JSON override plumbing in
  `constrained_renderer_config.cpp` (`"radius"` string, `adaptiveLightingRadiusMargin`
  float). No preset sets radius mode -- count-mode default applies everywhere,
  unchanged, per the plan's scope note.
- **Fix site**: `src/client/graphics/renderer_zone_draw.cpp`,
  `drawZoneGeometrySorted()`. Added `computeLightEffectiveRadius()` (anonymous
  namespace, alongside P06's `estimateRegionScreenArea()`): solves
  `constant + linear*d + quadratic*d^2 <= 255` for the largest `d` (quadratic
  formula when `quadratic>0`, linear solve when only linear, `FLT_MAX` when
  both are zero -- constant attenuation never decays below 1/255), then
  multiplies by `adaptiveLightingRadiusMargin`. Computed once per frame from
  `zoneShader_->playerLightPos()`/`playerLightAtten()` -- the SAME values the
  shader will use this frame (set immediately upstream in
  `renderer_sim_bridge.cpp:755/760`). Per-region decision: clamp-to-box
  closest-point distance from the region's world AABB (same
  `regionMesh->getBoundingBox() + node position` pattern P06/P07 use) to the
  light position; demote iff `distSq > R^2`. `adaptiveLightingZoneBudget`'s
  numeric value is unused in this branch (only its `>0` gate, folded into
  `adaptiveLight`, applies) -- documented in both the config header comment
  and the fix-site comment.
- **Benchmark parity**: `tools/hwcaps/gles2_zone_benchmark.cpp` (new
  `--pp-budget-mode radius` value, mutually exclusive with `area` via two
  bools `zonePPAreaMode`/`zonePPRadiusMode`, plus `--pp-radius-margin F`) and
  `tools/hwcaps/zone_bench_loader.cpp`/`.h` (`zonePPRadiusMode`,
  `zonePPRadiusMargin`, `computeLightEffectiveRadiusBench()` -- byte-for-byte
  the same solve as production's helper -- and a `radiusUseLW` pre-pass keyed
  by region index, mirroring the area-mode pre-pass's structure). The
  benchmark's player light is collocated with the camera (no vertical offset,
  confirmed via `gles2_zone_benchmark.cpp:514-516`,
  `setPlayerLightPos(irrPos.X, irrPos.Y, irrPos.Z)` using the camera's own
  Irrlicht-space position) with a FIXED attenuation set once at init
  (`gles2_zone_benchmark.cpp:479`, `setPlayerLightAtten(1.0f, 0.0f, 0.0005f)`)
  -- this fixed value turned out to be the central finding of this unit, see
  below.
- **Reporting (design point 6)**: `collectDrawEntries()` gained two optional
  output parameters (`float* outLightRadius`, `int* outRegionsDemotedRadius`,
  default `nullptr` so existing call sites are unaffected) threaded through to
  two new trailing CSV columns, `light_radius,regions_demoted_radius` (always
  present, 0 unless `--pp-budget-mode radius`; `zone_bench_metrics.h/.cpp`).
  Production also gets a one-time `LOG_INFO` (computed R + light pos, first
  frame radius mode is active) and a per-frame `LOG_TRACE` (demoted
  count/fraction) -- both module-filtered, informational only, not relied on
  for this review's numbers (the benchmark CSV is the actual data source).

### Computed R and demoted-region fraction (design point 6 -- the central finding)

**R = 712.741 EQ units, constant across every pose tested (golden manifest,
P07's 6 static poses, and all 1000 frames of the qeynos2 moving path, both on
x86 llvmpipe and the Pi)** -- because the benchmark's player light attenuation
is set ONCE at init (`constant=1.0, linear=0.0, quadratic=0.0005`), not
per-frame from a simulated player level, so R is frame-invariant here (as
opposed to production, where `renderer_entities.cpp`'s
`quad = 19/radius^2` with `radius = 20 + (level/10)*80` would give a
level-dependent R in roughly [73, 366] -- see the formula check below).
`0x1FE` decimal-form check: `1/(1 + 0.0005 * 712.741^2) = 1/254.95 ~= 1/255`,
confirms the solve is correct.

**Demoted-region fraction is ~0 almost everywhere tested**:

| dataset | frames/poses | regions_vis (mean/max) | regions_demoted_radius (mean/max) | frames with any demotion |
|---|---|---|---|---|
| x86 llvmpipe moving path (qeynos2, 1000 frames) | 1000 | 36.7 / 164 | 0.054 / 2 | 48 / 1000 (4.8%) |
| Pi moving path (qeynos2, 1000 frames) | 1000 | 36.2 / 164 | 0.04 / 2 | 34 / 1000 (3.4%) |
| Pi static poses (fr291/292/509/627/631/634, x9 steady reps each) | 54 | 1-159 | 0 / 0 | 0 / 54 (0%) |
| 9 golden-manifest views (qeynos2/gfaydark/befallen) | 9 | 111-156 | 0 / 0 | 0 / 9 (0%) |

**This is the plan's own explicit stop condition ("If R turns out to cover
the whole scene ... or nearly nothing, STOP and report for orchestrator
review rather than tuning unilaterally") -- at this benchmark's fixed light
attenuation, R=712.74 EQ units is larger than nearly every region's
distance-to-camera in the qeynos2 zone at every tested pose, so radius mode
essentially never demotes anything.** This is not a bug in the demotion
mechanism (see the targeted losslessness check below, which independently
confirms the mechanism is correct on the rare frames where it DOES trigger)
-- it is a property of the benchmark's fixture: its player-light attenuation
was set once, for some other/generic testing purpose, unrelated to
production's level-dependent tuning, and the plan's design point 4 explicitly
directs mirroring the benchmark's EXISTING light[0] setup rather than
inventing a new one or simulating player level. Per the plan, this unit does
NOT tune `adaptiveLightingRadiusMargin`'s default or alter the benchmark's
fixed attenuation to manufacture savings -- it stops here and reports.

### Losslessness check

**Two independent comparisons were run; they give opposite-looking results
for a well-understood reason (this is itself part of the finding, not a
contradiction):**

**(A) Targeted radius-vs-FULL-PER-PIXEL check (the actual "lossless by
construction" claim)**, x86 llvmpipe, at the two moving-path frames found
where radius mode DID demote a region (`regions_demoted_radius>=1`,
frames 16/175/195-201/etc in the x86 CSV; two sampled and captured as static
poses):
- Pose `304.8,112.1,6.1,103.3,0.0` (1 region demoted, 59 regions visible):
  count/radius vs full-per-pixel (no `--adaptive-lighting` at all) --
  **pixel-identical, max_delta=0, 0/480000 pixels differ**.
- Pose `319.7,50.5,6.1,351.3,0.0` (1 region demoted, 70 regions visible):
  **pixel-identical, max_delta=0, 0/480000 pixels differ**.
- **This directly validates the design's core claim on the (rare) frames
  where the mechanism actually engages**: a region correctly identified as
  beyond the light's effective range renders identically whether given the
  per-pixel or lightweight material. PNGs:
  `benchmarks/contact_sheets/p07d_radius/targeted_pose{1,2}_{fullpp,radius}.png`.

**(B) The plan's literally-specified check, count-mode vs radius-mode**, at
the 9 golden-manifest views (qeynos2/gfaydark/befallen x 3 poses) + the P07
plan's 4 named static poses (fr291/509/627/634), `--adaptive-lighting
--zone-pp-budget 20`, x86 llvmpipe, 800x600:

| view | max_delta | diff pixels | % |
|---|---|---|---|
| qeynos2_f0 | 170 | 144421/480000 | 30.1% |
| qeynos2_f1 | 113 | 52966/480000 | 11.0% |
| qeynos2_f2 | 192 | 309884/480000 | 64.6% |
| gfaydark_f0 | 143 | 35335/480000 | 7.4% |
| gfaydark_f1 | 30 | 3690/480000 | 0.8% |
| gfaydark_f2 | 144 | 46232/480000 | 9.6% |
| befallen_f0 | 148 | 332054/480000 | 69.2% |
| befallen_f1 | 0 | 0/480000 | 0.0% |
| befallen_f2 | 86 | 329192/480000 | 68.6% |
| qeynos2_fr291 | 0 | 0/480000 | 0.0% |
| qeynos2_fr509 | 0 | 0/480000 | 0.0% |
| qeynos2_fr627 | 0 | 0/480000 | 0.0% |
| qeynos2_fr634 | 99 | 1/480000 | 0.0% |

**These are NOT near-identical, and the plan's naive acceptance bar ("target:
pixel-identical or near-identical") is not met at 7 of 13 views.** Root cause,
confirmed via the regions_demoted_radius=0 data in the previous section:
radius mode keeps essentially EVERY region per-pixel at all of these views
(R covers the whole visible scene), so it is being compared to count-mode's
arbitrary "first 20 regions in distance order" cutoff -- and at every view
where regions_vis > 20 (all of them except the three fr* poses with 1-2
regions), count-mode demotes the remaining 80-95%+ of regions to lightweight
REGARDLESS of whether they are actually beyond the light's true falloff
distance. Count-mode is the one being lossy here (exactly the P06/P07 saga's
own diagnosis of count-mode's flaw, extended to a case P06/P07 didn't probe:
even at ordinary, non-outskirts city/building views with 100+ regions, a flat
count-20 budget silently unlights the majority of visible, well-lit geometry).
Diffs correlate cleanly with `regions_vis`: the three views with `regions_vis
<= 2` (fr291/509/627 -- coincidentally also demoted=0 in count mode there)
are pixel-identical or near it; every other view has substantial diffs.
**Comparison (A) is the correct test of this unit's actual claim; comparison
(B), while literally what the plan's Verification section specifies, mostly
re-measures count-mode's pre-existing lossiness rather than radius-mode's
losslessness.** Flagging this discrepancy explicitly rather than silently
reinterpreting the acceptance bar.

Contact sheet + full per-view table:
`benchmarks/contact_sheets/p07d_radius/contact_sheet.png` +
`benchmarks/contact_sheets/p07d_radius/results.txt`.

### Pi measurement (P02 protocol)

Orange Pi (10.0.30.192) reachable throughout. `ARM_BUILD_BIN_DIR` explicitly
overridden to this worktree's `build-arm-noble/bin` (the documented trap --
`scripts/pi/env.sh` defaults to the main worktree). Binary staged via
`strip_and_stage_binary`, confirmed newer than watched source dirs by
`preflight()`. Command (all variants): `--zone qeynos2 --eq-path
/mnt/projects/EverQuestP1999 --entities 200 --seed 42 --navmesh-path
/mnt/projects/maps/nav --res 1280 720 --drm`; moving runs add `--frames
1000`; static runs cycle the plan's 6 named poses (fr291/292/509/627/631/634)
x10 reps (first rep discarded as warmup, matching the H04/P07 convention).
Variants: `count` = `--adaptive-lighting --zone-pp-budget 20 --pp-budget-mode
count`; `radius` = same with `--pp-budget-mode radius`; `all_lw` =
`--adaptive-lighting --zone-pp-budget 0`.

P02 protocol: `GovernorPin` (performance, restored to `ondemand` after each
session, confirmed by readback), `CmaMonitor`, `randomized_variant_order`,
all timed-run I/O to Pi-local `/tmp` tmpfs, results rsynced back after each
timed phase. **One `SessionAbort` occurred** (first session, after 2
invocations: CmaFree dropped from 73112 kB to 33104 kB following the 1000
frame `all_lw` moving run, below the 40MB threshold) -- handled per protocol:
governor was restored automatically by `run_session`'s exception path, then a
single `sync; echo 3 > /proc/sys/vm/drop_caches` (not a reboot) reclaimed
CmaFree to 81876 kB, and a second session completed the remaining 4 work
items cleanly (no further aborts). Total 6 invocations across 2 sessions,
under the 12-invocation cap; no reboot was needed or triggered.

**Moving path (1000 frames, whole-run averages):**

| variant | avg total_ms | fps |
|---|---|---|
| count | 76.116 | 13.1 |
| radius | 76.934 | 13.0 |
| all_lw | 65.111 | 15.4 |

radius is **1.1% SLOWER** than count (opposite of the acceptance criterion's
direction), consistent with regions_demoted_radius averaging 0.04
regions/frame (i.e. radius mode is running almost identically to full
per-pixel, which is inherently costlier than count-mode's aggressive
past-budget-20 demotion in a >36-region-average scene).

**Static poses (mean total_ms over 9 steady reps after 1 discarded warmup
rep):**

| pose | regions_vis | count_ms | radius_ms | all_lw_ms | radius vs count | radius vs all_lw |
|---|---|---|---|---|---|---|
| fr291 | 1 | 18.227 | 18.234 | 6.960 | +0.0% | +162.0% |
| fr292 | 1 | 17.771 | 17.790 | 6.471 | +0.1% | +174.9% |
| fr509 | 2 | 19.778 | 19.783 | 7.315 | +0.0% | +170.4% |
| fr627 | 159 | 54.075 | 63.190 | 41.286 | **+16.9%** | +53.1% |
| fr631 | 151 | 55.709 | 63.454 | 40.135 | **+13.9%** | +58.1% |
| fr634 | 138 | 56.749 | 61.263 | 38.247 | **+8.0%** | +60.2% |

`regions_demoted_radius` was **0 at every single static pose, every rep**
(all six poses, `light_radius` a constant 712.741). At the anomaly poses
(1-2 regions, below count-mode's budget of 20, so count doesn't demote either)
radius matches count within noise, as expected. At the city poses (138-159
regions, far above the budget), radius is **8-17% SLOWER than count**, the
opposite of "captures a majority of the -19 to -24% area-floor1 saving" --
because radius keeps literally everything per-pixel there (0 demotions) while
count-mode demotes ~120-140 of those regions purely by count, which is faster
per-pixel-shader-cost-wise even though (per this unit's own design argument)
it is doing so somewhat arbitrarily rather than based on actual light falloff.

**draws/regions_vis/regions_drawn/regions_occluded/polys byte-identical
count vs radius**: **PASS** -- 0 diffs across all 1000 moving-path frames
(full-file programmatic check) and all 60 static rows. Confirmed on both x86
(1000-frame moving run) and the Pi (1000-frame moving run + 60-row static
run). Only the per-region lighting material decision differs between the two
modes, as required.

CSVs: `benchmarks/pi_runs/p07d_radius/{count,radius,all_lw}.csv` (moving,
1000 frames each), `static_{count,radius,all_lw}.csv` (60 rows: 6 poses x 10
reps), `p07d_{moving,static}_*.log` (benchmark stdout), `cma_log.tsv` +
`cma_log_resume.tsv` (CmaFree telemetry, both sessions).

### Acceptance criteria verdicts

- **Anomaly poses (fr291/292) radius within noise of count (~17-18ms)**:
  **PASS** -- +0.0%/+0.1% (18.234 vs 18.227, 17.790 vs 17.771 ms).
- **City poses (fr627/631/634) radius captures a majority of P07b's -19 to
  -24% saving vs count**: **FAIL, in the wrong direction** -- radius is
  8-17% SLOWER than count at these poses, not faster, because
  `regions_demoted_radius=0` at every one of them (R=712.74 covers the whole
  scene here). Per the plan's own instruction ("if it captures little ...
  that is a finding, not a failure -- report for review"), this is reported
  as the central finding of this unit rather than something to fix by tuning.
- **Whole-run avg: radius <= count**: **FAIL** -- 76.934 vs 76.116 ms
  (+1.1%), same root cause.
- **draws/regions_vis/regions_drawn/polys byte-identical count vs radius**:
  **PASS** -- 0 diffs, all 1000 moving frames + 60 static rows, both x86 and
  Pi.
- **Losslessness (9 golden views + P07 poses, count vs radius, target
  pixel-identical/near-identical)**: **FAIL as literally stated** at 7/13
  views (see table above) -- but see the "Losslessness check" section: this
  comparison's baseline (count-mode) is itself lossy at `regions_vis > 20`,
  so it is not a clean test of radius-mode's own losslessness. The
  **targeted radius-vs-full-per-pixel check (the actual mechanism under
  test) is pixel-identical (max_delta=0) at both sampled demoted frames** --
  **PASS** for the claim the unit is actually making.
- **Full production bar** (build, ctest, x86 GLES2 build, visual gate, ARM
  build, file-size check): **PASS**, see Verification section below.

### Verification

- `cmake --build build -j24`: clean.
- `ctest --test-dir build --output-on-failure`: 1224/1225 passed. Only
  failure: `AudioManagerTest.PreloadCommonSounds` (documented host exception).
  `RDPServerFrameTest.UpdateFrameMultipleTimes` (the other documented
  exception) passed this run. `visual_gate` (test #1225) **PASSED**.
- `scripts/build-gles2-x86.sh`: clean.
- Visual gate: **PASS**, confirmed via the ctest run above (default
  `count` mode everywhere, no preset enables radius mode, goldens untouched).
- `scripts/build-arm-noble.sh`: clean, from this worktree
  (`build-arm-noble/bin/`), used directly for the Pi staging above.
- `scripts/refactor/check_file_sizes.sh fable`: clean, no output.

### Deviations

- **The plan's literal "count vs radius, near-identical" losslessness
  acceptance bar does not hold** at views where `regions_vis` exceeds the
  count-mode budget (20) -- see the "Losslessness check" section above for
  the root-cause analysis and the supplementary targeted comparison that
  validates the actual mechanism. Not silently reinterpreted -- both the
  literal check's results and the targeted check's results are reported.
- **One Pi `SessionAbort`** (CmaFree threshold, after the `all_lw` moving
  run) was handled with a single `drop_caches` reclaim (no reboot), per the
  documented P03/P06/P07 precedent that low CmaFree here is reclaimable
  page-cache borrowing, not a leak. Session then completed cleanly.
- Per-frame radius-mode reporting needed new optional output parameters on
  `ZoneBench::collectDrawEntries()` (`float* outLightRadius`,
  `int* outRegionsDemotedRadius`, both defaulted to `nullptr`) rather than
  changing its signature outright, since the function has several call sites
  in `gles2_zone_benchmark.cpp` beyond the two (static-pose loop, moving-path
  loop) that actually needed the new stats -- kept those other call sites
  unchanged.
- `refactor/scratch`-style standalone Pi driver scripts
  (`p07d_measure.py`/`p07d_measure_resume.py`, built on `pi_common.py`
  primitives) and the local x86 losslessness/targeted-check scripts were
  kept in session scratch, not committed -- same P05/P06/P07 standalone-script
  precedent.
- Pi left pristine: governor `ondemand` on all 4 cores (confirmed by
  readback), staged binary and all remote `/tmp/p07d_*`/
  `/tmp/gles2_zone_benchmark_h04` files removed (confirmed via `ls /tmp`
  returning nothing for either pattern), CmaFree 58484 kB at session end, no
  reboot triggered.
