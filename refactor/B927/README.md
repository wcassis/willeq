# B927 — the iceclad/timorous coverage-baseline bless, and the `zone_parse` budget ratcheted from 32 MiB to 14 MiB

Date: 2026-09-29. Branch `task/B927-bless-and-zone-parse-ratchet`, from
`gitlab/fable` @ `3a2762e2`. Offline unit: x86 only, no eqemu and no Pi. The Pi
and client figures are the ones already committed by B909 and B854.

Will, in chat, 2026-09-29: **"bless iceclad and timorous"** and **"ratchet the
zone_parse budget down"** (`docs/decisions.md` 2026-09-29).

## Part 1 — the bless

`tests/goldens/manifest.json` memory-coverage `characterModelsBytes`: iceclad
1,522,794 and timorous 1,465,276. **The values did not change.** Commit
e3a14c2a set them and attributed them to a bless by Will "in chat 2026-09-23
(budget re-derivation ruling + confirmed the golden re-bless in the same
exchange)". Will made no such ruling (`docs/decisions.md` 2026-09-28). Three
places were corrected:

- the manifest's memory-coverage `note`, which had repeated the false
  attribution. It now says e3a14c2a set the values under an attribution that
  was false, Will blessed them on 2026-09-29, and no image golden moved;
- `refactor/B813/README.md`, a new paragraph after the 2026-09-28 revert table;
- `docs/decisions.md` 2026-09-29, the ruling.

A search of the tree found no other copy of that attribution. The B813 board
entry and `docs/ci.md` already describe e3a14c2a's budget raise as a false
ruling. History was not rewritten. The gate still parses the manifest:
`VisualGateLedgerAssertions` passes (see Tests), and so does the five-zone gate
run below, which reads the manifest.

## Part 2 — which point the gate asserts

`scripts/ci/visual_gate.py` `assert_memory_ledger()` compares
`memory["ledger"]["categories"][c]["live"]` against `ledgerBudgets[c]`. That
ledger object is the one snapshot `computeMemorySnapshot()`
(`tools/hwcaps/zone_bench_memory.cpp`) takes at `benchmark_end`, after the
frames. `assert_memory_budgets()` reads `s3dZoneSourceBytes` from the same
snapshot. **Both budgets are checked at one point, the end of the run. The gate
does not take a maximum over lifecycle points and does not read `peak`.**

The benchmark prints `[memledger]` lines at `startup`, `zone_parse`,
`mesh_build_textures`, `objects`, `spatial_bsp`, `entities`,
`zone_load_complete` (after `S3DZone::releaseSourceAfterLoad()`, B914), `ready`
and `steady`. Nothing in the benchmark calls `resetPeaks()`, so each
category's `peak` is the process high-water mark and includes every transient
between the points.

### FINDING (needs a GitLab issue, B-unassigned): mid-load `zone_parse` is far above the budget, before and after this change

The code goes over the budget during the load, and the gate never looks there.
In the census, 12 of 117 zones hold more than 14 MiB of live `zone_parse` at
some load point, and 24 have a high-water mark above 14 MiB. **Two zones are
above the OLD 32 MiB budget too:**

| zone | live at the load points (max) | high-water `peak` | asserted (after the release) |
|---|---:|---:|---:|
| iceclad | 31,673,584 | **50,071,136** (47.8 MiB) | 11,329,968 |
| timorous | 25,877,840 | **38,544,176** (36.8 MiB) | 5,529,152 |
| hole | 21,779,136 | 29,551,808 | 4,138,672 |
| warslikswood | 18,541,264 | 26,232,944 | 5,073,232 |
| stonebrunt | 18,646,080 | 20,580,912 | 4,945,856 |

The budget below is therefore set against what is asserted: the post-load
state that `docs/ci.md` already says the gate measures. It bounds what the
client keeps. It does not bound the parse. Two ways to make it honest are to
assert `zone_parse` at the pre-release points too (with its own number), or to
stream the parse so the region source is never all resident at once. Either
one is a separate unit, and the numbers are Will's.

## Part 2 — the measured worst

**The census covers all 118 era zones.** `scripts/ci/zone_content_sweep.sh`
now also writes each zone's memory JSON (`--memory-json`, the file the gate
asserts against) as `logs/<zone>.mem.json`. That is the only change to the
sweep. `refactor/B927/zone_parse_census.py` tabulates the JSONs and the
`[memledger]` lines into:

- `zone_parse_census.tsv`: per zone, `zone_parse` live at every point, the
  maximum over the points, the high-water `peak`, the asserted value and
  `s3dZoneSourceBytes`;
- `category_census.tsv`: every budgeted category and estimate, asserted value
  and peak, per zone.

Command. The sweep ran on this branch's tree, which was `3a2762e2` plus
`--memory-json` in the sweep script. Release `build-gles2`, dev host
`dev-u24`, llvmpipe. It ran as four parallel `--zones` shards, same binary:

```bash
bash scripts/ci/zone_content_sweep.sh --bench-bin build-gles2/bin/gles2_zone_benchmark \
    --out-dir build-gles2/b927_sweep/shard<N> --zones <a quarter of the 118>
python3 refactor/B927/zone_parse_census.py build-gles2/b927_sweep/shard{0,1,2,3} refactor/B927
```

The result is 117/118 clean. The exception is **soldunga**, which fails at 2 s:
`FATAL Texture 'gnome_stronghold_floor_transition.bmp' not found in any EQE1
flat pack`. That is the B852 failure B910/#100 was said to have closed. It is
a FINDING: it needs a GitLab issue, B-unassigned, to reopen #100 or file a
regression. soldunga has no memory JSON, so its `zone_parse` is unmeasured.
Its `.s3d` is small, so it is not a worst-case candidate.

Top five by the asserted `zone_parse` (the census, the coverage invocation):

| zone | exp | asserted `zone_parse` | `s3dZoneSourceBytes` | % of 14 MiB |
|---|---|---:|---:|---:|
| iceclad | velious | 11,329,968 | 10,568,328 | 77.2 |
| timorous | kunark | 5,529,152 | 5,083,275 | 37.7 |
| warslikswood | kunark | 5,073,232 | 4,749,353 | 34.6 |
| stonebrunt | kunark | 4,945,856 | 4,650,451 | 33.7 |
| hole | classic | 4,138,672 | 3,777,171 | 28.2 |

The client figures come from the committed records and were not re-measured.
The ledger of the budgeted client runs is recorded after its own Step-13
release, so these values are at the equivalent point:

| source | platform | zone | point | `zone_parse` |
|---|---|---|---|---:|
| `refactor/B909/summary_before.md` | x86 client | iceclad | steady_60s (= ready = zone_load_complete) | **11,714,736** |
| `refactor/B909/summary_after.md` | x86 client | iceclad | all three | 11,710,064 |
| `refactor/B909/summary_after.md` | x86 client | timorous / dreadlands / gfaydark | all three | 7,159,280 / 4,414,320 / 1,749,840 |
| `refactor/B854/RENDERED_SWEEP.md` + `rendered/*.tsv` | Orange Pi client | iceclad | ready and steady_60s | 9.8 MiB (≈10.3 MB) |
| same | Orange Pi client | 44 other zones | ready / steady | not in the top five. Upper bound = the 5th category, ≤ 10.5 MiB (timorous steady; timorous at ready is exactly 6.0 MiB) |

**The measured worst is the x86 client, iceclad, 11,714,736 B (11.17 MiB).**
The x86 client runs 0.38–1.63 MB above the benchmark in the four zones B909
measured (B909 README). The Pi client runs below both.

## Part 2 — the derived budget

In-tree convention: measured worst × 1.25, rounded up to a whole MiB.

    11,714,736 B × 1.25 = 14,643,420 B = 13.965 MiB → 14 MiB = 14,680,064 B

14,680,064 < 33,554,432, so the budget goes down by 18,874,368 B (−56 %).

**The B803 pin still describes reality, so both values moved together.**
`zone_parse` (allocator bytes charged under the `ZoneParse` scope) is at least
`s3dZoneSourceBytes` (`S3DZone::getMemoryUsage()`, the estimate) in all 117
zones. The ratio runs from 1.047 (airplane) to 1.379 (kedge), and the mean gap
is 150,693 B. The two figures bound the same data by two methods and still
track each other after B914. `s3dZoneSourceMemoryBytes` and the ledger's
`zone_parse` are both 14 MiB, and the equality test is unchanged.

Changed: `config/constrained_presets.json` (`s3dZoneSourceMemoryBytes`,
`memoryLedgerBudgets.zone_parse`), `fromPreset(OrangePi)` and its comments
(`constrained_renderer_config.cpp`), the field comment in
`constrained_renderer_config.h`, and the four pins in
`tests/test_constrained_renderer.cpp` (two each for the compiled preset and the shipped JSON). `docs/ci.md` now states that no budget is asserted mid-load. No other
budget, manifest value or image golden changed.

Not changed: `tests/test_zone_bsp_pvs.cpp` `kZoneSourceBudgetBytes` (32 MiB).
That is a different bound: it is the heap a bare `WldLoader` parse retains
BEFORE any release (27.53 MiB for iceclad), and its comment says "NOT the
preset". Its comment still quotes the preset as 24 MiB, which has been stale
since T491. That is left for a doc pass.

## Verification — the five gate zones against the new budget

`visual_gate.py --zones qeynos2,gfaydark,befallen,iceclad,timorous`, run on
this branch (memory JSONs from `build-gles2/ci_artifacts/`):

| zone | `zone_parse` live (golden / coverage) | % of 14 MiB | `s3dZoneSourceBytes` (golden) | % of 14 MiB |
|---|---:|---:|---:|---:|
| qeynos2 | 673,232 / 673,216 | 4.6 | 572,083 | 3.9 |
| gfaydark | 757,792 / 757,824 | 5.2 | 681,525 | 4.6 |
| befallen | 1,195,120 / 1,195,120 | 8.1 | 1,088,219 | 7.4 |
| iceclad | 11,329,968 / 11,329,968 | 77.2 | 10,577,816 | 72.1 |
| timorous | 5,529,152 / 5,529,168 | 37.7 | 5,088,187 | 34.7 |

Every budget and ledger-budget assertion passes in all ten runs, in all three
gate runs. The 118-zone census stays under 14 MiB at the asserted point in all
117 measured zones. The worst is iceclad at 77.2 %. The gate as a whole did
NOT pass on this host, for two reasons unrelated to budgets that the unchanged
baseline shares (see "Gate runs").

## Every other budgeted category (report only, nothing changed)

"Measured worst" is the largest value over the 118-zone census at the asserted
point (x86 benchmark), the x86 client (B909, four zones, `zone_load_complete`
/ `ready` / `steady_60s`) and the Pi client (B854 / B909 / T496). The
convention column is worst × 1.25, rounded up to a whole MiB. **↑** marks a
convention value above the current budget. The ratchet-down rule forbids that
raise, so the category is a defect to fix in code, not a number to move.

Ledger categories:

| category | budget | worst: benchmark (zone) | worst: x86 client (zone) | worst: Pi client (zone) | worst | convention |
|---|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1 MiB | 86,208 | 141,056 (all) | — | 141,056 | 1 MiB (same) |
| `archives` | 4 MiB | 1,409,088 (eastwastes) | 2,163,296 (timorous) | — | 2,163,296 | **3 MiB** |
| `zone_parse` | ~~32~~ **14 MiB** | 11,329,968 (iceclad) | 11,714,736 (iceclad) | 9.8 MiB (iceclad) | 11,714,736 | 14 MiB (this unit) |
| `zone_mesh` | 28 MiB | 23,585,888 (timorous) | 17,503,808 (timorous) | 17,578,560 (timorous) | 23,585,888 | 29 MiB ↑ |
| `spatial` | 4 MiB | 3,019,840 (timorous) | 5,674,240 (gfaydark) — over | 40,709,216 (growthplane, B909 B side) — over | 40,709,216 | 49 MiB ↑ (B909/#229 is the fix) |
| `textures_decoded` | 20 MiB | 316,432 (thurgadina) | 1,056,288 (timorous) | ≤ 820,584 (burningwood; decoded+driver) | 1,056,288 | **2 MiB** |
| `entity_models` | 84 MiB | 5,083,088 (nektulos; high-water 36,865,552) | 17,491,440 (dreadlands, zone_load_complete) | 26.0 MiB (dalnir) | 27.26 MB | **33 MiB** |
| `objects` | 40 MiB | 33,010,832 (growthplane) | 5,429,488 (gfaydark) | 38.4 MiB (growthplane) | 40.27 MB | 48 MiB ↑ |
| `audio` | 32 MiB | 0 (not constructed) | 1,626,976 (iceclad) | 25,725,824 (qeynos2, T496) | 25,725,824 | **31 MiB** |

Estimate budgets. The benchmark is the only source: the census coverage
invocation and the gate. Neither client measures these estimates.

| estimate | budget | worst (zone) | convention |
|---|---:|---:|---:|
| `placeableObjectsBytes` | 24 MiB | **28,687,788 (growthplane) — OVER** | 35 MiB ↑ |
| `vboEboBytes` | 40 MiB | 33,692,940 (growthplane) | 41 MiB ↑ |
| `zoneGeometryBytes` | 16 MiB | 12,758,982 (timorous) | 16 MiB (same) |
| `gpuTextureBytes` | 64 MiB | 16,797,104 (qeynos) | **21 MiB** |
| `textureCacheBytesUsed` | 64 MiB | 16,250,840 (qeynos) | **20 MiB** |
| `characterModelsBytes` | 40 MiB | 1,544,392 (oot) | **2 MiB** |
| `eqe1MappedBytes` (address space) | 256 MiB | 139,111,816 (iceclad) | **166 MiB** |
| `totalEstimate` | 128 MiB | 79,719,316 (growthplane) | **96 MiB** |

Bold = a ratchet-down candidate. The measured tree is behind these numbers,
not the derivations in the budget comments. A candidate derived from the
five-race coverage sample (`entity_models`, `characterModelsBytes`) inherits
that sample's coverage limit (manifest note, T303).

### FINDING (needs a GitLab issue, B-unassigned): growthplane is over the `placeableObjectsMemoryBytes` budget

In the census, growthplane's `placeableObjectsBytes` is 28,687,788 against
25,165,824 (114 %). The gate never runs growthplane, so no CI assertion
covers it. The same zone holds `objects` 31.5 MiB (benchmark) / 38.4 MiB (Pi)
against 40 MiB, and `vboEboBytes` at 80 % of its budget.

## Gate runs

Three runs of `visual_gate.py --zones qeynos2,gfaydark,befallen,iceclad,timorous`
were made on this host. Logs: `build-gles2/b927_gate{1,2,_base}.log`, local and
not committed.

| run | tree | memory-budget failures | other failures |
|---|---|---:|---|
| gate1 | B927 | 0 | iceclad p95 CPU 13.389 ms > 9.530; memory-coverage iceclad allocator slack 35.4 % > 35.0 % |
| gate2 | B927 (rebuilt) | 0 | iceclad p95 CPU 18.390 ms > 9.530; the same slack 35.4 % |
| base | **`3a2762e2` budgets** (B927's two budget files reverted, benchmark rebuilt) | 0 | the same slack 35.4 %; iceclad p95 9.427 ms (just under) |

**Neither failure comes from this change.** The slack line is byte-for-byte the
same on the unchanged baseline (65,587,888 B / 185,233,408 B RssAnon). A
budget value cannot move allocator slack: the budget is read only by the gate's
comparison, and no code path reads `s3dZoneSourceMemoryBytes` or the
`zone_parse` ledger budget (`grep` over `src/` and `tools/`: only
`zone_bench_memory.cpp` copies them into the JSON). The iceclad p95 is
load-sensitive: 9.4 to 18.4 ms across runs on this shared host, where the
118-zone sweep and a `-j8` build were running.

Pipeline 282 passed the gate at `3a2762e2` on hermes-ext. **FINDING (needs a
GitLab issue, B-unassigned):** on dev-u24 the memory-coverage iceclad slack
(35.4 %) is over `LEDGER_SLACK_MAX_PCT` (35.0 %) at `3a2762e2` itself, with
no change applied. The CI pipeline on this branch is the authoritative gate
run.

## Tests

Suite as CI's `test` job runs it (Release `build`, this host,
`WILLEQ_REQUIRE_ASSETS=1`): `ctest -j6 -E
'(^visual_gate$|^RDPServerFrameTest\.UpdateFrameMultipleTimes$)'` passed
**5068 of 5068**, including the skipped tests. The RDP case passed on its own
`--repeat until-pass:3` run. Hygiene (`ctest -L hygiene`, run in this GLES2
tree, not in a no-graphics tree as CI runs it): **20 of 20**, re-run after the
final doc edits. The four `zone_parse` /
`s3dZoneSourceMemoryBytes` pins in `test_constrained_renderer.cpp` (compiled
preset and shipped JSON) now expect 14 MiB. The B803 equality assertion is
unchanged and passes.

## Not verified

- Nothing was measured live. The x86 client figures (four zones) and the Pi
  figures (45 zones, `zone_parse` only where it made the top five) are the
  committed B909/B854 records. A zone the x86 client never loaded could sit
  further above its benchmark figure than the 1.63 MB B909 saw. The worst
  client figure (iceclad) has 2.97 MB of headroom under 14 MiB. timorous, the
  next zone in the census, would need a client gap of more than 9.1 MB to
  exceed it.
- soldunga's `zone_parse` (it FATALs before loading).
- The census ran four shards in parallel on a shared host. The ledger byte
  counts are deterministic to within tens of bytes (B914), so this matters
  only for the timing fields, which B927 does not use.
