# T385 — the ceiling really is tree-wide: `tools/` under the check, `model_viewer.cpp` decomposed (B167)

Class P (structural — Will's first-order rule). Two legs, one slot, no
server. Read B167, `refactor/M00_ceiling_closed.md`, the K/L mechanism
(`refactor/K_component_extraction.md` §Mechanism) and Batch G's method
decomposition shape.

## Leg A — the check

`scripts/refactor/check_file_sizes.sh --all` adds `tools/` (and
`benchmarks/` if it holds `*.cpp/*.h`). The FAIL list on the current tip
is the census: report it. Every file over 1,500 is decomposed in Leg B or,
if it is a pure data table (the `createDefaultConfig` precedent), listed
in the allowlist WITH its reason and a one-line comment — the allowlist
has been empty since M00 and re-opening it needs the reason in the file.

## Leg B — `tools/model_viewer.cpp` (3,246 lines)

Read it first; write the split plan into the Review BEFORE editing
(functional seams: window/input, model loading, spell FX exercise (the
B160 caller), rendering loop, CLI). Extract by function into
`tools/model_viewer/<part>.cpp` + headers, `main` stays tiny; no behaviour
change — prove with the tool's existing tests if any, else a golden of
its `--help` and one headless run's log (record before, compare after).
Each extracted part ≤ 1,500. One commit per part.

## Proof

Build (tools build with the main tree), `ctest -j6` (`FileSizeCeiling`
now covers tools), `check_file_sizes.sh --all` 0 FAIL, file sizes. Update
the M00 close-out and `CLAUDE.md`'s "tree-wide" sentence (DocClaims /
DocCounts may net the directory list — check).

STOP: a tool file is a generated table (say which; allowlist with reason).
