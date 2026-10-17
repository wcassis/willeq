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
- **The long proof** (`lease_integration_proof.sh`, lane 4, default
  900 s TTL, ~39 min, `proof-run1/`):
  - (i) idle PASS — a taker that never renews frees the lane at the 900 s
    TTL (verified by a second taker taking over at t=17m30s).
  - (ii) active PASS — a holder renewing every 60 s kept the lane for
    20+ minutes; steal attempts at t=4m/12m/20m all refused (rc=1, holder
    named).
  - (iii) reap — the FIRST run (proof-run1) failed at its very first step,
    before touching the lease: the plant command
    `setsid sleep 300 & echo $!` over ssh hangs, because the backgrounded
    child inherits ssh's stdout and ssh waits for that pipe to close. The
    plant is fixed (child fds redirected); `proof-run1/` is kept as the
    record, and the corrected run's output is `proof/` in the evidence
    dir.
