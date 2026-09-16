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

Done on the LOCAL lane; the 172.16.200.99 VM was not touched. Lock taken and
released as T493 on this host's `/mnt/projects/.eqemu-lock`; the local stack is
DOWN (`docker compose down`, no containers left). Evidence and a README in
`review/T493/`.

**Task 1 — reproduce.** [measured] Reproduced, and the mechanism is worse than
B784 states. Two facts, both measured before anything was changed:

- [measured] The `pkill` chain **killed its own shell**. The container-side
  `bash -c "pkill -f /tmp/spire-launcher; pkill -f 'bin/world'; ..."` matches
  the FIRST pattern against its own command line: the exec returned **143** and
  **27** world/zone processes were still alive afterwards. Nothing was ever
  killed, so the supervisor's respawn (B483) never even came into it.
- [measured] **The zone writes a character back when the ZONE shuts down, not
  when the client disconnects.** With Testbuddy in world, its row was moved to
  a sentinel and polled for 60 s after the client was killed — untouched — and
  changed to the zone's copy within 5 s of the zone getting SIGTERM. So the old
  restore printed `snapshot restored BYTE-IDENTICALLY` and `down.sh` then undid
  it. That is why the after-snapshot diff had never caught this in six months:
  it runs BEFORE the teardown that does the damage.

  [measured] The reproduction run (`scripts/eqemu/restore_selftest.sh --legacy`,
  `review/T493/restore_selftest/`): restore **22 s**, 27 world/zone processes
  alive at the end of it, and one mismatch row —
  `68 Testbuddy 36 1111 2222 3 0 1 0` restored, `68 Testbuddy 36 0 0 3 0 1 0`
  after the teardown. Exit **66**.

  [measured] The **2 min** in B784 is the REMOTE lane's figure: the per-row loop
  costs one ssh round trip per row there and one local `docker exec` here, so it
  is 22 s on this lane. The defect does not depend on it — 22 s was plenty.

**Task 2 — fix.** ONE implementation, `scripts/eqemu/restore_lib.sh`
(`eqemu_sql`, `eqemu_snapshot`, `eqemu_inventory_snapshot`,
`eqemu_restore_sql_from`, `eqemu_inventory_sql_from`, `eqemu_inventory_restore`,
`eqemu_world_stop`/`eqemu_world_start`, `eqemu_restore_snapshot`), sourced by
`scripts/scenario.sh`, `scripts/llmbench/teardown.sh` and
`scripts/llmbench/record.sh`. [measured] `grep` for the old awk builders and for
`eq_docker stop|start` returns nothing outside the library. `eqemu_world_stop`
is idempotent, which is what lets `record.sh` keep doing its spawn/corpse/
inventory work between the stop and the restore. Exit-66 semantics kept.
Two deliberate behaviour changes beyond the plan, both in `scenario.sh`:
the restore now runs on the `--keep-server` path too (it was skipped entirely,
so `--keep-server` left a moved character behind for the next unit), and the
binary is resolved through `$WILLEQ_BIN` then `main/build/bin/willeq`, because a
task worktree has no `build/` and the lane was unrunnable from one.

**Task 3 — pin.** `scripts/eqemu/restore_selftest.sh`, reached as
`scripts/scenario.sh --selftest`. It does NOT wait for a save to land (that is a
transient): it logs a character in, makes the snapshot and the world disagree on
purpose in SQL, kills the client in scenario.sh's own order, restores, runs
`down.sh`'s SIGTERM step, settles, and only THEN diffs. It asserts three things:
byte-identical through the teardown, **zero** world/zone processes alive when
the restore finished, and the restore's wall time inside `--max-seconds`
(default 40). It puts the database back to PRISTINE on every exit path and
verifies that too. `--legacy` runs the pre-T493 restore and fails, which is how
the net is known to test something. [measured] fixed run: **17 s, 0 processes,
byte-identical, exit 0**.

**Task 4 — a real scenario.** [measured] `scripts/scenario.sh zone_in --timeout
120 --unit T493 --config scripts/scenario_configs/peer.local.json`: client exit
**0**, verdict `zoned in to 'befallen'`, `snapshot restored BYTE-IDENTICALLY`,
**44 s** total including up.sh and down.sh. Logs in
`review/T493/scenario/`. [assumed] `two_client_survive` was not run — one
account pair, one lane, and `zone_in` is what the plan calls enough.

**Task 5 — board and docs.** B784 struck with the measured before/after. Two new
entries: **B797** (A) `eqemu_port_open` cannot tell a live stack from a dead one
on the local lane — 5998/9000 are published ports, so `docker-proxy` answers the
probe with world/zone/loginserver all at zero [measured]; `up.sh` prints
`port ...: ready (0s)` and exits 0 on that evidence and `record.sh` skips
`up.sh` on it, and this unit lost a 3.5-minute run to exactly that. **B798** (B)
`down.sh`'s `pkill` pattern is the one that does not match its own wrapper, and
nothing in the tree says so. CLAUDE.md's scenario-lane section now spells out
what "restore with the world DOWN" means, names the shared library, gives the
`--selftest` command and the three extra exit codes. [measured] Every other
`"world DOWN"` string in the tree is inside a COMPLETED unit plan
(T386/T389/T391/T405/T418/T422/T426/T428/T431/T433/T438/T449) describing what
that unit did; those are historical records, not live procedure, and were left
alone. `docs/` has no live-unit procedure text of its own —
`docs/decisions.md` points at CLAUDE.md for the lane.

**[assumed] items.**
- [assumed] That the remote (VM) lane behaves the same way. Everything here was
  measured locally, because the VM is held by another session. The recipe is
  T478's and T492's, both proven there, and the only lane-dependent number is
  the per-row loop's cost (2 min there, 22 s here).
- [assumed] The 40 s default budget in the selftest. It is ~2.4x the measured
  17 s, picked for headroom over ssh, not measured as a threshold.
- [assumed] That `eqemu_inventory_*` behave identically after the move into the
  library: only `record.sh`/`teardown.sh` use them and neither was run live in
  this unit (the llmbench rig needs the oracle client and hermes). The bodies
  are byte-identical to the ones deleted; the call sites were re-pointed, not
  rewritten.
- [assumed] That `--keep-server` now works end to end in `scenario.sh`. The
  restore-then-`eqemu_world_start` path was exercised repeatedly by the selftest
  (same two calls, same order) but not through `scenario.sh` itself.

**Orchestrator verification 2026-09-16 23:02 UTC [measured, local lane]:** `scenario.sh --selftest` PASS (17 s, 0 world processes, byte-identical through the teardown); `restore_selftest.sh --legacy` exit 66 (the B784 symptom reproduced); lane left clean. Found and fixed one lane-safety defect in the new selftest: with `EQEMU_HOST=local` and no `--config` it defaulted to `peer.json` (the VM) and pointed a client at 172.16.200.99 for 180 s (harmless only because that stack was down). Default now follows the lane and a host guard refuses a config whose host is not the lane's server (`LANE MISMATCH`, exit 65, verified).
