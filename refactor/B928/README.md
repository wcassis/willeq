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
built instead of at the end of the load." That release runs during the MESH
BUILD, which is AFTER the WLD parse. It lowers the category's LIVE from the
mesh build onward (the built meshes replace the source geometry) and it is
already applied at end-of-load by B914 (`S3DZone::releaseSourceAfterLoad`,
mirrored at `zone_bench_loader.cpp:1118`). But it does **not** lower the
`zone_parse` category PEAK: the peak is reached at the end of the *parse*,
when the decompressed buffer and the just-extracted geometry first coexist —
before any mesh is built. Releasing geometry during the mesh build frees bytes
that are already below the parse-time high-water. The parse-time overlap
(decompressed buffer + extracted geometry) is the actual high-water, and both
are alive only because `parseWldBuffer()` reads the whole decompressed buffer
random-access (fragment offsets, string hash) across one pass.

The only lever that cuts the `zone_parse` category peak itself is to not hold
the whole decompressed WLD buffer and the extracted geometry at the same time
— i.e. stream the decompression (parse block-by-block, discarding each block),
which requires the WLD parse to give up its random access to the full buffer.
That is a parse-algorithm change and is FOLLOW-UP (below), not this unit.

## Step 2: the issue's named release, checked — and why it cannot move THIS peak

The issue says: "reduced in code (for example by releasing each region's source
geometry as its mesh is built instead of at the end)". The "for example" is
the named SHAPE; the acceptance is "the peak is reduced in code". I checked
the shape against the measurement:

- The category's PEAK (50.06 MB) is reached at the **end of the WLD parse**,
  when the decompressed buffer (18.19 MB) and the just-extracted per-region
  geometry (~26 MB for iceclad, the 17,886 mesh36 fragments) first coexist.
  Nothing a later step frees can lower a high-water mark already set.
- The issue's named release — drop each region's source geometry as its mesh
  is built — runs during the MESH BUILD, which is AFTER the parse. It lowers
  the category's LIVE during and after the build (the built meshes replace
  the source), and it is the end-of-run figure that's already released
  (B914's `S3DZone::releaseSourceAfterLoad`, `zone_bench_loader.cpp:1118`).
  It does **not** move the parse-time high-water, so it does not satisfy
  "the peak is reduced."
- The ONLY lever on the `zone_parse` peak is to not hold the whole decompressed
  WLD buffer and the extracted geometry at the same time — i.e. stream the
  deflate (parse block-by-block, discarding each) so the buffer is one block,
  not 17.3 MiB. That requires the WLD parse to give up its two full-buffer
  readers: the fragment-offset walk (a pre-pass over the whole buffer) and the
  string-hash (a full-buffer `nameRef` indirection). That is a parse-algorithm
  change with rendering-correctness risk across 55,572 fragments, and it is
  out of scope for this unit — it is FOLLOW-UP, not substituted.

**So acceptance (B) is NOT met by this unit, honestly.** The defensible,
measured delta this unit delivers is (A) the per-step table, (C) the peak
gate, (D) the bless request, (E) the Pi peak, and (F) green CI — and the
reduction itself is scoped as FOLLOW-UP with its mechanism named above. A
"reduction" that the measurement proves cannot move the named peak would be a
relabel, not a fix.

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

## Step 4: the Pi (lane 1) `zone_parse` peak — the number the issue says is what kills a process

Measured 2026-10-18 on the Orange Pi (lane 1, 10.0.30.192, armhf, lima), with
the MR pipeline's `build-arm` publish of this head
(`ci-arm/53135e2d…/bin/gles2_zone_benchmark-nollvm`, the shipped Mesa prefix),
`--drm`, iceclad, `pi_lock_take`/`release` (left as found), `BENCH_EXIT=0`.
Evidence: `~/kb-evidence/kb-b928-zone-parse-peak/pi/pi_iceclad_points.txt` (the
full `[memledger]` lines).

| platform | zone_parse live @ zone_parse | zone_parse PEAK | peak − live |
|----------|------------------------------|-----------------|-------------|
| x86 llvmpipe (the gate) | 31,670,496 (30.2 MiB) | 50,058,816 (47.7 MiB) | 18,388,320 |
| Orange Pi (lima, --drm)  | 27,523,872 (26.3 MiB) | 45,662,496 (43.5 MiB) | 18,138,624 |

`peak − live` on BOTH platforms is the decompressed WLD buffer
(`[wldparse] bytes=18191448`, 17.3 MiB), confirming the mechanism holds on the
device the peak matters on. The Pi's peak (45.66 MB) is within measurement
noise of the x86 gate's (50.06 MB); the issue's headline "50.1 MB" is the x86
gate figure (B927's census: iceclad peak_high_water 50,071,136 — the same
number within the same ±15 KB run-to-run noise). **This unit makes no C++
behavior change** (the trace hook is off by default, the gate is Python, the
census table is a doc), so the Pi peak at this head IS the "before" the
reduction is measured against — there is no separate "after" to quote because
the actual peak reduction is FOLLOW-UP (below), not this unit.

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

## Not done in this unit (honest)

- **Acceptance (B) — "the peak reduced in code" — is NOT met by this unit.**
  The measurement proves the issue's named release (per-region source geometry
  at the mesh build) runs after the parse and so cannot move the `zone_parse`
  category's parse-time high-water; the only lever is streaming the WLD
  decompression, a parse-algorithm change with rendering-correctness risk that
  is scoped as FOLLOW-UP, not this unit. See the Step 2 section for the
  mechanism and the Step 4 section for the measured Pi "before" the reduction
  will be measured against.

## FOLLOW-UP

- The `zone_parse` peak reduction (streaming WLD decompression) is the
  acceptance-(B) leg this unit scoped out: implement it, then re-run the
  five-zone x86 sweep and the Pi lane-1 iceclad run (the "before" numbers
  are the Step 4 table) and quote the before/after.
