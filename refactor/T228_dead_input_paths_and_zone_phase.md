# T228 — delete the dead InputActionBridge input paths; make ZoneProcessNewZone complete its request phase

Slot: slot3. x86. NO eqemu. Class A. Do NOT touch application.cpp,
include/client/bridge/*, src/client/bridge/*, eq_merchant.cpp, tests/
test_game_state_sync_bridge.cpp, tests/test_game_state_source_writes.cpp
(T227), or any graphics loader (T226).

## 1. Dead input paths (T218 evidence, review/T218/, deferred.md T218 entry)
`InputActionBridge::processMouseInput`'s heading branch reads
`m_state.player().heading()` (never projected → 0) and fired 0 times in 120
mouse-motion events because the render thread consumes the same destructive
`getMouseDeltaX()` first; `processContinuousInput()` has ZERO callers. Had the
branch ever fired it would have sent a position update from heading 0.
Delete both paths (and the T218 DEBUG probe left in place), any now-unused
members/includes, and their tests if they only tested the dead code
(tests/test_input_action_bridge.cpp — check each case). Keep everything
else in the class. Confirm with grep that no caller remains.

## 2. ZoneProcessNewZone never calls CheckZoneRequestPhaseComplete (T04)
`eq.cpp` ~1918 defines it; the only callers are in eq_player_info.cpp
(:57,:65,:74,:83). NewZone is one of the phase inputs but the handler never
calls the check, so completion rests on the other packets arriving after
NewZone (true today by server causality). Add the call at the end of
`ZoneProcessNewZone` (idempotent — verify the function is), and a test in
tests/test_zone_connection_flow.cpp that NewZone arriving LAST still
completes the phase. Red/green measured.

## Verification
Full ctest; Review in this file; evidence review/T228/. Commit:
`T228: dead InputActionBridge paths deleted; NewZone completes the zone-request phase`.

## Review (executor, slot3, 2026-08-25, base dc494ef7)

### Item 1 -- dead InputActionBridge paths deleted

Deleted from `src/client/action/input_action_bridge.cpp` /
`include/client/action/input_action_bridge.h`:

| what | why it was dead |
|---|---|
| `processContinuousInput(float)` (73 lines) | zero callers; `update()` had explicitly dropped it (Batch D) |
| `processMouseInput(float)` (whole function) | its ONLY content was the mouse-look heading branch (T218 probe: 0 of 120 motion events reached it; would have turned from unprojected heading 0). Nothing else lived in it, so the function and its `update()` call went too -- this is the "whole function is dead after that" case named in the dispatch |
| `updateMovementState()` | empty body; only called from `processContinuousInput` |
| the `[T218]` LOG_DEBUG probe | inside the deleted branch |
| `m_mouseSensitivity`, `m_invertMouseY`, `m_turnSpeed` + 6 get/set accessors | only readers were the deleted paths; only external users were the deleted tests |
| `m_movingForward/Backward`, `m_strafingLeft/Right`, `m_turningLeft/Right` | only written by `processContinuousInput` |
| `#include "client/state/game_state.h"` in the .cpp | `m_state` is no longer dereferenced |

Kept: `m_state` and the `(GameState&, ActionDispatcher&)` constructor --
application.cpp constructs it that way and application.cpp is T227
territory. Annotated in the header. `update()` keeps its `deltaTime`
parameter (same reason) with a `(void)` cast.

Tests: `MouseLook_*` (5 cases) and `ConfigAccessors_RoundTrip` removed from
`tests/test_input_action_bridge.cpp` -- each exercised only the deleted
code. 37 -> 31 tests, 31/31 pass (`review/T228/item1_input_bridge_tests_after.txt`).

Grep before/after (`review/T228/grep_before.txt`, `grep_after.txt`): 63 hits
-> 5, and the 5 survivors are three comments naming the deletion plus the
unrelated `CameraController::setMouseSensitivity` (camera_controller.h /
irrlicht_renderer.cpp). No caller remains.

### Item 2 -- ZoneProcessNewZone now completes the zone-request phase

`CheckZoneRequestPhaseComplete()` added as the last statement of
`ZoneProcessNewZone` (eq.cpp, after the four request sends).

**Idempotent as-is: yes.** Its condition is
`m_new_zone_received && m_aa_table_count>0 && m_tribute_count>0 &&
m_guild_tribute_count>0 && !m_req_client_spawn_sent`; every input is
monotonic (false->true / counts only grow) and the send is guarded by
`m_req_client_spawn_sent`, which it sets itself. Calling it early (NewZone
first, counters at 0) is a no-op; calling it after it has fired is a no-op.
No change to the function was needed.

Measured red/green in `tests/test_zone_connection_flow.cpp`:
- T04's `NewZoneArrivingLast_DoesNotFireUntilAnotherGateArrives` (which
  ASSERTED the buggy behavior) rewritten as
  `NewZoneArrivingLast_CompletesPhase`; new `NewZoneFirst_CheckFromNewZoneIsANoOp`
  pins the early-call no-op.
- RED (`review/T228/item2_red_before_fix.txt`, test built against unfixed
  eq.cpp): `NewZoneArrivingLast_CompletesPhase` FAILS -- suppressed sends 4,
  expected 5; phase not ZONE_REQUEST_PHASE.
- GREEN (`review/T228/item2_green_after_fix.txt`): whole binary passes.
- Header comment and one stale cross-reference in the ordering test updated.

### Verification

| step | result |
|---|---|
| `cmake --build slot3/build -j24` | exit 0 (`review/T228/build_full.log`; the only warnings are pre-existing `-Wpointer-arith` in eq.cpp:1222-1279, untouched) |
| `ctest` full | **2853/2854**, sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA quirk). Baseline 2745/2746 at e70e3347; count grew with T215-T227 merges plus this unit's +1 test / -6 tests |
| `check_file_sizes.sh fable` | 5 ok, 1 FAIL: `src/client/eq.cpp` 3950 lines -- PRE-EXISTING (3940 at fable base, already >3000); +10 lines here, no new FAIL |
| gles2-x86 / visual gate / ARM | not run -- the plan's Verification section narrows the bar to full ctest; no rendering or platform-specific code touched |

Diff: 5 files, +76 / -287.

### Deviations
- None from scope. One judgment call flagged: `processMouseInput` was
  removed entirely rather than emptied, per the dispatch's "unless the whole
  function is dead after that" clause -- it had no targeting role, only the
  mouse-look branch.
