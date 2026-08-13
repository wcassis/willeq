# T93 — Where does a test fake stand in for the code that actually ships?

**Slot: whichever is free. Headless. NO EQEMU — do not start the server or
touch the lock.**

**Territory: `tests/`, `refactor/`, `/mnt/projects/fable/review/T93/`.
PRODUCTION CODE IS READ-ONLY — this unit adds tests, it does not change
behaviour.**

Read T90's Review (the "Existing test that encoded the bug" section) first.

## Why this unit exists

T90 fixed `EqActionHandler::setHeading()`, which double-converted degrees into
EQ units and corrupted `m_heading` for every `/turn` and `/face`. It survived
because **`tests/test_action_dispatcher.cpp` exercises heading through
`RecordingActionHandler`** (`tests/action_test_fakes.h`) — a fake that
implements `IActionHandler::setHeading()` as a bare recording call with no
conversion.

So the tests verified the dispatcher's routing, and the shipping implementation
had **zero coverage in either direction**. The seam under test was not the seam
that shipped.

## The risk criterion — use this, do not just count coverage

**A fake is dangerous exactly when the real implementation does work the fake
does not replicate**: conversion, validation, clamping, ordering, state
mutation, or error handling.

A fake standing in for a **pure forwarder** (`void moveToLocation(x,y,z) {
m_eq.Move(x,y,z); }`) is fine and needs no test. Do not manufacture tests for
those — that is noise, and it makes the suite slower without making it safer.

`setHeading()` was dangerous because it *transformed its argument*. That is the
signature to hunt.

## Part 1 — Inventory

Enumerate every test fake / stub / mock and the production type it substitutes
for. Known starting points, not an exhaustive list — find the rest:

- `tests/action_test_fakes.h` — `RecordingActionHandler` : `IActionHandler`
- `tests/fake_udp_transport.h` — the UDP transport seam
- inline fakes in `tests/test_command_processor.cpp`,
  `tests/test_game_state_sync_bridge.cpp`,
  `tests/test_zone_connection_flow.cpp`,
  `tests/test_zone_position_convention.cpp`

For each pairing produce: fake, interface, real implementation(s), and **which
real methods have any direct test coverage at all**.

## Part 2 — Classify, then rank

For every method on every faked interface, classify the **real**
implementation as:

- **FORWARDER** — passes arguments through unchanged. Low risk, no test needed.
- **TRANSFORMS** — converts units, changes frames, scales, normalizes,
  reorders. **This is where the bugs live.**
- **VALIDATES / CLAMPS** — rejects or bounds input.
- **MUTATES STATE** — writes members, publishes events, changes ordering.

Rank the TRANSFORMS group first, then VALIDATES, then MUTATES. **Report the
full classification even for what you do not test** — the inventory is a
deliverable in its own right, because it tells the next unit where to look.

`EqActionHandler` alone has ~30 methods and T90 covered two. Expect most to be
forwarders; the ones that are not are the point.

## Part 3 — Write the missing tests, by rank

Against the **real** implementation, constructed for real — the idiom exists
in `tests/test_setheading_conversion.cpp` (T90) and
`tests/test_position_frame_roundtrip.cpp` (T88), both of which stand up a
headless `EverQuest` plus the real handler.

- Start with TRANSFORMS. Work down until the budget is spent, and **say
  explicitly where you stopped and what is left** — a silent partial sweep
  reads as a complete one.
- **Every new test must be proven able to fail.** Break the behaviour
  temporarily, show red, restore, show green. Report that you did it. A test
  that cannot fail is worse than no test, because it manufactures confidence.
- If a test cannot reach the real implementation without new seams, prefer
  T88's default-off `Test_*ForSeam` idiom over restructuring production code —
  and say so in the Review.

## Part 4 — Report the pattern, do not fix it

If the sweep shows a systemic problem — an interface whose real implementation
is entirely untested, or a fake that has silently diverged from the real
behaviour it stands for — **report it**. Do not restructure the test
architecture inside this unit.

Note especially any fake whose behaviour **contradicts** the real one rather
than merely omitting work. That is worse than no fake: it encodes a false
belief that other tests then build on.

## What you may not do

- **No production code changes** except default-off test seams, if genuinely
  unavoidable, in the T88 idiom.
- Do not start the eqemu server.
- Do not write tests for pure forwarders to inflate the count.
- Do not delete or "simplify" existing fakes. They serve the dispatcher-level
  tests correctly; the gap is the missing real-implementation tests beside
  them.

## Verification

Full build and suite (expect 2078/2079 plus your new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens should not move; if they do, say so and explain why.

Inventory table and findings to `/mnt/projects/fable/review/T93/` with a
README.

## Review

(executor fills: the fake→interface→implementation inventory; the
per-method classification with the TRANSFORMS group called out; which tests
were added and which were deliberately skipped as forwarders; proof each new
test can fail; where you stopped and what remains; any fake that contradicts
rather than omits; deviations)

## Board

Record the inventory in `refactor/deferred.md` with the untested TRANSFORMS
methods listed explicitly, so the remainder is picked up rather than
rediscovered. This is the same family as the reachability pattern — code that
looks covered but is not.
