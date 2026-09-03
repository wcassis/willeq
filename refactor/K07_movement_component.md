# K07 — `MovementComponent` (family: movement — input, position updates, pathing, water)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED). Wave 2;
runs alongside K05/K06.

## Measured at `feb6b10b` (2026-09-03)

Three TUs, 2,229 lines, 42 bodies: `eq_movement.cpp` (486),
`eq_movement_state.cpp` (889), `eq_movement_update.cpp` (854), plus
`include/client/eq_movement_internal.h` (J23) which moves to
`include/client/movement/`. Three root `CMakeLists.txt` sites.

⚠ **This family's method names collide tree-wide** — `Move`, `Face`, `Jump`,
`Follow`, `UpdateMovement`, `FindPath` exist on the renderer, the combat
manager, the pathfinders and the input bridge. **A word grep is meaningless
here; the compiler is the only caller list.** Delete the declarations, build,
retarget what fails, repeat. Expect the real external callers to be:
`eq_action_handler*.cpp` (`Start*/Stop*`, `MoveToEntity`, `SetMovementMode`,
`SetPositionState`, `StopFollow`, `StopMovement`), `input/input_action_bridge.cpp`,
`eq_zone.cpp` / `eq_zone_entry.cpp` (`MoveTo`, `MoveToWithPath`, `FollowPath`,
`LoadPathfinder`, `StopMovement`), `combat_rounds.cpp` / `combat_hunting.cpp`
(`FaceEntity`, `StartCombatMovement`, `MoveToEntityWithinRange`, `UpdateMovement`),
`eq_bridge.cpp` (`UpdateWaterState`, `SendPositionUpdate`), `eq_gm.cpp`,
`eq_player_misc.cpp`, `eq_player_state.cpp`, `eq_player_social.cpp`,
`eq_entities.cpp`, `eq_commands_movement.cpp`, `eq_commands_spells.cpp`
(`CalculateDistance2D`), `eq_entities_appearance.cpp` (`CalculateDistance`),
`eq_entities_world.cpp` (`SendJump`), `application_frame.cpp` / `eq.cpp`
(`UpdateMovement`, the update thread), `include/client/action/action_dispatcher.h`,
`include/client/combat.h`, `include/client/state/player_state.h` (comments or
declarations — read each). `combat.h` declares `CombatManager` a friend of
`EverQuest` (CLAUDE.md "Friend class pattern"): if combat reaches movement
PRIVATE methods through that friendship, those methods become public on the
component and the Review says which.

Members:

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_last_position_update_time`, `m_test_last_sent_update` (+ its `Test_GetLastComputedPositionUpdate()` accessor), `m_use_pathfinding`, `m_water_surface_z` | 5 / 4 / 5 / 1 | none | **move** (4 + the accessor) |
| `m_current_path` (48), `m_current_path_index` (31), `m_pathfinder` (16), `m_navmesh_path` (1), `m_force_next_position_update` (2) | | `eq_zone.cpp` only | **move**; retarget the `eq_zone.cpp` reads through a small public getter on the component IF they are reads of state, or leave as `owner_` writes if they are zone-lifecycle resets — read each site and record. Prefer moving: zone is K09 and will call `Movement().ResetPath()` |
| `m_movement_history`, `m_last_movement_history_send` | 9 / 2 | `eq_zone.cpp`, `eq_zone_handshake.cpp`, `eq.cpp` | shared with zone: `owner_.`; K09 decides |
| `m_water_state` (4, `eq_bridge.cpp`), `m_zone_bsp_tree` (1), `m_test_bypass_send_guard` (4, group + seam), `m_pending_logout_disconnect` (3, player_social), `m_zone_out_*` (zone), `m_update_running`/`m_update_thread` (entities/app/combat), `m_my_spawn_id` (1, trade_manager) | | | shared: `owner_.` |
| core: `m_game_state` 220, `m_bridge` 12, `m_entities` 12, `m_zone_connection` 6, `m_combat_manager` 4, `m_character` 2, `m_skill_manager` 2, `m_audio_manager` 1, `m_zone_connection_manager` 1 | | | `ctx_.` |

Predicted `eq.h` delta: −42 declarations −9 members −1 accessor −banners +4
≈ **−45 or better**, assuming the path members move.

## Territory

`include/client/movement/` + `src/client/movement/` (new), the three
`eq_movement*.cpp` and `eq_movement_internal.h` (move), `eq.h`, `eq.cpp`
(ctor + `UpdateMovement` thread call sites), root `CMakeLists.txt` (three
lines), `tests/test_movement_component.cpp` + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the caller files the compiler
names. ⚠ `tests/test_first_person_camera_single_formula.cpp` names
`renderer_player_physics.cpp` as a string — not this family — but grep
`tests/` for `eq_movement` as a STRING before moving; repoint if found.

## Verification / Review

As K05. The `test_eq_action_handler_transforms_real.cpp` family (T234/T237)
pins movement transforms against the official client — they must pass
unchanged; if a call site there needs retargeting, that is a call-site edit
only.
