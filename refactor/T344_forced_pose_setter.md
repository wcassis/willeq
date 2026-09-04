# T344 — A server-forced pose cannot be applied without echoing it back (B001)

Class A. Slot3. `MovementComponent::SetPositionState` (K07 moved it to
`src/client/movement/movement_state.cpp`): every arm ends in
`SendSpawnAppearance`, so when the SERVER forces a pose (stun, root/mez
knockdown, GM `#kill`/`#summon`, feign, death) the client re-sends the
pose it was just told about. Its `AT_DIE` arm encodes a pose self never
receives (T311 Leg A measured: `AT_DIE` appears zero times in either
direction; the official client's death sequence is `OP_SpawnAppearance`
type 14 param 115 (Lying) then no more `OP_ClientUpdate` then
`OP_ZoneChange` — a full zone-in).

## Fix

1. Split the state write from the wire send: `ApplyPositionState(state)`
   (internal, no send — writes `game_state.player().positionState()`
   and publishes whatever the renderer needs, exactly what each arm does
   today minus the `SendSpawnAppearance`) and `SetPositionState(state)`
   = `ApplyPositionState` + the send (the user-initiated path: `/sit`,
   `/stand`, feign key). Bodies verbatim minus the send line — the
   multiset proves it.
2. Server-forced callers use `ApplyPositionState`: find them — the
   `OP_SpawnAppearance` handler for SELF (`ZoneProcessSpawnAppearance`
   in `entities/` when `spawn_id == MySpawnId()`), the stun handler
   (`ZoneProcessStun` in `commands/combat_actions.cpp` — does it force a
   pose?), the death path for self (`OP_Death`/`OP_BecomeCorpse` — read
   `player/player_state.cpp`'s death handling; T311's finding: self death
   is Lying via appearance then a zone-in, NOT `AT_DIE`). Delete the
   `AT_DIE` arm's send (it can never be right); keep the state write if
   any path reaches it, else delete the arm and record.
3. Tests: (a) a forced pose through the self-appearance handler leaves
   the outbound queue EMPTY (recording bridge / `PacketSender` seam) and
   the state set; (b) `/sit` still sends exactly one appearance; (c) the
   T311 death capture replayed (`review/T311/` has the packets; the
   session-replay oracle or `zonetest::ReplayZone`) produces no outbound
   appearance from self. Pre-fix failing runs pasted. The T234/T237
   transform suites and `test_zone_out_handshake` pass unchanged.

Territory: `src/client/movement/movement_state.cpp`,
`include/client/movement/movement_component.h` (T343 may be editing the
Start*/Stop* lines of the same header — touch only the SetPositionState
lines), the self-appearance and death handlers named (call-site edits),
tests, `refactor/BOARD.md`. No graphics files: gate + ARM skipped with
evidence. ⚠ `movement_state.cpp` is NOT golden-pinned (G06 pinned
`renderer_player_physics.cpp`); confirm with `grep -l movement_state tests/goldens`.

STOP: a forced-pose caller needs a state-module change beyond a publish;
the death path needs the zone-in re-implemented (that is a separate,
larger unit — report the shape).
