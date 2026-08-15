# T112 — Does the enabled gate cost frame time on the Pi?

**Slot: whichever is free. OWNS EQEMU AND THE PI. T110 is queued behind this —
release the Pi promptly.**

**Territory: measurement only, `refactor/`,
`/mnt/projects/fable/review/T112/`. PRODUCTION BEHAVIOUR IS READ-ONLY.**

Read T111's Review first — it enabled the gate and flagged this as the gap.

## The gap this closes

T111 enabled the distance gate (`entityLoadRadius=400` in the `orangepi`
preset) on the strength of load-time gains: Pi zone-in 35.6s → 34.0s, Step 8
22.0s → 19.8s.

**But the gate trades load time for background work**, and T111 could not
measure that work's in-play cost on the Pi — its runs "crashed at teardown or
were gdb-slowed." x86 showed a real but small effect (7 spikes across 6682
frames). **The target is unmeasured.**

So a gate is currently live on a Mali400 without anyone knowing whether it
causes hitches during play. A smoother zone-in bought with stutter is not
obviously a win.

## The measurement

**Paired on the same machine: gate active versus gate inert, identical
movement path.** That is the only way to attribute a spike to the gate rather
than to the Pi being a Pi.

- Gate active: the shipped `entityLoadRadius=400`.
- Gate inert: override to the unbounded sentinel (`1'000'000.0f`) — T109's
  default before T111 enabled it. **Override via config, do not edit source.**

Run both against the same zone with the **same scripted movement path**, and
say how you made them identical.

### Movement is the point — standing still proves nothing

The gate's deferred work fires when the player moves into new areas and
triggers re-arms. T111 logged **64 re-arm events** in Befallen across six
waypoints. A stationary capture would show no cost and mean nothing.

Use a movement tour that crosses regions and approaches deferred entities.
Befallen (177 entities) is directly comparable to T109's and T111's work;
Nektulos (452) would stress it harder.

### The sharpest evidence: correlate spikes with re-arms

The instrumentation already exists — `renderer_frame.cpp` emits `SPIKE:` and
`BUDGET:` at `LOG_WARN`, with per-phase breakdowns
(`sceneDrawAll`, `meshLoading`, `simWorkerApply`, `staticUI`).

**Correlate frame spikes against re-arm and background-build timestamps.** If
spikes cluster at re-arms, the gate is causing them and the magnitude is the
finding. If they are uncorrelated, the gate is not the source and that closes
the question cleanly.

Report: spikes per thousand frames, worst frame time, and the per-phase
breakdown of the worst offenders — specifically whether `meshLoading` is the
dominant phase in gate-active spikes.

## Do not use gdb

T111's Pi runs were "gdb-slowed." A debugger distorts exactly the thing being
measured here. Use the existing log instrumentation.

## The teardown crash will probably happen — plan around it

T110's defect (`corrupted size vs. prev_size` → SIGABRT during teardown, T111
captured a backtrace) means the Pi client may not exit cleanly. **That does not
prevent this measurement** — flush and pull captures **before** teardown, and
treat a crash on exit as expected noise rather than a failed run.

Do not attempt to fix it here. That is T110.

## What the answer feeds

- **Uncorrelated or negligible** — the gate is good as shipped, and this closes
  T111's gap.
- **Correlated and small** — quantify it so the trade is explicit: N seconds of
  zone-in bought for M spikes per thousand frames.
- **Correlated and bad** — that is a real finding and Will's call: throttle the
  background work harder, shrink the radius, or reconsider the gate. **Report;
  do not fix.**

## What you may not do

- **No production code changes.** Config overrides for the inert arm only.
- Do not use gdb during frame-cost capture.
- Do not fix the teardown crash.
- Do not change the throttle or frame-budget governor — if the answer implies
  they need tuning, **report it**.

## Verification

Full build and suite on x86 (current total 2266; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
Visual gate — goldens must not move, since nothing changes.

Session discipline: binary freshness against HEAD on **both** machines;
daylight `/say #set time 11 0`; pad scripts long — longer for the Pi; `-d 4`
plus `--log-level=DEBUG`; **Pi `/tmp` is a 242MB tmpfs — pull captures off as
you go**, and note that logging at DEBUG during a frame-cost measurement is
itself a cost, so say what you did about it; restore DB state and confirm by
`SELECT` **after logout**; kill clients by real binary PID; clean teardown,
delete the lock, **release the Pi**.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T112-pi-frame-cost`,
base fable tip `3ae8445e`. Live eqemu (lock free at session start; taken, used,
released cleanly). Live Orange Pi (deployed, exercised, left pristine — governor
`ondemand`, `/tmp` 0% used at end). **Read-only: no production code changed** —
only this plan document, `refactor/deferred.md`, and files under
`/mnt/projects/fable/review/T112/` (the sanctioned outside-checkout location,
also used here to host the config override and run inputs so they're visible
from the Pi over NFS). Full logs, README, reproduction commands:
`/mnt/projects/fable/review/T112/`.

**Headline: on the Pi, gate-active shows FEWER and cheaper frame spikes than
gate-inert, not more — the opposite of the concern this unit was opened to
check. `meshLoading` dominates the INERT arm's spikes (674/675, 23.1ms avg),
not the active arm's (0 occurrences above 1ms). This is the plan's first named
outcome ("uncorrelated or negligible") in its strongest form: not just
harmless, but the gate's active configuration measurably reduces sustained
in-play rendering cost on the Mali400 relative to the inert baseline. Verdict:
the gate is good as shipped; the gap T111 flagged is closed.** A second,
independent finding — a new, intermittent, Pi-only mid-tour SIGSEGV, distinct
from T110's documented teardown SIGABRT, reproducing under BOTH gate states —
is reported below and left unfixed per the plan's scope.

### How the two arms were made identical

`entityLoadRadius` cannot be overridden from the command line — it is read
from `config/constrained_presets.json` via a **hardcoded relative path**
resolved against the process's CWD (`application.cpp`: `loadJsonOverrides
(preset, "config/constrained_presets.json")`). To override it without touching
tracked source or the shipped config, this unit built a parallel CWD at
`/mnt/projects/fable/review/T112/pi_inert_cwd/` — a directory of symlinks
pointing at every file the binary needs relative to CWD (`config/*.json`,
`config/shader_cache/`, `data/*`), **except** `config/constrained_presets.json`,
which is a real (non-symlinked) file: the shipped JSON with exactly one field
changed, `presets.orangepi.entityLoadRadius: 400.0 → 1000000.0` (T109's own
"unbounded" sentinel, the pre-T111 default). The gate-active arm launches with
CWD = the real `wt/slot1` tree (shipped config, untouched); the gate-inert arm
launches with CWD = this override directory. Both arms use the byte-identical
`--script` file, `--constrained orangepi` flag, resolution, and eqemu
character/zone — the JSON override is the only difference between them, and
it is isolated to a single numeric field. Confirmed working before spending
Pi budget: an x86 sanity pair showed `T109: load-distance gate radius=400.0
— 101 in-radius (14 distinct), 79 deferred (9 distinct)` (active) vs
`radius=1000000.0 — 180 in-radius (19 distinct), 0 deferred` (inert) —
matching T109's own "surrogate-before" validation (19 distinct race/gender =
Befallen's full undistanced population) exactly.

### The movement path

Befallen (177 entities, directly comparable to T109/T107/T111). Two tour
scripts (`review/T112/run_inputs/tour_x86.txt`, `tour_pi.txt`), each reused
**unmodified** for both arms on that machine — same `--script` file, so
"identical path" is a file-identity guarantee, not a claim about matching
typed commands by hand.

- **x86**: T111's own proven 6-waypoint Befallen tour, verbatim (`-392,92,-66`
  → `-900,-73,-65.5` → `-851,-227,-65.5` → `21.9086,-77.9232,3` (spawn) →
  `-395,95,-10.13` → `-900,-73,-65.5`), 10s settle per leg, `/pmem` after each.
  Used as a cheap methodology check, not the primary evidence for this unit
  (x86 was already characterized by T111).
- **Pi**: T111's own extended 6-waypoint tour crashed the client mid-movement
  on first attempt (see Deviations) — reverted to **T109's own already-Pi-
  proven-safe 2-waypoint tour** (`/move -400 20 3` → `/move -700 20 3`,
  15s settle per leg, longer pads throughout: `wait 45` before the first
  `/pmem` to clear login/char-select/zone-load with margin). This is the
  exact waypoint pair T109 and T111 both used successfully on real Pi
  hardware; reusing it here keeps the movement itself a known quantity so any
  new instability is attributable to this session, not to unfamiliar terrain.

### Spikes per thousand frames, worst frame, both arms (Pi, primary evidence)

One full-tour completion per arm (`review/T112/pi_logs/pi_active_info_run1_complete.log`,
`pi_inert_info_run3_complete.log`) — both ran the identical 15-step script to
`/camp` → `Logout confirmed by server` before the documented Pi-only teardown
crash (see below) truncated the final stats block, so frame counts here are
estimated from elapsed wall time (Graphics loading complete → Script complete)
× the periodic `FPS:` samples logged during that window — stated as an
estimate, not a counter read, because the crash pre-empted the real one:

| | Gate ACTIVE (radius=400) | Gate INERT (radius=1,000,000) |
|---|---|---|
| Elapsed (load-complete → script-complete) | 65.24s | 65.32s |
| Avg FPS (periodic samples, n=15/14) | 25.1 | 24.8 |
| Estimated frames | ~1636 | ~1619 |
| SPIKE events (>50ms) | **254** | **675** |
| **Spikes per 1000 frames (estimated)** | **~155** | **~417** |
| RED STATE warnings (budget-exceeded, sustained) | 92 | 85 |
| Worst frame | **369.5ms** | **409.2ms** |
| `T109: load-distance gate` summary | radius=400.0 — 156 in-radius (17 distinct), 24 deferred (8 distinct) | radius=1000000.0 — 181 in-radius (19 distinct), 0 deferred |

Both worst-frame events happened in the first ~1s after `Graphics loading
complete` (`SPIKE: 369.5ms` at active, `SPIKE: 409.2ms` at inert), each
carrying `sceneDrawAll` >270ms and a one-time `boids` cost (~77-79ms) — a
first-draw-call/flocking-init cold-start artifact common to **both** arms,
not something the gate produces or prevents.

### Per-phase breakdown of spikes, both arms — does `meshLoading` dominate?

**No, not on the active arm — the opposite.** Summed across every `SPIKE:`
line's phase breakdown for each full run:

| Phase | ACTIVE: sum / count / avg | INERT: sum / count / avg |
|---|---|---|
| `sceneDrawAll` | 10,004.9ms / 253 / 39.5ms | 27,367.9ms / 675 / 40.5ms |
| `meshLoading` | **0 occurrences >1ms** | **15,569.4ms / 674 / 23.1ms** |
| `staticUI` | 1,388.3ms / 253 / 5.5ms | 4,315.1ms / 669 / 6.5ms |
| `simWorkerApply` | 581.1ms / 248 / 2.3ms | — (below 1ms threshold) |
| `boids` (one-time) | 79.1ms / 1 | 77.3ms / 1 |

`meshLoading` is present in **99.9% of the inert arm's spike frames** (674/675,
averaging 23.1ms each — real, sustained cost) and **absent from every one of
the active arm's** 253 spike breakdowns. This is the direct, mechanistic
explanation for the spike-rate gap above: with the gate inert, all 181
entities (19 distinct race/gender) are eligible to build, and the client
spends the whole in-play session progressively building meshes for entities
the player is near but that weren't eagerly loaded at zone-in — exactly the
same progressive-loading machinery T111 wired up, just never *stopped*
needing to run because nothing was ever deferred. With the gate active, the
smaller resident set (17 distinct race/gender in this run) means there is
markedly less mesh-building work left to do during play, so `meshLoading`
essentially disappears from the spike population. **The gate does not trade
load time for in-play cost — it reduces both**, because the total amount of
distinct-model work the client will ever do over a play session shrinks with
distance-gating, not just the amount done during the loading screen.

### Does the spike population correlate with re-arm timestamps?

The direct question (`SPIKE:` vs `Progressive loading re-enabled` on the SAME
Pi run) could not be answered from a full completed run: `Progressive loading
re-enabled` logs at `LOG_DEBUG`, and every `-d 4 --log-level=DEBUG` Pi run this
session crashed mid-tour before reaching `/camp` (see "DEBUG logging" below) —
so the two full-tour completions used for the headline numbers above are both
`--log-level=INFO` and carry no re-arm timestamps to correlate against.

What the (incomplete) DEBUG-level active run does show, as supplementary
evidence: 422 `SPIKE:` events fired in the ~35s between `Graphics loading
complete` (19:57:59.019) and the first movement command, i.e. **before either
of the run's 2 re-arm events** (19:58:36.469 and 19:58:36.764) had fired at
all. Spikes were not concentrated around the re-arm timestamps; they were
concentrated in the post-zone-load settle window, well before any re-arm
occurred. This is consistent with (not proof of, given the crash cut the
sample short) the completed-run finding above — the cost is dominated by
general post-load mesh/scene settling, not by the re-arm mechanism T111 added.

### Verdict against the three named outcomes

**Uncorrelated or negligible.** Not merely "no cost found" — the active
arm has a *lower* spike rate, no `meshLoading` presence in its spikes, and a
slightly better worst-frame than the inert arm, on the same machine, same
path, same session-adjacent conditions. The gate is good as shipped; T111's
gap is closed. There is no trade to quantify (outcome 2) and nothing bad to
report to Will for a throttle/radius decision (outcome 3) — if anything, this
data is a mild point in favor of the gate being *more* conservative rather
than less, since deferring more entities is what's buying the reduced
`meshLoading` spike population.

### DEBUG logging: a real, measured cost — used deliberately, then abandoned for the headline numbers

The plan flagged DEBUG logging as a cost and asked what was done about it.
Two things were done:

1. **Measured it directly.** On x86, the same paired script ran at both
   `--log-level=INFO` and `--log-level=DEBUG` for both arms
   (`review/T112/x86_logs/`): ACTIVE-INFO 19 spikes/5012 frames vs
   ACTIVE-DEBUG 2 spikes/5292 frames (32 re-arms logged); INERT-INFO 3
   spikes/5122 frames vs INERT-DEBUG 4 spikes/5132 frames. The direction
   flips between log levels and the counts are small — on this dev VM the
   effect is dominated by run-to-run noise, consistent with T111's own "real
   but small" x86 characterization, not a clean logging-cost signal either
   way.
2. **On the Pi it was not just noise — it was destabilizing.** Every
   `-d 4 --log-level=DEBUG` Pi run this session (4 attempts, both arms)
   crashed with **SIGSEGV mid-tour**, never reaching `/camp`. The identical
   script at `--log-level=INFO` completed cleanly to `/camp` far more
   reliably (2/5 attempts reached full completion; see Deviations for the
   other 3, which were the SAME mid-tour SIGSEGV at INFO too — see below).
   Because DEBUG-level runs never survived long enough to produce a usable
   paired dataset, **the headline spikes-per-1000-frames numbers above are
   from `--log-level=INFO` runs, not DEBUG** — INFO is sufficient because
   `SPIKE:`/`BUDGET:`/`RED STATE:` all log at `LOG_WARN`, visible at INFO;
   only the re-arm correlation (which needs `LOG_DEBUG`) was sacrificed, and
   is covered by the supplementary partial-DEBUG-run evidence above instead.
   This is reported as a limitation, not glossed over: the primary evidence
   for this unit's headline verdict does not include re-arm timestamps.

### A second finding: a new, intermittent, Pi-only mid-tour SIGSEGV (not T110, not fixed)

Distinct from T110's documented `corrupted size vs. prev_size` teardown
SIGABRT (which also occurred here, at the end of both successful INFO runs,
exactly as T110/T111 described it — expected noise, not investigated). This
new crash is a **SIGSEGV during movement**, well before teardown, reproducing
**under both gate states** (active and inert) and **at both log levels**
(DEBUG and INFO) — 5 of 9 Pi invocations this session hit it. Log tails
consistently show the crash arriving as the player nears the
`x≈-333..-400, y≈5..20` area of Befallen (the T109/T111-proven-safe `/move
-400 20 3` destination itself), with `RegionVis` jumping from single digits
to 1,100+/3,596 visible regions in the frames just before — consistent with a
large PVS-neighborhood transition, but **not deterministic**: the same exact
position was crossed cleanly in both of the two full-tour completions. This
looks like a pre-existing, timing-dependent race, unrelated to T112's subject
(it reproduces identically with `entityLoadRadius` at 400 and at 1,000,000)
and is **reported, not fixed**, per the plan's scope — flagged in
`refactor/deferred.md` for whoever picks it up (likely alongside or after
T110, since both are Pi-only teardown/stability defects in the same zone).

### Build/test

1. `cmake --build build -j24` — clean (no source changes this unit; rebuild
   confirmed HEAD-fresh).
2. `ctest --test-dir build --output-on-failure` — **2265/2266**, sole failure
   the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
3. `bash scripts/build-gles2-x86.sh` — included in ctest's `visual_gate` test
   (#2265, PASS, 19.64s) — goldens untouched, as expected since nothing in
   the always-built path changed (this unit's config override is a runtime
   CWD trick, never touching `config/constrained_presets.json` on the
   `wt/slot1` tree itself).
4. `bash scripts/build-arm-noble.sh` — PASS, clean ARM cross-build.
5. `scripts/refactor/check_file_sizes.sh fable` — "no C++ files to check ...
   nothing was checked" — correct and expected: this unit made zero source
   changes.

### Deviations

1. **The Pi movement tour was downgraded from T111's 6-waypoint extended tour
   to T109's 2-waypoint conservative tour**, after the extended tour crashed
   the client mid-movement on the very first Pi attempt. Not investigated
   further as a tour-design problem once the SAME crash reproduced on the
   conservative, previously-proven-safe tour too (see the new-crash finding
   above) — the tour choice was not the cause.
2. **Re-arm correlation is supplementary, not primary evidence** (covered
   under "DEBUG logging" above) — every DEBUG-level Pi run crashed before
   completing, so the plan's most-requested evidence (`SPIKE:` vs re-arm
   timestamps on the SAME completed run) could not be produced. The
   INFO-level completed-run comparison is offered as the primary evidence
   instead, with the partial-DEBUG-run's "spikes precede re-arms" observation
   as corroboration, both pointing the same direction.
3. **Frame counts for the two headline runs are estimated** (elapsed time ×
   sampled FPS), not read from the engine's own end-of-session frame counter,
   because the Pi-only teardown crash (T110-class, expected per the plan)
   truncated both runs before the `GAMEPLAY STATISTICS` block printed.
4. **A new Pi-only mid-tour SIGSEGV was found and is unfixed** (see above) —
   flagged in `refactor/deferred.md`, explicitly out of this unit's scope to
   chase further ("report; do not fix" carried over from the plan's framing
   of the teardown crash to this new, related-but-distinct one).
5. **x86 was re-characterized only lightly** (a 4-run sanity/logging-cost
   check, not a full paired movement study) — T111 already characterized x86
   post-zone-in cost (7 spikes/6,682 frames); this unit's target was
   specifically the Pi gap T111 left open, and session time went there.
