# T346 — The self-death sequence matches the official client (the remainder T344 recorded)

Class A. Slot3. T311 Leg A measured the official Titanium client's death:
(1) C→S `OP_SpawnAppearance` type 14 (`AT_ANIMATION`) param 115 (Lying)
answering our own `OP_Death`; (2) NO further `OP_ClientUpdate` while
dead; (3) C→S `OP_ZoneChange` to the bind point followed by a FULL
zone-in handshake, even into the zone we are already in. T344 wired
`ApplyPositionState(POS_DEAD)` from `ZoneProcessDeath` for self and
proved no `AT_DIE` is sent; it left the three steps above owed and
recorded that `ApplyPositionState(POS_DEAD)`'s `StopMovement()` fires one
last `SendPositionUpdate()`.

## Measure first

Replay T311's death capture (`review/T311/legA/decode/` — the opcode
timeline TSV and the pcap) through `zonetest::ReplayZone` and diff
fable's outbound sequence after `OP_Death` against the oracle's: which of
(1)/(2)/(3) fable already does, and what it sends that the oracle does
not. Paste the two sequences.

## Fix, one step per commit

1. **Lying appearance on death:** on self `OP_Death`, send exactly one
   `AT_ANIMATION` 115 (the same `SendSpawnAppearance` the user path uses)
   — through `SetPositionState`'s send half, or a dedicated call in the
   death handler; record which.
2. **No client updates while dead:** a `POS_DEAD` gate in the movement
   update pump (`sendPositionUpdateToServer` phase — ⚠ G06's golden pins
   `renderer_player_physics.cpp`; the gate probably belongs on the GAME
   side in `MovementComponent`'s send path, which is not golden-pinned —
   measure where the last update originates and gate there), including
   the `StopMovement()` one T344 found. Test: after self death, zero
   `OP_ClientUpdate` on the outbound seam for N ticks.
3. **Bind re-entry:** on self death, after (1), send `OP_ZoneChange` to
   the bind point (the fields the oracle's packet carries — copy them
   from the capture) and drive the existing zone-transition path
   (`ZoneComponent`'s `RequestZoneChange`/`ProcessDeferredZoneChange` —
   K09/K10 kept them) so the full handshake runs even for the same zone.
   Test: the replayed capture produces the same outbound opcode sequence
   as the oracle's (a golden extracted from the T311 TSV; size asserted;
   fault planted by dropping step 1's send).

Live proof: owed unless the lock is free — die once (`/say #kill` on
self or a GM-summoned mob), watch the client respawn at bind with a
full zone-in; `review/T346/`.

STOP: step 3 needs the zone-transition path changed beyond a new caller;
the oracle capture shows a step fable's architecture cannot reproduce
without a design change (report the sequence).

Territory: `src/client/player/player_state.cpp` (death handler),
`src/client/movement/` (the send gate), `src/client/zone/` (only a call
into the existing transition), the test + golden, `refactor/BOARD.md`.
No graphics files unless the gate lands in the physics TU (then the
golden learns it in place and gate + ARM run).
