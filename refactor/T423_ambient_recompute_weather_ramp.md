# T423 — the scene ambient recomputes on every input (B362); one weather ramp (B361)

Class A + B, slot2, no server. Read B362 (fable recomputes the scene
ambient only on an `OP_TimeOfDay` packet — T418's first live session ran
a whole block with the pre-fix 0.460 modifier live), B361 (three copies
of the weather ramp; the one under test is one production never runs),
T396/T418's §Reviews, `updateTimeOfDay()` / `composeSceneAmbient()`
(T396), the weather controller, and the vision floor (T405).

## Leg A — B362

Every input to `composeSceneAmbient()` (clock, weather modifier, vision
floor, zone override) must trigger a recompute when IT changes, not only
the clock packet: a dirty flag set by each writer, consumed once per
frame (G05 frame-order golden learns the statement in place). Tests
pre-fix failing: a weather-modifier change with no clock packet changes
the ambient next frame; a vision change likewise (T405's floors already
call `refreshVisionLighting()` — read whether that path is the same or a
second one; make it one).

## Leg B — B361

Three weather ramp implementations; `computeState()` and
`update()`/`updateStormAtmosphere()` are unreached. Delete the two
unreached ones (compiler proof; K12 ff), keep the one production runs,
and move T396/T418's tests onto it (they must fail against a planted
ramp change in the live copy). Strike B361.

## Proof

Build (background), `ctest -j6`, pre-fix/post-fix, visual gate standalone
+ ARM (no image golden should move), file sizes.

STOP: none expected.
