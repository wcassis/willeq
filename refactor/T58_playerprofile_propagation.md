# T58 — `PlayerProfile` sets the landing position without ever telling the state layer

**Slot: t2. x86 only. No Pi. NO EQEMU** — driven headlessly through the
`ReplayZonePacket` seam, exactly as T55 did.

**Territory: `ZoneProcessPlayerProfile` in `src/client/eq.cpp` — that
function's position/heading block ONLY — plus
`tests/test_zone_position_convention.cpp` and, if needed, a narrow
accessor in `include/client/eq.h`.**

**Everything else in `eq.cpp` is READ-ONLY, including the stats block
below the position code.** Do not touch `ZoneProcessZonePlayerToBind`,
`SetPosition`, `eq_login_world.cpp`, or `eq_zone.cpp` — T55 just fixed
those and they are correct. Do not touch `eq_movement.cpp`,
`eq_entities.cpp`, `eq_bridge.cpp`, `eq_commands_movement.cpp`.

**STATUS: PARKED, NOT DISPATCHED (2026-08-11).** Written during the
T57–T59 round-planning, then held when the session turned to the
ConstrainedMeshCache/paperdoll design questions. The finding it addresses
is real and verified (see below) — this plan is ready to dispatch
whenever a slot frees up. Re-check the sibling-unit list against what is
actually running before dispatching; at parking time nothing else owned
`src/client/eq.cpp`. `refactor/deferred.md` is append-only and shared by
every unit.

Read `refactor/EXECUTOR_PROTOCOL.md`, then **`refactor/T55_position_state_propagation.md`
in full including its Review** — this unit is its direct continuation and
inherits its seam, its vocabulary, and its scope discipline.

## Why this unit exists

T55 closed the forced-relocation propagation gap: `SetPosition`
(`eq.cpp:3109-3116`) writes both the raw `m_x/m_y/m_z` members and the
state layer's `PlayerState`, and the death/bind and zone-in paths now
route through it.

**T55 deliberately left one site alone and put it on the board:
`ZoneProcessPlayerProfile`.** It is the packet that actually determines
where you land when you zone in, and it still does the old thing:

- `eq.cpp:2154-2163` writes `m_x`/`m_y`/`m_z` and `m_heading` **raw** —
  no `SetPosition`, no `PlayerState` write. So the state layer's player
  position is still never written on the single most important
  position-setting path in the client.
- `eq.cpp:2176-2188` then pushes a `PlayerMoved` event **directly onto
  the `GameStateBridge`** — the renderer-facing queue — rather than
  through the state layer's EventBus.

T55 left it because it sits on the login path, where an unexpected event
could have ordering consequences, and because it was outside T55's named
territory. That reasoning was right for T55. This unit is where it gets
resolved properly.

## The trap — read this before writing any code

`SetPosition` writes `PlayerState::setPosition`, which **fires
`PlayerMoved` on the EventBus**. The existing code **already publishes a
`PlayerMoved`, by hand, to the bridge.**

**So the naive fix — swap the raw writes for `SetPosition` and leave the
hand-rolled publish alone — double-publishes the event.** Depending on
how the EventBus forwards to the bridge, the renderer may receive
`PlayerMoved` twice for one zone-in, with the second carrying
`heading` in a different unit (the hand-rolled one converts to the 512
format at `:2177`; check what the `PlayerState` path emits).

**Step 1 is to establish, with evidence, what actually reaches the bridge
today and what would reach it after the change.** Trace whether an
EventBus `PlayerMoved` is forwarded to the `GameStateBridge` at all, and
in what units. Write the answer down as a short table in the Review
before you change anything. If the two paths turn out to be genuinely
independent (EventBus not forwarded to the bridge), then both publishes
are needed and the fix is only the missing state write — say so.

Do not guess this. Getting it wrong means either the renderer stops
receiving the zone-in position (character renders at the wrong place on
zone-in) or it receives a duplicate with inconsistent heading units.

## Step 2 — prove the gap, then fix it

Tests first, and they must **fail against the current tree for the right
reason** — quote that failure in the Review, as T55 did.

Use T55's seam: `ReplayZonePacket` (`include/client/eq.h:711`),
`Test_SetZoneConnectedForSeam`, and **`Test_GetGameStatePlayerPosition()`**
(added by T55, `eq.h`) which reads `m_game_state.player()`'s position
directly. `tests/test_zone_position_convention.cpp` is the right home —
this is the same subject and T55's tests are the idiom.

Cover:
- Replaying a `PlayerProfile` populates the **state layer's** player
  position, not just the raw members.
- The x/y swap convention is preserved exactly (`m_x = server_y`,
  `m_y = server_x` at `:2154-2156`) — this unit must not become a
  convention change. That swap is correct; T52 settled it.
- The heading conversion at `:2158-2162` is preserved bit-for-bit. It has
  its own 12-bit-scaled-value math and is easy to break by accident.
- Exactly one `PlayerMoved` reaches the bridge per zone-in, with the
  heading in the format the renderer expects — whatever Step 1
  established that to be.

Then make the change, minimally, with a comment naming the convention and
the state write, per T52/T55 precedent.

## STOP conditions

- Step 1's tracing is inconclusive about EventBus→bridge forwarding.
  Report what you found; do not change publish behaviour on a guess.
- The fix appears to need any file outside this unit's territory.
- Any existing test failing in a way that suggests a login-path ordering
  dependency — report the ordering, do not suppress it.
- Any urge to touch the stats block below the position code, or to
  "finish the migration" its comment mentions. Out of scope.

## Verification

Full protocol bar. Additionally: quote the pre-fix failing test output;
confirm T55's four `ZonePositionConvention` tests and the T52 pins stay
green; state the event-count result (exactly one `PlayerMoved` per
zone-in) explicitly.

## Review

(executor fills: the Step 1 trace table — what publishes what, to which
queue, in which units, before and after; the pre/post test output; the
final shape of the fix; deviations)

Board (`refactor/deferred.md`, append, never delete): re-title the
`PlayerProfile` propagation bullet T55 added. If any part of the position
path remains unconverged after this unit, say exactly which and why —
the per-frame movement writes are still deliberately out of scope and
should stay recorded as such.
