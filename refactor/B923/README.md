# B923 — the CI gate sees the memory only the client allocates

GitLab #242. Branch `task/B923-client-only-owners-gate`, from `fable` @ `55c9ac0c`.

## The defect

The CI `gate` job measures `gles2_zone_benchmark`. Several ledger owners exist
only in the client, so no CI check covered them and their budgets were
unenforced where it matters. Two concrete splits, named in the issue:

- **The client charges its occluder table to a different category than the
  benchmark.** Both loaders build the SAME table — `region_occluders.h`'s
  `SelectRegionOccluders()` feeding `SoftwareOcclusionCuller::setRegionOccluders`
  in both — but the client charged it to `zone_mesh` (`zm.occluders`, Step 4's
  scope) while the benchmark charges it to `spatial` (`sp.occ_culler` /
  `sp.occluder_sets`). The same bytes landed in two categories, and the client's
  `zone_mesh` was asserted by a gate that never built those bytes.
- **The client's `spatial` and `audio` owners are invisible to the gate.**
  The benchmark builds neither the client's collision copy (`sp.region_bounds`,
  `sp.region_tris`, …) nor its audio. So the client's `spatial` was 8.13 MB in
  iceclad against a 4 MiB budget and no check caught it (B909/README.md);
  B977 (#295) found `audio` is client-only too.

## Acceptance (per issue #242) and how each is met

### 1. One category assignment per shared structure, same in client + benchmark

`src/client/graphics/zone_load/renderer_zone_load.cpp` (`loadZoneStep4_Regions`):
the client's occluder charge now opens a temporary `MemoryScope(Spatial)` and
charges `sp.occluders`, mirroring the benchmark's nesting. `zone_mesh`'s Step-4
scope resumes when it closes. The benchmark (`zone_bench_loader.cpp`) is
**unchanged** — it already charged `spatial`.

Pinned by `tests/test_region_occluders.cpp`
(`RegionOccludersCensus.BothLoadersChargeTheOccluderTableToSpatial`): both
sources are asserted to (a) charge the table inside a `Spatial` scope and
(b) NOT charge `zm.occluders` (the B909 split). Red if either loader regresses
to the other's category.

### 2. A CI-runnable client-ledger measurement, or `ClientLedgerLive` on a
###    schedule with the result recorded

There is **no server-free client zone-in**: `--zone-load <mode>` only selects
progressive vs manual loading, the zone data itself streams from the zone
server (`Application::render()` / `ZoneLoadSnapshot` are driven by the zone
packets), and the `--scenario` lane needs eqemu too. So this card takes the
issue's stated fallback: `ClientLedgerLive` run on a schedule, result recorded.

- **`client-ledger-live`** (`.gitlab-ci.yml`, `stage: gate`, manual,
  `allow_failure: true`): runs
  `scripts/client_ledger_capture.sh --zones iceclad --modes spatial
  --assert-budgets` — the rendered client (orangepi preset, `WILLEQ_BASELINE=1`)
  zoned into iceclad (Velious) past `steady_60s`, asserting every budgeted
  `memoryLedgerBudgets` category at `ready` and `steady_60s`. The evidence dir
  and the check's per-category lines are artifacts (30 d) — the recorded
  result. It takes the eqemu lane lock (~20 min) and is `allow_failure` so it
  never blocks the pipeline (a red client budget is a finding to work, not a
  pipeline kill).
- **`ClientLedgerLive`** ctest (`tests/cmake/commands.cmake`, label `live`):
  the same invocation; its `--out` now lands in the build tree under a dated
  name so the per-category check lines in `rig.log` + the capture's
  `summary.md` can be committed as evidence.
- **Run live in this run** (eqemu VM `172.16.200.99`, lock free, rendered x86
  llvmpipe, iceclad/Velious, `WILLEQ_BASELINE=1`): recorded below.

### 3. Repeatable test: the check goes red when a client-only owner exceeds its
###    category budget

`scripts/ci/test_client_ledger_check.py`, registered as `ClientLedgerBudgetCheck`
(label `hygiene`; `tests/cmake/hygiene.cmake`). Pure Python, no server, no
display, no binary. It builds a capture-shaped fixture in the client's own
`[memledger]` line format and drives `scripts/client_ledger_table.py check`:

| case | drives | required exit |
|---|---|---|
| every budgeted category inside its orangepi budget | `check` | **0** |
| one client-only owner's category over its budget | `check` | **1** |
| a run with no `steady_60s` point | `check` | **3** |

It asserts `check` asserts every budgeted category at **both** points
(`len(budgets) * 2` check lines) and that the over-budget line names the
category with `OVER`. The budgets come from `config/constrained_presets.json`
(orangepi preset), never from the test. **No budget value changed anywhere.**

## What was measured (live run, this branch, 2026-10-03 00:08–00:16 UTC)

Evidence: `~/kb-evidence/b923-client-ledger-gate/` (`run-20261003T000857Z/` +
`live-run.log`). Client = `build/bin/willeq` built from this branch;
`result.txt`: `zone=iceclad mode=spatial ready=1 steady_60s=1 landed=y`.

**The unified `spatial` category now includes the client's own owners, and the
client-side gate is RED for it — the whole point of B923.** iceclad, `steady_60s`:

| category | live (B) | budget (B) | % | verdict |
|---|---:|---:|---:|---|
| `spatial` | 4,605,600 | 4,194,304 | 109.8 % | **OVER** |
| `zone_mesh` | 8,442,672 | 29,360,128 | 28.8 % | ok |
| `zone_parse` | 11,714,512 | 14,680,064 | 79.8 % | ok |
| `entity_models` | 7,308,976 | 39,845,888 | 18.3 % | ok |
| `archives` | 2,126,288 | 3,145,728 | 67.6 % | ok |
| `audio` | 1,556,304 | 33,554,432 | 4.6 % | ok (client-only, B977) |
| `textures_decoded` | 853,872 | 2,097,152 | 40.7 % | ok |
| `objects` | 382,848 | 41,943,040 | 0.9 % | ok |
| `unattributed` | 142,944 | 1,048,576 | 13.6 % | ok |

The client's `spatial` owners (`[memledger-startup] point=steady_60s`):
`sp.occluders` 2,777,568 (the shared table, now here in both loaders),
`sp.region_bounds` 1,717,168 (the collision-copy B909 left under #229),
`sp.region_tris` 81,648, `sp.object_instances` 22,464, `sp.object_regions` 3,616,
`sp.light_regions` 288, `startup.misc` 2,848 — TOTAL 4,605,600. Before this
branch the gate saw none of the collision rows and the occluder lived in
`zone_mesh`, so no check ever reported this. **The `client-ledger-live` job is
expected to go red on `spatial` until #229's client-spatial work (BOARD item 2,
`sp.region_bounds` flat-array) lands**; that is the gate doing its job, and the
job is `allow_failure` so it records the finding without killing the pipeline.

The benchmark side is unaffected: the change is client-only, and the benchmark
already charged the occluder to `spatial` and measured in budget (B909), so the
blocking CI `gate` job's numbers are unchanged.

## Not verified

- **CI pipeline** for the pushed branch (the `client-ledger-live` job, a fresh
  full-suite `test` + `gate` run) — the runner executes it on push; the live
  run above is the same `client_ledger_capture.sh` invocation the job uses.
- **Orange Pi** `spatial` for this unified category (x86 llvmpipe only; the
  Pi budget is the same `orangepi` preset but the platform numbers are not
  taken here).
- The **other three zones** (gfaydark, dreadlands, timorous) on this branch —
  iceclad was the B909 in-budget zone and the one `ClientLedgerLive` asserts;
  the same client-only `sp.region_bounds` / occluder rows exist in the others
  (B909) and the gate will surface them identically when run.
- The 3 asset-reproducible hygiene tests (`TexturesReproducible`,
  `PackReproducible`, `DbReproducible`) are red **in this partial build tree
  only** — it lacks the `generate_textures` / `eqe1_builder` /
  `willeq_db_builder` targets; CI's full build produces them. This change
  touches no texture/pack/db generation.
