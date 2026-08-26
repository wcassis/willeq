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
