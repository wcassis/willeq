# Q01 — Benchmark camera-path record/replay + harness alignment to production lighting config

Approved by Will 2026-07-24 ("go ahead with the hygiene pair").
Slot: wt/slot1, branch task/Q01-path-replay. Runs in parallel with Q02
(slot2, production draw-order key) — file sets are disjoint.

## Motivation

1. **Path drift (six occurrences).** The navmesh-driven moving camera path
   (`--entities 200 --seed 42`) has failed to transfer across sessions six
   times (March logs → P05 → P06 → P07b/P07d…). Identical command, same
   worktree, different session → different poses; every perf unit re-finds
   poses from scratch and cross-unit frame-time comparison is impossible
   (deferred.md "Benchmark camera path is not reproducible across builds").
   The CSV already records per-frame pos/heading/pitch — replay is the only
   missing piece.
2. **Harness misalignment (P07c salvage).** The visual gate
   (tests/goldens/ + scripts/ci/visual_gate.py) and the Pi tier-2 baseline
   (benchmarks/pi_baseline/, scripts/pi/) measure FULL per-pixel lighting —
   no `--adaptive-lighting` flags anywhere — while production ships adaptive
   count mode (orangepi preset: zone budget 20, object budget 10,
   constrained_renderer_config.cpp:307). The harness should measure what
   ships. P07c did this alignment but against area mode (rejected at
   orchestrator review); redo it with count flags. Requires a golden
   re-bless by Will (orchestrator handles the bless at review — this unit
   produces the evidence).
3. **Silent orbit fallback.** x86/llvmpipe moving runs silently fall back to
   an orbit camera unless `--navmesh-path /mnt/projects/maps/nav` is passed;
   not in the help text (deferred.md, P06 review).
4. **Repo hygiene.** `benchmarks/pi_runs/` is gitignored (H04-era), which is
   how P05's "committed" CSVs silently never landed (salvaged 3fb85fc).

## Design

### A. Path record/replay (tools/hwcaps/gles2_zone_benchmark.cpp + zone_bench_loader)

1. `--dump-path FILE`: on any moving-path run, write the camera path as a
   text file — `#`-prefixed header comments recording zone, seed, entities,
   frames, and the full generating command, then one line per rendered
   timed frame: `x,y,z,heading,pitch` (EQ coords + degrees — exactly the
   `--camera-pose` convention). CSV frame row i must correspond to path
   line i.
2. `--path-file FILE`: replay — the moving camera path is loaded from the
   file, replacing navmesh/orbit generation entirely. Mutually exclusive
   with `--camera-pose` and `--pose-search`. `--frames` greater than the
   path length is an ERROR (explicit, no silent looping); fewer is fine
   (prefix replay, warn on stderr).
3. **Entity placement must reproduce.** `--entities N` places entities
   evenly along the camera path (zone_bench_loader.cpp
   "Place entities at evenly-spaced positions along a camera path"), so
   replay must feed the SAME path representation the placement code
   consumes. Investigate how the navmesh path → per-frame pose → entity
   placement pipeline actually flows before choosing the injection point;
   the acceptance test below (byte-identical metrics record vs replay) is
   the arbiter. Document what you found and where the file is injected.
4. **Warmup frames**: decide and document how warmup interacts with the
   path (e.g. warmup holds pose[0]); requirement is only that timed frame
   i ↔ path line i, byte-stable across runs.
5. **Canonical committed paths**: record on x86 llvmpipe with
   `--navmesh-path /mnt/projects/maps/nav --entities 200 --seed 42
   --frames 1000` for each golden zone →
   `benchmarks/paths/{qeynos2,gfaydark,befallen}_seed42_1000f.path`,
   committed (directory is not gitignored — verify). These become the
   standard moving paths for all future perf units (P08 will replay the
   befallen one).
6. **Help text + loud fallback**: document `--navmesh-path` (and the
   `/mnt/projects/maps/nav` convention) in the moving-path section of the
   help text; when a moving run falls back to the orbit camera because the
   navmesh is missing/unloadable, print an unmissable stderr WARNING naming
   the fallback and the flag that avoids it.

### B. Harness alignment to production lighting config

7. **Single source of truth**: add a top-level `productionConfig` block to
   tests/goldens/manifest.json, e.g.
   `{"args": ["--adaptive-lighting", "--zone-pp-budget", "20",
   "--pp-budget-mode", "count"], "note": "mirror of orangepi preset,
   constrained_renderer_config.cpp"}`. Consume it in BOTH
   scripts/ci/visual_gate.py (run_benchmark cmd) and scripts/pi/
   (pi_common.py's benchmark invocation). Do NOT put these flags in the
   per-zone `extraArgs` — pi_common.build_pose_args asserts extraArgs
   contains only `--camera-pose` entries (pi_common.py:425-434); keep that
   invariant.
8. **Mirror only what the benchmark can express.** Zone budget 20 + count
   mode are mirrorable. The object budget (10) is not meaningful unless the
   harness passes `--objects` (it doesn't; adding objects changes scene
   content and is NOT this unit). Document residual gaps between production
   config and harness flags in the review.
9. **Regenerate goldens** with `visual_gate.py --bless`; commit new goldens
   + manifest. Produce an old-vs-new contact sheet (all 9 views, old golden
   | new golden | diff) → `benchmarks/contact_sheets/q01_count_rebless/`,
   plus per-view pixel-diff stats (max delta, % pixels — expect real diffs:
   count mode unlights distant regions; that is the point). Set the
   manifest `approval` block to
   `{"status": "pending", "note": "Q01 count-mode alignment — awaiting
   Will's re-bless"}`; the orchestrator presents the sheet to Will and
   finalizes the approval block at merge time.
10. **Re-capture the Pi tier-2 baseline** under the aligned config using
    the H04 harness (scripts/pi/bench_pi.sh → pi_bench_run.py, its own
    randomized/interleaved protocol; read refactor/H04_pi_tier2.md and
    benchmarks/pi_baseline/baseline.json for how the current baseline was
    produced). New CSVs + updated baseline.json → benchmarks/pi_baseline/
    (this directory IS committed). Old baseline stays in git history; note
    the pre-replacement hash in the review and report old-vs-new per-zone
    deltas (expect faster frames under count demotion; ±40% tolerance
    context from H04).

### C. Repo hygiene

11. Remove the `benchmarks/pi_runs/` ignore rule (.gitignore:111 area) so
    unit deliverable CSVs can never silently vanish again. After
    unignoring, `git status` the directory: commit THIS unit's artifacts
    normally; do NOT mass-add pre-existing untracked run dirs — list them
    (with sizes) in the review for orchestrator decision.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  scripts/build-arm-noble.sh; scripts/refactor/check_file_sizes.sh fable.
- **Visual gate**: PASSES against the REGENERATED goldens (this unit
  intentionally changes them — the ctest `visual_gate` entry must be green
  on the branch tip). Contact sheet + stats as in design point 9.
- **Replay determinism (the headline)**, all with the aligned count config:
  - x86: record run R0 (navmesh, `--dump-path`) then replay run R1
    (`--path-file` with R0's path, same build/machine): ALL metric columns
    byte-identical R0 vs R1, and pose columns byte-identical to the path
    file. Then a second replay R2: identical to R1. Full-file programmatic
    checks, not spot checks.
  - Pi (P02 protocol): one qeynos2 replay run with the canonical committed
    path: pose columns byte-identical to the path file;
    draws/regions_vis/regions_drawn/polys byte-identical to the x86 replay
    (P07d precedent says geometry columns match across machines — if they
    do not, that is a reportable finding, not something to paper over).
  - Orbit-fallback warning demonstrated: one x86 moving run without
    `--navmesh-path`, stderr WARNING captured in the review.
- **Tier-2 re-capture** per design point 10, plus the summary table
  old-vs-new baseline.
- Pi rules: P02 protocol via scripts/pi/pi_common.py, ALL timed-run I/O to
  Pi-local /tmp only, stripped staged binary built from slot1
  (ARM_BUILD_BIN_DIR trap — env.sh defaults to the main worktree), drop
  caches (never reboot) for CmaFree, leave Pi pristine, report blocked if
  unreachable.

## Out of scope

- Changing any production default or preset (harness-only alignment).
- Passing `--objects` to the harness / mirroring the object budget.
- Converting historical CSVs (March/P05) into path files.
- Path files for other seeds/entity counts; P08 itself.
- Removing the gitignore rules for build dirs etc. — only pi_runs/.

## Review

### Implementation notes

- **Injection point (A3 investigation).** Entity placement
  (`zone_bench_loader.cpp` `placeEntitiesAlongPath`) previously sampled
  `camera.getPositionAt(t)` for `t` evenly spaced across
  `camera.totalLength()` -- the arc length of the *entire* raw
  navmesh/orbit polyline, evaluated once before the render loop, completely
  independent of the frame loop's own `pathDist += speed*dt` walk. That
  scheme has no equivalent once the navmesh isn't available (replay), and
  even if it did, the raw polyline's length has no fixed relationship to
  how much of it 1000 measured frames actually traverse. Fix: resolve one
  `std::vector<TracePose> fullTrace` (warmup + measured, one entry per
  rendered frame -- `x,y,z,heading,pitch` and the direction vector) *before*
  entity placement, then feed placement the trace's MEASURED-frame
  positions directly instead of resampling the raw polyline. Record mode
  builds the trace by walking `pathDist`/`camera.getPositionAt()` exactly
  as the old per-frame loop did (same math, just precomputed as an array
  instead of interleaved with rendering); replay mode builds it by parsing
  the `--path-file` and holding pose[0] for warmup frames. The render loop
  itself now just indexes `fullTrace[frame]` -- record and replay share
  exactly one code path for "what pose does frame N have," which is what
  makes the record/replay CSVs byte-identical rather than merely close.
  `gles2_zone_benchmark.cpp:498-591` has the full comment/design writeup;
  `:1682-1848` is the resolved-trace + placement site.
  This is a real behavior change for anyone who runs `--entities` +
  moving-path *without* `--dump-path`/`--path-file` (pre-Q01 P05/P06/P07
  runs are unaffected, they're historical CSVs, converting them is out of
  scope) -- entities now land at different points along the path than
  before. No canonical golden or CI path uses `--entities`, so this doesn't
  touch the visual gate.
- **trainMode interaction.** `--train` overrides the camera with
  `generateApproach()` *after* the old entity-placement block; since the
  new trace is resolved once, up front, and used by both entity placement
  and the render loop, the trainMode override was moved earlier (right
  after camera-path setup, before the trace build) so the trace reflects
  it. `--train` combined with `--path-file` is rejected outright
  (`ERROR: --train is not supported with --path-file`) rather than
  silently ignoring one of the two flags -- not exercised by any script,
  out of scope to reconcile.
- **Warmup handling (A4).** The path file only records rendered MEASURED
  (post-warmup) frames -- there is no navmesh data to fall back on for
  warmup frames during replay. Replay holds `pose[0]` for all warmup
  frames (`fullTrace[0..warmup)`), converting it once via `poseToDirection`
  (the same heading/pitch -> direction inverse already used by
  `--camera-pose`). Warmup frames are never measured/dumped/compared, so
  this only needs to prime GPU/driver state (shader binds, texture cache
  warm-up), not reproduce a specific trajectory.
- **Round-trip precision.** `--dump-path` writes pose columns with `%.9g`
  (the documented minimum to round-trip an IEEE-754 float32 exactly) rather
  than the CSV's `%.1f` -- required so `--path-file` reconstructs the exact
  same camera position/direction floats that produced the recording,
  not just values that agree to 0.1 EQ units.
- **Orbit fallback warning (A6).** Previously silent whenever
  `--navmesh-path` was omitted or the `.nav` file failed to load (deferred.md
  / P06 review). Now an unmissable `WARNING:` line on stderr names the
  fallback and the flag/convention (`/mnt/projects/maps/nav`) that avoids
  it; also documented in `--help`'s `--navmesh-path` entry and a new
  "Moving camera-path record/replay" help section.
- **Mutual exclusivity.** `--path-file` rejected outright if combined with
  `--camera-pose`/`--pose-search` (both already replace the moving path
  with something else). `--frames` greater than the path file's length is
  an ERROR (exit 1); fewer is a prefix replay with a stderr WARNING.

### Determinism check results (headline)

All checks are full-file programmatic comparisons (Python `csv.DictReader`,
column-by-column), not spot checks. "Metric columns" below means every CSV
column except `cpu_ms,gpu_ms,total_ms,anim_cpu_ms` (wall-clock timing,
inherently non-reproducible across separate process runs/machines --
same interpretation P07d used: "draws/regions_vis/regions_drawn/polys
byte-identical", see its review). Runs used the aligned production config
(`--adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count`) except
where noted.

- **x86, 1000-frame moving path, qeynos2** (canonical
  `benchmarks/paths/qeynos2_seed42_1000f.path` used for the record-side
  smoke test, `--entities 200 --seed 42`): record R0 -> replay R1
  (`--path-file`) -> replay R2. **0 diffs** on all non-timing metric columns
  across R0-vs-R1, R1-vs-R2, R0-vs-R2. Pose columns (`pos_x,y,z,heading,
  pitch`) **0 mismatches** vs the path file (rounded to the CSV's `.1f`)
  for all three runs, all 1000 rows. **PASS.**
- **x86, orbit-fallback warning demonstrated**: a moving qeynos2 run
  without `--navmesh-path` printed
  `WARNING: no navmesh path available (opts.navmeshPath=<empty>); falling
  back to a synthetic ORBIT camera path, not the real navmesh-driven path.
  Pass --navmesh-path /mnt/projects/maps/nav (or wherever the zone's .nav
  file lives) for the real moving path.` **PASS.**
- **`--frames` > path length**: `ERROR: --frames 31 exceeds --path-file
  '...' length (30 poses)`, exit nonzero, no run attempted. **PASS.**
- **`--frames` < path length (prefix replay)**: `WARNING: --frames 10 <
  --path-file '...' length (30 poses); replaying prefix (first 10 poses)`,
  run proceeds using the prefix. **PASS.**
- **Pi, qeynos2 canonical-path replay** (`benchmarks/paths/
  qeynos2_seed42_1000f.path`, `--drm`, P02 protocol, staged stripped binary
  built from this worktree's `build-arm-noble/`, output to Pi-local `/tmp`
  then rsynced back): pose columns **0 mismatches** vs the path file (1000
  rows). `draws/regions_vis/regions_drawn/polys` **0 diffs** vs the x86
  replay of the same path under the same config (`qeynos2_x86_canonical_
  replay.csv`), all 1000 rows -- geometry decisions reproduce exactly
  across x86 llvmpipe and the Pi's Lima/GLES2 driver, consistent with the
  P07d precedent that these columns are machine-independent. **PASS**
  (not a reportable divergence -- the plan flagged this as a possible
  finding if it failed; it didn't).

### Golden re-bless (harness alignment)

- `tests/goldens/manifest.json` gained a top-level `productionConfig`
  block (`--adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count`),
  consumed by both `scripts/ci/visual_gate.py` (`run_benchmark`, prepended
  before each zone's `extraArgs`) and `scripts/pi/pi_common.py`
  (`run_zone_bench_generic`, prepended before the per-call
  `extra_cli_args` so an investigation script's own override, e.g. P07d's
  `--pp-budget-mode radius`, still wins -- the benchmark's arg parser is
  last-flag-wins). `pi_common.pose_flags_repeated`'s invariant (extraArgs
  is `--camera-pose` entries only) is untouched -- productionConfig is a
  separate top-level key, never merged into any zone's `extraArgs`.
- Residual gap (documented per design point B8): the orangepi preset's
  object budget (10) is NOT mirrored -- only meaningful with `--objects`,
  which the harness doesn't pass (out of scope, would change scene
  content).
- Regenerated via `visual_gate.py --bless`; `visual_gate.py` (no `--bless`)
  then **PASSED** against the new goldens (also confirmed via
  `ctest --test-dir build -R visual_gate`, test #1225, part of the full
  ctest run below).
- Old-vs-new contact sheet + per-view pixel-diff stats:
  `benchmarks/contact_sheets/q01_count_rebless/{index.html,stats.json}`.
  Real diffs, as expected -- count mode unlights distant regions past the
  zone budget of 20:

  | view | max_delta | % pixels changed |
  |---|---|---|
  | qeynos2 f0 | 170 | 30.1% |
  | qeynos2 f1 | 113 | 11.0% |
  | qeynos2 f2 | 192 | 64.6% |
  | gfaydark f0 | 143 | 7.4% |
  | gfaydark f1 | 30 | 0.8% |
  | gfaydark f2 | 144 | 9.6% |
  | befallen f0 | 148 | 69.2% |
  | befallen f1 | 0 | 0.0% (identical -- this view has <=20 visible regions, below the count budget, so nothing gets demoted) |
  | befallen f2 | 86 | 68.6% |

- Manifest `approval` block set to
  `{"status": "pending", "note": "Q01 count-mode alignment -- awaiting
  Will's re-bless"}` per design point 9 -- **not** marked blessed by the
  executor; the previous `blessed`/`by: Will`/`date` fields were removed
  (they described the prior, now-superseded, full-per-pixel bless) rather
  than left stale.

### Pi tier-2 baseline re-capture

- Old baseline: `blessed_from_run: "p02_rebless"`, last touched by commit
  `5d1538e` ("P02: panic-safe protocol + lighting-model and EQE1-instability
  results"), `tolerance_pct: 30.0`.
- Re-captured via `scripts/pi/bench_pi.sh --run-name q01_tier2_rebless`
  (with `ARM_BUILD_BIN_DIR` overridden to this worktree's
  `build-arm-noble/bin` -- env.sh's default points at
  `/mnt/projects/fable/main`, the ARM_BUILD_BIN_DIR trap), one full
  randomized/interleaved pass (6 invocations: 3 zones x rgba/eqe1, 60
  static-pose frames each), no session-cap reboot needed (under the
  12-invocation cap), no `SessionAbort`. Blessed as the new baseline via
  `pi_bench_compare.py --run-name q01_tier2_rebless --bless
  --tolerance-pct 30.0` (kept the existing 30% tolerance for continuity;
  H04's original doc suggested 40%, but the currently-committed baseline
  had already been re-blessed at 30% by a later unit).
- Old-vs-new per-zone/variant p95 total_ms:

  | zone | variant | old p95 (ms) | new p95 (ms) | delta | old fps(p95) | new fps(p95) |
  |---|---|---|---|---|---|---|
  | qeynos2 | rgba | 34.802 | 30.787 | -11.5% | 28.7 | 32.5 |
  | qeynos2 | eqe1 | 39.234 | 27.379 | -30.2% | 25.5 | 36.5 |
  | gfaydark | rgba | 41.104 | 38.991 | -5.1% | 24.3 | 25.6 |
  | gfaydark | eqe1 | 39.086 | 32.888 | -15.9% | 25.6 | 30.4 |
  | befallen | rgba | 44.385 | 39.593 | -10.8% | 22.5 | 25.3 |
  | befallen | eqe1 | 43.169 | 38.213 | -11.5% | 23.2 | 26.2 |

  All six zone/variant combos are faster under count-mode demotion, as
  expected (fewer regions get expensive per-pixel lighting past the budget
  of 20). draws/state_changes/polys per designated frame are unaffected by
  lighting mode and remain exact-match against the manifest (checked by
  `pi_bench_compare.py`'s per-pose assertions, part of the bless run's
  underlying data).
- CmaFree dipped to 36144 kB during the separate 1000-frame moving-path
  replay determinism run (just under the 40960 kB abort floor -- that run
  is far heavier than the tier-2 baseline's 60-static-frame invocations,
  which never came close: min observed CmaFree during the baseline
  session was 60200 kB). Remediated with `sync && echo 3 >
  /proc/sys/vm/drop_caches` (never rebooted) immediately after, confirmed
  recovered to 80468 kB. Pi left pristine: staged binary and scratch CSV/
  log removed from `/tmp` after both sessions.

### Repo hygiene: `benchmarks/pi_runs/` unignored

- `.gitignore`'s `benchmarks/pi_runs/` rule removed (design point C11);
  this unit's own run dirs (`q01_tier2_rebless/`, `q01_replay_determinism/`)
  committed normally.
- Five pre-existing untracked run dirs found (predate the practice of
  individually `git add -f`'ing unit CSVs -- later units like P07d did
  force-add their own, which is why they don't show up here). Left
  untouched for the orchestrator's decision, NOT mass-added:

  | dir | size | files |
  |---|---|---|
  | `benchmarks/pi_runs/p01_investigation/` | 620K | 82 |
  | `benchmarks/pi_runs/p02_confirm/` | 112K | 14 |
  | `benchmarks/pi_runs/p02_investigation/` | 552K | 78 |
  | `benchmarks/pi_runs/p02_rebless/` | 212K | 27 |
  | `benchmarks/pi_runs/p03_investigation/` | 784K | 104 |

  (`p05_outskirts/`, `p06_area_budget/`, `p07d_radius/`, `p07_pp_floor/`
  are already tracked in git from their respective units' force-adds, so
  they don't appear as untracked and needed no action here.)

### Verification

- `cmake --build build -j24`: clean.
- `ctest --test-dir build --output-on-failure`: 1224/1225 passed
  (263s). Only failure: `AudioManagerTest.PreloadCommonSounds` (documented
  host exception). `RDPServerFrameTest.UpdateFrameMultipleTimes` (the other
  documented exception) passed this run. `visual_gate` (test #1225)
  **PASSED** against the regenerated count-mode goldens.
- `scripts/build-gles2-x86.sh`: clean.
- Visual gate (`scripts/ci/visual_gate.py`, no `--bless`): **PASS**
  (confirmed standalone and via the ctest run above).
- `scripts/build-arm-noble.sh`: clean, `gles2_zone_benchmark` built.
- `scripts/refactor/check_file_sizes.sh fable`: exit 0. One pre-existing
  WARN, not a new one -- `tools/hwcaps/gles2_zone_benchmark.cpp` was
  already over the 1500-line threshold before this unit (2210 lines on
  `fable`); this unit's ~230 net added lines (help text, path record/
  replay machinery, comments) brought it to 2442, still just a WARN (exit
  0), not a FAIL. Not addressed here -- out of scope for a hygiene/harness
  unit; a future split unit should pick this up (file-split candidate,
  see `refactor/F_file_split.md` conventions).
- Replay determinism (x86 + Pi): see above, all **PASS**.
- Tier-2 re-capture: see above, all zones faster under count mode, no
  SessionAbort, Pi left pristine.

### Deviations from the plan

- None material. The entity-placement resampling algorithm changed (arc-
  length-across-raw-navmesh-polyline -> index-into-resolved-measured-frame-
  trace) as an implementation necessity of design point A3 ("investigate...
  before choosing the injection point... the acceptance test is the
  arbiter") -- flagged here explicitly since it changes benchmark output
  for any future `--entities` + moving-path run that does NOT use
  `--dump-path`/`--path-file` (no canonical/CI path is affected).
- The manifest's stale `approval.by`/`approval.date` fields (describing the
  prior full-per-pixel bless) were dropped rather than kept alongside the
  new pending status, since they'd otherwise misleadingly suggest the new
  goldens were already reviewed by Will.
