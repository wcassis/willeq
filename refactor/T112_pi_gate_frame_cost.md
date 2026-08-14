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

(executor fills: how the two arms were made identical; the movement path; spikes
per thousand frames and worst frame time, both arms; the per-phase breakdown of
gate-active spikes; **whether spikes correlate with re-arm timestamps**; the
verdict against the three outcomes above; whether DEBUG logging perturbed the
measurement; deviations)
