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

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T90-setheading`, base `fable` tip `1cc77307`.

### The fix

`src/client/eq_action_handler.cpp`, `EqActionHandler::setHeading()`. Read
`EverQuest::SetHeading()` (`eq.cpp:3133`) first to confirm what it actually
stores, per the plan's instruction — it does a bare `m_heading = heading;
m_game_state.player().setHeading(heading);`, no conversion of any kind.
Removed the premature `heading * 512.0f / 360.0f` conversion; the function
now normalizes into `[0, 360)` in degrees and passes that straight to
`SetHeading()`:

```cpp
void EqActionHandler::setHeading(float heading) {
    float normalizedHeading = heading;
    while (normalizedHeading < 0.0f) normalizedHeading += 360.0f;
    while (normalizedHeading >= 360.0f) normalizedHeading -= 360.0f;
    m_eq.SetHeading(normalizedHeading);
    m_eq.SendPositionUpdate();
}
```

### Other `SetHeading()` callers

Searched (`grep -rn "SetHeading"`) — three other call sites, all inside
`eq.cpp`/`eq_login_world.cpp`, all inbound/state-restoration paths (the
zone-in `PlayerProfile` handler's `new_heading` — itself already the
correct `90 - server_heading_deg` degrees value — the same-zone respawn
handler's `bind_heading`, and the pending-zone-transition handler's
`m_pending_zone_heading`). All three already pass plain degrees; none
pre-converts to compensate for the bug. `ActionDispatcher::setHeading()`/
`faceLocation()`/`faceEntity()` and `InputActionBridge`'s keyboard-turn and
mouse-look handlers (the only other callers of the *interface-level*
`setHeading()`, which is what routes into `EqActionHandler`) also all pass
plain degrees. **`EqActionHandler::setHeading()` was the sole double-
conversion site; nothing else needed to change.**

### Existing test that encoded the bug

**None.** `tests/test_action_dispatcher.cpp`'s `SetHeading_NormalizesAndPassesThrough`
and the `FaceLocation`/`FaceEntity` tests exercise `ActionDispatcher`
against `RecordingActionHandler`, a test fake that implements
`IActionHandler::setHeading()` as a bare recording call with no unit
conversion — so those tests only ever verified the dispatcher's own
degree-in/degree-out normalization and routing, never the real
`EqActionHandler::setHeading()` implementation where the bug lived. No
existing test constructed a real `EqActionHandler` and called `setHeading()`
end to end, so nothing was asserting on the buggy 512-unit values — the bug
had zero test coverage in either direction, not "passing because of it."

### New tests (`tests/test_setheading_conversion.cpp`, 8 cases)

Constructs a real headless `EverQuest` + `EqActionHandler` (same idiom as
T88's `test_position_frame_roundtrip.cpp`) and asserts `eq.GetHeading()`
(reads `m_heading` via `GameState`) after calling `handler.setHeading()`
directly:
- `SetHeading90_LeavesHeadingAt90NotDoubleConverted`, `SetHeading225_...`,
  `SetHeading0_IsZero`, `SetHeading359_IsUnchanged` — direct value checks,
  including the exact 90->128 and 225->320 values T84/T90 measured live.
- `SetHeadingNegative10_WrapsTo350`, `SetHeading400_WrapsTo40` — wrap cases.
- `FaceEntity_RealHandler_ProducesSaneHeadingNotScaled` (entity due north),
  `FaceEntity_NonCardinalDirection_HeadingStaysInDegreeRange` (entity due
  east, expects exactly 90 — the double-conversion's most direct
  fingerprint) — both wire a REAL `EqActionHandler` into `ActionDispatcher`
  (not the `RecordingActionHandler` fake) so `/face`'s shared code path is
  actually exercised end to end, not just the dispatcher's routing layer.

### Proof the tests can fail

Per the plan's requirement: temporarily reverted `setHeading()` to the
original double-converting code (kept a copy of the fixed file first),
rebuilt just the new test target, and ran it — **6 of 8 tests failed**,
each with exactly the old double-converted numbers as the actual value
(`90->128`, `225->320`, `359->510.578`, `-10-normalized->497.778`,
`400-normalized->56.889`, `/face east->128`); the two cardinal-adjacent
cases that happened to coincide (`0`, and the due-north `/face` case where
0 is a fixed point of the scaling) still passed, as expected. Restored the
fix, rebuilt, reran — all 8 green again. This confirms the tests are not
tautological.

### Live wire verification (predicted vs transmitted)

`eqemu` up (`arena` zone, `Testchar`/GM, position server-frame `(0,0,51)`
so client-frame position is `(0,0)`), binary rebuilt and confirmed newer
than both the source edit and HEAD before any live conclusion. Ran with
`-d 4 --log-level=DEBUG --record-session`, script: force daylight,
`/turn 90`, `/turn 225`, `/turn 45`, `/face 50 0 51`, `/camp`. Read the
actual transmitted wire heading from `eq_movement.cpp`'s own
`POS C->S [SELF] m_heading=... -> server_heading=... -> heading_scaled=...`
log line (the real outbound encoder's own computed values, not an ack):

| command | predicted `m_heading` (post-fix) | predicted wire `server_heading` (`90-deg` mod 360) | predicted `heading_scaled` (`×2048/360`) | actual `m_heading` | actual wire `server_heading` | actual `heading_scaled` |
|---|---|---|---|---|---|---|
| `/turn 90`  | 90  | 0   | 0    | 90.00  | 0.00   | 0    |
| `/turn 225` | 225 | 225 | 1280 | 225.00 | 225.00 | 1280 |
| `/turn 45`  | 45  | 45  | 256  | 45.00  | 45.00  | 256  |
| `/face 50 0 51` (player at client-frame origin, target due east: `calculateHeadingTo` predicts 90°) | 90 | 0 | 0 | 90.00 | 0.00 | 0 |

**Exact match on every sample, including the wrap case (`/turn 225`,
previously measured live at T84/T90 as the buggy `320` value) and `/face`
sharing the same fixed function.** Full run log:
`/mnt/projects/fable/review/T90/t90_live_verification_run.log`; tour
script: `/mnt/projects/fable/review/T90/t90_tour_script.txt`.

### Build/test

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build --output-on-failure` — **2078/2079**, sole
   failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`
   (matches the plan's expected 2070/2071 baseline plus this unit's 8 new
   tests, plus other units' growth since T89's baseline was written).
3. `bash scripts/build-gles2-x86.sh` — clean, used for the live session
   above.
4. `scripts/ci/visual_gate.sh` — **PASS, goldens untouched** — exactly the
   "no signal" outcome a heading-only fix should produce; no golden change
   to explain.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh fable` — `ok
   src/client/eq_action_handler.cpp: 616 lines` (1 file checked; the new
   test file is not a size-check target per that script's own scope).

### Teardown

`Testchar` restored to the documented baseline (`zone_id=36,
x=-77.9232, y=21.9086, z=3, heading=190`) and confirmed via `SELECT`
**after** the in-session `/camp` logout (not while still connected, per
the plan's own caution about `character_data` reflecting only the last
save). `Testmelee`/`Testcaster`/`Testbuddy` were never touched this unit
(confirmed unchanged in the same `SELECT`). `scripts/eqemu/down.sh` run
clean (all `akk-stack-*` containers stopped/removed, confirmed via `docker
ps -a`). Lock file deleted, confirmed via `ls` failing.

### Deviations from the plan

None of substance. One incidental finding worth noting: the first live
run used `-d 4` without `--log-level=DEBUG` and produced zero
`POS C->S [SELF]` lines despite `-d 4` alone being sufficient for the
*command-outcome* (`CMD outcome=applied`) logging — the `LOG_DEBUG(
MOD_MOVEMENT, ...)` wire-heading line needs the module-level `DEBUG`
threshold too (`--log-level=DEBUG`), which `-d 4` alone does not set for
`LOG_DEBUG`-macro output (it only sets the legacy `s_debug_level` int that
gates the `if (s_debug_level >= 2)` check around that same line). Re-ran
with both flags together and got full wire evidence; not a code change,
just a note for future live-verification sessions that both flags are
needed together for this particular log line, matching how T84 originally
ran it.

## Board

`refactor/deferred.md` carries several now-superseded heading entries —
T87's "~252°" and T89's "+90°" are both artifacts, and the `setHeading`
entries predate T89's confirmation that nothing else in the stack is wrong.
**Correct them.** A wrong premise left on the board propagated twice in this
arc already.
