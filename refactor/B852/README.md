# B852 — nothing verifies that Kunark and Velious zones load

Card: kanban t_c10e8f98. Issue: #98 (`Closes #98`). Class A, lane offline.

Will, 2026-09-20: "velious and kunark zones are part of the willeq target, so
we need to verify that content works." (recorded in docs/decisions.md)

The zone set of record is the 118-zone census T490 took ("all 118
original/Kunark/Velious archives", `refactor/T490_every_classic_zone_fits_the_pi.md`).
Before this unit, the CI gate loaded three classic zones (qeynos2, gfaydark,
befallen) and nothing else; no test, script or gate opened a single Kunark or
Velious zone.

## What landed

- `scripts/ci/zone_content_sweep.sh` — the offline sweep: one
  `gles2_zone_benchmark` process per zone (the gate's memory-coverage
  invocation, `--eqe1-path/--objects/--entities/--test-races`, tiny frame
  count), heaviest `.s3d` first, per-zone timeout (300 s) + log-silence
  watchdog (120 s), one TSV row per zone (exit code, wall time, FATAL/ERROR
  counts, first FATAL line, missing-texture / missing-EQE1 counts,
  placeholder-model count, loader success line reached, peak RssAnon from the
  `[memledger]` lines). A row is CLEAN iff exit 0, zero FATAL/ERROR, the
  loader's `Zone loaded in` line present, no placeholder models, and peak
  RssAnon under the 256 MB ceiling (docs/decisions.md T293 envelope). A zone
  that dies on the 4 GiB worker cap or times out is a RESULT, not a stop.
- `scripts/ci/zone_content_sweep_zones.tsv` — the committed zone list
  (zone / expansion tag / s3d bytes / notes), heaviest first.
- `scripts/ci/zone_content_sweep_zones.py` — regenerates the list from the
  census + `docs/reference/pre_luclin_zones.json` (the expansion tags'
  cited source; `stonebrunt`/`warrens` are tagged from
  `tools/generate_zone_lines.py` and cited in the notes column).
- `scripts/ci/zone_content_sweep_smoke.sh` + ctest
  `zone_content_sweep_smoke` (label `slow`, tests/cmake/eqe1.cmake): the
  3-zone smoke list — qeynos2 (classic), sebilis (kunark), iceclad (velious) —
  fails on any non-clean row; SKIPs loudly (exit 125) when the benchmark
  binary or the gitignored EQ asset symlinks are absent; a `--zones` name not
  in the list fails loudly (exit 3) instead of sweeping zero zones.
- `docs/ci.md`: "The zone-content sweep" section (how to run it).
- `docs/decisions.md`: the 2026-09-20 ruling, quoted verbatim, with the
  consequence (gate zone set, offline B852, live B853; new goldens for added
  zones follow, existing goldens/budgets stay Will's).

## Results of the full 118-zone sweep (x86 dev VM, llvmpipe, this host)

`results_118.tsv` is the committed copy of the sweep's TSV (bulk per-zone
logs: `/home/user/kb-evidence/b852-zone-content-sweep/full/logs/`).

See `SUMMARY.md` for the per-expansion table and the issue list.

## Not in this unit

Fixes for any non-clean zone are separate issues (filed per distinct failing
line, labels `class::A,lane::offline,status::triage`). The gate's zone set
growing (B813/#7) and the live sweep (B853/#99) are their own cards.
