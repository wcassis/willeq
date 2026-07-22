# F08 — Split movement/pathfinding/water/keyboard into eq_movement.cpp

Move the movement core out of `src/client/eq.cpp` (9,569 lines). HIGH
entanglement but still a pure TU move. No behavior change, no interface
change, no reformatting.

## Scope (orig refs @ a0c02cd; enumerate by NAME)

Move to new `src/client/eq_movement.cpp`:
1. High-level movement (orig 10314–10627): `Move`, `MoveToEntity`, `Follow`,
   `StartCombatMovement`, `UpdateCombatMovement`.
2. Face/MoveTo/StopMovement/UpdateMovement (orig 10978–11498; UpdateMovement
   alone is ~447 lines).
3. SendPositionUpdate + movement-geometry helpers (orig 11499–11839):
   `SendPositionUpdate`, `CalculateHeading`, `CalculateDistance`,
   `HasReachedDestination`. NOTE: `FindEntityByName` / `ListEntities` in
   this range are entity-lookup helpers — LEAVE them for F11 (entities).
4. Pathfinding (orig 13334–13524): `LoadPathfinder`, `FindPath`,
   `MoveToWithPath`, `FollowPath` (the gap F09 documented).
5. MovementMode / PositionState (orig 13913–14003).
6. Jump / MovementHistory / UpdateJump (orig 14938–15238). Judgment calls in
   this range: `UpdateLoop` orchestrates more than movement — if it calls
   non-movement subsystems, LEAVE it in eq.cpp and document;
   `SafeQueueZonePacket` is a network helper — leave unless exclusively
   movement-used (document either way).
7. Water state (orig 15239–15394): `UpdateWaterState`, `OnEnterWater`,
   `OnExitWater`, `GetSwimSpeed`.
8. Keyboard movement (orig 17711–17923): `StartMove*`, `StopMove*`, `Turn*`,
   `UpdateKeyboardMovement`, `GetMovementSpeed`. NOTE: `QueuePacket` in this
   range is core networking — it STAYS in eq.cpp.

## Steps

1. Locate all clusters by name; produce the moved/left mapping in the review
   BEFORE moving. When in doubt, leave + document.
2. Create `src/client/eq_movement.cpp`: header comment + pruned includes
   (pathfinder, hc_map, bridge headers expected).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local static helpers: usual rule.
5. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_movement.cpp`
  — ~2,100 expected: WARN accepted, must not FAIL.
- eq.cpp shrinks by ~1,900+ lines.

## Review

**Located by name, not by orig line numbers.** eq.cpp had shifted substantially
from the plan's orig-a0c02cd estimates (F09/F11/F13 and others already landed
on `fable`). All clusters were located via `grep -n` for the named methods,
then verified against neighbors before moving. Base commit for this unit:
`233a2dfd524c93b7315473726ef17816389e963` (branched from `fable`, which had
already advanced to `aa158a4` by the time this unit finished — per
instructions, not rebased; orchestrator will integrate).

### Moved/left mapping (produced before moving)

1. **High-level movement** — eq.cpp lines 4658–4966 (308 lines, plan estimated
   313 for orig 10314–10627 — close match, confirms boundary). Moved:
   `Move`, `MoveToEntity`, `MoveToEntityWithinRange`, `Follow`, `StopFollow`,
   `StartCombatMovement(const std::string&, float)`, `UpdateCombatMovement`.
   `MoveToEntityWithinRange` and `StopFollow` are not named individually in
   the plan's enumeration, but sit contiguously between the named functions
   and the range-length match confirms they belong to this cluster — moved.
   Boundary confirmed: line 4967 is blank, line 4968 starts a
   "Movement State Accessors (Phase 7.1...)" banner section (`GetPosition`,
   `GetHeading`, `IsMoving`, `SetHeading`, `SetMoveSpeed`, `SetPosition`,
   `SetMoving`, `SetCombatStopDistance`, `IsAFK`, `IsAnonymous`, `IsRoleplay`,
   `IsCamping`) — not named in the plan, a GameState-sync accessor layer
   added in a later phase, not part of "movement implementation" — **left**
   in eq.cpp.

2. **Face/MoveTo/StopMovement/UpdateMovement** — eq.cpp lines 5066–5586 (520
   lines, exact match to plan's orig 10978–11498). Moved: `Face`,
   `FaceEntity`, `MoveTo`, `StopMovement`, `UpdateMovement`. `FaceEntity` is
   not named individually but the exact line-count match confirms it belongs
   (Face+FaceEntity+MoveTo+StopMovement ≈ 73 lines, UpdateMovement ≈ 447,
   totaling 520) — moved.

3. **SendPositionUpdate + geometry helpers** — eq.cpp lines 5587–5834.
   Moved: `SendPositionUpdate`, `CalculateHeading`, `CalculateDistance`,
   `CalculateDistance2D` (not named individually, but sits directly between
   `CalculateDistance` and `HasReachedDestination` — moved), and
   `HasReachedDestination`. `FindEntityByName` / `ListEntities`
   (lines 5836–onward) — **left** in eq.cpp per plan, for F11 (entities).

4. **Pathfinding** — eq.cpp lines 6539–6729 (191 lines incl. leading
   "// Pathfinding implementation" comment; plan's orig 13334–13524 = 190
   lines, exact match). Moved: `LoadPathfinder`, `FindPath`,
   `MoveToWithPath`, `FollowPath`.

5. **MovementMode / PositionState** — eq.cpp lines 6731–6820 (90 lines,
   matches plan's orig 13913–14003 = 90 lines exactly). Moved:
   `SetMovementMode`, `SetPositionState`.

6. **Jump / MovementHistory / UpdateJump** — moved non-contiguously:
   `SendJump` (7071–7087), `SendMovementHistory` (7089–7163), `Jump`
   (7234–7275), `UpdateJump` (7326–7370). Two judgment calls per the plan:
   - **`SafeQueueZonePacket`** (7165–7232, between `SendMovementHistory` and
     `Jump`): grepped all call sites — used by `SendPositionUpdate` and
     `SendMovementHistory` (both moved, movement-only) **and** by
     `eq_zone.cpp:194`. Not exclusively movement-used — **left** in eq.cpp
     per the plan's rule.
   - **`StartUpdateLoop` / `StopUpdateLoop`** (7277–7324, between `Jump` and
     `UpdateJump`): `StartUpdateLoop`'s thread body calls both
     `UpdateMovement()` **and** `m_combat_manager->Update()` — orchestrates
     more than movement — **left** in eq.cpp per the plan's rule.

7. **Water state** — eq.cpp lines 7372–7526 (155 lines; plan estimated 156
   for orig 15239–15394 — matches). Moved: `UpdateWaterState`,
   `OnEnterWater`, `OnExitWater`, `GetSwimSpeed`. (Lines 7527–7557, some
   empty "Phase 3/4" section-banner comments with no code under them, left
   in place — not movement content.)

8. **Keyboard movement** — moved non-contiguously: `GetMovementSpeed`
   (7598–7602) and, separately, `StartMoveForward` through
   `UpdateKeyboardMovement` (7661–7812, incl. leading "// Keyboard control
   methods" comment). Two items in the gap between them, both **left** in
   eq.cpp:
   - **`QueuePacket`** (7604–7627) — explicitly called out by the plan as
     core networking; stays.
   - **`StartCombatMovement(uint16_t entity_id)`** (7629–7659) — an overload
     of the `StartCombatMovement(const std::string&, float)` moved in
     cluster 1. Not named in the plan's enumeration for either cluster 1 or
     cluster 8, and not contiguous with either named range. Judgment call:
     left in eq.cpp rather than moved for API "completeness" with its sibling
     overload, per the plan's "no scope creep... when in doubt, leave"
     instruction. Flagged here for orchestrator review — a future cleanup
     unit may want to co-locate both `StartCombatMovement` overloads.

**Helper analysis (file-local statics / consts):** No file-local `static`
helper functions were needed. Eight file-scope `const float` constants
(`DEFAULT_RUN_SPEED`, `DEFAULT_WALK_SPEED`, `WALK_SPEED_THRESHOLD`,
`POSITION_UPDATE_INTERVAL_MS`, `FOLLOW_CLOSE_DISTANCE`, `FOLLOW_FAR_DISTANCE`,
`FOLLOW_MIN_SPEED_MULT`, `FOLLOW_MAX_SPEED_MULT`) at eq.cpp lines 81–91 have
C++ internal linkage (namespace-scope `const` without `extern`) and are used
exclusively by code being moved (verified via repo-wide grep — no other
`.cpp`/`.h` references them). Moved with the code per the "usual rule" for
file-local needs; left definitions in eq.cpp would have been invisible to
eq_movement.cpp regardless of linkage rules.

**Includes:** `client/eq.h`, `client/combat.h` (added after first build
failure — `CombatManager::IsAutoHunting()` needs the complete type;
`eq.h` only forward-declares `class CombatManager`), `client/pathfinder_interface.h`
(`IPathfinder`, `PathfinderOptions`), `client/pathfinder_nav_mesh.h` under
`#ifdef EQT_HAS_NAVMESH` (`PathfinderNavmesh` dynamic_cast in
`LoadPathfinder`), `client/skill/skill_manager.h` (`m_skill_manager->getSkillValue`
in `GetSwimSpeed`), `client/bridge/game_state_bridge.h` (`GameEvent` and the
various `*Data` event-payload structs), `client/graphics/eq/wld_loader.h`
under `#ifdef EQT_HAS_GRAPHICS` (pulls in `zone_bsp.h` for `BspTree`/
`RegionType` used unguarded in `UpdateWaterState` — matches the exact
precedent set by F09/`eq_zone.cpp` for the same BSP-tree dependency),
`client/audio/water_sounds.h` under `#ifdef WITH_AUDIO` (`WaterSoundIds::WaterIn`),
`common/logging.h`, plus standard `<algorithm> <chrono> <cmath> <cstring>
<ctime> <map> <tuple>` and the `#ifndef M_PI` guard (copied verbatim from
eq.cpp's own guard, since `M_PI` is used in `CalculateHeading` and
`UpdateKeyboardMovement`). One post-build fix was needed:
`client/combat.h` had to be added after the first build failed with
"invalid use of incomplete type `class CombatManager`" in
`UpdateCombatMovement`.

**Line counts:**
- `eq.cpp`: 9,569 → 7,708 lines (−1,861)
- `src/client/eq_movement.cpp`: 1,943 lines (new)

**CMakeLists.txt:** added `src/client/eq_movement.cpp` to
`CLIENT_CORE_SOURCES` immediately after `eq_zone.cpp` (before `eq_chat.cpp`).

### Verification results

- `cmake --build build -j24`: clean (0 errors after the `combat.h` include
  fix; pre-existing `-Wpointer-arith` warnings in unrelated eq.cpp code,
  unaffected by this unit).
- `ctest --test-dir build`: 1191/1192 run tests passed. The one failure,
  `AudioManagerTest.PreloadCommonSounds`, is the documented
  environment-dependent ALSA exception in `refactor/test_baseline.txt`
  (pre-existing, unrelated to Batch F). 9 tests skipped (asset-dependent,
  pre-existing, unrelated).
- `verify_move_only.py 233a2dfd..HEAD`: **PASS** (move-only modulo
  boilerplate: +66/-0 boilerplate lines, all `#include`/`#ifdef`/comment
  additions; `CMakeLists.txt` reported separately, not counted).
- `check_file_sizes.sh --files eq_movement.cpp`: **WARN** (1,943 lines >
  1,500 ceiling) — accepted per the plan's "~2,100 expected" note; not a
  FAIL.

### Deviations from the plan

- Two functions not individually named in the plan's enumeration
  (`MoveToEntityWithinRange`, `StopFollow` in cluster 1; `FaceEntity` in
  cluster 2; `CalculateDistance2D` in cluster 3) were moved because they sit
  contiguously within the plan's stated orig line ranges and the range
  lengths match almost exactly — treated as the plan's shorthand for "the
  named functions and their tightly-adjacent siblings," not an exhaustive
  exclusion list.
- `StartCombatMovement(uint16_t)` overload — left in eq.cpp, not moved, not
  named in the plan, not contiguous with any named cluster. Documented above
  for orchestrator review.
- `client/combat.h` include was not anticipated until the first build
  failure; added and rebuilt successfully.
