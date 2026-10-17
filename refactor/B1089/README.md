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
  LEASE_EXPIRED; 64 usage/identity; 65 unreachable, OR a release that
  returned 0 but whose re-show still names this exact identity (a silent
  ssh no-op must never claim the lane free — the verified release, below).

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
- **Lane 5, local-disk store** (`/opt/akk-stack/.eqemu-lease/lane5`,
  ThinkPad NVMe, never `/mnt/projects`): take -> show (state=HELD,
  host=thinkpad) -> release -> state=FREE, all driven FROM the ThinkPad
  (2026-10-17 04:31Z). That exposed the last client bug: a client on the
  lane's own host sshed to itself and 65'd; `_lease_client_cmd` now runs
  the core in-process when `hostname` / `hostname -I` matches the lane
  host (host check, not lane — lanes 1-4 never fire it).
- **Lanes 1-3** (2026-10-17 04:37Z): take -> show -> release -> FREE each,
  through the same helper (lane 4 carries the long proof above).
- **The status view** (`scripts/eqemu/leases`, from hermes-ext,
  `leases-final.txt`): one line per lane 1-5 + XU4, parallel ssh, no hang,
  all FREE at capture.
- **Hermetic net + docs green in CI's own environment** (rbuild on the
  ThinkPad, 2026-10-17 04:29Z): the full hygiene label 39/39 PASS,
  `LeaseCoreSelfTest` among them. The two failures it first caught were
  this unit's own: `DocCounts` (the new test takes the fragment 39->40,
  `docs/module_map.md` corrected) and `OracleSessionSafety` (B1089's
  `oracle_session.sh` renamed the gate's "lane lock held" to "lane lease
  held" and starts the renewer; the test's stub updated in lockstep).

## Not verified / left open

- **A real `scenario.sh` or oracle 20-min run over the live renewer is NOT
  among the evidence.** The long proof proves the lease mechanism with a
  60-s renewing holder; the converted live runners (`scenario.sh`'s wait
  loop, `oracle_session.sh`'s renewer, the ledger/pose/sweep runners)
  have been converted and their wrapper path is proven by the local
  smoke (`lease_smoke_local.sh`), but no 20-min live scenario was re-run
  under the new lease in this card's window (the lanes were shared with
  other cards). The first real scenario run on any lane is the remaining
  proof.
- **The reap's per-host ssh kill is proven by the ssh-shim hermetic case
  and by a live `sleep` on lane 4 (the proof's (iii))** — but never against
  a real `parecord` / `eqgame.exe` / Xvfb process. The tracking call
  (`eqemu_lease_track <run> <pid> <purpose> [host]`) is present in the
  call sites; its live end-to-end reap against an oracle process is the
  next thing to exercise (a FOLLOW-UP, not a card).
- **XU4's lease is proven (take/show/release on the box), but the XU4
  has no renewer yet** — its callers are short operations (a single ssh),
  so TTL 900 s is plenty; a future long XU4 operation must start the
  renewer like the lane scripts do.
- **The Pi lanes were NOT moved onto the helper** (scope: "PI_LANE
  devices only if you move pi_lock_* onto the helper"). `pi_lock_*`
  stays as-is; `scripts/eqemu/leases` reports only the eqemu lanes + XU4.
- **Lane 4's stack was left as found** (down, per the card); every proof
  release verified FREE by re-show (the verified release, not just rc=0).

## Proposed solo-prompt sentence

For the solo prompt's lane section (edited by Fable/Will, not this card):

> "Lanes are LEASED, not locked (`docs/ci.md` 'Lane leases', B1089): take
> with `eqemu_lock_take <owner>` (or `bash scripts/eqemu/lease take
> --lane N --card <card> --run <run> --purpose ...`), leave the lane free
> on the way out (`eqemu_lock_release` — a verified release; rc=65 means
> it did NOT free), and if a lane is HELD by a card that is no longer
> running, `bash scripts/eqemu/lease reap --lane N --dead-runs <file>`
> returns it (releases the lease and kills exactly that dead run's
> tracked pids; a live run's processes are never touched). A quiet lane
> frees itself at the 900 s TTL; a held lane renews itself — an error 63
> ('lease expired, re-lease: ...') means your lease lapsed, so re-take
> and re-do the operation. Never write to a lane's store path by hand;
> a hand-rolled ssh gets no renewal and loses the lane at TTL."
