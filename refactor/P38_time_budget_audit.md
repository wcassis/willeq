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

(executor fills in: budget table, per-suspect deltas, ranked fixes,
deviations)
