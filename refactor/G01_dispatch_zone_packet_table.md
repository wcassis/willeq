# G01 — `DispatchZonePacket`: the 145-arm switch becomes an opcode table (Batch G pilot)

Batch: `refactor/G_oversize_methods.md`. **Runs ALONE.** Measured at
`1c9afba7`: `src/client/zone/zone_dispatch.cpp` (958 lines),
`ZoneComponent::DispatchZonePacket` from line ~78 to the end; **145 `case`
arms**; **101 are a single call + `break`** (`ctx_.Family().ZoneProcessX(p);`
or `ZoneProcessX(p);`); **68 arm-lines carry local logic** (`if`/`for`/
`auto` — nested handlers, inline `OP_MemorizeSpell` scribe logic K05
noted, opcode-name logging, deliberate no-op arms with comments).

## Shape

1. **Every arm with local logic becomes a named private handler first**
   (`void ZoneComponent::OnMemorizeSpell(const EQ::Net::Packet&)` etc.),
   body verbatim (the K-batch multiset proves it), so that afterwards
   EVERY arm is a single call. This is the bulk of the unit and is
   move-only in the K sense. Deliberate no-op arms (a comment + `break`)
   become entries in a `kIgnoredOpcodes` set with the comment carried as a
   one-line reason string; the logging of unknown opcodes stays as the
   table miss path.
2. **The table:** `static const std::unordered_map<uint16_t, Handler>` where
   `Handler` is `void (ZoneComponent::*)(const EQ::Net::Packet&)` for
   in-family handlers and a small lambda-free adapter for sibling
   components (`[](ZoneComponent& z, const Packet& p){ z.ctx_.Guild().ZoneProcessGuildInvite(p); }`
   — or, simpler and preferred, a private one-line forwarder per sibling
   handler so the table holds only member pointers; count them). The
   body of `DispatchZonePacket` becomes: look up, call, else the miss
   path. Target: **under 40 lines.**
3. **Proof:** (a) the call-graph multiset — every handler body equals its
   former arm's lines; (b) **an opcode-coverage test**: for every `case`
   label that existed in the old switch (extract the list from `git show
   fable:src/client/zone/zone_dispatch.cpp` in the test's fixture or as a
   checked-in golden list `tests/goldens/zone_opcodes_g01.txt`), assert
   the table has an entry or the opcode is in `kIgnoredOpcodes`; and the
   reverse — no table entry without a former arm. This is the regression
   net for "an opcode silently stopped being dispatched".
4. Text readers: six tests name `DispatchZonePacket` (the ones that drive
   packets through it: `test_zone_component`, `test_entities_component`,
   `test_chat_component`, `test_guild_component`, `test_combat_manager`,
   `test_eq_commands_equip`) — they must pass unchanged (assertions
   untouched); `zonetest::ReplayZone`/`MakeEq` helpers in
   `zone_stream_test_utils.h` are the harness — read them first.
   `test_reachability_sweep.cpp` may count `case OP_` labels or
   `ZoneProcess*` arms as consumers — read it; if it does, it learns the
   table (correction 21 shape: widen, re-prove with a planted missing
   entry).

## Predicted

`zone_dispatch.cpp` stays about the same length (bodies move within the
TU); `DispatchZonePacket` 880 → < 40; ~68 new private handlers declared
in `include/client/zone/zone_component.h` (+~70 lines there — that
header is under the ceiling; report its count). `eq.h` untouched.

## STOP

- An arm's local logic needs state that no handler signature can carry
  → STOP with the arm named.
- Any of the six packet-driving tests would need an assertion change.
- The coverage test finds an opcode that was in the switch and is in
  neither the table nor the ignored set (that is a bug in the unit).

## Territory

`src/client/zone/zone_dispatch.cpp`, `include/client/zone/zone_component.h`,
`tests/test_zone_dispatch_table.cpp` (new) + cmake + its golden list,
`tests/test_reachability_sweep.cpp` (widening only, if forced),
`docs/module_map.md`. Nothing under `src/client/graphics`: visual gate +
ARM skipped with evidence.
