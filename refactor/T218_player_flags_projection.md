# T218 — project player AFK/anon flags (and settle the mouse-look heading read)

Slot: slot1. x86. OWNS eqemu (two-client probe). Decision: docs/decisions.md 2026-08-25 GameState projection.

## Defect
`Application::m_gameState->player()` never receives `setAFK`/`setAnonymous`/
`setRoleplay` — only `EverQuest::m_game_state.player()` does
(`eq_player_info.cpp` ~787-870). So `ActionDispatcher::toggleAFK`
(`action_dispatcher.cpp:809`) and `CommandProcessor` `/anon`
(`command_processor.cpp:1144`) read a permanently-false flag and compute
`newState = true` on every call: the toggles cannot turn OFF via those paths.

## Scope
1. Add a `PlayerFlagsChanged` (or reuse an existing appearance event if one
   already carries AFK/anon/roleplay — check `event_bus.h` first) projection
   case in `GameStateSyncBridge` → `m_state.player().setAFK/...`. Publisher
   side: wherever `EverQuest` already sets its own flags (the SpawnAppearance
   handlers), publish through `m_bridge` like the other projected domains.
2. Test in `tests/` that (a) the projection lands and (b) fails when the
   case is removed — MEASURE red/green.
3. Live: two clients. Testchar `/afk` → second client's `/who` or spawn
   appearance shows AFK; Testchar `/afk` again → clears. Same for `/anon`.
4. Heading: `input_action_bridge.cpp:220,254` reads `m_state.player().heading()`
   which is never projected (always 0). Determine at RUNTIME whether
   `InputActionBridge::processMouseInput` is reached in the graphical build
   (a LOG_DEBUG or breakpoint during mouse-look). If reached: project
   `PlayerMoved` heading too and note it. If never reached: do NOT fix — record
   it in deferred.md as dead-path candidate for a delete unit. Either way,
   report the answer with evidence.

## Territory
`src/client/bridge/game_state_sync_bridge.cpp`, `include/client/state/event_bus.h`
(additive only), the `eq_player_info.cpp` flag setters (publish only), tests.
Do NOT touch doors, `action_dispatcher.cpp` door methods, or `eq_entities.cpp`
(T219's territory).

## STOP
If the flags turn out to be projected by some path this plan missed, stop
and report the path — do not add a duplicate.
