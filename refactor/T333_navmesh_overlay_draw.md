# T333 — The navmesh overlay draws nothing and wipes the 2D UI (B048)

Class A, live-measured (`review/T329/`). Slot2. Territory:
`src/client/graphics/overlays/overlays_component.cpp` (`drawNavmeshOverlay`
and, if the fix belongs there, `drawMapOverlay` which likely shares the
pattern), `include/client/graphics/overlays/overlays_component.h`, a new
test, `refactor/BOARD.md`, `review/T333/`.

## Measure first

Read `drawNavmeshOverlay()` and the 2D-UI draw order in
`src/client/graphics/frame/renderer_frame_render.cpp` (G05 split it into
passes — find which pass draws the overlays and which draws the static
UI, and the order). Then answer, from the code and from Irrlicht's
semantics: (a) `driver->draw3DLine` after `setMaterial(...)` +
`setTransform(ETS_WORLD, …)` — does the function restore the material and
transform afterwards? If not, every later 2D draw in the same frame
inherits a 3D material with depth test/lighting — that is the "UI
vanishes" symptom. (b) Are the lines drawn in world space with the
camera's view/projection set, or after the 2D pass reset them to
identity — that is the "nothing draws" symptom. Compare with
`drawMapOverlay()` / `drawZoneLineOverlay()` (siblings that work, per
the goldens) and with how `collisionDebugLines_` are drawn
(`PlayerCameraComponent::drawCollisionDebugLines`, called from the frame
pass that works).

## Fix

Match the working sibling exactly: save/restore material and transform
around the 3D line batch (or draw in the pass the siblings use), and
draw with the same overlay material they use. Smallest change; no
behaviour change to any other overlay.

## Proof

1. A test that the material/transform state after `drawNavmeshOverlay()`
   equals the state before it (a null/mock driver recording
   `setMaterial`/`setTransform` calls — read how the existing UI-atlas
   pixel tests construct their driver; `tests/test_ui_atlas_*.cpp`), and
   that a non-empty triangle list produces `3 × N` line draws.
2. **The live pair** (needs the eqemu lock; T332 may hold it — if held,
   the pair is owed and said so): Ctrl+N ON and OFF in qeynos2, same
   session, force daylight, `--eqe1-path`; the ON capture shows navmesh
   lines AND the health bars / FPS / chat / hotbar. `review/T333/README.md`
   with both PNGs.
3. Visual gate + ARM standalone (a graphics file changes; goldens
   untouched — the overlay is off in the gate).

STOP: the fix needs a change in the frame pass order (that is G05's
golden — report, do not reorder).
