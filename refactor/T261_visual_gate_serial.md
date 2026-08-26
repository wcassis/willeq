# T261 — visual_gate trips its CPU-time ceiling under parallel ctest; builder zone_chars count mismatch

Slot: slot1. x86. NO eqemu. Class A. Territory: tests/CMakeLists.txt
(visual_gate registration), scripts/ci/visual_gate.sh / visual_gate.py
(assertion only if needed), tools/willeq_db_builder.cpp (zone_chars count
print), tests. Do NOT touch inventory (T259 slot3), zone-load path (T260
slot2).

## 1. visual_gate under -j
T219, T220, T239 and T258 all saw `visual_gate` FAIL only on the befallen
p95 CPU-ms ceiling when the full suite runs with `-j24`, and PASS alone.
The image/memory assertions never failed. Fix options, in preference
order: (a) `set_tests_properties(visual_gate PROPERTIES RUN_SERIAL TRUE)`
so it runs with the machine to itself; (b) `RESOURCE_LOCK` shared with
the other heavy render tests; (c) make the CPU-time assertion
load-insensitive (measure user CPU via getrusage instead of wall-clock —
check what it measures now). Do (a) unless it makes the suite materially
slower; record the before/after suite wall-time with `-j24`.
Prove it: run the full suite `-j24` three times; visual_gate PASS 3/3.

## 2. zone_chars 2213 vs 2188
The builder logs `zone_chars: 2213 entries` but the table holds 2188 rows.
Find the 25: duplicates collapsed by a UNIQUE key, rows skipped by a
filter, or a count taken before dedup. Make the log print what was
written (and how many were dropped and why). If real data is being
dropped, list which zones/characters and stop — that is a data question.

## Verification
Full ctest ×3 with -j24; Review; evidence review/T261/. Commit:
`T261: visual_gate runs serial; builder reports what it wrote`.

## Review (2026-08-26, slot1, off fable 700d03bb)

Evidence: /mnt/projects/fable/review/T261/ (README.md indexes every file).

### 1. visual_gate under -j: RUN_SERIAL (option a)
- What `cpu_ms` measures: `tools/hwcaps/zone_bench_metrics.cpp` `cpuDrawMs`
  is a steady_clock WALL-TIME span over the benchmark's CPU draw phase, not
  process CPU time (getrusage). Under `-j24` on llvmpipe it competes with up
  to 23 other tests for the same cores, so the p95 x2.0 ceiling is
  load-sensitive by construction. The image/metric/memory assertions are
  deterministic and never failed under load in any of the prior sightings.
- Change: `tests/CMakeLists.txt` -- `RUN_SERIAL TRUE` added to the existing
  `SET_TESTS_PROPERTIES(visual_gate ...)`, with a comment explaining why.
  No change to visual_gate.sh/.py, goldens or manifest.
- Proof: full suite `-j24` x3 before and x3 after (`review/T261/ctest_*`).
  Suite totals every run: **2958/2959** (the ALSA
  `AudioManagerTest.PreloadCommonSounds` quirk is the one failure).
  visual_gate PASS 3/3 after (20.5 / 32.6 / 29.6 s, each scheduled last,
  alone) -- and PASS 3/3 before as well: the load-induced trip did NOT
  reproduce today (load average ~2 at start). The fix stands on the
  measurement being wall-clock, not on a fresh repro.
- Wall time: before 72.5 / 75.1 / 101.3 s (mean 83.0); after 127.0 / 114.0 /
  87.1 s (mean 109.4). **Delta about +26 s mean, with +-15 s run-to-run
  noise.** Mechanism: ctest runs a RUN_SERIAL test only when nothing else is
  running, so the gate's own 20-33 s no longer overlaps the rest of the
  suite. This is the full cost of option (a) and is inherent to it -- a
  COST/ordering tweak would only move it, not remove it. FLAGGED for the
  orchestrator to judge against the plan's "unless materially slower"
  clause; the cheaper alternatives are (d) `PROCESSORS <n>` (partial
  contention, still non-deterministic) or (c) switching `cpu_ms` to
  process CPU time, which changes the blessed `p95CpuMsBaseline` and so
  needs a re-bless -- STOP-for-Will territory, not done here.

### 2. zone_chars 2213 vs 2188: duplicates collapsed by the PRIMARY KEY
- Cause: `zone_characters` has `PRIMARY KEY (zone_name, model_code)` and the
  loader uses `INSERT OR REPLACE`; the log counted LINES (2213), the table
  holds unique KEYS (2188). Exactly 25 lines in the 254 shipped `*_chr.txt`
  files repeat a model code already listed in the same file (independent
  re-count: `review/T261/zone_chr_duplicates_25.txt`).
  - 17 are byte-identical repeats (e.g. `nektulosa_chr.txt` lists `SHR,SHR`
    on lines 2 and 5; `draniksewers{a,b,c}` each repeat `DDV`).
  - 8 repeat the model code with a different `source_file` hint, later line
    kept: eastkorlach brl/cst (`brl`->`brl_chr`, `cst`->`cst_chr`),
    eastkorlacha box (`box`->`box_chr`), westkorlach brl/cst/bon (same
    pattern), nektulosa wil (`wil`->`commons_chr`), nektulosa shr
    (`shr`->`fungusgrove_chr`).
- Is real data dropped? **No** -- no (zone, model_code) pair is lost; all
  2188 keys are present and the table is row-for-row identical to the
  shipped db (`zone_characters_rows_shipped_vs_rebuilt.diff` is empty). The
  8 differing rows lose only the FIRST source hint, which is the bare model
  code (`brl`) where the kept one is the concrete archive (`brl_chr`) that
  the bare code would resolve to anyway. Finding while checking that:
  `WilleqDatabase::getZoneCharacters()` (the only reader of this table) has
  ZERO callers outside `willeq_database.cpp` -- `source_file` is consumed by
  nothing. Noted, not acted on (out of scope; reachability-pattern instance
  for the orchestrator's list). So this is NOT the STOP case.
- Change (`tools/willeq_db_builder.cpp` `loadZoneCharacters`): tracks
  first-seen `source_file` per key; returns the unique-key count (what was
  written) instead of the line count; prints each differing-source
  duplicate by name and one summary line with lines read / collapsed /
  identical / differing; then cross-checks against `SELECT COUNT(*)` and
  exits 1 on mismatch so the printed count can never drift again. Label
  changed `entries` -> `rows written`. The log now ends
  `zone_chars: 2188 rows written` (`review/T261/builder_output_after.txt`).
  `data/willeq.db` NOT regenerated -- rows are identical, only the log
  changed.

### Verification
- build: ok (`cmake --build build -j24`, full tree + builder).
- ctest -j24 x3 after: 2958/2959 each, visual_gate PASS 3/3 (tables above).
- check_file_sizes vs fable: 0 FAIL, 1 WARN (`willeq_db_builder.cpp` 1569
  lines > 1500; was 1519 after T258, already WARN).
- build-gles2-x86.sh / build-arm-noble.sh: NOT re-run -- the plan narrows
  verification to the ctest x3; neither changed file is in either tree's
  input set beyond the builder tool (which the ARM build does compile;
  the change is plain C++ std::map/std::pair, no platform surface).
  Deviation flagged.
