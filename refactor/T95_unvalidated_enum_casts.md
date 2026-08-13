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

Executed in `/mnt/projects/fable/wt/slot2`, task branch `task/T95-enum-casts`,
base `fable` tip `0636f67a` (note: `fable` moved to `0d765b0f` — T92's merge —
during this session; `check_file_sizes.sh` was run with `--files` against the
exact three files this unit touched to get an isolated result, since a plain
`fable`-diff would have picked up T92's unrelated changes too). Headless
throughout, no eqemu. Full findings and the `/pet` command-drift table are
also in `/mnt/projects/fable/review/T95/README.md` and
`refactor/deferred.md`'s new `## T95` section — this section summarizes.

### Part 1 — reachability per call site, and each enum's valid range

- **`setMovementMode(int mode)`** → `MovementMode` (`include/client/eq.h`):
  a **plain enum with no fixed underlying type**, contiguous range `0..2`
  (`MOVE_MODE_RUN=0`, `MOVE_MODE_WALK=1`, `MOVE_MODE_SNEAK=2`). An
  out-of-range `static_cast` into it is genuine undefined behaviour per
  [expr.static.cast], not merely a wrong result. **Reachability: NOT
  reachable from user input today.** The only call sites are
  `CommandProcessor::cmdWalk/cmdRun/cmdSneak` (`src/client/action/command_processor.cpp`),
  each passing a hardcoded compile-time constant (`0`/`1`/`2`) — `/walk`,
  `/run`, `/sneak` take no argument. `ActionDispatcher::setMovementMode(int)`
  and the `IActionHandler::setMovementMode(int)` interface both accept any
  `int`, so a future caller (script API, a numeric `/movemode <n>` command,
  a hotkey binding) could reach it directly — the guard is added for that
  surface and for robustness, not because a live path exists today.
- **`setPositionState(int state)`** → `PositionState` (`include/client/eq.h`):
  same shape — plain enum, no fixed underlying type, contiguous range `0..4`
  (`POS_STANDING=0`, `SITTING=1`, `CROUCHING=2`, `FEIGN_DEATH=3`, `DEAD=4`).
  **Reachability: NOT reachable from user input today.**
  `CommandProcessor::cmdCrouch/cmdFeign` pass hardcoded `2`/`3`; the other
  `SetPositionState()` call sites (`combat.cpp`, `eq_commands_movement.cpp`,
  `eq_action_handler.cpp`'s own `sit()`/`stand()`) all pass the named
  `POS_SITTING`/`POS_STANDING` constants directly to `EverQuest`, bypassing
  `EqActionHandler::setPositionState(int)` entirely. Same "guarded for the
  public-surface contract, not a live exploit" situation as `setMovementMode`.
- **`sendPetCommand(uint8_t command, uint16_t targetId)`** → `EQT::PetCommand`
  (`include/client/pet_constants.h`): declared `enum PetCommand : uint32_t`
  — a **fixed underlying type**. Per [expr.static.cast]p10, converting any
  value representable in the underlying type (every `uint8_t` value is
  representable in `uint32_t`) into a fixed-underlying-type enum is
  well-defined, just possibly a value with no corresponding enumerator — NOT
  the same UB class as the other two casts, and the plan's framing of "all
  three are the same severity" does not hold precisely for this one. It is
  still a real defect (silent garbage-command byte reaches
  `EverQuest::SendPetCommand()` and gets written into the wire packet with
  no complaint), just a different one: caller-bug-hiding, not memory-unsafe.
  The valid set is **non-contiguous**: enumerators exist at 1-14 and 16-21;
  0, 15, and 22-255 have no enumerator (15 is explicitly commented in
  `pet_constants.h` as "target command (doesn't send packet)" but has no
  name in the enum). **Reachability: reachable, but only via
  `ActionDispatcher`'s nine `/pet <name>` convenience wrappers
  (`action_dispatcher.cpp`), every one of which passes a hardcoded integer
  literal** — not raw user input directly, BUT see the live bug found below:
  several of those hardcoded literals are themselves wrong/stale (pre-dating
  a pet_constants.h renumbering), so "hardcoded" here did not mean "safe" in
  practice. `/pet <command>` (`CommandProcessor::cmdPet`) only ever routes to
  these nine named wrappers, never a raw number, so there is still no path
  for a live user-supplied out-of-range byte today — but the guard usefully
  catches the four wrappers whose stale literals are themselves out of range
  (see below).

### Part 2 — the fix: reject and log, never clamp

No existing enum-bounds helper was found in the codebase (searched for
`IsValid*Enum`/`EnumInRange`/`ValidateEnum`-shaped names — none exist).
Added three private static guard methods on `EqActionHandler`
(`include/client/eq_action_handler.h`, `src/client/eq_action_handler.cpp`):

- `isValidMovementMode(int)` / `isValidPositionState(int)` — plain
  `value >= min && value <= max` checks (their enums are contiguous, so a
  shared shape between these two is correct — they are NOT a single shared
  helper function since the two ranges differ, but the two implementations
  are structurally identical one-liners, kept as two named functions for
  clear call-site error messages rather than factored into one templated
  helper, which would have been more machinery than three call sites
  justify).
- `isValidPetCommand(uint8_t)` — a `switch` listing all 20 real
  `EQT::PetCommand` enumerators, `default: return false` — **deliberately
  NOT the same shape** as the other two, because the valid set is
  non-contiguous (a min/max range check would wrongly accept the gap value
  15, which is exactly what one of the new tests proves).

All three call sites now validate before casting and, on rejection, both
`LOG_ERROR` (module `MOD_MOVEMENT` for the first two, `MOD_COMBAT` for the
pet one) and call `EverQuest::AddChatSystemMessage()` so the rejection is
visible in-game, not just in the log — following T77's "say what was wrong
in terms the caller can act on" standard:

- `setMovementMode`: `"Movement mode <N> is not valid (must be 0=Run,
  1=Walk, or 2=Sneak)."`
- `setPositionState`: `"Position state <N> is not valid (must be 0-4:
  Standing/Sitting/Crouching/FeignDeath/Dead)."`
- `sendPetCommand`: `"Pet command <N> is not valid and was not sent."`

No clamping anywhere; on rejection the function returns immediately and the
real `EverQuest` setter/sender is never called (proved in Part 3).

### Part 3 — tests: 15 new, all against the real `EqActionHandler`

`tests/test_eq_action_handler_enum_guards.cpp` (registered in
`tests/CMakeLists.txt`), using the T88/T90/T93 headless
`EverQuest`+`EqActionHandler` construction idiom. Neither `MovementMode` nor
`PositionState` has a public getter on `EverQuest`, so observability uses
the bridge: `EverQuest::SetMovementMode()`/`SetPositionState()` each push a
`PlayerMovementModeChanged`/`PlayerPositionStateChanged` event (only when
the value actually changes — eq_movement.cpp's own no-op guard, worked
around in the boundary-low tests by moving away from the default first).
`AddChatSystemMessage()` also pushes a `SystemMessage` event, giving a
second, distinguishing signal. A minimal concrete `GameStateBridge`
subclass (`RecordingBridge`, local to the test file, no-op `applyEvent()`)
wired via `EverQuest::setBridge()` captures both.

- **Valid values reach the real enum member** (4 tests): `setMovementMode`
  at both boundaries (0=Run, 2=Sneak) and `setPositionState` at both
  boundaries (0=Standing, 4=Dead) each produce exactly the expected
  `PlayerMovementModeChanged`/`PlayerPositionStateChanged` event carrying
  the correct value.
- **Out-of-range values are rejected without invoking the cast** (5 tests):
  `setMovementMode(-1)`, `setMovementMode(3)`, `setMovementMode(9999)`,
  `setPositionState(-1)`, `setPositionState(5)` each produce exactly one
  `SystemMessage` rejection event and explicitly assert the ABSENCE of the
  corresponding `PlayerMovementModeChanged`/`PlayerPositionStateChanged`
  event (proving the real setter, and therefore the cast, was never
  reached) — not merely the absence of all events, since the guard's own
  chat report is an expected event.
- **`sendPetCommand` valid/invalid + interior-gap boundary** (6 tests):
  `PET_BACKOFF`(=1, low) and `PET_HOLD_OFF`(=21, high) pass the guard with
  no rejection message; `0` (below range), `22` (above range), `255` (max
  `uint8_t`), and **`15` (numerically inside 1..21 but has no
  enumerator — the case a naive min/max check would wrongly accept)** are
  each rejected with exactly one `SystemMessage` naming the value.
  `EverQuest::SendPetCommand()` itself is gated behind
  `m_zone_connection_manager` (a real network connection, no test seam) +
  `HasPet()` (`m_pet_spawn_id != 0`, likewise no seam) — the same shape T93
  hit and declined to force a new seam for on `targetSelf()`. This means
  full local round-trip proof through to the actual packet write isn't
  reachable headlessly for the valid-value tests; what's proven instead is
  that valid enumerators pass the guard silently (vs. invalid ones being
  loudly rejected before `EverQuest::SendPetCommand()` is ever entered) —
  stated plainly as a limitation, not hidden.

**Proof every test can fail (two separate red passes, not one):**
1. Backed up `eq_action_handler.cpp` verbatim. Replaced all three
   `isValid*` bodies with `return true;` (guard disabled — the original
   unvalidated-cast behaviour). Rebuilt just the test target, ran it:
   **9 of 15 tests failed** — exactly the 9 "rejected" tests (the 6 "valid
   value" tests still passed, since bypassing the guard doesn't break them).
2. From the same backup, instead replaced all three bodies with
   `return false;` (guard over-rejects everything). Rebuilt, ran: the
   **complementary 6 tests failed** — exactly the "valid value" tests (the
   9 "rejected" tests still passed, since over-rejection still rejects).
3. Restored from the verbatim backup; `diff` confirmed byte-identical;
   rebuilt; ran again: **all 15 green.**

Together, both passes show every one of the 15 tests can independently go
red for the failure mode it's meant to catch — not tautological.

### Further unvalidated casts / defects noticed, left for follow-up

- **The `/pet` command-drift bug** (found while tracing `sendPetCommand()`'s
  call sites for reachability, per Part 1 above): `git log -p` on
  `pet_constants.h` shows commit `b1c722b0` renumbered `EQT::PetCommand` to
  correct Titanium wire values but never updated the raw integer literals
  in `action_dispatcher.cpp`'s nine `/pet` wrapper methods, which still use
  the OLD numbers under comments claiming the NEW names. Net effect today:
  `petAttack`/`petFollow`/`petSit`/`petTaunt` send the wrong (but
  in-range, so T95's guard can't catch it) command; `petBackOff`/`petHold`/
  `petFocus`/`petHealth` send now-invalid values that T95's new guard will
  reject outright (a visible behavior change — previously silent-wrong,
  now loudly rejected, which is the standard's intended trade but worth
  flagging since it changes what `/pet backoff` etc. do today). `petGuard`
  is correct by coincidence (its literal happens to match). Full
  value-by-value table in `refactor/deferred.md`'s new `## T95` section.
  **Out of this unit's territory** (`action_dispatcher.cpp`, not
  `eq_action_handler.cpp`) — not fixed here, recorded for a follow-up unit.
  Not live-verified (found via static analysis + `git log -p`, no eqemu
  used or needed for the finding itself).
- T93's other three deferred `EqActionHandler` items —
  `targetSelf()`'s self/pet toggle, `targetGroupMember()`'s validation,
  `hailTarget()`'s fallback branch — are unrelated to enum casts and remain
  open, unchanged by this unit (out of scope per the plan).

### Verification bar

1. `cmake --build build -j24` — clean, no new warnings.
2. `ctest --test-dir build --output-on-failure` — **2095/2096 passed**
   (2096 total; plan's stated baseline 2080/2081 + this unit's 15 new
   tests = 2096 — matches exactly). Sole failure the documented ALSA host
   exception `AudioManagerTest.PreloadCommonSounds`.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. `scripts/ci/visual_gate.sh` — **PASS** (qeynos2/gfaydark/befallen);
   goldens untouched — this unit made no rendering changes, confirmed by
   `git status --short` showing only `eq_action_handler.{h,cpp}`,
   `tests/CMakeLists.txt`, and the new test file changed.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh --files <the 3 touched files>` —
   0 WARN, 0 FAIL for all three (185/719/372 lines). Note: a plain
   `fable`-diff run picked up two unrelated pre-existing WARNs from T92's
   concurrent merge (`constrained_texture_cache.cpp`,
   `zone_geometry.cpp`, neither touched by this unit) — the `--files` form
   was used instead to get an isolated, accurate result per the protocol's
   own guidance about a moved base ref.

### Deviations from the plan

- **Severity nuance on `sendPetCommand`**: the plan frames all three casts
  as the same UB severity class. `EQT::PetCommand` has a fixed
  (`uint32_t`) underlying type, so per the C++ standard its out-of-range
  cast is well-defined (an unspecified-but-not-undefined value), unlike
  the other two's plain-enum UB. Flagged explicitly in Part 1 rather than
  silently treating all three as equivalent, per the plan's own
  instruction not to imply equivalence when reachability/severity differs.
  The fix (reject-and-report) is identical regardless, since the defect —
  a meaningless value reaching the wire — is real either way.
- **`sendPetCommand`'s "valid values reach the intended enum member" proof
  is partial**, not full round-trip: `EverQuest::SendPetCommand()` is
  gated behind a real network connection + `HasPet()`, neither fakeable
  without a new production seam outside this unit's territory (same shape
  T93 hit for `targetSelf()` and declined to force). What's proven is that
  the guard itself passes valid enumerators through silently and rejects
  invalid ones loudly, not that the packet ultimately reaches the wire
  correctly — stated here rather than glossed over.
- **`fable` moved during this session** (T92 merged, `0636f67a` →
  `0d765b0f`) — `check_file_sizes.sh` run with `--files` instead of the
  protocol's literal `fable` invocation to avoid an inaccurate diff; noted
  above.
- No STOP condition was hit. No production enum was renumbered. No
  clamping was added anywhere. `RecordingActionHandler` was not touched.
  The scope was not extended beyond the three named casts — the `/pet`
  literal-drift bug is a **different** defect (wrong data, not an
  unvalidated cast) found as a byproduct of the required reachability
  analysis, reported rather than fixed, per the plan's explicit
  instruction for adjacent findings in the same file/area.
