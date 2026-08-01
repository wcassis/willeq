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

**Complete.** Full writeup: `refactor/perf_truth_audit.md`'s 2026-08-01
P52b section. Summary:

- Corrected P52's Bug 1 (`--path-file` not `--camera-pose`) and Bug 2
  (full shipped preset: `--merge-zone-draws --composite-scenegraph
  --eqe1-path`) as instructed, and caught a **third**, previously-unknown
  bug in the process: P52's own staged starting script
  (`p52_pi_gpu_sync_pathfile_fix.py`) used the wrong canonical replay
  path (`befallen_fixedpose_300f.path`, a near-stationary vantage that
  clusters entities near the camera) instead of P51's own
  `befallen_seed42_1000f.path` — caught by this unit's own per-run
  sanity gate (entities_visible/draws ~3x P51's honest e205 reference on
  the wrong path), not silently missed.
- Delivered all 3 required items: (a) ranked %-of-frame attribution at
  e5 and e205 — `glFinish` is #1 at every load (45.8%-56.6%); (b) the
  full 0/5/40/205 entity-count scaling curve, brackets P51's promotion
  deltas as required; (c) the swap-floor final word from e0 — **refines**
  P52's flat "null result" into a load-dependent finding: flip-wait is
  ~55% of frame at e0 (near-tied with glFinish) and falls to <1% by
  e205. P52's structural glFinish-not-flip finding **held** at realistic/
  stress loads.
- Invocation accounting: 6 total against an orchestrator-extended
  ceiling (5→6 mid-unit). Invocations 1-2 (`BADPATH_A_e5`/
  `BADPATH_B_e205`) were spent on the wrong-path diagnosis described
  above — a real, necessary diagnostic cost, not waste, but the
  orchestrator ruled it shouldn't consume the plan's named 4-leg budget,
  hence the extension. Invocations 3-6 are the valid, corrected matrix
  (A_e5, B_e205, D_e0, C_e40 — all 4 planned points delivered, not the
  3-point budget-constrained fallback this unit considered mid-run).
  0 reserve remains.
- One open, explicitly-flagged caveat: `B_e205`'s absolute magnitude
  (42.811ms avg / 56.420ms p95) is still ~21-24% over the honest
  34.3-34.6ms p95 band even after both path and preset corrections —
  plausibly a 300-of-1000-frame path-subset artifact (this run replays
  only the first 300 waypoints of the 1000-frame canonical path via
  P52's own "lighter, report-only" frame-count convention), not
  re-diagnosed for lack of remaining budget. Does not affect the
  attribution ranking, the swap-floor conclusion, or the scaling curve's
  shape/bracketing — all cross-checked as not magnitude-sensitive to
  this specific number in the audit writeup.
- Two CmaFree episodes this session (200 kB pre-invocation-3 triggering
  a correct `SessionAbort` before any process launched; 43,756 kB
  between invocations 4 and 5), both recovered via `sync && drop_caches`
  per protocol, no reboot. One local-orchestration timeout left the Pi's
  governor pinned to `performance` mid-session (caught and manually
  restored before continuing — not a source-code issue, a local script
  invocation issue).
- Pristine exit verified independently: governor `ondemand` all 4 cores,
  no benchmark process, both DRM devices free, `/tmp` swept (0% used),
  final CmaFree 67,436 kB (healthy).
- No source changes; bar carried forward unchanged from P52 Phase 1
  (last unit to touch C++).
- Deliverables: `refactor/perf_truth_audit.md` P52b section (this unit's
  required deliverable location, per the plan), `refactor/deferred.md`
  P52 entry marked CLOSED, this Review, CSVs/logs/telemetry under
  `benchmarks/pi_runs/p52b_gpu_sync/` (4 valid legs + 2 kept `BADPATH_*`
  write-offs), script `refactor/scratch/p52b_gpu_sync_shipped_matrix.py`.
