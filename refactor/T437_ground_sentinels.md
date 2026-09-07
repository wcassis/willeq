# T437 — the ceiling sentinel is not a floor (B435, B436)

Class A, slot2, no server (T428 holds the lock). Read B435 and B436 (the
T435 Leg C findings), `findGroundZBspFiltered()` and its `feetZ + 1000`
sentinel (`src/client/graphics/player/renderer_player_collision.cpp`),
the four jump-landing comparisons and the guarded swim clamps in
`renderer_player_physics.cpp`, `HCMap::FindBestZ()` (`src/client/hc_map.cpp`
~793–803) and its callers (`findGroundZ()`, game-side `GetBestZ`/`FixZ` in
`zone_component.cpp`), and T435's `RayTestDoorCollision` extraction as the
model for a device-free test.

1. B435: give the ground query a single honest answer shape — a
   "no floor / ceiling" result that no landing can mistake for ground
   (an optional/flag or `std::numeric_limits` marker with a named
   predicate; NOT another magic number). All four landings and the two
   swim clamps use the predicate; the `+100` heuristic guards go away if
   the shape makes them redundant (say so per site). Pre-fix failing
   test: a jump whose down-ray meets only a ceiling ends the jump on the
   original floor / falls, and never at feetZ+1000.
2. B436: `FindBestZ()`'s upward re-cast either returns a distinct
   "ceiling" marker the callers honour, or is deleted — read the two
   callers and pick by what they can do with a ceiling (a `FixZ` that
   writes a ceiling into the player position is the defect). Pre-fix
   failing test on `HCMap` with a hand-built triangle set: floor below →
   floor z; ceiling only → not-a-floor, and `FixZ` leaves the position.
3. The repro B435 owes (jump under low geometry) is a headless
   reproduction if a zone fixture with a ceiling exists in the test data
   (befallen is a dungeon: use the gate's zone fixtures); otherwise
   board the live repro.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
G05 in place if a pass changes, visual gate standalone + ARM (no image
golden should move; the CPU-p95 ceiling is an artefact under load —
re-run standalone), file sizes (`renderer_player_physics.cpp` is near
the ceiling — extract the landing logic if it grows). Strike B435/B436
with proof; new items B437+.

STOP: an image golden moves.
