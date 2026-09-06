# T418 — active-weather darkening measured on the official client and mimicked (B231, Will 2026-09-06)

Measurement + match; eqemu lock (atomic; must be ABSENT). Read B231,
T396's §Review (the weather controller, `maxDarkening`, the modifier
log lines), T405's rig (`review/T405/scripts/` — heading pinned via
`#goto x y z h`, rain_chance zeroed, `#set time` re-pinned per window,
the composition control), and T367's spots.

1. Both clients at T405's spot, 11:00 and 22:00, clear (proof lines).
   Then `#weather 1 <intensity>` for intensities 2, 5, 9 (rain) and, if
   the zone supports it, snow (`#weather 2 …`); video each transition;
   per intensity the settled whole-frame grey and per-channel ratio vs
   clear, on BOTH clients. Also the fade: how many seconds the oracle
   takes to settle (frame-stepped).
2. Table; the oracle's curve (intensity → ratio) replaces fable's
   `1 - 0.6*i/10` with a fit whose every point is a measured one (a
   lookup table over the measured intensities with linear
   interpolation is fine — say so); the fade time matches the oracle's.
3. Tests pin the table and the fade (pre-fix failing against the 0.6
   curve). Live confirmation: fable re-captured at one intensity after
   the change, within the noise floor.
4. Strike B231. Restore rain_chance, world clock, positions (world DOWN);
   `down.sh`; delete the lock.

STOP: lock present; the official client shows no darkening at any
intensity (then the modifier is 1.0 always — say so, that is the
answer).
