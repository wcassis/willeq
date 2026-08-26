# T242 — blackburrow / crushbone render blank in gles2_zone_benchmark (Batch H, never root-caused)

Slot: slot1. x86. NO eqemu (benchmark tool loads zones from assets). Class
B → A. Territory: tools/benchmark (gles2_zone_benchmark), zone loaders
under src/client/graphics/eq/ + renderer_zone_load*, benchmarks/ configs,
tests. Do NOT touch action/*, eq_action_handler*, combat.* (T239/T240),
renderer_frame.cpp input paths.

## Lead (deferred.md, Batch H / H02)
`gles2_zone_benchmark` renders blackburrow and crushbone BLANK (no
geometry) while other zones render; never root-caused; no later unit
touched it. Both are Titanium-era zones with S3D + EQE1 packs present
(verify: ls /mnt/projects/EverQuestP1999/blackburrow* and the EQE1 dir).

## Scope
1. Reproduce: run the benchmark on blackburrow and crushbone with
   `--eqe1-path` and a frame dump; confirm blank (pixel stats) vs. a
   control zone (qeynos2). Capture the loader log at DEBUG with
   `EQT_EQE1_REQUEST_LOG_DIR`.
2. Attribute: is it (a) zone-load failure (WLD parse error / fragment the
   loader rejects — grep the log for the first WARN/ERROR), (b) geometry
   loaded but culled (PVS/BSP region SIZE_MAX — the "Player BSP region is
   SIZE_MAX" board item may be the same bug; check the camera start
   position vs the zone's safe point), or (c) loaded and drawn but the
   benchmark's camera path (benchmarks/paths/*.path) points at nothing.
   State which with the log line / numbers.
3. Fix if narrow (a wrong safe-point/camera path, a fragment type the
   loader skips, a region lookup off-by-one). If it needs loader redesign,
   STOP after attribution.
4. If fixed: before/after frame crops with non-background pixel counts;
   visual gate PASS (are either goldens? check `scripts/ci/visual_gate*`);
   full ctest.

## Verification
Review; evidence review/T242/. Commit: `T242: blackburrow/crushbone render in the benchmark -- <cause>` or the attributed-not-fixed form.

## Review (2026-08-26, slot1, base fable `b9b503ec`)

### Attribution: (c) -- the benchmark's camera path pointed at nothing

Reproduced first (pre-fix `build-gles2/bin/gles2_zone_benchmark`,
`--eqe1-path`, `--navmesh-path /mnt/projects/maps/nav`, 30 frames, dumps at
0/15/29; `review/T242/repro/`):

| zone | frames | pixels | log |
|---|---|---|---|
| blackburrow | f0/f15/f29 | 1 distinct colour (40,40,60) x 480000, 0 non-bg | `draws=0 polys=0 reg vis=0` all 30 frames |
| crushbone | f0/f15/f29 | 1 distinct colour, 0 non-bg | ~20 draws / 86 polys per frame, nothing lands on screen |
| qeynos2 (control) | f0/f15/f29 | 60995 / 67023 / 37827 distinct, 99.9 / 97.7 / 92.2% non-bg | navmesh path, 20 waypoints |

(a) ruled out: zero WARN/ERROR/FATAL from the loader at DEBUG.
`S3D loaded: 2301 regions, 60 textures` / `Meshes built: 2269 regions (32
skipped), 4199 buffers` for blackburrow; `2695 regions` / `2692 built (3
skipped)` for crushbone -- same shape as qeynos2 (`2074` / `1898 built`).
All 60 blackburrow ETC1 textures uploaded from `blackburrow.eqe1`.

(b) ruled out and (c) confirmed by the static-pose test (`review/T242/poses/`,
same pre-fix binary, `--camera-pose` at the zones' zone-line coordinates from
`data/zone_lines.json`, both x/y orderings tried):

| zone | pose | non-bg pixels | draws/polys |
|---|---|---|---|
| blackburrow | (-162,38,6) hdg 0/90/180/270 | 99.8% / 97.5% / 79.4% / 99.8% | up to 208 draws / 1063 polys |
| blackburrow | (38,-162,6) hdg 0/90/180/270 | 81.2% / 99.1% / 99.2% / 99.2% | up to 536 draws / 2465 polys |
| blackburrow | (95,-340,6) / (-340,95,6) | 99.7% / 8.1% (sky+fog only, inside rock) | 9 / 92 draws |
| crushbone | (-637,162,6) hdg 0/90/180/270 | 99.6% / 99.0% / 99.8% / 98.3% | 5-29 draws |
| crushbone | (162,-637,6) hdg 0/90/180/270 | 3.3% / 0 / 0 / 0 (outside the zone -- the swapped ordering) | 0-13 draws |

Frames inspected: `poses/blackburrow_f4.png` shows the canyon floor, stone
walls and water; the after-frames below show the same class of content.

Where the default path actually went (pre-fix logs):

```
blackburrow: Zone bounds Z=[-228,605]  Surface position: (-23.0,-193.9,329.0) [41 polys]
             WARNING: Only 0 valid navmesh samples  ->  orbit center=(47,46,329) radius=266
crushbone:   Zone bounds Z=[-185,629]  Surface position: (98.0,-145.4,346.9) [17 polys]
             WARNING: Only 0 valid navmesh samples  ->  orbit center=(-189,271,347) radius=396
gfaydark:    Zone bounds Z=[-99,802]   Surface position: (-332.5,-518.6,417.9) [92 polys]
             WARNING: Only 0 valid navmesh samples  ->  orbit center=(-43,37,418) radius=1656
befallen:    Zone bounds Z=[-86,21]    Surface position: (-359.9,84.0,-5.0) [97 polys]
             WARNING: Only 0 valid navmesh samples  ->  orbit center=(-525,-43,-5) radius=290
qeynos2:     Zone bounds Z=[-391,80]   Surface position: (243.9,53.0,43.0) [158 polys]
             Sampled 20 valid waypoints (navmesh path)
```

Root cause, `tools/hwcaps/zone_bench_loader.cpp::findSurfacePosition`: the
"surface" threshold was `boundsMinZ + 0.5 * (boundsMaxZ - boundsMinZ)` over
the raw vertex extents. A few sky / far-scenery regions hundreds of units
up (blackburrow to z=605, crushbone to z=629, gfaydark to z=802) put that
threshold (188 / 222 / 351) above every playable region, so the winner was a
41- / 17- / 92-poly scenery region in mid-air. `PathfinderNavmesh::
GetRandomLocation` searches only (5,100,5) EQ units around the seed, found
no polygon, and the orbit fallback circled the zone at that height.
befallen is the second failure mode: its bounds are sane and the seed is at
floor level, but the largest region's bounding-box centre is inside rock /
off-mesh, and there was only one seed to try. So the H02 review's
"BSP above/below-ground split" speculation was wrong, and there is no
production rendering bug behind this item.

The "Player BSP region is SIZE_MAX" board item is NOT the same bug: that is
`CreateZoneLoadSnapshot()` in the client pairing a stale zone name/BSP with
new coordinates; the benchmark never calls it and its blank frames carried
`reg vis=0` from an out-of-zone camera, not a region-lookup failure.

### Fix (benchmark tool only; 5 files under tools/hwcaps/)

- `zone_bench_loader.{h,cpp}`: new `findSurfaceCandidates(data, maxN)` --
  regions ranked by polygon count, restricted to centre Z >= the
  polygon-weighted median of region-centre Z (outlier regions carry almost
  no polygons so they cannot move it; underground regions stay excluded).
  Prints `Surface candidates: N regions at or above the poly-weighted median
  Z=... (bounds Z=[..])`. `findSurfacePosition` = candidate 1 (unchanged
  signature; the entity-placement call site keeps its behaviour).
- `zone_bench_camera.{h,cpp}`: `generateFromNavmesh` overload taking a
  ranked candidate list; tries each in order (PRNG re-seeded per candidate so
  the path depends only on (seed, winning candidate)), loud per-candidate
  WARNING on failure, `[candidate k of N]` on success.
- `gles2_zone_benchmark.cpp`: the moving-path setup asks for 16 candidates
  and passes them all to the sampler. Static `--camera-pose`, `--path-file`
  replay, entity viewer and the orbit code are untouched.

### After (fixed binary, byte-identical arguments; `review/T242/after/`)

| zone | seed chosen | path | frames (non-bg) |
|---|---|---|---|
| blackburrow | (-103.5,-56.5,-25.0) [147 polys], candidate 1/16, median Z=-44.7 | navmesh, 20 waypoints, 532 units | f0 98.8%, f15 74.4%, f29 77.9% (2312-2787 polys/frame) |
| crushbone | (72.5,181.4,6.5), candidate 2/16 (candidate 1 off-mesh), median Z=6.5 | navmesh, 20 waypoints, 756 units | f0 97.6%, f15 81.8%, f29 76.8% |
| qeynos2 | (243.9,53.0,43.0) [158 polys], candidate 1/16 -- IDENTICAL to before | `diff before/qeynos2.path after/qeynos2.path`: only the `# command:` line | n/a |
| gfaydark | (595.6,644.2,196.9) [212 polys], candidate 1/16, median Z=86.9 | navmesh, 20 waypoints (was orbit r=1656) | n/a |
| befallen | (-175.4,-167.9,-7.0), candidate 10/16 | navmesh, 20 waypoints (was orbit r=290) | n/a |

Contact sheet: `review/T242/before_after_contact_sheet.png` (six frame
pairs, identical arguments and frame indices). The pixel numbers are
measured, not judged -- Will judges the content.

### Deviations / behaviour changes to know about

- gfaydark and befallen default moving paths CHANGE (orbit fallback ->
  real navmesh path). Nothing gated depends on them: the three golden zones
  use static `--camera-pose` (goldens untouched, gate PASS below), the T80
  memory-coverage run is unblessed (NOT YET BLESSED, no assertion), and every
  Pi measurement replays `benchmarks/paths/*.path` via `--path-file`. A
  future re-bless of the memory-coverage baselines should be taken with this
  binary. Flagged, not absorbed.
- `check_file_sizes.sh fable`: `gles2_zone_benchmark.cpp` (3026 -> 3033
  lines) and `zone_bench_loader.cpp` (3196 -> 3232) both report FAIL (>3000)
  -- both were already over the limit at the base rev (`git show fable:...
  | wc -l`), so these are pre-existing, not new FAILs. The additions are
  +7 and +36 lines; moving a 75-line function into a new TU would not bring
  either file under 3000.
- Plan step 1 asked for the EQE1 request log; `EQT_EQE1_REQUEST_LOG_DIR`
  requires the directory to pre-exist (the benchmark logged `cannot write
  EQE1 request log`). Not re-run: the request log answers "which archive
  served which texture", and the attribution was settled by draws/regions
  counters + static poses before texture lookups mattered (all 60/73 zone
  textures uploaded, 0 NOT-FOUND at DEBUG).

### Verification bar

1. `cmake --build slot1/build -j24`: OK.
2. `ctest --test-dir slot1/build --output-on-failure`: **2858/2859** -- the
   one failure is `AudioManagerTest.PreloadCommonSounds` (documented ALSA
   host quirk). Suite grew from the 2746 baseline via T215-T241 merges.
3. `scripts/build-gles2-x86.sh`: OK.
4. `scripts/ci/visual_gate.sh`: **PASS**, goldens untouched (`git status`
   shows only the five tools/hwcaps sources).
5. `scripts/build-arm-noble.sh`: OK.
6. `check_file_sizes.sh fable`: 3 ok, 2 pre-existing FAIL (see deviations).
