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

(executor fills in)
