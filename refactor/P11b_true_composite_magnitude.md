# P11b — The true composite perf magnitude: animated entities through the production path, on the Pi

Approved by Will 2026-07-27 ("go ahead with P11b").
Slot: wt/slot1, branch task/P11b-composite-magnitude, cut from fable
(2a609ac tip). Measurement unit; Pi non-interactive (P02 protocol).
This retires two bad numbers: P08c's -33.1% (stand-in path with its
own cull bug) and P11's ~0% (static scenegraph route that never builds
composite meshes). Composite meshes build ONLY on the ANIMATED path
(single SMeshBuffer per entity) — measurable at last because P12b
fixed the benchmark's animated rendering (static-HW-buffer freeze).

## Design

### A. x86 preflight (correctness + calibration before any Pi spend)

1. **Animated-path determinism**: animation time is frame-indexed
   (frame*33.3ms) — verify an animated canonical-replay run is
   deterministic (two identical runs → geometry/pose columns
   byte-identical; timing columns exempt). If not, stop and report.
2. **Composite engagement proof**: with --composite-scenegraph +
   --animate, confirm via draw counts / logs that composite entities
   really dispatch as single-buffer meshes (fewer entity draws than
   multibuffer-animated at the same frames) — the thing P11's static
   route couldn't do. Quantify entity draws per frame both ways.
3. **Visible-entity calibration for the ANIMATED path**: P08b's static
   calibration (placed 205 → median 38 visible) may not transfer
   (P08b measured 4.5-5x visible inflation with the OLD --animate; the
   P12b fix may have changed behavior). Check whether entities_visible
   populates on the animated scenegraph path (extend the counter
   additively if not), re-run the calibration curve, and pick placed-N
   for median ~38 visible on the befallen canonical replay. Document
   the curve.
4. Known context, do not re-litigate: the low-magnitude atlas-seam diff
   under animation (P12b Z35 verdict) is a KNOWN visual item — this
   unit measures PERF; no pixel-identity bar beyond confirming no
   structural rendering breakage at 2 spot frames.

### B. Pi measurement (P02 protocol, canonical befallen replay, production config)

5. Variants (all --animate, calibrated placed-N, 1000-frame replay):
   a. multibuffer-animated (baseline for THIS comparison);
   b. composite-animated (--composite-scenegraph) — THE number;
   c. merge + composite-animated (the full honest stack) if budget
      allows — the real KPI-table entry.
   Whole-run avg/p95 + submission phase columns + anim_cpu_ms +
   pinned-pose frame extraction (bf355/472/626/791/917/987).
   Randomized order; <=12 invocations/session, 2 sessions OK.
6. Cross-checks: geometry/pose columns byte-identical across variants
   where expected (a vs b: entity draw counts differ BY DESIGN — zone
   columns identical; document expected deltas); unmerged zone columns
   vs P08b/P09c baselines where comparable (animated visibility may
   legitimately differ from static baselines — compare and explain
   rather than assert).

### C. Deliverables

7. **The corrected number**: composite-animated vs multibuffer-animated
   (avg/p95/fps, entity draws, submission ms) — alongside the two
   retired numbers (P08c -33.1%, P11 ~0%) with one line each on why
   they were wrong.
8. **The honest KPI table update**: where does befallen stand with the
   full stack (merge + real composites, animated) against 33.3ms/30fps;
   the measured residual and its split (entity submission / zone
   submission / GPU / anim CPU).
9. Recommendation: what (if anything) remains between befallen and
   30fps, and which deck item addresses it.

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- A1 determinism proof; A2 engagement proof; replay discipline (pose
  columns vs path file).
- Pi rules: P02 via pi_common.py, /tmp-only I/O, ARM_BUILD_BIN_DIR trap
  (slot1 build — rebuild ARM first so P12b's fix + all flags are in the
  staged binary), drop_caches never reboot, pristine exit.
- CSVs → benchmarks/pi_runs/p11b_composite_magnitude/.

## Out of scope

- Any production change; visual fixes (seam/foliage/pop-in); the
  eviction mystery; preset flips (Will's).

## Review

Executed 2026-07-27 on `wt/slot1`, branch `task/P11b-composite-magnitude`, cut from
`fable` at `126e324` (this plan's own commit). No production or benchmark source
changes were needed (see Phase A.3) — this unit is measurement-only, with one bespoke
Pi driver script (`refactor/scratch/p11b_pi_composite_animated_replay.py`, same
pattern as P09c's/P11's own `p11_pi_stacked_replay.py`) and its output data.

### Standard bar (all before any Pi spend)

- `cmake --build build -j24`: clean.
- `ctest --output-on-failure -j4`: 1233/1235, exactly the two documented host
  exceptions (`AudioManagerTest.PreloadCommonSounds`, `RDPServerFrameTest.
  UpdateFrameMultipleTimes` — SEGFAULT, matches the known host-flaky pattern).
- `scripts/build-gles2-x86.sh`: clean.
- `scripts/ci/visual_gate.sh`: **PASS**, goldens untouched (re-confirmed again after
  the Pi phase, since this unit's changes are additive-data-only).
- `scripts/build-arm-noble.sh`: clean, rebuilt **before** the Pi session per the
  ARM_BUILD_BIN_DIR rule — confirmed `dfac1e5` (P12b's node-wiring fix) is an
  ancestor of this branch's tip (`git merge-base --is-ancestor`), so the staged
  binary carries the fix.
- `scripts/refactor/check_file_sizes.sh fable`: exit 0, no new FAILs (no C++ files
  changed).
- File sizes: `build/bin/willeq` 73.0MB, `build-gles2/bin/gles2_zone_benchmark`
  9.8MB, `build-arm-noble/bin/gles2_zone_benchmark` 53.2MB (unstripped),
  `build-arm-noble/bin/willeq` 355MB (unstripped) — no anomalies.

### A. x86 preflight

**A1 — Animated-path determinism.** Two identical runs (`--zone befallen --path-file
benchmarks/paths/befallen_seed42_1000f.path --entities 205 --animate
--composite-scenegraph --eqe1-path ... --adaptive-lighting --zone-pp-budget 20
--pp-budget-mode count --player-light-level 9`, 300 measured frames, x86/llvmpipe):
**0/300 mismatches on every geometry/pose column** (`pos_x/y/z/heading/pitch`,
`draws`, `state_changes`, `polys`, `regions_vis/drawn/occluded`, `alpha_test_draws`,
`solid_draws`, `light_radius`, `regions_demoted_radius`, `entities_visible`). Timing
columns exempted per the plan. **PASS — did not need to stop.**

**A2 — Composite engagement proof.** Canonical setup (`--frames 1000`, N=205,
`--animate`, production config), multibuffer-animated vs composite-animated
(`--composite-scenegraph`), x86:

| | draws avg/frame | entity draws/entity (draws − regions_drawn, ÷ entities_visible) | entities_visible avg |
|---|---|---|---|
| multibuffer-animated | 710.1 | 18.42 | 34.7 |
| composite-animated | 287.7 | 6.26 | 34.7 (0/1000 mismatches vs multibuffer) |

Zone-geometry columns (`regions_vis/drawn/occluded`) and pose columns: **0/1000
mismatches** between the two variants — confirms composite-scenegraph changes ONLY
the entity mesh-building path, nothing else. Entity draws per visible entity drop
66% (18.42 → 6.26) — composite entities really do dispatch as few-SMeshBuffer
meshes under animation, the thing P11's static route could never show (P11's own
Phase A found `baseline vs composite` byte-pixel-identical and draws identical on
the static route — see the retired-numbers section below for why). Avg total draws
per frame drop 422.5 (710.1 → 287.7, 59.5%). **Quantified, real engagement — PASS.**

**A3 — Visible-entity calibration for the ANIMATED path.** `entities_visible`
**already populates** on the animated `--composite-scenegraph` path (confirmed in
A1/A2's CSVs) — both `--composite-scenegraph` and the default/unbatched entity mode
fall through the SAME code branch in `gles2_zone_benchmark.cpp` (`opts.compositeEntities
&& !opts.compositeSceneGraph` is false when `--composite-scenegraph` is set, so
control falls to the final default `else`, which calls `collectEntityDrawEntries()`
and sets `metrics.setEntitiesVisible()`). **No benchmark extension was needed.**

Calibration curve (x86, `--animate`, canonical 1000-frame befallen replay,
`--frames 1000` explicitly set — see the important caveat below):

| placed N | median visible | p95 visible | max visible |
|---|---|---|---|
| 100 | 19 | 20 | 20 |
| 120 | 22 | 24 | 24 |
| 140 | 26 | 28 | 28 |
| **205** | **38** | **40** | **40** |

**Headline finding: post-P12b, the animated path's visible-entity distribution at
N=205 is IDENTICAL to P08b's static calibration** (median 38 / p95 40, both). P08b's
"OLD `--animate` gives 4.5-5x more visible entities than static at the same N" finding
(the P12-era construction bug) **does not reproduce anymore** — P12b's node-wiring fix
resolved not just the crumpled-pose rendering bug but also this visibility-inflation
side effect (both traced to the same wrong-mesh-state root cause: distance/AABB-based
culling reading the entity's position, which is unaffected by pose but was apparently
disturbed by whatever the pre-P12b construction bug corrupted). **Calibrated N = 205**
(same as static) — median 38 / p95 40 visible, squarely the plan's ~38 target, no
recalibration away from the static value was needed.

**Important caveat found along the way, not a contradiction**: an early exploratory
run using default `--frames` (300, not `--frames 1000`) with the same `--path-file`
showed median 64 visible at N=205 — this is NOT a residual animate bug. `placeEntitiesAlongPath`
spreads N entities along the DECLARED frame count, so the same 205 entities are
packed 3.3x denser over a 300-frame span than a 1000-frame span, inflating
per-frame visibility purely as a placement-density artifact. Verified directly: the
first 300 rows of the canonical `--frames 1000` replay give median 38 (matching the
full run), not 64 — confirming the discrepancy is a `--frames` mismatch between the
two invocations, not a live bug. **All Pi/canonical measurements below use `--frames
1000` explicitly, matching the path file's own length.** Noted here so a future unit
doesn't waste budget re-chasing a phantom "animate still inflates visibility" lead.

**A4 — 2-frame structural spot check.** `--dump-frames 355,626` on both
multibuffer-animated and composite-animated (N=205, canonical replay), 800x600, x86.
Diff: frame 355 — 6,031/480,000 px (1.26%), max_delta 202; frame 626 — 18,821/480,000
px (3.92%), max_delta 212. Visual inspection (`benchmarks/contact_sheets/
p11b_spot_check/spot_{multi,composite}_f{355,626}.png`) confirms: **coherent,
matching walk-cycle poses, correct silhouettes, correct entity count and ordering
both frames** — the diff is the same low-magnitude atlas-seam class P12b's Z35 item
already characterized and explicitly parked as out of scope for this perf unit
(known, not re-litigated). **No structural rendering breakage — PASS.**

### B. Pi measurement (P02 protocol)

Pi session: `user@10.0.30.192`, reachable throughout, `/tmp` verified pristine before
starting (only unrelated `systemd-private-*` dirs), CmaFree healthy at session start
(49,164 kB, above the 40 MB abort floor). `ARM_BUILD_BIN_DIR` explicitly overridden
to this worktree's `build-arm-noble/bin` (script:
`refactor/scratch/p11b_pi_composite_animated_replay.py`, same bespoke `--path-file`
moving-replay pattern as P09c's/P11's scripts — `pi_bench_run.py`'s pose-cycle
helpers don't apply to a moving replay). ARM binary rebuilt immediately before the
session (includes P12b's fix + all needed flags), stripped 53.2MB → 5.9MB before
staging to Pi `/tmp`.

**A real bug caught mid-session, not asserted past**: the first 3 invocations
(multibuffer-animated, composite-animated, merge+composite-animated) came back with
composite-animated's `draws_avg` **identical** to multibuffer-animated's (both 710.1)
— a red flag caught by the draws_avg sanity check, not accepted as a "~0%" finding.
Root cause: `--composite-scenegraph` is a **silent no-op** unless `--eqe1-path` is
also passed (`zone_bench_loader.cpp:576/610` gate composite mesh-building on
`config.compositeEntities && !config.eqe1Path.empty()`, no warning printed
otherwise) — the first script draft omitted `--eqe1-path` for the Pi runs (present
correctly in every x86 preflight command above, an oversight specific to the Pi
script). Fixed and re-ran the 2 affected variants (composite-animated,
merge+composite-animated) with `--eqe1-path /mnt/projects/EverQuest_ETC1` added;
kept the multibuffer-animated CSV from invocation #1 (unaffected — `compositeEntities`
is false for that variant, so the missing flag was a no-op there). Post-fix,
composite-animated's `draws_avg` (287.7) matches the x86 preflight number (287.686)
closely, confirming the fix and giving confidence the two are measuring the same
thing. **5/12 invocations used in 1 session** (well under the cap), no SessionAbort,
CmaFree stayed healthy throughout (49,164 → 44,776 kB across invocations #1-3; the
fix's 2 re-run invocations actually recovered CmaFree, 45,076 → 70,360 kB — no
ratchet). Governor pinned to `performance` during runs, restored to `ondemand` on
exit (verified). Pi left pristine (`/tmp` cleaned of staged binary/CSVs/logs/path
file, verified via `ls /tmp` showing only unrelated systemd-private dirs).

Canonical replay: `benchmarks/paths/befallen_seed42_1000f.path`, `--frames 1000`,
seed 42, production config (`--adaptive-lighting --zone-pp-budget 20
--pp-budget-mode count --player-light-level 9`), `--entities 205` (calibrated, all
3 variants `--animate`), 800x600, DRM/GLES2/Mali400.

**Replay discipline** (full 1000-frame programmatic checks, not spot checks):
- Pose columns (`pos_x/y/z/heading`) vs the path file: **0/1000 mismatches, all 3
  variants**.
- Zone-geometry columns (`regions_vis/drawn/occluded`) cross-variant: **0/1000
  mismatches**, multibuffer vs composite AND multibuffer vs merge+composite — the
  merge lever changes HOW zone draws are submitted (coalesced), not what's visible
  or culled, matching P09c's/P11's own finding.
- `entities_visible` cross-variant: **0/1000 mismatches** across all 3 — entity
  draw-COUNT differs by design (that's the whole point), but WHICH entities are
  visible does not, confirming culling is identical regardless of mesh-building mode
  (position-based, unaffected by composite vs multibuffer).

### THE TABLE (befallen combined e205, --animate, 1000-frame canonical replay, whole-run avg/p95, Pi)

| variant | avg total_ms | p95 total_ms | fps (avg) | fps (p95) | draws avg | submission avg/p95 | GPU avg/p95 | anim_cpu avg/p95 |
|---|---|---|---|---|---|---|---|---|
| multibuffer-animated (baseline) | 116.187 | 291.221 | 8.6 | 3.4 | 710.1 | 74.249 / 208.553 | 33.919 / 78.423 | 4.532 / 5.876 |
| **composite-animated (THE number)** | **52.764** | **183.786** | **19.0** | **5.4** | 287.7 | 27.529 / 107.762 | 17.909 / 64.291 | 4.461 / 5.748 |
| merge + composite-animated (full stack) | 25.111 | 35.320 | 39.8 | 28.3 | 42.6 | 6.244 / 9.347 | 11.630 / 17.509 | 4.405 / 5.759 |

Pinned-pose extraction (bf355/472/626/791/917/987) is in the CSVs
(`benchmarks/pi_runs/p11b_composite_magnitude/`); bf472 (704-region pose, the
zone-geometry worst case) remains the outlier at every variant (multibuffer 400.2ms,
composite 400.4ms — composite alone barely touches bf472 since that pose's cost is
zone-dominated; merge+composite 61.0ms — the merge lever is what actually tames
bf472). All other pinned poses (bf355/626/791/917/987) drop sharply once composite
engages and drop further once merge is added.

### C. Deliverables

**7. The corrected composite number, and the two retired numbers**

| number | value (p95) | route | why it was wrong / right |
|---|---|---|---|
| P08c | **−33.1%** | `--composite-entities` (stand-in, simulated 1-draw-per-entity) | Measured through a hypothetical dispatch production doesn't have, AND through the P09b-discovered stand-in cull path with its own cull bug (different visible entity subset than the baseline it was compared against) — not a measurement of the real composite pipeline at all. |
| P11 | **~0%** (−0.006%) | `--composite-scenegraph`, STATIC | Exercised the real composite asset-build call (`setCompositeEntityPath`/`preloadModelData`), but on the static (non-animated) route composite meshes still render through the same per-bodypart Irrlicht draw calls as baseline — composite meshes only actually collapse to single/few `SMeshBuffer`s once entities are driven through `OnAnimate()`/`applyAnimation()`/`createInstanceMesh()`, the animated construction path. Static route = correct asset path, wrong dispatch shape, structurally can't show the win. |
| **P11b (this unit)** | **−36.9% p95 / −54.6% avg** | `--composite-scenegraph`, **ANIMATED**, production-faithful | Real composite asset path + real animated per-instance dispatch (the SAME path production uses, per P12b's node-wiring fix) — draws avg 710.1 → 287.7 (−59.5%), entity draws/entity 18.42 → 6.26 (−66%), p95 frame time 291.221 → 183.786ms (**−36.9%**), avg frame time 116.187 → 52.764ms (**−54.6%**). This is the first time composite's real perf effect has ever been measurable — P08c's route was a fiction, P11's route was structurally blind to it. |

**8. The honest KPI table**

Full stack (merge + real composites, animated) vs 33.3ms/30fps:

| | avg | p95 |
|---|---|---|
| merge+composite-animated | 25.111ms (39.8 fps) | 35.320ms (28.3 fps) |
| vs 33.3ms KPI line | **8.19ms UNDER budget** | **2.02ms OVER budget** |

**The full honest stack (merge + real animated composites) puts befallen's
whole-run AVERAGE frame time under the 30fps budget for the first time in this
campaign's history** (P08b/P09c/P11 all measured well over budget at every prior
stacking point). The p95 (worst 5% of frames, dominated by bf472-class dense
zone-geometry poses) is still 2.02ms over — close, not crossed.

Residual split at the full-stack p95 point (35.320ms total): submission 9.347ms
(26.5%), GPU sync 17.509ms (49.6%), collect/cull 4.083ms (11.6%), other 5.868ms
(16.6%), swap 0.222ms (0.6%) — components don't sum exactly to total_ms at p95
because each column's own p95 is driven by a different frame (only the AVG row
sums near-exactly: 2.500+0.147+6.244+4.583+0.197+11.630 = 25.300ms vs total_ms avg
25.111ms, within rounding). `anim_cpu_ms` (5.759ms p95) is a P08b-instrumented
sub-measurement already folded inside the CPU draw phase totals above (confirmed by
the avg-row sum check), not a separate additive line item. **GPU sync, not
submission, is now the largest single component of the residual** — a reversal
from every prior unit in this campaign (P08/P08b/P09c/P11 all found CPU submission
dominant); once both zone-merge and real animated composites land, the frame is
fill-rate/GPU-sync-bound at befallen's worst poses, not CPU-submission-bound.

**9. Recommendation**

Befallen's full stack (merge + real animated composites) is now within ~2ms of the
30fps p95 line and already under budget on average — the closest this campaign has
ever gotten. What remains: **GPU sync (glFinish/fill-rate), not CPU submission**,
is now the largest residual component (49.6% of the p95 frame at the full-stack
point) — the occluder-budget lever (P11's own verdict: "not worth landing on
befallen's canonical replay, ~1-3% effect") is not the answer; bf472's
704-region dense pose is the tail driving p95 over the line, and P08's own
occlusion-budget headroom finding (+225 regions) was measured at a different,
farther-view static pose, not this moving replay's bf472 specifically — a
follow-up unit re-running the occluder-budget experiment focused specifically on
bf472's pose (rather than the whole-path average P11 used) is the most direct next
step to see if that 2ms gap closes. Absent that, the honest verdict is: **befallen
is functionally at the 30fps line with the two levers already measured (merge +
composite) — no new lever is obviously required, only a possible tuning pass on
the existing occlusion-budget lever aimed at the specific worst-case pose.**

### Deviations from the plan

- No benchmark code changes were needed for A3 (entities_visible already
  populates on the animated composite-scenegraph path — the plan anticipated
  possibly needing to "extend the counter additively," but the existing
  P08b-era instrumentation already covers this route since both fall through
  the same default entity-collection branch).
- Design step B5c ("merge + composite-animated... if budget allows") was
  RUN (not skipped) — invocation budget (5/12) left ample room, and it's the
  plan's own "real KPI-table entry."
- A genuine mid-session bug (missing `--eqe1-path` on the Pi script's composite
  variants, causing a silent composite no-op) was caught via the draws_avg
  sanity check rather than reported as a false "~0%" finding, and fixed with
  2 additional invocations in the same session (not a second session) — see
  Phase B above for the full account.
- No production changes; no visual fixes; no preset flips — all out-of-scope
  items respected.
