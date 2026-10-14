# B929 — growthplane's placed objects are over the 24 MiB budget, and nothing asserts it

GitLab #247. Measured 2026-10-14 on hermes-ext (x86, llvmpipe), branch
`task/kb-b929-placed-object-sharing` at `origin/fable` 7ad78a6c plus this
unit's commits. Offline: `build/bin/gles2_zone_benchmark` (the gate's
memory-coverage invocation), no device, no lane. Raw evidence:
`/home/user/kb-evidence/kb-b929-placed-object-sharing/` (sweep TSV + per-zone
memory JSONs; not on this host's NFS — this README cites the numbers).

## Step 1 — the measurement (before any code)

**The fix this issue asked for is already in fable.** B936b (branch
`task/B936b-bench-shared-objects`, MR !55, merged 2026-09-30) ported the
shared-mesh design to `gles2_zone_benchmark`, and Will blessed the five
gate-zone `placeableObjectsBytes` baselines it moved (`docs/decisions.md`,
2026-09-30; the manifest's memoryCoverage note). Issue #247's own notes track
the same: acceptance 1 "is in; acceptance 2 (the census job) remains". So
this unit measures to confirm, and then builds the net.

Re-measured on this branch (x86, llvmpipe, the gate's memory-coverage
invocation: `--eqe1-path --objects --entities 5 --test-races 1,2,3,4,5
--occluder-budget 16 --memory-json`, 60 frames for growthplane, 5 for the
five gate zones):

**growthplane: models vs placements vs bytes.**

| quantity | value | source |
|---|---:|---|
| placements in the zone | 4,333 | loader log `Objects built: 4333 objects, 4405 buffers` |
| distinct object meshes | 11 | loader log `Object meshes: 11 built, 4322 placements reused one (sharing on)` |
| keyframe-animated placements | 4,314 drawing 4 meshes | same line (`keyframe-animated: 4314 placements draw 4 meshes`) |
| `placeableObjectsBytes` | **111,348 B** (0.4 % of budget) | memory JSON `growthplane.mem.json` |
| budget `placeableObjectsMemoryBytes` | 25,165,824 B (24 MiB) | `config/constrained_presets.json:49` (untouched) |

The mechanism, as filed and as it stands in the tree: the benchmark's
object loop built ONE mesh per geometry and counted each distinct mesh once
(`tools/hwcaps/zone_bench_memory.cpp:173-180`, the `countedObjectMeshes`
set — the comment there names B936); the per-placement state is the scene
node, transform and material (`tools/hwcaps/zone_bench_loader.cpp:621-651`).
**Bytes scale with models, not placements.** On the pre-B936b tree the same
run was 28,687,788 B — 114 % of budget — because the loop built a private
mesh and VBO per placement and `placeableObjectsBytes` summed one mesh per
placement (issue #247 comment 2026-09-30; `refactor/B936/README.md`
"Benchmark — branch B"): 4,333 placements of 11 geometries, the animated
majority at ~8.66 KB of vertex/index arrays plus keyframe mapping each.

**The five gate zones, re-measured and matching the blessed manifest
bit-for-bit** (`tests/goldens/manifest.json` memoryCoverage, blessed
2026-09-30):

| zone | re-measured | manifest | |
|---|---:|---:|---|
| qeynos2 | 145,812 | 145,812 | = |
| gfaydark | 266,388 | 266,388 | = |
| befallen | 130,968 | 130,968 | = |
| iceclad | 215,112 | 215,112 | = |
| timorous | 981,756 | 981,756 | = |

**Acceptance B therefore holds without a code change: no geometry-holding
path in the benchmark (or the client — B936's client half) builds per
placement, so there is nothing left to share.** The draw/state/poly counts
below are taken once, on the current tree; the MR pipeline's gate run
re-proves the goldens byte-identical (acceptance C) because this branch
touches no render path at all.

**growthplane frame/scene counters, current tree** (60-frame run,
`before/growthplane.log`): Total frame p95 3.46 ms, `frame_p50_ms=2.09`;
Draw calls avg 35 (6–74), State changes avg 3 (1–4), Polygons avg 1,550
(265–2,994), Avg FPS 437.7 (llvmpipe, unconstrained — a throughput figure,
not a Pi frame time; the Pi's own 62 fps for growthplane after B936 is in
`refactor/B936/README.md`).

## The fix (this unit)

There is no render or loader change: the sharing landed with B936/B936b.
What was missing was the NET — acceptance 2:

- `scripts/ci/zone_budget_census.py` — reads the sweep's per-zone
  `<zone>.mem.json` (the same file the gate asserts) and fails on ANY zone
  over ANY budget: every estimate pair the gate checks (the gate's
  `MEMORY_BUDGET_PAIRS` minus the two also-baselined, compared against the
  budgets the benchmark read out of the shipped orangepi preset, so
  `config/constrained_presets.json` stays the single source) and every
  `memoryLedgerBudgets` category. Exit 0 all inside / 1 a zone over or
  unasserted (no memory JSON) / 2 not a sweep dir / 3 bad zone list.
- `.gitlab-ci.yml` job `zone-budget-census` (gate stage, blocking, no
  retries) — sweeps all 118 zones of
  `scripts/ci/zone_content_sweep_zones.tsv` (B852's list) and runs the
  checker over it. Runtime measured 246 s of zone time + the sweep's 2 s
  polling ticks = **4 m 46 s** (118 rows, `results.tsv` column `wall_s`),
  on this runner-class host's llvmpipe — too slow for the ctest suite,
  hence the gate-stage job. No retries: the gate's 3 in-script retries
  exist for its load-sensitive CPU-p95 assertion; the census asserts
  platform-invariant memory figures, so a retry would only mask a real
  over-budget zone. The benchmark is the per-ref tree's
  `build-gles2/ref/<slug>/bin/gles2_zone_benchmark` (B1012's `GLES2_TREE`,
  passed with `--bench-bin`; the sweep's default `build/bin` does not exist
  in the gate stage), and the job first runs the pack self-heal below.
- **The pack self-heal** — `scripts/ci/zone_census_pack_selfheal.sh`, the
  job's first step. The runners' EQE1 tree is the shared NFS mount
  (`data/eqe1_client` → `/mnt/projects/EverQuest_ETC1`), and B930 (#248)
  left that tree's `soldunga.eqe1` stale (v1 holding a 31-char name — the
  pre-B910 truncation; the hermes-ext copy was rebuilt to v3 on 2026-09-21,
  the runner's was not). A stale pack FATALs the benchmark's all-or-nothing
  texture load, so that zone could never be asserted against the budgets,
  and the sweep's exit-1-on-NOT-CLEAN would then fail the job on an asset,
  not a budget. The runner CANNOT repair the NFS tree in place: it is
  read-only for the runner (measured, pipeline 1174 job 6273:
  `eqe1_builder` "Cannot open output: data/eqe1_client/soldunga.eqe1" after
  90 s of a successful .s3d read + ETC1 encode).
  So the self-heal builds a LOCAL pack tree the sweep reads
  (`--eqe1-path`, the sweep's existing option). The sweep runs WITHOUT
  `--composite-entities`, so the benchmark opens packs only by the
  loader's fixed names — every swept zone's three packs (`zone`,
  `zone_obj`, `zone_chr`) plus the fixed pack list. That MANDATORY set is
  exactly what the local tree holds (379 packs today: 118 × 3 + 25):
  - the fixed list is `tools/hwcaps/zone_bench_loader.cpp`'s `eqe1Names`
    array, re-read from the source on every run (25 entries today: the
    `global_chr` globals, the `gequip`/`gatesequip`/... equipment packs,
    `willeq`; the array's `_obj`/`_chr` literals are zone-derived and are
    excluded) so no hand-kept copy can drift; the script FATALs if the
    extraction no longer names `global_chr`, which the loader is
    guaranteed to keep.
  - each mandatory pack is verified with `verify_eqe1 --require-v3`
    (B930/B1017: flags exactly the truncation-signature packs, so a clean
    v1/v2 whose names all fit passes) and hard-LINKED into the tree when
    it is clean (a second name for the same inode — nothing is copied,
    nothing is written in the source tree).
  - a STALE mandatory pack is REBUILT with
    `eqe1_builder --archive <name>.s3d` (the client FATAL's own named
    remedy) into the local tree, where the write succeeds; the builder
    emits v3 only when a name is >31 chars, so a rebuild of a clean pack
    would be byte-identical and this path is taken only for the ones that
    are not.
  - a mandatory pack that is MISSING, or stale with no `.s3d`, fails the
    job (exit 1) — the sweep would die on it.
  Packs OUTSIDE the mandatory set (the other ~1,127 in the NFS tree) are
  NOT linked: the runner's out dir is a different filesystem from the NFS
  mount, so `ln` falls back to `cp` and linking them all would copy
  gigabytes for packs the sweep never opens.
  The NFS tree is left exactly as it was — B930's open item for the
  runner/Pi/dev-u24 copies stays open (it needs the tree's own writer),
  while this job no longer dies on the asset. E2E on this host with the
  REAL `verify_eqe1`/`eqe1_builder`: the full mandatory set (379) linked
  in ~11 s, a synthetic stale v1 `arena` pack rebuilt to a clean v1
  (7 textures), a missing mandatory pack failing with its name, a stale
  non-mandatory pack left alone (not linked, not rebuilt). A 3-zone sweep
  (qeynos2/arena/iceclad) run against the local tree is CLEAN and the
  budget checker PASSES all three. The stub-driven contract is
  `test_zone_census_pack_selfheal` (9 cases, ~5 s; its fixed list is
  re-parsed from `zone_bench_loader.cpp` the same way the script does).
- ctest legs (in `build-test`, so they run on every code pipeline):
  - `test_zone_budget_census` — pure python, no benchmark/assets: drives
    the checker through its real exit-code contract on synthetic sweep
    output and pins its estimate-pair set against `visual_gate.py`'s
    `MEMORY_BUDGET_PAIRS` (a gate edit that adds a budgeted pair goes red
    here instead of silently unasserted).
  - `zone_budget_census_smoke` (label `slow`) — the net's two halves: the
    synthetic over-budget growthplane (28,687,788 > 25,165,824, B927's
    measured figure) MUST fail the checker, and the three smoke zones
    (qeynos2/sebilis/iceclad, one per expansion) swept fresh and inside
    every budget. 6.4 s local.

## RED on today's tree / GREEN after

The tree is post-B936b, so a real zone cannot currently be over budget —
proven by the full sweep (below): **118/118 zones inside every budget**.
The RED state the net exists to catch is demonstrated, not synthesized as
evidence: the smoke's first half feeds the checker B927's measured
pre-fix growthplane row (28,687,788 B of `placeableObjectsBytes`,
`refactor/B927/category_census.tsv`) and the checker fails it, naming the
figure:

```
FAIL: 1 of 1 zones not inside every budget: growthplane
  OVER/UNASSERTED: growthplane    estimate:placeableObjectsBytes live=28687788 budget=25165824 status=over
```

(`zone_budget_census_smoke` ctest, 2026-10-14.) If a future regression
re-introduces per-placement geometry, the same path fails the sweep zone
itself in `build-test` (smoke zones) and the pipeline (all 118).

## The full census (GREEN)

`bash scripts/ci/zone_content_sweep.sh` (118 zones) +
`python3 scripts/ci/zone_budget_census.py --check-budgets --sweep-dir
<out>`, 2026-10-14 on this host: sweep 118/118 clean, checker `PASS: all
118 zones inside every budget`. Worst zone per budgeted category (the
checker's rows, `budget_rows.tsv`):

| category | worst zone | worst | budget | % |
|---|---|---:|---:|---:|
| `placeableObjectsBytes` | thurgadina | 1,011,990 | 25,165,824 | 4.0 % |
| `zoneGeometryBytes` | timorous | 12,758,982 | 16,777,216 | 76.0 % |
| `s3dZoneSourceBytes` | iceclad | 10,569,352 | 14,680,064 | 72.0 % |
| `vboEboBytes` | timorous | 13,839,972 | 41,943,040 | 33.0 % |
| `characterModelsBytes` | oot | 1,544,608 | 2,097,152 | 73.7 % |
| `gpuTextureBytes` | qeynos | 16,316,376 | 39,845,888 | 40.9 % |
| `totalEstimate` | timorous | 40,168,885 | 134,217,728 | 29.9 % |
| ledger `zone_parse` | iceclad | 11,326,576 | 14,680,064 | 77.2 % |
| ledger `zone_mesh` | timorous | 23,586,768 | 29,360,128 | 80.3 % |
| ledger `spatial` | timorous | 3,019,840 | 4,194,304 | 72.0 % |
| ledger `entity_models` | nektulos | 5,263,968 | 39,845,888 | 13.2 % |
| ledger `objects` | overthere | 1,929,536 | 41,943,040 | 4.6 % |
| ledger `archives` | eastwastes | 1,409,376 | 3,145,728 | 44.8 % |
| ledger `textures_decoded` | thurgadina | 376,080 | 2,097,152 | 17.9 % |
| ledger `unattributed` | stonebrunt | 87,888 | 1,048,576 | 8.4 % |
| ledger `audio` | (all zero) | 0 | 33,554,432 | 0.0 % |

growthplane: `placeableObjectsBytes` 111,348 B (was 28,687,788 pre-B936b);
every other budgeted category of the zone under 30 % of its budget in the
same run (`ledger objects` 1,648,640 / 41,943,040 = 3.9 %).

## Verification

- `test_zone_budget_census` 9/9 (unittest, direct + ctest);
  `zone_budget_census_smoke` ctest PASS (6.4 s: RED half + 3-zone GREEN
  half); `test_zone_census_pack_selfheal` ctest PASS (~5 s: the self-heal's
  exit-code contract on stub verify/builder binaries, the fixed list
  re-parsed from `zone_bench_loader.cpp`); all three run in the MR
  pipeline's `build-test`.
- Self-heal e2e on this host with the REAL tools (2026-10-14): hard-linked
  inodes for clean packs, a synthetic stale v1 rebuild to a clean pack,
  the missing-mandatory and stale-non-mandatory contracts — E2E PASS
  (the scratch evidence under
  `/home/user/kb-evidence/kb-b929-placed-object-sharing/` does not survive
  on this host; the stub test is the durable net).
- Full 118-zone sweep + checker: PASS, 2026-10-14, this host (runtime
  cited above).
- **MR !182 pipeline 1177, job 6298 (the runners, 2026-10-14):**
  `zone-budget-census` GREEN, 8 min. The self-heal rebuilt the runner's
  stale `soldunga.eqe1` in place of the NFS copy — "REBUILD: soldunga.eqe1
  (stale, B930 signature) from data/eq_client/soldunga.s3d" → "v3, 9148 KB,
  144 textures, 1 long name(s)" → "soldunga: rebuilt, verify clean" (~94 s,
  matching this host's ~90 s single-archive pass); "378 linked, 1 rebuilt,
  0 failed"; the full sweep then ran all 118 zones, soldunga's row
  `rc=0 load=y ... CLEAN`, and the checker printed "PASS: all 118 zones
  inside every budget". This closes the unmeasured leg below: it cost
  ~94 s of rebuild + ~9 s of links on top of the 4 m 46 s zone time,
  inside the 3 h `.goldfarm` default.
- The MR pipeline's gate stage runs the new `zone-budget-census` job cold;
  its `gate` job re-runs the five zones against the untouched manifest —
  acceptance C (goldens byte-identical) and E in one.
- `config/constrained_presets.json`, `tests/goldens/manifest.json` and
  every golden image: untouched (bless-only).

## Not verified / left open

- ~~**The re-run of `zone-budget-census` on the runners after the
  self-heal** is the one unmeasured leg~~ — measured: pipeline 1177,
  job 6298 (2026-10-14), 8 min, GREEN (see Verification above).
- The Pi figures for this unit: none were needed. B936 measured the Pi's
  own `objects` ledger and frame rate after the fix (`refactor/B936/README.md`
  Orange Pi A/B table), and the census's asserted quantities
  (`placeableObjectsBytes` and the ledger live bytes) are
  platform-independent by construction (vertex/index sums; allocator bytes
  at a fixed lifecycle point — B927), so the x86 census is the assertion
  surface. The Pi's live `objects` bytes would be a client run (the census
  uses the benchmark, which is what the gate asserts), which B936 already
  took. The Pi and dev-u24 pack copies remain B930's open item (they are
  not on the x86 runners' NFS tree); this unit's self-heal heals only what
  the job reads.
- The 4 m 46 s census runtime was measured on hermes-ext (16 cores, shared
  with kanban workers); the x86 runners are the same class (B1008). The job
  has no `timeout:` beyond the 3 h `.goldfarm` default, so a 2× slower host
  still fits.
- `eqe1MappedBytes` (address space, 48.8 % of its 256 MiB budget in
  growthplane) is asserted by the census like every other pair; its
  convention-derivation ratchet-down (166 MiB) is B927's report-only table,
  not this unit's scope.
