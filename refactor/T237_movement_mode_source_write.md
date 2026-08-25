# T237 — write movementMode / positionState into GameState at the source (T234 finding)

Slot: slot1. x86. NO eqemu. Class A. Territory: the `EverQuest::SetMovementMode`
/ `SetPositionState` bodies (eq_movement.cpp or eq.cpp — grep), the two
`DISABLED_` tests in tests/test_eq_action_handler_transforms_real.cpp
(un-DISABLE them), tests/test_game_state_source_writes.cpp (add the two
cases). Do NOT touch inventory/*, command_processor equip path, eq_merchant
(T236 slot3) or renderer_frame.cpp / graphics_input_handler (T235 slot2).

## Defect
`PlayerState::setMovementMode/setPositionState` have zero production
callers; `EverQuest::SetMovementMode/SetPositionState` update private
members and push the bridge event only. `GameState.player()` stays
Run/Standing forever. Per docs/decisions.md 2026-08-25 (second entry) every
wire/action-learned state is written at the source before the publish.

## Scope
1. Add `m_game_state.player().setMovementMode(...)` / `setPositionState(...)`
   before the bridge push in each setter (mirror T221's pattern: write
   precedes any `if (!m_bridge)` guard).
2. Un-DISABLE the two T234 tests; add matching WithBridge/NoBridge cases to
   test_game_state_source_writes.cpp. Red (revert the two lines) / green.
3. Check whether the server echo (SpawnAppearance AT_Anim / position-state
   packets for SELF) also needs a write — if `ZoneProcessSpawnAppearance`
   handles our own spawn id for these types, write there too; else note.

## Verification
Full ctest; Review; evidence review/T237/. Commit:
`T237: movement mode and position state written into GameState at the source`.
