# B936: vertex-animated placeables share one mesh per geometry

GitLab #254 (and #247 on the benchmark side). Plan:
`refactor/B936_shared_animated_placeables.md`. Two branches, both from
`fable` @ `a5068aee`:

- `task/B936-shared-animated-placeables` (branch A, the client): the fix.
  Closes #254.
- `task/B936b-bench-shared-objects` (branch B, stacked on A): the same
  sharing in `gles2_zone_benchmark`, plus `--object-anim-frame`. It moves the
  gate's exact-match `placeableObjectsBytes` baselines in all five gate
  zones, so it waits for Will's re-bless. It would close #247.

Measured 2026-09-30 on `dev-u24` (x86, llvmpipe) and on the Orange Pi. eqemu
lane: the VM (`172.16.200.99`), lock owner `b936`. Raw evidence:
`/mnt/projects/fable/review/B936/` (its README lists every file).

## Step 1: root cause

`loadZoneStep6_Objects()` shared one `IMesh` per `ZoneGeometry` (T504) but
excluded vertex-animated geometries, "because their vertices are rewritten
every frame". Each animated placement got its own mesh (vertex and index
arrays, `SMesh`/`SMeshBuffer`/`SMaterial`) and its own `VertexAnimatedMesh`
registration, which holds a `size_t` per vertex (the keyframe mapping).
`SimBridgeComponent::applySimulationOutput()` then rewrote every
registration's vertices on every keyframe change.

The reason for the exclusion does not hold. The animation is recorded
keyframes (fragment 0x37: `frames[k].positions`, one `delayMs` per geometry,
no interpolation). The simulation worker starts every registration's counter
at keyframe 0 with 0 ms elapsed (`updateVertexAnimData`) and advances all of
them by the same delta (`computeVertexAnimations`). So every placement of a
geometry is on the same keyframe at every instant. The private meshes always
held the same bytes. The one per-placement difference was a skip for
`!node->isVisible()`, which never fired: objects leave the scene graph by
`remove()`, which keeps `isVisible()` true.

growthplane (client log, x86):

| geometry | placements | vertices | keyframes | delay |
|---|---:|---:|---:|---:|
| CMPLANT102 | 1,779 | 200 | 16 | 100 ms |
| CMPLANT101 | 1,443 | 136 | 16 | 100 ms |
| DRGRASS102 | 589 | 96 | 16 | 100 ms |
| DRGRASS101 | 503 | 96 | 16 | 100 ms |

- **Bytes per animated placement, fable:** 8.66 KB of `objects` on average
  ((47,107,296 − 9,738,560) B freed / 4,314 placements, ledger chunk bytes).
  That is about 6.6 KB of vertex and index arrays (36 B per vertex), 8 B per
  vertex of keyframe mapping (1.2 KB on average), and the mesh, buffer and
  material objects.
- **The 4 geometries once:** 22,800 B of vertex and index arrays
  (`[objmesh] vertex-animated` log line). Every mesh the zone draws (11
  geometries) is 126,992 B of ledger (`obj.meshes`). The 4 keyframe bindings
  are 5,040 B (`obj.anim`).
- **CPU per keyframe change, fable:** 656,880 vertex positions written
  (1,779×200 + 1,443×136 + 589×96 + 503×96), ten times a second, on the render
  thread. Now 528. On the Pi the renderer's "SimWorker Apply" phase, where this
  runs, was 23.5 ms of a 37.8 ms frame in growthplane on fable (table below).

## The fix

- `PlacedObjectMeshes` (`include/client/graphics/placed_object_meshes.h`,
  `src/client/graphics/placed_object_meshes.cpp`) is T504's per-geometry mesh
  table without the animated exclusion. It registers ONE `VertexAnimatedMesh`
  per mesh and counts the placements that draw it.
  `WILLEQ_SHARE_OBJECT_MESHES=0` still builds a mesh and a registration per
  placement, so one binary can render both layouts.
- `applyVertexAnimFrame()` is the one routine that poses a mesh (moved
  verbatim from the sim bridge). `bindVertexAnimation()` is the binding
  (moved verbatim from step 6). The client's per-frame apply and the
  benchmark's fixed keyframe both call it.
- The per-placement state is what a static object has: the scene node, its
  transform and its material copies.
- `ObjectCollisionSet` is unchanged. It still snapshots the load-time pose,
  and its content dedup now sees 4 meshes instead of 4,314 copies of 4.
- `/pmem` (`getMemoryReportBreakdown()`) counted a shared mesh once per node.
  It now counts it once.

**Why not a vertex-shader sway**, which the dispatch preferred: the
animation is recorded keyframe positions, not a procedural sway. A shader
would need every keyframe of every geometry resident on the GPU and a way to
select one per draw. The Mali-400 vertex processor has no texture units (no
vertex texture fetch), so that means one attribute set per keyframe: 16× the
vertex data of the one shared buffer. It would buy nothing, because the
shared CPU pose is already O(distinct geometries): 4 meshes, 528 vertices
per keyframe change in growthplane. The CPU path that remains is the "shared
source + one buffer per geometry" the dispatch allows.

## Accounting

With `WILLEQ_LEDGER_OWNERS=objects`, step 6 labels its allocations
`obj.meshes` (the shared mesh arrays, built once per geometry),
`obj.instances` (per placement: the scene node, its transform and material
copies, the object light and the renderer's per-object vectors) and
`obj.anim` (one keyframe binding per animated mesh). Nothing moved to
another category. The step 6 log names the shared block once, with its
claimants:

    [objmesh] vertex-animated: 4314 placements draw 4 meshes (22800 B of
    vertex+index arrays, held once; claimants: object draw, keyframe pose,
    object collision snapshot)

## x86: the client's `objects`

`scripts/client_ledger_capture.sh --zones growthplane,trakanon,overthere,gfaydark
--modes objects --char-level 60 --char-exp 620000000`, one rendered run per
zone and side. Settings: orangepi preset, 1280x720, `WILLEQ_BASELINE=1`.
Before: fable `a5068aee` (`review/B936/bin/fable_a5068aee/willeq`, sha256
`e3bb77e0…c781`). After: branch A `4f540656` (`bin/branchA/willeq`,
`4fd4ba6d…bf86`). The branch's gfaydark run was repeated with `--hold 100`:
its first run spent 15 s in "Building asset indexes" while the host was
compiling, so it quit before `steady_60s`. Every snapshot was restored
byte-identically, and the lock was released after each rig.

| zone | `objects` @steady_60s fable | branch | % of the 40 MiB budget | `obj.instances` | `obj.meshes` | `obj.anim` |
|---|---:|---:|---:|---:|---:|---:|
| growthplane | 47,107,296 | **9,738,560** | 23.2 % | 9,606,528 | 126,992 | 5,040 |
| trakanon | 27,511,584 | **7,564,352** | 18.0 % | 6,502,704 | 1,051,280 | 10,368 |
| overthere | 21,890,032 | **7,867,648** | 18.8 % | 6,735,264 | 1,122,928 | 9,456 |
| gfaydark | 5,413,568 | **5,407,232** | 12.9 % | 5,053,568 | 351,456 | 2,208 |

Owned = RssAnon − `gpu_driver` at `ready` (x86 `gpu_driver` is llvmpipe and
grows by a different amount in every run after `ready`, B924): growthplane
66.4 → 29.4 MB, trakanon 67.2 → 46.8, overthere 86.4 → 67.5, gfaydark
43.8 → 42.7. The Pi figures below are the ones that count.

## Orange Pi A/B

`scripts/pi/collision_ab.sh --zones growthplane,trakanon,overthere,gfaydark
--rounds 3 --ledger-owners objects --owner b936`, legs interleaved A B / B A /
A B. A = fable `a5068aee` (CI publish `ci-arm/a5068aee…`, sha256
`44ee9855…069a`). B = branch A `02786b36` (`ci-arm/02786b36…`,
`7112c483…aa3a`). Every leg ran on DRM/GLES2 at 1280x720 with the orangepi
preset, `WILLEQ_BASELINE=1`, `WILLEQ_LEDGER_OWNERS=objects`, level 60 and the
fixed uinput key sequence. Every leg ended with "locks released; eqemu lock
now: ''; pi lock now: ''". Table: `review/B936/pi-ab/table.md`, made by
`scripts/pi/collision_ab_table.py --category objects` (this unit added the
`--category`, `gpu_driver`, owned and "SimWorker Apply" columns).

| zone | side | `objects` @steady_60s | RssAnon @steady | gpu_driver | **owned = RssAnon − gpu_driver** | owned − 64 MiB |
|---|---|---:|---:|---:|---:|---:|
| growthplane | A | 40,295,808–40,296,320 | 137.8 MB | 19.3 MB | 118.5 MB | +51.4 MB |
| growthplane | B | **6,715,192–6,715,336** | 104.4–104.8 | 19.3 | **85.1–85.5** | +18.0…+18.4 |
| trakanon | A | 23,401,240–23,401,424 | 122.6–127.2 | 19.7 | 102.8–107.4 | +35.7…+40.3 |
| trakanon | B | **5,484,872–5,484,992** | 110.1 | 19.7 | **90.3–90.4** | +23.2…+23.3 |
| gfaydark | A | 3,662,672–3,662,720 | 95.0–96.7 | 20.0–20.2 | 75.0–76.5 | +7.9…+9.4 |
| gfaydark | B | 3,656,800–3,656,888 | 94.7–97.0 | 19.9–20.1 | 74.8–76.9 | +7.7…+9.8 |
| overthere | A, B | not reached (0 of 3 legs each, B940) | | | | |

(MB = 10^6 B; ranges over the three legs of a side.) Owner rows on B at
`steady_60s`: growthplane `obj.instances` 6,590,544 / `obj.meshes` 122,064 /
`obj.anim` 2,632; trakanon 4,467,192 / 1,012,312 / 5,432; gfaydark 3,331,272 /
324,344 / 1,184. fable's rows are all `misc`, because it has no labels.
`objects` on the Pi is 16 % (growthplane), 13 % (trakanon) and 9 % (gfaydark)
of its 40 MiB budget. Owned memory fell 33 MB in growthplane and 12–17 MB in
trakanon. The client is still over the 64 MiB target in all three zones.

**Frame time** (medians of the client's 60-frame `/frametiming` blocks,
pooled over the legs of a side; moving = inside the key sequence):

| zone | A moving | B moving | B vs A | SimWorker Apply A → B (moving) | A idle | B idle |
|---|---:|---:|---:|---:|---:|---:|
| growthplane | 38,896 µs (25.7 fps) | **16,094 µs (62.1 fps)** | **−58.6 %** | 23,539 → 1,966 µs | 36,541 µs (27.4 fps) | 16,295 µs (61.4 fps) |
| trakanon | 26,632 µs (37.5 fps) | **16,326 µs (61.2 fps)** | **−38.7 %** | 9,786 → 1,475 µs | 24,951 µs (40.1 fps) | 24,494 µs (45.9 fps) |
| gfaydark | 33,303 µs (30.0 fps) | 33,313 µs (30.0 fps) | +0.03 % | 338 → 334 µs | 33,313 µs | 33,310 µs |

"SimWorker Apply" is the render-thread phase that applies the worker's
results, and in fable most of it in these zones was the per-placement
keyframe rewrite. Player Movement (collision) is unchanged: growthplane
310 → 311 µs, trakanon 264 → 268, gfaydark 414 → 408. growthplane was under
30 fps on fable (#245, B926); on this branch it moves at 62 fps. gfaydark
sits at the 30 fps frame cap on both binaries.

## Benchmark (`placeableObjectsBytes`, #247) — branch B

The benchmark's `--objects` loop built a private mesh and VBO for every
placement. Neither T504 nor B936 had been ported to it, and
`placeableObjectsBytes` summed one mesh per placement. Branch B builds
through `PlacedObjectMeshes` (one mesh and one upload per geometry, the
merged texture map built once instead of once per placement). It counts each
distinct mesh once and adds `--object-anim-frame N`.

The gate's memory-coverage invocation (`review/B936/scripts_coverage.sh`),
fable vs branch B, x86 (`review/B936/bench_coverage/table.md`):

| zone | fable | branch B | 24 MiB budget |
|---|---:|---:|---:|
| growthplane | 28,687,788 (over) | **111,348** | 0.4 % |
| trakanon | 20,312,166 | **929,718** | 3.7 % |
| overthere | 13,294,668 | **982,410** | 3.9 % |
| gfaydark (gate) | 5,624,100 | 266,388 | 1.1 % |
| qeynos2 (gate) | 1,365,564 | 145,812 | 0.6 % |
| befallen (gate) | 2,168,052 | 130,968 | 0.5 % |
| iceclad (gate) | 389,688 | 215,112 | 0.9 % |
| timorous (gate) | 3,990,714 | 981,756 | 3.9 % |

`placeableObjectsBytes` is vertex count × 36 B + index count × 2 B over the
distinct meshes. It is platform-independent by construction. The same invocation on the Orange Pi (DRM), with the CI ARM benchmark
binaries (`review/B936/pi_bench_coverage/`, `pi_bench_coverage.sh`, Pi lock
`b936`): growthplane 28,687,788 → **111,348**, trakanon 20,312,166 →
**929,718**, overthere 13,294,668 → **982,410**, gfaydark 5,624,100 →
**266,388**, identical to x86. The A side is the ARM benchmark built from
branch A `02786b36`, whose object loop is fable's. With one VBO per geometry
instead of one per placement, the benchmark's own `gpu_driver` on the Pi also
fell: growthplane 34.4 → 24.7 MB, trakanon 36.5 → 27.6 MB.

**The gate:** CI pipeline 344 (branch B `b3d30911`) and the local gate fail
only on the five memory-coverage `placeableObjectsBytes` exact-match
baselines (expected fable's per-placement sum, got the per-geometry one). No
golden image moved: the golden-image runs do not pass `--objects`. That
re-bless is Will's; branch B's MR stays open for it. Branch A (pipeline 342,
`02786b36`) passed hygiene, build, test and gate.

## Visual parity (x86 llvmpipe, per pixel)

`gles2_zone_benchmark --objects` at fixed poses. Settings: 800x600, the
gate's production flags, `--occluder-budget 16`, `--eqe1-path`, 5 warm-up
frames. Scripts and PNGs: `review/B936/parity*`. Per-frame numbers:
`review/B936/parity_results.txt`.

Poses: four per Kunark/Velious zone, aimed at animated placements. Each
camera sits 25 units south of and 8 above a placement the benchmark prints
(every 250th), heading 0, pitch −15 (`parity2/`). For gfaydark, timorous and
iceclad the poses are the gate manifest's (`parity/`).

| comparison | frames | max abs diff | differing pixels |
|---|---:|---:|---:|
| fable vs branch B, no keyframe (static sharing) | 12 aimed + 24 other | 0 | 0 |
| branch B, keyframe 0 / 5 / 11: a private mesh per placement (`WILLEQ_SHARE_OBJECT_MESHES=0`) vs the shared mesh | 3 × 12 aimed + 3 × 24 other | 0 | 0 in every frame |
| control: keyframe 0 / 5 / 11 vs no keyframe (does the animation move pixels at these poses?) | 3 × 12 aimed | 79–202 per frame | 23,859 / 91,431 / 95,804 in total (16–24,011 per frame) |

The shared layout is byte-identical to the per-placement layout at every
keyframe tested, and the control shows the poses do see the animation. It is
bit-exact rather than "bounded" because nothing moved to the GPU: the same
`applyVertexAnimFrame()` writes the same floats into one buffer instead of
4,314. In the client the equivalence also needs every placement to be on
the same keyframe, which is the worker's behaviour above. That behaviour is
pinned by the census test. The gate manifest's poses show no animated object
(keyframe vs no keyframe: 0 px), and its golden images do not load objects,
so no golden moved. None was re-blessed.

## Tests

`tests/test_placed_object_meshes.cpp` (7 tests, EDT_NULL device, the
production `ZoneMeshBuilder::buildColoredMesh()`):

- `TwoPlacementsOfOneAnimatedGeometryOwnOneVertexArray` (#254 acceptance 3):
  50 placements, one build, one vertex array, one registration with
  `placements == 50`;
- `EachGeometryGetsItsOwnMeshStaticOrAnimated`,
  `SharingOffBuildsAMeshPerPlacement`, `AFailedBuildIsNotCached`;
- `AKeyframeLandsThroughTheMappingWithTheCentreOffset`: the pose arithmetic
  against an independent evaluation, out-of-range keyframes ignored;
- `PrivateMeshesPosedInPhaseEqualTheSharedMeshByteForByte`;
- census `ProductionSharesAnimatedMeshesAndPosesThemOnce`: step 6 uses the
  table with no animated exclusion and no per-placement registration, the
  apply goes through `applyVertexAnimFrame()`, and the worker's in-phase
  premise (one delta, every counter reset to 0) is still in the source.

RED (`review/B936/red/animated_exclusion_restored.txt`): with T504's
exclusion put back inside `acquire()`, three tests fail, including the
acceptance test.

## Verification

- Full suite on branch B's tip, which contains A, as CI runs it
  (`WILLEQ_REQUIRE_ASSETS=1`, asset links in place, `-LE live`,
  `visual_gate` excluded, RDP test repeated): 5,084 + 1 passed
  (`review/B936/ctest_branchB_assets.log`). Branch A alone: 5,085 of 5,085
  without assets (`ctest_branchA.log`).
- CI: branch A pipelines 340–342 green (hygiene, build, test, gate); branch B
  pipeline 344: gate red on the five baselines only (above).
- Local `visual_gate` on branch B: the same five baseline mismatches, plus
  the iceclad CPU-p95 ceiling (9.894 ms against 9.530) while the Pi rig and
  parity captures were loading this host. Every golden image passed.
- `check_file_sizes.sh fable`: 4 changed files, all ok. The new files are
  under 200 lines.

## Findings filed

- **B938 (#256)**: after the sharing, the per-placement scene node is the
  largest `objects` owner (`obj.instances` about 2.2 KB per placement,
  9.6 MB of growthplane's 9.7 MB).
- **B939 (#257)**: the oracle's placement phase was not measured. #254
  acceptance 1 asked for it. The baked pose is not keyframe 0.
- **B940 (#258)**: overthere never reaches `ready` on the Pi. The zone port
  refuses UDP about 38 s into zone-in, during entity preparation (3,400+
  entities), in 6 of 6 legs on both binaries. The shape matches B925 (#244).

## Not verified

- **The era client's phase.** fable animates all placements of a geometry in
  phase, and B936 keeps exactly that. Whether the official client does was
  not measured (B939).
- Pixel parity is on x86 llvmpipe only, through the benchmark. The client's
  own frames were not compared, because they are not deterministic across
  runs (entities, sky, particles).
- overthere on the Pi: no leg reached `ready` on either binary (B940), so overthere has x86 client numbers and Pi benchmark numbers but no Pi client numbers.
- x86 figures are one run per zone and side. The Pi figures are three legs per
  side.
- The local gate on branch A was not run (CI's gate passed on `02786b36`).
