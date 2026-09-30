# B951 — the collision map's remaining arrays (GitLab #269)

Branch `task/B951-collision-map-arrays` (fable `267e5a36` + this unit). Plan:
`refactor/B951_collision_map_arrays.md`. Evidence (NFS, not in the repo):
`/mnt/projects/fable/review/B951/` (`x86-before/`, `x86-after/`, `pi-ab/`,
`arm-identity/`). Follow-on to B941 (`refactor/B941/README.md`).

## What the collision map was made of (before, fable 267e5a36)

Per array, MB, from a harness over the real loader (the same bytes the client's
`gs.collision_map` owner row carries, `x86-before/`). `.map` = the file on disk
(zlib); payload = the inflated V2 payload the loader parses.

| zone | vertices | indices | leaf lists | nodes | marks | total | `.map` | payload | total / `.map` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| emeraldjungle | 4.40 | 7.42 | 3.77 | 1.68 | 0.62 | 17.88 | 3.52 | 11.21 | 5.1x |
| timorous | 1.63 | 3.09 | 2.30 | 2.02 | 0.26 | 9.31 | 1.88 | 5.05 | 4.9x |
| growthplane | 0.68 | 1.29 | 1.15 | 1.53 | 0.11 | 4.76 | 0.81 | 2.35 | 5.9x |
| gfaydark | 0.97 | 1.56 | 1.49 | 1.98 | 0.13 | 6.13 | 0.55 | 1.68 | 11.2x |

Why each array was that size (emeraldjungle: 366,338 vertices, 618,240
triangles, 46,579 nodes, 23,290 leaves):

- **indices**: 3 x uint32 per triangle for vertex numbers that need 19 bits.
- **leaf lists**: a uint32 count per leaf plus a uint32 per listed triangle.
  918,775 listings for 618,240 triangles -- a triangle that straddles a split
  goes into both children, so each triangle is listed by 1.49 leaves on
  average (timorous 2.13, growthplane 2.49, gfaydark 2.66); 432,221 triangles
  are in one leaf, 2,188 in eight or more.
- **nodes**: 36 bytes (left, right, 6 float bounds, leaf-list index). Every
  internal node has two children, and the build pool is pre-order: the left
  child is ALWAYS the next node (measured, 4 zones), so the left index is
  redundant.
- **marks**: one byte per triangle, written by every ray (see B953 below).
- **vertices**: exact float triples of the placements' transformed positions;
  365,803 distinct bit patterns of 366,338 -- nothing left to dedupe. The V2
  file stores emeraldjungle as 169,630 placements (9.4 MB of the 11.2 MB
  payload is placement records, ~55 bytes each) of 142 models; the loader has
  to materialise them in world space, because the ray test runs on world-space
  floats and every answer must stay bit-identical.
- 1,596 triangles in emeraldjungle are exactly degenerate (zero cross
  product) -- negligible, left alone.

## The fix (representation only; `src/client/raycast_mesh.cpp`)

The AABB tree is BUILT by the unchanged `NodeAABB` code (same splits, same
bounds, same leaf sets) and then re-laid-out:

1. **Triangles stored once, in leaf order.** The first leaf (pre-order) that
   lists a triangle owns it; a leaf is `[begin, end)` of the triangles it owns
   plus a range of references to triangles an earlier leaf owns. References:
   emeraldjungle 300,535 (was 918,775 list entries), 3 bytes each.
2. **A triangle is three uint16 offsets** from a base vertex shared by a block
   of 64 stored triangles (6 bytes, was 12). Vertices are renumbered in
   first-use order, so a block's vertices sit together; a triangle that uses a
   vertex below its block's base is escaped to three plain uint32
   (emeraldjungle 1,353 of 618,240; the other three zones 0).
3. **Nodes 36 -> 28 bytes**: exact float bounds + one word (right child index
   or leaf ordinal); two per-leaf range starts (8 bytes a leaf).
4. **No raycast marks.** A re-test of a triangle a previous leaf listed returns
   the same `t`, so it cannot change the nearest hit (a tie keeps the same
   location and distance; the only tie-dependent output is the hit NORMAL,
   which no `HCMap` caller asks for). `raycast()` is now `const`.
5. **Placeable flag** is one bit per (reordered) triangle.
6. `IR()` reads float bits through `memcpy`; the `(RmUint32&)x` cast was a
   strict-aliasing violation, and GCC warned `MaxT` "may be used
   uninitialized" once it inlined into the new traversal.

Tried on the Pi and **not shipped** (they made rays slower; B957 #275 puts the
trade to Will):

- child bounds derived from the parent's (73-81 % of child coordinates are the
  parent's own, 15-17 % its split midpoint, 3-11 % need storing): nodes to
  ~40 % (emeraldjungle 1.30 -> 0.50 MB, timorous 1.57 -> 0.49, gfaydark 1.54
  -> 0.54, growthplane 1.19 -> 0.42), but the per-node decode made cache-warm
  rays 11-31 % slower than fable (three decode forms tried);
- bit-packed per-triangle deltas (5 bytes instead of 6): emeraldjungle
  -0.37 MB, cache-warm rays +17 % against the 6-byte form;
- restoring per-triangle marks (as a test): slower, not faster.

## After

Per array, MB (`[raycastmesh]` line = the `gs.collision_map.*` owner rows,
`x86-after/`):

| zone | verts | tris | refs | nodes | leaves | flags | total | before | / `.map` | / payload |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| emeraldjungle | 4.40 | 3.76 | 0.90 | 1.30 | 0.19 | 0.08 | **10.63** | 17.88 | 3.0x | 0.95x |
| timorous | 1.63 | 1.56 | 0.87 | 1.57 | 0.22 | 0.03 | **5.89** | 9.31 | 3.1x | 1.17x |
| growthplane | 0.68 | 0.65 | 0.48 | 1.19 | 0.17 | 0.01 | **3.18** | 4.76 | 3.9x | 1.35x |
| gfaydark | 0.97 | 0.79 | 0.64 | 1.54 | 0.22 | 0.02 | **4.18** | 6.13 | 7.6x | 2.5x |

**The plan's bar "emeraldjungle well under 2x its `.map`" is NOT met** (3.0x).
The vertices alone are 1.25x the `.map`; see "Not verified" and B957.

### x86 `game_state` (rendered client, VM lane, `scripts/client_ledger_capture.sh --modes game_state`, MB at `ready`)

| zone | game_state before -> after | gs.collision_map rows before -> after | load peak before -> after |
|---|---|---|---|
| emeraldjungle | 34.91 -> **27.66** | 17.88 -> 10.63 | 61.0 -> 60.4 |
| timorous | 32.47 -> 32.18 | 9.31 -> 5.89 | 68.0 -> 64.6 |
| growthplane | 22.93 -> **21.33** | 4.76 -> 3.18 | 34.7 -> 33.2 |
| gfaydark | 11.38 -> **9.70** | 6.13 -> 4.18 | 21.0 -> 19.0 |

timorous's total hides its -3.42 MB: its `misc` row was 3.08 MB before and
6.20 MB after -- the B945 (#263) `GameEvent` staging buffer, whose capacity
depends on the zone-in burst (B941 measured 6.2 MB on the old code). The
navmesh rows are identical before/after. The owner TOTAL equals `game_state`
in every run (100 % attributed).

(`before` = this branch's `build/bin/willeq` with the four code files
reverted to fable, same rig, same session.)

### Ray micro-benchmark on the Pi (`review/B951/arm-identity/`)

Cross-built (`willeq-arm-noble` image, the ARM build's flags) against the OLD
(fable 267e5a36) and the NEW sources, run on the Pi. Median ns per query.

`bench` (the B941 benchmark: 200 k FindBestZ + 200 k short LOS at random
points over the whole zone, every query a cold path), 6 reps:

| zone | FindBestZ old -> new | LOS old -> new |
|---|---|---|
| emeraldjungle | 2,222 -> 1,152 (-48 %) | 1,891 -> 890 (-53 %) |
| timorous | 13,170 -> 10,598 (-20 %) | 4,796 -> 3,608 (-25 %) |
| growthplane | 1,552 -> 1,136 (-27 %) | 1,233 -> 900 (-27 %) |
| gfaydark | 2,529 -> 1,937 (-23 %) | 1,168 -> 789 (-32 %) |

`local` (new, `bench_local.cpp`: the same query kinds clustered +-40 units
around 50 anchor points, 4,000 each -- a player's own rays frame to frame,
cache-warm), 3 reps:

| zone | FindBestZ old -> new | LOS old -> new |
|---|---|---|
| emeraldjungle | 14,161 -> 12,739 (-10 %) | 16,010 -> 14,505 (-9 %) |
| timorous | 9,388 -> 7,830 (-17 %) | 6,200 -> 4,943 (-20 %) |
| growthplane | 7,438 -> 6,276 (-16 %) | 8,172 -> 7,089 (-13 %) |
| gfaydark | 8,120 -> 6,823 (-16 %) | 8,349 -> 7,346 (-12 %) |

### Orange Pi A/B

`scripts/pi/collision_ab.sh --ledger-owners game_state`, local lane,
uinput-driven W/S/D/Space, shipped LLVM-less Mesa prefix (`willeq-nollvm`),
2 rounds alternating A B / B A. A = fable `267e5a36` (`ci-arm/267e5a36.../`),
B = this branch `f578bc00` (CI job 1730, pipeline 370). Full table with ranges
and the per-leg owner rows: `review/B951/pi-ab/table.md`
(`scripts/pi/collision_ab_table.py --category game_state`).

| zone | game_state @ready A -> B | RssAnon @steady A -> B | owned (RssAnon - gpu_driver) @steady A -> B | owned - 64 MiB, B | moving frame us A / B | Player Movement us (moving) A / B | idle frame us A / B |
|---|---|---|---|---|---|---|---|
| emeraldjungle | 34.76 -> **27.51** | 96.0-97.1 -> 88.8 | 76.6-77.7 -> **69.4** | +2.3 | 15,537 / 15,882 (+2.2 %) | 257 / 256 | 16,076 / 12,348 |
| growthplane | 22.75 -> **21.17** | 89.6-90.1 -> 88.2-89.0 | 70.3-70.8 -> **68.9-69.6** | +1.8..+2.5 | 16,077 / 16,146 (+0.4 %) | 317 / 303 | 15,460 / 15,230 |
| gfaydark | 11.46 -> **9.46-9.52** | 85.1-85.6 -> 83.1-84.9 | 65.1-65.7 -> **63.1-64.9** | -4.0..-2.2 | 33,324 / 24,155 (-27.5 %) | 417 / 402 | 33,314 / 16,336 |
| timorous | no verdict | | | | | | |

MB (10^6 bytes) except the us columns. On the Pi the `gs.collision_map.*`
rows are byte-for-byte the x86 ones plus allocator rounding (emeraldjungle
verts 4,399,104, tris 3,765,896, nodes 1,304,232, refs 901,632, leaves
186,352, flags 77,304; the old single row was 17,887,240).

- **Player Movement (the collision phase) is not slower in any zone**
  (emeraldjungle 257 -> 256 us, growthplane 317 -> 303, gfaydark 417 -> 402).
- Frame time: emeraldjungle +2.2 % moving (within the 3 % bar) while its idle
  frame was 23 % FASTER and Player Movement equal -- the frame total moves with
  the render side, not with this code. gfaydark's -27.5 % is the client
  stepping between the 30 and 60 fps vsync rates (A's medians were 30.0 fps
  moving and idle, B's 41.4 moving / 61.2 idle); not claimed as a B951 effect.
- **timorous: no verdict.** All four legs (2 A, 2 B) failed to reach a moving
  window: zone connection lost / no `ready` -- the B925/B940 class, on both
  sides, before any B951 code matters. The x86 and ray-benchmark numbers
  above are the timorous evidence.
- gfaydark is now under 64 MiB owned on both sides; emeraldjungle's gap went
  +9.5..+10.6 -> +2.3 MB. (A's emeraldjungle owned is 76.6-77.7 MB, not
  B941's 83.5: fable moved on since -- B935's Mesa prefix and B943 among
  others.)

## Query identity

- `GameStateIdentity.*` (11 real zones + the synthetic V2 map): every
  **query digest unchanged** (4,000 FindBestZ + 4,000 CheckLOS/CheckLOSWithHit
  per zone, pinned from pre-B941 code). The triangle digest was
  order-dependent; triangles are now stored in leaf order, so it is re-derived
  as a **sorted multiset digest** (each triangle's 9 raw floats in their own
  winding order + its placeable bit, with multiplicity) and the new pins were
  computed with the pre-B951 code (fable 267e5a36): all 11 match. Everything a
  query or a consumer depends on is that set; the overlay and the manual-mode
  placeholder draw the same triangles in a different order.
- `SyntheticV2MatchesTheOldTriangleSoup` compares the sorted records against
  the old soup emission (was index by index) and 3,000 rays as before.
- **ARM (the Pi itself)**: the identity test cross-built against old and new
  sources and run on the Pi: **all 11 zones' digests identical old vs new**
  (`arm-identity/ident_{old,new}_print.txt`; ARM's bits differ from x86's for
  both, as in B941, and the synthetic test is x86-only for both).
- `tests/goldens/hc_map/*_rays.txt` (T504d ray-set goldens) untouched and
  passing.
- New: `RaycastMeshConcurrency.TwoThreadsGetTheSingleThreadedAnswers`
  (`tests/test_raycast.cpp`) -- two threads cast 20,000 rays through a
  `const RaycastMesh*`; every hit bit-identical to the single-threaded run.
  Against the pre-B951 `raycast_mesh.cpp` (with `-fpermissive` to get past the
  const) it FAILS 13 of 20 repeats: **B953 (#271)**, the render thread and the
  game thread share `HCMap` and the old marks were a data race.

## Issues filed

- **B953 (#271)** (A) HCMap raycast was not re-entrant (shared marks/frame
  counter between the render and game threads). Fixed here, with the test.
- **B957 (#275)** (C) the collision map is still ~3x its `.map`; the two
  measured levers each cost 10-30 % of ray CPU -- Will's call.

## Not verified

- **The 2x-`.map` bar is not met** (emeraldjungle 3.0x, 10.63 MB). Below it
  needs the two slower layouts above or a lossy representation (B957).
- timorous on the Pi: no leg of either side reached a moving window (zone
  link, B925/B940); its Pi evidence is the ray benchmark only.
- The live Player Movement phase is ~1-2 % of a Pi frame and moves by less
  than the leg-to-leg spread; the ray benchmark is the direct measure.
- Local `visual_gate.py` FAILED only on the CPU-p95 check (iceclad 11.97 /
  12.41 ms vs ceiling 9.53, timorous 25.10 vs 24.21 in the second run) with a
  loaded host; `gles2_zone_benchmark` never loads a collision map (no
  `HCMap::Load` call in `tools/hwcaps/`), so this code does not run in it. The
  CI gate on hermes-ext PASSED (pipeline 370). Memory checks all passed
  (iceclad memory-coverage slack 34.0 %).
- No local `scripts/build-arm-noble.sh` run: the ARM build is CI job 1730
  (pipeline 370, `ci-arm/f578bc00.../`), which the Pi A/B ran.
- V1 maps (6 files, none heavy) go through the same layout; only `sky` is in
  the identity set.
