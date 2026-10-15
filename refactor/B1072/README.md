# B1072 (#392): the zone-in connect is bounded

Will, 2026-10-15 ~00:45Z, XU4 play test: zoning qeynos -> qeynos2 left the
client at 20 % "Connecting to zone..." until it was killed over ssh. The
server side (world assigned qeynos2 to two zone processes in one second; the
one the client reached logged nothing after "New connection") is a separate
follow-up and is NOT addressed here. This unit fixes the client half: no bound,
no retry, no way back.

## Mechanism (before this unit, gitlab/fable 05d46db9)

- `EverQuest::ConnectToZone()` (`src/client/core/everquest.cpp:829-845`) set
  `LoadingPhase::ZONE_CONNECTING` (20 %, `bridge_component.cpp:962`), built a
  `DaybreakConnectionManager` and called `Connect()`. Nothing else ran on a
  clock. The phase ends only when the zone's PlayerProfile arrives
  (`ZoneProcessPlayerProfile`, `src/client/zone/zone_entry.cpp:278`, sets
  `ZONE_RECEIVING_PROFILE`).
- **Session never comes up (dead port).** `DaybreakConnectionManager::Process`
  (`src/common/net/daybreak_connection.cpp:147-152`) drops a `StatusConnecting`
  connection after `connect_stale_ms` (5000, `daybreak_connection.h:300`) by
  ERASING it and then setting `StatusDisconnecting`; an erased connection is
  never visited again, so `StatusDisconnected` never fires and the reconnect
  in `ZoneOnStatusChangeReconnectEnabled` (`everquest.cpp:873-881`) never runs.
  Silence, forever.
- **Session up, zone never answers (the XU4 case).** `StatusConnected` sends
  stream-identify, ack and `OP_ZoneEntry` (`everquest.cpp:855-871`); if the zone
  process never processes it, no PlayerProfile comes and nothing times out
  (`stale_connection_ms` = 60000 only fires when NOTHING is received, and the
  server's Daybreak layer keeps the session alive).
- The only zone timer, `kZoneOutHandshakeTimeoutMs` (2000,
  `include/client/zone/zone_component.h:208`, used at `zone_component.cpp:574`
  and `movement_update.cpp:126`), bounds the zone-OUT hand-off, not the
  zone-in.

## The fix

- `include/client/zone/zone_connect_watchdog.h`: the pure deadline (Arm per
  ZoneServerInfo, `Tick(connecting, now)` -> None / Retry / GiveUp; leaving
  ZONE_CONNECTING disarms).
- `ZoneComponent::kZoneConnectTimeoutMs = 30000`, `kZoneConnectRetries = 1`,
  next to `kZoneOutHandshakeTimeoutMs`.
- `src/client/core/zone_connect.cpp`: `OpenZoneConnection()` (the session,
  shared by `ConnectToZone` and the retry; a retry drops the old manager with a
  non-reconnecting handler first), `TickZoneConnect()` (ticked at the top of
  `MovementComponent::UpdateMovement()` via `ctx_.lifecycle.TickZoneConnect`,
  outside any packet callback), `AbandonZoneConnect()`: WARN naming
  host:port and the stuck stage, a chat line, `ZoneToZoneInProgress = false`,
  `DisconnectFromZone()`, then a FRESH world login -- `ReturnToCharacterSelect()`
  when the run can show character select (graphics, or the scenario lane's
  opt-in), `ReconnectWorld()` headless (the configured character re-enters by
  itself, as at startup). Never a hang, never a process exit. The world
  connection is re-made exactly the way the zoning flow and the B1005 camp
  return do (`LoginWorldComponent::ReconnectWorld`).
- The mid-session reconnect in `ZoneOnStatusChangeReconnectEnabled` is bounded
  too (it arms the watchdog if no zone-in attempt holds it).

## The timeout value

Not measured on the official client: no capture of it against a dead or
silent zone exists in `review/` or `docs/`, and lane 5 cannot point the
official client at a dead zone port. So the value is fable's own measured
worst case, doubled: `review/B1072/zone_connect_interval_census.txt`
(`review/B1072/p56.py` over every `*.log` under `/mnt/projects/fable/review`):
1,663 ZONE_CONNECTING -> ZONE_RECEIVING_PROFILE intervals, median 0.598 s,
p99 4.641 s, max 14.397 s (`review/UIGAP-live/pickup_roundtrip/...`). 30 s is
twice the max; with the retry a dead zone costs ~60 s before character select.

## Pins

- `tests/test_zone_connect_timeout.cpp` (13 cases): the watchdog on a test
  clock; a headless `EverQuest` driven through the world replay seam
  (ZoneServerInfo -> wait -> one retry to the same host:port -> character
  select on a fresh login, zoning = 0, no quit; headless variant; a zone that
  answers in time is left alone; the lever); the `zone_connect_timeout`
  scenario over a fake world (pass, no retry, early give-up, hang, lever not
  taken).
- `tests/test_scenario_state_machines.cpp`: registry count 40 -> 41.

## Live run (lane 2, 2026-10-15 01:51Z, `EQEMU_LANE=2 bash scripts/scenario.sh zone_connect_timeout --unit B1072`)

PASS, exit 0, 95 s (`review/B1072/live_run1.txt`, client log
`review/B1072/scenario-lane2/zone_connect_timeout_client.log`):

    01:51:53.492 [WARN ] [ZONE] zone_connect_timeout lever: connecting to 172.16.200.98:1 instead of the ZoneServerInfo port 7012
    01:52:23.505 [WARN ] [ZONE] Zone server 172.16.200.98:7012 did not complete the zone-in within 30000 ms, stuck at UDP session (no session response); retrying the connect (attempt 2/2)
    01:52:53.514 [WARN ] [ZONE] Zone server 172.16.200.98:7012 did not complete the zone-in after 2 attempts of 30000 ms, stuck at UDP session (no session response); returning to character select
    01:52:53.514 [INFO ] [WORLD] [CHARSELECT] zone connect failed: back to character select (return #2), reconnecting to world
    VERDICT pass: dead zone port -> 2 connects -> character select (fresh list) at 60.6824 s -> Enter World Testbuddy -> zoned in to 'qeynos' 1.13272 s later

Snapshot restored byte-identically; lane-2 lock released.

## Not verified / left open

- The **silent-zone** shape (session up, OP_ZoneEntry unanswered -- the actual
  XU4 hang) is pinned by the unit test only; the live leg uses a dead port
  (no session). Both go through the same phase deadline.
- The **official client's** own bound and message are not measured (assumed
  conservative, above). If an oracle capture ever shows a different bound,
  the constant is one line.
- **Graphics run**: the character-select SCREEN coming up after the give-up is
  the B1005 camp path (`ReturnToCharacterSelect`), exercised live headless via
  the scenario opt-in only; no rendered capture was taken.
- **Headless without the scenario**: a permanently dead zone loops (fresh
  login, auto Enter World, ~60 s per cycle) rather than sitting at a screen
  that does not exist; bounded per cycle, never a hang, but not asserted live.
- **Server side** (separate follow-up, #392): world's double "Setting zone
  process to Zone [North Qeynos]" in one second and the assigned dynamic zone
  never booting. Not touched.
- The XU4 was not used (Will's box; asleep).
