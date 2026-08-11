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

**STATUS: DISPATCHED and CLOSED (2026-08-11, slot t2).** Written during
the T57–T59 round-planning, held as PARKED while the session turned to
the ConstrainedMeshCache/paperdoll design questions, then dispatched this
round. See Review below for the trace table, fix, and verification
evidence.

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

### Step 1 — the trace table (established before any code change)

Traced with evidence, not guessed, per the plan's own warning:

| Path | Fires on | Consumed by | Reaches renderer? | Heading units |
|---|---|---|---|---|
| `SetPosition`/`SetHeading` → `PlayerState::setPosition`/`setHeading` → `firePlayerMovedEvent()` | `EverQuest::m_game_state`'s **own, private** `state::EventBus` (a value member, `eq.h:1210`) | Nothing in production. `grep -rn "\.subscribe(\|->subscribe(" src/ include/` (excluding tests) found exactly two production subscribers: `console_renderer.cpp` (subscribes to `ChatMessage`, `EntitySpawned`, `EntityDespawned`, `CombatEvent`, `ZoneChanged`, `TargetChanged`, `PetCreated`, `PetRemoved`, `PetStatsChanged`, `CastingStateChanged` — no `PlayerMoved`) and `automated_mode.cpp` (subscribes to everything) — **both attach to `Application::m_gameState`, a distinct, separately-constructed `state::GameState` instance** (`application.cpp:180`, `std::make_unique<state::GameState>()`), never to `EverQuest::m_game_state`. Confirmed by the existing comment at `application.cpp:1793-1797`: "this is a *different* state::GameState instance than EverQuest::m_game_state... EverQuest owns its own, used internally by a few Phase 7.x getters." | **No** | Raw degrees (`m_heading`, unconverted) |
| `ZoneProcessPlayerProfile`'s hand-rolled `m_bridge->pushEvent(GameEvent(PlayerMoved, data))` (`eq.cpp:2176-2189`, unchanged by this unit) | `EverQuest::m_bridge` (a `GameStateBridge*`, set via `setBridge()`) | `GameStateBridge::drainEvents()` → `applyEvent()`, consumed by `IrrlichtBridge` (`renderer_->setPlayerPosition(...)`) or `GameStateSyncBridge` (which delegates every event it doesn't specially handle — and `PlayerMoved` is not in its narrow switch, `game_state_sync_bridge.cpp:9-...` — to the wrapped `IrrlichtBridge`/`ConsoleBridge` unchanged) | **Yes** — this is the only thing that reaches the renderer | 512-scaled (`heading512 = m_heading * 512.0f / 360.0f`, `eq.cpp:2177`) |

**Verdict: the two paths are genuinely independent** — exactly the plan's
"if independent, both publishes are needed and the fix is only the
missing state write" branch. There is no double-publish risk: routing the
position/heading writes through `SetPosition`/`SetHeading` adds a write to
a bus nobody in production reads; it does not add a second event to the
bus the renderer actually drains. The hand-rolled bridge push was left
completely unchanged.

Confirmed empirically, not just by static trace: the new
`ZonePositionConvention.PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat`
test (uses a `RecordingBridge` that only records, never applies, so it
observes exactly what `EverQuest` pushed) **passed both before and after**
the fix — proof the fix does not change the bridge's event count, only
adds the missing state write.

### Step 2 — pre-fix failing output (quoted)

New tests added to `tests/test_zone_position_convention.cpp`, run against
the tree exactly as inherited from `fable` (before any production change):

```
[ RUN      ] ZonePositionConvention.PlayerProfile_PropagatesToGameStatePlayerPosition
.../test_zone_position_convention.cpp:405: Failure
Expected equality of these values:
  statePos.x
    Which is: 0
  444.0f
    Which is: 444
.../test_zone_position_convention.cpp:406: Failure
  statePos.y: 0 vs 333.0f
.../test_zone_position_convention.cpp:407: Failure
  statePos.z: 0 vs 5.0f
[  FAILED  ] ZonePositionConvention.PlayerProfile_PropagatesToGameStatePlayerPosition (318 ms)
[ RUN      ] ZonePositionConvention.PlayerProfile_HeadingConversionPreservedBitForBit
.../test_zone_position_convention.cpp:433: Failure
Expected equality of these values:
  eq.GetHeading()
    Which is: 0
  kExpectedHeadingDeg
    Which is: 45
[  FAILED  ] ZonePositionConvention.PlayerProfile_HeadingConversionPreservedBitForBit (1 ms)
[ RUN      ] ZonePositionConvention.PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat
[       OK ] ZonePositionConvention.PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat (38 ms)
...
[  PASSED  ] 1 test.
[  FAILED  ] 2 tests, listed below:
[  FAILED  ] ZonePositionConvention.PlayerProfile_PropagatesToGameStatePlayerPosition
[  FAILED  ] ZonePositionConvention.PlayerProfile_HeadingConversionPreservedBitForBit
 2 FAILED TESTS
```

Both failures are the right reason: the state layer stays at the
constructor's `(0,0,0)`/`0.0` defaults while the legacy raw members
(`GetPosition()`) and the bridge push already had the correct values —
exactly the T39-pattern propagation gap the plan describes. Incidentally,
`GetHeading()` already reads `m_game_state.player().heading()` (unrelated
pre-existing code, `eq.cpp`'s `GetHeading()`), so the heading gap was
directly observable through an existing public accessor — no new accessor
needed for that half of the fix.

### Step 2 — the fix (minimal)

`src/client/eq.cpp`, `ZoneProcessPlayerProfile`'s position/heading block
(`:2153-2162`): replaced the four raw member writes (`m_x = server_y;
m_y = server_x; m_z = z; m_heading = ...`) with `SetPosition(server_y,
server_x, z); SetHeading(new_heading);` — same helper T55 wired at the
other forced-relocation sites, which writes both the raw members (so
`GetPosition()`/`GetHeading()` and every other raw-member reader keep
working unchanged) and `m_game_state.player()`. The x/y swap and the
12-bit-scaled heading math are byte-for-byte unchanged (same three lines
computing `server_heading_deg`/wrap, just assigned to a local
`new_heading` instead of `m_heading` directly, since `SetHeading` now
owns that write). The hand-rolled bridge publish (`:2176-2189` in the
original numbering) is completely untouched — same 512-scale conversion,
same event, same call. A comment names the convention, the independence
finding, and points at this unit's Review, per T52/T55 precedent.

No `eq.h` change was needed — unlike T55, which added
`Test_GetGameStatePlayerPosition()` because no position reader existed at
all, this unit's heading assertion reuses the already-existing
`GetHeading()` (which has read `m_game_state.player().heading()` since
before this unit) and the position assertion reuses T55's own
`Test_GetGameStatePlayerPosition()`. Territory stayed narrower than the
plan allowed for.

### Post-fix test output

Full `test_zone_position_convention` suite, 10/10 passed, including all
three new T58 tests and all four T55 tests + T52's own pins, untouched:

```
[==========] Running 10 tests from 1 test suite.
[ RUN      ] ZonePositionConvention.ProducerA_SwapsWireOffsetsIntoClientFormat
[       OK ] ...
[ RUN      ] ZonePositionConvention.ProducerA_DeniedChangeClearsPendingZone
[       OK ] ...
[ RUN      ] ZonePositionConvention.ProducerB_ZoneLinesPassesDestinationThroughUnswapped
[       OK ] ...
[ RUN      ] ZonePositionConvention.Consumer_AppliesPendingZoneWithoutASecondSwap
[       OK ] ...
[ RUN      ] ZonePositionConvention.ProducerC_SwapsWireOffsetsIntoClientFormat
[       OK ] ...
[ RUN      ] ZonePositionConvention.ForcedRelocation_CrossZoneDeathRespawnDoesNotWriteLocalPosition
[       OK ] ...
[ RUN      ] ZonePositionConvention.ForcedRelocation_SameZoneDeathRespawnPropagatesLocally
[       OK ] ...
[ RUN      ] ZonePositionConvention.PlayerProfile_PropagatesToGameStatePlayerPosition
[       OK ] ...
[ RUN      ] ZonePositionConvention.PlayerProfile_HeadingConversionPreservedBitForBit
[       OK ] ...
[ RUN      ] ZonePositionConvention.PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat
[       OK ] ...
[  PASSED  ] 10 tests.
```

### Event-count result

**Exactly one `PlayerMoved` reaches the bridge per `PlayerProfile` zone-in,
both before and after this fix**, in the 512-scaled heading format the
renderer expects (`heading512 = m_heading * 512/360`, unchanged). Pinned
by `ZonePositionConvention.PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat`.

### Sites changed

- `src/client/eq.cpp` — `ZoneProcessPlayerProfile`'s position/heading block
  only (`:2153-2162` original numbering): raw writes replaced with
  `SetPosition`/`SetHeading`, comment added. Everything else in the
  function (stats block, bridge publish, skill loading) untouched.
- `tests/test_zone_position_convention.cpp` — added `#include
  "client/bridge/game_state_bridge.h"`, a `BuildPlayerProfilePayload()`
  helper, a `RecordingBridge` class, and three new tests:
  `PlayerProfile_PropagatesToGameStatePlayerPosition`,
  `PlayerProfile_HeadingConversionPreservedBitForBit`,
  `PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat`.

### Verification bar results

1. `cmake --build build -j24` (GLES2-config tree): clean full rebuild,
   only pre-existing unrelated `-Wpointer-arith` warnings in `eq.cpp`'s
   trade-packet code (untouched by this unit, same warnings T55 also
   observed).
2. `ctest --test-dir build --output-on-failure`: **1944/1945 passed**,
   sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA
   host exception). All new and pre-existing position-convention tests
   green.
3. `bash scripts/build-gles2-x86.sh`: clean build, `willeq` +
   `gles2_zone_benchmark` produced.
4. `bash scripts/ci/visual_gate.sh`: `=== VISUAL GATE: PASS ===`
   (qeynos2/gfaydark/befallen). `git status --porcelain data/` empty
   after — goldens untouched.
5. `bash scripts/build-arm-noble.sh`: `=== Success ===`, all ARM binaries
   built including `build-arm-noble/bin/willeq`.
6. `bash scripts/refactor/check_file_sizes.sh fable` (run from inside the
   slot, task branch checked out): 2 files changed vs `fable` —
   `src/client/eq.cpp` 3867 lines (**FAIL, pre-existing**: `git show
   fable:src/client/eq.cpp | wc -l` = 3853, already over the 3000-line
   threshold before this unit's +14-line net addition — same pre-existing
   condition T55 documented at this same file, unchanged in kind);
   `tests/test_zone_position_convention.cpp` 469 lines (ok). **No new
   FAILs or WARNs** — the one FAIL was already present at the `fable`
   base revision.

### Deviations from the plan, flagged rather than absorbed

- **No `eq.h` accessor added**, though the plan's territory allowed for
  "a narrow accessor in `include/client/eq.h`" if needed. Not needed:
  `GetHeading()` already read the state layer (pre-existing, unrelated to
  this unit) and T55's `Test_GetGameStatePlayerPosition()` covered the
  position half. Narrower footprint than the plan anticipated.
- **No live eqemu acceptance leg** — correctly out of scope per the
  unit's own header ("NO EQEMU... driven headlessly"); all evidence above
  is `ReplayZonePacket`-driven.
- No other deviations. Step 1's tracing was conclusive (not the STOP
  case); the fix stayed inside the named territory (`eq.cpp`'s
  `ZoneProcessPlayerProfile` position/heading block plus tests); no
  existing test failed in a way suggesting an ordering dependency; the
  stats block below the position code was not touched.

Board (`refactor/deferred.md`): re-titled the Index's succor/evac/
death-bind bullet to point at both the "T55 addendum" and a new "T58
addendum" appended (never replacing prior text) to the `` #goto `` same-
zone teleport section, documenting the independence finding and the
closure of the `PlayerProfile` gap T55 had explicitly left open. The
per-frame movement writes remain the one deliberately unconverged piece
of the position path, recorded as such in both the T55 and T58
addenda.
