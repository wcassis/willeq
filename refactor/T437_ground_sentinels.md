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

---

# Review (T437, executed 2026-09-07, slot2, base `96a080d3`)

Both defects are FIXED and pinned. The plan's third item — the headless
reproduction — was possible for B436 and **not** possible for B435; that split
is stated below rather than absorbed.

## 1. The answer shape (B435)

`EQT::Graphics::GroundZ` in the new
`include/client/graphics/player/ground_query.h`: a two-member class with
`Floor(z)` / `NoFloor()` constructors and `hasFloor()` / `z()` / `zOr(fallback)`.

**Why a type and not a marker constant.** The plan allowed an optional, a flag
or a `numeric_limits` marker. A marker of any kind is still a float, and the
whole defect family is float-shaped: three different magic numbers travelled
this path (`feetZ + 1000.0f`, `BEST_Z_INVALID`, and — via `FindBestZ` — an
unmarked ceiling z) and each was mistaken for a floor by at least one caller.
A type with no conversion to float makes the compiler the enforcement: every
site that reads the answer must ask `hasFloor()` or say `zOr(...)` out loud.

**That is not a theoretical benefit — it caught a site the board entry missed.**
B435 named four landings and two swim clamps. The first build failed at
`renderer_player_physics.cpp:498`, `no match for operator-`: the ordinary
WALKING step-height path in `resolveIrrlichtClearPath()` also consumed the raw
value. Pre-T437 the sentinel made its `stepHeight` ≈ 1000, which fell through
to "step up too high" and blocked the move — the right outcome by arithmetic
accident. It now says so.

Two free functions carry the decisions, in a new device-free TU
`src/client/graphics/player/ground_query.cpp` (T435's `RayTestDoorCollision`
is the model):

```
bool ShouldLandJump(const GroundZ&, float feetZ, float verticalVelocity);
bool ShouldClampSwimToFloor(const GroundZ&, float feetZ);
```

They are four lines each. They exist because the comparisons they replace lived
inline in bodies that cannot be driven in a unit test — a live Irrlicht device,
an event receiver, a loaded zone with collision geometry, an entity renderer
and a bridge — which is precisely why B435 shipped with no regression net.

## 2. Every call site

All three ground queries now return `GroundZ`:
`findGroundZ()`, `findGroundZIrrlicht()`, `findGroundZBspFiltered()`
(`src/client/graphics/player/renderer_player_collision.cpp`).

| # | site (`renderer_player_physics.cpp`) | was | now | guard |
|---|---|---|---|---|
| 1 | `resolveIrrlichtClearPath()` jump landing | `feetZ <= groundZ && vel <= 0` | `ShouldLandJump(...)` | — |
| 2 | `resolveIrrlichtClearPath()` swim floor clamp | `groundZ < ctx_.playerZ + 100.0f && feetZ < groundZ` | `ShouldClampSwimToFloor(...)` | **`+100` heuristic REMOVED** |
| 3 | `resolveIrrlichtClearPath()` walking step height | `stepHeight = groundZ - currentFeetZ` | `groundZ.zOr(currentFeetZ)`, `if (groundZ.hasFloor() && ...)` | **guard ADDED** (compiler found it) |
| 4 | `resolveIrrlichtBlockedPath()` jump landing | `feetZ <= groundZ && vel <= 0` | `ShouldLandJump(...)` | — |
| 5 | `resolveIrrlichtBlockedPath()` swim floor clamp | `groundZ < ctx_.playerZ + 100.0f && feetZ < groundZ` | `ShouldClampSwimToFloor(...)` | **`+100` heuristic REMOVED** |
| 6 | `resolveIrrlichtBlockedPath()` wall slide X | `stepHeight <= stepHeight limit` | same + `groundZ.hasFloor() &&` | **guard ADDED** |
| 7 | `resolveIrrlichtBlockedPath()` wall slide Y | as above | as above | **guard ADDED** |
| 8 | `resolveHCMapCollision()` jump landing (clear) | `!= BEST_Z_INVALID && feetZ <= g && vel <= 0` | `ShouldLandJump(...)` | dead guard → live |
| 9 | `resolveHCMapCollision()` jump landing (blocked) | as above | `ShouldLandJump(...)` | dead guard → live |
| 10 | `resolveHCMapCollision()` swim clamp (clear) | `!= BEST_Z_INVALID && feetZ < g` | `ShouldClampSwimToFloor(...)` | dead guard → live |
| 11 | `resolveHCMapCollision()` swim clamp (blocked) | as above | `ShouldClampSwimToFloor(...)` | dead guard → live |
| 12 | `resolveHCMapCollision()` step height + wall slide X/Y | `!= BEST_Z_INVALID` | `hasFloor()`, `.z()`, `.zOr(currentFeetZ)` | dead guard → live |
| 13 | `irrlicht_renderer.cpp` NPC ground-snap callback | `findGroundZ(...)` | `.zOr(currentZ)` | behaviour **unchanged** by design |

**Why the two `+100` guards go away rather than staying.** They existed for one
reason — to keep `feetZ + 1000` out of the swim clamp — and a ceiling now
answers `hasFloor() == false`, which no clamp can read. Keeping them would also
keep their own bug: a GENUINE floor more than 100 units above the player (a
flooded shaft's bottom, seen from below) was rejected. That is asserted in
`GroundSentinelsT437.TheOldPlusHundredHeuristicWasBothLeakyAndWrong`.

**Why the `!= BEST_Z_INVALID` guards were DEAD.** `findGroundZ()` rewrote every
failure to the caller's own `currentZ` before returning, so `BEST_Z_INVALID`
never reached a caller. That was not harmless: the phantom floor sat at the
player's CENTRE z, and the landing then snapped to `centre + modelYOffset` —
lifting the player by one model offset per frame over a column the map has no
floor for. Those guards are now live, spelled `hasFloor()`. **This is a stated
behaviour change on the HCMap path**, not a refactor.

**One thing deliberately left alone**, flagged: `findGroundZBspFiltered()`
still answers `Floor(feetZ)` when its ray hits nothing at all, so a falling
player "lands" in mid-air over a hole. That is pre-existing, has no measurement
behind either answer, and changing it would have mixed a second behaviour
change into a unit scoped to the ceiling sentinel. Boarded as **B438**.

## 3. The FindBestZ decision (B436): DELETED

The plan said "a distinct ceiling marker the callers honour, or deleted — pick
by what they can do with a ceiling". Every caller was read:

| caller | what it does with the answer | what it could do with a ceiling |
|---|---|---|
| `ZoneComponent::GetBestZ()` → `FixZ()` (`src/client/zone/zone_component.cpp`) | `ctx_.self.SetPosLocal(... PosZ() + adjustment)` | nothing — writing a ceiling into the player position IS the defect |
| `PlayerCameraComponent::findGroundZ()` | feeds the landings and the swim clamp | nothing |
| `SimBoids` ×3 (`src/client/graphics/sim_boids.cpp`) | spawn / ground height for creatures | nothing |

Not one of the five could act on a ceiling, so a distinct marker would have had
**zero consumers** — the reachability defect this project has now hit ten
times. The upward re-cast is deleted; "nothing below" is `BEST_Z_INVALID`, and
every caller already handled that by leaving the position alone.

It is not a lost "player sunk under the floor" recovery: the downward cast
already starts at `start.z + 10.0f`, so any penetration under ten units is
resolved without the re-cast, and beyond ten units a silent snap is a teleport.

**Headless repro, real dungeon geometry.** `review/T437/befallen_ceiling_probe.cpp`
loads `/mnt/projects/akk-stack/server/maps/base/befallen.map` and scans a
60x60 grid: **1,709 of 3,600 columns** would have had a ceiling returned as
ground, one of them **48.16 units above** the query point. gfaydark: 3,038 of
3,600. Full output in `review/T437/befallen_ceiling_probe.txt`.

## 4. Was the headless ceiling repro possible?

**B436: yes** (above, real map data).
**B435: no**, and this is the plan's item 3 answered rather than skipped.
`findGroundZBspFiltered()` reads `ctx_.zoneBspTree` and
`ctx_.regionWorldTriangles`, which only the zone loader populates and only with
a live Irrlicht device; the comparison it feeds sits inside
`resolveIrrlichtClearPath()`, which additionally needs an event receiver, an
entity renderer and a bridge. The gate's zone fixtures do not help —
`gles2_zone_benchmark` never drives player movement. The live repro is boarded
as **B437**. This unit held no eqemu lock (T428 had it) and opened no
connection to the server.

## 5. Tests

`tests/test_ground_sentinels.cpp` (new, 264 lines), registered in
`tests/cmake/action_handlers.cmake`. Twelve cases in two suites: seven over the
answer shape and the two decisions, five over `HCMap::FindBestZ()` driven by
**hand-written V1 map files** (the format straight out of `HCMap::Load()`) —
floor below, ceiling only, floor + ceiling, empty column, plus `FixZ`'s own
arithmetic.

**Pre-fix red.** The pre-T437 tree had no device-free seam for B435 at all —
that absence is the defect's cause — so the red is produced by restoring the
DEFECT in place while keeping the scaffolding: `ShouldLandJump()` /
`ShouldClampSwimToFloor()` reverted to their unguarded comparisons against
`ground.zOr(feetZ + 1000.0f)`, and `FindBestZ()`'s upward re-cast put back
verbatim. Applied and reverted with a scripted patch, never `git stash`.

| run | result |
|---|---|
| `test_ground_sentinels`, defect restored | **5 FAILED, 7 passed** — `AJumpWhoseRayMeetsOnlyACeilingDoesNotLand`, `NoLandingEverPutsTheFeetAThousandUnitsUp`, `SwimDoesNotClampToACeiling`, `FindBestZT437.ACeilingOnlyIsNotAFloor`, `FindBestZT437.FixZLeavesThePositionWhenOnlyACeilingIsAbove` |
| `test_ground_sentinels`, fix in place | **12 / 12 passed** |

## 6. G06 — the movement golden learned the edit IN PLACE

The golden the branch moves is **G06** (`tests/goldens/movement_order_g06.txt`,
`tests/test_movement_order.cpp`), not G05 — G05 pins
`renderer_frame_render.cpp`, which this branch does not touch. No other order
golden covers a TU here (`irrlicht_renderer.cpp` has none).

The learning was done by diff, not by regeneration, and it was *verified* to be
one: the normalisation and composition rules in `test_movement_order.cpp` were
reimplemented and run against `fable:renderer_player_physics.cpp`, and the
result matched the checked-in golden **byte for byte, all 475 lines**, before
being run against the new TU. So the diff below is exactly this unit's edit and
nothing else.

- **475 → 473.** Two statements DELETED, both named in the `kGoldenSize`
  comment: the two `float maxReasonableGround = ctx_.playerZ + 100.0f;`
  declarations.
- **Zero insertions, zero reorderings.** Every other changed line is an
  in-place rewrite of the statement that was already at that position.
- The `kGoldenSize` comment names each class of rewrite and why.

## Verification

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config, background + exit file) | **exit 0** (first attempt exit 2 — the type rejected an unconverted site, see §1; no exit 143 anywhere in this unit) |
| `ctest --test-dir build -j6` | **4525 / 4525, 0 failed** |
| `test_ground_sentinels` | **5 red pre-fix → 12 green post-fix** |
| `test_movement_order` | 4 / 4 green after G06 learned the edit |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS** — qeynos2 5.528/10.108, gfaydark 5.935/10.672, befallen 8.819/10.948 |
| image goldens | **none moved** — `git status tests/goldens/` shows only `movement_order_g06.txt` |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh fable` | **0 fail**; `renderer_player_physics.cpp` 1,011 (it SHRANK by 2), `renderer_player_collision.cpp` 668, `hc_map.cpp` 987; new files 75 / 29 / 264 |
| eqemu | never contacted — T428 holds the lock, this unit is offline |

`renderer_player_physics.cpp` was named in the plan as near the ceiling. It is
1,011 lines against 1,500 and got smaller, so no extraction was needed there;
the new code went into its own TU regardless, because it had to be device-free.

## Board

| entry | action |
|---|---|
| **B435** | STRUCK — closed, with the call-site table and the extra site the compiler found |
| **B436** | STRUCK — closed by DELETION, with the per-caller reasoning and the headless measurement |
| **B437** | NEW (A) — B435's live repro, and why it cannot be taken headless |
| **B438** | NEW (B) — the BSP query answers "floor at your feet" on a total miss; deliberately unchanged here |

B437/B438 are the numbers this unit used. T436 in slot1 may have allocated from
B437 too — renumber at merge if so. (Note: `fable` has since advanced to
`d1458444` and carries a B444; this branch is based on `96a080d3` as dispatched.)

## Deviations

1. **The pre-fix red for B435 is produced by restoring the defect, not by
   running the new test against the old tree.** The old tree has no seam the
   test could call — that is the defect. Stated in §5 rather than presented as
   an ordinary before/after.
2. **`findGroundZ()`'s "no ground" answer changed from a phantom floor to
   NoFloor**, which makes four previously-dead `!= BEST_Z_INVALID` guards live
   on the HCMap path. This is wider than "remove the ceiling sentinel", and it
   is the honest reading of the plan's "a single honest answer shape". Called
   out in §2.
3. **A third `+100`-class site was found and fixed** (the walking step-height
   path) that B435 did not list. §1.
