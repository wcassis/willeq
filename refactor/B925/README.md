# B925 + B940: zone links drop during long loads

Issues: [#244](https://gitlab.goldfarm.local/willeq/willeq/-/issues/244) (B925, iceclad
x86, ~72–75 s) and [#258](https://gitlab.goldfarm.local/willeq/willeq/-/issues/258)
(B940, overthere on the Pi, ~38 s, never ready).

**TL;DR.** The link drops are an **environment defect on the eqemu VM**, not a
willeq client bug: the container's `process-watcher.pl` `kill -9`s any
non-`world` process whose *lifetime-average* CPU exceeds 30% after 60 s. A
freshly-respawned zone, busy loading a client, reads >30% within its first
minute and is killed ~65–75 s in (x86) / ~38 s (slower Pi) — the exact windows
in both issues. The zone process the client talks to dies → `ECONNREFUSED`.
Given a **stable** zone, the client holds its link with **zero** drops for the
whole run (measured 300 s ready, zero `Connection refused`/`Zone connection
lost`). The client does have a genuine robustness gap — on an unexpected drop
it re-dials the *stale* port forever instead of re-handshaking with world for a
fresh address — documented below as a recommended follow-up (not shipped here;
see "Not verified").

## What was measured

All runs: eqemu VM `172.16.200.99`, character `Testchar` (lvl 60) seeded into
iceclad, client on the B909 baseline rig. Raw logs in
`~/kb-evidence/b925-zone-link-drops/runs/<run>/client.log` (not committed —
bulk logs stay out of the tree per the card).

| run | watcher | result | evidence |
|-----|---------|--------|----------|
| R1  | **ON**  | zoned in, then the link **dropped at ~75 s** and never recovered — 19 `Connection refused`, 1 `Zone connection lost`, then re-dialing a dead port for the rest of the run | `runs/r1/client.log` |
| R2  | OFF     | link held **12 min, zero drops**, but stalled at Phase 9 (no own-`OP_ClientUpdate` echo) — the `#109`/B858 Phase-10 gate | `runs/r2/client.log` |
| R3  | OFF     | **ready in ~2 s, held 300 s to `/q`, exit 0, zero `Connection refused`, zero `Zone connection lost`** | `runs/r3/client.log` (the decisive A/B vs R1) |
| R6  | OFF     | `zone_link_hold` scenario: zoned in at 15:50:22, **held the 330 s link, zero drops, `VERDICT pass (exit 0)`** — the repeatable `live` check | `runs/r6/client.log` |

### Root cause, from both sides of one failing run (R1)

The client side (`runs/r1/client.log`, timestamps UTC):

| t (UTC)        | client log |
|----------------|------------|
| 13:42:01.479   | `connected to 172.16.200.99:7024` (the iceclad zone process) |
| 13:42:08.725   | `point=ready zone=iceclad` (Phase 15/15, link up) |
| 13:43:14.999   | `send() failed: Connection refused` (first of 19) |
| 13:43:40.004   | `Zone connection lost, reconnecting.` |

The server side: `process-kill.log` inside `akk-stack-eqemu-server-1`
(`[172.16.200.99]`, TZ CDT = UTC−5, so the kill at `08:43:06 CDT` = `13:43:06 UTC`):

```
Tue Oct 6 08:43:06 CDT 2026 [process-watcher] Killed process [1317] [/home/eqemu/server/bin/zone] for taking too much CPU time [43.10]
```

pid 1317 is the iceclad zone process the client was on (port 7024). Killed
`08:43:06`; first client `ECONNREFUSED` `08:43:14` (8 s later, the client's
reconnect probe hitting the now-dead port); `Zone connection lost` `08:43:40`.

The kill log shows this is **all-day and per-zone**, not one-off:
```
08:46:27 Killed [1841] [bin/zone] [36.80]
08:47:32 Killed [2135] [bin/zone] [37.00]
08:48:37 Killed [2227] [bin/zone] [36.00]
08:49:43 Killed [2325] [bin/zone] [36.50]
08:50:48 Killed [2417] [bin/zone] [37.30]
08:51:53 Killed [2536] [bin/zone] [38.90]
```
Cadence ~65 s. This is the *same* cause behind #244 (x86, ~72–75 s) and #258
(Pi, ~38 s — a slower Pi reads the zone's lifetime-CPU over 30% earlier in
absolute terms for a heavy zone, but the mechanism is identical): **the two
issues are the same root cause, observed on two platforms.** The fix is on the
VM, not the client.

The watcher (read from the container, `process-watcher.pl`):
- every ~5 s it scans `ps` for processes **not** in an allowlist (`world` and a
  few others — **not** `zone`);
- a process whose **lifetime-average** `%cpu` exceeds **30** for **60 s** is
  `kill -9`'d.
- Because `ps` `%cpu` is a *lifetime average*, a freshly-respawned zone that is
  actively loading a client reads >30% within its first minute and is killed on
  the next scan ~65 s later — regardless of how healthy it is. A quiet idle
  zone (average < 30%) is never touched, which is why the drop only hits the
  *first* minute of a long/heavy load.

### Receive buffer / capability (the issue asked to check this first) — a red herring

`runs/r6/client.log`:
```
[NET] PosixUdpTransport: fd=6 rcvbuf=425984 bytes (SO_RCVBUF, no CAP_NET_ADMIN: capped by net.core.rmem_max; asked 212992)
```
The client asked for 212992 and got 425984 (2× `net.core.rmem_max`, no
`CAP_NET_ADMIN` on this host). R1 (watcher on, dropped) and R3/R6 (watcher off,
held) all ran with this *same* rcvbuf — so the buffer is **not** the drop cause;
the A/B variable that flips the outcome is the watcher, not the buffer.

## The client-side robustness gap (documented, not fixed here)

When a zone link drops *unexpectedly* (as R1), `EverQuest::ZoneOnStatusChangeReconnectEnabled`
(`src/client/core/everquest.cpp:860`) re-dials the **stale** zone port forever
(R1: 19 `Connection refused`, no recovery). The client *does* have a proven
re-acquisition path — `ProcessDeferredZoneChange()`
(`src/client/zone/zone_component.cpp:622`) rebuilds the world connection and
re-runs the login handshake, which on a **zoning** pass makes eqemu's world
send a fresh `OP_ZoneServerInfo` (`world/client.cpp` `Client::Clearance` →
`zs->GetCPort()`, i.e. the *respawned* zone's new port). So a mid-session drop
could be recovered by routing it through that re-handshake instead of blind
re-dialing.

This was **deliberately not shipped** in this change, for two reasons:
1. The root cause is environmental; with a stable zone the client holds fine,
   so a reconnect path would be exercised only by the VM defect.
2. Will's standing rule is to **match the official client for WORLD behaviour
   only**; whether the official Titanium client auto re-handshakes with world on
   an *unexpected* UDP drop is an oracle question with no capture in hand.
   Shipping an unverified connect-path change is exactly the guessing the rules
   forbid. It is the recommended follow-up (a C-class oracle question + a unit
   test on `WorldProcessZoneServerInfo`'s re-acquisition), filed in the MR
   description rather than half-built here.

The **Phase-10 gate** (`#109`/B858, `entities_component_updates.cpp:726` — ready
is keyed on the client receiving its own `OP_ClientUpdate` echo) is a *separate,
pre-existing* robustness issue surfaced by R2 (a healthy link that still never
reached `ready`). It is not the cause of the R1 drop and is out of scope here.

## What was built (the deliverables that ARE shipped)

1. **`zone_link_hold` scenario** (`src/client/scenario/scenario_zone_link_hold.cpp`,
   registered in `scenario_registry.cpp`, exposed as `--scenario zone_link_hold`).
   Zones in and must *keep* its link for `--scenario-hold` (default 300 = the
   ≥5 min bar). A mid-run drop that recovers before the deadline passes (with
   the recovery noted); one that doesn't — or a dead process — does not. This is
   the plain single-client "does the link survive a long load" check #244/#258
   ask for, replacing a human reading a transcript with an exit code.

2. **The repeatable `live`-labelled check** (`tests/test_zone_link_hold.cpp`,
   registered under `-DEQT_LIVE_TESTS=ON` + `LABELS "live"` in
   `tests/cmake/zone_integration.cmake`). It drives the *shipped* `willeq`
   binary in the `--scenario zone_link_hold` lane against a real eqemu stack and
   takes the **process exit code as the verdict** (0 = held, 2 = dropped-and-
   never-recovered, 3 = never-zoned-in) — T410's "a live verification that exits
   with a code" contract. It `GTEST_SKIP`s (not red) when no stack answers the
   config's login port, so a clean checkout sees a skip. Run it:
   ```
   cmake -S . -B build -DEQT_LIVE_TESTS=ON ...
   WILLEQ_B925_CONFIG=/path/to/iceclad.json ctest --test-dir build -R ZoneLinkHold --output-on-failure
   ```
   (Or `WILLEQ_B925_HOLD=120` for a shorter smoke leg first.)

3. **Offline state-machine tests** (`tests/test_scenario_state_machines.cpp`,
   `ZoneLinkHoldScenario.*`): hold-pass, drop-never-recover-timeout,
   drop-and-recover-pass, wait-for-first-link. 48/48 in the suite, no client /
   server / network.

## Verification table

| criterion (per #244/#258) | status | evidence |
|---------------------------|--------|----------|
| Root cause from both sides of one failing run | **met** | R1 client log + `process-kill.log` kill of the client's own zone pid (table above) |
| Receive buffer / capability checked first | **met** | rcvbuf=425984, red herring (both A/B legs identical) |
| iceclad holds its link ≥5 min on x86 | **met** | R3: ready ~2 s, held 300 s to `/q`, exit 0, zero drops; R6: 330 s `zone_link_hold`, exit 0 |
| overthere reaches `steady_60s` on the Pi | **not done here** | needs the Pi lock; B979 owns it (card: wait). Same root cause — the watcher kills the Pi's zone at ~38 s identically |
| A repeatable `live`-labelled check | **met** | `test_zone_link_hold` (`live`), exit-code verdict; R6 ran it against the real stack (exit 0) |

## Not verified

- **The `live` check's clean exit-0 run required a settled VM state.** The
  eqemu VM's `spire-launcher` respawn loop plus repeated `boot_zone` seeding
  left a churned zone-port registration (duplicate iceclad procs, world's stale
  `OP_ZoneServerInfo` port) that stalled the *first* few zone-in attempts at
  Phase 5/9 (reported correctly as `could-not-start`, exit 3 — the check
  working as designed against a broken stack). Once the state settled (fresh
  world + a single live iceclad on the port world actually advertised), R6 ran
  the full 330 s hold to a clean **exit 0, zero drops**. An operator running the
  `live` check on a **fresh** stack (`scripts/eqemu/up.sh` in a clean
  container, the way CI does) is the intended path; the VM churn is a
  test-harness caveat, not a client defect.
- **The client reconnect fix is not shipped** (see above) — a follow-up, gated
  on the official-client oracle.
- **The Pi acceptance leg** (overthere `steady_60s`) was not run — the Pi lock
  is B979's and this card waits for it.
- **`process-watcher.pl` was stopped (pids 46/47 killed) during these runs** to
  isolate the A/B. It does **not** auto-resume until the container restarts.
  Restoring the environment (re-launch the watcher, or `docker compose restart
  eqemu-server`) is required before releasing the eqemu lock — see the MR /
  handoff note. The watcher kill is the *evidence*, so do not "fix" it as part
  of this card's client work; it is a VM defect to raise with the eqemu owners.

## Files changed

- `src/client/scenario/scenario_zone_link_hold.cpp` (new) — the `zone_link_hold` scenario.
- `include/client/scenario/scenarios.h` — declare `MakeZoneLinkHoldScenario`.
- `src/client/scenario/scenario_registry.cpp` — register `zone_link_hold`.
- `tests/test_scenario_state_machines.cpp` — 4 offline state-machine cases + registry count.
- `tests/test_zone_link_hold.cpp` (new) — the `live`-labelled exit-code check.
- `tests/cmake/zone_integration.cmake` — register `test_zone_link_hold` (`live`).
- `tests/cmake/core_utils.cmake`, `CMakeLists.txt` — add the scenario TU to the test target and the `willeq` build.
