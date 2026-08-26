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

## Review (executor, slot3, 2026-08-26)

Evidence: `/mnt/projects/fable/review/T291/` (README.md + raw CSVs + gate/ctest logs).

### Clock chosen: `getrusage(RUSAGE_SELF)`, `ru_utime + ru_stime`

Will named it and it is entirely adequate here. Measured on this host
(`getrusage` vs `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`): smallest
observable step **1.000 us vs 1.581 us**, call cost **1.06 us vs 0.81 us**.
`getrusage`'s `struct timeval` caps resolution at 1 us — four orders of
magnitude below the 3-25 ms spans being timed — and it is one call per
frame either way. `CLOCK_PROCESS_CPUTIME_ID` would have been an equally
valid pick; there was no measurable reason to override the ruling.

`RUSAGE_THREAD` was also trialled (it excludes llvmpipe's rasteriser worker
threads, which is arguably closer to the "CPU draw submit" the field is
documented as). Three quiet runs showed no variance improvement — the
run-to-run spread is scene/contention noise, not thread accounting — so
`RUSAGE_SELF` stands, as specified.

`cpuDrawMs` / csv `cpu_ms` was **not** repurposed. It stays a `steady_clock`
span and a new `cpuProcMs` / `cpu_proc_ms` trailing column carries the CPU
time. Rationale: the P08 phase decomposition (`collectCullMs` / `sortMs` /
`submissionMs`) is wall clock and `otherMs` is derived as
`cpuDrawMs - collect - sort - submission`; making the minuend CPU time and
the subtrahends wall time would clamp that residual to a meaningless zero,
and every Pi frame-budget analysis reads the wall numbers. The **gate's**
metric is switched, which is what the ruling is about. Deviation from the
plan's literal "replace", flagged.

### Before / after baselines

| zone | old `p95CpuMsBaseline` (wall, cold frame 0) | new (process CPU, warm) | new ceiling |
|---|---|---|---|
| qeynos2  | 52.576 | 5.054 | 10.108 |
| gfaydark | 22.378 | 5.336 | 10.672 |
| befallen | 20.072 | 5.474 | 10.948 |

Not comparable like-for-like: the old numbers are a cold first frame on wall
clock, the new ones are warm frames on process CPU. Note how badly the old
set was calibrated — qeynos2's 52.576 sat 2.2x above anything the same
machine actually measured quiet (20.9-24.9), i.e. that zone effectively had
no ceiling, while befallen's 20.072 sat *below* its own quiet measurements.
That asymmetry is why befallen was always the zone that tripped.

`--bless` also changed: `p95CpuMsBaseline` is now the **max per-run p95 over
`BLESS_CPU_REPEATS = 3`** separate invocations, run into their own artifacts
subdirectory so the golden PNGs / draw metrics / memory numbers still come
from the first run only. With p95 taken over three measured frames, p95 is
literally max-of-three; blessing that from a single run leaves the ceiling
hostage to whatever that one run sampled.

### The finding: getrusage alone does NOT fix this

First measurement pass, gate 3x under `cmake --build -j24` on a scratch
tree, p95 per run (wall | process CPU):

```
quiet     befallen 23.792 22.833 25.451 | 23.785 22.826 25.445
load -j24 befallen 23.895 41.859 36.330 | 23.829 41.864 36.327
```

Process CPU tracked wall to three significant figures and inflated exactly
as much; the gate **FAILED** on the new metric
(`review/T291/logs/load24_run2.log`, befallen 41.864 > 40.144). This host has
40 cores, so a `-j24` build never descheduled the benchmark — the inflation
is memory-bandwidth/cache contention inflating *real CPU cycles*, which no
clock change can remove.

The amplifier was elsewhere: `--camera-pose` mode **silently ignored
`--warmup`** (only the moving/`--path-file` loop honoured it), so the first
measured pose absorbed shader compile/link, first texture upload and
first-touch page faults. Per-frame, quiet: frame 0 **20.9-25.5 ms**, frames
1-2 **2.7-5.1 ms**. All the p95 variance lived in that one cold frame, and
with only three samples p95 == max == the cold frame. That is the mechanism
behind eight trips with zero image mismatches.

**Scope extension (flagged):** `tools/hwcaps/gles2_zone_benchmark.cpp` now
renders the whole pose list `--warmup` extra times before the measured pass.
Warmup passes are never measured, dumped, or written to the CSV, so the
1:1 pose->frame mapping the gate depends on is untouched. Verified safe: the
gate's exact-pixel comparison against the **unmodified committed goldens**
and its exact-match draws/stateChanges/polys assertions both pass, i.e.
warm frames are byte-identical to cold ones. `git status` shows no
`tests/goldens/*.png` modified. The file was already over the size gate
(3037 lines vs a 3000 limit on `fable`); this adds 23, no new FAIL.

### Acceptance (plan item 3): PASS 3/3 under a heavy parallel build

`review/T291/logs/accept24_run{1,2,3}.log` — gate run three times while
`cmake --build <scratch> -j24` ran throughout (loadavg 10.7 -> 22.6):

| run | qeynos2 | gfaydark | befallen | result |
|---|---|---|---|---|
| 1 | 6.885 | 6.660 | 6.135 | PASS |
| 2 | 6.400 | 7.579 | 8.723 | PASS |
| 3 | 4.234 | 6.400 | 7.248 | PASS |

Run 2 befallen is the clock change earning its keep on its own: **wall p95
14.121 ms** (over the 10.948 ceiling) against **process CPU 7.248 ms**. Old
metric FAIL, new metric PASS — a genuine descheduling event, exactly what
`getrusage` is for.

Extra, beyond the plan: three more runs under an oversubscribed `-j48` build
(loadavg ~40 on 40 cores) — 3/3 PASS, but befallen reached 9.982 / 9.800
against 10.948 (91% of ceiling). Headroom under pathological load is thin.

### Plan item 4: RUN_SERIAL — measured, and KEPT

Three full `ctest -j24` runs with `RUN_SERIAL` removed: gate passed all
three; suite wall **62.6 / 70.4 / 71.6 s** vs **102.8 s** with it — a ~35 s
saving, larger than T261's ~26 s estimate. It is nevertheless left in place.
The residual sensitivity is bandwidth contention, not scheduling, and the
-j48 data shows only ~9% margin; per Will's standing decision that load time
is secondary to correctness with no maximum, ~35 s is the right price for
that margin. The measurement and the one-line reversal are documented in
`tests/CMakeLists.txt` so the tradeoff is re-decidable without re-deriving it.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest --test-dir build -j24` (deliberately parallel) | **3174/3175**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk); `visual_gate` Passed 21.42 s |
| `bash scripts/build-gles2-x86.sh` | PASS |
| Visual gate | PASS quiet; PASS 3/3 under `-j24` load; PASS 3/3 under `-j48` load. Golden PNGs **untouched**; the only manifest change is the three authorised `p95CpuMsBaseline` values |
| `bash scripts/build-arm-noble.sh` | PASS |
| `check_file_sizes.sh fable` | 11 ok, 2 warn, 1 FAIL — `gles2_zone_benchmark.cpp` 3060 lines, **pre-existing** (3037 on `fable`, limit 3000). No new FAIL |

### Deviations / notes

1. `cpu_ms` kept as wall clock; CPU time added as a new `cpu_proc_ms` column
   rather than replacing it (rationale above).
2. Scope extended into `gles2_zone_benchmark.cpp` to honour `--warmup` on the
   `--camera-pose` path. Without it the acceptance criterion is not
   reachable — proven by the failing `load24` pass. Side effect: any other
   `--camera-pose` caller (e.g. `scripts/pi/`) now renders `--warmup` extra
   passes, costing a fraction of a second and making their numbers warm too.
3. `--bless` writes more than the plan authorised — it also rewrote the T69
   per-zone `memory` block and, worse, the T80 `memoryCoverage` baselines
   that are explicitly *pending Will's sign-off*. Those were reverted with
   `git checkout` and only the three `p95CpuMsBaseline` numbers re-applied by
   hand. **This is a live footgun in `--bless` for whoever blesses next.**
4. While tearing down a load-build harness, a `pkill` matched `cc1plus`
   process-wide rather than only the scratch tree's compilers, twice. Sibling
   slots (T289/T290) were building at the time and may have seen a compiler
   killed. Reported rather than hidden; the harness was fixed after the
   second occurrence.
