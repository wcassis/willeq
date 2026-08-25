# T234 — pin the five untested EqActionHandler transforms against the real EverQuest

Slot: slot1. x86. NO eqemu. Class A (T93 leftover, deferred.md ~L5462).
Territory: tests/ only (a new or existing test_eq_action_handler_transforms*
binary + tests/CMakeLists.txt). Do NOT edit production code; if a test
exposes a defect, write the failing test, mark it DISABLED_ with a comment
naming the defect, and report it — do not fix in this unit. Do NOT touch
tests that T230 (slot3, unmerged) edits: test_action_dispatcher.cpp,
test_input_action_bridge.cpp, test_eq_action_handler_hotbar_item.cpp, or
combat/hotbar tests.

## Gap (T93)
`EqActionHandler` methods whose transform logic is exercised only through
the fake handler, never against the real `EverQuest`: `targetSelf` (self /
pet toggle), `targetGroupMember(index)` (validation + bounds), `hailTarget`
(fallback when no target), `setMovementMode`, `setPositionState`. Build
the real `EverQuest` the way tests/test_eq_action_handler_trade.cpp and
tests/test_game_state_source_writes.cpp do (RecordingBridge + Test_ seams),
drive each method through real state (a pet spawned via the packet path,
group members via the group handlers, no target), and assert on
`GetGameState()` / the recorded bridge events / the sent packet bytes.
Each test must be shown to FAIL against a deliberately wrong expectation
or a stubbed-out method (measure red the T219 way) so it is known to bite.

## Verification
Full ctest; Review in this file (list any defect found); evidence
review/T234/. Commit: `T234: EqActionHandler transforms pinned against the real client`.

## Review (2026-08-25, slot1, branch task/T234-transform-tests from b417ffff)

### Delivered

`tests/test_eq_action_handler_transforms_real.cpp` (new, 561 lines,
registered in `tests/CMakeLists.txt` as `test_eq_action_handler_transforms_real`),
21 tests: 19 active, 2 `DISABLED_` (defect below). One 3-line comment update
in `tests/test_eq_action_handler_transforms.cpp` pointing at the new file
(T93's "not covered here" note). No production code changed -- confirmed
by sha256 of `src/client/eq_action_handler.cpp` before/after the red
sweep (`review/T234/red/SUMMARY.txt`) and `git status` (tests/ only).

### How the T93 blockers were cleared without a new seam

- `EverQuest::m_entities` / `m_pet_spawn_id` / `m_group_members` are
  populated by replaying real NewSpawn and GroupUpdate packets through the
  existing `ReplayZonePacket` seam (`zone_stream_test_utils.h`). The pet is
  detected by the production pet-detection path (is_pet + petOwnerId ==
  m_my_spawn_id), not by `Test_SetPetSpawnIdForSeam`.
- `hailTarget()`'s only observable is the C->S ChannelMessage packet, and
  `SendChatMessage()` refuses without `m_zone_connection`. The harness
  builds a REAL `DaybreakConnection` on a REAL `DaybreakConnectionManager`
  over the existing `FakeUdpTransport` (test_daybreak_state_machine.cpp's
  fixture) and installs it into `m_zone_connection` via the T17
  `#define private public` idiom (test_game_state_source_writes.cpp
  precedent) plus `Test_SetZoneConnectedForSeam(true)`. Every reliable
  packet EverQuest queues sits, framed but unencoded, in
  `m_streams[0].sent_packets`, so the tests assert the actual TargetMouse
  target id and the ChannelMessage channel/message bytes.

### Coverage per method

| method | tests | asserted on |
|---|---|---|
| targetSelf | 5 | CombatManager target, GameState combat().targetId(), TargetMouse bytes: no-target->self, self-no-pet stays self (never 0), self->pet->self toggle, third-party->self not pet, spawn-id-0 sends nothing |
| targetGroupMember | 7 | index 0 = self, in-zone member -> their spawn, not-in-zone (spawn_id 0) no change AND m_current_target_id untouched, index==count, index==MAX_GROUP_MEMBERS, negative, not-in-group |
| hailTarget | 4 | "Hail" on say with no target; "Hail, Guard Hezlan" (toDisplayName of Guard_Hezlan000); fallback after clearTarget; fallback when target vanished from GameState but combat id is stale |
| setMovementMode | 2 (+1 DISABLED) | every valid int -> `m_movement_mode` enumerator (private read); rejection leaves source unchanged and reports the exact message |
| setPositionState | 2 (+1 DISABLED) | same shape |
| harness | 1 | SetTarget on a spawned entity reaches the wire with the right id |

setMovementMode/setPositionState were already guard-tested by T95
(bridge-event level); this unit adds the source-member mapping and the
rejection message text. Deliberately small -- no suite inflation.

### Measured red (`review/T234/red/`)

8 deliberate breaks in eq_action_handler.cpp, each compiled, run, and
reverted (byte-identical sha256). All 8 caught; per-break test lists in
`review/T234/README.md`. Two artefacts in `SUMMARY.txt` (a duplicate sed
pattern for break 05 and an mtime-preserving `mv` on restore) are
explained there; both were re-run and the rerun lines are authoritative.

### Defect exposed (not fixed -- tests-only unit)

**GameState player movementMode/positionState projection is never
written.** `state::PlayerState::setMovementMode()`/`setPositionState()`
have zero production callers; `EverQuest::SetMovementMode()`/
`SetPositionState()` write only the private members and the bridge event.
`GetGameState().player().movementMode()` is permanently `Run`,
`positionState()` permanently `Standing`. Pinned by
`DISABLED_SetMovementMode_WritesGameStatePlayerProjection` and
`DISABLED_SetPositionState_WritesGameStatePlayerProjection`
(`review/T234/red/disabled_tests_fail_as_expected.log`). Nothing reads
those two fields today, so it is a T221 "written at the source" contract
gap / dead projection, not a user-visible bug. Fix is two one-line source
writes in eq_movement.cpp; enable the two tests with it.

Also observed, not a defect but worth knowing: `targetGroupMember()`
ignores `SetTarget()`'s bool and calls `updateRendererTargetInfo()`
unconditionally, so if the `spawn_id != 0` guard were ever removed,
`m_current_target_id` would be zeroed while CombatManager keeps the old
target (break 03 shows exactly that divergence). The guard is now pinned.

### Verification

1. `cmake --build build -j24`: PASS.
2. `ctest`: **2830/2831**, sole failure `AudioManagerTest.PreloadCommonSounds`
   (documented ALSA quirk). `review/T234/ctest_full.log`.
3-5. build-gles2-x86 / visual gate / ARM: NOT RUN -- tests-only unit, no
   production or shader change; the plan's bar is "Full ctest". Flagged as
   a deviation from the protocol's default bar, not silently skipped.
6. `check_file_sizes.sh`: the slot's `fable` ref has advanced past this
   branch's base (T230/T233 merged), so `fable`-relative mode reports
   their files; run with `--files` on the three files this unit touched:
   2 ok, 1 pre-existing WARN (tests/CMakeLists.txt), 0 fail.

### Deviations

- Verification steps 3-5 not run (above).
- The sibling file `test_eq_action_handler_transforms.cpp` got a 3-line
  comment addition only; T230's excluded files were not touched.
