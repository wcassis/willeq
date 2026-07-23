# P02 — Resume hardware experiments under panic-safe protocol

Complete P01's blocked experiments on the recovered Pi, under a protocol
that protects the board and instruments the suspected panic mechanism.

## Protocol upgrades (implement in scripts/pi/pi_common.py first — these
are permanent improvements, committed)

1. **CmaFree telemetry**: sample /proc/meminfo CmaFree (+ MemAvailable) on
   the Pi before and after EVERY benchmark invocation; record in the run
   metadata; ABORT the session (clean, with governor restore) if CmaFree
   drops below 40MB or trends monotonically down across 5 consecutive runs
   — and LOG the trend prominently either way (a downward ratchet is the
   panic-mechanism finding).
2. **Session caps**: max 12 benchmark invocations per session; then a
   scripted `sudo reboot`, wait-for-ssh (fstab now auto-mounts NFS — verify
   mount after reboot), and continue. Cooldowns stay.
3. **Randomized/interleaved A/B ordering** in pi_bench_run.py (fixes the
   H3 confound permanently for the tier-2 gate too — note the committed
   baseline's EQE1 columns become non-comparable; re-bless the baseline at
   the end of this unit with the fixed harness).
4. The VM-side kmsg follower is running (log:
   /mnt/projects/fable/pi_console/kmsg_follow.log). If the Pi panics again:
   capture the follower log tail + your run correlation into the review,
   stop hardware work, report. kernel.panic=15 auto-reboots the board.

## Experiments

A. **6b — per-vertex vs per-pixel lighting on real geometry** (P01's top
   priority, zero data so far): add a flag-gated switch to the benchmark
   (`--lighting-model perpixel|pervertex|lightweight` — per-pixel is
   hard-coded at zone_bench_loader.cpp:209; investigate what the lightweight
   materials need to be selected) and A/B on qeynos2 + gfaydark. Compare
   against docs/reference/orangepione/perpixel.txt's historical ~3x claim.
B. **EQE1 instability characterization**: 8+ interleaved EQE1/RGBA runs
   (within session caps, across 2-3 sessions with reboots between) with
   CmaFree telemetry — test whether EQE1's >49% spread correlates with CMA
   state, reboot recency, or session position. RGBA runs double as
   stability controls.
C. **H2 runtime spot-check** (small): the GL filter-state audit P01
   answered only from source — one instrumented run confirming actual
   min/mag filters on a few zone textures per path.

## Deliverables

- Review section: 6b verdict with numbers vs history; EQE1 instability
  characterization (CMA-correlated or not); updated fix-candidate list.
- Re-blessed pi_baseline with the interleaved harness (both paths, honest
  tolerances from the new variance data).
- Pi pristine after every session; all timed I/O via Pi /tmp per the
  standing rule.

## Review

**STATUS: complete. No kernel panic occurred this unit. The panic-safe
protocol's CmaFree abort fired three times — each a clean, governor-restored
stop — and in doing so produced the unit's headline finding: the P01 panic
mechanism is confirmed as CMA exhaustion driven by an EQE1-correlated
kernel-side memory leak that survives process exit and is only recovered by
reboot.**

### Protocol upgrades (committed, permanent — all four in before any experiment ran)

1. **CmaFree telemetry + abort thresholds** (`scripts/pi/pi_common.py`:
   `read_pi_meminfo`, `CmaMonitor`, `SessionAbort`): CmaFree + MemAvailable
   sampled before/after every benchmark invocation, appended to a per-run
   `cma_telemetry.tsv`, trend logged every sample. Aborts cleanly (governor
   restore still runs) below 40 MB CmaFree or on a monotonic downward
   ratchet across 5 consecutive samples. **Fired 3 times this unit** (20.2 MB,
   3.3 MB, 24.3 MB) — worked exactly as designed each time.
2. **Session caps + scripted reboot cycle** (`SessionRunner`,
   `scripted_reboot_and_wait`, `wait_for_ssh`, `verify_nfs_mounted`,
   `run_session`): default 12 invocations per session (CLI-tunable via
   `pi_bench_run.py --session-cap`; lower it for EQE1-heavy runs), then
   `sudo reboot` + ssh polling + NFS mount verification + binary re-stage +
   fresh governor pin. Two real bugs were found and fixed live: (a) the
   reboot wipes Pi /tmp (tmpfs), deleting the staged benchmark binary — the
   first capped session's invocation 13 failed exit-127; fixed with
   `run_session(post_reboot_fn=...)` re-staging. (b) a trailing reboot after
   the final invocation wasted ~90 s and could push bounded callers past
   their timeout before results were written; fixed by rebooting only when
   more work remains.
3. **Randomized/interleaved A/B ordering** (`randomized_variant_order`,
   `pi_bench_run.py --repeats/--seed`): every pass shuffles the full
   (zone, variant) cross product fresh; per-rep CSVs (`_r<N>` suffix) are
   pulled to NFS immediately after each invocation (which saved 10/12
   measurements when an abort split the re-bless run), and
   `summarize_multi_csv` pools post-warmup frames across reps with per-rep
   p95s recorded for transparency. H3's fixed rgba-then-eqe1 order is gone
   from the harness permanently.
4. The VM-side kmsg follower ran throughout
   (`/mnt/projects/fable/pi_console/kmsg_follow.log`); no panic entries were
   ever appended (verified before/after sessions). `run_zone_bench_generic`
   was also added so ad hoc experiments (e.g. `--lighting-model`) reuse the
   same staging/pose/I-O machinery as the gate instead of forking it.

### Headline: the P01 panic mechanism is confirmed — EQE1 leaks CMA kernel-side

Per-invocation CmaFree deltas across 30 telemetered invocations (blocks B,
re-bless, confirm; fresh reboot = 102,056 kB every time):

- **EQE1 invocations drain CMA massively and variably**: -14.6, -16.7,
  -20.4, -24.3, -27.6, -58.3, -59.3, and **-80.9 MB in a single 60-frame
  invocation** (gfaydark, the worst repeat offender: -14.6/-58.3/-80.9 across
  its three largest). Small/no-drain EQE1 runs exist too (-0.4, +0.9, -3.1) —
  the drain is episodic, not per-run-constant.
- **RGBA invocations are neutral or RECOVER CMA** (+0.1, +4.2, +6.2, +7.0,
  +19.9 MB; the first DRM run after any boot costs a one-time ~3-9 MB,
  plausibly scanout/framebuffer allocations).
- **The drained CMA is NOT returned when the benchmark process exits** —
  after the 20.2 MB abort, CmaFree was still 20.2 MB with zero benchmark
  processes alive; only a reboot restores it (to exactly 102,056 kB, five
  times out of five). This is a kernel-side leak (GEM/CMA buffers not freed
  on process teardown somewhere in the lima/sun4i-drm stack when the ETC1
  texture path is exercised), not userspace accumulation.
- The 3.3 MB CmaFree event (one gfaydark EQE1 run drained -80.9 MB) is
  essentially the P01 pre-panic state reproduced under full telemetry: P01
  ran 41 uninstrumented invocations, many EQE1, with no reboots — cumulative
  CMA exhaustion to ~zero, then an asynchronous allocation failure in the
  kernel, matches every observable from the P01 panic record. **Fix
  candidate #1 below.**
- Control: block 6b's 12 invocations + first-attempt's 12 (all
  RGBA-textured, no --eqe1-path) held CmaFree stable within ±100 kB for 24
  DRM init/teardown cycles — rapid DRM cycling per se does NOT leak; the
  EQE1/ETC1 path does.

### Experiment A / 6b verdict: per-pixel lighting costs +17-18% frame time on real geometry — the historical ~3x does NOT reproduce

12 invocations (qeynos2 + gfaydark × perpixel/lightweight × 3 reps,
randomized interleaved, 60 frames each, RGBA textures), via the new
flag-gated `--lighting-model perpixel|pervertex|lightweight`
(`tools/hwcaps/gles2_zone_benchmark.cpp`; "pervertex" and "lightweight" are
the same shader variant in this codebase — the `*_LIGHTWEIGHT_SRC` programs,
all 8 lights per-vertex, no per-pixel player light — implemented as
`--adaptive-lighting` + `--zone-pp-budget 0`, default behavior unchanged
when the flag is absent):

| zone | perpixel p95 (per-rep) | lightweight p95 (per-rep) | frame-time delta | fps(p95) |
|---|---|---|---|---|
| qeynos2 | 34.74 ms [34.80, 34.83, 34.74] | 29.35 ms [29.28, 29.38, 29.34] | **-15.5% (pp is +18.4%)** | 28.8 → **34.1 (crosses 30fps KPI)** |
| gfaydark | 41.05 ms [41.09, 41.05, 41.13] | 35.07 ms [35.12, 35.00, 35.09] | **-14.6% (pp is +17.0%)** | 24.4 → 28.5 |

Per-rep spreads under 0.3% — the interleaved protocol produces answers the
old ordered harness never could. GPU-only cost per pose (steady-state mean
`gpu_ms`): qeynos2 18.91→11.02 / 12.69→6.52 / 20.73→15.26 ms
(**1.36-1.95× per-pixel/lightweight ratio**); gfaydark 32.36→26.42 /
18.39→11.14 / 21.87→18.16 (1.20-1.65×).

**Verdict vs the historical claim** (docs/reference/orangepione/perpixel.txt:
7.3 ms lightweight vs 23.3 ms per-pixel, >3×, synthetic fullscreen quads at
1280×720): the *direction* is confirmed — per-pixel shading is the largest
single GPU-cost lever measured on this hardware — but the *magnitude* does
not transfer to real zone geometry: GPU fragment cost is 1.2-2.0× (not 3×),
and total frame time only +17-18%, because real frames are partly
CPU/draw-call-bound and many fragments are killed by depth/culling rather
than fully shaded. Will's memory that "fps was consistently high with
per-pixel lighting DISABLED" is real and quantified — but on today's
codebase it's a ~5-6 ms/frame lever, not a 16 ms one. Notably it is enough
to push qeynos2 over the 30fps KPI bar.

### Experiment B: EQE1 instability characterized — bimodal stall-tails, NOT CMA-level-correlated; steady-state EQE1 is actually FASTER than RGBA

11 interleaved qeynos2 runs (6 EQE1 + 5 RGBA; the CmaFree abort cut session
1 at 5 invocations, so one planned RGBA rep is absent) across 2 sessions
with a reboot between:

- **RGBA control: p95 34.68-34.84 ms — 0.5% spread** across everything
  (session position, CMA level 72-92 MB, reboot recency). Deterministic.
- **EQE1: bimodal per-invocation.** Fast mode: 30.70, 30.54 ms p95 —
  *faster than RGBA*. Slow mode: 39.02, 40.15, 42.60, 42.90 ms p95. Within a
  run, the steady-state frame pattern is identical in both modes (per-cycle
  30/18/29 ms — again faster than RGBA's 33/20/34); slow-mode runs differ
  ONLY by sporadic stall bursts (1-4 consecutive frames at +10-30 ms, e.g.
  ...30 32 43 47 39 40 30... and an isolated 60 ms frame). p95 of a 57-frame
  sample is captured by its 3rd-worst frame, so a single burst flips a run
  from "fast" to "slow" mode.
- **No correlation with CMA state or session position**: the freshest-CMA
  run (r3, 102 MB post-reboot) was slow-mode; a depleted-CMA run (r4,
  77.7 MB) was fast-mode. First-of-session runs appear in both modes.
- **Revision of P01's H4**: ETC1 sampling is NOT slower on average — it is
  faster at steady state (bandwidth savings are real). The "EQE1 slower"
  signal in H04/P01 is tail contamination from episodic stalls, plausibly
  kernel/driver memory-management activity (the same subsystem the CMA leak
  lives in) interrupting rendering, plus H04's order confound. RGBA never
  exhibits the bursts.

### Experiment C / H2 runtime spot-check: source-level answer confirmed on the real driver

`--dump-filters 8` (new flag) on qeynos2, both paths, real Lima driver
readback via `glGetTexParameteriv`: unit-0 (main) filters are **identical**
between the RGBA and EQE1 runs (`MIN=GL_LINEAR MAG=GL_LINEAR` for every
sampled entry — these particular textures carry no mip chains, so the
LINEAR_MIPMAP_LINEAR branch doesn't engage), and unit-1 (companion)
`GL_NEAREST/GL_NEAREST` as the source predicts. No filter asymmetry exists
on the path that carries the draws; P01's H2 refutation stands, now
runtime-verified.

### Re-blessed baseline (benchmarks/pi_baseline, committed)

Blessed from `p02_rebless`: 2 interleaved reps per zone/variant (12
invocations, 60 frames each, randomized seed 802), pooled p95 with per-rep
values recorded in `baseline.json`, per-rep CSVs stored. The run was split
across 3 sessions by two CmaFree aborts (both after gfaydark/eqe1 drains);
every completed invocation's CSV had already been pulled, so nothing was
re-measured.

| zone | rgba p95 (reps) | eqe1 p95 (reps) |
|---|---|---|
| qeynos2 | 34.80 [34.80, 35.07] | 39.23 [39.23, 39.85] |
| gfaydark | 41.10 [41.28, 41.08] | 39.09 [39.21, 38.94] |
| befallen | 44.38 [44.38, 44.41] | 43.17 [45.68, 43.17] |

With the order confound removed, **EQE1 ≈ RGBA or slightly faster on 2 of 3
zones**; qeynos2/eqe1's +12.7% is stall-tail, not steady-state cost. H04's
blessed "EQE1 consistently slower" columns are superseded.

**Tolerance: ±30%** on pooled p95 (was ±40%). Evidence: RGBA needs <2%;
EQE1's stall-mode bimodality intrinsically swings single runs -23%/+9%
around their median, and the confirmation run's qeynos2/eqe1 landed at
**+26.8%** — a genuine, benign stall-mode sample that ±25% would have
false-failed. Confirmation compare (`p02_confirm`, fresh session, seed 900):
**PASS all 6 zone/variant rows, draw/state/poly counts exact** (RGBA rows
within ±0.3%).

All zones remain **below the 30fps KPI** at these poses on both texture
paths (qeynos2/rgba 28.8 fps closest) — unchanged from H04, but see fix
candidate #2: lightweight lighting puts qeynos2 at 34.1 fps.

### Updated fix-candidate list (prioritized)

1. **NEW / top: the Lima/sun4i CMA leak on the ETC1 path is a stability
   bug, not a perf tradeoff.** Single EQE1 benchmark invocations leak up to
   81 MB of CMA that only a reboot recovers; cumulative exhaustion kernel-
   panicked the board in P01. Actions: build a minimal reproducer (one
   ETC1-upload/teardown loop), check lima-ml/kernel bugtrackers, report
   upstream; until fixed, treat EQE1 on this device as
   session-length-limited (the session-cap + CmaFree-abort protocol now
   enforces exactly this) and prefer RGBA for long-running client sessions.
2. **Ship a lower zone per-pixel budget on the Mali400/constrained preset.**
   All-lightweight is worth 15-18% frame time and moves qeynos2 from 28.8 to
   34.1 fps — over the KPI bar. The production adaptive-budget mechanism
   already exists (`renderer_zone_draw.cpp` zonePPBudget); this is a tuning
   change, testable per-zone with `--lighting-model`/`--zone-pp-budget`
   sweeps. Best measured effort-to-impact ratio in the whole investigation.
3. **Reconsider EQE1's role on this device with better statistics, after
   (1).** Steady-state EQE1 is faster than RGBA and 8× smaller in texture
   memory — the case for it is *better* than H04's baseline suggested — but
   the stall bursts (and the leak) currently make it strictly worse for
   frame-time consistency. If the leak fix also removes the bursts (same
   subsystem), EQE1 likely becomes the right default; re-measure then.
4. **Gate-metric hygiene**: p95-of-57-frames flips modes on a single stall
   burst. For EQE1 rows, consider longer captures (120+ frames) or a
   trimmed metric (e.g. p90, or median + burst count as separate columns) if
   the gate ever needs to be tightened below ±30%.
5. Culling correctness/perf work remains the dominant geometry-side lever
   (P01 experiment 7: 12×+ draw reduction; unchanged by anything found
   here).

### Panic events

**None.** kernel.panic=15 auto-reboot was never exercised; the kmsg follower
log shows only normal boot messages for the unit's five reboots (3 scripted
protocol reboots, 2 manual recovery reboots after aborts). The three
SessionAbort events each left the Pi governor-restored and reachable.

### Pi final state

Verified pristine and then rebooted once more for a fresh start: /tmp
empty (only systemd-private dirs), governor `ondemand` ×4, no benchmark
processes, NFS mounted, CmaFree 102,056 kB.

### Deviations / honesty notes

- **First 6b attempt produced unusable timing data**: I initially ran each
  invocation with only 3 frames (one pose cycle — entirely cold-start, which
  `summarize_csv` discards as warmup). Re-ran at 60 frames. The 12
  short-run CSVs are kept under
  `benchmarks/pi_runs/p02_investigation/cold3_firstattempt/` (gitignored)
  and their CMA telemetry remains valid control data (12 RGBA-texture DRM
  cycles + 1 reboot cycle, CMA stable).
- The re-bless required 3 sessions due to two protocol aborts; per-
  invocation rsync meant zero re-measurement. The session-cap reboot's
  /tmp-wipe bug (exit-127) and the trailing-reboot waste were found by these
  runs and fixed in the committed harness.
- Block B collected 6 EQE1 + 5 RGBA runs (plan asked 8+ total interleaved;
  11 delivered) — the missing 12th (rgba r2) was cut by the first abort.
- Experiment A used RGBA textures only (isolating the lighting variable);
  lighting-model × EQE1 interaction was not measured.
- The `--lighting-model` and `--dump-filters` flags are committed
  (flag-gated, default-off, default behavior byte-identical); the ad hoc
  orchestration (`p02_run.py`, `p02_rebless_finish.py`) stayed in the
  session scratchpad, not the repo. No scratch instrumentation exists in
  committed source.
- Full VM build + ctest run for the committed diff: 2 failures, both
  preexisting/environmental and unrelated to this diff
  (`AudioManagerTest.PreloadCommonSounds` — no ALSA device on this VM;
  `RDPServerFrameTest.UpdateFrameMultipleTimes` — intermittent segfault that
  passes on rerun). The changed target (`gles2_zone_benchmark`) builds clean
  on VM and ARM; all Pi runs used the ARM build from THIS worktree
  (`build-arm-noble/` here, built by pointing `scripts/build-arm-noble.sh`'s
  own directory resolution at the worktree — main was never touched).
- H04's `bench_pi.sh`/`compare_pi.sh` wrappers and `pi_bench_compare.py`
  needed no changes: the pooled multi-rep summary intentionally keeps the
  same dict shape.
