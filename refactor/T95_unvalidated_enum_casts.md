# T95 — Three unvalidated `int`→`enum` casts are undefined behaviour

**Slot: slot2. Headless. NO EQEMU — do not start the server or touch the lock.**

**Territory: `src/client/eq_action_handler.cpp` (and the enum definitions it
casts into), `tests/`, `refactor/`, `/mnt/projects/fable/review/T95/`.**

Read T93's Review — this is its deferred finding.

## The defect

`EqActionHandler::setMovementMode()`, `setPositionState()` and
`sendPetCommand()` cast a caller-supplied `int` straight into an enum with no
range check.

**An out-of-range value there is undefined behaviour, not merely a wrong
result.** That is a different severity class from the gem-slot and heading bugs
this arc has been fixing, which produced wrong-but-defined answers.

It is also unreachable by the existing tests: `RecordingActionHandler`
(`tests/action_test_fakes.h`) records the raw `int` and never performs the
cast, so no test in the suite can hit it. Same shape as the `setHeading` gap
T90 found, with worse consequences.

## Part 1 — Establish reachability before deciding severity

For each of the three:

- **Where does the `int` come from?** A slash command parsing user text, a
  hotkey binding, a script, a network value, or an internal caller that can
  only produce valid values?
- **Is an out-of-range value actually reachable** from any input path a user
  or script controls? A `/`-command taking a raw number is reachable; an
  internal call site passing a compile-time constant is not.
- What is each enum's valid range, and is it contiguous?

**Report reachability per call site.** If one of the three turns out to be
unreachable from any external input, say so — it still deserves the guard for
robustness, but the priority differs and the Review should not imply
equivalence.

## Part 2 — The fix

Validate before casting. **Reject and log; do not silently clamp.**

Clamping hides caller bugs — a `/pet 99` that silently becomes `/pet 3` is a
worse outcome than one that reports "99 is not a valid pet command", because
the user learns nothing and the bug survives. Follow the error-message
standard T77 established for `/equip`: say what was wrong in terms the caller
can act on.

Use whatever the codebase already does for enum bounds if such a helper
exists — search before adding one. If nothing exists and all three need the
same shape, one small shared helper is fine; do not build a validation
framework.

## Part 3 — Tests

Against the **real** `EqActionHandler`, using the T88/T90/T93 idiom
(`tests/test_eq_action_handler_transforms.cpp` is the closest existing
example).

- Valid values still work and reach the intended enum member.
- Out-of-range values are rejected without invoking the cast.
- Boundary values on both ends of each enum.
- **Prove every test can fail**: remove the guard temporarily, show red,
  restore, show green, and report that you did it.

## What you may not do

- **Do not clamp.** Reject and report.
- Do not change the enums themselves or their numbering — other code and
  possibly the wire format depend on those values.
- Do not extend this to every cast in the codebase. Three were identified by
  T93; if the sweep obviously implicates others in the same file, note them in
  the Review for a follow-up rather than expanding scope silently.
- Do not modify `RecordingActionHandler` to perform the cast. The fake is
  correct as a routing recorder; the gap is the missing real-implementation
  tests beside it, which is what you are adding.

## Verification

Full build and suite (expect 2080/2081 plus your new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens should not move; if they do, say so and explain why.

Findings to `/mnt/projects/fable/review/T95/` with a README.

## Review

(executor fills: reachability per call site and from which input paths; each
enum's valid range; the guard and its error messages; proof each test can
fail; any further unvalidated casts noticed but left for follow-up;
deviations)
