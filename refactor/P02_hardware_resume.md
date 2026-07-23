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

(executor fills in)
