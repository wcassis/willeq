# B948 — `gpuTextureBytes` counts each ETC1 alpha chain once

Date: 2026-10-03. Branch `task/B948-gpu-texture-alpha-count` from
`origin/fable` @ `4912af9f` (post-B949), GitLab #266 (class B, lane offline).
Found by B944 (`refactor/B944/README.md`); B962 took the double count out of
the `gpu_buffers` row only (`refactor/B962/README.md` via issue #266's
2026-10-01 comment) — this issue covers the gate-asserted `gpuTextureBytes`.

Will's rules this unit works to: memory first, frame rate second; 100 %
memory accounting. No budget, golden or baseline was moved (none was
blessed by a ruling quoted in this card; see "What the gate saw" for why
that was unnecessary).

## The defect

`ConstrainedTextureCache::uploadCompressedETC1()` wrapped the MAIN ETC1
texture with `gpuSize` = main mip chain + alpha mip chain, and then wrapped
the `_alpha` companion with the alpha mip chain again.
`COpenGLES2Driver::getGpuTextureMemoryUsage()` sums every texture's
`GpuBytes`, so every ETC1 texture with alpha had its alpha chain counted
**twice** in `gpuTextureBytes` — the gate-asserted figure (budget 38 MiB
since B943; `visual_gate.py`'s `MEMORY_BUDGET_PAIRS` +
`MEMORY_BASELINE_FIELDS`). The cache's own `textureCacheBytesUsed` counted
the pair once (its `sizeBytes` was always main+alpha).

Consistent with the census: on EQE1 runs `gpuTextureBytes` exceeds
`textureCacheBytesUsed` (B927 census, qeynos2: 16,797,104 vs 16,250,840 —
the 546,264-byte gap is exactly the sum of the doubled alpha chains).

## The fix

Per the issue: wrap the MAIN texture with the main chain only; the cache
entry's `sizeBytes` stays main+alpha (the entry owns both storages).

- `src/client/graphics/constrained_texture_cache_compressed.cpp` —
  `uploadCompressedETC1()`: `mainChain`/`alphaChain` computed once; the
  budget figure `gpuSize` is still their SUM (eviction and
  `textureCacheBytesUsed` are unchanged); the main wrap gets `mainChain`,
  the companion wrap gets `alphaChain`.
- `src/client/graphics/constrained_texture_cache_residency.cpp` —
  `rebuildStorage()` (B944's restore): the main texture now re-adopts
  `etc1MipChainSize(...)` (its own chain) instead of `entry.sizeBytes`
  (the pair); the companion's figure was already its own chain and is
  unchanged. A restore that handed the pair back to the main would have
  re-introduced the double count.
- `docker/irrlicht-drm/COGLES2Texture.h` — the `StorageBytes` comment
  (B962's "main texture wrapped with its companion's bytes too -- B948")
  updated: since this fix GpuBytes is each texture's own chain.

`gpuTextureBytes` moves DOWN only (by exactly one alpha chain per
ETC1-with-alpha texture); `textureCacheBytesUsed`, `totalEstimate`'s other
terms, the cache budget and every eviction decision are unchanged.

## What the gate saw (and did not see)

The corrected figure is asserted two ways by `scripts/ci/visual_gate.py`:

- **Budget** (`gpuTextureBytes <= gpuTextureBudgetBytes`, 38 MiB since B943):
  the corrected value is lower, so the assertion is strictly weaker. No
  budget move.
- **Baseline exact-match** (`MEMORY_BASELINE_FIELDS`): the committed
  `tests/goldens/manifest.json` carries **no `gpuTextureBytes` baseline**
  (re-checked on this branch: `grep -c gpuTextureBytes tests/goldens/manifest.json`
  → 0; the per-zone `memory` blocks hold only `textureCacheBytesUsed` /
  `textureCacheBytesLimit` / `textureCount`, and the `memoryCoverage`
  blocks hold the four T80 categories). The gate's `(b)` loop therefore
  `continue`s on this field — there is no blessed per-zone baseline that
  this change could trip. The card's block condition ("if the gate then
  fails a BASELINE … block with the per-zone numbers") did not fire.

So no gate number needed re-pinning, and none was touched. The x86
coverage-run delta is measured below.

## Measured — unit test, red/green

`tests/test_texture_etc1_alpha_count.cpp` (ctest, real EDT_OGLES2
surfaceless device, self-skips without EGL; a synthetic 3-entry pack:
`texA` + its `_alpha` companion, both 256x256 complete 9-level chains, and
`texN` with no companion). The wrapped figures: mainChain = alphaChain =
43,704 B (256x256 ETC1 to 1x1), pair = 87,408 B.

| case | pre-fix (reverted) | post-fix |
|---|---|---|
| `Etc1WithAlphaCountsEachChainOnce` — driver bytes after one upload | **131,112** (main + 2×alpha — RED) | **87,408** (main + alpha, once each) = cache usage |
| `RestoreReAdoptsTheCorrectedByteCounts` — B944 evict → re-bind round trip | **131,112** again (RED) | **87,408** (restored main adopted its own chain) |
| `Etc1WithoutAlphaCountsItsChainOnce` — control | 43,704 | 43,704 (unchanged) |

(Logs: `~/kb-evidence/b948-alpha-double-count/test_red.log`,
`test_green.log`.)

## Measured — the gate's coverage run (x86 llvmpipe)

The gate's memory-coverage invocation (`--eqe1-path data/eqe1_client
--objects --entities 5 --test-races 1,2,3,4,5`, the `run_memory_coverage_benchmark()`
line, `--occluder-budget 16`), before vs after on this host (x86 llvmpipe,
Mesa 25.2.8):

| zone | gpuTextureBytes (before) | (after) | delta | textureCacheBytesUsed (both) |
|---|---|---|---|---|
| qeynos2 | 9,322,992 | 8,973,400 | −349,592 | 8,907,864 |
| gfaydark | 6,479,352 | 6,348,240 | −131,112 | 6,282,704 |
| befallen | 2,633,520 | 2,546,088 | −87,432 | 2,480,552 |
| iceclad | 3,977,608 | 2,681,816 | −1,295,792 | 2,616,280 |
| timorous | 4,557,552 | 3,158,024 | −1,399,528 | 3,092,488 |

`textureCacheBytesUsed` and `textureCount` are byte-identical before/after
(the entry's `sizeBytes` was and is the pair — by design). Every zone's
after-value is now BELOW its cache usage (pre-B948 it was above — the B927
census gap, qeynos2 16,797,104 vs 16,250,840, live client). `totalEstimate`
moved down by exactly the same deltas (it sums `gpuTextureBytes`);
`processRssBytes` is run noise (RSS, not GPU). Each zone's delta is the sum
of the once-doubled alpha chains of the ETC1-with-alpha textures resident in
that zone (chains vary by texture size, so the delta is not an integer
multiple of the 256x256 figure).

(Raw TSVs + per-zone logs: `~/kb-evidence/b948-alpha-double-count/`.)

## Files

| file | change |
|---|---|
| `src/client/graphics/constrained_texture_cache_compressed.cpp` | `uploadCompressedETC1()` wraps each texture with its own chain; budget stays the pair |
| `src/client/graphics/constrained_texture_cache_residency.cpp` | `rebuildStorage()` re-adopts the main's own chain |
| `docker/irrlicht-drm/COGLES2Texture.h` | `StorageBytes` comment updated |
| `tests/test_texture_etc1_alpha_count.cpp` | the repeatable test (3 cases) |
| `tests/cmake/ui_rendering.cmake` | registers `test_texture_etc1_alpha_count` |

## Not verified / out of scope

- **The Pi (Lima) GPU** is not exercised (lane offline); the change is a
  bookkeeping-only figure on the wrap — no GL call, no pixel change.
- **`textureCacheBytesUsed`** is by design unchanged (the entry owns the
  pair), so the golden-image `memory` blocks in the manifest are
  untouched; no golden image can move from a byte-count change.
- The B927 census gap (qeynos2 16,797,104 vs 16,250,840) is from the live
  client lane; it is not re-taken here (lane offline). The unit test and
  the coverage run above cover the same code path.
