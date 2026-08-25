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
