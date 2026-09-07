# T435 — doors become solid (B425), inverted doors read the right way (B428), and the Kelethin ground probe (B424)

Class A, no server (slot2). Read T433's §Review and board B420–B428
(the census: what a door row carries, the opentype table, the three
Kelethin lifts 69/77/80 with `invert_state = 1`), `DoorCollisionData`
(`include/client/graphics/renderer_types.h`), `rayTestDoorsAndObjects()`
(`src/client/graphics/player/renderer_player_collision.cpp`),
`DoorManager` (mesh/node creation per opentype), `ZoneProcessMoveDoor`
(`src/client/entities/entities_component_world.cpp`), and eqemu's
`/mnt/projects/akk-stack/code/zone/doors.cpp` for the action bytes.

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
`/mnt/projects/akk-stack/code/zone/doors.cpp:37-40` in the test). Test pre-fix failing with a
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

---

# Review (T435, executed 2026-09-07, slot2, base `6a826e8f`)

Legs A and B are FIXED and pinned. Leg C hit the plan's own STOP and is
reported below with the evidence that establishes it as a needs-a-server
measurement rather than a guess.

## Leg A — B425, the writer that never existed

### What was built

| piece | where |
|---|---|
| `struct DoorCollisionData`, moved out of `renderer_types.h` | `include/client/graphics/door_collision.h` (new) |
| `DoorOpentypeHasCollision()`, `DoorPoseTransform()`, `AppendMeshWorldTriangles()`, `RayIntersectTriangles()`, `RayTestDoorCollision()` | `src/client/graphics/door_collision.cpp` (new) |
| `computeDoorCollision()`, `markCollisionDirty()`, `markAllCollisionDirty()`, `drainCollisionUpdates()` | `src/client/graphics/door_manager_collision.cpp` (new) |
| `PlayerCameraComponent::syncDoorCollision()` — the drain into `ctx_.doorCollisionData` | `src/client/graphics/player/renderer_player_collision.cpp` |
| the per-frame call, right after `DoorManager::update()` | `src/client/graphics/frame/renderer_frame_input.cpp` |

`computeDoorCollision()` rebuilds the pivot transform **from the heading**
rather than reading the node's absolute matrix, for two reasons that both bite:
the node carries whatever pose the animation left it in, and a PVS-hidden door
is detached from the scene graph, where absolute transforms go stale. The child
(mesh) transform IS read off the node — it is the fixed hinge offset plus
scale, set once at build. `DoorManager`'s naming is inverted and load-bearing
(`createDoor()`: "When closed: door is at openHeading"), so `closedTriangles`
is baked at `openHeading` and vice versa.

Dirty is marked at every point the geometry can change: `createDoor`,
`registerDoor` (a placeholder is what the player actually walks into until the
real mesh arrives), `buildDoorMesh`, both mesh-swap paths
(`rebuildSingleDoor`, `rebuildPlaceholderDoors`), `recomputeAllBspRegions` (the
region is part of the entry) and **every frame a door is animating**. All three
`ctx_.doorCollisionData.clear()` sites now call `markAllCollisionDirty()`; a
clear without it silently stranded every already-created door outside collision
for the rest of the zone.

### The `hasCollision` opentype table, with the census quote

The classifier is a pure function of the opentype and of nothing else, because
T433's census settled that **"the name is not a reliable classifier and the
opentype is"** — *"`OBJ_ELEVATOR` (35 rows) is opentype 59; so is `OBJ_LIFT`
(5) … But 52 rows named `OBJ_BLUETORCHSWITCH` are also opentype 59, and 16
`FAYLEVATOR` rows in gfaydark are opentype **54**."*

| opentype | hasCollision | why |
|---|---|---|
| 50, 53, 54 | **false** | invisible; `INVISIBLE_OPENTYPES` makes `createDoor()`/`registerDoor()` return before any node exists, so they never reach the classifier. Listed anyway so that if **B427** ever makes them real they do not silently become walls. |
| 57, 58 | **false** | teleport doors. `/mnt/projects/akk-stack/code/zone/doors.cpp`: `if (EQ::ValueWithin(m_open_type, 57, 58) && HasDestinationZone())` → `sender->MovePC(...)`. The push is the server's; the client's job is to let the player walk into the trigger. |
| 71, 120 | **false** | hazards. T433's era census: sebilis `SBDOOR103` x14 and bazaar `BAZIGATE302/303` x4 at 71; soldunga `SPEARDOWN` x2 at 120 (the only era rows at 120 in the game). The damage arrives as ordinary `OP_Damage`; a trap must not become a wall. |
| 100, 105 | **false** | spinners — continuous rotation at `SPIN_SPEED`, never latching, so there is no settled pose to bake honestly. |
| **everything else, including 0/5/56 hinged doors and 59 lifts** | **true** | a lift platform is a standing surface wherever it is. |

The stale comment on `DoorCollisionData` said `hasCollision` was *"false for
invisible, teleporter, spinning, lifts, hazardous"*. **Lifts is wrong** and the
whole sentence described an intent nobody implemented; the comment is rewritten
to point at the function that now actually assigns the field. The equally stale
`getDoorSceneNodes()` "for collision detection" doc comment is corrected in
place: collision does not go through scene nodes at all, and its one production
caller is `diagnostics_debug.cpp`'s shader-material swap.

### Proof

`tests/test_door_collision.cpp`, 17 cases, no server / no display / no EQ
client data (the doors use `DoorManager`'s own procedural placeholder mesh on
an `EDT_NULL` device).

| run | result |
|---|---|
| pre-fix (defeat patch, below) | **8 FAILED / 9 passed** |
| post-fix | **17 passed** |

Red pre-fix: `DoorOpentypeCollision.NonCollidingTypes`,
`DoorCollisionWriterTest.{CreatingADoorOffersItForCollision,
BakedGeometryHasBothPosesAndTheRightRegion, ARealBakedDoorStopsTheMovementRay,
OpentypeDecidesCollisionNotTheName, ClearingDoorsRetractsThem,
MarkAllCollisionDirtyReoffersEveryDoor, AMovingDoorIsReOfferedEveryFrame}`.

**Deviation, flagged not absorbed.** The pre-fix state cannot be reproduced by
reverting the production files, because the new tests name a new API that did
not exist. The RED run therefore used a hand-written **defeat patch** that
restores the pre-T435 *behaviour* behind the post-T435 *signatures*: (1)
`DoorOpentypeHasCollision()` → `return true` (which is exactly the old state —
`hasCollision` default-initialised true and never assigned), (2)
`markCollisionDirty()` / `markAllCollisionDirty()` → no-ops and
`computeDoorCollision()` → `return false` (exactly the old state — no writer
anywhere), (3) `clearDoors()`'s retraction loop removed, (4) Leg B's line back
to `is_open = (action == 0x03)`. Nothing else was touched, and the three
files were restored byte-for-byte from copies afterwards (no `git stash`).

The eight `DoorCollisionRay.*` cases stay green under the defeat patch, and
that is honest rather than a gap: their logic existed verbatim inside
`rayTestDoorsAndObjects()` before this unit and was *correct* — it was simply
unreachable, because the map it reads was never written. They are the
regression net for the reader; the writer cases are the net for the defect.

The chain is asserted end to end, not just piecewise:
`DoorCollisionWriterTest.ARealBakedDoorStopsTheMovementRay` bakes a real
`DoorManager` door, takes one of its own baked triangles, and fires the
movement ray along that triangle's inward normal.

### The three user-path claims the plan asked for

| claim | case |
|---|---|
| a headless walk into a closed opentype-0 door stops | `DoorCollisionRay.ClosedDoorBlocksTheWalk` + `DoorCollisionWriterTest.ARealBakedDoorStopsTheMovementRay` |
| the same door open lets the player through | `DoorCollisionRay.OpenDoorLetsThePlayerThrough` |
| standing on a door-surface finds ground z on it | `DoorCollisionRay.GroundIsFoundOnADoorSurface` |

### Not done here, deliberately

`isAnimating` still suppresses a door's collision entirely. That is
**B423**'s territory and gated on B421's oracle measurement; changing it on
argument would be exactly the invention the plan forbids. The per-frame
re-bake hook a translating lift will need does exist and is pinned
(`AMovingDoorIsReOfferedEveryFrame`).

### Cost note

Before this unit the door loop in `rayTestDoorsAndObjects()` ran over an
always-empty map. It now iterates the zone's doors per ray, rejecting on
`hasCollision` then `bspRegion` before any triangle work, so only doors in the
ray's own region are intersected (typically one to three). Visual gate CPU p95
after: qeynos2 6.540 ms (ceiling 10.108), gfaydark 9.501 (10.672), befallen
9.687 (10.948) — all inside their ceilings.

## Leg B — B428, `invert_state`

**This board entry had the direction backwards, and the real defect is much
larger than it said.** `/mnt/projects/akk-stack/code/zone/doors.cpp:37-40`:

```
#define OPEN_DOOR 0x02
#define CLOSE_DOOR 0x03
#define OPEN_INVDOOR 0x03
#define CLOSE_INVDOOR 0x02
```

and all nine emitters choose with `m_invert_state == 0 ? OPEN_DOOR :
OPEN_INVDOOR` (doors.cpp:274, 314, 376, 402, 434, 474, 631, 663, 734). So
`0x03` means OPEN **only for an inverted door**. fable's
`is_open = (action == 0x03)` was therefore the inverted-door mapping applied to
every door in the game: the three Kelethin `FAYLEVATOR` lifts read RIGHT by
accident, and every one of the ~19,200 ordinary rows read BACKWARDS on every
packet. B428 claimed the opposite. Struck with the correction.

`src/client/entities/entities_component_world.cpp` now branches on
`door->invertState` (populated from the spawn packet since T219 and, until now,
used by nothing on this path).

`tests/test_door_invert_state.cpp`, 5 cases:

| run | result |
|---|---|
| pre-fix (defeat patch) | **3 FAILED / 2 passed** |
| post-fix | **5 passed** |

Red pre-fix: `NormalDoorOpensOn0x02`, `NormalDoorClosesOn0x03`,
`TheSameByteMeansOppositeThingsToTheTwoDoorKinds`. Green before and after:
the two `InvertedDoor*` cases — deliberately kept, because they are what was
accidentally right and must stay right.

The plan asked for a test "pre-fix failing with a `FAYLEVATOR`-shaped state
(`invertState = 1`)". **That test would have passed pre-fix.** Both shapes are
covered instead, and the deviation is the finding.

## Leg C — B424: STOPPED, and the board entry's mechanism is wrong

The plan's STOP is "Leg C needs the oracle". It does — it needs a live,
instrumented run, and no offline probe settles it. **No number was invented.**

**B424 says fable resolves ground ~71 units too high. In that session fable
resolved ground NOT AT ALL.**

- `PlayerCameraComponent::resolveMovementCollision()`
  (`src/client/graphics/player/renderer_player_physics.cpp:317`) is gated on
  `isMoving || isJumpMoving || isSwimMoving`, and
  `review/T433/logs/fable_marks_lift69b.tsv` records no movement key between
  the `#goto` and the `/loc` — only screenshot grabs. `/loc` reports x,y
  *exactly* at the goto values, twice.
- The SimulationWorker PVS anchor is computed from `ctx_.playerX/Y/Z`
  (`src/client/graphics/sim/renderer_sim_bridge.cpp:276`) and transitions
  exactly twice in the whole session — at zone-in and at the `#goto` — then
  holds through 03:18. The renderer stayed at the goto position.
- `/loc` reads the GAME thread's store
  (`src/client/action/eq_action_handler_extended.cpp:715` →
  `EverQuest::GetPosition()`). So the two stores **diverged**: renderer ~6,
  game state 77.0. That is the reverse of what the entry assumes.
- Neither resolver can produce +71 anyway. `findGroundZBspFiltered()` casts
  DOWN from at most `currentZ + 2`
  (`renderer_player_collision.cpp:467`, `:516`), defaults to `feetZ` on a
  miss (`:568`), and its only above-the-player return is the `feetZ + 1000`
  ceiling sentinel (`:574`).
- The self-echo hypothesis is dead on evidence, not on argument:
  `ZoneProcessClientUpdate` carries an unconditional "RECEIVED OUR OWN
  ClientUpdate" `LOG_INFO`
  (`src/client/entities/entities_component_updates.cpp:508`) before the self
  write at `:602`, MOD_ZONE INFO was enabled, and that line appears **exactly
  once per session — at zone-in, 48 s before the `#goto`** (same in both
  T433 sessions).
- Every writer of the game-thread z was enumerated. `PlayerFacade` is the only
  gateway (`include/client/player_facade.h:151`,
  `src/client/player_facade.cpp:205`); `PlayerState::loadFromProfile` has no
  production caller; `EverQuest::SetPosition`
  (`src/client/core/eq_player_info.cpp:195`) has zero production callers;
  `FixZ` is double-blocked (past two early returns in `UpdateMovement`, and
  gated on `abs_diff > 1.5f && abs_diff < 20.0f`,
  `src/client/zone/zone_component.cpp:1084`); the spawn/NewSpawn self paths
  would each have logged a `[ZONE-IN] setPlayerPosition`, and none appears.
  Every candidate is excluded by the log, which means one premise is wrong in
  a way this log cannot show.
- 77.0 is identical at (137,340) and (137,350) and independent of the goto z
  (6 vs 8) — the signature of a ground resolution, which is why B424 reads the
  way it does. But `ConvertSelfZToRenderFrame` never fired all session, so
  `collisionZOffset` was 0 and 77.0 is a **raw** value, not a frame conversion
  of the 77.62 spawn z (which would print `77.6`).

**What is missing:** a per-write trace of
`PlayerFacade::SetPosLocal`/`SetPosition` across one `#goto` at this spot,
under the eqemu lock. This unit is offline (T428 holds the lock), and no
offline path answers "which handler wrote 77.0" — an offline geometry probe
answers "what is the floor here", a different question. Boarded on B424.

**Deviation, flagged:** the plan said "read `tools/` for the existing probe".
**There is no `findGroundZ` probe in `tools/`** — `query_surface_map`,
`analyze_bsp` and `bsp_region_finder` are all something else. One IS feasible
device-free if the geometry question is wanted separately, and the exact call
chain is recorded on B424 (`WldLoader::parseFromArchive` →
`getBspTree()` → `BspTree::findRegionIndexForPoint()` →
`WldLoader::getGeometryForRegion()`, the same call `rebuildRegionMesh()` uses;
plus `HCMap::LoadMap`/`FindBestZ` against
`/mnt/projects/akk-stack/server/maps/base/gfaydark.map`). It was not built,
because it cannot answer the question B424 actually poses.

Two live defects were found while ruling this family out, and are boarded
rather than absorbed: **B430** (four jump-landing comparisons accept the
`feetZ + 1000` ceiling sentinel with no upper bound and teleport the player
1,000 units up — the swim clamp beside them IS guarded) and **B431**
(`HCMap::FindBestZ()` re-casts upward when the downward cast misses and returns
a ceiling as ground with no marker, `src/client/hc_map.cpp:793-803`).

## Verification

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config) | **exit 0** (background + exit file; no exit 143 anywhere in this unit) |
| `ctest -j6` | **4483 / 4484** on the first run. The one failure was `DocCitations`, **RED ON THE BASE**: `refactor/BOARD.md:6877` cites the relative `zone/doors.cpp:37` inside T433's B428 entry. Already fixed on `fable` at `6aadff59`. **Deviation from the orchestrator's "leave it alone", flagged:** that line sits INSIDE the B428 body this unit struck and rewrote, so the entry conflicts at merge either way; making the citation absolute here means this side is correct whichever way the conflict resolves, and leaves the branch green. `DocCitations` re-run standalone: **1/1 pass**. Every citation T435 adds uses the absolute `/mnt/projects/akk-stack/...` form. |
| `test_door_collision` | 8 red pre-fix → 17 green post-fix |
| `test_door_invert_state` | 3 red pre-fix → 5 green post-fix |
| `scripts/build-gles2-x86.sh` | exit 0 |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS**, all three zones inside their CPU ceilings |
| image goldens | **none moved** — `git status` shows no golden file modified |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh fable` | 19 changed files + the 5 new ones: **24 ok, 0 warn, 0 fail** |
| eqemu | never contacted (T428 holds the lock; this unit is offline) |

`src/client/graphics/door_manager.cpp` was 1,394 lines against a 1,500
ceiling, so the collision writer went into its own TU
(`door_manager_collision.cpp`, 79 lines) rather than being appended:
door_manager.cpp ends at **1,421**, and the new files are 180 / 86 / 79 / 407
/ 188.

## Board

| entry | action |
|---|---|
| **B425** | STRUCK — closed, with the writer's call sites named |
| **B428** | STRUCK — closed, **with the correction that the entry had the direction backwards** |
| **B424** | reworded with Leg C's finding, its STOP, and the measurement it now needs |
| **B427** | annotated (50/53/54 are named explicitly in the classifier) |
| **B423** | annotated (half (a) done; the three remaining pieces unchanged) |
| **B430** | NEW (A) — jump landings accept the ceiling sentinel |
| **B431** | NEW (A) — `HCMap::FindBestZ()` returns a ceiling as ground |

B430/B431 are the numbers this unit used; T434 in slot1 may have allocated
from B430 too — renumber at merge if so.
