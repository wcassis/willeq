# T88 — Is the position frame actually broken, or did we mis-drive it?

**Slot: slot1. Headless first. Live leg only if Part 3 requires it — and
eqemu must be free and locked normally if so.**

**Territory: `src/client/eq_movement.cpp`, `src/client/eq_action_handler.cpp`,
the `/move` command surface, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T88/`.**

Read T86's Review, then T87's Review **including the merge commit's
qualification of it** (`git log 381a8bd1`), then this plan in full before
touching anything.

## Do not assume there is a bug

T86 concluded WillEQ reports a position ~1300 units from where it believes it
is, axis-mirrored, and that this is "a real WillEQ-side defect." **That
conclusion is not established, and your first job is to determine whether it
is true at all.**

Here is the competing explanation, which fits every observed number without
any production defect:

- `/move` → `EqActionHandler::moveToLocation()` → `EverQuest::Move()` →
  `MoveToWithPath()` operates in **client** frame — the frame of `m_x`/`m_y`.
- The T85 join spec gave `Oracledelta`'s position as
  `zone 1 (qeynos) x = -653 y = 314 z = 3.75`, and T86 confirmed those numbers
  match `Oracledelta`'s own **wire** self-report — i.e. **server** frame.
- T85's executor issued `/move -650 310 3.75`, feeding **server-frame numbers
  to a client-frame command**.
- Inbound convention is `m_x = server_y`, `m_y = server_x`. Outbound
  (`eq_movement.cpp:1064-1068`) sends `x_pos = m_y`, `y_pos = m_x` — a correct
  inverse that round-trips cleanly.
- So `/move -650 310` sets client `(-650, 310)` and correctly transmits server
  `(310, -650)`. Observed wire value: `(306.462, -646.949)`. **That is exactly
  what a correct client does when handed mirrored input.**

Under that reading WillEQ did precisely what it was told, and the fault is a
coordinate-frame confusion in the harness.

**Establish which it is before changing a single line.** Both outcomes are
valuable and neither is embarrassing; shipping a "fix" for a non-bug is.

Note also a known EQ trap worth checking while you are here: the official
client's `/loc` conventionally reports **Y, X, Z** order, not X, Y, Z. If the
oracle session read those coordinates off a client UI, the frame labelling in
the join spec may itself be muddled. Do not treat any documented coordinate
as frame-labelled unless you can show where it came from.

## Part 1 — Prove the round trip, headlessly

Before any live work, establish with a test what the code actually does:

- Feed a known server-frame position through the inbound
  `ZoneProcessPlayerProfile` path, then read what `SendPositionUpdate()` would
  transmit. **Assert identity.** If it round-trips, the encoder and decoder
  are a matched pair and the defect is not there.
- Do the same through `Move()`/`MoveToWithPath()`: set a client-frame target,
  run the movement update, and check the transmitted position corresponds to
  that target under the documented convention.

This is cheap, deterministic, and it settles Part 1 without a server. **These
tests should survive as regression tests regardless of the outcome** — nothing
currently pins this round trip, which is why the ambiguity persisted.

## Part 2 — The real defect may be the interface, not the math

Even if the math is correct, **the frame is undocumented at the command
surface, and that ambiguity is what actually cost us a live session**. `/move
-650 310` is accepted silently whichever frame the caller meant.

If Part 1 shows the math is sound, treat this as the finding and fix *that*:

- Document the frame explicitly at `EverQuest::Move()`, `moveToLocation()`,
  and the `/move` command registration — client frame, with the relationship
  to server frame stated.
- **Log the frame conversion** when a move is issued: target in client frame,
  and the server-frame position it will transmit. One log line would have made
  T85's problem obvious within seconds instead of costing a capture session
  and two analysis units.
- Consider whether `/move` should echo back where it thinks it is going in
  both frames. Propose; do not build a new command surface unasked.

## Part 3 — Only if Part 1 finds a genuine defect

If the round trip does *not* hold, you have found a real bug. Then:

- State it in one sentence with the failing round-trip test as evidence.
- Fix the narrowest thing that makes the test pass.
- **STOP and report instead of fixing** if the fix requires changing the
  coordinate convention broadly. That convention is threaded through every
  consumer in the client — renderer, pathfinding, BSP, audio, navmesh (see
  `docs/CLAUDE.md`'s coordinate table) — and a broad change is its own unit
  with its own review, not a side effect of this one.
- A live leg to confirm against the server is warranted **only** in this
  branch. Take the eqemu lock normally if so; it is free now.

## What you may not do

- **Do not touch heading or rotation code.** The `90 - server_heading_deg`
  player offset is deliberate and hard won — see the load-bearing comment in
  `src/client/eq.cpp` and `docs/CLAUDE.md`'s Heading/Rotation Conventions.
  This unit is about **position**. T87's apparent ~252° heading offset is
  **an artifact** of exactly the frame confusion under investigation here —
  its Pass A cardinals are spaced 59°/74°/101° where the official client's are
  spaced 92°/90°/89°, and a constant offset preserves spacing. **Heading is
  re-measured after this unit, not during it.**
- Do not "fix" T87's 252°. It is not a real transformation.
- Do not change the coordinate convention broadly (see Part 3 STOP).

## Verification

- Full build and suite (expect 2066/2067; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`). New round-trip tests must pass and
  must fail if the conversion is inverted — **prove that by temporarily
  inverting it and showing the test goes red**, then restore. A test that
  cannot fail is not a test.
- Visual gate; if goldens change, say so explicitly and explain why.
- Findings to `/mnt/projects/fable/review/T88/` with a README.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T88-position-frame`, base `af745eda`. No eqemu server was started
(`.eqemu-lock` never touched) — Part 1's headless proof settled the
question without one.

### Verdict: the round trip holds. T86 found a harness frame confusion, not a production defect.

**One sentence**: WillEQ's inbound-decode/outbound-encode position round
trip is IDENTITY (proven headlessly, `tests/test_position_frame_roundtrip.cpp`),
and T85's `/move -650 310 3.75` fed `Oracledelta`'s own **server-frame**
wire self-report to `Move()`'s **client-frame** command surface, which
correctly transmitted it axis-mirrored — exactly what T86 measured,
exactly what a correct client does, and not a WillEQ encoder/decoder bug.

### Part 1 — headless proof

`tests/test_position_frame_roundtrip.cpp`, 4 new tests, registered in
`tests/CMakeLists.txt` (same auto-discover idiom as
`test_zone_position_convention`):

1. **`ServerFrame_InboundThenOutbound_IsIdentity`** / **`_SecondSample`** —
   feed a known server-frame position through the real
   `ZoneProcessPlayerProfile` decoder (via `ReplayZonePacket`), then read
   what the real `SendPositionUpdate()` encoder computes. Two
   independently-chosen coordinate pairs, including the exact T85/
   Oracledelta magnitudes. **Result: exact identity both times** — the
   decoder's `m_x = server_y, m_y = server_x` and the encoder's
   `x_pos = m_y, y_pos = m_x` are a correct, matched inverse.
2. **`MoveToLocation_ClientFrameTarget_TransmitsUnderDocumentedConvention`**
   — drives the real `/move` entry point
   (`EqActionHandler::moveToLocation()` -> `EverQuest::Move()`) with a
   client-frame target and confirms both (a) `m_x`/`m_y` land at the
   target UNSWAPPED (proving `Move()` is client-frame) and (b) the
   transmitted server-frame position is the target's `(y, x)`, per the
   documented convention.
3. **`T85Scenario_ServerFrameNumbersFedToClientFrameMoveProduceObservedMirroring`**
   — reproduces T85's exact scenario end to end: `/move -650 310` (T85's
   real issued values) transmits server-frame `(310, -650)`, matching
   T86's measured wire value `(306.462, -646.949)` within the documented
   `/move` waypoint slop.

**Why a test seam was needed and why it's narrow**: `SendPositionUpdate()`'s
top guard and `MoveToWithPath()`'s "Not in zone yet" guard both require a
live `m_zone_connection`/`IsFullyZonedIn()` that no headless test can
construct. Everything past those guards already degrades gracefully with a
null connection (`SafeQueueZonePacket()`/`SendMovementHistory()` both
self-check and no-op), so the guards are stricter than production actually
needs for safety — they're just the only thing standing between a headless
test and the real encoder. Added, in `include/client/eq.h`/
`src/client/eq_movement.cpp`, all documented test-only, default-off, same
idiom as T52's existing `Test_SetZoneConnectedForSeam`:
- `Test_SetBypassSendGuardForSeam(bool)` — skips those two specific guards
  plus `SendPositionUpdate`'s process-wide-static send throttle (which
  would otherwise make test order/timing-dependent).
- `Test_SetMySpawnIdForSeam(uint16_t)` — `SendPositionUpdate()` silently
  no-ops when `m_my_spawn_id` is still 0 (by design); lets a test drive
  `Move()` without first replaying a full `PlayerProfile`.
- `Test_GetLastComputedPositionUpdate()` — read-only capture of the
  encoder's computed `x_pos`/`y_pos`/`z_pos`/`heading_scaled`, taken
  unconditionally right before the (possibly test-suppressed) send.
- `Test_SnapToMovementTargetForSeam()` — mirrors `UpdateMovement()`'s own
  "distance < 2.0f" arrival branch (`m_x = m_target_x` etc. then
  `StopMovement()`) without depending on wall-clock-driven incremental
  movement steps, for the T85-scenario test's large (-650, 310) target.

All four are additive, default-off/no-op in production, and documented
in-place with why they exist. **Production behavior is unchanged when the
seam is not engaged** — confirmed by the fact that the guards' conditions
are unchanged (`!m_test_bypass_send_guard &&` prefixed onto the exact same
checks) and by the full suite passing with the seam off everywhere except
these four new tests.

**Proof the tests can fail**: temporarily inverted the outbound
assignment (`update.y_pos = m_x; update.x_pos = m_y;` ->
`update.y_pos = m_y; update.x_pos = m_x;`), rebuilt, reran — **all 4
tests went red** with the expected inverted-value failure messages.
Reverted, rebuilt, reran — all 4 green again. Full console output:
`/mnt/projects/fable/review/T88/inversion_test_output.txt`.

### Part 2 — documented and logged the frame (the real fix)

- **Frame contract documented** at all four sites on the `/move` call
  path: `EverQuest::Move()`, `MoveToWithPath()` (`src/client/eq_movement.cpp`),
  `EqActionHandler::moveToLocation()` (`src/client/eq_action_handler.cpp`),
  and the `/move` command's `usage`/`description` strings
  (`src/client/eq_commands_movement.cpp`).
- **Both frames logged** from `Move()` (`LOG_INFO`, `MOD_MAIN`): the
  client-frame target as given, and the server-frame position that will
  actually transmit.
- **`/move`'s chat response echoes both frames** (`Moving to client=(...)
  -> server=(...)`) — the plan's proposed echo-both-frames behaviour,
  applied to the *existing* command surface (no new command added, per
  the plan's instruction not to build unasked).

This is the change that actually addresses what cost T85 a capture
session: the math was never wrong, but nothing on the whole `/move` call
path said which frame it expected until now.

### Part 3 — not entered

Part 1 found the round trip is identity, so Part 3's fix-only-if-broken
branch does not apply. No live eqemu session was needed, used, or locked.

### The `/loc` Y,X-ordering trap

Checked and **not implicated**. T86 already established the T85 join
spec's coordinates match `Oracledelta`'s own decoded **wire**
self-report (from the capture, via the real Daybreak decoder), not a
`/loc`-command UI reading — so the well-known official-client `/loc`
Y,X-display-ordering trap does not apply to this data path. Worth keeping
in mind for any future join spec assembled from a `/loc` reading instead
of a decoded wire capture.

### Board updates

`refactor/deferred.md`: corrected the T85/T86 ClientUpdate-broadcast entry
with a T88 addendum stating plainly that T86's "real WillEQ-side defect"
conclusion does not hold, with the round-trip evidence and the T85-scenario
reproduction; corrected the entry title itself so it no longer reads as a
confirmed defect. Also appended a T88 addendum to the T87 heading entry
recording: the merge-time qualification of T87's "~252°" still stands
(not superseded by this unit); heading was deliberately not touched or
re-measured here; and a follow-up unit should re-run T87's method now that
the `/move` frame ambiguity is documented/logged, to see whether a
corrected capture produces clean, evenly-spaced cardinal deltas.

### Verification

- `cmake --build build -j24` — clean, exit 0.
- `ctest --test-dir build --output-on-failure` — **2070/2071**, sole
  failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`
  (suite grew from the 2066/2067 baseline by exactly the 4 new tests in
  this unit's file; the `visual_gate` ctest entry itself passed, so
  goldens are untouched — no visual gate output to report). Full output:
  `/mnt/projects/fable/review/T88/ctest_output.txt`.
- New tests individually reconfirmed: `ctest -R PositionFrameRoundTrip` —
  4/4 passed.
- Proof of failability: see Part 1 above and
  `/mnt/projects/fable/review/T88/inversion_test_output.txt`.
- No visual/screenshot capture was produced — this unit made no
  rendering-affecting change (movement/position math and log/chat text
  only; no shader, geometry, or asset path touched), so there is nothing
  for Will to look at visually. `review/T88/README.md` explains this
  explicitly rather than silently omitting the section.

### Deviations from the plan

- Added four test-only seam methods to `EverQuest` (`include/client/eq.h`
  / `src/client/eq_movement.cpp`) that the plan did not name explicitly,
  to make Part 1's headless proof possible at all — the plan's Part 1
  instructions ("read what `SendPositionUpdate()` would transmit") assumed
  this was reachable, and without a seam it is not (both relevant guards
  require a live connection). Followed the project's existing precedent
  (T52's `Test_SetZoneConnectedForSeam`) for how to do this narrowly:
  default-off, test-only, documented in place, and shown not to change
  production behavior (guards' effective conditions are byte-identical
  with the seam unset).
- Part 2's `/move` chat-message and usage-string text changed
  (`eq_commands_movement.cpp`) is a user-facing string change beyond pure
  logging; flagged here rather than silently bundled in, though it is the
  exact "echo back both frames" behaviour the plan explicitly invited as
  a proposal, applied to the existing surface with no new command added.
- No live eqemu leg was taken (Part 1 made it unnecessary); confirmed
  `.eqemu-lock` untouched throughout.

## Board

Record the verdict against T86's entry in `refactor/deferred.md` — it
currently states this as a confirmed WillEQ defect, and if that is wrong it
must not be left standing. Also record that heading re-measurement is blocked
on this unit's outcome.
