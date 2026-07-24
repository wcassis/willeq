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

(executor fills in)
