# K10 — `LoginWorldComponent` (family: login_world — login server, world server, character select)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED through
K06/K07). Wave 3; runs alongside K08. **K09 waits for both.**

## Measured at `feb6b10b` (2026-09-03) — re-measure at your tip

One TU, `eq_login_world.cpp`, 37 bodies. Almost every method is internal to
the family (`Login*`, `World*` handlers and senders). External callers found
by grep: `eq.cpp` (the connection-manager callback wiring for
`LoginOnNewConnection`, `LoginOnPacketRecv`, `LoginOnStatusChange*`,
`WorldOn*`), `eq_zone.cpp` (`WorldOn*` — the zone-transition path
re-enters world), `eq_player_state.cpp` (`WorldProcessZoneServerInfo`).
Compiler confirms.

Members:
- **move** (single-family): `m_character_select_index`,
  `m_character_select_list`, `m_login_sequence`, `m_mail_key`,
  `m_ucs_connection_type`, `m_ucs_host`, `m_ucs_port`, `m_world_servers`.
  ⚠ the three `m_ucs_*` feed the DEAD `UCSConnection` (BOARD B005); move
  them anyway — deleting is B005's job.
- **decision — the connection managers.** `m_login_connection`,
  `m_login_connection_manager`, `m_world_connection`,
  `m_world_connection_manager`, `m_world_ready`, `m_world_server_host`,
  `m_enter_world_sent`, `m_zone_server_host/port`, `m_user`, `m_pass`,
  `m_server`, `m_host`, `m_port`, `m_key`, `m_dbid`, `m_session_recorder`,
  `m_zone_to_zone_in_progress`. Readers outside the family: `eq.cpp`
  (ctor/teardown/tick), `eq_zone.cpp` (zone transition), `eq_commands.cpp`
  (`/who`-style status), `ucs.cpp` (dead). The batch doc says the
  connection managers "move with it". **Do it in this shape:** the login
  and world managers + their connections + credentials + `m_world_servers`
  move into the component; `EverQuest` keeps `m_zone_connection` /
  `m_zone_connection_manager` (core) and `m_session_recorder` (core-shaped,
  ctor-owned). `eq.cpp`'s tick/teardown reach the moved managers through
  `LoginWorld().LoginManager()` / `.WorldManager()` getters; `eq_zone.cpp`'s
  reads likewise. If the ctor wiring of the manager callbacks cannot move
  cleanly (the managers are constructed in `EverQuest`'s ctor with
  `std::bind`/lambdas capturing `this`), STOP and report the exact shape —
  do not half-move.
- `m_replay_suppressed_sends` (12 here, also zone handshake + player buffs)
  stays, `owner_.`; K09 decides.
- `m_x/m_y/m_z` hits are `PlayerState`'s, not `EverQuest`'s — ignore.

`DispatchLoginPacket` / `DispatchWorldPacket` / `ReplayLoginPacket` /
`ReplayWorldPacket`: the replay pair is the session-replay oracle's entry
(`test_session_replay`) — grep `tests/` for them and retarget.

Predicted `eq.h` delta: −37 declarations −8 members −(managers block if it
moves, ≈8) −banners +4 ≈ **−45 or better**.

## Territory

`include/client/login_world/` + `src/client/login_world/` (new),
`src/client/eq_login_world.cpp` (moves), `eq.h`, `eq.cpp` (ctor, tick,
teardown call sites), root `CMakeLists.txt`, `tests/test_login_world_component.cpp`
+ cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in
`eq_zone.cpp`, `eq_player_state.cpp`, `eq_commands.cpp`, and tests
(`test_login_world_seam.cpp`, `test_login_world_flow.cpp`,
`test_session_replay*.cpp` — call sites only, assertions untouched; if a
seam test reaches a private through `Test_*`, move the seam with it).

## Verification / Review

As K05. The login/world flow tests (T05–T09 family) must pass unchanged.
