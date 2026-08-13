# T90 — Fix `setHeading()`'s double conversion

**Slot: slot1. Headless fix + test. Live verification leg at the end —
eqemu is free, lock normally.**

**Territory: `src/client/eq_action_handler.cpp`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T90/`.**

Read T84's Review, T85's Review, **T89's Review INCLUDING the CORRECTION
section at the end** (this is the unit that establishes the rest of the
heading stack is fine), then `docs/CLAUDE.md`'s Heading/Rotation Conventions.

## The defect, already fully characterized

`EqActionHandler::setHeading()` (`src/client/eq_action_handler.cpp:87-93`)
converts degrees → EQ 512-units, then passes the **converted** value to
`EverQuest::SetHeading()` (`src/client/eq.cpp:3133`), which stores it raw
into `m_heading` — a field every other site treats as **plain degrees**.
Double conversion. `/face` shares the same function.

Measured, repeatedly:
- `/turn 90` → `m_heading = 128` (90 × 512/360)
- `/turn 225` → `m_heading = 320` (225 × 512/360)
- T84's observer decoded the actor at 321.9° = `90 − 128 + 360`

## What is NOT wrong — do not touch any of it

T89 settled this and it is the reason this unit is narrow:

- **The outbound convention is CORRECT.** Verified against the official
  Titanium client's own empirically-derived mapping
  (`heading = 91.39° − travel_angle` server frame, R = 0.957, 83 samples);
  WillEQ's four cardinals predict to within an identical −1.39°, which is
  residual in the reference, not error in WillEQ.
- **`server_heading = 90 − m_heading`** in `eq_movement.cpp:1048` is right.
- **The inbound `m_heading = 90 − server_heading`** in `eq.cpp` is right, and
  is the hard-won, load-bearing offset. **Leave it alone.**
- T87's reported "~252°" and T89's own reported "+90°" are both **artifacts**
  and are superseded. Do not act on either.

**The entire fix is inside `setHeading()`.** If you find yourself editing
`eq.cpp`, `eq_movement.cpp`, or anything about the 90° terms, stop — you have
gone outside the defect.

## The fix

`SetHeading()` expects degrees. `setHeading()` should pass degrees. Remove the
premature 512-unit conversion; keep the normalization if it is still needed in
degrees. Confirm by reading `SetHeading()` what it actually stores, rather
than assuming.

Check for **other callers** of `SetHeading()` that may have been written to
compensate for the bug — if any pre-convert, they must be fixed together or
they will break. Search before editing.

## Tests — required, and they must be able to fail

- Unit test: `setHeading(90)` leaves `m_heading == 90`, not 128. Cover several
  angles including a wrap case.
- Test that `/face` produces a sane heading toward a known entity position.
- **Prove the tests can fail**: restore the double conversion temporarily,
  show red, restore the fix, show green. Report that you did this. T88's
  round-trip test would have been worthless without this step.
- Check whether any existing test asserted the buggy values and so passed
  because of the bug. If one did, that is a finding worth stating.

## Live verification leg

Take the lock normally, bring the server up, verify against the wire — not
against an ack. `outcome=applied` means a handler returned success, nothing
more.

- Issue `/turn` to a known angle; confirm the **transmitted** heading matches
  what the official convention predicts for that facing.
- Use T89's method: predicted wire heading = `90 − travel_angle(server frame)`
  for movement-derived facings, and for `/turn <deg>` the transmitted value
  should be `90 − deg` normalized, scaled by 2048/360.
- `/face` toward a known entity: confirm the transmitted heading points at it.

Session discipline: verify binary freshness against HEAD first (this has cost
four units); force daylight `/say #set time 11 0`; pad scripts far longer than
you think (a script that runs dry exits the client); `-d 4` for command
output; `--record-session`; restore DB state and confirm by `SELECT` **after
logout**; clean teardown and delete the lock.

## Verification

Full build and suite (expect 2070/2071 plus your new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — if goldens change, say so and explain why; a heading fix should not
move them, so a change is a signal.

## Review

(executor fills: the fix; other `SetHeading()` callers found and what was done
about them; whether any existing test encoded the bug; proof the new tests can
fail; the live wire verification with predicted vs transmitted values;
teardown; deviations)

## Board

`refactor/deferred.md` carries several now-superseded heading entries —
T87's "~252°" and T89's "+90°" are both artifacts, and the `setHeading`
entries predate T89's confirmation that nothing else in the stack is wrong.
**Correct them.** A wrong premise left on the board propagated twice in this
arc already.
