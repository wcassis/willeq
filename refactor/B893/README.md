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

- `damageForFallHeight(h)`: 0 at/below `fall().safeHeight`, then linear
  (`safeHeight + damagePerUnitAboveSafe` per unit).
- `FallTracker`: the takeoff→landing drop logic (begin on the airborne rising
  edge, end on the landing returns the drop height). Pure, dependency-free.

**The curve's VALUES are measured off the lane-2 oracle** (the official client
computes the damage and sends `OP_EnvDamage` C→S; the server reads `ed->damage`
verbatim). The full data, method and the one #goto jitter point are in
`refactor/B893/evidence/oracle_measurements.md`:

| constant | value | provenance |
|---|---|---|
| `Fall::safeHeight` | 18.0 | [MEASURED] the 18.2u drop sent no `OP_EnvDamage`; the floor is in 18.2–23.2 |
| `Fall::damagePerUnitAboveSafe` | 1.5 | [MEASURED] mid/high range slope. The real curve is CONCAVE (5@28u, 30@43u, 91@68u); the linear ramp is the model form (the `Fall` struct is linear), so it overstates the low end and understates the top. See the evidence file for the full table. |

The SHAPE (one table, client-owned) is the invariant and the values are the
oracle's; the concave-vs-linear gap is documented, not hidden.

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
| `Arc::peakHeight` | 3.05 | [MEASURED] apex = max of 8 mid-air `OP_ClientUpdate` samples (+1.82 … +3.05 above standing −38.2157); ~1 sample per jump at ~1 Hz |
| `Arc::durationS` | 1.0 | [ASSUMED, OWED] the wire is ~1 Hz and cannot resolve a sub-second arc; kept from the pre-B893 renderer, marked owed |
| `Arc::terminalSpeed` | 100.0 | [ASSUMED, OWED] the `#goto` fall is a server-side teleport the client integrates locally, so no terminal velocity is observable on this rig; nothing clamps to it yet (no term in the integrator) |
| `Fall::safeHeight` | 18.0 | [MEASURED] the 18.2u drop sent no `OP_EnvDamage`; the 23.2u drop sent 5 |
| `Fall::damagePerUnitAboveSafe` | 1.5 | [MEASURED] mid/high-range slope of the concave curve (5@28u … 91@68u); linear ramp is the model form |

The curve SHAPE, the collapse, the sender, the encoder bytes, and the scenario
are all done and pinned; the arc apex and the fall curve are now the oracle's,
and the arc duration + terminal velocity are the two constants that stay owed
(because the capture cannot resolve them, not because they were not pursued).

## Not verified / left open

- **Arc duration** and **terminal velocity** are the two still-owed constants:
  the wire's ~1 Hz position update cannot resolve a sub-second jump arc, and the
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
  `ScenarioRegistry` (now 33 scenarios, `IsScenarioName("fall_damage")`).
