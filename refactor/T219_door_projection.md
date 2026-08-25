# T219 — one door instance: project doors into GameState, retire the third copy

Slot: slot2. x86. Does NOT own eqemu initially — see Live below. Decision:
docs/decisions.md 2026-08-25 GameState projection.

## Defect
`state::GameState::doors()` is populated on NEITHER instance. The live door
data is a THIRD `DoorStateManager`, `EverQuest::m_door_state_manager`
(`eq.h`, written at `eq_entities.cpp:576` on spawn and ~2206/2307 on
open/close; read only by `eq_bridge.cpp:368` for renderer snapshots). So
`/door` (`/u`), the `Interact` hotkey and `ActionDispatcher::clickDoor` /
`clickNearestDoor` / `interactNearest` (`action_dispatcher.cpp:573-620`)
always fail "No door nearby", and `EqActionHandler::clickNearestDoor`
(`eq_action_handler.cpp:645`) reads the equally-empty EverQuest copy.

## Scope
1. Make `EverQuest` write doors into `m_game_state.doors()` (its own
   GameState) instead of the separate `m_door_state_manager`; delete the
   third member; keep `eq_bridge.cpp:368`'s renderer snapshot working off
   the GameState copy.
2. Add `DoorSpawned` / `DoorStateChanged` projection (check `event_bus.h`
   for an existing door event first) so `Application::m_gameState->doors()`
   mirrors it via `GameStateSyncBridge`; include zone-unload clearing
   (mirror what `ZoneUnloading` does for entities).
3. Tests: projection lands; red when the case is removed (measure it);
   existing door tests still pass.
4. Live: `/door` next to a real door (qeynos2 has plenty; player must be
   within the dispatcher's range) opens it — confirm via the server's
   `OP_ClickDoor` handling / door state change on a second client or the
   renderer's door snapshot log. eqemu: T218 holds the lock first. Do all
   code+tests, then check `/mnt/projects/fable/.eqemu-lock`; if present,
   commit and report "live leg pending" — the orchestrator will message you
   when the lock is free; do not wait in a loop.

## Territory
`include/client/eq.h` (door member only), `src/client/eq_entities.cpp` door
paths, `src/client/eq_bridge.cpp:368` area, `game_state_sync_bridge.cpp`
(door cases only — T218 is adding a player-flags case in the same switch:
add yours at the END of the switch to keep the rebase trivial),
`event_bus.h` additive, `door_state_manager.*`, tests.

## STOP
If `DoorStateManager` semantics differ between the renderer's needs and the
action layer's (e.g. coordinate frame), report rather than fork a fourth copy.

## Review (2026-08-25, slot2, branch task/T219-door-projection off fable 3f5eaf30)

### What changed
- `EverQuest::m_door_state_manager` (and its `GetDoorStateManager()` accessor)
  DELETED. Every reader now uses `m_game_state.doors()` (the write model):
  `eq_entities.cpp` (spawn add, MoveDoor get/set, SendClickDoor get),
  `eq_bridge.cpp` (renderer `ZoneLoadSnapshot.doors` copy -- unchanged
  semantics, same `getAllDoors()` iteration), `eq_zone.cpp` (`CleanupZone`
  clear), `eq_commands.cpp` (diagnostics door count). `grep m_door_state_manager
  src include tests` = 0 hits.
- `DoorSpawnedData` gained `invertState` + `doorParam` (additive) so the
  projection holds the FULL `DoorState`, not a partial; the publisher in
  `ZoneProcessSpawnDoor` fills them.
- `GameStateSyncBridge`: `DoorSpawned` -> `doors().addDoor()`,
  `DoorStateChanged` -> `doors().setDoorState()` (both at the END of the
  switch per dispatch), and `ZoneUnloading` now also `doors().clear()`
  (one line inside the existing case -- same reasoning as T81's entity clear).
- No existing door event was missing: `DoorSpawned`/`DoorStateChanged` already
  existed in `event_bus.h` and were already published end-to-end for
  `IrrlichtBridge`; the gap was only that nothing fed them into the projection.

### Coordinate-frame STOP check
No STOP. Door x/y are server-swapped at parse time "same as entity spawns"
(`eq_entities.cpp`), the renderer consumes them via the snapshot unchanged,
and `ZoneProcessMoveDoor`'s audio distance already compares `door->x/y/z`
directly against `m_x/m_y/m_z` -- the same frame `PlayerState::x()/y()/z()`
and `DoorStateManager::getNearestDoor()` use. Renderer and action layer agree.

### Tests: measured red/green (`review/T219/projection_test_{red,green}.txt`)
6 new cases in `tests/test_game_state_sync_bridge.cpp`. Red = bridge TU
reverted to fable's copy (no door cases), test file kept:

| case | red (no cases) | green |
|---|---|---|
| DoorSpawnedAddsCompleteDoorToProjection | FAIL (count 0) | OK |
| DoorSpawnedMakesNearestDoorQueryFindIt | FAIL (nearest null) | OK |
| DoorStateChangedUpdatesOpenFlag | FAIL (door null) | OK |
| ZoneUnloadingClearsAllDoors | FAIL (count 0 != 2) | OK |
| DoorStateChangedForUnknownDoorIsANoOp | OK (by design: no-op either way) | OK |
| DoorEventsStillDelegateToWrappedBridge | OK (by design: delegation is unconditional) | OK |

Red = 4 FAIL / 2 OK, exit 1; green = 6/6, exit 0. First red attempt
SEGFAULTED (test derefed `getDoor()` without a null check) -- the test was
hardened to `ASSERT_NE(nullptr)` first so an absent case fails cleanly
instead of crashing; the table is from the hardened run.
Existing door tests (`test_action_dispatcher` door cases, `test_door_*`) pass.

### Verification bar
1. `cmake --build build -j24`: PASS.
2. Full ctest: **2760/2761** (`review/T219/ctest_full.txt` + `ctest_orphans_rerun.txt`).
   Only failure: documented ALSA `AudioManagerTest.PreloadCommonSounds`.
   `check_build_tree_orphans` failed on the first run -- 9 stale binaries in
   slot2's `build/bin` from its T161-era tree (`analyze_hum_model`,
   `test_eq_commands_{assist,attack,vendor}`, ...), pinned to since-deleted
   sources; deleted per the script's own instruction, re-run PASS. Not
   related to this unit.
3. `build-gles2-x86.sh`: PASS.
4. Visual gate: PASS on second run, goldens untouched. First run FAILED on a
   timing metric only (`befallen p95 CPU ms 45.8 > ceiling 40.1`) immediately
   after the ARM docker build finished; no image/memory assertion failed;
   rerun on an idle host PASS (`review/T219/visual_gate{,_rerun}.txt`).
5. `build-arm-noble.sh`: PASS.
6. `check_file_sizes.sh fable`: 5 ok / 3 WARN (pre-existing oversized eq.h,
   eq_bridge.cpp, eq_entities.cpp) / 0 FAIL.

### Live leg
See "Live leg" subsection below (filled when the eqemu lock was available).
