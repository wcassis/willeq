# P40 — Resolution-dependent visibility anomaly (root-cause)

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Sibling: P39 (slot1,
config plumbing for the occluder key) — do not touch
constrained_renderer_config parse/preset files; yours is the
visibility/culling pipeline.

P38 found: rendering befallen at 400x300 produced MORE draws and
triangles than 800x600 (same replay, same config). Resolution should
not change what is VISIBLE. Root-cause it.

## Work

1. Reproduce on x86 (llvmpipe, the benchmark + committed
   `--gl-counters` from P38): draws/tris per frame at 800x600 vs
   400x300 vs one more point (e.g. 1024x768) on the canonical befallen
   replay. Confirm the anomaly and its direction/monotonicity.
2. Root-cause: plausible mechanisms to check (verify, don't assume):
   (a) the occlusion-culling system rasterizes occluders in
   screen-space — lower res → coarser occlusion buffer → fewer pixels
   pass the occluder test → less culling → more draws (if so, this is
   a DESIGN property with a tunable, not a bug — say so and
   characterize the tradeoff); (b) FOV/aspect or frustum derivation
   depending on resolution incorrectly; (c) LOD/screen-size thresholds
   inverted; (d) something else the counters point to.
3. Fix if it's a genuine bug (wrong math); REPORT-ONLY with a
   characterization + recommendation if it's a design property
   (e.g. occlusion buffer should be fixed-size independent of screen
   resolution — propose, don't implement).
4. Quantify the Pi-relevant implication either way (the Pi ships
   800x600; does this matter at shipped res, or only for hypothetical
   res changes?).

## Rules
- Territory: occlusion/PVS/visibility code (read), fix only if
  genuine-bug class; benchmark/counter usage. NOT config parse files
  (P39's), NOT the driver, NOT goldens (gate must PASS if you change
  anything).
- STOP: fix would change culling behavior at the SHIPPED resolution
  (that would move blessed Pi behavior — report with numbers instead).

## Verification
- Full bar if code changed (ctest baseline, gate PASS, ARM clean,
  file sizes); if report-only, build+repro evidence suffices.
- The three-resolution counter table + the named mechanism.

## Review

Executed 2026-07-30 on `wt/slot2`, branch `task/P40-res-visibility`, cut from
`fable` at `4b36622` (adds P39/P40 plan doc, no code). x86 only (llvmpipe,
SSE2 path), as scoped.

### Result up front: the anomaly does NOT reproduce on x86

Rebuilt `build-gles2/` (it predated P38's `--gl-counters` addition) and ran
the identical canonical replay P38 used — befallen zone,
`benchmarks/paths/befallen_seed42_1000f.path` (Q01 deterministic replay,
1000 frames), `--entities 5`, `--gl-counters` — at 400x300, 800x600, and
1024x768, both under the shipped/merged preset flags
(`--merge-zone-draws --composite-scenegraph`, matching
`config/constrained_presets.json`'s `orangepi` block P39 plumbs) and under
the unmerged config P38's original (flagged-as-mistaken) probe used.

**Three-resolution counter table (shipped/merged config, avg over 1000 frames):**

| res | draws | polys | regions_vis | regions_drawn | regions_occluded | GPU sync avg (ms) | total avg (ms) |
|---|---|---|---|---|---|---|---|
| 400x300  | 15 | 1380 | 134 | 134 | 28 | 4.17  | 8.72  |
| 800x600  | 15 | 1380 | 134 | 134 | 28 | 6.44  | 12.50 |
| 1024x768 | 15 | 1380 | 134 | 134 | 28 | 11.69 | 18.44 |

**Unmerged-config counter table (P38's original, flagged-as-deviation methodology):**

| res | draws | polys | regions_vis | regions_occluded | GPU sync avg (ms) | total avg (ms) |
|---|---|---|---|---|---|---|
| 400x300 | 263 | 1380 | 134 | 28 | 4.80 | 11.16 |
| 800x600 | 263 | 1380 | 134 | 28 | 8.96 | 16.60 |

Per-frame diff of all 1000 rows (`draws`, `polys`, `regions_vis`,
`regions_drawn`, `regions_occluded`) across all three resolutions, both
configs: **0 of 5,000 cells differ — byte-identical.** Only GPU-sync/total
time scale with pixel count (monotonically, roughly proportional to
resolution — exactly the expected fill-rate behavior, not an anomaly).
Raw CSVs + logs kept: `benchmarks/pi_runs/p40_res_visibility/`.

### Root-cause read (why this is expected, not surprising)

Checked all three plausible mechanisms named in the plan by reading the
code, then confirmed by measurement:

- **(a) Screen-space occlusion rasterization coarseness — RULED OUT.**
  `SoftwareOcclusionCuller`'s depth buffer is sized from
  `OcclusionCullerConfig::width/height`, which the renderer sets from
  `constrainedConfig.occlusionBufferWidth/Height` (`irrlicht_renderer.cpp`
  ~L1462-1468) — a **fixed value from the hardware-tier preset** (64x32 for
  orangepi, `constrained_renderer_config.cpp` L281-282), **not derived
  from `--res` at all**. This was already noted as suspicious by P38; this
  unit confirms by code read there is no wiring from display resolution to
  this buffer anywhere in the tree.
- **(b) FOV/aspect/frustum derivation — RULED OUT for this probe.**
  `IrrlichtRenderer::processFrameVisibility()` (`renderer_frame.cpp`
  ~L958-964) does compute `aspect = screenSize.Width / screenSize.Height`
  from the actual driver resolution every frame, and feeds it to both
  `FrustumCuller::update()` and (via `renderer_sim_bridge.cpp`/
  `renderer_zone_load.cpp`) the occlusion culler's camera. This IS a live
  resolution dependency in principle — but 400x300, 800x600, and 1024x768
  are all exactly 4:3, so `aspect` is bit-identical (1.333...) at all three
  points the plan asked for. `camera_->getFOV()` is a fixed vertical FOV,
  not resolution-derived. Net effect at these three specific resolutions:
  no frustum change. (Flagging as an open question for future testing:
  a non-4:3 resolution change WOULD move the frustum culling result
  slightly, which is correct/intended aspect-correction behavior, not a
  bug — out of scope here since the plan's three probe points are all 4:3.)
- **(c) LOD/screen-size threshold inversion — RULED OUT.** No LOD or
  projected-screen-size code exists in the visibility path
  (`sim_visibility.cpp`, `frustum_culler.cpp`, `software_occlusion_culler.cpp`)
  that references pixel counts or `--res`; entity LOD (`--lod-entities`,
  billboard distance) is a separate, distance-based (not resolution-based)
  system not exercised by this zone-geometry probe.
- **(d) Something else — none found; the code has no resolution-dependence
  in this pipeline for same-aspect-ratio resolutions**, and the
  byte-identical 5,000-cell diff is direct empirical confirmation, not
  just an absence-of-evidence code read.

### Named mechanism: NOT a bug in the shared C++ visibility pipeline

The visibility/occlusion/culling code that runs identically on x86 and
ARM has zero structural resolution-dependence at same aspect ratio
(fixed-size occlusion buffer; aspect-driven not pixel-driven frustum). The
x86 replay proves this empirically for the exact same replay, entity
count, and config P38 used. Two remaining explanations, neither
verifiable from this x86-only unit:

1. **P38's own methodology flag is the more likely culprit.** P38
   explicitly documents that `res_400x300` was one of the "first 5 Pi
   invocations" run *without* `--merge-zone-draws --composite-scenegraph
   --eqe1-path` — i.e., not a clean apples-to-apples comparison against
   its "control" row (which *was* run with the shipped config). P38's own
   text calls this "CONFOUNDED, not a clean result" and "an honest
   null-with-a-twist... not... a fill-bound percentage." This unit's
   unmerged-config x86 run (matching that same flag set) still shows
   byte-identical geometry counts across resolutions, which weakens (but,
   absent the actual Pi re-run, does not fully rule out) the
   methodology-drift explanation — it shows the flag mismatch alone isn't
   sufficient to explain a res-dependent count change in the shared code.
2. **Pi/Lima-driver or ARM/NEON-path floating-point divergence.**
   `frustum_culler.cpp` has separate NEON (ARM) and SSE2 (x86) SIMD
   implementations of the same plane-vs-AABB math; a rounding difference
   at a frustum-edge AABB could in principle flip a handful of borderline
   regions in/out. This cannot be checked from x86 (this unit's territory
   is x86-only, no Pi) and, more importantly, a boundary-rounding effect
   would predict small/noisy differences, not the systematic ~15-40%
   draws/tris increase P38 measured — so it's a weak candidate, named for
   completeness, not endorsed.

### Fix-or-characterization

**REPORT-ONLY — no code changed.** No genuine bug was found in x86-reachable
territory: the code is provably resolution-independent for same-aspect-ratio
inputs (by read and by 5,000/5,000-cell-identical measurement), so there is
nothing to fix here. Recommendation for follow-up (not implemented, per the
plan's report-only path): re-run P38's `res_400x300`/`res_800x600` A/B on
the Pi using the *current* canonical flags (`--path-file` + `--merge-zone-draws
--composite-scenegraph --eqe1-path`, i.e. this unit's exact merged-config
command) before trusting the original confounded numbers at all — the
likeliest single fix is simply re-measuring cleanly, not code.

### Shipped-resolution implication

**None.** The Pi ships 800x600. This unit found no code path, on the
platform this unit could test, where resolution changes visibility/draw
counts at same aspect ratio — so there is no hypothetical fix that would
touch, or even could touch, shipped 800x600 behavior. STOP condition
("fix would change culling at the SHIPPED 800x600") does not apply: no fix
was made.

### Verification

Report-only path per the plan (build + repro evidence, not the full bar):
- `bash scripts/build-gles2-x86.sh` — rebuilt clean (had to; the tree
  predated P38's `--gl-counters` flag), succeeded, `build-gles2/bin/
  gles2_zone_benchmark` and `willeq` built.
- Three-resolution + two-config repro above, byte-identical-frame diff
  script output kept alongside the CSVs.
- No source files changed — ctest/gate/ARM-build/file-size steps not
  applicable (nothing to regress).

### Deviations

- Had to rebuild `build-gles2/` before any repro was possible (stale vs.
  P38's committed `--gl-counters`); not a deviation from the plan, just
  setup.
- Did not have exact P38 Pi command lines to replicate byte-for-byte
  (perf_truth_audit.md doesn't quote them); reconstructed the canonical
  invocation from the path-file/flags it describes. Absolute counter
  magnitudes here (draws=15 avg vs P38's Pi draws_avg=8.7) differ from
  P38's Pi numbers — expected, since occluder-budget/entity-placement
  defaults and platform aren't identical — but the property under test
  (resolution-invariance of the SAME run) does not depend on matching
  P38's absolute numbers, only on the counts being self-consistent across
  the three resolutions within this unit's own runs, which they are.
- Could not test on Pi/Lima hardware (out of territory for this unit) —
  named as the open follow-up in "Named mechanism" above rather than
  silently dropped.

Commit: (see branch `task/P40-res-visibility` HEAD after this review is committed).
