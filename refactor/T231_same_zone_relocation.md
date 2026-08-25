# T231 — same-zone OP_RequestClientZoneChange relocates locally instead of re-zoning

Slot: slot2. x86. OWNS eqemu briefly for the two-client live leg (noclobber;
T227/T230 may hold it — code first, report pending if held). Class A (T229
finding, review/T229/). Territory: src/client/eq_zone.cpp
(`ZoneProcessRequestClientZoneChange`), the position-set path it needs
(eq.cpp SetPosition / the T55/T58 state write — additive call only), tests.
Do NOT touch application.cpp, bridge/*, eq_merchant.cpp (T227), combat.cpp,
action/* (T230), eq_gm.cpp.

## Defect
For a same-zone summon (and any same-zone teleport: `#goto`, `/summon`,
zone-line bounce), eqemu sends `OP_RequestClientZoneChange` naming the
CURRENT zone with the destination x/y/z, and explicitly expects NO
`OP_ZoneChange` reply (akk-stack `zone/zoning.cpp` ~1060: "Client wont
generate a zone change packet to the server in this case"; it has already
reset zone_mode to ZoneUnsolicited). `ZoneProcessRequestClientZoneChange`
(`eq_zone.cpp:39`) replies unconditionally → server `SendZoneCancel` at the
last-reported position → full teardown/reconnect → client lands where it was.

## Scope
1. In `ZoneProcessRequestClientZoneChange`: if `zone_id` (and instance, if
   the struct carries it) equals the current zone, do NOT send
   `OP_ZoneChange`; instead apply the destination position/heading locally
   through the same path a real zone-in uses (T55/T58's `SetPosition`/
   `SetHeading` — the ones that write `m_game_state.player()` and publish
   to the renderer), then send the position update the official client
   sends after a teleport (check what T88/T148 evidence shows: an
   `OP_ClientUpdate` with the new coords). Otherwise keep the existing
   cross-zone behaviour byte-for-byte.
2. Test in tests/test_zone_connection_flow.cpp (or the closest harness):
   same-zone request → no OP_ZoneChange sent, position == destination,
   heading applied; cross-zone request → OP_ZoneChange sent as before.
   Red/green measured.
3. Live, two clients: Testchar (GM) `/summon Oraclerogue`; Oraclerogue's
   log shows NO reconnect (spawn id unchanged, no "linkdead" in the server
   log) and its `/loc` == Testchar's position; Testchar (observer) sees
   Oraclerogue's entity move to it (`/dumpscene` or entity-moved log line).
   Then `#goto`-style: Testchar `/gmzone`? — no, use `#goto <x> <y> <z>` via
   `/say` if GM commands work, same assertions on self.

## Verification
Full ctest; Review in this file; evidence review/T231/. Commit:
`T231: same-zone zone-change requests relocate locally -- no reconnect`.

## Review

Evidence: /mnt/projects/fable/review/T231/ (README.md first).

### Server behaviour matched (read from /mnt/projects/akk-stack/code/zone/zoning.cpp)

- `Client::ZonePC` builds `OP_RequestClientZoneChange` for every teleport
  mode: ZoneSolicited/ZoneToSafeCoords at 973-996, EvacToSafeCoords at
  999-1030 (deliberately FAKES a foreign zone id so the client does zone),
  and the default branch (GMSummon/GMHiddenSummon/SummonPC/Rewind/
  GateToBindPoint) at 1031-1054. In that default branch, when
  `zoneID == GetZoneID()`, the server ALREADY moves its own entity
  (`entity_list.ProcessMove` + `SentPositionPacket`, 1033-1037) -- so
  observers see a GM-summoned player move before the client says anything.
- 1055-1068: "Clear zonesummon variables if we're zoning to our own zone.
  Client wont generate a zone change packet to the server in this case" --
  for `zoneID == zone->GetZoneID() && instance_id == zone->GetInstanceID()`
  (and zm not Evac/SafeCoords/Bind) it zeroes `m_ZoneSummonLocation`,
  `zonesummon_id`, and sets `zone_mode = ZoneUnsolicited`.
- Consequence of replying anyway (pre-T231): `Handle_OP_ZoneChange` at 99-116
  treats the reply as ZoneUnsolicited, finds no zone point in range,
  "Invalid unsolicited zone request" -> `SendZoneCancel` (113) -> the
  client's teardown/reconnect T229 observed (spawn id 32 -> 34, landed at
  the origin position).
- Cheat exemption: `ZonePC` sets `cheat_manager.SetExemptStatus(Port, true)`
  (858) before any of this, so the client's post-teleport `OP_ClientUpdate`
  jump is not flagged as a warp.
- ZoneSolicited same-zone (`#goto`) does NOT set the server's `m_Position`
  (only `m_ZoneSummonLocation`, 884-887, then cleared) -- the server learns
  the landing spot ONLY from the client's next OP_ClientUpdate. That is why
  the client must send one immediately, and why it is forced past the
  throttle.

### Changes

- `src/client/eq_zone.cpp` `ZoneProcessRequestClientZoneChange`: same-zone
  test mirroring the server's (`m_current_zone_id != 0 && zone_id ==
  m_current_zone_id && instance_id == 0`; this Titanium client always sits
  in instance 0 -- `RequestZoneChange` hard-codes it) -> new
  `RelocateWithinZone(x, y, z, wire_heading)`; otherwise the cross-zone path
  is byte-for-byte unchanged.
- `RelocateWithinZone`: clears any scripted path/target (same fields
  `StopMovement()` clears), `SetPosition`/`SetHeading` (T55/T58 state write),
  heading converted with the PlayerProfile player-own rule (`90 -
  wire*360/512`, wrapped) -- copied, NOT factored into a helper, per the
  load-bearing comment; one hand-rolled `PlayerMoved` on the bridge in 512
  heading format (exactly what PlayerProfile does, so the renderer's camera/
  player moves and its next intent does not drag the position back); then
  `m_force_next_position_update = true; SendPositionUpdate()`.
- `src/client/eq_movement.cpp` `SendPositionUpdate`: one-shot
  `m_force_next_position_update` bypasses the 250 ms throttle and restarts
  the delta baseline at the new position (a teleport is not a velocity).
  **Deviation (flagged):** the plan named eq_zone.cpp + an additive call
  only; this 6-line flag is the minimum that makes the forced update
  actually leave the client (the throttle is a function-local static, and
  a standing player generates no other updates). Production behaviour with
  the flag unset is unchanged.
- `include/client/eq.h`: `RelocateWithinZone` decl, the flag, and the T231
  test seam `Test_GetLastZoneChangeRequest()` (attempt count + zone id,
  captured at the top of `RequestZoneChange` before its connection gate --
  T88/T229 seam shape).
- `tests/test_zone_position_convention.cpp`: 5 new `SameZoneRelocation.*`
  tests (same-zone: no reply, raw+state position, heading 45deg, one
  PlayerMoved at 64.0; forced ClientUpdate carries server-frame (146,-1009,
  51) heading_scaled 256; cross-zone still replies and writes nothing;
  unknown current zone keeps handshake; non-zero instance keeps handshake).

### Red/green (measured)

- Red: same code with only the `if (same_zone)` decision disabled (seams
  kept so it compiles): **2/5 FAIL** -- the two same-zone tests
  (`unit_test_red.txt`); the three handshake-preserving tests pass on both.
- Green: **5/5** (`unit_test_green.txt`). File restored and verified
  (grep for the temp marker = 0).

### Verification bar

- `cmake --build build -j24`: clean.
- `ctest`: **2866/2867** (`ctest_full.txt`); the sole failure is the
  documented ALSA `AudioManagerTest.PreloadCommonSounds`. 5 new tests
  registered (`ctest -N` count).
- `check_file_sizes.sh fable` from the slot: 4 files checked, 0 FAIL
  (eq.h / eq_movement.cpp WARNs pre-existing).
- Not run (no visual surface, no Pi ownership): build-gles2-x86.sh, visual
  gate, build-arm-noble.sh.

### Live (two clients, eqemu lock T231, arena) -- ALL PASS

Evidence: review/T231/live/ (driver, scripts, both client logs, server log,
DB before/staged/after/restored). Staged as T229: Testchar GM at server
(146, -1009, 51), Oraclerogue at (176, -1009, 51). A = Testchar
(`--log-level=DEBUG -d 2`), B = Oraclerogue.

**/summon Oraclerogue (GMSummon, zone_mode 4):**
- Server: `ZonePC Client [Oraclerogue] zone_id [77] ... x [146] y [-1009]
  z [51] ... zone_mode [4]`; **no `linkdead`, no `cancel`** anywhere in the
  zone log (T229's run had both).
- B: `Server requests zone change: zone=77 ... pos=(-1009.0, 146.0, 51.0)`
  -> `Same-zone relocation (zone 77): applying locally, no OP_ZoneChange
  reply` -> `Relocated within zone to client=(-1009.00, 146.00, 51.00)`.
  Spawn id **32 before and after** (one `Server assigned new spawn ID: 32`
  line in the whole log, no `Spawn ID changed`, no world reconnect). `/loc`
  before: `-1009.0, 176.0, 51.0`; after (twice): `-1009.0, 146.0, 51.0`
  == Testchar's `/loc`.
- A (observer): `POS S->C spawn_id=32 pos=(-1009.00, 176.00, 51.00)` x5
  before the summon, then from 21:19:29.062 (33 ms after the summon echo)
  `pos=(-1009.00, 146.00, 51.00)` for the rest of the session -- the
  observing client saw Oraclerogue's entity move to the GM. `/gmfind
  Oraclerogue` afterwards: `in zone 77 at -1009.0, 146.0, 51.0`.

**#goto 200 -1009 51 on self (ZoneSolicited -- the case where the server
does NOT move its own entity and depends on our forced OP_ClientUpdate):**
- Server: `ZonePC Client [Testchar] ... x [200] y [-1009] z [51]`, no
  linkdead/cancel.
- A: same three-line relocation to client=(-1009, 200, 51); `/loc` ->
  `-1009.0, 200.0, 51.0`; `/gmfind Testchar` (the server's own m_Position)
  -> `-1009.0, 200.0, 51.0` -- proof the forced update reached the server.
- DB after camp (`db_after_run1.txt`): Oraclerogue x=146, Testchar x=200
  (staged 176 / 146). The server persisted both teleports.

Restored (`db_restored.txt`) to `db_before.txt`: Testchar zone 36
(-77.9232, 21.9086, 3, h190) gm=0; Oraclerogue zone 2 (167, 30, -29.25,
h384). eqemu down (`eqemu_down.log`); `.eqemu-lock` deleted.

Note: `-d N` and `--log-level=DEBUG` "disagree" per the client's own INFO
line and `--log-level=` wins; `-d 2` still enabled the per-entity POS S->C
lines A's observer evidence relies on.
