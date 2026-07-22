# F19 — Split player movement/collision and raycasting out of irrlicht_renderer.cpp

Move the player-physics clusters out of
`src/client/graphics/irrlicht_renderer.cpp` (12,340 lines). Pure move — no
behavior change, no interface change, no reformatting.

## Scope (orig refs @ a0c02cd; heavily shifted by F14–F16/F21. Enumerate by NAME.)

Move to new `src/client/graphics/renderer_player_physics.cpp`:
1. **Player movement/collision cluster** (orig 11835–12991):
   `updatePlayerMovement` (~828 lines), `checkMovementCollision`,
   `findGroundZ`, collision-setup methods in that contiguous region.
2. **Collision raycasting subsystem** (orig 13447–14053; F15's review lists
   these left-behind): `checkCollisionIrrlicht`, `findGroundZIrrlicht`,
   `extractWorldTriangles`, `rayIntersectTriangles`,
   `rayTestDoorsAndObjects`, `checkCollisionBspFiltered`,
   `findGroundZBspFiltered`, `addCollisionDebugLine`,
   `drawCollisionDebugLines`, `clearCollisionDebugLines`.

In-range non-physics methods (judgment): leave + document.

## Steps

1. Locate both clusters by grep (watch return-type regex traps); produce the
   mapping in the review BEFORE moving.
2. Replicate namespace wrapper + include patterns (F14–F16/F21 reviews).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local statics: usual rule.
5. Add to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/renderer_player_physics.cpp`
  — ~1,800 expected: WARN accepted, must not FAIL.
- irrlicht_renderer.cpp shrinks by ~1,700+ lines.

## Review

(executor fills in)
