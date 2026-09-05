# T360 — Vision switch un-swapped (B066); appearance/animation through PacketSender (B068)

Class A + B, slot2. Two disjoint items; one branch, two commits. Read the
board text of B066 and B068 (both measured live in T358; `review/T358/`).

## Leg A — B066

`SimulationWorker::applyVisionWeatherToZoneLights` and
`computeLightAnimations` (`src/client/graphics/sim_animation.cpp`) switch
on `visionType` with hand-written `case 1: // Ultravision`, `case 2: //
Infravision` — swapped against `EQT::Graphics::VisionType` (Normal 0,
Infravision 1, Ultravision 2) and `computeVisionType`. The header comment
on `SimulationInput::visionType` carries the same wrong mapping.

1. Replace the hand-written cases with the enum (cast the int to
   `VisionType` once; switch on the enum; no magic numbers survive).
2. Correct the header comment.
3. Test, pre-fix failing: Ultravision → intensity 1.0 / redShift 0;
   Infravision → 0.75 / 0.3; Normal → untouched. Drive the two functions
   through whatever seam the existing sim tests use.
4. Leave B067 alone (it needs the oracle — a separate unit); do NOT change
   the intensity values, only which arm they belong to.
5. Strike B066.

## Leg B — B068

`EntitiesComponent::SendSpawnAppearance` / `SendAnimation`
(`src/client/entities/entities_component_world.cpp`) call
`ctx_.zone_connection->QueuePacket(p)` directly, bypassing `PacketSender`,
so the session recorder never sees them and the death burst reaches the
server with the appearance BEFORE OP_Save (T349 asserts Save first).

1. Route both through `PacketSender` (the same call `SendSave` uses —
   `SafeQueueZone`); delete the explicit `Test_RecordQueuedPacket` line if
   PacketSender already feeds that seam (check; if it does not, keep the
   seam fed from ONE place).
2. Tests, pre-fix failing: (a) a recorded session (the R01 recorder's test
   seam) contains the C→S OP_SpawnAppearance after `SendSpawnAppearance`;
   (b) the death burst order at the PacketSender level is OP_Save, then
   SpawnAppearance, then ZoneChange (extend `test_self_death_sequence` or the
   G07 golden — a statement-text golden learns a reviewed edit IN PLACE, both
   faults re-planted).
3. Any T268 ordered-capture test must still pass unchanged.
4. Strike B068.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, G07 golden,
file size check; Leg A touches `src/client/graphics` → visual gate
standalone + ARM build. Tip carries willeq.db v6: rebuild all three trees
first.

STOP: `PacketSender` cannot be reached from `EntitiesComponent` without a
new context service (then report the shape; do Leg A).
