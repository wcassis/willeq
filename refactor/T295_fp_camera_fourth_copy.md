# T295 — the fourth first-person camera copy discards T76's fix whenever the player moves

Slot: slot2. x86. OWNS eqemu (noclobber) for the live proof. Class A.
Territory: `src/client/graphics/renderer_player_physics.cpp` (the
`updatePlayerMovement()` camera block ~690-740), the shared first-person
camera helper it should call instead, `entity_renderer.cpp`'s dead
`updateFirstPersonWeapons()` + its declaration, tests. Do NOT touch the near
plane or add a viewmodel — that is a separate decision (see below). Do NOT
touch ui_renderer / paperdoll (T294 slot1).

## Finding (audit 2026-08-27, verified at the tip)
T75's Review claims it removed "the last duplicate" of the first-person
camera formula, but that de-duplication was at `irrlicht_renderer.cpp:2683`
(the server-position path). **A FOURTH copy lives in
`renderer_player_physics.cpp:690-740`**, the per-frame LOCAL movement path.
It sets the camera from the head bone / bbox eye height directly and applies
**neither `kFirstPersonCameraStandoffUnits` (0.3) nor the 12° down-pitch** —
it uses `playerPitch_`. It runs whenever `positionChanged || headingChanged`.

**So T76's only shipped fix is overwritten the moment the player moves or
turns** — exactly the "while moving" case. Anyone re-testing first person in
motion is testing the pre-T76 camera without knowing it.

## Scope
1. Route that path through the SAME helper the other three use (one body —
   T230/T239 rule). If no helper exists, extract one; do not add a fifth
   formula. A test must fail if a fourth formula reappears (the T264
   source-parsing style is the precedent).
2. Prove it live: enter first person, stand still, capture; then move and
   turn, capture again — the camera origin/pitch must be identical in both
   (log the computed eye position and pitch each frame at DEBUG and diff
   them). Before the fix they differ; after, they do not. **This is a camera
   correctness proof, NOT a claim that first person looks right** — it does
   not, and that is T296's question.
3. **Delete `updateFirstPersonWeapons()`** (`entity_renderer.cpp:2831`, decl
   `entity_renderer.h:407`): zero callers anywhere in src/include/tests, and
   it is a near-no-op that re-calls `updateEquipmentTransforms` which the
   normal per-entity update already does at six sites. Reachability pattern
   #9's last surviving member — strike it on the board with the grep proof.
4. While you are there, confirm T76's head-bone eye-height lever actually
   engages live: its own captures logged the BBOX fallback (3.65), not the
   head bone (3.10), so the lever is proven only headlessly. Report which
   fires live; do not change it.

## Explicitly NOT in scope
The near clip plane and any viewmodel pass. T76 measured that arm geometry
straddles the 1.0 near plane in EVERY pose and that no camera-position lever
clears it; fixing that is an approach decision for Will (T296 will carry it).

## Verification
Full ctest (`-j6`); visual gate; Review; evidence review/T295/ with the
still-vs-moving camera logs. Commit:
`T295: one first-person camera formula; the movement path no longer discards it`.
