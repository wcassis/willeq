# T435 — doors become solid (B425), inverted doors read the right way (B428), and the Kelethin ground probe (B424)

Class A, no server (slot2). Read T433's §Review and board B420–B428
(the census: what a door row carries, the opentype table, the three
Kelethin lifts 69/77/80 with `invert_state = 1`), `DoorCollisionData`
(`include/client/graphics/renderer_types.h`), `rayTestDoorsAndObjects()`
(`src/client/graphics/player/renderer_player_collision.cpp`),
`DoorManager` (mesh/node creation per opentype), `ZoneProcessMoveDoor`
(`src/client/entities/entities_component_world.cpp`), and eqemu's
`zone/doors.cpp` for the action bytes.

## Leg A — B425, the writer that never existed

1. Populate `ctx_.doorCollisionData` from `DoorManager` when a door's
   node is created, moved (open/close animation, every frame it moves)
   and removed — the door's CURRENT world-space geometry, so a closed door
   blocks and an open one does not, and a lift platform (opentype 59)
   is a standing surface wherever it is. Set `hasCollision` from the
   opentype per its own comment (false for teleporters, spinning 100/105,
   hazardous — quote the census), NOT by name (T433: the name is not a
   classifier).
2. `rayTestDoorsAndObjects()` already feeds walls and ground; prove the
   path a USER reaches: a headless walk into a closed opentype-0 door
   stops; the same door open lets the player through; standing on a
   door-surface finds ground z on it. Tests pre-fix failing.
3. The census read of `getDoorSceneNodes()` and the `hasCollision`
   comment become true or are deleted (stale doc = defect).

## Leg B — B428, `invert_state`

`is_open = (action == 0x03)` becomes the eqemu mapping: normal doors
OPEN 0x02/CLOSE 0x03, inverted doors OPEN 0x03/CLOSE 0x02 (quote
`zone/doors.cpp:37-40` in the test). Test pre-fix failing with a
`FAYLEVATOR`-shaped state (`invertState = 1`).

## Leg C — B424, the 71-unit ground error at the lift (measure, then fix if it is fable's)

T433 measured fable at `#goto 340 137 6` → z 77.0 where the official
client resolves z −0.09. Reproduce OFFLINE with the headless ground probe
(the T3xx `findGroundZ` tool / benchmark rig against gfaydark's BSP at
340,137 — read `tools/` for the existing probe) and say what surface
fable picks (the Kelethin platform above? a placeable? a door mesh?). If
the fix is local (a filter that already exists for placeables, a
top-down vs bottom-up ray choice), make it and pin it; if it needs the
oracle, board the measurement and STOP the leg. No number invented.

## Proof

Build in the background; `ctest -j6` (no expected reds on this tip —
the T430 pack is pinned); pre-fix/post-fix per leg; G05 in place if a
pass is added; visual gate standalone + ARM (no image golden should
move — a door is not in any golden frame; if one moves, STOP with the
sheet); file sizes (`renderer_player_collision.cpp` and `DoorManager`
are near the ceiling — extract, do not grow past 1,500). Board: strike
B425/B428 with proof; B424 per Leg C's outcome; new items B430+.

STOP: an image golden moves; Leg C needs the oracle.
