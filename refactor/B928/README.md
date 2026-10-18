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
full benchmark stdout, every `[memledger]` line per step), `after/<zone>.log`
(the same sweep after the early release), `trace/<zone>.log` (the
`WILLEQ_LEDGER_TRACE=zone_parse` run, the `[memledger-trace]` per-site
blocks), the sweep scripts, and `parse_trace.py`/`b928_sym.py` (offset
symbolization). Each table row below quotes the exact `[memledger]` field.

**Reproducing:** `scripts/ci/zone_parse_sweep.sh <benchmark> <eq-client>
<eqe1-pack> <out-dir> [extra flags]` re-takes every number in this document
(the five gate zones, the gate's own invocation, the `[memledger]` line per
point). It is a measurement tool — it prints, it never fails.

**CI status of this branch (recorded 2026-10-18, honest):** the full MR
pipeline is green at the CODE head `10c8b0a7` (pipeline 1724: hygiene,
build-headless, build-test, gate, zone-budget-census, build-arm — every
automatic job `success`; `arm64` and `client-ledger-live` are manual).
Everything pushed after that head is docs (this README, plus the
comment-only fix in `include/client/graphics/eq/s3d_loader.h`) plus the
standalone sweep script above, plus the `origin/fable` merge of already-merged
MRs — verified by `git diff 10c8b0a7..HEAD -- ':(exclude)refactor/*'
':(exclude)scripts/ci/zone_parse_sweep.sh'` naming only the other MRs' own
files and `include/client/graphics/eq/s3d_loader.h` (comment only). Since
~14:49 the GitLab instance had been creating MR-event pipelines as
**skipped with 0 jobs and `name: null`** for this MR (1727, 1730, 1733) and
other open MRs showed no head pipeline at all — an instance-level condition,
not this branch. **That condition cleared at ~17:59Z on 2026-10-18**
(pipeline 1750 on MR !243, `merge_request` event, `success`), and a fresh MR
pipeline at this head is created by the push of this commit — it is the
process gate this commit exists to close (the fix leg of QA t_e62ed92a /
review t_b1176af6, whose sole not-met row was the skipped pipeline 1735 at
the previous head). At the merged head `7b111635` the hygiene net (45/45) and
the B928 tests (`ZoneSourceRelease*`, `VisualGateLedgerAssertions`, 7/7) were
re-run through rbuild and pass.

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

## Step 2: the issue's named release, implemented — and what it moves

The issue says: "the peak is reduced in code (for example by releasing each
region's source geometry as its mesh is built instead of at the end)". Commit
`10c8b0a7` implements the named shape: `S3DZone::releaseRegionGeometryEarly()`
(`include/client/graphics/eq/s3d_loader.h`, `src/client/graphics/eq/s3d_loader.cpp`)
releases the geometry-only half of B914's end-of-load release in Step 4, after
the occluder pass (the last build consumer of the per-region source geometry);
the benchmark mirrors the client (the occluder triangles moved to a new Step 7c
before the objects build, and the source geometry is released there).
`tests/test_zone_source_release.cpp` pins the ordering (early release after the
last source read, before the objects/entities peak), that the early release
frees every region's geometry and keeps the BSP tree and lights, and that the
end-of-load release afterwards is a safe no-op for the geometry half.

Measured before/after, the same gate invocation for the five zones (raw logs
`b928-sweep/base/<zone>.log` vs `b928-sweep/after/<zone>.log`, every `[memledger]`
line per point; the process peak is the `ledger_peak` field — the ledger's
whole-process high-water — and `owned` is the 64 MB target figure, RssAnon
minus gpu_driver):

| zone     | process peak (ledger_peak) before | after  | reduction  | owned before | owned after |
|----------|-----------------------------------|--------|------------|--------------|-------------|
| qeynos2  | 129,260,096                       | 117,449,008 | 11,811,088 | 12,462,176 | 9,828,496 |
| gfaydark | 124,544,176                       | 118,787,792 | 5,756,384  | 16,389,632 | 10,650,896 |
| befallen | 120,093,792                       | 116,367,424 | 3,726,368  | 11,927,072 | 8,183,344 |
| iceclad  | 208,159,888                       | 192,127,872 | **16,032,016 (15.3 MiB)** | 74,514,880 | 58,717,088 |
| timorous | 200,287,296                       | 183,933,024 | **16,354,272 (15.6 MiB)** | 64,771,808 | 48,433,136 |

That is the whole-process load peak — the memory that gets the process killed —
down 15.3 MiB in iceclad and 15.6 MiB in timorous. The process peak is set at
the `objects` point in iceclad and timorous and at the `entities` point in the
three small zones (the rows above quote the peak point's `ledger_peak` per
zone, and the `owned` column quotes it at the same point); at that
point the `zone_parse` live is now the retained half only:

| zone     | zone_parse live at the peak point (before) | after   |
|----------|----------------------------------|---------|
| iceclad  | 31,669,664                       | 12,848,688 (−18.8 MiB released early) |
| timorous | 25,870,064                       | 6,493,776 (−18.4 MiB released early) |
| qeynos2  | 8,034,080                        | 5,433,008 |
| gfaydark | 7,824,160                        | 2,066,448 |
| befallen | 5,470,960                        | 1,745,328 |

The released per-region source geometry (~18.8 MiB of iceclad's 31.7) no longer
coexists at the process high-water with the built meshes (`zone_mesh` 19.0 MiB)
and the objects/entities/model build on top of it. (The base sweep's benchmark
mirrored the OLD client — release at end of load — so its process peak IS the
old client's; the after sweep mirrors the new one.) Rendering is unchanged: the
gate's goldens are byte-identical (pipeline 1724 `gate` job, the `visual_gate`
over all five manifest zones) and the pooled draw path reads only built meshes +
BSP + lights after the release.

The `zone_parse` CATEGORY peak is not moved, exactly as the mechanism section
predicts — it is set at the end of the WLD parse, before any mesh is built:

| zone     | zone_parse category peak before | after   |
|----------|---------------------------------|---------|
| qeynos2  | 8,182,160                       | 8,181,616 |
| gfaydark | 9,860,272                       | 9,860,272 |
| befallen | 7,627,056                       | 7,627,056 |
| iceclad  | 50,058,816                      | 50,058,784 |
| timorous | 38,527,776                      | 38,527,872 |

(±32 B is allocator run-to-run noise.) The end-of-run `zone_parse` live is
identical before/after (11,326,576 in iceclad at `ready`) — Step 13 still runs
and its geometry half is a no-op the second time, so nothing rises at the point
the gate already checked.

So this unit moves the process peak in code (the number the issue says is what
kills the process), and Step 3 gates the category peak. The category peak
itself — the 50.1 MB figure in the issue title — is not reachable by ANY
post-parse release (the mechanism section above names why: decompressed WLD
buffer + freshly extracted geometry coexist for the whole parse), and its only
lever is streaming the decompression, a parse-algorithm change that is
FOLLOW-UP, not substituted.

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

## Step 4: the Pi (lane 1) `zone_parse` peak and process peak — before/after

Measured 2026-10-18 on the Orange Pi (lane 1, 10.0.30.192, armhf, lima),
iceclad, `--drm`, 300 frames, `pi_lock_take`/`release` (left as found),
`BENCH_EXIT=0` both runs. "Before" = the MR pipeline's `build-arm` publish of
`53135e2d` (the head before the release landed); "after" = the publish of
`10c8b0a7` (this head). Same binary type (`gles2_zone_benchmark-nollvm`, the
shipped Mesa prefix), same data root, same invocation. Evidence:
`~/kb-evidence/kb-b928-zone-parse-peak/pi/pi_iceclad_before.log` /
`pi_iceclad_after.log` (the full `[memledger]` lines per point).

| platform | zone_parse live @ zone_parse | zone_parse PEAK (category) | process peak (ledger_peak) |
|----------|------------------------------|----------------------------|----------------------------|
| Pi, before (53135e2d) | 27,523,872 | 45,662,496 | 150,656,256 @ zone_load_complete |
| Pi, after (10c8b0a7)  | 27,523,872 | 45,662,496 (unchanged, the parse-time high-water) | **134,583,440 @ zone_load_complete (−16,072,816, 15.3 MiB)** |
| x86 gate (this head) | 31,670,464 | 50,058,784 | 192,127,872 @ objects (was 208,159,888) |

`peak − live` on BOTH platforms is the decompressed WLD buffer
(`[wldparse] bytes=18191448`, 17.3 MiB), confirming the mechanism holds on the
device the peak matters on. The Pi's zone_parse live at the objects point
dropped from 27,523,872 to 11,450,992 (−16.1 MiB — the early-released region
geometry) in the after run, and the process peak — the memory that gets the
process killed — dropped 15.3 MiB, matching the x86 sweep (15.3 MiB) within
noise. The category peak is unchanged on the Pi, exactly as the mechanism
section predicts (it is set at the end of the WLD parse, before any mesh is
built). The Pi `owned` figure at zone_load_complete ran slightly higher in the
after run (176.6 vs 174.4 MiB) — that is the `gpu_buffers` row
(`drm_maps_lower_bound`, GEM shmem below RssAnon) run-to-run variation, not a
load difference: every `zone_parse`/`zone_mesh`/`spatial`/`objects` live field
is identical or lower.

## BLESS REQUEST (measured post-fix peaks per zone)

The measured `zone_parse` PEAK per gate zone AT THIS HEAD (the "after" sweep,
`b928-sweep/after/<zone>.log` — the value Will would bless as the per-zone
`zoneParsePeakBudget`). The category peak is the parse-time high-water (the
decompressed WLD buffer + the freshly extracted geometry), which this unit's
release does not move — so the post-fix peaks are the before peaks within
allocator noise (±32 B); they are quoted from the after sweep as the state
this head leaves:

| zone     | zone_parse peak (bytes) | zone_parse peak (MiB) |
|----------|-------------------------|-----------------------|
| qeynos2  | 8,181,616               | 7.8                   |
| gfaydark | 9,860,272               | 9.4                   |
| befallen | 7,627,056               | 7.3                   |
| iceclad  | 50,058,784              | 47.7                  |
| timorous | 38,527,872              | 36.8                  |

Until a budget is blessed the gate PRINTS these peaks and does not fail
(`[ledger-peak] … NO BUDGET BLESSED`); the existing `zone_parse` LIVE budget
(14 MiB) still gates the end-of-run value, which is unchanged by this unit.
(The manifest's `memoryCoverage` section is untouched by this unit.)

## Not done in this unit (honest)

- The `zone_parse` CATEGORY peak (50.1 MB in iceclad, the issue's headline
  number) is not reduced by this unit: it is set at the end of the WLD parse,
  before any mesh is built, and no post-parse release can lower it. The
  process peak IS reduced in code (Step 2 table: −15.3 MiB iceclad, −15.6 MiB
  timorous) — that is the memory that gets a process killed. The category
  peak's only lever is streaming the WLD decompression, a parse-algorithm
  change with rendering-correctness risk across every fragment — FOLLOW-UP
  below, not substituted. Step 3 still gates the category peak against a
  budget (the mid-load high-water the gate previously never saw), and the
  BLESS REQUEST below names the measured post-fix peaks.

## FOLLOW-UP

- The `zone_parse` peak reduction (streaming WLD decompression) is the
  acceptance-(B) leg this unit scoped out: implement it, then re-run the
  five-zone x86 sweep and the Pi lane-1 iceclad run (the "before" numbers
  are the Step 4 table) and quote the before/after.
