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

## Review

Executed in `wt/slot3` on `task/K07-movement-component`, branched from `fable`
at `c4d3ea97`. Ran alongside K05 (slot1) and K06 (slot2).

### Compiler-derived caller list vs the grep lead

The plan's warning held: the family's names collide tree-wide and the grep lead
was a lead only. The declarations were deleted from `eq.h`, the tree was built
with `-k`, and every site the error list named was retargeted. **The compiler
found 36 production call sites in 14 files and 47 test call sites in 8 files.**

| file | sites | receiver expression | change |
|---|---|---|---|
| `src/client/application.cpp` | 1 | `m_eqClient->` | `SetPathfinding` |
| `src/client/application_frame.cpp` | 1 | `m_eqClient->` | `UpdateMovement` |
| `src/client/combat_hunting.cpp` | 4 | `m_eq->` | `SetPositionState` x2, `Move`, `MoveToEntityWithinRange` |
| `src/client/combat_rounds.cpp` | 4 | `m_eq->` | `IsPathfindingEnabled`, `Move`, `StartCombatMovement`, `FaceEntity` |
| `src/client/eq_action_handler.cpp` | 12 | `m_eq.` | `SendPositionUpdate`, `Jump`, `SetPositionState` x2, the four `Stop*`, `Move`, `MoveToEntity`, `Follow`, `StopFollow` |
| `src/client/eq_action_handler_extended.cpp` | 4 | `m_eq.` | `SetMovementMode`, `SetPositionState` x2, `SetPathfinding` |
| `src/client/eq_bridge.cpp` | 2 | unqualified (inside `EverQuest::`) | `UpdateWaterState`, `SendPositionUpdate` -> `m_movement->` |
| `src/client/eq_bridge_intents.cpp` | 1 | unqualified | `CalculateDistance2D` |
| `src/client/eq_commands_movement.cpp` | 1 | unqualified | `SendPositionUpdate` |
| `src/client/eq_commands_spells.cpp` | 1 | unqualified | `CalculateDistance2D` |
| `src/client/eq_entities_appearance.cpp` | 1 | unqualified | `CalculateDistance` |
| `src/client/eq_player_social.cpp` | 1 | unqualified | `StopMovement` |
| `src/client/eq_zone.cpp` | 7 | unqualified / direct member writes | `SendPositionUpdate`, `StopMovement`, and the five path-member sites below |
| `src/client/eq_zone_entry.cpp` | 1 | unqualified | `LoadPathfinder` |
| `tests/test_position_frame_roundtrip.cpp` | 26 | `eq.` | `SendPositionUpdate`, `Test_GetLastComputedPositionUpdate`, `UpdateMovement`, `Test_SnapToMovementTargetForSeam` |
| `tests/test_beneficial_spell_casting.cpp` | 6 | `eq_->` | `UpdateMovement`, `SetPositionState`, `SendPositionUpdate` |
| `tests/test_game_state_source_writes.cpp` | 4 | `eq.` | `SetMovementMode` x3, `SetPositionState` |
| `tests/test_zoning_graphics_integration.cpp` | 4 | `eq_->` | `UpdateMovement` |
| `tests/test_zoning_integration.cpp` | 3 | `eq_->` | `UpdateMovement` |
| `tests/test_position_store_coherence.cpp` | 2 | `eq.` | `MoveTo`, `Test_SnapToMovementTargetForSeam` |
| `tests/test_hunt_target_body_type.cpp` | 1 | `eq.` | `SetPathfinding` |
| `tests/test_zone_position_convention.cpp` | 1 | `eq.` | `Test_GetLastComputedPositionUpdate` |

**Assertions were not touched anywhere** — only the receiver of the call.
`test_eq_action_handler_transforms_real.cpp` (T234/T237) needed **no edit at
all**: it drives movement through `EqActionHandler`, whose forwarders were the
things retargeted, so the transform pins are untouched by construction.

**Where the plan's expected-caller list was wrong:**

| plan expected | measured |
|---|---|
| `input/input_action_bridge.cpp` | **no call site.** It drives `ActionDispatcher`, which reaches movement via `EqActionHandler` — the bridge names nothing in this family |
| `eq_gm.cpp`, `eq_player_misc.cpp`, `eq_player_state.cpp`, `eq_entities.cpp` | **no call sites** |
| `eq_entities_world.cpp` (`SendJump`) | **no call site** — `SendJump` has exactly one caller, `Jump()`, inside the family |
| `eq_zone.cpp` `MoveTo`/`MoveToWithPath`/`FollowPath` | **none of those.** `eq_zone.cpp`'s real traffic is `StopMovement`, `SendPositionUpdate` and five direct writes to the path members |
| `include/client/action/action_dispatcher.h`, `include/client/combat.h`, `include/client/state/player_state.h` | **comments/declarations only**, no reference (K04 correction g again) |
| `combat.h`'s `friend class CombatManager` reaching movement privates | **it does not.** Everything `combat_rounds.cpp` / `combat_hunting.cpp` calls (`Move`, `StartCombatMovement`, `FaceEntity`, `MoveToEntityWithinRange`, `SetPositionState`, `IsPathfindingEnabled`) is **public** on the component. `MovementComponent` declares **no friend** |

### Methods: 43 measured, not 42

`eq_movement.cpp` 13 + `eq_movement_state.cpp` 24 + `eq_movement_update.cpp` 6
= **43** `EverQuest::` bodies. The plan says 42; the miscount is the second
`StartCombatMovement` overload.

**Public (26)** — have an external caller: `Move`, `MoveToEntity`,
`MoveToEntityWithinRange`, `StartCombatMovement` (both), `Follow`, `StopFollow`,
`FaceEntity`, `MoveTo`, `StopMovement`, `UpdateMovement`, `SendPositionUpdate`,
`CalculateDistance`, `CalculateDistance2D`, `LoadPathfinder`, `SetMovementMode`,
`SetPositionState`, `Jump`, `UpdateWaterState`, `StopMoveForward`,
`StopMoveBackward`, `StopTurnLeft`, `StopTurnRight`,
`Test_SnapToMovementTargetForSeam`, plus the moved inline `SetPathfinding` /
`IsPathfindingEnabled` / `Test_GetLastComputedPositionUpdate`.

**Private (14)** — reached only from inside the family: `UpdateCombatMovement`,
`Face`, `CalculateHeading`, `FindPath`, `MoveToWithPath`, `FollowPath`,
`SendJump`, `SendMovementHistory`, `UpdateJump`, `OnEnterWater`, `OnExitWater`,
`GetSwimSpeed`, `GetMovementSpeed`, `UpdateKeyboardMovement`.

**Deleted (1):** `HasReachedDestination()` — **zero callers anywhere**, inside
the family or out (K03 correction a applied). 11 lines. §Mechanism 5. Its logic
is duplicated inline in `UpdateMovement()`'s arrival branch, which is what
actually runs. Boarded below as the reachability pattern's next instance.

**⚠ DEVIATION from §Mechanism 5 — the four `StartMove*`/`StartTurn*` have zero
callers and were KEPT.** §Mechanism 5 says delete outright. A 30-line note
directly above `StartMoveForward()` in `eq_movement_state.cpp`, written by
**T316**, is an explicit prior decision that they stay: the Start* -> four
`PlayerState` flags -> `UpdateMovement()`'s keyboard branch ->
`UpdateKeyboardMovement()` -> `SetPosLocal`/`FixZ`/`CheckZoneLine`/
`SendPositionUpdate` chain is a complete working movement path with no writer
for the flags, the Stop* half IS called (`EqActionHandler::stopAllMovement`),
and deleting the Start* half leaves a switch that can only be turned off. A
structural unit does not overturn a recorded product decision; they were made
**public** (the wiring T316 names lands on `EqActionHandler`, outside this
component). Reported, not absorbed.

### Members: the path-member decision

| member | refs | decision | evidence |
|---|---|---|---|
| `m_last_position_update_time` | 5, movement only | **moved** | |
| `m_test_last_sent_update` | 4, movement only | **moved**, with `struct TestSentPositionUpdate` and `Test_GetLastComputedPositionUpdate()` | K04 correction f |
| `m_use_pathfinding` | 4, movement only | **moved**, with `SetPathfinding()`/`IsPathfindingEnabled()` | K04 correction f |
| `m_water_surface_z` | 1, movement only | **moved** | it is WRITE-ONLY (`UpdateWaterState` assigns it, nothing reads it). Boarded |
| `m_current_path` (48), `m_current_path_index` (31), `m_pathfinder` (16), `m_force_next_position_update` (2) | movement + `eq_zone.cpp` | **moved** | the plan's preference. All five `eq_zone.cpp` sites are ZONE-LIFECYCLE RESETS, not reads of state: `RelocateWithinZone` clears the path and sets the force flag, `ZoneProcessZoneChange` clears the path, `CleanupZone` resets the pathfinder. They became three one-line public methods on the component — `ResetPath()`, `ResetPathfinder()`, `ForceNextPositionUpdate()` — exactly the `Movement().ResetPath()` shape the plan predicted for K09 |
| `m_movement_history`, `m_last_movement_history_send` | movement + `eq_zone.cpp` + `eq_zone_handshake.cpp` + `eq.cpp` | **stay**, `owner_.` | genuinely shared; K09 decides |
| `m_water_state` | movement + `eq_bridge.cpp` (3) | **stays**, `owner_.` | and so do its inline accessors `GetWaterState()`/`IsSwimming()` |
| `m_zone_bsp_tree`, `m_test_bypass_send_guard`, `m_pending_logout_disconnect`, `m_zone_out_acknowledged`, `m_zone_out_handshake_pending`, `m_zone_out_handshake_deadline` | shared | **stay**, `owner_.` | |
| **`m_navmesh_path`** | 1 movement (a read in `LoadPathfinder`), **3 in `eq_zone.cpp`**, plus the public `SetNavmeshPath()` with 4 external callers | **⚠ STAYS — deviates from the plan's "move" row** | §Mechanism 3's shared rule wins: it is more zone than movement (3:1), and moving it would drag `SetNavmeshPath()` and four call sites in `application.cpp` + three tests for one read |
| `m_update_running`, `m_update_thread`, `m_my_spawn_id`, `m_audio_manager` in the plan's "shared" row | — | `m_update_running`/`m_update_thread` appear in this family **only in comments**; `m_my_spawn_id` only inside a log FORMAT STRING; `m_audio_manager` is real and is `ctx_.audio_manager` | K04 correction g/h again |
| **`m_last_movement_update`** | **0 tree-wide** | **NOT moved; boarded dead** | §Mechanism 3's zero-reference rule (the `m_guild_name` case). Left in `eq.h` under a K07 comment |
| `m_swim_up` / `m_swim_down` + `SetSwimUp()`/`SetSwimDown()` | 0 readers, 0 setter callers | **untouched, boarded** | dead water-input state. Not in the plan's tables and not in a moved body, so K07 left them alone rather than widen scope |
| core | | `ctx_.` | `m_game_state` (220), `m_bridge` (12), `m_entities` (12), `m_zone_connection` (6), `m_combat_manager` (4), `m_skill_manager` (2), `m_character` (2), `m_zone_connection_manager` (1), `m_audio_manager` (1) |

**Eight members moved**, not the plan's nine — `m_navmesh_path` stayed.

### Predicted vs measured `eq.h` delta

| | lines |
|---|---|
| at dispatch (`fable` `c4d3ea97`) | **1,913** |
| **predicted** before any edit | **1,837** (−76) |
| **measured** after | **1,837** (**−76**) |

Prediction, computed line-by-line before starting: −43 declarations, −3 moved
inline accessors, −8 moved members, −28 banner/comment lines that go with them,
+6 scaffolding = −76. The plan file's pre-dispatch estimate was "−45 or better".

Breakdown of the −76 (82 deleted, 6 added):

| deleted from `eq.h` | lines |
|---|---|
| `Test_SnapToMovementTargetForSeam` decl + its 6-line T88 comment | 7 |
| `struct TestSentPositionUpdate` (6) + `Test_GetLastComputedPositionUpdate` (1) + its 5-line T88 comment | 12 |
| 10 decls from the "Public movement interface" block (`Move`…`FaceEntity`) | 10 |
| `UpdateMovement` + `SendPositionUpdate` decls | 2 |
| `SetPathfinding` + `IsPathfindingEnabled` + `HasReachedDestination` | 3 |
| `SetMovementMode`/`SetPositionState`, `SendJump`/`SendMovementHistory`/`Jump`/`UpdateJump` | 6 |
| `UpdateWaterState`/`OnEnterWater`/`OnExitWater`/`GetSwimSpeed` | 4 |
| `GetMovementSpeed` | 1 |
| "Keyboard control methods" banner + 9 decls + blank | 11 |
| "Movement methods" banner + 6 decls + blank | 8 |
| "Pathfinding methods" banner + 3 decls + blank | 5 |
| `m_test_last_sent_update` (its T88 comment was rewritten, net 0 on that line pair, +1 line) | 1 |
| `m_last_position_update_time` | 1 |
| "// Pathfinding" banner + `m_pathfinder` | 2 |
| `m_current_path` + `m_current_path_index` + blank | 3 |
| `m_use_pathfinding` | 1 |
| `m_force_next_position_update` + its 3-line T231 comment + blank | 5 |
| `m_water_surface_z` | 1 |
| **added** | |
| `#include "client/movement/movement_component.h"` | +1 |
| `Movement()` accessor + 2 comment lines | +3 |
| `friend class eqt::movement::MovementComponent;` | +1 |
| `std::unique_ptr<...> m_movement;` | +1 |
| (plus 2 net comment lines: the K07 board note on `m_last_movement_update`, and one extra line on the reworded T88 seam comment) | +2 |
| **net** | **−76** |

`scripts/refactor/file_size_allowlist.txt` lowered **1,913 → 1,837**.
⚠ **Shared file**: K05/K06 lower this same line. **Measured value for K07 alone
is 1837**; the orchestrator resolves at merge.

### Files

| file | lines |
|---|---|
| `include/client/eq.h` | 1,913 -> **1,837** (−76) |
| `src/client/eq_movement.cpp` -> `src/client/movement/movement_component.cpp` | 486 -> 492 |
| `src/client/eq_movement_state.cpp` -> `src/client/movement/movement_state.cpp` | 889 -> 896 |
| `src/client/eq_movement_update.cpp` -> `src/client/movement/movement_update.cpp` | 854 -> 850 |
| `include/client/eq_movement_internal.h` -> `include/client/movement/movement_internal.h` | 19 (byte-identical, pure rename) |
| `include/client/movement/movement_component.h` | 162 (new) |
| `tests/test_movement_component.cpp` | 92 (new) |

`grep tests/ scripts/ docs/` for `eq_movement` as a STRING before the move (K03
correction c): **8 hits, every one a prose comment**, no reader of the file.
`docs/module_map.md` and `refactor/BOARD.md` B001 each cite a moved path and
were repointed — `DocCitations` caught the BOARD one, which is exactly what it
is for.

### Multiset residual (§Mechanism 8)

Normalisation applied to both sides before `sort`, longest-name-first
(K04 correction i): `eqt::movement::MovementComponent::` -> `EverQuest::`;
`EverQuest::{s_debug_level,MovementHistoryEntry,DumpPacket,kZoneOutHandshakeTimeoutMs}`
-> unqualified; `ctx_.zone_connection_manager` BEFORE `ctx_.zone_connection`,
then `game_state`/`entities`/`bridge`/`combat_manager`/`skill_manager`/
`character`/`audio_manager`; `&owner_` -> `this`; `owner_.m_` -> `m_`;
`owner_.` -> ``; the internal-header include path; trailing whitespace.

**Result: ZERO code lines modified in all three TUs.** The entire residual is:

- `movement_component.cpp`: **+6, −0** — the 5-line K07 note and
  `#include "client/movement/movement_component.h"`.
- `movement_state.cpp`: **+7, −0** — the same six plus one blank line after the
  note.
- `movement_update.cpp`: **+6, −11** — the same six, minus the 11 lines of
  `HasReachedDestination()` (10 code/comment lines and one blank; a second blank
  in that block normalises away because it was a tab-only line).

No scaffolding, friend or accessor line appears in the moved TUs — those are all
in `eq.h` and `eq.cpp`, as K01 correction 7 says.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest -j6 --output-on-failure` | **3,534 / 3,536 passed**, 17 skipped. Failures: `AudioManagerTest.PreloadCommonSounds` (the known ALSA quirk) and `DocCitations`, which flagged the stale `refactor/BOARD.md:25` citation of `src/client/eq_movement_state.cpp`; repointed and **`DocCitations` re-run PASSES**. `FileSizeCeiling`, `DuplicateStateStores` and `ReachabilitySweep` **PASS unchanged**, no file-list edit needed |
| `test_movement_component` (new) | 6 tests, all pass |
| T234/T237 `test_eq_action_handler_transforms_real` | **pass, file unmodified** |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate | **not run, and correctly so**: `git diff --cached --name-only -M` matches nothing under `src/client/graphics` or `include/client/graphics`. It also ran inside the ctest above (`EntityViewerRegressionTest`, `UIAtlasPixelTest`, `ChatScrollPixelTest`, …) and passed; goldens untouched |
| `scripts/build-arm-noble.sh` | **not run**, same evidence |
| `scripts/refactor/check_file_sizes.sh --all` | **808 ok, 0 warn, 0 fail** (ratchet lowered to 1,837) |

⚠ One build hit the 600 s tool ceiling (exit 143). The T293/J07 scan found
**4 truncated test binaries** (`test_eq_action_handler_hotbar_command`,
`…_hotbar_item`, `…_tradeskill`, `test_guild_wire_rank`) behind valid ELF checks
on the other 211, plus three `Terminated` link targets and six stale `st*`
linker temp files in `build/lib`. Both archives passed `ar t`, and
`libwilleq_core.a` was PRESENT (the absence hole). All seven suspect binaries
and the temp files were deleted and rebuilt; the post-rebuild scan is
**224 binaries, bad=0**.

### Corrections to §Mechanism

1. **§Mechanism 4's third substitution must say "any `EverQuest` static OR
   NESTED TYPE".** K01 correction 2 covers `s_debug_level`. This family also
   needed `EverQuest::MovementHistoryEntry` (a private nested struct, used by
   `SendMovementHistory` — its storage `m_movement_history` stays behind),
   `EverQuest::DumpPacket` (static member function) and
   `EverQuest::kZoneOutHandshakeTimeoutMs` (`static constexpr int`). All three
   are the same edit class: friendship grants access, not scope.

2. **A FOURTH substitution is needed: `this` -> `&owner_` where a moved body
   uses `this` purely as an identity key.** `UpdateMovement` and
   `SendPositionUpdate` keep five function-static
   `std::map<EverQuest*, …>` throttle/stuck-detection tables keyed on `this`
   (20 sites). Left alone, `this` silently becomes a `MovementComponent*` and
   the map's declared key type stops matching. Rewriting the map type instead
   would change the key's identity; `&owner_` keeps the key **bit-for-bit the
   pointer it always was**, and it is the natural extension of the
   `Foo()` -> `owner_.Foo()` rule. This must be in §Mechanism 4 before any unit
   moves a body that caches per-instance state in a function static.

3. **§Mechanism 5's "delete a method with zero callers" needs an explicit
   carve-out for a RECORDED KEEP DECISION.** T316's 30-line note above
   `StartMoveForward()` says in terms why those four zero-caller methods stay.
   A rule that mechanically deletes them destroys a documented, deliberate
   state. The rule should read: zero callers -> delete, **unless the code
   carries a prior unit's written decision to keep it, in which case the unit
   reports it and moves on**.

4. **The plan-writing step "grep the family TU for `m_*`" must be
   comment-filtered and string-filtered, not just comment-filtered.**
   K04 correction g caught prose. K07 adds the string case: `m_my_spawn_id`'s
   only hit in this family is inside a `LOG_WARN` FORMAT STRING, and
   `m_update_running`/`m_update_thread` are comment-only. Three of the plan's
   nine "shared, use `owner_`" members needed no substitution at all.

5. **A method-count taken from `grep -c "EverQuest::"` misses overloads** —
   the plan's 42 vs the measured 43 is the second `StartCombatMovement`.
   Count definition lines, not names.

### Left for the board

- `EverQuest::HasReachedDestination()` deleted (zero callers anywhere; its
  logic is duplicated inline in `UpdateMovement`'s arrival branch). Noted as the
  reachability pattern's next instance, not boarded — §Mechanism 5 deletes.
- **`m_last_movement_update`** — zero references tree-wide. Left in `eq.h`
  under a K07 comment; a board B-item alongside K01's `m_guild_name`.
- **`m_water_surface_z`** — moved into the component but is **write-only**:
  `UpdateWaterState()` assigns the water surface Z and nothing ever reads it.
  Swim height therefore has no consumer.
- **`m_swim_up` / `m_swim_down` and `SetSwimUp()` / `SetSwimDown()`** — the
  setters have zero callers and the members have zero readers. The swim-input
  half of the water feature is unreachable, the same shape as the T316 keyboard
  cluster but without a decision recorded.
- **The four `StartMove*`/`StartTurn*` remain unwired** (T316's note lists what
  would wire them). K07 makes them public on the component, which removes the
  "they are private on a god class" half of the obstacle.
