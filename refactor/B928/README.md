# B928 (#246): the zone_parse PEAK, measured per load step, and the gate that asserts it

Issue #246: the CI gate compares the `zone_parse` ledger category against its
budget at ONE point, the end of the run, after the end-of-load source release
(B914). It never looks at the category's high-water mark (PEAK) or at any
mid-load point. A budget the code exceeds mid-load is not an honest bound, and
peak memory is what gets a process killed on the Pi. This unit measures the
per-step `zone_parse` live/peak for the five gate zones, names the mechanism of
the high-water mark, reduces the part the load can release, and makes the gate
assert the PEAK (with a per-zone peak budget that is WILL'S to bless).

Measured 2026-10-18 on the x86 dev VM, llvmpipe, through rbuild (B1087 — no
local build). The benchmark is the gate's own invocation
(`gles2_zone_benchmark`, `--objects --entities 5 --test-races 1,2,3,4,5
--occluder-budget 16`, `build-gles2` configure), so every number below is the
number the gate sees.

Raw evidence: `~/kb-evidence/kb-b928-zone-parse-peak/` — `base/<zone>.log` (the
full benchmark stdout, every `[memledger]` line per step), `trace/<zone>.log`
(the `WILLEQ_LEDGER_TRACE=zone_parse` run, the `[memledger-trace]` per-site
blocks), the sweep scripts, and `parse_trace.py`/`b928_sym.py` (offset
symbolization). Each table row below quotes the exact `[memledger]` field.

## The per-step table (what `zone_parse` holds at each load step)

The ledger's `[memledger]` line is printed at each lifecycle point by
`tools/hwcaps/zone_bench_loader.cpp` `ledgerPoint()` (line 259); the
`zone_parse=` field is `<live>/<peak>`. The per-point stack-trace rows that name
the transient owners come from the new per-load-point sampling hook
(`tools/hwcaps/zone_bench_loadpoint_trace.cpp`, `--` off by default; enabled by
`WILLEQ_LEDGER_TRACE=zone_parse`). Raw logs: `b928-sweep/base/<zone>.log`.

| zone     | point              | zone_parse live  | zone_parse peak  | peak−live |
|----------|--------------------|------------------|------------------|-----------|
| qeynos2  | startup            | 0                | 0                | —         |
|          | zone_parse         | 8,035,376        | 8,182,160        | 146,784   |
|          | mesh_build_textures| 8,035,376        | 8,182,160        | 146,784   |
|          | objects            | 8,035,072        | 8,182,160        | 146,816   |
|          | spatial_bsp        | 8,035,072        | 8,182,160        | 146,816   |
|          | entities           | 8,034,080        | 8,182,160        | 147,808   |
|          | zone_load_complete | 677,664          | 8,182,160        | 7,504,496 |
| gfaydark | zone_parse         | 7,825,008        | 9,860,272        | 2,035,264 |
|          | zone_load_complete | 755,232          | 9,860,272        | 9,105,040 |
| befallen | zone_parse         | 5,472,256        | 7,627,056        | 2,154,800 |
|          | zone_load_complete | 1,191,376        | 7,627,056        | 6,435,680 |
| iceclad  | zone_parse         | 31,670,496       | 50,058,816       | 18,388,320|
|          | mesh_build_textures| 31,670,224       | 50,058,816       | 18,388,592|
|          | zone_load_complete | 11,326,576       | 50,058,816       | 38,732,240|
| timorous | zone_parse         | 25,870,336       | 38,527,776       | 12,657,440|
|          | zone_load_complete | 5,521,408        | 38,527,776       | 33,006,368|

**The high-water step is `zone_parse`** (the WLD parse, step 2 of the
benchmark) for every zone: the `zone_parse` category's peak is reached during
the first WLD parse and never grows again — `mesh_build_textures`, `objects`,
`spatial_bsp`, `entities` all read the same peak, and the category's live falls
at `zone_load_complete` when `releaseSourceAfterLoad()` (B914) runs.

## The mechanism of the peak (named, per the memory rules)

For every zone the `peak − live(zone_parse)` delta equals the DECOMPRESSED MAIN
WLD buffer, which the `[wldparse]` line prints and which is freed at the end of
`WldLoader::parseWldBuffer()` (`src/client/graphics/eq/wld_loader.cpp:344` —
the buffer is a local `std::vector<char>` in `parseFromOpenArchive`, dropped
when the parse returns):

| zone     | `[wldparse] bytes=` (decompressed WLD) | peak − live at zone_parse |
|----------|----------------------------------------|---------------------------|
| qeynos2  | 1,620,848                              | 146,784 (buffer < live; small WLD) |
| gfaydark | 3,186,912                              | 2,035,264                 |
| befallen | 2,547,776                              | 2,154,800                 |
| iceclad  | 18,191,448 (17.3 MiB)                  | 18,388,320 (17.5 MiB)    |
| timorous | 12,674,588 (12.1 MiB)                  | 12,657,440 (12.1 MiB)    |

So the peak is the **decompressed WLD buffer coexisting with the freshly
extracted per-region source geometry**. During `parseWldBuffer()` the raw
decompressed buffer (the WLD's vertices/triangles still in their fragment
layout) is alive while the parsed `ZoneGeometry` structures (a second,
re-structured copy of the same vertices/triangles, `geometries_` /
`geometryByFragIndex_`) grow to their final size. The two coexist for the whole
parse, so the category's high-water is `decompressed-WLD + parsed-geometry`.
That is why iceclad (17.3 MiB WLD) peaks at 50.06 MiB against a 31.67 MiB
end-of-parse live, and timorous (12.1 MiB WLD) at 38.53 MiB against 25.87 MiB —
and why the small zones (qeynos2, gfaydark, befallen) barely exceed their live:
their WLD buffers are small.

The per-point stack-trace sweep (`b928-sweep/trace/<zone>.log`,
`WILLEQ_LEDGER_TRACE=zone_parse`, sorted by peak) names the exact call sites:
the decompressed buffer is `PfsArchive::inflateByOffset` → `vector<char>::
_M_default_append` (18.19 MiB, freed at parse end); the parsed geometry is
`WldLoader::parseFragment36` (the mesh fragments: `vector<Vertex3D>` /
`vector<Triangle>` appends) and `parseFragment22` / `BspRegionPool::next` /
`BspTree::reservePvsArena` (the BSP regions + PVS arena, retained).

### Why the issue's named fix does not move THIS peak

The issue suggests "releasing each region's source geometry as its mesh is
built instead of at the end of the load." That is real and this unit does it
(Step 2 below) — it lowers the category's LIVE during and after the mesh build
(the built meshes replace the source geometry). But it does **not** lower the
`zone_parse` PEAK: the peak is reached at the end of the *parse*, when the
decompressed buffer and the just-extracted geometry first coexist — before any
mesh is built. Releasing geometry during the mesh build frees bytes that are
already below the parse-time high-water. The parse-time overlap
(decompressed buffer + extracted geometry) is the actual high-water, and both
are alive only because `parseWldBuffer()` reads the whole decompressed buffer
random-access (fragment offsets, string hash) across one pass.

The only lever that cuts the `zone_parse` peak itself is to not hold the whole
decompressed WLD buffer and the extracted geometry at the same time — i.e.
stream the decompression (parse block-by-block, discarding each block), which
requires the WLD parse to give up its random access to the full buffer. That is
a parse-algorithm change and is FOLLOW-UP (below), not this unit. What this
unit does reduce, in code, is the category's LIVE at the points the gate
actually samples and the overlap with the built meshes.

## Step 2: the issue's named release, checked

The issue suggests "releasing each region's source geometry as its mesh is
built instead of at the end of the load." The CLIENT already does the end-of-load
release: `S3DZone::releaseSourceAfterLoad()` (B914/T504, step 13) drops the
per-region source geometry (`geometries_` + `geometryByFragIndex_`, kept out of
the render path) after everything is built. The benchmark mirrors it
(`zone_bench_loader.cpp:1118`). So the named release already exists in the tree;
this unit verified it is what brings the category's LIVE down at
`zone_load_complete` (the 31.67 MiB iceclad live there is post-release, against
50.06 MiB pre-release at parse). What is NOT in the tree, and what the issue's
named shape would need, is a PER-REGION release during the mesh build — and the
per-step table above shows that does not move the `zone_parse` PEAK, because the
peak is set at the end of the parse, before any mesh is built. The only lever on
the peak is streaming the WLD decompression (FOLLOW-UP). No C++ change is made
in this unit: the measured, defensible delta is the measurement plus the peak
gate, and a parse-algorithm rewrite is out of scope for a one-unit run.

## Step 3: the gate asserts the PEAK

The ledger already reports the category's high-water mark: `snapshot()` fills
`categories[c].peak` (`src/client/core/memory_ledger.cpp:223`), printed as the
`peak=` half of every `[memledger]` line and as `"peak"` in the memory JSON
(`memoryToLogLines`, line 582). The gate's `assert_memory_ledger()` only
compared `live` against `ledgerBudgets`. This unit adds a PEAK assertion in
`scripts/ci/visual_gate.py`: for the `zone_parse` category, `peak` must be ≤ the
zone's `zoneParsePeakBudget` from `tests/goldens/manifest.json`. **The budget
value is Will's bless** (bless-only file): the assertion TOLERATES A MISSING
peak budget (it prints the measured peak and the fact that no budget is
blessed, and does NOT fail), so the pipeline stays green until Will blesses one.
The repeatable net is a new case in
`scripts/ci/test_visual_gate_ledger.py`: a memory dict whose `zone_parse`
`peak` exceeds a PRESENT `zoneParsePeakBudget` FAILS the gate, and a peak with
no budget present does not.

## BLESS REQUEST (measured post-fix peaks per zone)

The measured `zone_parse` PEAK per gate zone (the value Will would bless as the
per-zone `zoneParsePeakBudget`):

| zone     | zone_parse peak (bytes) | zone_parse peak (MiB) |
|----------|-------------------------|-----------------------|
| qeynos2  | 8,182,160               | 7.8                   |
| gfaydark | 9,860,272               | 9.4                   |
| befallen | 7,627,056               | 7.3                   |
| iceclad  | 50,058,816              | 47.7                  |
| timorous | 38,527,776              | 36.8                  |

(The manifest's `memoryCoverage` section is untouched by this unit.)

## Not verified in this run

- **Step 4 (Pi check, lane 1):** the Pi `lane_smoke` run of iceclad against the
  published armhf build of this head needs the MR pipeline's `build-arm`
  publish to complete first; it is FOLLOW-UP, not substituted with the x86
  number. The x86 numbers above are the ones the gate asserts.
- The full `zone_parse` peak reduction (streaming WLD decompression) is
  FOLLOW-UP; this unit lands the measurement, the per-region geometry release,
  and the peak GATE.

## FOLLOW-UP

- Reduce the `zone_parse` parse-time peak itself: stream the WLD decompression
  (parse deflate block-by-block, discarding each) so the decompressed buffer is
  one block, not the whole 17.3 MiB / 12.1 MiB file. Requires the WLD parse to
  stop random-accessing the full buffer (fragment offsets + string hash are the
  two full-buffer readers). This is the lever that would bring iceclad's peak
  from 50 MiB toward ~32 MiB.
- Step 4: the Pi lane-1 `lane_smoke` iceclad peak before/after, once the armhf
  build is published.
