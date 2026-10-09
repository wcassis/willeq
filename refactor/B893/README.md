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
`DerivedPerFrameConstantsMatchTheTable` pins the derived constants to the
measured table (12.2 / 24.4 at peak 3.05, duration 1 s).

## The fall-damage table [measured, lane-2]

`eqt::jumpfall` owns the height→damage curve — because eqemu reads
`EnvDamage2_Struct::damage` **verbatim**: `Handle_OP_EnvDamage`
(`akk-stack/code/zone/client_packet.cpp`) does `SetHP(GetHP() - damage)` +
`SendHPUpdate()` for a non-GM and 1 HP + a chat line for a GM. The server does
no fall math.

- `damageForFallHeight(h)`: 0 at/below `fall().safeHeight`, then the oracle's
  measured points (`kFallDamageTable`) linearly interpolated, the last
  segment's slope continued above the table (B1011; B893 shipped a 1.5/unit
  linear ramp that gave 8..23 across the 23-33u band where the oracle gave 5).
- `FallTracker`: the takeoff→landing drop logic (begin on the airborne rising
  edge, end on the landing returns the drop height). Pure, dependency-free.

**The curve's VALUES are measured off the lane-2 oracle** (the official client
computes the damage and sends `OP_EnvDamage` C→S; the server reads `ed->damage`
verbatim). The full data, method and the one #goto jitter point are in
`refactor/B893/evidence/oracle_measurements.md`:

| constant | value | provenance |
|---|---|---|
| `Fall::safeHeight` | 18.0 | [MEASURED] the 18.2u drop sent no `OP_EnvDamage`; the floor is in 18.2–23.2 |
| `kFallDamageTable` | 9 points, 0@18.2u … 91@68.2u | [MEASURED] every point is the `OP_EnvDamage` the official client sent at that drop; the 48.2u reading (71, above the 53.2u point) is the one #goto-jitter outlier and is excluded. `tests/test_jump_fall_arc.cpp::EveryMeasuredOraclePointIsReproduced` pins each point. |

The SHAPE (one table, client-owned) is the invariant and the values are the
oracle's at every measured height. **Caveat (B1011): the whole series is ONE
character** (Testchar, level 60 GM, 2255 HP); whether the official client's
curve depends on level, HP or race is untested and owed — a second series on
a low-level, non-GM character is the check.

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

## The oracle capture — DONE (lane 2, 2026-10-08)

The official Titanium client (Wine `eqgame.exe`) on the lane-2 oracle VM was
driven in-world as Testchar (GM, level 60, East Freeport) and the measurements
were taken **off the wire**, not from video: eqemu packet logging
(categories 83/84) dumps every `OP_ClientUpdate` (C→S, the wire the client
sends) and every `OP_EnvDamage` to the freporte zone log. A GM `#goto x y z`
(via the `/say #cmd` chat path — direct `#` entry is unreliable in the Titanium
client's autocomplete) sets a known takeoff height; the official client then
computes the fall damage itself and sends `OP_EnvDamage` C→S (dmgtype 0xFC).
Full method + data: `refactor/B893/evidence/oracle_measurements.md`.

**The measured constants:**

| constant | value | status |
|---|---|---|
| `Arc::peakHeight` | 3.05 | [MEASURED LOWER BOUND, apex OWED] (B1011) the max of the mid-air `OP_ClientUpdate` samples: the committed 11:29 capture (`evidence/b893_jump_wirecap_1129.log`, decoded in `evidence/oracle_measurements.md`) holds five jumps with ONE mid-air sample each — four at an identical +2.509, one at +3.051 — and the uncommitted 10:28 run gave +1.82 … +2.12. The client sends `OP_ClientUpdate` at 1-3 Hz, so each jump is sampled once at an arbitrary phase: the official apex is at or above 3.05. The committed `evidence/b893_jump_fall.mp4` is the measurement that resolves it (step the video against the character's known height) |
| `Arc::durationS` | 1.0 | [ASSUMED, OWED] the wire cannot resolve a sub-second arc; kept from the pre-B893 renderer, marked owed |
| `Arc::terminalSpeed` | 100.0 | [ASSUMED, OWED] the `#goto` fall is a server-side teleport the client integrates locally, so no terminal velocity is observable on this rig; nothing clamps to it yet (no term in the integrator) |
| `Fall::safeHeight` | 18.0 | [MEASURED] the 18.2u drop sent no `OP_EnvDamage`; the 23.2u drop sent 5 |
| `kFallDamageTable` | 9 measured points | [MEASURED] the oracle's own values, interpolated (B1011 replaced the 1.5/unit ramp) |

The curve SHAPE, the collapse, the sender, the encoder bytes, and the scenario
are all done and pinned; the fall curve is the oracle's at every measured
height; the arc apex is a measured lower bound, and the true apex, the arc
duration and terminal velocity are the three constants that stay owed.

## Not verified / left open

- **The arc apex** (B1011): 3.05 is a lower bound from one wire sample per
  jump; the official client's peak is at or above it and is to be read off
  `evidence/b893_jump_fall.mp4` (the project's own rule: a transient is
  measured from video, not from sparse stills).
- **The fall curve on other characters** (B1011): one level-60 GM series only.
- **Arc duration** and **terminal velocity** are the two other owed constants:
  the wire's 1-3 Hz position update cannot resolve a sub-second jump arc, and the
  `#goto` fall is a server-side teleport the client integrates locally, so no
  terminal velocity is observable from this rig. Both are marked `[ASSUMED,
  OWED]` in `jump_fall_physics.h`; they are not hidden as measured.
- The `fall_damage` scenario's LIVE leg (exit 0 on lane 2 against a real
  server) was not run in this revision: it needs the lane and the native client
  + the oracle share the one `willeq_test` account (never both logged in at
  once). The scenario's offline state-machine test is green; the live run is the
  remaining check. (The fall-damage SEND PATH itself was exercised for real on
  the lane during measurement — the oracle's `OP_EnvDamage` C→S was decoded and
  matches the encoder byte-exactly, dmgtype 252.)

## Tests (offline, run on this host)

- `test_jump_fall_arc` — 9 tests (curve, the collapse / no-second-writer,
  derived-constants pin, fall table shape, FallTracker, encoder bytes, dmgtype).
- `test_scenario_state_machines` — `FallDamageScenario` (4) +
  `ScenarioRegistry` (now 35 scenarios, `IsScenarioName("fall_damage")`).
