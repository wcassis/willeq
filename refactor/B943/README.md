# B943 — six orangepi memory budgets ratcheted down, audio held

Date: 2026-09-30. Branch `task/B943-ratchet-six-budgets` from `gitlab/fable`
@ `0b84eef6`, issue #261. Offline unit: no eqemu, no Pi, no new measurement.

Will, in chat, 2026-09-30: **"lower the seven budgets, hold audio"**
(`docs/decisions.md` 2026-09-30). `zone_parse` was the seventh, done by B927.
The model is `refactor/B927/README.md`, whose "Every other budgeted category"
table named these candidates.

## The recomputation

`refactor/B943/budget_ratchet_census.py` recomputes every worst from the
sources (`census_output.tsv` is its output). Convention: measured worst x 1.25,
rounded up to a whole MiB. Sources:

- benchmark: `refactor/B927/category_census.tsv`, the 118-zone x86 census at
  `3a2762e2` (117 zones measured; soldunga FATALs, B930), value at the point the
  gate asserts;
- x86 client: `refactor/B909/summary_before.md` + `summary_after.md`, the client
  columns zone_load_complete / ready / steady_60s (four zones);
- gate: `refactor/B943/gate_mem_json/*_mem.json`, the memory JSON of both gate
  invocations (golden and coverage) of the five gate zones, from a
  `visual_gate.py` run on a build with the pre-change 64 MiB texture budget;
- Pi client: `/mnt/projects/fable/review/B854-rendered/sweep-full/zones/*/details.json`
  (45 zones plus retries), the ledger categories at zone_load_complete / ready /
  steady_60s. These are fuller than B927's top-five reading.

| budget | now | worst (source) | x 1.25 | rounded | new | new bytes |
|---|---:|---|---:|---:|---:|---:|
| `archives` | 4 MiB | 2,163,296 (x86 client, timorous; Pi 2,010,568 eastwastes; bench 1,409,088 eastwastes) | 2,704,120 | 2.58 MiB | **3 MiB** | 3,145,728 |
| `textures_decoded` | 20 MiB | 1,056,288 (x86 client, timorous; Pi 899,208 chardok; bench 316,432 thurgadina) | 1,320,360 | 1.26 MiB | **2 MiB** | 2,097,152 |
| `entity_models` | 84 MiB | 31,563,888 (Pi, charasis_retry, zone_load_complete; x86 client 17,491,440 dreadlands; bench 5,083,088 nektulos) | 39,454,860 | 37.63 MiB | **38 MiB** | 39,845,888 |
| `gpuTextureBytes` | 64 MiB | 31,735,808 (gate golden run, qeynos2; census 16,797,104 qeynos) | 39,669,760 | 37.83 MiB | **38 MiB** | 39,845,888 |
| `textureCacheBytesUsed` (`textureMemoryBytes`) | 64 MiB | 42,226,984 (gate golden run, qeynos2; census 16,250,840 qeynos) | 52,783,730 | 50.34 MiB | **51 MiB** | 53,477,376 |
| `characterModelsBytes` | 40 MiB | 1,544,392 (bench, oot) | 1,930,490 | 1.84 MiB | **2 MiB** | 2,097,152 |

Every convention value is below the current budget, so nothing was left alone.

**Correction to the derived targets.** The task gave 21 MiB (`gpuTextureBytes`)
and 20 MiB (`textureCacheBytesUsed`) from the 118-zone census. That census runs
the EQE1 path. The gate's own qeynos2 *golden* invocation loads S3D textures and
holds 31,735,808 B of GPU textures and a 42,226,984 B cache
(`gate_mem_json/qeynos2_mem.json`), which the census never saw. With
`textureMemoryBytes` = 20 MiB that run segfaulted (exit -11) in
`ZoneBench::textureGroupId`; reverting to 64 MiB made the same command pass.
So the two texture budgets are 38 and 51 MiB, and the crash is finding **B944
(#262)**. The other four values stand: none of the gate's ten runs exceeds them
(`census_output.tsv`, last table).

**`audio` is held at 32 MiB** (Will: "hold audio"). Its only Pi worst on record
is 25,725,824 B (qeynos2, T496, 2026-09-17), which predates T504, and the
rendered sweep covered no classic zone. Convention on that figure would give
31 MiB.

The `entity_models` worst is a Pi run of charasis whose row says
`wrong_zone (qeynos)` (the retry loaded another zone): the ledger figure is a
real measurement of the Pi client either way. The next Pi worst is a
dalnir 26.0 MiB.

## What changed

Values, where B927 changed `zone_parse`:

- `config/constrained_presets.json`: `textureMemoryBytes` (51 MiB),
  `gpuTextureMemoryBytes`, `characterModelsMemoryBytes`,
  `memoryLedgerBudgets.{archives,textures_decoded,entity_models}`;
- `src/client/graphics/constrained_renderer_config.cpp` (`fromPreset(OrangePi)`
  and its derivation comments) and the field comments in
  `include/client/graphics/constrained_renderer_config.h`;
- `tests/test_constrained_renderer.cpp`: the compiled-preset and shipped-JSON
  pins, and a new shipped-JSON pin for `textureMemoryBytes`, plus the two
  `FromPreset_OrangePi_*` pins of `textureMemoryBytes` (64 -> 51).

Docs: `docs/decisions.md` (2026-09-30), `docs/ci.md` (a pointer, no numbers),
`refactor/BOARD.md`. A grep of `scripts/ci/visual_gate.py` and every CLAUDE.md
found no quoted copy of an old number; the gate reads the budgets from the
memory JSON.

**The `characterModelsBytes` BASELINES in `tests/goldens/manifest.json` are
untouched** (iceclad 1,522,794 and the rest). Only the budget the estimate is
checked against moved.

**`textureMemoryBytes` is the texture cache's real eviction ceiling**
(`ConstrainedTextureCache`), not only a gate assertion. Lowering it to 20 MiB
changes runtime behaviour on the Pi if any zone's cache would have exceeded
51 MiB. The Pi ships the EQE1 path (census worst 16.25 MB, qeynos), but the
cache figure was never measured on the Pi, and S3D-path zones other than the
gate's five were never measured (T293 recorded 40.27 MiB for qeynos2/S3D).

## Headroom against the new budgets (asserted point, census at `3a2762e2`)

No measured zone is over any new budget (117 of 117 for each, EQE1 census). Worst
zone, and the five gate zones (census values; the gate's own ten invocations
are the last table of `census_output.tsv`, worst 79.6 % of the GPU texture
budget and 79.0 % of the cache budget, both qeynos2 golden):

| category | worst zone % | qeynos2 | gfaydark | befallen | iceclad | timorous |
|---|---:|---:|---:|---:|---:|---:|
| archives | 44.8 (eastwastes) | 42.3 | 41.4 | 40.7 | 41.0 | 42.0 |
| textures_decoded | 15.1 (thurgadina) | 7.1 | 5.3 | 2.5 | 4.0 | 9.1 |
| entity_models | 12.8 (nektulos) | 10.7 | 10.6 | 11.2 | 11.3 | 10.7 |
| gpuTextureBytes | 42.2 (qeynos) | 23.4 | 16.3 | 6.6 | 10.0 | 11.4 |
| textureCacheBytesUsed | 30.4 (qeynos) | 16.7 | 11.7 | 4.6 | 4.9 | 5.8 |
| characterModelsBytes | 73.6 (oot) | 68.9 | 68.9 | 68.9 | 72.6 | 69.9 |

The census values are the ones the gate asserts (`benchmark_end`). They bound
what the client keeps, not the transient high-water mark: `entity_models`
`peak` reaches 36.9 MB in nektulos, over nothing asserted.

## Tests

Build tree `build` (GLES2, tests on): `test_constrained_renderer` 74/74.
`ctest -L hygiene`: all pass except `TexturesReproducible`, `PackReproducible`
and `DbReproducible`, which need tools this tree did not build (they fail with
"not found"; unrelated). `visual_gate.py` on the five gate zones, this build:
all memory-budget assertions pass in all ten runs (`gate_mem_json/`); the gate
as a whole still fails on this host for the reasons B927 recorded (iceclad and
timorous p95 CPU under a loaded host, and the memory-coverage iceclad allocator
slack 35.4 %, B937 #255), none of them a budget.

## Not verified

- Nothing was re-measured. The x86 client (four zones) and Pi client (45
  zones) figures are the committed B909/B854 records; a zone neither loaded
  could sit above its benchmark figure.
- The Pi's `textureCacheBytesUsed` and `gpuTextureBytes` were never measured
  (the Pi ledger has no such estimates). The 51/38 MiB values rest on x86
  only, and the 51 MiB cache ceiling is untested on the Pi.
- S3D-path (no EQE1) texture figures exist for the five gate zones only. A
  zone with more textures than qeynos2 in that mode is unmeasured.
- `characterModelsBytes` is a five-race coverage sample (T303): races outside
  it are unmeasured, so the 2 MiB budget inherits that limit.
- The census predates this branch (`3a2762e2`); fable has moved to `0b84eef6`.
  B936 and later merges touched `objects`, not these categories, but the
  census was not retaken.
