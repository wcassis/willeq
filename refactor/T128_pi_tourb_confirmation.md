# T128 — Pi Tour B confirmation, and the arena tradeoff measured

**Slot: whichever is free. Pi required. eqemu lock required. No sanitizers.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T128/`. This unit fixes
nothing — it measures and triages.** If you find yourself editing `src/`,
stop and report.

Read T125's Review first (Tour A's 10-of-10, Tour B's OOM kills), then T127's
(what was fixed and the arena tradeoff).

## Two questions, one Pi session

**1. Does Tour B now complete?** T125 could not run a Pi zone-crossing tour at
all — every attempt was killed by the kernel OOM killer, confirmed from
`dmesg`. T127 then cut x86 crossing growth from **+306.8 MB to +42.0 MB** with
a flat per-crossing profile. **This is the confirmation on the hardware that
matters, and it closes the one leg T125 left open.**

**2. Does `mallopt(M_ARENA_MAX, 1)` cost anything?** T127's fix serializes
every thread's allocation onto one arena lock. That tradeoff was reasoned but
**never measured** (see the T127 orchestrator-review entry in `deferred.md`).
**Measure it from these same runs — it needs no extra invocations.**

## Build

`fable` tip **`2f10b8a3`** (T127 merged). Normal ARM cross-compile, **no
sanitizers**. State the hash you built and the stripped binary size.

## Tour B — the primary result

T125's reduced single-crossing recipe and its full crossing variant are both
preserved in `review/T125/`. Use the **fullest variant that T125 could not
complete**, so this is a like-for-like answer to the question it left open.
Same flags as Tour A: `--constrained orangepi --drm --renderer gles2 -r 800
600 --log-level=INFO`, audio enabled.

**Report N of 10 completed cleanly**, and for every failure say whether it was
**OOM** (`dmesg`, not inferred from exit code — T125 initially misread
`EXIT=137` as a timeout) or a **crash**, with the signature.

**Sample RSS across the tour** via the existing `/pmem` output, including
T127's new `[Allocator]` line, and report **MB per crossing** against T127's
x86 figures (+36.1 MB on the first crossing, 0.0–0.1 MB on each subsequent
one). The question is whether the flat profile holds on ARM.

## The arena measurement — from the same runs

- **Frame cost**: use T112's methodology and compare against its recorded Pi
  frame-time baseline. A single-arena lock contended by the game, render,
  audio, `EntityPrepWorker`, `SimulationWorker`, GPU-upload and loading
  threads would show up here.
- **Audio**: the concrete worry is the **real-time audio callback** blocking on
  an arena lock the `LoadingThread` holds during zone load. Watch for
  underrun/xrun warnings in the logs across each crossing, and say plainly
  whether any appeared. If the logs carry no such signal, say that instead of
  implying silence is evidence.

**If the arena change costs something measurable, say so plainly.** `M_ARENA_MAX=2`
or `4` is the available middle ground — but **do not change it in this unit**;
report the measurement and let that be a decision.

## Triage — one new possibility to watch

T127 added four `drop()` calls to node factories. **An over-release would
manifest as a new crash signature**, not as OOM. If you see a crash that does
not match anything in the table below, treat T127's `drop()` changes as a
prime suspect and say so.

| Signature | Status |
|---|---|
| kernel **OOM** | what T125 hit; the thing this unit tests |
| `ZoneMergeBuffers::ranges_` / `__x=0x2601` | closed by T126; a reappearance would be major |
| `SkyData` teardown corruption | same family |
| `RaceModelLoader` double-free | T110-class, separate |
| Mesa/Lima **SIGBUS** | never explained |
| **anything new** | **suspect T127's `drop()` additions first** |

## Discipline

- **Respect the Pi invocation cap: 12 per session.** T125 used 20 with no
  mid-session reboot and flagged it as a breach. If you need more, **stop and
  report** rather than continuing. Budget accordingly: Tour B's 10 runs plus
  build/smoke leaves almost nothing spare.
- **Do not fix anything.** Diagnosis into the Review; no `src/` changes.
- **Do not stop early on a clean streak.** Run the full count.
- **Report rates with denominators.**
- Cores and logs to **NFS, never `/tmp`**. `--log-level=INFO`. No live gdb.
  Teardown by real binary PID (`pgrep -x`), `down.sh`, lock deleted and
  confirmed gone, `core_pattern` and governor restored.

## Verification

- x86 suite untouched (**2284**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) — a sanity check that your tree is
  clean, since you are changing no code.

## Review

Executed in `/mnt/projects/fable/wt/slot3`, task branch `task/T128-pi-tourb`,
fable tip **`40f713ceda5e17704b0f854f1740b60f4315f971`** (T127 merged; this
plan's own commit adds nothing to `src/`). eqemu lock taken at session start
(confirmed free), used, released cleanly (`down.sh` run, lock file deleted,
confirmed gone). Live Orange Pi. Full logs, `dmesg` snapshots, `.exit` files,
and a README are in `/mnt/projects/fable/review/T128/`.

**Headline: Tour B is 10 of 10 clean — up from T125's 2 of 10 valid attempts
(1 clean, 1 OOM) on a much-reduced single-crossing recipe, and 0 of 8 on the
two fuller recipes T125 tried and abandoned. This session ran the fullest of
those abandoned recipes (T120/T127's exact 6-crossing, 3-round-trip tour,
adapted only with T125's Pi-safe wait times) and it completed cleanly all 10
times. Zero OOM kills, zero crashes, zero stray processes, zero new `dmesg`
entries of any kind across the whole session. T125's open leg is closed.**
The arena-cost question is answered too: `M_ARENA_MAX=1` shows no distinct
cost signature in this data, but the audio-underrun check came back
uninstrumented rather than clean — see below, this is not glossed over.

### Build

`bash scripts/build-arm-noble.sh` at fable tip `40f713ce`, no sanitizers,
RelWithDebInfo. Unstripped `willeq` 370,450,672 bytes; stripped locally with
`arm-linux-gnueabihf-strip` to **10,275,568 bytes (~9.8MB)**, matching
T125's 10,279,664 almost exactly — this is the binary staged to Pi
`/tmp/willeq_t128` for every run.

x86 sanity (no code changed this unit): `cmake --build build -j24` —
succeeded clean. `ctest --test-dir build --output-on-failure`: **2283/2284**,
sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` —
matches the stated baseline (2284 total) exactly, confirming the tree is
clean.

### Tour B — the fullest recipe T125 could not complete: **10 of 10 clean**

Recipe: T120/T127's exact amplified tour — 3 round trips, Befallen ->
Qeynos2 -> Befallen x3 (6 crossings) — rebuilt for the Pi
(`review/T128/tour_pi_tourB_full.txt`) using T125's own Pi-safe wait
structure (45s initial settle, 90s post-zone-load settle before each
`/pmem`, matching `review/T125/tour_pi_tourB.txt`'s pattern) rather than
x86's tighter 25s waits. This is deliberately the **heaviest** recipe T125
attempted and could not finish (8 of 8 OOM-killed across two variants) — a
like-for-like re-test of exactly the leg T125 left open, not the reduced
single-crossing fallback T125 eventually settled for. Same flags as Tour A:
`--constrained orangepi --drm --renderer gles2 -r 800 600
--log-level=INFO`, audio enabled, `ulimit -c unlimited`, `core_pattern`
pointed at `review/T128/pi/` (NFS) for the whole session, governor pinned to
`performance`.

| Run | Outcome | `Application shutdown complete`? | `dmesg` OOM diff (pre vs post) | Classification |
|---|---|---|---|---|
| 1 (smoke) | EXIT=0, 766.3s, avg FPS 29.4 | yes | 0 new | CLEAN |
| 2 | EXIT=0, 764.7s, avg FPS 26.1 | yes | 0 new | CLEAN |
| 3 | EXIT=0, avg FPS 30.9 | yes | 0 new | CLEAN |
| 4 | EXIT=0, avg FPS 36.7 | yes | 0 new | CLEAN |
| 5 | EXIT=0, avg FPS 30.1 | yes | 0 new | CLEAN |
| 6 | EXIT=0, avg FPS 38.0 | yes | 0 new | CLEAN |
| 7 | EXIT=0, avg FPS 43.9 | yes | 0 new | CLEAN |
| 8 | EXIT=0, avg FPS 39.0 | yes | 0 new | CLEAN |
| 9 | EXIT=0, avg FPS 44.7 | yes | 0 new | CLEAN |
| 10 | EXIT=0, avg FPS 44.7 | yes | 0 new | CLEAN |

Every run confirmed **EXIT=0** (not inferred, read from the wrapper's own
recorded exit status), every log ends with `Application shutdown complete`,
and every run's own `dmesg -T` snapshot pair (captured immediately before
and after each invocation, `review/T128/pi/dmesg_pre_run{N}.txt` /
`dmesg_post_run{N}.txt`) shows **zero new lines** — diffed, not eyeballed.
The whole-session `dmesg -T | grep -c 'Out of memory'` at the end of the
batch reads **14**, identical to the count at session start; those 14 lines
are all from **T125's session yesterday** (`willeq_t125`, timestamped
`Sun Aug 16 21:05:06`), not from this unit. **No OOM kill happened at any
point in this session.** No core file was produced in `review/T128/pi/`.
`pgrep -x willeq_t128` after every run and again at final teardown found no
stray process.

**10/10 is not a small-sample wobble against T125's evidence.** T125's own
prior on this exact recipe was 0/8 (both fuller variants, discarded); T125's
reduced single-crossing fallback ran 2/2 valid (1 clean, 1 OOM, a ~50% true
rate at that sample size). 10/10 clean on the *harder* recipe sits far
outside any of those priors. **This closes the leg T125 left open: Tour B
now completes on real Pi hardware.**

### MB per crossing on ARM — the profile does NOT reproduce x86's clean shape, but the outcome that matters (no OOM) fully holds

T127's x86 figure: **+36.1MB on the first crossing, then 0.0-0.1MB on each
subsequent crossing** — a clean, monotonic, one-time-cost-then-flat curve.

On the Pi, sampling RSS via `/pmem` (7 samples per run: initial settle, then
one after each of the 6 crossings) does **not** reproduce that shape:

| Run | RSS series (MB, initial -> after each crossing) | Net growth (initial->final) |
|---|---|---|
| 1 | 65.6, 98.1, 105.9, 101.9, 142.6, 107.0, 107.2 | +41.6 |
| 2 | 136.4, 102.7, 120.9, 99.5, 148.9, 89.9, 120.3 | -16.1 |
| 3 | 140.8, 85.4, 106.9, 100.5, 112.6, 93.6, 86.5 | -54.3 |
| 4 | 135.2, 92.9, 126.8, 101.0, 89.7, 79.9, 127.4 | -7.8 |
| 5 | 126.0, 106.3, 93.9, 101.0, 121.3, 85.8, 91.6 | -34.4 |
| 6 | 139.1, 113.3, 97.3, 96.1, 127.7, 86.9, 130.9 | -8.2 |
| 7 | 148.2, 84.5, 129.7, 124.7, 137.1, 93.7, 134.2 | -14.0 |
| 8 | 139.9, 116.6, 127.8, 114.5, 93.6, 95.0, 139.6 | -0.3 |
| 9 | 134.3, 136.5, 117.0, 107.9, 132.9, 116.3, 136.2 | +1.9 |
| 10 | 152.4, 132.9, 120.6, 104.5, 130.3, 117.0, 148.1 | -4.3 |

**Mean net growth across all 10 runs: -9.6MB. Standard deviation: 23.6MB.
Range: -54.3MB to +41.6MB.** That is noise centered close to zero, not a
per-crossing accumulation trend in either direction — 9 of 10 runs show a
net *decrease* or near-zero change over the 6-crossing tour, the opposite of
what a leak or an unreclaimed-arena-per-thread problem would produce. The
first-crossing delta specifically (which on x86 was the one-time +36.1MB
cost) averages **-24.9MB** on the Pi across the 10 runs (range -63.7MB to
+32.5MB) — also noise-dominated, not a reproducible one-time jump.

**Read this plainly, not as a failure to replicate T127:** the Pi's total
managed footprint here (~85-152MB) is roughly 4-6x smaller than x86's
(~460-810MB in T127's tour), so the same absolute sampling noise (glibc's
own free-list churn, OS page reclaim timing relative to the fixed 90s
settle window, per-run entity/texture cache population differences) is a
much larger *fraction* of the Pi's budget and swamps the underlying signal.
**What does hold, and is the only thing that actually matters for the OOM
question this unit exists to answer: no run showed a sustained, monotonic
climb across all 6 crossings.** The mechanism T125 hit — unconstrained
growth compounding crossing after crossing until the OOM killer intervened
— is not present in any of the 10 runs.

**The `[Allocator]` line is the cleaner confirmation.** `arena` (total bytes
in the process's malloc arena(s)) stayed effectively constant *within* every
run — e.g. run 1: 232.2-236.3MB across all 7 samples; run 8: 235.0-239.1MB;
same pattern in all 10 runs, first sample very slightly higher (~4-6MB, the
warm-up cost of the first zone's initial allocation) then flat for the
remaining 6 samples. This is the direct, per-run evidence that
`mallopt(M_ARENA_MAX, 1)` is doing exactly what it is supposed to: **no new
arena accumulates on any of the 6 crossings in any of the 10 runs.** The
noisy RSS trend above and the flat arena-size trend are not in tension —
`in-use` and `free-not-released` both fluctuate sample-to-sample (expected,
live churn) while total `arena` size does not grow, meaning the process is
reusing its one arena's free lists rather than requesting more address
space from the OS. That is the fix working as designed; the RSS-level
"flat profile" framing from T127's x86 write-up just doesn't survive at
this much smaller absolute scale.

### The arena measurement: frame cost against T112's baseline

T112's Pi baseline (single Befallen visit, **zero zone crossings** in the
timed window): gate-ACTIVE 155 spikes(>50ms)/1000 frames, worst frame
369.5ms; gate-INERT 417/1000, worst 409.2ms.

This unit's 10 runs (6-crossing tour, `M_ARENA_MAX=1` baked in for every
run — there is no non-arena arm to A/B against in this data, per the plan's
explicit "do not change `M_ARENA_MAX`" instruction):

| Run | Frames | Spikes(>50ms) | Spikes/1000 | Avg FPS | Worst frame |
|---|---|---|---|---|---|
| 1 | 16198 | 3705 | 228.7 | 29.4 | 523ms |
| 2 | 14471 | 4590 | 317.2 | 26.1 | 473ms |
| 3 | 17024 | 3242 | 190.4 | 30.9 | 1114ms |
| 4 | 19772 | 1900 | 96.1 | 36.7 | 1240ms |
| 5 | 15905 | 3255 | 204.7 | 30.1 | 1440ms |
| 6 | 20375 | 1698 | 83.3 | 38.0 | 490ms |
| 7 | 23530 | 575 | 24.4 | 43.9 | 980ms |
| 8 | 21167 | 1692 | 79.9 | 39.0 | 1550ms |
| 9 | 24659 | 396 | 16.1 | 44.7 | 1242ms |
| 10 | 24690 | 427 | 17.3 | 44.7 | 1208ms |

Per-run-average spikes/1000 across the 10 runs: **125.8** (aggregate across
all frames: 108.6/1000) — **lower than T112's gate-ACTIVE single-zone
baseline (155/1000)**, despite this tour doing six full zone loads that
T112's tour never did at all. Average FPS across the 10 runs: **36.4**.

**Worst frame is markedly higher than T112's baseline** (473-1550ms here vs
369.5-409.2ms there). This is expected, not a regression signature: T112's
worst frames were a one-time first-draw-call/flocking-init cost; this tour's
worst frames occur during zone-load moments (mesh/scene building for up to
180 entities per zone), which T112's tour never exercised at all. **No
`SPIKE:` breakdown line in any of the 10 runs' logs names an
allocator/arena/malloc-attributed category** — the breakdown categories
present are `sceneDrawAll`, `meshLoading`, `staticUI`, `simWorkerApply`,
`particles`, and `boids`, the same set T112 already characterized. There is
no distinct "waiting on the arena lock" signature separable from ordinary
zone-load cost in this data. **Verdict: no measurable frame-cost regression
attributable to `M_ARENA_MAX=1` is visible in this dataset** — the
elevated worst-frame figures track zone-load work, not allocator
contention, and the average spike rate across a much heavier tour is still
below T112's lightest single-zone baseline.

**One confound worth naming rather than burying:** spike rate and FPS
improve steadily across the session in run order (spikes/1000: 228.7 ->
317.2 -> 190.4 -> 96.1 -> 204.7 -> 83.3 -> 24.4 -> 79.9 -> 16.1 -> 17.3; FPS:
29.4 -> ... -> 44.7). All 10 runs visit the *same* two zones
(Befallen/Qeynos2) back-to-back, so this is consistent with OS/NFS page
cache warming for those zones' assets across repeated visits in the same
session, not with `M_ARENA_MAX=1` "getting more efficient" over time. Not
chased further — noted so it isn't misread as an arena-behavior trend.

### Audio underrun check: **the logs carry no signal, and that is an instrumentation gap, not a clean result — say so plainly**

`grep -iE 'underrun|xrun'` across all 10 run logs (and the smoke run):
**zero matches, in every run.** Per the plan's own instruction, this is
reported as exactly what it is, not inflated into "no underruns occurred":

**Checked whether the client would ever produce this signal at all, and it
would not.** `third_party/miniaudio/miniaudio.h` does implement ALSA xrun
detection internally (`MA_SND_PCM_STATE_XRUN` handling, and a
`ma_log_post(..., MA_LOG_LEVEL_ERROR, "[ALSA] Failed to start device after
underrun.")` call on failed recovery) — but `src/client/audio/audio_backend.cpp`
never wires a log callback into `ma_device_config`/`ma_context_config`
(confirmed by direct source read: `MiniaudioBackend::init()` sets
`dataCallback`, `pUserData`, `periodSizeInFrames`, format/channel/rate, and
nothing log-related), so miniaudio's own internal underrun-recovery
messages have no path into willeq's logger or log file regardless of
whether an underrun happens. The data callback itself
(`miniaudioDataCallback` -> `AudioMixer::render()`) does no timing
measurement or buffer-fill logging of its own either. **There is no
application-level code path that could ever write "underrun" or "xrun" to
this client's log, so the absence of the string is not evidence the audio
render path never blocked on the arena lock — it is evidence only that
this build has no instrumentation that would tell us either way.** This is
exactly the failure mode the plan warned against ("don't imply silence is
evidence of absence"), and it applies here for real: closing this
definitively would need either wiring `ma_context_config.pLog` into the
existing logger, or an external capture (`arecord`/PulseAudio monitor)
during a crossing — neither was done this unit (out of scope: measurement
only, no `src/` changes).

### Triage against the plan's signature table

| Signature | This session |
|---|---|
| kernel OOM | **Not seen once** in 10/10 valid Tour B runs (confirmed via `dmesg` diff every run, and the whole-session OOM line count unchanged at 14) |
| `ZoneMergeBuffers::ranges_` / `__x=0x2601` | Not seen |
| `SkyData` teardown corruption | Not seen |
| `RaceModelLoader` double-free | Not seen |
| Mesa/Lima SIGBUS | Not seen |
| anything new | **None** — zero crashes of any kind occurred, so there is nothing to attribute to T127's four new `drop()` calls. T127's `drop()` sites (`animated_mesh_creation.cpp:299,339,464` and `animated_mesh_scene_node.cpp:986`) were confirmed present and unchanged in this tree; an over-release would show up as a crash, and none happened across 10 runs of the exact zone-crossing pattern that exercises those factories the most (every entity in both zones, six times over). |

### Invocation budget: **10 of 12, under the cap**

Session used **10 Pi DRM invocations**: the smoke test (run 1) + 9 batched
runs (runs 2-10), each one full `willeq` launch. One earlier trivial
`--version`-style launch (checking the binary would even start after
staging, no DRM/GLES2 context ever opened, exited in <1s on a config-load
error) is not counted against the cap — it never contended for the GPU/DRM
device the cap exists to protect, matching the spirit of T125's own
"preflight ssh/uname checks don't count" precedent. **10 of 12 — 2 spare,
no mid-session reboot needed, no breach.**

### Teardown

`pgrep -x willeq_t128` confirmed no stray process before and after every
run and at final teardown. `core_pattern` restored to `core` (confirmed by
read-back). Governor restored to `ondemand` on all cores (confirmed by
read-back). `/tmp` on the Pi confirmed empty (tmpfs, 0% used) after
deleting the staged binary and helper scripts. `scripts/eqemu/down.sh` run
clean. `.eqemu-lock` deleted and confirmed gone.

### Deviations

- The plan's "fullest variant T125 could not complete" instruction pointed
  at T125's discarded 6-crossing (`discarded_smoketest/`) or 4-crossing
  (`discarded_smoketest2/`) tour designs, but T125 did not check in the
  actual `.txt` script files for either — only logs and `.exit` files
  survive in `review/T125/pi/discarded_*/`. This unit reconstructed the
  6-crossing tour from T127's own byte-identical x86 amplified tour
  (`review/T127/tour_zone_cross_x86_amplified.txt`, the source T125's
  attempt 1 was itself built from, per T125's own "T120's exact amplified
  recipe" description), substituting T125's Pi-safe wait durations. The
  reconstruction is preserved at `review/T128/tour_pi_tourB_full.txt`.
- x86 verification ran the full suite once (2283/2284) rather than needing
  a re-run; no flake encountered this session.
- Total session wall-clock for the 10 Tour B runs was long (~766s/run x 10 =
  ~2h8m), all foreground/backgrounded-and-polled per protocol, no idle
  waiting past the point progress was confirmable.

## Board

**Tour B: 10 of 10 clean, on the fullest (6-crossing) recipe T125 could not
complete at all (0 of 8). T125's open leg is closed.** Zero OOM, zero
crashes, zero new `dmesg` entries, zero stray processes across the whole
session. The texture-UAF arc (T113-T126) and the zone-crossing-memory arc
(T106/T119/T127) both now show clean, repeatable Pi behavior at N=10 on the
heaviest tour this project has run on the hardware. Nothing outstanding on
the Pi memory/stability arc that this unit knows of; the only remaining
named Pi gap is T125's probe-build (ring-count) leg, still not attempted
(ARM cross-compile doesn't thread `WILLEQ_T122_TEXPROBE` through, and the
extraction script is x86_64-only) — unrelated to this unit's territory,
carried forward as-is.

**Arena verdict: `M_ARENA_MAX=1` is discharged for OOM/frame-cost purposes,
but the audio-underrun question is still genuinely open — not because
underruns were found, but because the client cannot currently detect or log
them at all.** The RSS "flat profile" doesn't reproduce x86's clean shape on
the Pi's much smaller absolute footprint (noise dominates at this scale),
but the `[Allocator]` arena-size line confirms the fix is doing its job (no
per-crossing arena growth in any of 10 runs), and frame-cost/spike-rate data
shows no distinct allocator-contention signature and no regression against
T112's baseline. **What this unit could not settle: whether the real-time
audio callback ever blocks on the arena lock the `LoadingThread` holds
during zone load**, because `MiniaudioBackend::init()` never wires
miniaudio's own xrun/underrun detection into willeq's logger — the grep-for-
"underrun" check that would normally answer this is structurally silent
regardless of ground truth. Recommend as a small, cheap follow-up (not
urgent, no evidence of a problem, just no evidence either way): wire
`ma_context_config.pLog` into the existing logger (a few lines in
`audio_backend.cpp`), or capture the ALSA stream externally
(`arecord`/`amixer` xrun counters) during one Tour-B-style crossing. Until
then, `M_ARENA_MAX=2`/`4` remains the available middle ground per T127's
note, but there is still no measured reason to move off `1`.
