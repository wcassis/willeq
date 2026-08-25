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
