# T331 — Re-measure the login z-compounding (B049): does T311's fix hold?

Class A suspicion, measurement first (`verify-dont-report`). Needs the
eqemu lock; slot1. No code change in this unit.

## What is known

- T311 Leg B (2026-09-02) matched the official client's wire z
  (`groundZ + 0.625*size`) and reported the per-login compounding closed.
- T328 Leg B (2026-09-04) found `Testchar` at `z=11.8806` after being left
  at `z=3.751` — exactly 13 × 0.625 — across the sessions between.

## Measure

1. Lock eqemu (noclobber). Start the server (`scripts/eqemu/up.sh`).
2. Read `Testchar`'s persisted position from the DB (the `character_data`
   row: `x, y, z, heading, zone_id`) — record it.
3. Run fable headless (`build/bin/willeq --no-graphics`, account
   willeq_test / Testchar), let it fully zone in, wait 10 s, `/camp` or
   quit cleanly. Read the DB row again. Repeat the login/quit cycle
   **three times**, recording z after each.
4. If z grows by 0.625 per cycle: the writer. Instrument by reading the
   packet log (`--log-level` for NET at DEBUG) for the FIRST
   `OP_ClientUpdate` fable sends after zone-in, and the `OP_ZoneEntry` /
   `OP_PlayerProfile` z it received; diff them. Name the code path that
   adds the offset (`movement/` `SendPositionUpdate` packing, or a
   `SetPosition` on zone-in that re-applies `+0.625*size` to an already
   adjusted z).
5. Control: the same three cycles with the OFFICIAL client under Wine
   (T328 Leg A's rig), reading the DB row after each — the oracle's z must
   stay constant. If the official client ALSO drifts, the server is the
   writer and B049 is closed as "not fable".
6. Restore `Testchar` to `zone_id=36, x=-77.9232, y=21.9086, z=3, h=190`
   (SELECT first — it may not be there); `down.sh`; delete the lock.

## Deliverable

`review/T331/README.md` with the z table per cycle (fable, official),
the packet excerpts, and the named writer or the "server" verdict;
B049 updated on the board (fixed in a follow-up unit if fable is the
writer — that fix is NOT this unit).

STOP: lock held; the client cannot log in; the DB row cannot be read.
