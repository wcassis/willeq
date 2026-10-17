# B1089 (#409) — the lane lock becomes a lease

The old eqemu lane lock was a noclobber-over-ssh file on a shared path:
check-then-take raced, a dead holder left the lane held forever (nothing
could tell HELD-but-dead from HELD-and-live), and the record's home
(`$EQEMU_HOST:/mnt/projects`) was NFS from lane 5's view. The lease
replaces it with a TTL-by-activity record that lives ON the lane's own
host. The design is `scripts/eqemu/lease_core.sh` + `lease_lib.sh`
(committed); this directory holds the evidence that the design holds.

## The contract (short version)

- One key=value record per lane under a per-store `flock`; EVERY mutating
  operation (take/renew/release/reap/track) is one flock critical section,
  so the check-then-take race is gone by construction.
- EXPIRY RELEASES THE LANE ONLY; NOTHING IS EVER KILLED BY EXPIRY. A
  record whose `last_touched` is >= `LANE_LEASE_TTL_S` (900 s) old is free,
  whatever pid it names.
- The live long run keeps the lane by renewing: `eq_remote` renews on
  every lane touch while `LANE_LEASE_AUTO_RENEW=1`, and long scripts start
  the background renewer. A stale holder's renew fails with 63
  (`LEASE_EXPIRED`) plus the re-acquire one-liner.
- `reap <dead-runs-file>` releases the named dead runs' leases and kills
  exactly their tracked pids (`track`); a live holder's lease and pids are
  untouched. A named host's pids are killed over raw ssh; an unreachable
  host is reported, never fatal.
- Exit codes: 0 ok; 1 held by another (named); 2 corrupt record; 63
  LEASE_EXPIRED; 64 usage/identity; 65 unreachable.

## Evidence

- **Hermetic net (CI)**: `scripts/eqemu/lease_selftest.sh` — ctest
  `LeaseCoreSelfTest`, label hygiene, no ssh/network. Covers
  take/refuse/show, owner-checked release, renew, TTL expiry + takeover,
  63 + re-lease one-liner, reap live-vs-dead, reap --dry (changes nothing,
  never reaches ssh), per-host pids (an ssh shim stands in for the host),
  the five-taker race (exactly one winner), corrupt records.
- **Live lifecycle** (`lease_live_lifecycle.sh`, lane 4, TTL=8 s): take ->
  refuse -> renew -> wait past TTL -> expired takeover names the former
  holder -> the old holder's renew is 63 with the re-acquire line ->
  re-take -> release -> FREE. PASS.
- **Local client end-to-end** (`lease_smoke_local.sh`): `eqemu_lock_take`
  -> `eqemu_lock_show` -> renewer start/renew/stop -> `eqemu_lock_release`,
  through the exact wrapper names the converted call sites use. PASS.
- **The long proof** (`lease_integration_proof.sh`, lane 4, default 900 s
  TTL, ~39 min). **PASS, run 7** (`proof-run7/`, 2026-10-17 03:18-03:59Z):
  - (i) idle PASS — a taker that never renews frees the lane at the 900 s
    TTL (a second taker takes over at t=17m30s; nothing is killed by
    expiry).
  - (ii) active PASS — a holder renewing every 60 s kept the lane for
    20+ minutes; steal attempts at t=4m/12m/20m all refused (rc=1, holder
    named).
  - (iii) reap PASS — a dead run's planted tracked pid is killed by reap
    and its lease released; a second reap with the same dead list leaves a
    LIVE holder's lease and pid untouched.

  The first six runs are kept in the evidence dir (`proof-run1/`,
  `proof/`, `proof-run5-corrupted/`, `proof-run6/`) — they are as much a
  record of what the proof CAUGHT as of the lease:
  1. run 1, (iii): the plant `setsid sleep 300 & echo $!` over ssh hangs —
     the backgrounded child inherits ssh's stdout. Fixed: child fds
     redirected (`plant()`).
  2. run 4, reclaim: `reap` 64'd on a `deadruns` file that WAS shipped —
     the core resolved the relative name against the ssh login CWD, not
     the store. Fixed: store-relative resolution + local-mode copy
     (`lease_core.sh`); red-before pinned by self-test case 10b.
  3. run 5: a release that returned 0 left the record in place (a silent
     ssh no-op) — a caller believed the lane was free while still held.
     Fixed: verified release (re-show, rc=65 if still ours, `lease_lib.sh`).
  4. run 6, (iii): the proof's direct ssh lines built `user@172.16.200.$LANE`
     — 172.16.200.4 does not exist (lanes are .99/.98/.97/.96/.205).
     Fixed: `LANE_SSH` resolved from the env.sh lane table.
  5. every run: the `trap ERR` on the steal-attempt line `out=$(take); rc=$?`
     wrote a spurious `exit=1` mid-run (VAR=$(cmd) failing fires the ERR
     trap; `cmd && {…}` does not — verified with a probe). Fixed: a single
     EXIT trap writes the real verdict once (verified with a 3-path
     harness); the exit file is now absent until the proof actually ends.

  Operational notes (these are MY mistakes, not the lease's): a
  `terminal(background=true)` process is reaped when the agent turn ends,
  so the proof must be launched detached (the `launch_proof_once.sh`
  guard in the evidence dir); and only ONE proof may run at a time — two
  share card ids + lane + log and collide (run 4/5). All of this is why
  the proof reclaims its own leftovers at start and the launcher refuses
  a concurrent start.
