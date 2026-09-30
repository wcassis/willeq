# B909b: placed-object collision holds each mesh once

GitLab #229 (the rest of B909). Branch `task/B909b-object-tris`. It started
from `fable` @ `f2bb6aad` and was merged with `fable` @ `58ff848c` (B933).
Plan: `refactor/B909b_object_tris.md`. Measured 2026-09-30 on `dev-u24`
(x86, llvmpipe) and on the Orange Pi. eqemu lane: the VM (`172.16.200.99`),
lock owner `b909b`. Logs and raw output are under
`/mnt/projects/fable/review/B909b/` (NFS).

## Step 1: how the collision triangles were stored

`ZoneLoadComponent::loadZoneStep9_Collision()` ran an Irrlicht triangle
selector over every placed-object node. It appended the world-space triangles
to `std::map<size_t, std::vector<triangle3df>>`, one entry per BSP region, at
36 B per triangle per placement. The ledger bytes per triangle are higher
because the vectors grow by doubling. The x86 figures come from the client's
own step 9 log lines: the old "N objects -> M world-space triangles" line
(fable binary) and the new sharing census (branch binary), in the same four
rendered runs.

| zone | placements | world triangles in the old copy | triangles per placement | `sp.object_tris` (ledger) | ledger B per triangle | `IMesh`es | vertex-animated placements / geometries | most placements of one geometry |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| gfaydark | 2,829 | 111,012 | 39.2 | 5,257,648 | 47.4 | 80 | 3 / 2 (flags) | NEKPINE1 ×1,247 (36 tris), NEKPINE2 ×1,068 |
| growthplane | 4,333 | 814,110 | 187.9 | 40,212,336 | 49.4 | 4,321 | **4,314 / 4** | CMPLANT102 ×1,779, CMPLANT101 ×1,443, DRGRASS102 ×589, DRGRASS101 ×503 |
| trakanon | 2,758 | 522,449 | 189.4 | 25,431,600 | 48.7 | 2,420 | **2,368 / 5** | CMPLANT102 ×914, JNPLANT101 ×764, CMPLANT101 ×682; static: TRTSTONE101 ×44 |
| overthere | 2,879 | 320,954 | 111.5 | 15,302,960 | 47.7 | 2,542 | **2,487 / 9** | OTPLANT101B ×749, OTPLANT103B ×747; static: OVERROCK103 ×61 |

**The hypothesis was half right.** Every placement did get its own
world-space copy of a mesh that other placements also use. In gfaydark that is
the whole story: 2,829 placements of 80 shared meshes. In the Kunark and
Velious zones, 86–99 % of the placements are **vertex-animated** grass and
plants. T504 deliberately gives each of those a private `IMesh`, because their
vertices are rewritten every frame. growthplane has 4,314 private meshes of
just 4 geometries.

## The fix: `ObjectCollisionSet`

`include/client/graphics/object_collision_set.h`,
`src/client/graphics/object_collision_set.cpp`. The set holds:

- **Each distinct static mesh once, read in place.** It stores index runs
  into the mesh the draw path already holds. Those arrays stay charged to
  `objects`, so the block has two claimants (object drawing and object
  collision) and is counted once. The set `grab()`s each mesh so the mesh
  outlives any node that drops it. The step 9 log line names the shared bytes
  and both claimants.
- **Vertex-animated meshes as a snapshot shared by content.** The snapshot
  is taken at the moment the old copy was taken, so collision keeps the frozen
  load-time pose it always had. Every placement of one animated geometry has
  the same load-time bytes, so snapshots are compared by FNV-1a plus `memcmp`.
  growthplane's 4,314 animated placements share 4 snapshots.
- **Per placement:** the selector's own world matrix
  (`SelectorWorldMatrix()`: identity, then `*=` the node's absolute
  transformation, exactly as `CTriangleSelector::getTriangles()` builds it),
  an inflated world AABB and a mesh slot. Placements are sorted by region
  (a stable sort, so placement order is kept) with a flat (region, first) index.

A ray against a region first skips each placement whose box the segment
[0, closestT] cannot reach. It transforms the remaining placements' vertices
with the same `matrix4::transformVect(out, in)` the selector used. Then it runs
the same Moller–Trumbore step, now shared in `moller_trumbore.h` with the door
and region sets. The single `closestT` runs across the whole region, as the old
single loop over the region's concatenated vector did. The only reader is
`PlayerCameraComponent::rayTestDoorsAndObjects()` (movement collision and both
ground phases).

`sp.region_bounds` was **not** changed. It has readers in eight TUs, a
camera-controller pointer to the map, and a frame-render order golden. After
this unit it is at most 0.48 MB on x86 and 0.21 MB on the Pi in the measured
zones, so it no longer holds `spatial` over budget.

## Identical rays

`tests/test_object_collision_set.cpp` (8 tests). The fixture is an EDT_NULL
scene: 900 placements of 5 shared meshes, including an atlased
`S3DVertex2TCoords` mesh and an exactly horizontal table top, plus private
"animated" meshes of 2 geometries. It uses Y-only and full rotations,
non-uniform scale, 9 regions plus `SIZE_MAX`. The OLD copy is built with the
production `PlayerCameraComponent::extractWorldTriangles()` over the same
scene nodes.

- `QueryTimeTransformReproducesTheSelectorBitForBit`: more than 10,000
  triangles, transformed at query time, equal the selector's output bit for bit.
- `EveryRayGivesBitIdenticalResultsToTheOldCopy`: 13,200 rays (4,000 random,
  4,000 straight-down ground rays at both phase lengths, 4,000 short movement
  rays through placements, 1,200 rays lying in the plane of horizontal table
  tops). About 4,600 of them hit, 453 of those are grazing hits. Hit or miss,
  hit point, hit triangle and distance are equal to the old copy's **bit for
  bit: 0 differences**.
- `ATieGoesToTheInstancePlacedFirst`: equal-t hits on two placements go to
  the one placed first, as in the old loop.
- `AnimatedMeshIsSnapshottedAtBuild`, `HoldsEachMeshOnceNotACopyPerInstance`
  (owned bytes bounded per placement, and under a quarter of the old copy),
  `SameRegionsAndTrianglesAsTheOldCopy`, `EmptyAndClear`, and the census
  `Step9BuildsTheSetAndMakesNoPerInstanceCopy`.

**RED**, each run separately against the test binary
(`review/B909b/red/*.txt`):

| perturbation | result |
|---|---|
| swap two vertices in the ray loop | `EveryRay…` fails in all four ray kinds |
| boxes shrunk instead of inflated | `EveryRay…` fails (down, move, random) |
| reverse the placement order within a region | `ATieGoes…` fails |
| wrong initial `closestT` | `EveryRay…` and `EmptyAndClear` fail |

**A defect the identity test caught in the first version.** A snapshot's run
cached `snapshot.data()`. `Mesh` is copied rather than moved when `meshes_`
grows, because `irr::core::vector3d` has a user-declared copy constructor and
so no noexcept move, and that left a dangling pointer. 18–48 of 13,200 rays
differed. The run now resolves the snapshot at query time.

The ray identity is proven on x86. On the Pi (armv7, `-mfpu=neon-vfpv4`, GCC's
default `-ffp-contract=fast`) the compiler may fuse multiply-adds differently in
the selector's TU and in this one. Any difference would be at float rounding
level of a vertex position. It was not measured, because the old path is not in
the new binary.

## x86: the client's `spatial`, before and after

`scripts/client_ledger_capture.sh --zones growthplane,gfaydark,trakanon,overthere
--modes spatial --char-level 60 --char-exp 620000000` (the eligibility leg is
new in this unit: growthplane kicks a character under level 46). One rendered
run per zone and side, orangepi preset, 1280x720, `WILLEQ_BASELINE=1`. Before:
fable `f2bb6aad` (`review/B909b/bin/fable_f2bb6aad/willeq`, sha256
`362d95fb…bac3`). After: branch `ad5b4c68` (`bin/b909b_ad5b4c68/willeq`,
`3b382188…961e`). B933 is in the after binary only, and it changes the
printed gap, not a category. Every run reached `steady_60s` in its zone.
Snapshots were restored byte-identically and the lock was released after both
rigs. Table: `review/B909b/x86/table_before_after2.md`.

| zone | `spatial` before | after | after % of 4 MiB | `sp.object_tris` before → `sp.object_instances` after |
|---|---:|---:|---:|---|
| gfaydark | 5,674,432 | **740,016** | 17.6 % | 5,257,648 → 323,232 |
| growthplane | 40,962,176 | **1,236,416** | 29.5 % | 40,212,336 → 486,576 |
| trakanon | 26,014,016 | **929,376** | 22.2 % | 25,431,600 → 346,880 |
| overthere | 16,063,168 | **1,106,400** | 26.4 % | 15,302,960 → 348,032 |

`ready` = `steady_60s` in every run. The other owner rows (`sp.object_regions`,
`sp.region_bounds`, `sp.region_tris`, `sp.light_regions`) are unchanged within
64 B. The shared mesh arrays the set reads in place are 0.09 MB (growthplane)
to 0.95 MB (overthere). They are charged to `objects`, which did not move:
47,107,104 → 47,106,960 in growthplane.

On x86, RssAnon is dominated by llvmpipe's `gpu_driver`, which varies by up to
90 MB between runs (B924). Owned = RssAnon − `gpu_driver` at `steady_60s`
therefore swings with `malloc_slack` (13–54 MB here). The attributed
non-driver bytes (ledger − `gpu_driver`) went 45.8 → 43.1 MB (gfaydark),
114.1 → 74.7 (growthplane), 81.8 → 56.7 (trakanon) and 83.1 → 77.2 (overthere).
x86 owned vs 64 MiB is in `review/B909b/x86/table_before_after2.md`. The Pi
figure below is the one that counts.

## Orange Pi A/B

`scripts/pi/collision_ab.sh --zones growthplane,trakanon,gfaydark --rounds 3`,
with legs interleaved A B / B A / A B. A = fable `58ff848c` (CI publish,
`ci-arm/58ff848c…`, sha256 `97c74599…89a6`). B = this branch `ad5b4c68`
(`ci-arm/ad5b4c68…`, `44ee9855…069a`). Every leg ran on DRM/GLES2 at
1280x720 with the orangepi preset, `WILLEQ_BASELINE=1`,
`WILLEQ_LEDGER_OWNERS=spatial`, level 60 and the fixed uinput key sequence
(`scripts/pi/uinput_drive.py`, which drives the renderer's collision rays).
All 18 zone runs reached `steady_60s`, and every leg ended with "locks
released; eqemu lock now: ''; pi lock now: ''". Table:
`review/B909b/pi-ab2/table.md`.

| zone | side | `spatial` @steady | objects | RssAnon @steady | gpu_driver | **owned = RssAnon − gpu_driver** | owned vs 64 MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| gfaydark | A | 5.5 MB | 3.7 MB | 94.0–95.4 MB | 19.9–20.0 | 74.0–75.5 MB | +6.9…+8.4 MB |
| gfaydark | B | **0.58 MB** | 3.7 | 95.0 | 20.0 | 75.0 | +7.8…+7.9 |
| growthplane | A | 40.7 MB | 40.3 | 152.7–161.9 | 19.3 | 133.4–142.6 | +66.3…+75.5 |
| growthplane | B | **0.97 MB** | 40.3 | 134.7–137.8 | 19.3 | 115.5–118.5 | +48.4…+51.4 |
| trakanon | A | 25.8 MB | 23.4 | 128.1–138.5 | 19.7 | 108.4–118.8 | +41.3…+51.7 |
| trakanon | B | **0.71 MB** | 23.4 | 125.0–128.0 | 19.7 | 105.2–108.2 | +38.1…+41.1 |

(MB = 10^6 B; ranges over the three legs of a side.) `spatial` on the Pi is
under 4 MiB in every measured zone: 582,208, 966,864 and 712,144 B, against
5.52, 40.71 and 25.79 MB on fable. The owner rows for every leg are in the
table file. Owned memory fell 17–24 MB in growthplane and 3–11 MB in trakanon.
RssAnon falls by less than `spatial` did, and the difference stays in glibc's
arena. The client is still over the 64 MiB target in all three zones. The
largest remaining owners are `objects` (B936, #254) and `game_state` (#238).

**Frame time while moving** (medians of the client's 60-frame `/frametiming`
blocks inside the key sequence, pooled over the legs of a side):

| zone | A moving | B moving | B vs A | Player Movement (the collision phase) A → B |
|---|---:|---:|---:|---:|
| gfaydark | 33,316 µs (30.0 fps) | 33,312 µs (30.0 fps) | −0.01 % | 426 → 411 µs |
| growthplane | 38,848 µs (25.8 fps) | 38,266 µs (26.1 fps) | −1.50 % | 562 → 308 µs |
| trakanon (3 rounds) | 26,358 µs (37.9 fps) | 27,216 µs (36.7 fps) | +3.26 % | 317 → 264 µs |
| trakanon (4 more rounds, `review/B909b/pi-ab3-trakanon/table.md`) | 25,876 µs (38.6 fps) | 26,189 µs (38.1 fps) | +1.21 % | 312 → 259 µs |
| **trakanon, all 7 + 7 legs pooled** | 26,298 µs | 26,925 µs | **+2.38 %** | 315 → 260 µs |

The collision phase is faster in every zone and every leg. growthplane
halves it because the box test skips most of the plants a ray cannot reach,
where the old loop tested every triangle in the region.

trakanon's first three rounds crossed the 3 % rule, at +3.26 %, so four more
rounds were run. trakanon's frame rate is multimodal on both binaries.
Per-leg moving medians: A 23,936–30,177 µs, B 23,100–28,027 µs. Idle blocks
sit at either ~16 ms or ~25 ms in some legs of each side. Over all 7 + 7 legs,
B is +2.38 %, inside the rule, while its collision phase is −55 µs. The
per-phase medians of the first three rounds put the moving difference in
"SimWorker Apply" (+403 µs) and the draw passes (+146 µs), not in Player
Movement (−53 µs). Neither reads the collision set. growthplane's idle
blocks show the same modes (~36.5 ms against ~39.9 ms, on both sides).
Under 30 fps: growthplane is below 30 fps on both binaries (A 25.8, B 26.1
moving). That is #245 (B926), which this unit did not cause and did not fix.

## Verification

- Full suite, as CI's `test` job runs it (asset links in place,
  `WILLEQ_REQUIRE_ASSETS=1`, `-LE live`, `visual_gate` excluded): **5,078 of
  5,078 passed** on `ad5b4c68`'s tree (`review/B909b/logs/ctest_full2.log`).
  That includes the `hygiene` label, and ZeroCallers is clean after
  `hasRegion` was classified.
- CI pipeline 336 (`ad5b4c68`): hygiene, build, test and gate green. The gate
  passed on attempt 1.
- Local `visual_gate` on `dev-u24`: FAIL, twice, on memory-coverage iceclad
  allocator slack 35.4 % (the bound is 35.0 %). The first run also missed the
  iceclad CPU-p95 ceiling by 0.024 ms. The benchmark binary contains no B909b
  code (`nm` finds no `ObjectCollisionSet`), and the runner measured 33.2 % on
  the same commit. Filed as B937 (#255). fable was not run through the gate
  locally.
- `check_file_sizes.sh fable`: 11 files, all ok.

## Findings filed

- **B936 (#254)**: vertex-animated grass and plants keep a private `IMesh`
  per placement. Client `objects` is 47.1 MB in growthplane on x86, over the
  40 MiB budget, and 40.3 MB on the Pi. This is the cause behind #247 and
  #238's `objects` rows.
- **B937 (#255)**: the local gate's memory-coverage slack bound fails on
  `dev-u24` and passes on the runner.

## Not verified

- Ray identity on ARM (see "Identical rays"). The Pi runs show the collision
  phase working (Player Movement changes with the keys), but no ray-for-ray
  comparison was made there.
- `sp.region_bounds` was not changed (see above).
- overthere was measured on x86 only, not on the Pi.
- The x86 figures are one run per zone and side. The Pi figures are three
  legs per side (trakanon: seven).
- Local `visual_gate` did not pass on this host (B937). The CI gate did.
- Vertex-animated collision stays the frozen load-time pose, as it was before.
  Whether swaying grass and plants should collide at all was not examined.
