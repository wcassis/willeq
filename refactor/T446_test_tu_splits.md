# T446 — split the three test TUs riding the 1,500 ceiling (structural)

Class B (structural: file size + functional separation outrank minor
edits — Will 2026-09-03), slot2, no server. The three largest test TUs:
`tests/test_eq_commands_equip.cpp` (1,492), `tests/test_zone_connection_flow.cpp`
(1,367), `tests/test_combat_manager.cpp` (1,316). Read `CLAUDE.md`'s
test-layout rules, `tests/cmake/*.cmake` (how a test target is
registered), Batch G's golden-pinned list discipline, and each TU's
fixture/section structure.

1. Split each TU by functional group into 2–3 TUs with a shared fixture
   header where one exists (e.g. `test_eq_commands_equip_{wear,swap,
   validation}.cpp`; `test_zone_connection_flow_{login,zone_in,
   teardown}.cpp`; `test_combat_manager_{attack,skills,death}.cpp` —
   name by what the cases actually cover, not this guess). Every case
   keeps its name and body; the case COUNT before and after is identical
   (list both; ctest totals prove it). No behaviour change.
2. Register the new targets; delete the old TU; any doc/net naming the
   old file (`module_map`, `DocClaims`, the T444 COVERAGE table if it
   covers tests/) learns the new names.
3. Largest resulting TU under 1,000.

## Proof

Build (background), `ctest -j6` (no expected reds; the total rises by
zero cases — say the per-target case counts), file sizes, DocClaims/
DocCounts/DocCitations green. No golden changes. No board strikes (no
board item); new items B461+ if you find something while reading (T445
in slot1 allocates from B459+; say which you used).

STOP: a case count changes.
