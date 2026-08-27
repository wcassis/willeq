# T296 — first-person viewmodel pass, STAGE 1: get arms on screen at all

Slot: slot3. x86. OWNS eqemu (noclobber) for the live capture. Class A,
feature. Decision: docs/decisions.md 2026-08-27 viewmodel entry (Will's).
Territory: a NEW viewmodel render pass (its own TU under
`src/client/graphics/`), the render-frame hook that invokes it
(`renderer_frame.cpp`), the first-person branch in `entity_renderer.cpp`
that currently un-hides the in-world player, tests. Do NOT touch
ui_renderer/paperdoll (T294 slot1) or the shared first-person camera helper
(T295 slot2 is de-duplicating it — coordinate: read its result before
touching camera code).

## Why the previous four attempts failed — do not repeat them
T71/T74/T75/T76 all rendered the IN-WORLD player model viewed from inside.
T76 measured the outcome precisely: in camera space the arm geometry spans
**idle Z=[-1.06, 1.11], 1H-slash Z=[-1.02, 2.18], hand-to-hand Z=[-0.04,
2.15]**. **Negative Z is behind the camera.** Pullbacks of 0/0.2/0.3/0.5/1.0
were each tested; none clears it, because the span exceeds any camera
offset. **No near-plane value and no camera position fixes geometry that is
partly behind the eye.** Will has ruled out lowering the global near plane.

## The shape (verify it before building it)
A viewmodel is drawn in VIEW space at a designed offset, not at its world
position:
1. Render the world as now.
2. **Clear the depth buffer** (viewmodel must not be clipped by world
   geometry, and must not write into the world's depth for later passes).
3. Render the arm geometry + the two bone-attached weapon nodes with a
   transform that places them at a fixed offset in front of the camera, with
   the viewmodel pass's OWN projection (a small near plane is safe here
   because it applies only to this pass).
4. UI draws after, unchanged.
**Step 0 of this unit is to confirm that reasoning against the actual
renderer** — Irrlicht scene-manager structure, whether a second camera or an
explicit transform is the cleaner hook, and how the GLES2 driver handles a
mid-frame depth clear. If the renderer makes a different shape obviously
better, say so with evidence and take it; do not force this one.

## STAGE 1 SCOPE — the bar is "arms are visibly on screen", nothing more
- The pass exists, is entered only in first person, and draws the T74
  arm-filtered geometry plus the primary/secondary equip nodes.
- **Acceptance: a live capture in which arms and a held weapon are
  identifiable as arms and a weapon.** Pose quality, offset tuning, animation
  polish and FOV are STAGE 2 — do not gold-plate.
- Headless proof that the pass runs and emits draw calls, plus a pixel test
  that the viewmodel region is non-background when first person is on and
  background when it is off.
- The world render must be unchanged when first person is OFF: visual gate
  PASS with goldens untouched is the guard.

## Reporting discipline — this is the part four sessions got wrong
**OPEN EVERY CAPTURE AND DESCRIBE WHAT IS ACTUALLY IN IT.** T76's README
called an unrecognizable smeared mass "recognizable... hair strands, eyebrow,
eye"; the frames show no arm, hand, or blade. If your capture does not show
identifiable arms, **say that plainly and report Stage 1 as not achieved** —
that is a useful result, not a failure to hide. **Never self-declare visual
acceptability; Will judges.** Put the frames in review/T296/ with a README
that says exactly what each one shows.

## STOP
If the pass cannot be added without restructuring the frame loop or breaking
the world render, stop and report the conflict rather than forcing it.

## Verification
Full ctest (`-j6`); visual gate PASS with goldens untouched; Review; evidence
review/T296/. Commit: `T296: first-person viewmodel pass, stage 1`.
