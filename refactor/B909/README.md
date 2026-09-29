# B909 — the client binary's own memory ledger, measured live; its spatial collision copy held once

GitLab #229. Branch `task/B909-client-spatial-ledger`, from `fable` @ `2f18b55e`.
Date 2026-09-29. Host `dev-u24` (x86, llvmpipe, Mesa 25.2.8). eqemu lane: the VM,
`172.16.200.99`, lock owner `b909-client-ledger`.

Every memory number of the last two days came from the CI gate, which measures
`gles2_zone_benchmark`, not `willeq`. This unit measures the **client** and
compares it with the benchmark per category.

## How it was measured

- **Client.** `scripts/client_ledger_capture.sh` runs the RENDERED client
  (`--renderer gles2 --constrained orangepi -r 1280 720 --eqe1-path ...`,
  surfaceless EGL on llvmpipe, audio on, `WILLEQ_BASELINE=1`) driven by
  `--script` (`wait 75`, `/q`). The character is placed with the zone-in sweep's
  own helpers, shared in `scripts/eqemu/zone_place_lib.sh` (the one
  placement library; the zone-in sweep and the rendered Pi sweep source it too): bounce the zone
  process, seed `character_data` at the zone's safe point, read back. The rig
  takes one lock, one snapshot, one restore with the world down, `down.sh`, and
  releases the lock. It prints the lock state last. Watchdog: `ready` within
  180 s, log silence 120 s, 330 s overall. `RssAnon` is also sampled from
  `/proc/<pid>/status` every 2 s.
- **One run per (zone, owner mode).** `spatial` and `zone_mesh` modes set
  `WILLEQ_LEDGER_OWNERS=<category>`. This unit added the owner labels (`sp.*`,
  `zm.*`). The `zone_parse` mode adds `WILLEQ_LEDGER_TRACE=zone_parse:startup`,
  because the shared WLD parser carries no labels. So every zone has three
  independent runs of every category.
- **Benchmark.** `scripts/client_ledger_table.py bench` imports the gate's own
  `run_benchmark()` and `run_memory_coverage_benchmark()` from
  `scripts/ci/visual_gate.py`, so the arguments are the gate's. dreadlands is
  not in the manifest, so it runs with no camera poses. Value = the larger of
  the golden and coverage invocations, from the memory JSONs' `ledger`
  (`benchmark_end`). These match `refactor/B914/README.md` to the byte for the
  three gate zones (for example iceclad `zone_parse` 11,329,968).
- **Tables.** `scripts/client_ledger_table.py table`. The full generated tables
  are in `summary_before.md` and `summary_after.md` in this directory. Nothing
  below is hand-computed except the before/after deltas, which are subtractions
  of two file values.

Commands (exact):

```
bash scripts/client_ledger_capture.sh --out /mnt/projects/fable/review/B909/before
bash scripts/client_ledger_capture.sh --modes spatial --assert-budgets --out /mnt/projects/fable/review/B909/after
python3 scripts/client_ledger_table.py bench --out /mnt/projects/fable/review/B909/bench
python3 scripts/client_ledger_table.py table --run <run dir> --bench /mnt/projects/fable/review/B909/bench --bin build/bin/willeq
```

| run | commit (src identical to) | binary sha256 | UTC |
|---|---|---|---|
| before | `abcf1613` (= `cfe8fe64`: labels + 64-slot owner table) | `b100ebbc…ba32` | 05:36:26 → 06:17:35 |
| after | `ebedec1d` (collision fix) | `298e069a…8d8e` | 06:18:46 → 06:31:42 |

Both runs: 12 + 4 client runs, every one reached `ready` and `steady_60s` in
the zone it was seeded into. The snapshot was restored byte-identically and the
lock was released (`lock-show (empty = free): ''`).

## Result 1: client vs benchmark, budgeted categories (BEFORE the fix)

Client value = the largest of the three runs at `steady_60s`. `ready` is the
same to within the spread (`summary_before.md` has both, plus
`zone_load_complete`). The run-to-run spread is at most 1.03 MB (entity_models,
gfaydark) and a few kB elsewhere. The budget is `orangepi` `memoryLedgerBudgets`.

| zone | category | budget | client | % | benchmark | % | client − bench |
|---|---|---:|---:|---:|---:|---:|---:|
| gfaydark | `spatial` | 4,194,304 | **8,491,552** | **202.5 %** | 621,328 | 14.8 % | +7,870,224 |
| gfaydark | `zone_parse` | 33,554,432 | 1,754,544 | 5.2 % | 757,872 | 2.3 % | +996,672 |
| gfaydark | `zone_mesh` | 29,360,128 | 6,522,192 | 22.2 % | 6,781,824 | 23.1 % | −259,632 |
| gfaydark | `archives` | 4,194,304 | 2,136,128 | 50.9 % | 1,301,296 | 31.0 % | +834,832 |
| gfaydark | `entity_models` | 88,080,384 | 11,416,640 | 13.0 % | 4,225,472 | 4.8 % | +7,191,168 |
| gfaydark | `objects` | 41,943,040 | 5,413,952 | 12.9 % | 9,804,304 | 23.4 % | −4,390,352 |
| dreadlands | `spatial` | 4,194,304 | **9,352,912** | **223.0 %** | 1,251,840 | 29.8 % | +8,101,072 |
| dreadlands | `zone_parse` | 33,554,432 | 4,419,024 | 13.2 % | 3,388,704 | 10.1 % | +1,030,320 |
| dreadlands | `zone_mesh` | 29,360,128 | 11,418,768 | 38.9 % | 12,417,664 | 42.3 % | −998,896 |
| dreadlands | `archives` | 4,194,304 | 2,158,240 | 51.5 % | 1,324,160 | 31.6 % | +834,080 |
| iceclad | `spatial` | 4,194,304 | **8,129,968** | **193.8 %** | 2,786,288 | 66.4 % | +5,343,680 |
| iceclad | `zone_parse` | 33,554,432 | 11,714,736 | 34.9 % | 11,329,968 | 33.8 % | +384,768 |
| iceclad | `zone_mesh` | 29,360,128 | 11,219,664 | 38.2 % | 19,064,912 | 64.9 % | −7,845,248 |
| iceclad | `archives` | 4,194,304 | 2,125,984 | 50.7 % | 1,290,688 | 30.8 % | +835,296 |
| timorous | `spatial` | 4,194,304 | **13,762,720** | **328.1 %** | 3,019,840 | 72.0 % | +10,742,880 |
| timorous | `zone_parse` | 33,554,432 | 7,163,936 | 21.4 % | 5,529,168 | 16.5 % | +1,634,768 |
| timorous | `zone_mesh` | 29,360,128 | 17,503,152 | 59.6 % | 23,587,088 | 80.3 % | −6,083,936 |
| timorous | `archives` | 4,194,304 | 2,163,232 | 51.6 % | 1,319,360 | 31.5 % | +843,872 |

The other budgeted categories are inside their budgets in all four zones, with
the largest share shown: `unattributed` 141,008 (13.4 %), `textures_decoded`
≤ 1,050,880 (5.0 %), `entity_models` ≤ 12,861,760 (14.6 %), `objects`
≤ 5,413,952 (12.9 %) and `audio` ≤ 1,626,976 (4.8 %). The benchmark reports
`audio` 0 because it has no audio.

**Where the two diverge, and why (from the owner rows):**
- `spatial`: the client builds collision copies that the benchmark never
  builds (`sp.region_tris`, `sp.object_tris`) and a `std::map` of region
  bounding boxes (`sp.region_bounds`). The benchmark's `spatial` is
  its occluder table.
- `zone_mesh`: the client charges its occluder table here (`zm.occluders`
  0.61–3.01 MB; Step 4's scope). The benchmark charges it to `spatial`. The
  benchmark also keeps one 336-byte `SMeshBuffer` per region (its unpooled draw
  path, B904). Result: the client is 0.26–7.8 MB below the benchmark.
- `zone_parse`: the client is 0.38–1.63 MB above the benchmark. Trace sites
  (below) show the placeable-object parse (`loadObjectsFromArchive`) and PVS/BSP.
- `archives`: the client is about 0.83 MB above the benchmark in every zone.
  This was not split. It is inside its budget.

## Result 2: the `spatial` owner split (BEFORE → AFTER), `ready` = `steady_60s`

| zone | owner | before | after |
|---|---|---:|---:|
| gfaydark | `sp.object_tris` | 5,257,648 | 5,257,616 |
| gfaydark | `sp.region_tris` | 2,840,736 | 23,568 |
| gfaydark | `sp.object_regions` | 229,408 | 229,408 |
| gfaydark | `sp.region_bounds` | 154,928 | 154,848 |
| dreadlands | `sp.region_tris` | 5,183,312 | 25,104 |
| dreadlands | `sp.object_tris` | 3,762,480 | 3,762,512 |
| dreadlands | `sp.region_bounds` | 334,976 | 334,992 |
| iceclad | `sp.region_tris` | 6,068,560 | 90,672 |
| iceclad | `sp.region_bounds` | 1,717,184 | 1,717,200 |
| iceclad | `sp.object_tris` | 337,376 | 337,376 |
| timorous | `sp.region_tris` | 8,674,160 | 74,656 |
| timorous | `sp.object_tris` | 3,756,192 | 3,756,192 |
| timorous | `sp.region_bounds` | 1,267,888 | 1,267,248 |

`sp.light_regions` and the unlabelled rest (`misc`) are below 14 kB in every
zone. The rows sum to the category (`owner=TOTAL`).

**`spatial` total, client, after the fix:**

| zone | before | after | budget | after % |
|---|---:|---:|---:|---:|
| gfaydark | 8,491,552 | 5,674,240 | 4,194,304 | **135.3 % OVER** |
| dreadlands | 9,352,912 | 4,194,576 | 4,194,304 | **100.006 % OVER (by 272 B)** |
| iceclad | 8,129,968 | 2,152,000 | 4,194,304 | 51.3 % |
| timorous | 13,762,720 | 5,162,576 | 4,194,304 | **123.1 % OVER** |

The after run's `--assert-budgets` check printed exactly these three
over-budget lines at both points ("72 category checks, 6 over budget").

## Part 2: the collision copy was real, and it is now held once

In a pooled zone (the `orangepi` preset's `mergeZoneDraws`),
`PlayerCameraComponent::setupMinimalZoneCollision()` copied every region's
triangles out of the zone draw pools into
`std::map<size_t, std::vector<triangle3df>>`. That is 36 B per triangle, plus a
vector and a map node per region, as a second copy of the pools' own vertices.
It was the largest `spatial` owner in three zones (2.8–8.7 MB) and more than
1 MiB in all four.

Readers: movement collision (`checkCollisionBspFiltered`), ground
(`findGroundZBspFiltered`, both phases), and the detail system's ground query
(`renderer_zone_load_env.cpp`, off under `WILLEQ_BASELINE`). Camera collision
uses the BSP directly, and zone lines use their own data, so neither reads the
copy. The pools stay in memory until `unloadZone()`. In pooled mode nothing
destroys them mid-visit: `/mergedraws` refuses, and eviction swaps a texture.

The fix is `RegionWorldTriangles` (`include/client/graphics/region_world_triangles.h`).
A pooled zone reads the pools in place: `ZoneMergeBuffers` is a
`RegionTriangleSource`, and the set holds only an 8 B-per-region index. The
legacy per-node path keeps owned vectors. Both `RayIntersectTriangles` overloads
run one shared Moller–Trumbore step on the same floats. `setSource()` drops the
player's region, which was built as a node first. Before this unit, the pooled
copy overwrote that region the same way.

- **Identical results.** `test_region_world_triangles`: 12,000 rays (half of
  them straight down, as the ground query casts them) against the pool view
  and against the pre-B909 copy built with the old code. Hit or miss, hit point,
  hit triangle and distance match bit for bit. The region set is the same,
  including a region with no triangles. All 356 collision, door, raycast,
  ground, zone-line, camera, movement-order and dead-member tests pass
  (`ctest -R "Door|Raycast|RayCast|ZoneLine|Ground|MovementOrder|DeadRenderer|Collision|Camera|RegionWorld|LOS|Los"`).
- **Held once.** `ownedTriangleBytes() == 0` for a pooled set. The census test
  `PooledCollisionSetupTakesTheViewAndMakesNoCopy` checks that the pooled branch
  calls `setSource(` and has no `emplace_back`/`setOwned(`. **RED** with the
  old copy restored in that branch: 3 failures, "the pooled branch copies
  triangles". The client now logs it, for example "Collision setup: 10203
  regions (total 133103 world-space triangles, 0 bytes held, read from the zone
  draw pools)" (iceclad, after).
- **Measured saving** (`sp.region_tris`): gfaydark −2,817,168, dreadlands
  −5,158,208, iceclad −5,977,888, timorous −8,599,504 B. The triangle counts
  in the log are unchanged: 73,660, 135,831, 133,103 and 210,248.
- No budget, golden or manifest changed.

**What is left over budget, and why it is not fixed here.**
- `sp.object_tris` (`ctx_.objectWorldTriangles`) is placeable objects'
  triangles, pre-transformed to world space per instance. It is 3.76–5.26 MB
  in gfaydark, dreadlands and timorous. Holding it once means transforming at
  query time, which changes the arithmetic, so results would not be
  bit-identical. That is outside this unit's STOP rule.
- `sp.region_bounds` is `std::map<size_t, aabbox3df>`, one node per region,
  1.72 MB in iceclad.

Both stay under B909 (issue #229, the client-spatial umbrella).

## Result 3: RssAnon against 64 MB and 256 MB

Peak = the maximum of the 2 s `/proc` samples and the lifecycle points.

| zone | runs | peak RssAnon (range over runs) | at `ready` | at `steady_60s` |
|---|---|---|---|---|
| gfaydark | before ×3, after ×1 | 238.9–357.4 MiB | 190.9–195.9 MiB | 238.9–357.4 MiB |
| dreadlands | before ×3, after ×1 | 210.6–215.2 MiB | 204.2–209.0 MiB | 210.6–215.2 MiB |
| iceclad | before ×3, after ×1 | 238.5–278.1 MiB | 234.1–273.5 MiB | 236.6–275.7 MiB |
| timorous | before ×3, after ×1 | 228.6–331.2 MiB | 220.9–267.4 MiB | 228.6–330.4 MiB |

(Computed from the two runs' `rss.tsv`. Per-run bytes are in `summary_before.md` and `summary_after.md`.)

**Every run is over the 64 MB target, by 146.6 MiB (dreadlands) to 293.4 MiB
(gfaydark). The 256 MB failing bound is exceeded by the peak in 8 of the 16
runs:** gfaydark 2 of 4, iceclad 3 of 4, timorous 3 of 4, dreadlands 0 of 4.

The bulk of it is `gpu_driver`, which is llvmpipe's pseudo-VRAM inside this
process: 140–206 MB at `ready`. Will's 2026-09-20 ruling excludes it from the
64 MB client budget, but it is RssAnon. It also GROWS after `ready` by
different amounts in different runs, up to 325,525,568 B at `steady_60s` in
gfaydark. In one smoke run (`smoke-2`, gfaydark) it grew about 0.9 MB per
frame for 50 s and plateaued at 2,416,143,488 B, with RssAnon 2.34 GB. That is
unexplained: B924 (issue #243). RssAnon on x86 is therefore not a usable yardstick
for the client's own bytes. The ledger categories are.

## Result 4: `zone_parse` call sites (trace, `steady_60s`)

The first non-allocator frame is resolved with `nm` to the nearest preceding
symbol, so an inlined or static function can show under its neighbour. For
example, "`BspRegion::pvsMemoryBytes`" for the 1-block PVS arena. Largest per
zone: iceclad PVS arena 4.83 MB, BSP nodes and slabs 3.61 MB, frag 0x21
2.11 MB. timorous: 2.13 / 1.86 MB, object parse 1.13 MB. gfaydark: object parse
0.45 MB, 0x2D 0.41 MB. dreadlands: PVS 1.60 MB, BSP 0.99 MB. The full lists are
in the summaries.

## Other findings

- **The owner-row table was too small for the live client.** It had 32 slots,
  filled in first-open order, and the startup, gfx and ent labels had used 30 of
  them before a zone loaded. The first smoke run printed gfaydark's 8.1 MB of
  `spatial` as `misc`. The table is now 64 slots.
  `MemoryLedger.ALateLabelStillGetsItsOwnOwnerRow` pins it (RED at 32:
  "label b909.lbl44 fell into the catch-all row").
- **`boot_zone` aborted under `set -u`.** `local a=$1 b=…${a}` expands `${a}`
  before it is set. The first smoke run hit this. B854's merge into `fable`
  fixed the same line independently in `zone_place_lib.sh`, which is now the
  only copy.
- **iceclad's zone link dropped in all four iceclad runs**: the client logs
  "Zone connection lost" 72–75 s after launch, and in the run inspected
  (`before/iceclad/zone_mesh`) UDP sends to the zone port were refused from
  about 53 s. It happened once in timorous too (the slow trace run). Sends are
  refused, and `/q` is then held "waiting for connection" until the 330 s
  deadline. The zone categories had loaded by then and did not move. It is
  recorded per run (`zone_link_lost=` in `result.txt`, and in the summaries).
  The cause is not investigated: B925 (issue #244).

## Orange Pi A/B (the merge gate for the collision change), 2026-09-29

Lane: the eqemu VM (the Pi reaches 172.16.200.99: 0.7 ms, and every leg zoned
in). Lock owner `b909-pi-ab` for both the eqemu and Pi locks. Rig:
`scripts/pi/collision_ab.sh` runs `scripts/pi_rendered_zone_sweep.sh` once
per leg, alternating A B / B A / A B, three rounds, zones timorous, iceclad,
growthplane and gfaydark. Every leg is identical: orangepi preset on
DRM/GLES2 at 1280x720, `WILLEQ_BASELINE=1`, `WILLEQ_LEDGER_OWNERS=spatial`,
level 60 (growthplane kicks lower levels; the snapshot restore puts it back),
the binary staged on the Pi's ext4 home and setcap'd, and the tour
`/frametiming`, `wait 240`, `/loc`, `/q`.

**Movement.** `/move`, `/jump` and `/turn` go through the game thread, which
takes ground from the zone map. The renderer's collision rays (the code this
unit changed) run only for KEYBOARD movement. So `scripts/pi/uinput_drive.py`
creates a `/dev/uinput` keyboard before the client starts and plays one fixed
sequence 10 s after `ready`: (W 3 s, Space, W 1 s, S 2 s, D 0.6 s, Space) ×5,
about 36 s. The same sequence runs in every leg. Proof that it drives the
rays: the client's "Player Movement" phase rises from ~190 µs idle to
~390–5,300 µs while the keys are held.

**Metric.** The client's `/frametiming` block (a 60-frame mean): "Total Frame"
and "Player Movement", the phase that holds the collision and ground rays.
"Moving" = blocks wholly inside the key sequence, "idle" = after it. Medians
are pooled over the three legs of a side.

Binaries (sha256 of the published, stripped ARM `bin/willeq`):

| | commit | sha256 |
|---|---|---|
| A (baseline) | fable `b1132c3f` (CI publish, `ci-arm/b1132c3f…`) | `477436330f14f53f23566c570a9e6673260d85e9ab3c91a0dfeccb00ce9eac4e` |
| B (held-once, std::function view) | `753955a1` | `a4eeb74085dbfe709fa35b5177a2f1c10dc84a9f5bf4cfddf0804982a7d3c820` |
| C (held-once, direct run loop) | `506f9de2` | `22e38e75af127fc5e083e5451b37dc3915170f54cbfe63a639aacf71e5352536` |

**A vs B** (`review/B909/pi-ab/table.md`, 07:12 → 08:41 UTC). Moving median
frame time, B vs A: gfaydark −0.01 %, growthplane **+3.76 %** (26.9 → 26.0 fps),
iceclad −20.9 %, timorous +1.69 %. Player Movement moving median, B − A:
+45, +124, +44 and +4 µs. growthplane crosses the 3 % rule, so the view was
changed: `RegionTriangleSource::regionRuns()` now hands out (pool vertices,
template indices, count) in batches of 16, and
`RegionTrianglesView::forEachTriangle` is an inlined index loop with no
per-triangle `std::function` call and no per-triangle `triangle3df`. The
Moller–Trumbore step takes the three vertices and builds the hit triangle
only on a hit. The arithmetic is identical. Identity and RED:
`test_region_world_triangles` passes 5 of 5, including the 12,000-ray
bit-identity test and the new `RunBatchesVisitTheSameTrianglesInTheSameOrder`
(37 runs, crossing two batch boundaries). Truncating the loop to one batch
fails the batch test. Swapping two vertices in the loop fails both tests.

**A vs C, the result that gates the merge** (`review/B909/pi-ac/table.md`,
08:41 → 10:09 UTC, 3 legs each, all 24 zone runs reached `steady_60s`):

| zone | side | moving frame µs (fps) | idle frame µs (fps) | Player Movement µs moving / idle | `spatial` @ready = @steady | peak RssAnon | RssAnon @steady |
|---|---|---:|---:|---:|---:|---:|---:|
| gfaydark | A | 33,301 (30.0) | 33,313 (30.0) | 392 / 192 | 8,273,728–8,274,152 | 102.6–103.5 MB | 97.0–98.3 MB |
| gfaydark | C | 33,304 (30.0) | 33,313 (30.0) | 435 / 191 | 5,519,064–5,519,240 | 99.5–100.9 MB | 94.4–98.4 MB |
| growthplane | A | 37,116 (26.9) | 35,764 (28.0) | 417 / 165 | 44,097,376–44,097,424 | 176.0–176.2 MB | 168.6–168.8 MB |
| growthplane | C | 37,943 (26.4) | 36,988 (27.1) | 476 / 165 | 40,709,192–40,709,216 | 172.7–172.8 MB | 163.6–165.5 MB |
| iceclad | A | 26,523 (37.7) | 26,328 (38.0) | 3,012 / 3,032 | 7,093,928–7,094,000 | 136.0–136.1 MB | 129.2 MB |
| iceclad | C | 33,318 (30.0) | 65,434 (15.3) | 3,067 / 1,298 | 1,533,840–1,533,944 | 130.5–135.1 MB | 112.3–124.1 MB |
| timorous | A | 38,010 (26.3) | 35,797 (27.9) | 5,016 / 1,994 | 12,906,968–12,906,984 | 163.0–165.8 MB | 150.9–158.1 MB |
| timorous | C | 35,888 (27.9) | 35,526 (28.1) | 5,346 / 5,346 | 4,673,984–4,674,008 | 161.1–165.4 MB | 145.5–157.4 MB |

(MB = 10^6 bytes, from the byte values in the table file.)

Verdict, C vs A:
- **gfaydark +0.01 %, growthplane +2.23 %, timorous −5.6 %**: inside the 3 %
  rule. No zone drops under 30 fps that A holds above it.
- **iceclad +25.6 % is not a binary effect.** iceclad's zone link drops in
  every run on both sides (B925, #244). After the reconnect, the client lands
  in one of three frame-rate modes, 15, 30 or 38 fps, per run and on either
  binary. In A vs B the sign was reversed: B −20.9 %, 38 fps against A's 30.
  Player Movement, which does not depend on those modes, is +55 µs (+1.8 %).
- **The collision phase itself**, C − A while moving: +43, +59, +55 and
  +330 µs. That is 0.13–0.87 % of A's frame. The remaining cost is reading
  36-byte pool vertices through an index list instead of a packed triangle
  array. timorous's +330 is partly a state that appears on both sides: in some
  runs (A r2, C r1 and r2) Player Movement stays at the moving level after the
  keys stop.
- **Memory on the Pi.** `spatial` A → C: gfaydark −2.75 MB, growthplane
  −3.39 MB, iceclad −5.56 MB, timorous −8.23 MB. Peak and steady RssAnon fall
  or overlap in every zone.
- **Already under 30 fps on A (pre-existing, not this change):** growthplane
  (26.9 moving) and timorous (26.3 moving). Under the owner's rule this is a
  client defect. It needs a number and is noted on the board.
- **The Pi's own `spatial` split (C, steady_60s):** growthplane
  `sp.object_tris` 40,188,128 B is by far the largest `spatial` owner on the
  Pi. It is the item already open under B909/#229 (object collision triangles
  pre-transformed per instance). The other zones:
  gfaydark `sp.object_tris` 5.23 MB, iceclad `sp.region_bounds` 1.14 MB,
  timorous `sp.object_tris` 3.74 MB + `sp.region_bounds` 0.84 MB.
  `sp.region_tris` is 12–49 kB in every zone.

Lock cleanup: every one of the 12 legs (plus both smoke legs) ended with
"locks released; eqemu lock now: ''; pi lock now: ''", and the snapshot was
restored byte-identically each time.

## Part 3: pinning it in CI terms

The gate cannot see the client-only owners (B923, issue #242).
`ClientLedgerLive` (`tests/cmake/commands.cmake`) is registered only under
`-DEQT_LIVE_TESTS=ON` with label `live`, so it is never in the default suite.
It runs `client_ledger_capture.sh --zones iceclad --modes spatial
--assert-budgets`, and `client_ledger_table.py check` asserts every budgeted
category at `ready` and `steady_60s`. Exit codes: 1 over budget, 3 a run
without a `steady_60s` point. The mechanism was exercised by the after run
(exit 1, the three `spatial` overruns). It was not run through `ctest` itself.
iceclad was chosen because its `spatial` is inside budget after the fix and it
is the zone with the most regions (37,609), so a regression there shows. Its
zone-link drop costs about 4 extra minutes per run.

## Tests

- Suite, as CI's `test` job runs it (Release `build`, `-LE live` because this
  tree registers the live tests): `ctest -j6 -E
  '(^visual_gate$|^RDPServerFrameTest\.UpdateFrameMultipleTimes$)'`
  **5044 of 5044 passed, 53 skipped.** The RDP case passed on its own
  `--repeat until-pass:3` run.
- Hygiene (`ctest -L hygiene` in a no-graphics tree, as CI does it): 16 of 16.

## Not verified

- The ledger TABLES in Results 1-4 are x86/llvmpipe only. The Pi was measured
  only for the collision change's A/B (spatial, RssAnon, frame time; section
  "Orange Pi A/B"), not for every budgeted category.
- `ClientLedgerLive` was not run through `ctest`. Its script was run with the
  same arguments except `--zones`.
- `archives` (+0.83 MB against the benchmark) and the client's
  `entity_models` excess (live NPCs against the benchmark's five test races)
  were not split.
- The gate itself (`visual_gate`) was not re-run. The benchmark code is
  untouched. The collision change is client-only and so are the labels.
