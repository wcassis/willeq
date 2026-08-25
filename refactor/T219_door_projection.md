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
