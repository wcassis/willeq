# T359 — Combat movement and Follow track a spawn id (B003d); loadSoundFont precondition (B015); setEntitySize limit (B011)

Class B, slot3. Three disjoint items; one branch, three commits.

## Leg A — B003d

T356 made moveTo/follow resolve ONCE (ranked, in the dispatcher) and pass a
spawn id down. The same unranked prefix resolver
(`EntitiesComponent::FindEntityByName`) still backs
`MoveToEntityWithinRange`, `StartCombatMovement(name)`, `UpdateCombatMovement`,
and `MovementComponent::Follow` re-matches its target NAME every tick
(`movement_update.cpp:183`).

1. Find every production caller of `FindEntityByName` (comment-filtered grep
   + the compiler by deleting the declaration). Record the list.
2. Combat movement: resolve once at the entry point (`StartCombatMovement`'s
   caller — find where the name comes from; if it is the current TARGET, the
   target's spawn id is already known and the name lookup is pure drift), pass
   the spawn id; `UpdateCombatMovement` reads the entity by id each tick.
3. Follow: store the spawn id; each tick look the entity up by id; if it is
   gone (despawned / zoned), stop following the same way a name miss does
   today (read what it does now and preserve it).
4. When `FindEntityByName` has zero callers, delete it (K12 ff check).
5. Tests, pre-fix failing: two identically-named mobs, the nearer spawned
   second; combat movement and Follow must track the nearer across two ticks
   (mirror T356's test). Movement TUs are in the G06 order golden: run
   `test_movement_order`; if a statement changes, the golden learns it IN
   PLACE (correction 32), both faults re-planted.
6. Strike B003d.

## Leg B — B015

`MidiPlayer::loadSoundFont` swaps a raw `tsf*` with no synchronisation; safe
only because its one call site runs before the audio thread exists. Assert
the precondition: a check that the audio thread is not running (an existing
flag / the thread handle) that LOGs and refuses the swap otherwise — do not
add a mutex to the audio callback. Test: call after the thread is up → refused
and logged; before → accepted. Strike B015.

## Leg C — B011

`EntityRenderer::setEntitySize` updates neither `collisionHeight` nor `deckZ`.
Nothing resizes a boat, so assert the limit: a test that `setEntitySize` on an
entity with a deck leaves `deckZ` unchanged AND a source-reader pin that no
production caller passes a boat — OR recompute both if the recompute is a
one-liner from the existing height formula (read it; decide; say which and
why). Strike B011.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, movement
golden, file size check; Leg C touches `src/client/graphics` → visual gate
standalone + ARM build. Tip carries willeq.db v6: rebuild all three trees
first.

STOP: `StartCombatMovement`'s name does not come from the target and has no
spawn id available (then report the shape and do Legs B and C).
