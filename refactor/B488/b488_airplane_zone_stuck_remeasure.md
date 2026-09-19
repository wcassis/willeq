# B488: `#zone airplane` stuck at Phase 5/15 — re-measured, NOT reproducible, owner identified

Card: kanban t_fc496f97 (issue #36). Lane: vm (172.16.200.99).
Session: 2026-09-19 16:06–17:00 UTC (server local 11:06–12:00).
Binary under test: this worktree built from base **ac34acab** (branch
`task/kb-b488-airplane-zone-stuck`, which has no production-code changes —
see "What the tree is" below).

## Verdict

The defect as filed does not reproduce on this build, under this rig, against
this server fleet, in either direction (qeynos2 -> airplane, airplane ->
qeynos2, 4 full round-trips, 6 successful airplane entries total across two
client sessions). The most consistent owner of the ORIGINAL observation is the
**B483 double-world race, fixed by T457 (8900efa3, merged 728749e6)** — which
is an ancestor of this branch's base. T457's unit plan named B488 as item 3
and explicitly did not start it; it never got a live re-measurement until now.

## What the tree is

[measured] `task/kb-b488-airplane-zone-stuck` = ac34acab + docs/evidence only
on the plan to close this card. The zone-change client path
(`src/client/core/everquest.cpp`, `src/client/login_world/`,
`src/client/zone/`, `src/client/bridge/bridge_component.cpp`) has **zero**
commits between ac34acab and origin/fable tip bb7ea554, so a build from
ac34acab is byte-equivalent to a build from the tip for every line that can
affect this path.

[measured] T457's fix is in this build: `git merge-base --is-ancestor 8900efa3
ac34acab` -> true. That fix (a) makes `scripts/eqemu/up.sh` adopt a running
fleet instead of starting a second world, and (b) fixes the B482
zone-change-with-inventory-open segfault that T449 found in the SAME session.

## Reproduction attempts (both failed)

Rig: Xvfb :77, llvmpipe EGL, GM char Testchar (account willeq_test, dbid 1)
at level 50 via `#level 50` (GM 200, confirmed by the server's own
`admin [200]` line), `#zone airplane` typed into the live chat line.
Fleet per T457's adoption: **one** world (pid 142, `./bin/world` started by
`up.sh`), one loginserver (pid 229, `./bin/loginserver`), 27 launcher
`bin/zone` processes.

### Client side, session 1 (16:06–16:20, /mnt/projects/fable/review/kb-b488/client.log)

[measured]
```
[16:09:53.424] [ZONE] Zone change APPROVED to zone 71
[16:09:55.091] [MAIN] [LOADING] Game state setup complete. Spawn ID: 134, Zone: airplane
[16:09:55]     [LOADING][Phase 15/15] (100%) Ready!      <- ~1.5 s after approve
/loc in airplane: 10.0, 0.0, 4.4                          <- T449's own arrival coords
[16:15:25.552] [ZONE] Zone change: airplane -> qeynos2    <- control, AC4
[16:15:5x]     #zone airplane again: Phase 15/15          <- second entry, same session
```

### Client side, session 2 (16:22–16:34, client2.log)

Fresh process (clean state, no carried-over UI prefs of consequence).
[measured]
```
[16:25:53.681] Game state setup complete. Spawn ID: 278, Zone: qeynos2   (control, AC4)
[16:25:58.253] [LOADING][Phase 15/15] (100%) Ready!
[16:28:30.802] Game state setup complete. Spawn ID: 138, Zone: airplane
[16:28:33.676] [LOADING][Phase 15/15] (100%) Ready!        <- 2.9 s after approve
[16:31:4x]     /loc in airplane: 1415.0, 614.0, -650.6 (fable x/y swapped)
[16:34:21.030] /camp -> clean logout
```

Zero `[ERROR]` / `[FATAL]` lines in either full log. One benign
`Spawn ID changed unexpectedly` WARN (normal for a re-zone: the new zone
process assigns a new spawn id — 278 -> 138, then 134 -> 138 in session 1).

### Server side (same moments; server local time = UTC-5)

[measured] world log `logs/world_142.log` (pid 142, the up.sh world):
```
[11:28:29] (Testchar) [Zoning to] [airplane] (Zone ID [71]: Instance ID: [0])
[11:28:29] Sending client to zone [airplane] ([71]:[0]) at [172.16.200.99]:[7001]
[11:28:29] Online status [willeq_test] (70) status [InZone] (5)   <- 1 s later
```

[measured] the airplane zone process's own log
(`logs/zone/airplane_version_0_inst_id_0_port_7001_189.log`, **zone pid 189**):
```
[11:28:29] ZoneToZone ... requested_zone [airplane] current_zone [qeynos2] response [1] admin [200]
[11:28:30] New connection from address [172.16.200.100] port [2465]
[11:28:30] New client from [172.16.200.100]:[2465]
```

[measured] `ss -ulnp` at the moment: `0.0.0.0:7001 users:(("zone",pid=189))`.
So: world told the client 172.16.200.99:7001; the client connected to exactly
that; exactly one zone process owns that UDP port and it accepted the client
within 0.5 s of the client's "Connecting to zone server" line. Every
AC2-bucket is ruled out directly:

| bucket (AC2) | this measurement |
|---|---|
| zone process never booted | **false** — pid 189 booted 11:09:06, alive, owns :7001 |
| client connected to wrong addr:port | **false** — told 7001, dialed 7001, 189 owns 7001 |
| client never sent the connect | **false** — zone log has "New connection ... New client" |
| handshake stalled at packet X | **false** — "Zone connected" 0.5 s after connect; player data 100 ms after that |

### The "stuck forever" mechanism (why the original symptom looks permanent)

[assumed, from code] The client's zone path has **no timeout**. `ConnectToZone()`
(`src/client/core/everquest.cpp:816`) creates the UDP DaybreakConnection and
waits for the server's `ZoneEntry` response; nothing in
`ZoneOnStatusChangeReconnectEnabled` (everquest.cpp:840) or the
DaybreakConnection layer bounds how long Phase 5/15 ("Connecting to zone",
set at everquest.cpp:820) may stay. UDP "connect" cannot fail: it is a local
socket bind with no RTT, so a wrong/dead port produces a *silent* stall, not
an error — exactly T449's symptom ("no further loading phase is printed").
This is why a zone hand-off to a port nobody answers on would look like the
client is broken.

## Owner of the ORIGINAL observation

The original log is not available to me (the card says so: it is on NFS at
`review/T449/logs/t449_control_inventory_closed.log`; that file does not
exist on this host, and `review/T449/` here carries only a subset of T449's
files). What is available: T449's own README, which documents that session:

[measured] `review/T449/README.md` §5 (the scenario table):
> `zone_in` (first attempt) | 3 | **CONTAMINATED, discarded** — an earlier launcher fleet of mine
> was still up, so `up.sh` started a second world; the client logged in and
> got "could not find world server WillEQ"

and `refactor/BOARD.md` B483 (struck by T457):
> `up.sh`'s default path then launches `./bin/world` and `./bin/loginserver`
> anyway... **Which world a client reaches is a race.**

The airplane attempt happened in that same contaminated session, after two
other runs had been lost to the double-world race. Under B483's mechanism:
two `bin/world` processes exist; a world hands a client off to a
`zone=71 port=P` that the *other* world's fleet never assigned (or a
zone-71 process the hand-off world does not own); the client dials the port;
nobody answers (UDP cannot fail); Phase 5/15 forever; the renderer stays on
the old zone; `/loc` still works because it is served by the connection that
IS working. Every clause of the filed symptom is produced by that mechanism
without touching any client code, and T457's fix (adopt the running fleet:
**one** world, proven in its review section) removes it. The client code that
could own this path was unchanged by any commit between T449's base
(a714738c, 2026-09-07) and this build except for B850 log-module naming,
T491/T483/T494 (unrelated to zone connect), and B839/B841 (memory accounting)
[measured, `git log a714738c..ac34acab` over the zone paths].

[assumed] I cannot paste the original log's two-sided lines, because the log
is not on this host; the owner statement is therefore an inference from
T449's own recorded contamination plus T457's fix plus this negative
re-measurement. That is flagged as the one inferred link in this card.

## What was NOT done (and why)

- **No client-side RED/GREEN test.** AC3's test requirement attaches to a
  CLIENT-side owner. The live evidence points at the (already-fixed)
  server-fleet race, and the client path cannot be shown to be broken at all
  on this build. Writing a synthetic "server never answers" test would only
  pin the (correct) absence of a timeout — out of scope, and a timeout
  itself is a design question (board it, don't invent one).
- **The second-world re-creation was tried and correctly refused by the
  host**: a manual second `./bin/world` exited immediately on the port bind
  [measured], and the launcher fleet now auto-adopts; today's lane cannot
  manufacture the pre-T457 state without reverting up.sh, which is exactly
  what T457's review section already proved both ways on a cold container.
- **B453's `/dumpscene` unblock** (why B488 was boarded): the original
  B488 entry says B453's /dumpscene-in-sky proof was blocked on airplane
  zone-in. It is no longer blocked: airplane zones in cleanly on this build,
  including the map the B453 proof needs — [measured] `Successfully loaded
  map for zone: airplane`, `Loaded zone 'airplane' in 48ms (7611 BSP regions,
  15221 nodes)`, `Loaded Navmesh V[2] .../airplane.nav`. (Note: the "sky has
  no BSP" concern in T457's plan is answered by this: airplane.map loads with
  7611 BSP regions. T440's "no BSP" measurement must have been a different
  artifact or a pre-map-generation state.)

## Server-side remedy (smallest, AC3's server-side branch)

None is needed: the remedy (T457's `up.sh` adoption + B504's
`eqemu_zone_booted` pin, merged) is already on fable. If a Phase-5 stall is
ever observed again on a modern fleet, the triage order is:
1. `ps` in the eqemu container: exactly one `bin/world`, one
   `bin/loginserver` (a second is B483 regressed — check the up.sh history);
2. `ss -ulnp | grep :700X` and the zone process's own log line
   `airplane_version_0_inst_id_0_port_XXXX_*.log` — is the pid that owns the
   port the one the world's `Sending client to zone` named?
3. only then suspect the client.

## Lane state at finish

[measured]
- willeq: dead (process gone; log ends "IrrlichtRenderer shutdown"); Xvfb :77 left up (harmless, display-only) — actually killed below at teardown.
- Testchar restored to the T449 snapshot row: level 20, zone 2
  (qeynos2), (-74, 428, 3.75) h160 — re-read from mariadb after the UPDATE,
  before `down.sh` [measured]; no client was connected when down.sh ran, so
  no in-world write-back could have overwritten the row.
- stack DOWN: `down.sh` exit 0, `docker ps -a` empty.
- lock released: `eqemu_lock_show` -> empty (rc=1).
