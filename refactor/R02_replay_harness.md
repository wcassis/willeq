# R02 — Replay harness and game-state oracle test

Replay a session log (format: `include/common/net/session_log.h`) into a
headless EverQuest instance and assert the resulting game state. Ships green
using a synthetic session built in-test; automatically switches to
golden-snapshot mode when a real recording exists.

## Design

- **Replay seam**: public `void EverQuest::ReplayZonePacket(uint16_t opcode,
  const uint8_t* data, size_t size)` — dispatches exactly as
  `ZoneOnPacketRecv` does for an app packet with that opcode (reuse the
  dispatcher; examine how it extracts the opcode and enter below that
  point). Document in eq.h as the replay/test seam.
- **Null-connection safety**: replay runs with no network. Audit the zone
  handler send paths (`QueuePacket`, `SafeQueueZonePacket`) and any other
  m_zone_connection uses reachable from handlers; guard so a null connection
  no-ops safely. Add a counter (`m_replay_suppressed_sends`) so tests can
  assert how many replies were suppressed. m_bridge is already null-checked
  throughout (verify with a grep sample; fix any unguarded use you hit).
- **Test** `tests/test_session_replay.cpp` (register in tests/CMakeLists.txt,
  link the same core sources as other eq-level tests):
  1. **Synthetic mode (always runs):** construct
     `EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar")`-style
     headless instance (no InitGraphics, no connections). Build a synthetic
     session in-memory or via a temp file using session_log.h structs:
     NewZone → PlayerProfile → a few ZoneSpawns/NewSpawn entries →
     TimeOfDay → HP updates → DeleteSpawn, using packet_structs.h layouts.
     Investigate what ordering/flags the handlers expect
     (CheckZoneRequestPhaseComplete's m_*_received flags) and satisfy the
     minimum needed. Replay, then assert via public accessors: zone name,
     entity count and per-entity name/level/position, player profile stats
     (GetLevel/GetMaxHP/...), time of day, entity removed after DeleteSpawn.
  2. **Golden mode (activates when a real capture exists):** if
     `data/captures/session.sleq` exists (path overridable via
     `WILLEQ_SESSION_LOG` env), replay all Zone-stream S→C records, produce
     a deterministic state summary (sorted text: zone, player stats, entity
     table), and compare to `data/captures/session.golden`. If the golden
     file is missing, WRITE it and GTEST_SKIP with a "golden created —
     re-run to verify" message. `data/captures/` is gitignored (verify;
     data/eq_client precedent).
- Handlers that crash on partial/synthetic data: fix ONLY by adding bounds
  checks that are correct for real traffic too; document each.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions; the new
  synthetic-mode test must PASS (not skip).
- Review documents: seam location, every null-guard added, the synthetic
  packet sequence, every assertion, and any handler robustness fix.

## Review

### Seam location

- Public seam: `EverQuest::ReplayZonePacket(uint16_t opcode, const uint8_t* data, size_t size)`,
  declared in `include/client/eq.h:678-685` (doc comment + declaration),
  defined in `src/client/eq.cpp:714-723`.
- `ZoneOnPacketRecv` (`eq.cpp:700-712`) was split: it now only does the
  trace-log + `DumpPacket` bookkeeping and then calls a new private helper
  `EverQuest::DispatchZonePacket(const EQ::Net::Packet&)` (`eq.h:1173-1174`
  declaration, `eq.cpp:725` opening) which owns the ~845-line opcode
  switch (unchanged body, just re-hosted). `ReplayZonePacket` builds a
  synthetic `EQ::Net::DynamicPacket` (2-byte opcode prefix + payload, matching
  `session_log.h`'s on-disk layout exactly) and calls `DispatchZonePacket`
  directly, so replay runs through the identical switch/handler code the
  live connection uses. `conn` (the `DaybreakConnection` shared_ptr) was
  confirmed unused inside the switch body (grep audit), so nothing was lost
  by not threading it through.
- Counter/accessor: `uint64_t m_replay_suppressed_sends` (`eq.h:1314-1315`),
  read via `GetReplaySuppressedSendCount()` (`eq.h:690`).

### Null-connection guards added

Audited every S->C `ZoneProcess*` handler for sends it triggers as a side
effect (i.e. every place a decoded Zone packet's handler goes on to call a
`ZoneSend*` sender), by tracing all call sites of `ZoneSend*` functions
across `eq.cpp`/`eq_entities.cpp`/`eq_player_info.cpp`. That produced exactly
11 sender functions reachable from packet dispatch, all of which called
`m_zone_connection->QueuePacket(...)` directly (not through the already-null-
safe `SafeQueueZonePacket`). Each was guarded with an `if (m_zone_connection)
{ ...QueuePacket... } else { ++m_replay_suppressed_sends; }`, preserving the
existing "mark sent" flag semantics so replay doesn't re-trigger the same
send on a later packet of the same type:

| Sender | eq.cpp line (guard) | Triggered by |
|---|---|---|
| `ZoneSendZoneEntry` | 1603-1607 | `HC_OP_SessionReady` case |
| `ZoneSendAckPacket` | 1704-1708 | `HC_OP_SessionReady` case |
| `ZoneSendReqClientSpawn` | 1632-1636 | `CheckZoneRequestPhaseComplete()`, called from `ZoneProcessSendAATable`/`ZoneProcessRespondAA`/`ZoneProcessTributeInfo`/`ZoneProcessSendGuildTributes` (eq_player_info.cpp) |
| `ZoneSendClientReady` | 1649-1653 | `ZoneProcessSendZonepoints` fallback path and `ZoneProcessGuildMOTD` |
| `ZoneSendSetServerFilter` | 1674-1678 | same two handlers as above |
| `ZoneSendReqNewZone` | 1719-1723 | `ZoneProcessWeather` |
| `ZoneSendSendAATable` | 1735-1739 | `ZoneProcessNewZone` (unconditional first time) |
| `ZoneSendUpdateAA` | 1752-1756 | `ZoneProcessNewZone` |
| `ZoneSendSendTributes` | 1768-1772 | `ZoneProcessNewZone` |
| `ZoneSendRequestGuildTributes` | 1784-1788 | `ZoneProcessNewZone` |
| `ZoneSendSendExpZonein` | 1816-1820 | `ZoneProcessWorldObjectsSent` |

Deliberately left unguarded (not reachable from any Zone S->C handler, so
`ReplayZonePacket`/`DispatchZonePacket` can never hit them): `ZoneSendSessionReady`
(called only from `ConnectToZone`/connection-open path), `ZoneSendStreamIdentify`
(called only from `ZoneOnNewConnection`), `ZoneSendSpawnAppearance` (declared,
currently uncalled anywhere). `SafeQueueZonePacket` (`eq.cpp:3423`) was
already null-safe before this unit (pre-existing `if (!m_zone_connection)`
guard with a `LOG_WARN`) — left untouched.

Out of scope, documented as a known gap: the ~50 other `m_zone_connection->
QueuePacket` call sites in `eq_entities.cpp`, `eq_player_info.cpp`,
`eq_merchant.cpp`, `eq_group.cpp`, `eq_gm.cpp` are all reachable only from
explicit client-invoked actions (`SendChatMessage`, `RequestLootCorpse`,
group/guild/trade commands, GM commands, etc.), never from replaying a
decoded S->C packet, since golden mode only replays Zone-stream S->C
records. They are not exercised by `ReplayZonePacket` and were left as-is.

### Handler-safety investigation (no code change needed)

Delegated a targeted investigation of `ZoneProcessNewZone`'s four
`Load*` calls (`LoadPathfinder`, `LoadZoneMap`, `LoadZoneLines`,
`LoadZoneSources`) with an empty `m_eq_client_path` (the state a headless
`EverQuest` instance is always in unless `SetEQClientPath` is called, which
the harness never does). Findings: `LoadPathfinder`, `LoadZoneMap`, and
`LoadZoneLines` all guard on the empty path/zone name and no-op cleanly.
`LoadZoneSources` (`eq_zone.cpp:507`) logs `LOG_FATAL` and calls
`RequestQuit()` (which only sets `m_quit_requested = true`, not an abort/
exit) when the path is empty or the zone `.s3d` can't be opened — confirmed
not a crash, and `RequestQuit()`'s flag is never read by the replay harness
(no game-tick loop is run), so this is silently harmless for the test. Left
unmodified since it's already correct/safe behavior for the "no client
files present" case; documented here rather than "fixed" since there was no
bug to fix.

### Synthetic packet sequence and why it satisfies handler preconditions

Built directly in `tests/test_session_replay.cpp` using
`BuildPayload(totalPacketSize, fillLambda)`, which fills a scratch
`EQ::Net::DynamicPacket` at the exact packet offsets documented in each
handler (i.e. `base + structOffset` where the handler adds `base=2` for the
opcode prefix) and then strips the leading 2 bytes to produce the payload
`ReplayZonePacket` expects. No `CheckZoneRequestPhaseComplete` gating was
needed for the assertions made — that gate only affects when
`ZoneSendReqClientSpawn` fires (a *send*, already guarded/suppressed above),
not whether `ZoneProcessZoneSpawns`/`ZoneProcessNewSpawn`/etc. accept and
apply their packet data, so the minimal sequence below just replays each
opcode once, in a plausible order, and does not attempt a full zone-in:

1. **`HC_OP_NewZone`** (700-byte payload): sets `zone_short_name` to
   `"qeytoqrg"` at packet offset 66 and `zone_id=100` at offset 686 (offsets
   from the handler's own comments). Verifies `GetCurrentZoneName()` and
   that all four `ZoneSendSendAATable`/`UpdateAA`/`SendTributes`/
   `RequestGuildTributes` sends were suppressed (`GetReplaySuppressedSendCount()
   == 4`) rather than null-dereferencing `m_zone_connection`.
2. **`HC_OP_PlayerProfile`** (19588-byte payload, the full `PlayerProfile_
   Struct` size guard the handler checks): sets race/class/level, HP/mana/
   stats, currency, position (server x/y swapped per protocol), name
   `"Testchar"` (matches the `EverQuest` ctor's character name so later
   `ZoneSpawns`/`NewSpawn` "is this us" name-matching logic isn't exercised
   by accident), and `entity_id=1001` (becomes `m_my_spawn_id`). No
   preconditions beyond packet-length ones — the handler is a pure parser,
   gated only by its own `p.Length()` checks, which the fixed struct size
   satisfies exactly.
3. **`HC_OP_ZoneSpawns`** (one 385-byte `Spawn_Struct` entry): NPC
   `"a_rat"`, spawn_id 2002, level 5, position packed into the same bitfield
   layout `ZoneProcessZoneSpawns` unpacks (x/y/z as 19-bit fixed-point *8,
   heading as 11-bit *4 scale). No gating flags — spawns are appended to
   `m_entities` unconditionally once `spawn_id`/`name` pass the handler's
   own sanity check (`0 < spawn_id < 100000 && !name.empty()`).
4. **`HC_OP_NewSpawn`** (single 387-byte packet, exercising the alternate
   single-spawn path): NPC `"a_snake"`, spawn_id 3003, level 8. Same
   preconditions as ZoneSpawns (none beyond the struct-size check).
5. **`HC_OP_TimeOfDay`** (6-byte payload): hour/minute/day/month/year at
   the handler's literal offsets 2-6. No preconditions.
6. **`HC_OP_HPUpdate`** (10-byte payload) targeting `spawn_id=1001` (our
   own spawn id set by PlayerProfile above), so the handler's `is_self`
   branch updates `m_game_state.player()`'s HP (which `GetCurrentHP()`/
   `GetMaxHP()` read from). No preconditions.
7. **`HC_OP_DeleteSpawn`** (2-byte payload) targeting `spawn_id=3003` (the
   snake), verifying it's erased from `GetEntities()` while the rat and our
   own spawn remain. No preconditions (the corpse-looting special case only
   applies when `Entity::is_corpse` is set, which it isn't here).

### Every assertion

All in `SessionReplaySynthetic.DrivesGameStateThroughPublicAccessors`:
`GetReplaySuppressedSendCount()==0` (pre-replay sanity), `GetCurrentZoneName()
== "qeytoqrg"`, `GetReplaySuppressedSendCount()==4` (post-NewZone), `GetLevel()
==25`, `GetMaxHP()==250`, `GetCurrentHP()==250`, `GetPosition().x==800`,
`GetPosition().y==1600`, entity 1001 present with name `"Testchar"` and
`level==25`, entity 2002 (`"a_rat"`) present with `level==5` and
`x==100,y==200,z==10`, entity 3003 (`"a_snake"`) present with `level==8`,
`GetTimeOfDay` returns `hour==14,minute==30`, post-HPUpdate `GetCurrentHP()
==123` and `GetMaxHP()==250`, post-DeleteSpawn entity 3003 absent while 2002
and 1001 remain present.

### Golden mode

`SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline` reads
`WILLEQ_SESSION_LOG` or defaults to `data/captures/session.sleq` (gitignored,
added alongside the pre-existing `data/eq_client` entry in `.gitignore`).
Parses the `session_log.h` file header (magic/version check), replays every
`SessionStream::Zone` + `SessionDirection::ServerToClient` record through
`ReplayZonePacket`, and produces a sorted deterministic text summary (zone,
player stats, sorted entity table) compared against a sibling `.golden`
file. If the golden file doesn't exist it's written and the test
`GTEST_SKIP()`s with a "re-run to verify" message, per the plan. On this
host there is no capture, so this test `GTEST_SKIP()`s cleanly (verified —
see Verification below) without ever touching `ReplayZonePacket`, which is
exactly the "must not crash on the always-passing path" requirement in
practice — the *synthetic* test is the one that actually exercises the
guards.

### Deviations from the plan

- The plan's example snippet used `EverQuest("127.0.0.1", 0, "test", "test",
  "test", "Testchar")`. Kept exactly as written.
- No struct definition exists for `PlayerProfile_Struct`/`Spawn_Struct`
  layouts as named C++ types in this codebase (the handlers parse raw byte
  offsets directly, not a struct cast) — the plan says "using packet_structs.h
  layouts", but there is no `PlayerProfile_Struct` there; `Spawn_Struct` and
  `NewZone_Struct` do exist in `packet_structs.h` but the handlers don't
  actually cast through them (they use hardcoded numeric offsets matching
  the struct layout in comments). Built payloads by matching those documented
  numeric offsets directly instead of instantiating the named structs, since
  that's what the handlers actually read.
- Golden-file naming: plan says `data/captures/session.golden`; implemented
  generically as "same path as the session log with its extension replaced
  by `.golden`" so `WILLEQ_SESSION_LOG` overrides stay self-consistent
  (e.g. `/tmp/foo.sleq` -> `/tmp/foo.golden`). With the default path this is
  exactly `data/captures/session.golden` as specified.
- File-size check: `eq.cpp` (3,617 lines on `fable` before this unit, now
  3,657) and `eq.h` (1,854 lines before, now 1,864) both already exceed the
  `check_file_sizes.sh` FAIL/WARN thresholds *before* this unit's changes
  (eq.cpp is the post-Batch-F "Core" TU per `docs/module_map.md`, already
  over the 1,500-line split target at 3,629 lines as of Batch F completion).
  This unit added 40 lines net to eq.cpp and 19 to eq.h — additive and small,
  per instructions — but did not fix the pre-existing over-limit condition,
  which is out of this unit's scope. `scripts/refactor/check_file_sizes.sh
  --files tests/test_session_replay.cpp` (the only genuinely new file) is
  clean at 339 lines.
- `AudioManagerTest.PreloadCommonSounds` fails on this host in isolation
  (confirmed by rerunning it standalone) due to no real ALSA audio device in
  this container (`snd_pcm_open_noupdate` errors), unrelated to this unit.
  This is the sole failing test in the full `ctest` run (1193/1194 passing,
  plus the expected skips including this unit's golden-mode test).
