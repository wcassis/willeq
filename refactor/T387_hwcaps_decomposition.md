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

### Result

Nine new TUs; both target files under the ceiling. No function body was
rewritten — every extraction is a verbatim move. Deviations from the plan
above: `renderBatchedEntities` went to the entity TU rather than the draw TU
(it renders, it does not collect), and the small path-file family was folded
into `zone_bench_frame` rather than getting a TU of its own (90 lines does not
earn one).

| file | before | after |
|---|---|---|
| `tools/hwcaps/zone_bench_loader.cpp` | 3,280 | **977** |
| `tools/hwcaps/zone_bench_draw.cpp` | — | 581 |
| `tools/hwcaps/zone_bench_entities.cpp` | — | 593 |
| `tools/hwcaps/zone_bench_lod.cpp` | — | 587 |
| `tools/hwcaps/zone_bench_regions.cpp` | — | 708 |
| `tools/hwcaps/zone_bench_draw_util.h` | — | 41 |
| `tools/hwcaps/gles2_zone_benchmark.cpp` | 3,060 | **992** |
| `tools/hwcaps/zone_bench_options.h` / `.cpp` | — | 290 / 364 |
| `tools/hwcaps/zone_bench_frame.h` / `.cpp` | — | 75 / 697 |
| `tools/hwcaps/zone_bench_modes.h` / `.cpp` | — | 26 / 867 |

`main()` fell from 1,713 lines to 227.

Commits: `0d5014a0` (this plan), `4b5a697c` draw, `b607423d` entities,
`cb1f04d9` lod, `ec01db71` regions, `da9dd157` options, `8bf3b1c9` frame,
`39131bf6` modes, plus a final commit for the stale comment pointers, the
board and this section.

### Behaviour proof — visual gate before/after

Both runs `=== VISUAL GATE: PASS ===`, exit 0. Before: base `0e430393` binary,
built and run before the first edit. After: the seven-commit branch tip.
`git status tests/goldens/` clean both times — nothing was blessed.

| compared | result |
|---|---|
| 9 golden PNGs (`qeynos2/gfaydark/befallen` × f0/f1/f2) | **byte-identical** (`cmp`, all nine) |
| 6 memory JSONs, every field except `processRssBytes`/`untrackedBytes` | **byte-identical** (all 3 zones × golden + memory-coverage runs) |
| `processRssBytes` / `untrackedBytes` | differ by 0.05–0.3 % — these are exactly the two fields the gate documents as category (c), *reported, not asserted*, because RSS is noisier than the tallied categories (T69 Review). Every **asserted** category — texture cache, framebuffer, GPU textures, zone geometry, placeable objects, S3D zone source, VBO/EBO, character models, EQE1 mapped bytes, tracked total, and every count alongside them — is identical to the byte. |
| 3 metric CSVs, deterministic columns (`frame,pos_*,heading,pitch,draws,state_changes,polys,regions_vis,regions_drawn,regions_occluded,alpha_test_draws,solid_draws,light_radius,regions_demoted_radius,entities_visible,tex_binds,tex_param_calls,actual_width,actual_height`) | **byte-identical** all three zones |
| 3 metric CSVs, timing columns (`cpu_ms`, `gpu_ms`, `total_ms`, `cpu_proc_ms`, the per-phase `*_ms`/`gpu_*_ns`) | differ run to run, as they always do; the gate's own p95 assertion passed with headroom in both runs (worst case gfaydark 9.376 vs a 10.672 ceiling) |
| `--help` stdout (167 lines) | **byte-identical** (`diff`, exit 0) — checked twice, after the options extraction and again at the tip |

No golden moved, so no STOP fired.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 config, tests on) | exit 0 |
| `ctest --test-dir build -j6` | **4040 / 4041**, exit 8; sole failure `AudioManagerTest.PreloadCommonSounds` — the standing ALSA host quirk |
| `bash scripts/build-gles2-x86.sh` | exit 0 (and an incremental `--target gles2_zone_benchmark` after every one of the seven commits) |
| Visual gate | PASS before and after; see above |
| `bash scripts/build-arm-noble.sh` | exit 0; `build-arm-noble/bin/gles2_zone_benchmark` is a 73 MB ELF 32-bit ARM EABI5 executable (the benchmark IS in the ARM tree — the script's own deploy line lists it) |
| `scripts/refactor/check_file_sizes.sh` | 13 files checked, **13 ok, 0 warn, 0 fail** |

⚠ Read that last row carefully. Run as `check_file_sizes.sh fable` it reports
17 files, 3 warn and 1 FAIL — but the four extra entries are
`tools/model_viewer.cpp`, `tools/model_viewer_spell_bar.h`,
`tools/entity_composite_builder.cpp` and
`tools/hwcaps/gles2_fog_volume_benchmark.cpp`, none of which this branch
touches. `fable` moved past this branch's base while the unit ran (T385 merged),
so "changed vs fable" picks up the *other* side of the divergence. Against the
actual base (`check_file_sizes.sh 0e430393`) the count is 13 and every one is
ok. Whoever merges should re-read the check on the merged tip, not on this
branch.

### Findings worth carrying forward

1. **`tools/hwcaps/gles2_fog_volume_benchmark.cpp` is 1,711 lines and belongs
   to neither T385 nor T387.** With `--all` now covering `tools/`, it is a
   `FileSizeCeiling` FAIL waiting on the merged tip unless T385 decomposed or
   allowlisted it. Boarded on B167.
2. **The pre-split `zone_bench_loader.cpp` included `<GLES2/gl2.h>` mid-file,
   INSIDE `namespace ZoneBench`** (line 1174 at base), so every GL entry point
   the direct-GL and LOD paths call was named through that namespace. The
   extracted TUs include it at file scope. The declarations are `extern "C"`
   either way, so both spellings resolve to the same symbols — but it is the
   one place in this unit where the moved code is not in a character-identical
   environment, and it is worth knowing it was ever written that way.
3. **The four `main()` mode blocks needed no substitutions at all**, because
   each was already self-contained: `--tree-test` and `--composite-entity-test`
   touch only `opts`/`data`, `--pose-search` opens and closes its own CSV
   handle inside the block, and the static pose capture's only outer local is
   `metrics`. Taking `data` as the owning `std::unique_ptr&` rather than a
   `ZoneBenchData&` kept every `data->` in 819 moved lines character-identical;
   that is worth more than the marginally cleaner signature.
4. **Stale comment pointers were a real cost of the split and are fixed in the
   same branch**: five comments in `zone_bench_entity_viewer.cpp`,
   `zone_bench_merge_probe.h`, `zone_bench_merge.cpp` and `zone_bench_merge.h`
   pointed at `gles2_zone_benchmark.cpp`/`zone_bench_loader.cpp` for functions
   that moved. `zone_bench_merge.cpp`'s said `textureGroupId` was "kept
   private/anonymous there" — no longer true, it is `inline` in
   `zone_bench_draw_util.h` now, and the sync obligation between the three
   copies (here, `zone_bench_merge.cpp`, production's
   `zone_lifecycle/renderer_zone_draw.cpp`) is unchanged but now points
   somewhere real.
