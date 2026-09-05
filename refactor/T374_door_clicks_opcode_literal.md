# T374 — pending door clicks cleared at zone change (B136); opcode literal (B137)

Class A + B, slot3, no server. Read the B136 and B137 board text (T372
wrote them so they can be worked without the surveyed branch).

## Leg A — B136

`EntitiesComponent::m_pending_door_clicks` is never cleared at zone
change; door IDs are `uint8_t` reissued per zone, so a click sent just
before zoning flags the next zone's first matching `OP_MoveDoor` as
user-initiated (`DoorStateChanged.userInitiated` wrong). Clear the set on
the zone-cleanup path of `EntitiesComponent` (find the existing per-zone
reset — the same place entity maps are cleared; do not add a second
cleanup site). Test, pre-fix failing: click door 3, zone, receive
`OP_MoveDoor` for door 3 in the new zone → `userInitiated == false`.
Strike B136.

## Leg B — B137

`HC_OP_TargetCommandReply` is a bare `0x61f9` literal in
`kZoneOpcodeTable` while every sibling uses an enumerator. Add the
enumerator in `include/client/eq_opcodes.h`, use it in the table and in
`eq_strings.cpp`; the G01 zone-opcode golden pins the table — it must
stay byte-identical (the value is unchanged), and `test_zone_dispatch_table`
must pass unedited. A source-reader pin that no bare hex literal remains
in the table. Strike B137.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, G01 golden
unchanged, file sizes. No graphics change.

STOP: none expected.
