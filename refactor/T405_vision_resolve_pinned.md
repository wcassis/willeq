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
