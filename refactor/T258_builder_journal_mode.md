# T258 — willeq_db_builder ships a WAL-mode database (T257 leftover)

Slot: slot1. x86. NO eqemu. Class A, tiny. Territory:
tools/willeq_db_builder.cpp (journal mode at final close), the shipped
data/willeq.db (regenerate with proof like T246), tests. Do NOT touch
willeq_database.cpp (T257 merged — leave the immutable open as is),
inventory (T259 slot3), combat/eq_movement (T254 slot2).

## Gap
`tools/willeq_db_builder.cpp:1347` sets `journal_mode=WAL` and leaves it in
the shipped file. T257 made READERS immune (immutable open), but the file
itself still advertises WAL, so any other tool opening it normally (sqlite3
CLI on NFS, scripts) hits the same recovery race and creates sidecars.

## Scope
1. Before the builder's final close: `PRAGMA journal_mode=DELETE` (and
   `VACUUM` if cheap) so the shipped file is a plain rollback-journal DB.
2. Regenerate `data/willeq.db` with the T246-style proof (row counts and
   every table diffed against the previous file: 0 differing lines apart
   from the header's journal flag). Confirm `PRAGMA journal_mode` on the
   shipped file reads `delete`.
3. Test: a builder unit test (or the willeq_database version test) asserts
   the shipped file's journal mode; T257's concurrent-open test still
   green.

## Verification
Full ctest; Review; evidence review/T258/. Commit:
`T258: willeq.db ships in DELETE journal mode`.
