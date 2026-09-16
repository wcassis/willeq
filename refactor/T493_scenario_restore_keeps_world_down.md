# T493 — B784: `scenario.sh` restore actually keeps the world down (and takes 25 s, not 2 min)

Class A. Every live unit on the board runs through `scripts/scenario.sh`, and
its `restore_snapshot()` has two defects (B784, found by T492): it `pkill`s the
server processes inside the eqemu-server container, whose entrypoint
supervisor respawns them; and it restores one row per ssh round trip (~90 rows,
**measured 2 min**), so the world is back up before the loop ends and any
character that was IN WORLD gets its row written back over the restore
(T492 measured: Testbuddy zone 2/−64/428 instead of the snapshot's 36/0/0).
The correct recipe already exists and is measured at **25 s, byte-identical**:
`scripts/llmbench/teardown.sh` (T478's restore v2 did the same) — `docker stop`
the eqemu-server CONTAINER, leave mariadb up, send the restore as ONE statement
batch, snapshot again, diff.

**Lane: LOCAL eqemu** (`export EQEMU_HOST=local` before sourcing
`scripts/eqemu/env.sh`; clients use a config with `"host": "10.0.30.23"`; see
memory `eqemu-local-lane`). Do NOT touch the VM at 172.16.200.99 — another
session is using it. Worktree `wt/t4`, branch `task/T493-scenario-restore`.

## Tasks

1. **Reproduce first, on the local lane.** Write `scripts/eqemu/restore_selftest.sh`:
   up, snapshot (scenario.sh's `snapshot()` columns), log a headless client in
   as Testbuddy (`--scenario zone_in`, config `.local.json`, it changes zone/x/y
   on login), leave it IN WORLD, run the restore under test, kill the client,
   snapshot again, diff. With the CURRENT `restore_snapshot()` it must report a
   MISMATCH on Testbuddy's row (the B784 symptom). Record the run in
   `review/T493/` (before/after TSVs, timing).
2. **Fix `restore_snapshot()` in `scripts/scenario.sh`** the teardown.sh way:
   `eq_docker stop "${EQEMU_CONTAINER}"` (mariadb stays up), one `UPDATE ...;
   UPDATE ...;` batch built from `$BEFORE` for both tables, snapshot `$AFTER`,
   diff, then `eq_docker start` the container when `--keep-server`, else the
   existing `down.sh`. Factor the batch builder into `scripts/eqemu/env.sh` (or
   a sourced `restore_lib.sh`) so `scenario.sh`, `teardown.sh` and
   `record.sh` share ONE implementation — delete the duplicates in
   `scripts/llmbench/` and source the shared one. Keep the exit-66 semantics.
3. **Pin it.** `restore_selftest.sh` becomes the regression net: it must pass
   (byte-identical, and print the restore wall time, expected ≤ 40 s) against
   the fixed script, and it is wired where a user reaches it — a `--selftest`
   flag on `scenario.sh` that runs it, documented in the script header, and a
   line in `docs/` wherever live-unit procedure is described (grep for
   "restore with the world DOWN" and update every mention).
4. Run one real scenario end to end on the local lane through the fixed
   `scenario.sh` (`zone_in` is enough; `two_client_survive` if the buddy config
   works locally) and confirm exit 0 with the byte-identical line in its log.
5. Board: strike B784 (with the measured before/after timings); add entries for
   anything else found. Update the plan's Review with `[measured]`/`[assumed]`.

## Rules
- Lock: take the LOCAL lock (`eqemu_lock_take T493`, on this host's
  `/mnt/projects/.eqemu-lock` in local mode); exit 64 = stop. Release on every
  path. `down.sh` at the end.
- No Testchar (T489 evidence). No edits to the shared eq-oracle client copy.
- Do not touch `wt/slot1`, `wt/slot3`, `wt/t1`, `wt/t2` (dead executors'
  uncommitted work), `wt/slot2` (T479 held) or `wt/t3` (T492, resumable).
- Commit only scripts, docs, board, plan Review. Corpus-style outputs go to
  `review/T493/` with a README.

## Review
(executor)
