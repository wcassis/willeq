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

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T93-fake-sweep`, base `fable` tip `1bac2d5c`. Headless throughout, no
eqemu. Full inventory table, per-method classification, and detail are in
`/mnt/projects/fable/review/T93/README.md` — this section summarizes.

### Inventory (fake -> interface -> real implementation)

- `RecordingActionHandler` (`tests/action_test_fakes.h`) : `IActionHandler`
  -> `EqActionHandler`. **The high-risk one** — records every call verbatim
  with zero conversion, same shape as the fake T90 found responsible for
  `setHeading()` shipping untested.
- `FakeUdpTransport` (`tests/fake_udp_transport.h`) : `EQ::Net::UdpTransport`
  -> the real socket transport. LOW RISK: only wire I/O is faked;
  `DaybreakConnection`'s real protocol logic (compression/CRC/fragmentation/
  sequencing) runs unmodified over it in `test_daybreak_state_machine.cpp`.
- `FakeInputHandler` (`tests/action_test_fakes.h`) : `IInputHandler` ->
  `GraphicsInputHandler`/`ConsoleInputHandler`/`NullInputHandler`. LOW RISK:
  read all three real implementations end to end — pure event/state
  forwarding, no unit conversion to miss.
- `RecordingBridge` (3 separate inline definitions across
  `test_game_state_sync_bridge.cpp`/`test_zone_connection_flow.cpp`/
  `test_zone_position_convention.cpp`) : `GameStateBridge` (only the
  pure-virtual `applyEvent()` hook is overridden; `pushEvent`/`pushIntent`/
  `drainIntents`' real queue/coalescing logic is inherited unmodified) ->
  `IrrlichtBridge`/`ConsoleBridge`. LOW RISK for this sweep's purposes: the
  real `applyEvent()` 99-case switch has its own separate, already-claimed
  100% coverage bar (CLAUDE.md) — out of scope here, not re-verified.
- `RecordingOutputRenderer` (`test_command_processor.cpp`) :
  `IOutputRenderer` -> `ConsoleRenderer`/`GraphicalRenderer`/`NullRenderer`.
  LOW RISK: pure display-sink forwarder interface, no transform reachable
  from `CommandProcessor`.

### Classification (EqActionHandler, ~30 methods)

TRANSFORMS (ranked first, per the plan): `setHeading` (T90, already fixed
and tested), `castSpell`/`castSpellOnTarget`/`memorizeSpell` (gemSlot-1
conversion — **found this unit, now tested**), `setMovementMode`/
`setPositionState`/`sendPetCommand` (unvalidated int-to-enum casts — found,
deferred). VALIDATES: `targetGroupMember` (deferred). MUTATES:
`targetSelf` (self/pet toggle — investigated, deferred),
`hailTarget` (fallback branch — deferred). Everything else (~20 methods)
is a FORWARDER and was deliberately left untested, per the plan's explicit
prohibition on inflating the suite. Full per-method table:
`/mnt/projects/fable/review/T93/README.md`.

### Tests added

`tests/test_eq_action_handler_transforms.cpp` (registered in
`tests/CMakeLists.txt`), 2 tests, against the real `EverQuest` +
`EqActionHandler` + `CombatManager` (T88/T90 headless-construction idiom):

- `CastSpell_GemSlotIsOneIndexedToZeroIndexedSpellSlot` — memorizes a spell
  into gem 2 only; `castSpell(1)` must NOT start casting, `castSpell(2)`
  must, observed via `CombatManager::IsCasting()`.
- `CastSpellOnTarget_SharesSameGemSlotConversion` — same shape at the far
  boundary (gem 11 empty, gem 12 memorized) via `castSpellOnTarget()`.

### Proof the tests can fail

Temporarily removed the `- 1` conversion from both `castSpell()` and
`castSpellOnTarget()` in `eq_action_handler.cpp` (verbatim backup kept
first), rebuilt just the new test target, ran it: **both tests failed**,
each showing `combat->IsCasting()` unexpectedly `true` — the broken code
lets `castSpell(1)` silently route into the OTHER gem's memorized spell.
Restored the file from the verbatim backup (confirmed byte-identical via
`diff`), rebuilt, reran: both green again. Not tautological.

### Where the sweep stopped, what remains

Stopped after the gem-slot TRANSFORMS group — the clearest, highest-risk
finding, structurally identical to T90's bug (a silent off-by-one that
routes a real player action to the wrong game-state target). NOT
exhaustive. `targetSelf()`'s self/pet toggle was attempted and dropped:
`CombatManager::SetTarget()` validates the target id against
`EverQuest::GetEntities()` (EverQuest's own internal `m_entities` map,
distinct from `GameState::entities()`), and no existing test seam
populates it — even the reachable no-pet branch fails without one, since
`SetTarget()` rejects the player's own spawn id set only via
`Test_SetMySpawnIdForSeam` (which does not add an entity). Adding a new
production seam for this felt like a second unrelated production touch
beyond the gem-slot fix and was left for the next unit rather than forced
into this one's budget. `setMovementMode`/`setPositionState`/
`sendPetCommand`'s unvalidated casts, `targetGroupMember`'s validation, and
`hailTarget`'s fallback branch are also untested and listed by name in
`refactor/deferred.md`'s new T93 section, per the plan's Board instruction.

### Fakes that contradict rather than omit

None found. Every fake either correctly stands in for a true forwarder
boundary or is a deliberate isolation fake whose real counterpart's
coverage is a separate, already-tracked claim.
`RecordingActionHandler` OMITS conversions (it never claims any happen);
it does not contradict them.

### Build/test/visual gate

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build --output-on-failure` — **2080/2081 passed**
   (2081 total, 1 failure), matching the plan's expected baseline plus this
   unit's 2 new tests; sole failure the documented ALSA
   `AudioManagerTest.PreloadCommonSounds`.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. `scripts/ci/visual_gate.sh` — **PASS**, goldens untouched (test-only
   change, no production behaviour affected).
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh fable` — one pre-existing `WARN`
   (`variant_loading.cpp`, 1614 lines) in files this unit never touched
   (diff base has moved since branch point); nothing new from this unit
   exceeds any threshold.

### Deviations from the plan

- Attempted a third test (`targetSelf()`'s no-pet branch) before finding it
  blocked on a missing test seam for `EverQuest::m_entities`; removed
  rather than left flaky or papered over with a new production seam beyond
  budget. Documented above and in `refactor/deferred.md` instead of
  silently dropped.
- No production code changed except the temporary, fully-reverted break
  used to prove the two new tests can fail (`src/client/eq_action_handler.cpp`,
  restored and diff-confirmed byte-identical before the final build).

## Board

Record the inventory in `refactor/deferred.md` with the untested TRANSFORMS
methods listed explicitly, so the remainder is picked up rather than
rediscovered. This is the same family as the reachability pattern — code that
looks covered but is not.
