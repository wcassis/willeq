# T291 — visual_gate: measure process CPU time, not wall clock (Will's ruling 2026-08-26)

Slot: slot3. x86. NO eqemu. Class A. Territory: `tools/hwcaps/zone_bench_metrics.cpp`
(`cpuDrawMs`), the gate's baseline file (`p95CpuMsBaseline` — wherever it lives:
grep `scripts/ci/visual_gate*` and `benchmarks/`), tests. Do NOT touch the Pi
run (T289 slot1) or the loot window (T290 slot2).

## The ruling
Will: **"For the visual gate, switch to getrusage."** The re-bless of
`p95CpuMsBaseline` that this forces is authorised as part of it.

## Why
`cpuDrawMs` is a `steady_clock` wall-time span (T261 established this), so the
p95 ceiling is defeated by unrelated load — every trip in the record (T219,
T220, T239, T258, T274, T282, T286, T287) was a timing trip with **zero image
mismatches**, and T261's `RUN_SERIAL` only serialises within one ctest run, not
against a sibling worktree compiling. T274 and T286 both tripped it after that
fix.

## Scope
1. Replace the wall-clock span with process CPU time —
   `getrusage(RUSAGE_SELF)` (`ru_utime + ru_stime`) or
   `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`; say which and why. Keep the field
   name/units stable if anything parses them (grep the benchmark JSON readers).
2. Re-measure the baseline on a QUIET machine and re-bless `p95CpuMsBaseline`
   with the new metric. Record old vs new numbers side by side.
3. Prove it is now load-insensitive: run the gate three times while a heavy
   parallel build runs (`cmake --build -j24` on a scratch tree) — it must PASS
   all three. That is the acceptance.
4. Consider whether `RUN_SERIAL` can now be dropped (T261 measured it costs
   ~+26 s of suite wall time). If the metric is load-insensitive, dropping it
   buys that back — measure and say.

## Verification
Full ctest (`-j24` deliberately, to prove the point); visual gate under load;
Review with the before/after baseline table; evidence review/T291/. Commit:
`T291: the visual gate measures CPU time, not wall clock`.
