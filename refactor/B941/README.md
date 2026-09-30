# B941 — `game_state` memory: the collision map and the navmesh (GitLab #259)

Branch `task/B941-game-state-memory` (fable `0b84eef6` + this unit). Plan:
`refactor/B941_game_state_memory.md`. Evidence (NFS, not in the repo):
`/mnt/projects/fable/review/B941/` (README there lists every directory).

## What `game_state` was made of (before, owner rows at `ready`)

`game_state` is the game thread's default ledger category. B941 names its
owners at the four game-thread zone loads (`zone_entry.cpp`) with
`StartupScope` labels, printed by `WILLEQ_LEDGER_OWNERS=game_state`:
`gs.navmesh`, `gs.nav_query`, `gs.collision_map`, `gs.zone_lines`,
`gs.zone_sources`; everything else stays in `misc`.

x86 rendered client (VM lane, `scripts/client_ledger_capture.sh --modes
game_state`, the pre-B941 code + labels), MB at `ready`:

| zone | gs.collision_map | gs.navmesh | gs.nav_query | misc | total | `.map` on disk | `.nav` on disk |
|---|---:|---:|---:|---:|---:|---:|---:|
| emeraldjungle | 33.78 | 19.50 | 0.08 | 0.40 | 53.76 | 3.52 | 7.89 |
| timorous | 11.36 | 23.35 | 0.08 | 6.20 | 40.98 | 1.88 | 8.79 |
| growthplane | 5.32 | 20.52 | 0.08 | 0.52 | 26.44 | 0.81 | 8.34 |
| gfaydark | 7.75 | 5.47 | 0.08 | 0.68 | 13.98 | 0.55 | 2.33 |

Blow-up vs the file: emeraldjungle's collision map 9.6x its `.map`, the
navmesh 2.5x its `.nav` (the Detour tiles are the file's inflated payload).
The Pi (fable A legs of the A/B below) agrees to within 0.2 MB: emeraldjungle
53.60, timorous 35.25, growthplane 26.27, gfaydark 13.80.

`zone_lines` is 0-8 KB and `zone_sources` holds nothing at `ready` (its
parse is transient). `misc` is entity/event state; timorous's 6.2 MB is
mostly a 3.1 MB `GameEvent` staging buffer kept after the zone-in burst
(**B945, #263**, symbolised trace in `review/B941/x86-trace-timorous/`).

## The defects and the fixes (representation only)

1. **Collision map stored as triangle soup.** `HCMap::Load` (V2) gave every
   placed-object triangle three private vertices: emeraldjungle 549,559
   placeable triangles -> 1.65 M vertices, 20.2 MB of a 33.7 MB map. Now each
   placement transforms each model vertex it uses ONCE and indexes it, after
   canonicalising each model's vertex list by bits (source meshes repeat a
   position per texture/normal). Every vertex goes through the exact
   per-vertex transform the old loop applied, and the triangle order is
   unchanged, so every value and every query is identical.
   Vertices: emeraldjungle 1,683,062 -> 366,338; timorous 302,676 -> 136,084;
   growthplane 100,720 -> 56,858; gfaydark 213,221 -> 81,007.
   The placeable flag `vector<bool>` is a `[begin, end)` range (placeables
   are one contiguous run).
2. **Collision map load peak 2.4x live** (emeraldjungle 127 MB peak for
   53 MB): the compressed + inflated buffers lived through the whole build,
   the arrays grew by doubling, then `createRaycastMesh` copied both. Now the
   compressed bytes die after the inflate, the inflated payload after the
   parse, the arrays are MOVED into the mesh (new vector factory) and trimmed
   after the tree build, and the tree's per-node lists are plain vectors (they
   were `posix_memalign(4096)` each for a KSM merge that never applies;
   `ksm.h` had no user left and is deleted).
3. **Navmesh load peak = compressed + inflated + a copy of every tile**
   (emeraldjungle 46.7 MB for 19.5 MB kept). Now `NavmeshStreamReader`
   inflates each tile straight into its Detour blob (`navmesh_load.cpp`).
4. **Navmesh `dtLink` capacity never trimmed.** Detour sizes each tile's link
   array for the worst case and hands links out from a free list in index
   order; with no tile ever removed, the used links are a prefix. After the
   last `addTile` each tile's unused tail is cut out and the later sections
   moved down (no link index changes). Links used: emeraldjungle 279,006 of
   524,805 (-2.95 MB), timorous 333,287 of 612,033 (-3.34 MB), growthplane
   263,535 of 509,306 (-2.95 MB), overthere 350,078 of 839,359 (-5.87 MB).
   A tile whose free list is not a clean tail is left alone (none were).
5. **Refused navmesh tiles leaked.** `addTile` refuses a second tile at an
   occupied grid cell and the blob stays the caller's; the old load ignored
   the status. overthere: 90 of 16,384 tiles, 4.26 MB, leaked for the zone's
   lifetime. Now freed (the navmesh never contained them).
6. **B942 (#260)**: `FindPath` told Detour its 256-entry stack arrays held
   2048 straight-path points. Fixed (capacity = the arrays' size).

## Query identity

`tests/test_game_state_identity.cpp` (ctest `GameStateIdentity.*`):

- 11 real zones (befallen, gfaydark, iceclad, emeraldjungle, wakening,
  timorous, growthplane, overthere, skyfire, warslikswood, sky[V1]): FNV-1a
  digests over the raw bits of every collision triangle (index order,
  placeable flag), 4000 `FindBestZ` + 4000 `CheckLOS`/`CheckLOSWithHit`, every
  navmesh detail triangle, 1500 smoothed `FindPath` (the client's /follow
  options) + 500 `FindRoute`. Digests pinned from the pre-B941 code: **all
  11 identical, bit for bit** (x86). The B942 capacity fix leaves them
  unchanged (no sampled path reached 256 points). A zone whose `.map` is
  absent skips; a host with the maps but no `.nav` (the CI runner, measured
  in pipeline 358) checks the collision map half and says the navmesh half
  was not checked.
- `SyntheticV2MatchesTheOldTriangleSoup` (always runs): a synthetic V2 map
  (duplicated model positions, +0/-0, invisible polys, a missing model, a
  placement group, flat + bumpy tiles) against an in-test copy of the OLD
  soup emission: every triangle bit-identical, 3000 ground + LOS rays
  identical against a reference tree built from the soup.
- **ARM (the Pi itself)**: the identity test cross-built against the OLD and
  the NEW sources and run on the Pi (`review/B941/arm-identity/`): **all 11
  zones' digests identical old vs new**. ARM's bits differ from x86's for the
  old code too (VFPv4 fused multiply-add), so the committed x86 goldens do
  not apply on ARM and the synthetic test's in-file reference is x86-only.

## Measurements after

### x86 (rendered, VM lane, same rig; MB at `ready`)

| zone | game_state before -> after | collision_map | navmesh | load peak before -> after |
|---|---|---|---|---|
| emeraldjungle | 53.76 -> **34.91** (-18.85) | 33.78 -> 17.88 | 19.50 -> 16.55 | 127.1 -> 61.0 |
| timorous | 40.98 -> **35.64** (-5.34) | 11.36 -> 9.31 | 23.35 -> 20.00 | 73.4 -> 68.0 |
| growthplane | 26.44 -> **22.90** (-3.54) | 5.32 -> 4.76 | 20.52 -> 17.57 | 49.4 -> 34.7 |
| gfaydark | 13.98 -> **11.64** (-2.34) | 7.75 -> 6.13 | 5.47 -> 4.75 | 27.0 -> 20.9 |

(x86 RssAnon/owned are dominated by llvmpipe and are not a 64 MiB reading;
`steady_60s` RssAnon on x86 blew past 1 GB in two runs, the known B924
llvmpipe growth.)

### Orange Pi A/B (`scripts/pi/collision_ab.sh --ledger-owners game_state`,
local lane, uinput-driven W/S/D/Space, A = fable 0b84eef6, B = 01b059fe;
`review/B941/pi-ab/table.md`)

| zone | game_state @ready A -> B | RssAnon @steady A -> B | owned (RssAnon - gpu_driver) @steady A -> B | owned - 64 MiB B | moving frame us A / B | idle frame us A / B |
|---|---|---|---|---|---|---|
| emeraldjungle | 53.60 -> **34.76** | 121.8 -> 103.0 | 102.4 -> **83.5** | +16.4 | 16,094 / 16,162 (+0.42 %) | 15,836 / 16,016 |
| growthplane | 26.27 -> **22.74** | 100.2-100.8 -> 91.8-94.8 | 80.8-81.4 -> **72.6-75.6** | +5.5..+8.5 | 16,113 / 16,059 (-0.34 %) | 16,244 / 16,136 |
| gfaydark | 13.80 -> **11.47** | 95.5-97.0 -> 92.8-93.0 | 75.4-76.9 -> **72.8-73.0** | +5.7..+5.9 | 33,302 / 33,308 (+0.02 %) | 33,315 / 33,312 |
| timorous | 35.25 -> **30.47** | 139.1 -> 138.3 | 119.1 -> 118.3 | +51.2 | 33,316 / 33,319 (+0.01 %) | 33,318 / 33,321 |

Two legs per side (emeraldjungle, growthplane, gfaydark); timorous ONE leg per
side: in 6 further timorous attempts (2 in this A/B, 4 in a timorous-only
re-run `review/B941/pi-ab-timorous/`) BOTH sides failed to reach `ready`
(zone connection lost / zone port silent before any B941 code runs -- the
B925/B940 class). Pi game-thread load times (A -> B, one leg): navmesh
emeraldjungle 1.40 -> 1.15 s, timorous 1.95 -> 0.96 s; map 5.92 -> 4.86 s,
2.61 -> 2.06 s.

**Player Movement phase (collision rays), Pi, median us:** emeraldjungle
248 -> 268, growthplane 304 -> 315, gfaydark 424 -> 413, timorous (one leg
each) 821 -> 1,986. The frame totals are unchanged (vsync-bound 30/60 fps).
Raycast microbenchmark (200 k FindBestZ + 200 k short LOS, old vs new code,
`review/B941/arm-identity/bench.cpp`): **on the Pi new is 1-3 % faster**
(timorous FindBestZ 13.4 -> 13.2 us, LOS 4.85 -> 4.80 us; emeraldjungle 2.28
-> 2.22 us, 1.90 -> 1.89 us); on x86 timorous +2-5 %, emeraldjungle faster.
So the one-leg timorous Player Movement gap is not the collision map's ray
cost.

### Live pathing (x86, VM lane, gfaydark, `review/B941/x86-path-{before,after}/`)

Tour: `/loc`, `/move -173.4 -9.2 -3.7` (MoveToWithPath -> navmesh FindPath),
wait 30 s, `/loc`. Before: "Found path with 16 waypoints", moved (-20.0,
10.0) -> (-169.2, -8.7). After: "Found path with 16 waypoints", (-20.0, 10.0)
-> (-168.8, -8.6). No "Pathfinding failed" on either side.

## Not verified

- **Pi Player Movement in timorous**: one leg per side (821 vs 1,986 us); the
  zone would not reach `ready` in 8 further attempts on either side. The Pi
  ray micro-benchmark says the collision map's rays are not slower, so the
  live gap is unexplained (where the player walked, entities in range), not
  attributed. The other three
  zones are within +20 us (<= 0.12 % of a frame). Frame time is within 0.5 %
  everywhere measured.
- ARM identity is proven by a one-off cross-build run on the Pi, not by a
  ctest: no ARM test lane exists (the sysroot has no gtest).
- The x86 `gate` CPU-p95 check for iceclad failed once under load (other runs
  on the host) and passed standalone; not a memory result.
- No V1-map vertex dedup (6 V1 files exist, none a heavy zone).
- Remaining collision-map representation (emeraldjungle after: vertices
  4.4 MB, indices 7.4 MB, leaf refs 3.8 MB, nodes 1.7 MB, marks 0.6 MB) and
  the Detour tile format itself (polys, verts, detail meshes) are unchanged.
