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

## Review (2026-08-25, slot1, branch task/T237-movement-mode-write from 10851ab2)

### Delivered

- `src/client/eq_movement.cpp` (+5): `SetMovementMode()` and
  `SetPositionState()` each write `m_game_state.player().setMovementMode/
  setPositionState(static_cast<eqt::state::...>(x))` right after the
  private member and BEFORE the `if (m_bridge)` publish -- T221's
  write-before-guard placement. Both enum pairs are value-identical
  (eq.h:551-564 vs player_state.h:15-28), so the cast is exact. The
  early-return on "no change" stays above the write: the source and the
  member are always updated together, so a no-op for one is a no-op for
  the other.
- `tests/test_eq_action_handler_transforms_real.cpp`: the two T234
  `DISABLED_` tests are enabled (comment rewritten from "defect, not
  fixed" to "fixed by T237"). 23/23, zero DISABLED left in that binary.
- `tests/test_game_state_source_writes.cpp` (+36): `Move_SetMovementMode_
  WritesSourcePlayerMovementMode` (Run->Walk->Sneak->Run) and
  `Move_SetPositionState_WritesSourcePlayerPositionState` (all five
  states, table-driven), each x {WithBridge, NoBridge}. 68/68 (was 64).

### Measured red / green (`review/T237/`)

| binary | before production edit | after |
|---|---|---|
| `test_game_state_source_writes --gtest_filter=*Move_Set*` | 4/4 FAIL (`red/source_writes_red.txt`) -- every read is `<00-00 00-00>` = Run/Standing | 68/68 full binary (`green/source_writes_green.txt`) |
| `test_eq_action_handler_transforms_real --gtest_filter=*WritesGameStatePlayerProjection` | 2/2 FAIL (`red/transforms_real_red.txt`) | 23/23 full binary (`green/transforms_real_green.txt`) |

The red was taken in the natural order (tests written and built first,
production untouched, run; then the 5 production lines added, rebuilt,
run) rather than by reverting after the fact, so no restore step was
needed. The NoBridge legs fail and pass identically to the WithBridge
legs, which is the proof the tests observe the source and that the writes
precede the guard.

### Server echo (plan step 3): NO write added, by design

`ZoneProcessSpawnAppearance` (eq_entities.cpp:681) does not special-case
our own spawn id for `AT_ANIMATION`: self is treated like any entity --
`m_entities[id].animation` is set and `EntityPoseStateChanged` /
`EntityAnimationEvent` are published for the renderer. It never writes
`m_position_state` either, so today the echo of our own sit/stand feeds
neither the legacy member nor the source; adding a GameState write there
alone would make `player().positionState()` diverge from
`m_position_state`, which is the opposite of the ONE-GameState rule.
Routing the echo through `SetPositionState()` is not an option as-is:
that setter SENDS `SendSpawnAppearance(AT_ANIMATION, ...)`, so the echo
would re-send and loop. The client-side setter IS the source of truth for
these two fields (the server merely echoes what we sent; `m_position_state`
is written only by the action handler, CombatManager sit/stand, and camp).
A server-FORCED position change (stun, death via `AT_DIE`, GM) reaching
self through this handler would be a separate, larger unit -- it needs a
non-sending internal setter and a decision on what `AT_DIE` for self means
for `POS_DEAD` -- and is out of this plan's scope. Noted, not done.
`AT_SNEAK` for self is likewise entity-map-only; the source write for
sneaking already exists in `SetSneak()` (eq.cpp:3893, Phase 7.8), the
sending side.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build slot1/build -j24` | exit 0 (`build_full.log`) |
| 2. ctest | **2848/2849**; sole failure `AudioManagerTest.PreloadCommonSounds` (ALSA, documented). Count is up from the 2745 baseline because fable advanced past T214 (T234 alone added 21) and this unit registers +4 cases and un-DISABLEs +2 (`ctest_full.txt`) |
| 3. `build-gles2-x86.sh` | exit 0 (`build_gles2_x86.log`) |
| 4. Visual gate | **PASS**, goldens untouched (`visual_gate.txt`, `git_status.txt`: only the three unit files) |
| 5. `build-arm-noble.sh` | exit 0 (`build_arm_noble.log`) |
| 6. `check_file_sizes.sh fable` | 3 files checked, 0 FAIL; the WARN on `eq_movement.cpp` (2043 > 1500) is pre-existing (2038 at fable, +5 here) |

### Territory

Only `src/client/eq_movement.cpp` and the two named test files. Nothing
in inventory/*, command_processor, eq_merchant (T236) or renderer_frame /
graphics_input_handler (T235).

### Deviations

None. Nothing unexpected.
