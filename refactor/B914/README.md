# B914 — the gate benchmark releases the zone source the client releases at the end of a zone load (Step 13)

Date: 2026-09-29. Branch `task/B914-bench-region-release`, before =
`3c6ea8b3` (fable). Approved by Will on 2026-09-29: "go ahead with the
benchmark change, I'll bless the new baselines".

## What the client releases, and where

`ZoneLoadComponent::loadZoneStep13_Cleanup()`
(`src/client/graphics/zone_load/renderer_zone_load_env.cpp`) is the last step
of `loadZoneSequential()`, run after the regions, objects, doors, entities,
collision, sky, environment, lights and the first simulation frame, and
immediately before the client's `zone_load_complete` ledger point. Before this
unit it did four releases inline, on `currentZone`:

| release | condition | frees |
|---|---|---|
| `S3DZone::releaseTexturePixelData()` | preset `releaseTextureDataAfterUpload` (true on `orangepi`) | raw zone/object/character texture pixels |
| `S3DZone::clearCharacterData()` | always | the zone archive's character models |
| `WldLoader::releasePostLoadData()` | a `wldLoader` exists | skeleton/track/placeable/light/material parse tables |
| `WldLoader::releaseRegionGeometry()` (T504) | a `wldLoader` exists | every region's source `ZoneGeometry` and the frag-index map |

The benchmark (`tools/hwcaps/zone_bench_loader.cpp`, what `visual_gate.py`
measures) ran none of them, so the gate measured source data the client has
already freed.

## The change

- **One shared function:** `S3DZone::releaseSourceAfterLoad(bool
  releaseTexturePixels)` (`include/client/graphics/eq/s3d_loader.h`, body in
  `src/client/graphics/eq/s3d_loader.cpp`) does exactly the four releases
  above, in the same order, and returns what it freed. The client's Step 13
  now calls it (same log lines as before). The benchmark calls it at the end
  of `loadZone()`, after its `entities` ledger point, with the same
  `releaseTextureDataAfterUpload` flag from the same `fromPreset(OrangePi)`
  config, and then records a `zone_load_complete` ledger point, as the client
  does.
- **Nothing the benchmark's frames read is released.** After `loadZone()` the
  frames read the built region meshes, `data->regions` (AABBs),
  `data->bspTree`, `S3DZone::lights`, the occluder table and the texture
  cache. The source geometry is read only inside `loadZone()` (mesh build and
  occluder extraction, both before the release). The two portal diagnostics in
  `zone_bench_regions.cpp` also read it, but their callers are commented out
  (`gles2_zone_benchmark.cpp`); `zone_bench_loader.h` now says they must run
  before the release if revived. Proof: every image golden is bit-identical
  and every draw/state/poly metric matches (below).

## The bless table

**No field of `tests/goldens/manifest.json` changes, so there is nothing to
write to the manifest.** The fields this release moves, `s3dZoneSourceBytes`
and `totalEstimate`, are in `MEMORY_BASELINE_FIELDS`, but the committed
manifest has never blessed them: every `zones[].memory` block carries only
`textureCacheBytesUsed`, `textureCacheBytesLimit` and `textureCount`, and
`verify()` skips a baseline field the manifest does not hold. The memory
coverage blocks carry only `eqe1MappedBytes`, `eqe1ArchiveCount`,
`placeableObjectsBytes` and `characterModelsBytes`, none of which move. B903's
note that the change "lowers the exact-match `s3dZoneSourceBytes` /
`totalEstimate` baselines" was wrong for the committed manifest. The gate
checks both fields against their budgets, and the ledger's `zone_parse`
category against its budget, and those values are below.

Measured with `visual_gate.py` (all five zones), Release `build-gles2`, dev
host `dev-u24` (llvmpipe, Mesa 25.2.8, the blessed `glVersion`). "before" =
`3c6ea8b3`, "after" = this branch. The values are from the benchmark's memory
JSONs (`build-gles2/ci_artifacts/<zone>_mem.json` and
`<zone>_coverage_mem.json`).

Golden-image run:

| zone | field | before | after | delta |
|---|---|---:|---:|---:|
| qeynos2 | `s3dZoneSourceBytes` | 7,534,105 | 572,083 | −6,962,022 |
| qeynos2 | `totalEstimate` | 45,649,221 | 38,687,199 | −6,962,022 |
| gfaydark | `s3dZoneSourceBytes` | 7,430,814 | 681,525 | −6,749,289 |
| gfaydark | `totalEstimate` | 29,708,226 | 22,958,937 | −6,749,289 |
| befallen | `s3dZoneSourceBytes` | 4,667,957 | 1,088,219 | −3,579,738 |
| befallen | `totalEstimate` | 14,194,281 | 10,614,543 | −3,579,738 |
| iceclad | `s3dZoneSourceBytes` | 27,271,618 | 10,577,816 | −16,693,802 |
| iceclad | `totalEstimate` | 47,930,366 | 31,236,564 | −16,693,802 |
| timorous | `s3dZoneSourceBytes` | 23,275,714 | 5,088,187 | −18,187,527 |
| timorous | `totalEstimate` | 55,087,182 | 36,899,655 | −18,187,527 |

Memory-coverage run (same deltas, different absolute values):

| zone | field | before | after | delta |
|---|---|---:|---:|---:|
| qeynos2 | `s3dZoneSourceBytes` | 7,533,497 | 571,475 | −6,962,022 |
| qeynos2 | `totalEstimate` | 27,511,426 | 20,549,404 | −6,962,022 |
| gfaydark | `s3dZoneSourceBytes` | 7,429,998 | 680,709 | −6,749,289 |
| gfaydark | `totalEstimate` | 40,525,319 | 33,776,030 | −6,749,289 |
| befallen | `s3dZoneSourceBytes` | 4,667,405 | 1,087,667 | −3,579,738 |
| befallen | `totalEstimate` | 20,479,630 | 16,899,892 | −3,579,738 |
| iceclad | `s3dZoneSourceBytes` | 27,262,130 | 10,568,328 | −16,693,802 |
| iceclad | `totalEstimate` | 51,334,386 | 34,640,584 | −16,693,802 |
| timorous | `s3dZoneSourceBytes` | 23,270,802 | 5,083,275 | −18,187,527 |
| timorous | `totalEstimate` | 66,732,256 | 48,544,729 | −18,187,527 |

Ledger `zone_parse` live bytes, against its 33,554,432-byte (32 MiB) budget
(golden run; the coverage run is the same to within 48 bytes):

| zone | before | % | after | % | delta |
|---|---:|---:|---:|---:|---:|
| qeynos2 | 8,029,536 | 23.9 | 673,280 | 2.0 | −7,356,256 |
| gfaydark | 7,826,784 | 23.3 | 757,872 | 2.3 | −7,068,912 |
| befallen | 5,474,752 | 16.3 | 1,195,152 | 3.6 | −4,279,600 |
| iceclad | 31,673,040 | 94.4 | 11,329,984 | 33.8 | −20,343,056 |
| timorous | 25,877,552 | 77.1 | 5,529,168 | 16.5 | −20,348,384 |

What the shared function reported freeing (benchmark stdout, one run each):

| zone | texture pixels | "region source geometry" (see the caveat) |
|---|---:|---:|
| qeynos2 | 4,674,560 | 60,491,408 |
| gfaydark | 1,290,880 | 16,987,695 |
| befallen | 545,152 | 11,749,148 |
| iceclad | 1,512,306 | 63,504,208 |
| timorous | 950,554 | 119,979,243 |

**Caveat, a pre-existing finding (not fixed here):** the byte count
`WldLoader::releaseRegionGeometry()` returns, which the client also logs at
Step 13, overstates what is freed several times over: iceclad reports 63.5
MB, but `s3dZoneSourceBytes` fell only 16.7 MB for the whole release,
texture pixels included. It sums
`ZoneGeometry::getFullMemoryUsage()`, which adds the shared `materialData`
into every geometry that points at it. The same memory counted once
(`s3dZoneSourceBytes`) and measured at the allocator (the ledger) fell by the
amounts in the tables above. That is the double counting the 2026-09-17
shared-block rule forbids. It is on the B914 board entry as an unnumbered
follow-up.

Unchanged in every zone, both runs: `textureCacheBytesUsed`, `textureCount`,
`eqe1MappedBytes`, `eqe1ArchiveCount`, `zoneGeometryBytes`,
`placeableObjectsBytes`, `gpuTextureBytes`, `vboEboBytes`, `framebufferBytes`,
`characterModelsBytes`. Report-only fields (`processRssBytes`,
`untrackedBytes`) move with run noise as they always do. RSS falls by
3.2–8.4 MB on the golden runs and does not move on the coverage runs, where
glibc keeps the freed heap. None of these is asserted.

## Run-to-run stability (three after-runs)

`visual_gate.py` was run three times on the after build (`after1`..`after3`)
and passed all three times against the unchanged manifest.

- `s3dZoneSourceBytes`, `totalEstimate` and every other `MEMORY_BASELINE_FIELDS`
  and coverage field: **bit-identical across the three runs, all five zones,
  both invocations.**
- Ledger `zone_parse` live: bit-identical in 9 of the 10 zone×invocation
  cells. The exception is the qeynos2 golden run: 673,280 / 673,248 / 673,280.
  Categories this change does not touch jitter the same way across the same
  three runs (`zone_mesh`, `entity_models`, `objects`, `textures_decoded`, by
  16 to ~4,300 bytes). The ledger's live bytes are never baselined, only
  checked against a budget, so this is not a bless value.

## Image, metric and p95 status

| zone | 3 image goldens (md5, before + 3 after) | draws/state/polys | p95 CPU ms, before → after1/2/3 (ceiling) |
|---|---|---|---|
| qeynos2 | bit-identical | match | 5.022 → 4.699 / 4.203 / 5.024 (10.108) |
| gfaydark | bit-identical | match | 5.793 → 5.606 / 6.274 / 5.214 (10.672) |
| befallen | bit-identical | match | 6.258 → 6.477 / 7.076 / 6.260 (10.948) |
| iceclad | bit-identical | match | 6.141 → 5.583 / 7.377 / 3.800 (9.530) |
| timorous | bit-identical | match | 11.259 → 18.780 / 11.234 / 11.657 (24.210) |

No image re-bless and no p95 re-bless. `p95CpuMsBaseline` is untouched. The
p95 spread is the usual max-of-three-poses noise; timorous's 18.78 was one run.

## Budget headroom after the change (no budget changed)

The worst zone over both invocations, before → after. Only the three budgets
this change moves are listed. Every other budget is unchanged to the byte.

| budget | value | worst before (zone) | worst after (zone) | headroom after |
|---|---:|---|---|---:|
| ledger `zone_parse` | 33,554,432 | 31,673,040 (94.4 %, iceclad) | 11,329,984 (33.8 %, iceclad) | 22,224,448 |
| `s3dZoneSourceMemoryBytes` | 33,554,432 | 27,271,618 (81.3 %, iceclad) | 10,577,816 (31.5 %, iceclad) | 22,976,616 |
| `totalMemoryBudgetBytes` (tracked total) | 134,217,728 | 66,732,256 (49.7 %, timorous cov) | 48,544,729 (36.2 %, timorous cov) | 85,672,999 |

Unchanged budgets, for reference: ledger `zone_mesh` 80.3 % (timorous),
`spatial` 72.0 % (timorous), `archives` 31.7 %, `objects` 23.4 %,
`entity_models` 5.1 %, `textures_decoded` 0.9 %, `unattributed` 8.2 %; zone
geometry 76.0 %, texture cache 62.9 %, GPU textures 47.3 %, VBO/EBO 40.2 %,
EQE1 mapped 51.8 %, placeables 22.3 %, character models 3.6 %, framebuffer
36.6 %. A ratchet-down of `zone_parse` / `s3dZoneSourceMemoryBytes` (they are
pinned equal, B803) is Will's call and is not made here.

## Tests

`tests/test_zone_source_release.cpp` (`test_zone_source_release`, registered
in `tests/cmake/world_geometry.cmake`):

- `ZoneSourceRelease.DropsEveryRegionsSourceGeometryAndKeepsTheBsp`: loads
  befallen with the benchmark's options and calls the shared function. Every
  `getGeometryForRegion()` is null afterwards and `getMemoryUsage()` falls.
  The BSP tree and the lights the frames read survive. Asset-gated through
  `tests/asset_requirement.h`.
- `ZoneSourceRelease.TexturePixelsFollowThePresetFlag`.
- `ZoneSourceReleaseCensus.BothZoneLoadersCallTheSharedRelease`: both loaders
  call `->releaseSourceAfterLoad(`, and neither calls any of the four parts
  directly, so a second copy is red.
- `ZoneSourceReleaseCensus.TheBenchmarkReleasesAfterItsLastSourceRead`: the
  call comes after the benchmark's last `getGeometryForRegion(` and before its
  `zone_load_complete` point.

RED proof (the census reads the source at run time, so no rebuild was needed):
with the benchmark's `->releaseSourceAfterLoad(` call replaced by an empty
result, both census tests FAIL ("Expected: (src.find(\"->releaseSourceAfterLoad(\"))
!= npos"). With the call replaced by a direct
`wldLoader->releaseRegionGeometry()` (a second copy), `BothZoneLoadersCallTheSharedRelease`
FAILS with "tools/hwcaps/zone_bench_loader.cpp calls releaseRegionGeometry(".
With the source restored, all 4 pass. The gate cannot be this net, because the moved fields are not
baselined and all of them are under budget with or without the release.

Suite, as CI's `test` job runs it (Release `build`, this host):
`ctest -j6 -E '(^visual_gate$|^RDPServerFrameTest\.UpdateFrameMultipleTimes$)'`
passed 5034 of 5034, 129 of them skipped. The RDP case passed on its own
`--repeat until-pass:3` run. Hygiene (`ctest -L hygiene` in a no-graphics
tree, as CI does it) passed 16 of 16.

## Not verified

- The client binary was not run. Its Step 13 now goes through the shared
  function, and the refactor keeps its order, conditions and log lines. It is
  covered by the build and the unit tests, not by a live zone-in.
- The Orange Pi was not measured. Everything above is x86 llvmpipe.
