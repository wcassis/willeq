# T349 — `OP_Save`: the 192-byte body and its place in the self-death burst (B059)

Class A. Slot1. `PlayerComponent::SendSave` queues a bare 2-byte `OP_Save`;
the official client's is 192 bytes and it is the FIRST C→S packet of every
self-death burst (T311 capture, four deaths agree), before the Lying
appearance. fable sends none from the death path.

## Measure

Decode the 192-byte body from `review/T311/legA/decode/` (the pcap or the
per-packet dumps): diff the four instances against each other (what
varies per death, what is constant) and against the character's state
at each moment (position? the `Save_Struct` in `include/common/packets/`
if one exists; eqemu's `zone/client_packet.cpp` `Handle_OP_Save` in
`/mnt/projects/akk-stack/code/` says what the server reads — cite it as
an absolute path). Establish which fields the server actually consumes;
the rest are filled to match the oracle's bytes.

## Fix

1. `SendSave()` builds the 192-byte body (a packed struct in
   `include/common/packets/`, `static_assert`ed to 192, per the J14
   convention).
2. `ZoneProcessDeath`'s self branch sends it first, before the Lying
   appearance (T346's step 1). The self-death golden
   `tests/goldens/self_death_outbound_g07.txt` learns the new first line
   IN PLACE (T337 shape), both faults re-planted.
3. Test: the struct's layout against the captured bytes (a golden of one
   captured `OP_Save` body with the per-death-varying fields masked);
   `SendSave` after a replayed death produces a 192-byte packet matching
   it. Pre-fix failure pasted.

Territory: `src/client/player/player_misc.cpp`, `src/client/player/player_state.cpp`
(`ZoneProcessDeath` self branch — T346's territory, merged), the new
packet struct header, the goldens (in place), tests, `refactor/BOARD.md`.
No graphics files.

STOP: the body's semantics cannot be established from the capture and
the server source (then report what is known and leave it).
