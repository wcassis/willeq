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
