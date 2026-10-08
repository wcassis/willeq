# B893 — one jump model, and the client reporting falls

Issue #209 (resolves B463 #33, the 25-unit jump). Branch
`task/kb-b893-jump-fall-damage` (2026-10-08). Two independent jump arcs lived in
the client at once, and willeq never sent fall damage. This unit collapses the
two arcs to one owner and adds the fall-damage path end to end.

## Leg A — which model owned the wire Z, and the collapse [done]

In Player Mode **both** integrators ran off the same Space keypress:

- the headless game thread: `MovementComponent::UpdateJump()`
  (`src/client/movement/movement_state.cpp`) ran a closed-form 25-unit / 1000 ms
  arc and its Z reached the wire through `SendPositionUpdate()`;
- the render thread: `PlayerCameraComponent` integrated `jumpVelocity 40` /
  `gravity 80` (`include/client/graphics/renderer_types.h`) — a 10-unit, 1 s
  arc — whose Z reached the wire through the `PlayerPositionChanged` intent
  (`src/client/graphics/player/renderer_player_physics.cpp:957`).

**The renderer owns the wire Z in Player Mode.** `sendPositionUpdateToServer()`
is the LAST phase of `updatePlayerMovement()` (phase 7, after phase 5's
collision snap), and its `PlayerPositionChanged` intent is drained on the game
thread by `BridgeComponent::onPlayerPositionChanged` → `OnGraphicsMovement` →
`SendPositionUpdate` — so the renderer's Z overwrites the headless
`UpdateJump`'s Z on the same frame. The 25-unit arc was headless-only
inflation; the 10-unit arc was the player-visible one.

The collapse: the arc's peak height and duration are ONE pair of constants in
`include/client/jump_fall_physics.h` (`Arc`, `arc()`), and both integrators read
them:

- `UpdateJump()` now reads `eqt::jumpfall::arc()` — its local 25/1000 constants
  are deleted;
- `renderer_types.h` seeds `jumpVelocity`/`gravity` from the table via
  `arc().initialVelocity()` / `arc().gravity()` (derived `4P/D` and `8P/D²`), so
  the pair cannot drift.

Pinned by `tests/test_jump_fall_arc.cpp::NoSecondWriterOfTheArcConstants`: it
fails if `movement_state.cpp` declares its own `jump_duration_ms`/`jump_height`,
or if `renderer_types.h` seeds an arc constant from a BARE numeric literal (a
`eqt::jumpfall::` derivation is the allowed single writer). The collapse is
behaviour-preserving at the assumed values:
`DerivedPerFrameConstantsMatchTheOldLiterals` pins 40/80.

## The fall-damage table [shape done, values OWED]

`eqt::jumpfall` owns the height→damage curve — because eqemu reads
`EnvDamage2_Struct::damage` **verbatim**: `Handle_OP_EnvDamage`
(`akk-stack/code/zone/client_packet.cpp`) does `SetHP(GetHP() - damage)` +
`SendHPUpdate()` for a non-GM and 1 HP + a chat line for a GM. The server does
no fall math.

- `damageForFallHeight(h)`: 0 at/below `fall().safeHeight`, then linear
  (`safeHeight + damagePerUnitAboveSafe` per unit).
- `FallTracker`: the takeoff→landing drop logic (begin on the airborne rising
  edge, end on the landing returns the drop height). Pure, dependency-free.

**The curve's VALUES are [ASSUMED], not measured** (safeHeight 4,
damagePerUnit 3 — see the "oracle capture" section). The SHAPE (one table,
client-owned) is the invariant; the numbers move when the lane-2 capture lands.

## The sender and the scenario [done, offline]

- `MovementComponent::SendFallDamage(float fallHeight)`
  (`src/client/movement/movement_state.cpp`) converts height→damage via the
  table (0 = no send), guards on `IsFullyZonedIn()`, encodes
  `EnvDamage2_Struct` (dmgtype 252) and `SafeQueueZone`s it. This is the client's
  first `OP_EnvDamage` sender — before this unit the client only PRINTED the
  opcode.
- `tests/test_jump_fall_arc.cpp::TheEncoderIsByteExact` pins the wire bytes to
  the pcap (`envdamage` 0x31b3 C→S); `DmgTypeFallingIs252` pins eqemu's
  `EnvironmentalDamage::Falling`.
- The `fall_damage` scenario (`src/client/scenario/scenario_fall_damage.cpp`,
  registered in `scenario_registry.cpp`) reads the baseline HP, drives
  `world.SimulateFall` (→ the client's own `SendFallDamage`), then waits
  `fallWaitSeconds` for the server's HP to drop. A pass is "the HP dropped after
  the send"; a fail is "the send went out and the HP did not move" — exactly the
  pre-B893 defect (no sender). Its state-machine cases run device-free in
  `tests/test_scenario_state_machines.cpp` (4 cases: pass, HP-never-moves fail,
  unreadable-HP fail, height-is-an-option).

## The in-play trigger (graphical mode) [done, offline]

Only the renderer can SEE a fall — it is the one that integrates gravity and
snaps to ground; the headless game thread (the only packet sender) has no
per-frame integrator. The renderer integrates jump/fall gravity **only while
`isJumping` is set** (Space, `renderer_player_physics.cpp:129` and
`:301`), so a damaging fall in the current model is a jump that lands lower than
its takeoff (jumping off a ledge). `FrameComponent::processFrameInput()`
(`src/client/graphics/frame/renderer_frame_input.cpp`) straddles
`updatePlayerMovement()` and, on the airborne falling edge, pushes a
`FallDamageIntent` for the drop; the game thread drains it via
`BridgeComponent::onFallDamageIntent` → `MovementComponent::SendFallDamage`
(the sanctioned cross-thread channel). A normal standing jump lands at its
takeoff → 0 → nothing sent. A horizontal relocation (teleport/door/zoning)
breaks the fall so it deals nothing.

This straddle is deliberately in `renderer_frame_input.cpp`, NOT
`renderer_player_physics.cpp`: the movement-order golden
(`tests/test_movement_order.cpp`, `kTU` = `renderer_player_physics.cpp`) polices
that TU's statement order and the static-ui test parses this TU only for the
mouse-wheel reads and wheel-routing calls — which the block does not touch.

## The oracle capture — NOT done, BLOCKED [values owed]

**What blocked it, exactly:** the lane-2 eqemu server was healthy (loginserver
pid 325 on UDP 5998, world on 9000, zones up; a native `willeq_test` login
succeeded), but the **official-client (Wine `eqgame.exe`) login looped**: every
submit of the credentials returned the "You need to enter a username and
password" modal, across a fresh Wine instance, scripted field-fills, and
manual box-clicks. This is a **client-side field-focus issue in the Wine
oracle**, not a server or lane defect — the native client logs in fine on the
same lane. It could not be cleared from a headless session, so the video
capture (three standing jumps + a fall from a known height) never happened.

**Therefore every constant that depends on the capture is [ASSUMED] and owed:**

| constant | value | status |
|---|---|---|
| `Arc::peakHeight` | 10.0 | [ASSUMED] — Leg C measures it |
| `Arc::durationS` | 1.0 | [ASSUMED] — Leg C measures it |
| `Arc::terminalSpeed` | 100.0 | [ASSUMED, OWED] — nothing clamps to it yet (no terminal-velocity term in the integrator) |
| `Fall::safeHeight` | 4.0 | [ASSUMED] — Leg C measures the highest safe fall |
| `Fall::damagePerUnitAboveSafe` | 3.0 | [ASSUMED] — Leg C measures the damage for a known drop |

The curve SHAPE, the collapse, the sender, the encoder bytes, and the scenario
are all done and pinned; the NUMBERS are owed to the lane-2 capture.

## Not verified / left open

- Oracle capture (blocked above); peak height, arc duration, damage-for-drop,
  and terminal velocity are owed, so the table values stay [ASSUMED].
- The `fall_damage` scenario's LIVE leg (exit 0 on lane 2 against a real
  server) was not run: it needs the lane, and the native client + the oracle
  oracle share the one `willeq_test` account (never both logged in at once).
  The scenario's offline state-machine test is green; the live run is the
  remaining check.
- Terminal velocity: no integrator term yet (pre-B893 had none); owed with the
  capture.

## Tests (offline, run on this host)

- `test_jump_fall_arc` — 9 tests (curve, the collapse / no-second-writer,
  derived-constants pin, fall table shape, FallTracker, encoder bytes, dmgtype).
- `test_scenario_state_machines` — `FallDamageScenario` (4) +
  `ScenarioRegistry` (now 33 scenarios, `IsScenarioName("fall_damage")`).
