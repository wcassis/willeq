# T405 — the vision re-solve with weather AND heading pinned (B203, B252, B253)

Measurement + match; slot2; eqemu lock (atomic; must be ABSENT — if
present read it and STOP). Read B203/B252/B253/B211, T401's §Review §4
(the first proven-clear measurement: fable Ultravision ×1.16 / Infra
×1.17 vs oracle ×1.54 / ×1.34 in the same session; the oracle frames
between sessions differ by 10.3% RMSE because of HEADING), T361's rig
and method (`review/T361/`), T396's weather log lines, and
`src/client/graphics/vision_ambient.cpp` (T394 restored T361's values).

## Rig first (Leg A)

1. Pin heading on both clients: after `#goto`, set the facing explicitly
   (fable `/face`-style or the heading arg the driver supports; official
   client: T389's heading convention `world_angle = h*(360/512)+90`,
   `#goto` leaves h=0 — turn with a key held for a measured time, or
   `#goto x y z h` if eqemu accepts a heading — read `goto.cpp`). Prove:
   two independent sessions' unbuffed night frames on the SAME client
   agree within T386's 2.7% RMSE control. Until that holds, no ratio is
   comparable.
2. Pin weather: T396's per-zone-in line reads 1.000 and no modifier line
   appears in the window (both clients: `#weather 0 0` on the server
   side before the session).
3. Record the rig in `review/T405/scripts/` so the next unit inherits it.

## Leg B — measure and solve

4. Oracle: unbuffed / Ultravision / Infravision night frames, per channel
   + whole-frame grey (video-stepped). Fable: the same, on the tip's
   restored T361 constants. Table.
5. Solve the two scalar gains AND the per-channel tints by LIVE A/B: one
   parameter at a time, a probe build with the candidate value, re-capture,
   compare to the oracle frame (T361's power-law probe method, but with
   the measured session as the truth, not a forward model). Stop when
   both visions are within the noise floor on grey and on each channel,
   or report the nearest reach with the search log. Every constant in the
   commit carries the probe that produced it.
6. Tests pin the solved constants and the per-channel expectations from
   THIS session's oracle table (replacing T394's baseline test).

Strike B203/B252/B253 as the evidence supports; restore every server
value (world DOWN); `down.sh`; delete the lock.

STOP: lock present; the heading pin cannot be made to hold on the
official client (then the oracle table is per-session only — record and
STOP the solve).

---

## Review

Executed slot2, branch `task/T405-vision-resolve-pinned`, base **`e02b369e`**.
Full evidence, media and the rig: **`review/T405/`** (`README.md` is the
worked write-up; §3 is the search log).

### Verdict

| leg | ask | verdict |
|---|---|---|
| A | pin heading, prove two sessions inside 2.7% RMSE | **PASS** — oracle **0.389%**, fable **1.540%** |
| A | pin weather, per-zone-in line 1.000 and no modifier line | **PASS** — every session, zero modifier lines |
| A | record the rig for the next unit | **PASS** — `review/T405/scripts/` (10 files) |
| B | oracle table per channel + grey, both visions | **PASS** — and it REPRODUCES to <0.2% |
| B | solve the gains AND the tints by live A/B | **PASS** — 6 probe builds, all 6 channels ≤3.2% |
| B | tests pin the solved constants + this session's expectations | **PASS** — 8 cases, 5 red before / all green after |

Neither STOP condition fired: the lock was absent and taken cleanly, and the
heading pin holds on the official client.

### The three confounds, in the order they were found

1. **Weather** — and `#set weather 0 0` is **not sufficient**. eqemu's
   `Zone::ChangeWeather()` re-rolls on a 60 s timer from `zone.rain_chance1..4`
   (10/10/10/10 for qeynos2), and it dropped rain *inside this unit's first
   Ultravision window* (`weatherMod 1.000 -> 0.880`). The rig zeroes those
   columns for the session so `HasWeather()` disables the timer outright, and
   restores them. Every session after that carries zero
   `updateTimeOfDay: weatherMod … -> …` lines under a zone-in line reading
   1.000 — the T396 proof, per session.
2. **Heading** (B253) — `#goto x y z h`; eqemu's `goto.cpp` passes the optional
   4th argument to `MovePC`, the only placement that also aims. h=160 → world
   angle 202.5°. fable round-trips it (wire 160 → 337.5° → "EQ units" 480)
   self-consistently; the composition control is the proof, not the trace.
3. **The world clock** (new, → B281) — fable's unbuffed night base creeps
   upward with world time and the oracle's does not. One probe session drifted
   ×1.68 and was discarded. The rig now re-pins `#set time 22 0` before every
   window.

### The two facts that made the solve possible

- **The three channels are coupled.** Probe 1 raised the Ultravision G floor
  and the frame's G ratio *fell* (1.2769 → 1.2162). Probe 2 swept G alone at
  fixed R and B and the response is clean and monotonic (1.0895 / 1.2162 /
  1.4806 at G = 0.129 / 0.158 / 0.200) — so what actually happens is that
  raising R and B *lowers* the G ratio. A per-channel fit cannot converge;
  probes 3–6 are 3-D Newton steps with the measured cross-terms. This is why
  T361's "one free scalar over a measured chromaticity" was never going to work,
  and the header no longer claims it.
- **The old Infravision floor could not move two of its three channels.**
  (0.147, 0.081, 0.059) against a night base of (0.08, 0.08, 0.15): G cleared
  the base by 1% and **B sat below it**. Infravision's measured B ratio on those
  constants is x0.9967 — nothing. Every Infravision fit before T405 was solved
  from one usable channel. `EveryFloorChannelClearsTheNightBase` forbids it.

### Committed constants and residuals

    kUltravisionAmbientGain = 0.24607f   kUltravisionTint = {1.07877, 0.79866, 1.12256}
    kInfravisionAmbientGain = 0.16257f   kInfravisionTint = {1.15398, 0.70739, 1.13864}

| vision | R | G | B |
|---|---|---|---|
| Ultravision | +0.58% | +2.30% | +1.88% |
| Infravision | −0.35% | −3.18% | −2.56% |

From 75% / 88% of the oracle's grey (T401's clear-sky measurement of the T394
constants). **The greys are not within 2% of the oracle's and cannot be** — a
grey ratio is base-weighted by each client's own night base, and fable's is
red-heavy where the oracle's is green-heavy. Against the grey fable would report
at exact per-channel agreement the residuals are **+1.39% / −1.67%**, and a test
pins the arithmetic. The underlying base difference is boarded as **B280**.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 |
| `ctest -j6` | **4,148 / 4,152**; four reds, all from the shared pack (below) |
| `bash scripts/build-gles2-x86.sh` | exit 0 |
| visual gate standalone | **FAIL on `eqe1MappedBytes` only — no PIXEL golden moved**, goldens untouched |
| `bash scripts/build-arm-noble.sh` | exit 0, ELF 32-bit ARM |
| `check_file_sizes.sh fable` | 46 checked, 46 ok, 0 fail |
| tests before the fix | 5 of the 8 new cases **RED** against T394's constants |
| tests after the fix | 25 / 25 green |

`git status` shows exactly two modified files.

### Deviations, flagged not absorbed

1. **A server DATA write beyond the sanctioned character-position set.**
   `zone.rain_chance1..4` for qeynos2 was zeroed (10,10,10,10 → 0) and the zone
   processes restarted so `LoadZoneCFG` re-read the row. Without it the sky
   cannot be held clear for a five-minute window and the unit's premise fails.
   Snapshotted (`logs/zone_weather_before.tsv`), restored with the world down
   and verified by `SELECT`.
2. **The shared pack changed three times under this unit** (2,830,206 →
   19,994,910 → 5,270,190 → 5,532,254 bytes), which is all four ctest reds and
   also crossed the solve chain. Whether it invalidated the measurement was
   **checked, not assumed**: the unbuffed base is 0.004759 / 0.004779 / 0.004803
   across the three packs (0.9% between group means, no step at either boundary),
   and the committed result rests on a matched pair on one pack against an oracle
   that never reads it. Annotated onto **B255**.
3. **A probe-only edit to `vision_type.h`** set Infravision's lamp modifiers
   equal to Ultravision's so one session could sweep a single ambient channel
   with the lamp state fixed. Reverted; the file is byte-identical to the tip and
   `LampModifiersAreStillT360sLiterals` pins it.
4. **Probe 3's first session was discarded** (base drift ×1.68 → B281) and
   re-run as `p3b`. Both are in `review/T405/`.

### Board

**B203 STRUCK** (the live re-solve landed; the refuted extrapolation and its
superseded target table are gone), **B252 STRUCK**, **B253 STRUCK**.
**B255 annotated** with this unit's three-rewrite measurement. New:
**B280** (fable's night base is 5.6× darker and a different colour from the
oracle's) and **B281** (fable's night brightens with the world clock).
