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

## Review (2026-08-26, slot1, off fable d1cbb0f6)

Evidence: /mnt/projects/fable/review/T258/ (README.md indexes every file).

### Change (tools/willeq_db_builder.cpp)
- WAL stays as the BUILD-TIME journal (fast bulk insert); a comment now says
  so. After `ANALYZE` and before `sqlite3_close`: `PRAGMA journal_mode=DELETE`
  (checkpoints the WAL into the main file, header bytes 18/19 2/2 -> 1/1;
  failure to switch is now a hard error, exit 1) then `VACUUM` (cheap: the
  whole build is 1.6 s, file 4,050,944 -> 3,997,696 bytes).

### Regenerated data/willeq.db (proof: db_regeneration_proof.txt)
- Built with `willeq_db_builder --eq-path /mnt/projects/EverQuestP1999
  --data-path data --config-path config --output data/willeq.db` (T246's
  invocation; no s3d index).
- Header 18/19: before 2/2 (WAL), after 1/1 (legacy).
- Schema (`sqlite_master`): 0 differing lines. All 15 tables dumped and
  diffed: identical row counts (spells 8066, dbstrings 8305, eqstrings 5901,
  zone_characters 2188, ...). **TOTAL 2 differing lines = the
  `metadata.build_date` row** ("Aug 26 2026 01:29:45" -> "05:08:26").
  DEVIATION from the plan's "0 differing lines apart from the header's
  journal flag": the builder stamps a build timestamp into `metadata`, so
  a regeneration can never diff to 0 there; T246's proof had the same row
  change. No data row differs.
- A plain (non-immutable) read-only open reports `journal_mode=delete` and
  creates no `-wal`/`-shm` (this is what the sqlite3 CLI / scripts see).

### Tests (tests/test_willeq_database.cpp, +2, `WilleqDatabaseJournalModeTest`)
- `ShippedDbHeaderIsNotWal`: reads the real file's 100-byte header, asserts
  magic + bytes 18/19 == 1.
- `PlainReadOnlyOpenReportsDeleteAndLeavesNoSidecars`: `sqlite3_open_v2`
  READONLY on the real path (deliberately NOT the client's immutable URI),
  `PRAGMA journal_mode` == "delete", sidecars absent (guarded on pre-state
  like T257's test).
- RED against the pre-T258 file (`red_journal_tests_before_regen.txt`):
  both fail -- header 2/2, mode "wal", and the plain reader created
  `willeq.db-wal`/`-shm` beside the tracked file (the T257 symptom, seen
  live). GREEN after regeneration: 12/12 in the binary including T257's
  `EightProcessesOpenTheShippedDbTogether` and `ReaderCreatesNoWalOrShmFiles`.

### Verification
- build: ok. ctest -j24: **2955/2957** -- ALSA `AudioManagerTest.PreloadCommonSounds`
  (documented) and `visual_gate` (befallen p95 CPU-ms 42.3 > ceiling 40.1
  under the parallel suite). `visual_gate` re-run ALONE: PASS 18.8 s
  (`ctest_visual_gate_rerun_alone.txt`). The unit touches no renderer code
  and the db rows are identical, so this is load-induced timing; flagged,
  not waived -- the orchestrator may want the gate serialized under -j.
- check_file_sizes: 0 FAIL, builder WARN 1519 (> 1500; was 1502 after T246).
  The script listed combat.cpp / test_eq_action_handler_cast.cpp as
  "changed vs fable" though `git status` shows only my 3 files -- not
  investigated (outside territory; T254 owns combat).
- Not run (plan bar is "Full ctest"): build-gles2-x86.sh, build-arm-noble.sh.
- willeq_database.cpp untouched (T257's immutable open stays).

### Side note
- The builder prints `zone_chars: 2213 entries` while the table holds 2188
  rows, before and after alike (dedup on insert, presumably). Pre-existing,
  not this unit.
