# T344 — A server-forced pose cannot be applied without echoing it back (B001)

Class A. Slot3. `MovementComponent::SetPositionState` (K07 moved it to
`src/client/movement/movement_state.cpp`): every arm ends in
`SendSpawnAppearance`, so when the SERVER forces a pose (stun, root/mez
knockdown, GM `#kill`/`#summon`, feign, death) the client re-sends the
pose it was just told about. Its `AT_DIE` arm encodes a pose self never
receives (T311 Leg A measured: `AT_DIE` appears zero times in either
direction; the official client's death sequence is `OP_SpawnAppearance`
type 14 param 115 (Lying) then no more `OP_ClientUpdate` then
`OP_ZoneChange` — a full zone-in).

## Fix

1. Split the state write from the wire send: `ApplyPositionState(state)`
   (internal, no send — writes `game_state.player().positionState()`
   and publishes whatever the renderer needs, exactly what each arm does
   today minus the `SendSpawnAppearance`) and `SetPositionState(state)`
   = `ApplyPositionState` + the send (the user-initiated path: `/sit`,
   `/stand`, feign key). Bodies verbatim minus the send line — the
   multiset proves it.
2. Server-forced callers use `ApplyPositionState`: find them — the
   `OP_SpawnAppearance` handler for SELF (`ZoneProcessSpawnAppearance`
   in `entities/` when `spawn_id == MySpawnId()`), the stun handler
   (`ZoneProcessStun` in `commands/combat_actions.cpp` — does it force a
   pose?), the death path for self (`OP_Death`/`OP_BecomeCorpse` — read
   `player/player_state.cpp`'s death handling; T311's finding: self death
   is Lying via appearance then a zone-in, NOT `AT_DIE`). Delete the
   `AT_DIE` arm's send (it can never be right); keep the state write if
   any path reaches it, else delete the arm and record.
3. Tests: (a) a forced pose through the self-appearance handler leaves
   the outbound queue EMPTY (recording bridge / `PacketSender` seam) and
   the state set; (b) `/sit` still sends exactly one appearance; (c) the
   T311 death capture replayed (`review/T311/` has the packets; the
   session-replay oracle or `zonetest::ReplayZone`) produces no outbound
   appearance from self. Pre-fix failing runs pasted. The T234/T237
   transform suites and `test_zone_out_handshake` pass unchanged.

Territory: `src/client/movement/movement_state.cpp`,
`include/client/movement/movement_component.h` (T343 may be editing the
Start*/Stop* lines of the same header — touch only the SetPositionState
lines), the self-appearance and death handlers named (call-site edits),
tests, `refactor/BOARD.md`. No graphics files: gate + ARM skipped with
evidence. ⚠ `movement_state.cpp` is NOT golden-pinned (G06 pinned
`renderer_player_physics.cpp`); confirm with `grep -l movement_state tests/goldens`.

STOP: a forced-pose caller needs a state-module change beyond a publish;
the death path needs the zone-in re-implemented (that is a separate,
larger unit — report the shape).

## Review

Branch `task/T344-forced-pose-setter` off `fable` at `5fd018ee`.

### The split

`src/client/movement/movement_state.cpp` now holds two functions where it
held one:

- **`ApplyPositionState(PositionState)`** — the whole pre-split body minus
  the five `SendSpawnAppearance` lines: the equality guard, the
  `game_state.player().setPositionState()` source write (T237/T286), the
  `PlayerPositionStateChanged` bridge publish, the per-arm `StopMovement()` /
  `CancelCamp()`, and the debug log. No wire traffic of its own.
- **`SetPositionState(PositionState)`** — the same equality guard (so a
  no-op change still sends nothing, exactly as before), then
  `ApplyPositionState(state)`, then a switch that re-adds **four** of the
  five sends: 100 Standing, 110 Sitting, 111 Crouching, `ANIM_LYING` for
  feign death. The multiset of sends is unchanged except for the deliberate
  `AT_DIE` deletion below.

One ordering nuance the split introduces, recorded rather than hidden: when
the new pose also stops movement, `StopMovement()`'s `OP_ClientUpdate` now
precedes the `OP_SpawnAppearance` instead of following it. The multiset of
outbound packets is identical and eqemu's `Handle_OP_SpawnAppearance` and
`Handle_OP_ClientUpdate` read none of each other's state.

`include/client/movement/movement_component.h`: one declaration added next
to `SetPositionState`; nothing else in that header was touched (T343 owns
the `Start*`/`Stop*` lines).

### Every caller and the setter it uses

**`SetPositionState` (user-initiated, still sends) — unchanged, all five:**

| Call site | What the user did |
|---|---|
| `src/client/action/eq_action_handler.cpp:137` | `/sit` |
| `src/client/action/eq_action_handler.cpp:141` | `/stand` |
| `src/client/action/eq_action_handler_extended.cpp:67` | the `setPositionState(int)` action |
| `src/client/action/eq_action_handler_extended.cpp:754` | `/camp` sit-down |
| `src/client/combat_hunting.cpp:234,244` | auto-hunting sit-to-med / stand-to-fight |

**`ApplyPositionState` (server-forced, sends nothing) — two NEW call sites:**

| Call site | Trigger |
|---|---|
| `src/client/entities/entities_component.cpp` `ZoneProcessSpawnAppearance`, `AT_ANIMATION` arm, `spawn_id == ctx_.self.MySpawnId()` | any pose the server forces on us — root/mez knockdown, GM `#kill`/`#summon`, a spell that sits us |
| `src/client/player/player_state.cpp` `ZoneProcessDeath`, `victim_id == ctx_.self.MySpawnId()` | our own `OP_Death` → `POS_DEAD` |

Both were **holes, not echoes**: before this unit *nothing* wrote
`game_state.player().positionState()` from the zone stream, so it stayed
`Standing` through every server-forced pose and through our own death. The
echo would have appeared the moment either was wired to the only setter that
existed — which is what B001 was warning about.

Mapping used at the appearance site: `ANIM_STAND`/`ANIM_STANDING` →
`POS_STANDING`, `ANIM_SITTING` → `POS_SITTING`, `ANIM_CROUCHING` →
`POS_CROUCHING`, `ANIM_LYING` → `POS_FEIGN_DEATH` (the state whose *outbound*
pose is `ANIM_LYING`; the wire cannot distinguish feign from lying-dead).
Guard: a Lying appearance does **not** downgrade a state already `POS_DEAD`,
because that appearance is the death we already know about. Pinned by
`LyingAppearanceAfterDeath_DoesNotDowngradeDead`.

**`ZoneProcessStun` (`src/client/commands/combat_actions.cpp`) — measured, no
change.** Its whole body is the `m_stunned_until` deadline plus the two
string-table messages (12479/12480). It forces no pose and calls no position
setter, so it has nothing to route through the new one.

### The AT_DIE decision: send DELETED, arm KEPT

Measurement, not argument. In `review/T311/legA/decode/
T311A_table2_opcode_timeline.tsv` — a full official-client session including
two self-deaths — `AT_DIE` (`OP_SpawnAppearance` type 0) appears **zero
times in either direction**. What the official client actually does on its
own death (record `09-01-2026 20:12:31`) is C→S `OP_SpawnAppearance` type
`0x0e` param `0x73` (AT_ANIMATION, 115 = Lying), immediately followed by C→S
`OP_ZoneChange` — a full zone-in. So the `AT_DIE` send can never be right and
is gone.

The **arm is kept**, because `POS_DEAD` is still reachable by measurement:
the `setPositionState(4)` action reaches it from the user side, and
`ZoneProcessDeath` now reaches `ApplyPositionState(POS_DEAD)` from the
server side. Both need the state write, the bridge publish and the
`StopMovement()` that the arm carries; only the send was wrong.

### The three tests

`tests/test_forced_pose_no_echo.cpp` (new, 8 tests, registered in
`tests/cmake/zone_integration.cmake`). Harness idiom is T234's and T322's: a
real `DaybreakConnection` over `FakeUdpTransport` installed as
`m_zone_connection`, with all three `IsFullyZonedIn()` terms set so
`SendSpawnAppearance`'s own early return cannot be mistaken for a missing
send. **The connection IS the seam here**: `SendSpawnAppearance` calls
`zone_connection->QueuePacket` directly and never goes through
`PacketSender::Queue`, so the T268 capture seam cannot see it.

Leg (c) replays the T311 self-death `OP_Death` **verbatim** — the 32 payload
bytes copied out of the opcode timeline.

**Pre-fix run** (production patch reverse-applied, test unchanged and
rebuilt — 4 of 8 failed, `4 FAILED TESTS`):

```
[  FAILED  ] ForcedPoseNoEcho.SelfAppearanceSitting_SetsStateAndQueuesNothing
  test_forced_pose_no_echo.cpp:172: Failure
  Expected equality of these values:
    h.eq.GetGameState().player().positionState()   Which is: 4-byte object <00-00 00-00>
    eqt::state::PositionState::Sitting             Which is: 4-byte object <01-00 00-00>
  a pose the server forced on us must reach the player's positionState

[  FAILED  ] ForcedPoseNoEcho.UserDeadState_NeverSendsAtDie
  test_forced_pose_no_echo.cpp:248: Failure
  Expected: (a.first) != (static_cast<uint16_t>(AT_DIE)), actual: 0 vs 0
  AT_DIE is not an appearance any client ever sends
  test_forced_pose_no_echo.cpp:251: Failure
  Value of: h.sentAppearances().empty()   Actual: false   Expected: true

[  FAILED  ] ForcedPoseNoEcho.T311SelfDeathCapture_SetsDeadAndSendsNoAppearance
  test_forced_pose_no_echo.cpp:276: Failure
  Expected equality of these values:
    h.eq.GetGameState().player().positionState()   Which is: 4-byte object <00-00 00-00>
    eqt::state::PositionState::Dead                Which is: 4-byte object <04-00 00-00>
  OP_Death for our own spawn id is what drives self's positionState

[  FAILED  ] ForcedPoseNoEcho.LyingAppearanceAfterDeath_DoesNotDowngradeDead
  test_forced_pose_no_echo.cpp:294: Failure
  Expected equality of these values:
    h.eq.GetGameState().player().positionState()   Which is: 4-byte object <00-00 00-00>
    eqt::state::PositionState::Dead                Which is: 4-byte object <04-00 00-00>
```

**Post-fix run:** `8 tests from ForcedPoseNoEcho ... [  PASSED  ] 8 tests.`

Honest note on leg (b): `UserSit_StillSendsExactlyOneAppearance` and
`UserStand_StillSendsExactlyOneAppearance` are **preservation pins and passed
before as well as after** — they cannot fail before a change whose whole point
is not to disturb them. What they buy is the proof that the send was not lost
in the split, and they would have caught it if it had been. The plan's "each
of the three tests fails before" holds for legs (a) and (c); leg (b) is
green on both sides by construction.

### Step results

- `cmake --build build -j24` — **exit 0**. (One earlier build hit the 10-minute
  tool timeout at exit 143; per `EXECUTOR_PROTOCOL.md` the zero-byte sweep
  under `build/bin` and `build/lib` found nothing and both archives validated
  — `libEtcLib.a` 15 members, `libwilleq_core.a` 321 — and the re-run was
  clean.)
- `ctest --test-dir build -j6 --output-on-failure` — **3,814 of 3,816
  passed.** `AudioManagerTest.PreloadCommonSounds` is the expected ALSA host
  quirk. `visual_gate` failed under `-j6` load on a CPU-timing flake only
  (`befallen: p95 process-CPU ms 15.614 exceeds ceiling 10.948`, no pixel
  diff) and **passes on its own**: `ctest -R '^visual_gate$'` → `1/1 Passed
  (32.62 s)`. Suite count is up 8 from the 3,808 this branch inherited — the
  new file, nothing removed.
- Named suites re-run individually, assertions untouched, all PASS:
  `test_eq_action_handler_transforms_real` (T234/T237),
  `test_game_state_source_writes` (T237), `test_zone_out_handshake`,
  `test_movement_component`, `test_reachability_sweep`,
  `test_eq_action_handler_enum_guards`, `test_beneficial_spell_casting`.
- `bash scripts/refactor/check_file_sizes.sh --all` — `checked 876 file(s):
  876 ok, 0 warn, 0 fail`.
- gles2 / visual-gate-specific / ARM runs skipped: **no graphics file is
  touched.** `git diff --name-only` is exactly
  `include/client/movement/movement_component.h`,
  `src/client/entities/entities_component.cpp`,
  `src/client/movement/movement_state.cpp`,
  `src/client/player/player_state.cpp`, `tests/cmake/zone_integration.cmake`,
  plus the new `tests/test_forced_pose_no_echo.cpp`.
- `movement_state.cpp` confirmed NOT golden-pinned: `grep -rl movement_state
  tests/goldens` returns nothing.

### STOPs

Neither STOP condition fired as a blocker, but one is reported as required.

- **No state-module change** was needed. `ApplyPositionState` writes the
  existing `PlayerState::setPositionState` and publishes the existing
  `PlayerPositionStateChanged` event; no module gained a field, an
  enumerator or an accessor.
- **The death zone-in is NOT in this unit, as planned.** The death test's
  no-outbound-appearance assertion *was* provable without it, and so was the
  `positionState == Dead` half, so the unit closes on its own terms. The
  shape of what remains, for whoever takes it: T311 Leg A measured the
  official client's self-death as three steps — (1) C→S `OP_SpawnAppearance`
  AT_ANIMATION param 115 (Lying) sent by the client itself in answer to
  `OP_Death`; (2) **no further `OP_ClientUpdate` at all** while dead; (3) C→S
  `OP_ZoneChange` (88 bytes, the bind-zone re-entry) roughly at once,
  answered by two S→C `OP_ZoneChange` and a full zone-in handshake. We
  implement none of the three. Step (2) is not free either: today
  `ApplyPositionState(POS_DEAD)` runs `StopMovement()`, which fires one last
  `SendPositionUpdate()`, and the position-update pump is not gated on being
  dead. That unit needs the zone-in state machine and a death gate on the
  update pump; it is well beyond a pose setter.
