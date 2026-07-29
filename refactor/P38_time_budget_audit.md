# P38 — First-principles frame time-budget audit (Pi, realistic load)

**Slot: slot1. OWNS THE PI (P02 protocol per EXECUTOR_PROTOCOL.md —
/tmp-only writes during timed runs, ARM_BUILD_BIN_DIR at THIS slot,
≤12 invocations/session, 2 sessions OK, pristine exit). No eqemu.**

Will's steering (2026-07-29, recorded): stop optimizing the KPI
margin; find the fundamental cost. Befallen is a Voodoo1-era zone;
real gameplay areas hold ~5 NPCs; nothing about that should stress a
Mali 400 — yet pre-optimization e205 ran 54ms and even now p95 GPU
sync is ~17ms and CPU submission ran ~9ms for ~8 draws. Explain the
gap between what a frame SHOULD cost and what it does. This is an
AUDIT: report + local experiments; production fixes are follow-up
units.

## Workload

REALISTIC, not stress: befallen canonical replay path with
`--entities 5` (plus one stationary fixed-pose run), production
config, shipped orangepi preset, 800x600. Same binary discipline as
all Pi units.

## Work

1. **Stage budget**: extend/use the benchmark's existing timing split
   (submission vs GPU-sync from P11-era instrumentation) to produce a
   per-stage table at the realistic load: input/sim, scene traversal,
   submission (draws, uniforms, state), swap/sync, and GPU time.
   Also capture draw count, triangle count, texture binds/frame,
   glTexParameteri calls/frame (instrument the driver counters
   locally).
2. **Test the named suspects, each with a bounded local experiment**
   (uncommitted prototype branches; measure on Pi; revert):
   a. **Mipmaps missing** (P22: zero mip usage anywhere): prototype
      glGenerateMipmap at upload + MIPMAP min-filter in the driver;
      measure. This is the bandwidth elephant — quantify it.
   b. **Per-draw state churn** (P22: wrap params re-sent every
      setMaterial; check filter params, program uniforms too):
      prototype dedup; measure CPU submission delta.
   c. **Per-pixel lighting cost**: lightweight vs per-pixel shader
      A/B (config already supports tiers) at same scene.
   d. **Overdraw/fill**: resolution-scaling probe (e.g. 400x300 vs
      800x600 GPU-time ratio ≈ fill-bound share); note depth-sort
      order of zone draws.
   e. **Swap/vsync stalls**: eglSwapInterval state; measure
      swap-blocked time separately from real GPU work.
   f. Anything the counters reveal (redundant uploads per frame,
      glGet* syncs, per-frame allocations) — follow the data.
3. **Deliverable**: `refactor/perf_truth_audit.md` (committed) — the
   budget table, per-suspect measured deltas, and a RANKED list of
   fundamental fixes with expected wins ("the biggest single lie
   named"), each sized as a follow-up unit. Honest nulls included.

## Rules

- Report-only for production code: every experiment is local and
  reverted; commit contains only the audit doc + this plan's Review
  (+ any counter instrumentation IF cleanly flag-gated and zero-cost
  when off — justify it).
- Pi protocol absolute; x86 llvmpipe numbers may supplement but Pi
  numbers decide.
- Goldens untouched; no preset changes.
- STOP: Pi unreachable (report; do NOT reboot), or an experiment
  requires driver changes too large to prototype locally (document
  the design instead).

## Verification

- Bar narrowed (audit): GLES2 tree builds clean with any committed
  instrumentation; ctest at current baseline; ARM build clean; visual
  gate PASS. Pi invocation count + pristine-exit confirmation
  reported.

## Review

Executed 2026-07-29 on `wt/slot1`, branch `task/P38-time-budget`, cut from
`fable` at `b74aca9`. Full deliverable, budget table, per-suspect deltas,
ranked fixes, and deviations are in `refactor/perf_truth_audit.md`
(committed alongside this Review) — not duplicated here.

### Standard bar (narrowed per plan: audit-scoped)

- `cmake --build build -j24`: clean.
- `ctest --test-dir build --output-on-failure -j4`: 1415/1416, exactly the
  documented host exception (`AudioManagerTest.PreloadCommonSounds`).
- `scripts/build-gles2-x86.sh`: clean (pre-existing warnings only).
- `scripts/ci/visual_gate.sh`: **PASS**, goldens untouched.
- `scripts/build-arm-noble.sh`: clean, rebuilt on the final (post-revert)
  tree — the ARM binary staged for every Pi measurement in this unit's
  final numbers matches the committed source exactly.
- `scripts/refactor/check_file_sizes.sh` (against this branch's actual cut
  point `b74aca9`, not the literal `fable` ref which advanced mid-session
  from a concurrent unit landing unrelated changes to `tests/test_wld.cpp`):
  no new FAILs — two pre-existing WARNs (`COpenGLES2Driver.cpp` 1975 lines,
  `gles2_zone_benchmark.cpp` 2816 lines, both already over the 1500-line
  WARN threshold before this unit and still WARN, not FAIL).

### Committed vs. reverted

Kept (counter instrumentation, flag-gated, zero-cost off — justified in
`gl_call_counters.h`'s header comment): `docker/irrlicht-drm/gl_call_counters.h`
(new), `GLCallCounters` wiring in `COpenGLES2Driver.cpp` (bind/texParam
counters only — mipmap and state-dedup prototype code fully reverted from
both `.cpp` and `.h`, confirmed via `git diff` showing zero change to
`COGLES2Texture.cpp`/`COpenGLES2Driver.h` vs the branch point), `--gl-counters`
CLI flag + `tex_binds`/`tex_param_calls` CSV columns in
`tools/hwcaps/gles2_zone_benchmark.cpp` / `zone_bench_metrics.{h,cpp}`,
the `docker/Dockerfile.arm-noble` / `scripts/build-gles2-x86.sh` copy-steps
for the new header, and the `CMakeLists.txt` include-dir addition. Also
committed: `refactor/perf_truth_audit.md` (deliverable),
`refactor/scratch/p38_pi_time_budget_audit.py` (Pi driver script, same
precedent as P11/P14's own committed scratch scripts),
`benchmarks/paths/befallen_fixedpose_300f.path` (fixed-pose replay
fixture), `benchmarks/pi_runs/p38_time_budget_audit/` (raw CSVs).

### Headline (full detail in perf_truth_audit.md)

Shipped-preset realistic load (befallen, e5, `--merge-zone-draws
--composite-scenegraph --eqe1-path`, production lighting, 800x600): total
avg 11.108ms / p95 25.836ms of the 33.3ms 30fps budget (66%/22% headroom),
GPU-sync is 54% of the frame, draws_avg 8.7 / submission avg 2.44ms — this
reproduces Will's quoted numbers almost exactly. Mipmap prototype (two
real bugs found and fixed together — see doc): honest **null**, ~0%
measured. Lightweight lighting: **−15% total / −22% GPU**, the largest
lever measured. State-dedup: small real win (−0.5% to −2% depending on
config), caught and fixed a stale-ARM-binary measurement bug mid-unit.
Resolution-scaling probe: **confounded** (lower res increased draws/tris,
not decreased them) — reported honestly, not forced into a fill-bound
number. Ranked fixes (fullest form in the doc): (1) land P14's
already-measured `occlusionMaxOccluderRegions` 256 — trivial, highest
confidence; (2) resolve the `enableMipmaps` dead-config-flag bugs; (3)
root-cause the resolution-dependent visibility anomaly; (4) land the
state-dedup fix as defense-in-depth; (5) diagnostic-only
`eglSwapInterval(0)` test of the suspected vsync/swap-time floor; (6) the
still-open, biggest-unknown residual GPU-sync cost after accounting for
lighting model.

### Pi discipline

15 timed data-producing invocations + 2 untimed debug-verification runs
(root-causing the stale-binary state-dedup bug) across 10 script launches,
4 SessionAbort recoveries (all handled per protocol: `sync && drop_caches`,
no reboot, governor restored and verified each time). Well under the
24-invocation (2-session × 12) ceiling. Pi left pristine: governor
`ondemand` on all 4 cores, `/tmp` clean, verified at final exit.

### Deviations

Two real methodology bugs were caught and corrected mid-unit rather than
silently absorbed into the headline numbers — both are documented in full
in `perf_truth_audit.md`'s own Deviations section: (1) the first 7 Pi
invocations used the wrong "orangepi preset" reference (a hardcoded C++
fallback, not the actual shipped `config/constrained_presets.json`),
corrected with 8 additional invocations under the true shipped-preset
flags; (2) the ARM binary used for the first two state-dedup measurements
predated that prototype's own code (a background-build-timing mistake),
caught via a direct on-Pi A/B, ARM rebuilt, both variants re-measured. The
resolution-scaling probe did not produce the planned clean fill-bound
percentage (a genuine confound, reported honestly rather than forced).
`test_wld.cpp` appearing in a `check_file_sizes.sh fable` diff is from an
unrelated concurrent unit that landed on `fable` mid-session, not from
this unit's own changes (verified via `git status`/branch-point diff).
