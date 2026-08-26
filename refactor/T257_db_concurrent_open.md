# T257 — two clients opening willeq.db in the same second: one fails "missing metadata table" (T256 finding)

Slot: slot1. x86. NO eqemu. Class A. Territory: src/client/willeq_database.*
(open flags, retry/busy handling, metadata check), tests. Do NOT touch
inventory (T255 slot3), combat/eq_movement (T254 slot2).

## Symptom (review/T256/README.md)
Two `willeq` processes launched within ~1 s: the second's `sqlite3_prepare`
on the metadata table fails and the client aborts "missing metadata
table". The DB is read-only data shipped with the client; concurrent
readers must never fail.

## Scope
1. Reproduce headlessly: two processes (or two threads in a test) opening
   `data/willeq.db` simultaneously → one fails. Capture the exact sqlite
   error code (SQLITE_BUSY? SQLITE_LOCKED? a WAL/shm creation race —
   T-notes mention `willeq.db-shm/-wal` files appearing in wt/t2).
2. Fix by the cause: open with `SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX`
   and `?immutable=1` URI if the file is never written at runtime (verify
   nothing writes at runtime — grep for INSERT/UPDATE outside the builder);
   set a busy timeout for the prepare path; make the metadata check
   distinguish "table missing" from "database busy" so the error message
   is honest. Ensure no `-wal`/`-shm` files are created by a reader.
3. Test: N concurrent opens all succeed (thread test with the real file);
   the version-validation tests (T56) still pass.

## Verification
Full ctest; Review; evidence review/T257/. Commit:
`T257: concurrent willeq.db readers never fail`.

## Review (2026-08-26, slot1)

### Root cause (measured, review/T257/)
- `willeq.db` is built in **WAL journal mode** (`tools/willeq_db_builder.cpp:1347`
  `PRAGMA journal_mode=WAL`; header bytes 18/19 = 2/2) and lives on **NFS**
  (`10.0.30.10:/volume2/projects`, nfs v3, `local_lock=none`).
- A plain `SQLITE_OPEN_READONLY` connection to a WAL file still runs the WAL
  protocol: it creates `-wal`/`-shm`, mmaps the wal-index and takes the
  WAL_RECOVER lock to rebuild it on first use. Two processes opening in the
  same instant race on that recovery; the loser's first prepare fails with
  **SQLITE_BUSY (5), extended SQLITE_BUSY_RECOVERY (261), "database is
  locked"**. `open()` reported every prepare failure as "missing metadata
  table" -- the T256 message.
- Reproduction (standalone fork harness, `review/T257/repro.cpp`, pre-fix
  flags): **97/200 losers with TWO processes** on the project mount, 62/240
  with eight, 99/200 with stale sidecars present. **0/400 on local ext4**
  with the same flags -- the race is NFS-only, which is why the `/tmp`
  unit-test databases never saw it. Readers DID create `-wal`/`-shm` on
  every round (the sidecars the T-notes saw in wt/t2).
- Nothing writes the db at runtime (grep INSERT/UPDATE/DELETE outside the
  builder: none).

### Fix (src/client/willeq_database.cpp)
- Open via URI `file:<path>?immutable=1` with
  `SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_URI`. Immutable
  = no locks, no `-wal`/`-shm`, no recovery. Measured 0/240 with eight
  processes, sidecars absent every round, also with stale sidecars present.
- `uriEscapePath()` percent-encodes `%`, `?`, `#` so any path round-trips.
- `sqlite3_busy_timeout(2000)` as belt-and-braces (never fires on an
  immutable file; measured as a working but rejected alternative because it
  still creates sidecars and still depends on NFS locking).
- Metadata check is honest: `SQLITE_ERROR` from prepare = "missing metadata
  table"; any other rc logs "could not read the metadata table" with the
  sqlite rc + extended code; a failed `sqlite3_step` is also reported
  separately instead of being treated as "no version row".

### Tests (tests/test_willeq_database.cpp, +4)
- `EightProcessesOpenTheShippedDbTogether` -- 8 forked readers x 5 rounds on
  the REAL `data/willeq.db` (path via `WILLEQ_DB_REAL_PATH` compile def,
  `tests/CMakeLists.txt`). fork, not threads: threads share SQLite's
  per-inode lock state and never see the race. RED 14/40 failures ->
  GREEN 0 (3 repeat runs).
- `ReaderCreatesNoWalOrShmFiles` -- RED (sidecars created) -> GREEN.
- `PathWithUriSyntaxCharactersOpens`, `MissingFileStillFailsToOpen`
  (the `OpenDatabase` legacy-fallback contract) -- pass both sides.
- T56 version-validation tests: unchanged, pass.

### Verification
- build: ok. ctest: **2945/2946** (ALSA `AudioManagerTest.PreloadCommonSounds`
  only). check_file_sizes: 2 ok. gles2-x86 / ARM / visual gate not run --
  the plan's bar is "Full ctest" and the change is a single TU in every
  tree (flagged, not absorbed).

### Deviations
- Plan Scope 2 named `SQLITE_OPEN_NOMUTEX`; **kept FULLMUTEX** -- T111
  moved to it for a live SIGSEGV (two threads share this one connection),
  and NOMUTEX would reopen that. The mutex flag is unrelated to the
  inter-process race.
- Builder untouched (outside territory). Recommendation for a later unit:
  `PRAGMA journal_mode=DELETE` before the final close in
  `willeq_db_builder` so the shipped file is not WAL-mode at all; with
  immutable=1 the client no longer cares either way.

### Side finding
- In T256's baseline ctest, `SpellsEffIndexResolutionTest.NegativeOne...`
  and `...SpellAffectIndex4...` were SKIPPED while a sibling passed; that
  suite's only skip path is `open()` failing on an existing file
  (`test_spells_eff_reader.cpp:255`). Under `ctest -j` each case is its own
  process opening the same db -- the same race. Both pass now (skips 47 ->
  43). Any "willeq.db not found" skip in an old ctest log is suspect.
