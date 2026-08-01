# P52b — GPU-sync matrix re-run under the corrected config (completes item 6)

**Slot: slot3. OWNS THE PI (P02 protocol; ≤5 invocations). Errand
completing P52's authorized scope — its Phase 2 magnitudes were
invalidated by two self-caught methodology bugs.**

## Background (read first)

refactor/P52_gpu_sync_characterization.md Review (both phases) —
especially: Lima has NO EXT_disjoint_timer_query (CPU-side
sync-point attribution is the ONLY path; GPU columns stay -1);
--camera-pose runs NO per-frame instrumentation (must use
--path-file); the recovery pair omitted the shipped-preset flags
(--merge-zone-draws --composite-scenegraph --eqe1-path) making
B_stress 232.7ms garbage. The corrected script is staged:
refactor/scratch/p52_pi_gpu_sync_pathfile_fix.py (adapt: add the
shipped flags). Also read perf_truth_audit.md's P51 + P52 sections
(honest config = promotion ON default; canonical band 34.3-34.6;
the trustworthy P52 finding = glFinish is the block, flip-wait
~0.1ms — your numbers should re-confirm it incidentally).

## Work

1. Corrected matrix, --path-file canonical replay, FULL shipped
   preset flags, --gpu-sync-probe: e5 (+ --gl-counters), e205, e40,
   e0 — 4 invocations, 1 reserve. Sanity per run: totals must land
   near the honest references (e5 ≈ 10.8ms avg / e205 ≈ 34.3-34.6
   p95 band) or STOP and diagnose before burning budget.
2. Deliver what P52 owed: (a) ranked percentage-of-frame attribution
   of the GPU-sync share (glFinish-wait by phase/load, swap,
   flip-wait) at realistic e5 AND stress e205; (b) the entity
   scaling curve 0/5/40/205 (must bracket P51's +0.455ms e5 /
   +2.22ms e205 promotion deltas consistently); (c) swap-floor
   final word (e0's near-zero-work frame time = the floor).
3. Update perf_truth_audit.md's P52 section (mark the confounded
   table superseded, add the valid one + options note), deferred.md,
   plan Review here. CSVs → benchmarks/pi_runs/p52b_gpu_sync/.

## Rules

- No source changes expected (instrumentation already landed) — if
  a probe bug surfaces, fix minimally + note.
- Pi protocol absolute; DRM mode line per run; pristine exit.
- Bar (measurement errand): builds clean/non-stub, gate PASS only if
  source touched.

## Review

(executor fills in)
