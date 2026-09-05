# T387 — the two hwcaps tool files under the ceiling (B167, hwcaps half)

Class P (structural), slot3, no server. Read B167, the K/L mechanism
(`refactor/K_component_extraction.md` §Mechanism), and T385's plan (the
sibling unit decomposing `model_viewer`; T385 also extends
`check_file_sizes.sh --all` to `tools/` — do NOT touch that script or the
allowlist here; if your branch merges first the check still covers only
src/include/tests, and that is fine).

Files: `tools/hwcaps/zone_bench_loader.cpp` (3,280) and
`tools/hwcaps/gles2_zone_benchmark.cpp` (3,060). These are the visual
gate's benchmark (`build-gles2/bin/gles2_zone_benchmark`) — the gate is
the behaviour proof.

1. Read both first; write the split plan into the Review BEFORE editing
   (functional seams: CLI/config, zone loading steps, memory accounting
   (`zone_bench_memory.h` already exists), the frame loop / capture, the
   report writer). Extract by function into `tools/hwcaps/<part>.cpp` +
   headers; each part ≤ 1,500; `main` tiny. One commit per part.
2. Behaviour proof: the visual gate run BEFORE the first edit (record its
   manifest / metrics JSON and the golden comparison) and AFTER the last —
   pixels identical (goldens untouched — a moved golden is a STOP, never
   bless), memory categories identical to the byte, CPU p95 within the
   ceiling (load artefacts re-run standalone). Also `--help` output
   byte-identical.
3. CMake: the tool target lists its new sources; both `build` and
   `build-gles2` build it.
4. Annotate B167 (hwcaps half done, hashes); T385 strikes it when the
   check covers tools/.

## Proof

`build`, `build-gles2` (both trees), `ctest -j6`, the visual gate before
and after, `check_file_sizes.sh --files` on every new file, ARM build if
the benchmark is in the ARM tree (check `scripts/build-arm-noble.sh`).

STOP: a pixel golden moves; a memory category changes by even one byte.

## Review

### Split plan (written BEFORE the first edit, per the plan's step 1)

Base: `0e430393` (fable tip), branch `task/T387-hwcaps-decomposition`, slot3.

Both files are pure tool code with no `class` to decompose, so the K/L
"component" shape does not apply literally: the seam is by FUNCTION FAMILY,
each family becoming its own translation unit behind the existing public
header. No function body is rewritten — every extraction is a verbatim move
plus the include set the new TU needs.

#### A. `tools/hwcaps/zone_bench_loader.cpp` (3,280) → five TUs

`zone_bench_loader.h` (493) is already the module's public interface and
declares every one of these functions; it is NOT split (it is under the
ceiling and it is the shared `ZoneBenchData`/`BenchConfig`/`DrawEntry` type
header the other tools include). One new INTERNAL header carries the two
file-static helpers that two of the new TUs both need.

| new TU | contents (source lines at base) | est. |
|---|---|---|
| `zone_bench_loader.cpp` (kept) | `applyEntityShaderMaterialPromotion`, `~ZoneBenchData`, `createDevice`, `uploadMeshHWBuffers`, `loadZone` (46–987) | ~990 |
| `zone_bench_draw.cpp` (new) | `estimateRegionScreenAreaBench`, `computeLightEffectiveRadiusBench`, `collectDrawEntries`, `collectObjectDrawEntries`, `collectEntityDrawEntries` (1016–1394, 1448–1590) | ~600 |
| `zone_bench_entities.cpp` (new) | `placeEntitiesAlongPath`, `renderBatchedEntities`, `renderDirectGLEntities`, `renderDirectGLCompositeEntities`, `renderCompositeEntities`, `placeEntitiesTrain` (1395–1447, 1591–1691, 1892–2232, 2390–2438) | ~640 |
| `zone_bench_lod.cpp` (new) | `renderLodEntities`, `getOrCreateImpostor`, `renderAdaptiveLodEntities` (2233–2389, 2439–2813) | ~560 |
| `zone_bench_regions.cpp` (new) | `pointToSegmentDistSq`, `segmentToSegmentDistSq`, `analyzePortals`, `PlaneGrid`, `RegionBoundary`, `findRegionBoundariesRecursive`, `detectPortalsFromOccluders`, `findSurfaceCandidates`, `findSurfacePosition` (1692–1891, 2814–3279) | ~700 |
| `zone_bench_draw_util.h` (new) | `textureGroupId` + `debugMaterialOverride`, both `static` today and both needed by `zone_bench_draw.cpp` AND `zone_bench_entities.cpp` — made `inline` in an internal header, bodies unchanged | ~60 |

#### B. `tools/hwcaps/gles2_zone_benchmark.cpp` (3,060) → four TUs

`main()` alone is 1,713 lines at base (1348–3060). Its early-return "mode"
blocks are already self-contained (each touches only `opts`, `data`, and —
for the static-pose block — `metrics`, and each ends in `return`), so each
becomes a function returning `int` that `main` forwards.

| new TU | contents (source lines at base) | est. |
|---|---|---|
| `zone_bench_options.h/.cpp` (new) | `struct Options` (66–337, to the header), `printUsage`, `parseArgs` (338–686) | h ~290 / c ~360 |
| `zone_bench_frame.h/.cpp` (new) | `TracePose`, `PathFilePose`, `loadPathFile`, `writePathFile`, `setupLighting`, `setupCamera`, `renderDrawEntries`, `countPolygons`, `countMaterialFamilies`, `glFilterName`, `dumpFilterState`, `poseToDirection`, `computeFrameStats`, `runSyntheticDrawTest` (687–1345) | h ~90 / c ~700 |
| `zone_bench_modes.h/.cpp` (new) | `runTreeTest` (main 1563–1925), `runCompositeEntityTest` (1927–2138), `runPoseSearch` (2343–2457), `runStaticPoseCapture` (2479–2596) — the four early-return modes, each `int f(ZoneBenchData&, const Options&[, MetricsCollector&])` | h ~40 / c ~850 |
| `gles2_zone_benchmark.cpp` (kept) | option post-processing, zone load, camera/trace setup, the measured benchmark loop, CSV/memory output | ~900 |

#### CMake

Three targets compile `zone_bench_loader.cpp` (`gles2_zone_benchmark`,
`gles2_shader_diagnostic`, `gles2_composite_audit`); all three get the four
new loader TUs. The three benchmark-only TUs go on `gles2_zone_benchmark`
only.

#### Behaviour proof

The visual gate is the oracle. Recorded BEFORE the first edit and re-run
after the last commit; the comparison is byte-level over
`build-gles2/ci_artifacts/` (9 golden PNGs, 3 metric CSVs, 6 memory JSONs)
plus `--help` stdout. Goldens under `tests/goldens/` are never written.

#### STOP conditions honoured

A moved pixel golden or a memory category differing by one byte is a STOP
with the contact sheet, never a bless.
