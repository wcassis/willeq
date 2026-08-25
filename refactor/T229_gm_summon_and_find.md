# T229 — SendGMSummon sends zeroed coords; ZoneProcessGMFind decodes floats as ints

Slot: slot2. x86. OWNS eqemu for the live leg (noclobber; T227 in slot1 will
need it later — do code first, take it briefly, release promptly). Class A
(T148 board entry, `## T148 (2026-08-19)` in refactor/deferred.md). Territory:
src/client/eq_gm.cpp, include/client/packet_structs.h (the two GM structs
only), tests. Do NOT touch application.cpp, bridge/*, eq_merchant.cpp
(T227) or input_action_bridge / eq.cpp NewZone (T228).

## Defect 1 — SendGMSummon (eq_gm.cpp ~30-44)
Builds a GMSummon_Struct, memsets it, fills nothing but the name → x/y/z = 0.
Works live only because eqemu's Handle_OP_GMSummon ignores the coords and
summons to the GM's own position. Check /mnt/projects/akk-stack/code/zone/
client_packet.cpp Handle_OP_GMSummon and common/eq_packet_structs.h
GMSummon_Struct: if the server never reads the coords, fill them with the
GM's current position anyway (what the official client sends) and document;
if it does read them in some branch, fill them correctly. Test pins the body.

## Defect 2 — ZoneProcessGMFind (eq_gm.cpp ~200)
Decodes the reply's coordinates via GMSummon_Struct's int32_t y/x/z; live
printed -998490112 (float bit pattern). Check eqemu's sender
(Handle_OP_GMFind / the struct it fills): if the fields are float, fix the
struct (or the local decode) and print sane coords; add a test with the
captured bytes from review/T148/ if present, else a constructed packet.
Live: `/gmfind Oraclerogue` (or the second char) prints a plausible
location matching the DB row. Red/green measured.

## Verification
Full ctest; Review in this file; evidence review/T229/. Commit:
`T229: GM summon carries the GM's position; GM find decodes floats`.

## Review

Evidence: /mnt/projects/fable/review/T229/ (README.md first).

### Server truth (read from /mnt/projects/akk-stack/code)

- **Summon coords are READ.** `Client::OPGMSummon` (zone/client_process.cpp:1877)
  calls `MovePC(zone, inst, gms->x, gms->y, gms->z, GetHeading(), true)` for an
  in-zone player target and copies x/y/z into `ServerZonePlayer_Struct` for a
  cross-zone one. Only a non-client mob and a corpse ignore them. The plan's
  "evidently ignored" hypothesis is refuted: T148's zeros sent Testbuddy to
  (0,0,0). `GMSummon_Struct` is `float y; float x; float z;` (y first).
- **GMFind reply is `GMFind_Struct`**: same 152 bytes, `float x; float y;
  float z;` (x FIRST). `Handle_OP_GMFind` (zone/client_packet.cpp:6640) fills
  success/zoneID/x/y/z for an in-zone target, success=0 + zeros otherwise.

### Changes

- `include/common/packet_structs.h`: `GMSummon_Struct` y/x/z int32 -> float
  (152 bytes unchanged); new `GMFind_Struct` (x-first) with static_assert.
- `src/client/eq_gm.cpp`: `SendGMSummon` fills y/x/z with the GM's position
  in server coordinates (wire y = our m_x, wire x = our m_y -- the T52
  convention); `ZoneProcessGMFind` decodes `GMFind_Struct`, prints
  `{name} is in zone {z} at {sy}, {sx}, {z}` in /loc's convention, and says
  `{name} is not in this zone.` on success=0 (was "in zone 0 at (0, 0, 0)").
- `include/client/eq.h`: `Test_GetLastGMSummon()` seam (T217 shape).
- `tests/test_gm_summon_find.cpp` (new, registered): struct layout/type pins,
  summon body (incl. IEEE word 0xC47C4000 for -1009.0f), find decode from the
  T148 captured words, not-found, short packet.

### Red/green (measured)

- Red, pre-T229 code + new test: compile fails on 3 static_asserts
  (`unit_test_red_compile.txt`); with those stripped, 4/8 FAIL -- the summon
  body words 136..147 all zero, find prints
  `(-998375424, 1125908480, 1112276992)` (`unit_test_red_runtime.txt`).
- Green: 8/8 (`unit_test_green.txt`).
- Note: T148's `-998490112` is 0xC47C4000 = **-1009.0f** (server y), matching
  Testbuddy's server-log position (156, -1009, 51); the test uses these words.

### Verification bar

- `cmake --build build -j24`: clean.
- `ctest`: **2866/2867**, sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds` (`ctest_full.txt`).
- `check_file_sizes.sh --files <my 5 files>`: 0 FAIL (eq.h /
  packet_structs.h / tests/CMakeLists.txt WARNs pre-existing). The plain
  `check_file_sizes.sh fable` run reports eq.cpp FAIL because fable has moved
  past this branch's base (T228 merged); eq.cpp is untouched here.
- Not run (no visual surface, no Pi ownership): build-gles2-x86.sh, visual
  gate, build-arm-noble.sh.

### Live (eqemu lock T229, arena, Testchar GM + Oraclerogue)

- `/gmfind Oraclerogue`: `Oraclerogue is in zone 77 at -1009.0, 176.0, 51.0`
  = DB row (x=176, y=-1009, z=51) = B's own `/loc`. **PASS.**
- `/summon Oraclerogue`: server echoed `Local: Summoning Oraclerogue to
  146.000000, -1009.000000, 51.000000` (Testchar's position) and logged
  `ZonePC ... x [146] y [-1009] z [51] zone_mode [4]`. The wire body is
  correct. **PASS for this unit's defect.**

### Deviation / new finding (NOT fixed, out of territory)

The summoned WillEQ client does not end up moved. For a same-zone summon eqemu
sends `OP_RequestClientZoneChange` for the CURRENT zone and expects no
`OP_ZoneChange` back (zoning.cpp ~1060: "Client wont generate a zone change
packet to the server in this case"); it has already reset `zone_mode` to
`ZoneUnsolicited`. Our `ZoneProcessRequestClientZoneChange`
(src/client/eq_zone.cpp:39) replies unconditionally, the server answers with
`SendZoneCancel` at the client's last-reported position, and the client tears
down, reconnects to world, re-zones into arena (`Client linkdead`, spawn id
32 -> 34) and lands where it started (176). T148's run shows the identical
`ZonePC` -> `linkdead` pair, so this predates T229. Follow-up unit: handle a
same-zone `OP_RequestClientZoneChange` by relocating locally (SetPosition +
position update) without a zone cycle. eq_zone.cpp is outside this plan's
territory, so it is filed, not touched.

Server restored to pre-session rows; eqemu down; lock deleted.
