# T335 — The three debug overlays with T333's defect verbatim (B054)

Class B (off by default, no live evidence) but the cause is measured and
the fix is a pattern. Slot2. Territory: `src/client/graphics/overlays/overlays_component.cpp`
(`drawMapOverlay`), `src/client/graphics/frame/renderer_frame_render.cpp`
(the `frustumDebugDraw_` block — inside its G05 pass; do NOT reorder
passes), `src/client/graphics/player/renderer_camera.cpp`
(`PlayerCameraComponent::drawCollisionDebugLines`), one test per site
modelled on `tests/test_navmesh_overlay_draw.cpp`, `refactor/BOARD.md`.

## The fix (T333's, applied three times)

Each site draws 3D lines after `smgr->drawAll()` while the GLES2 driver
is latched in 2D mode: it must (a) set all three transforms from
`ctx_.captured3DView` / `captured3DProj` gated on `have3DTransforms()`
(the `drawTargetOutline()` / fixed `drawNavmeshOverlay()` idiom) and (b)
restore the three transforms and the 2D-safe render state afterwards via
`setMaterial()` with the `EMT_TRANSPARENT_ALPHA_CHANNEL` restore material
(its TYPE is load-bearing — read T333's in-place comment). Factor the
save/restore into ONE helper on the component that owns the captured
transforms (`OverlaysComponent::withWorldLines(...)` or a small RAII
guard) and call it from all four sites including the navmesh one, so
the pattern exists once. If the frustum block or the collision lines
need a test seam like T333's `seedNavmeshOverlayTriangleForTest`, add
the smallest equivalent.

## Proof

Per site: the pre-fix failing pixel test (a line lands where the 3D
projection puts it; the 2D UI quad drawn afterwards survives), pasted;
post-fix passing. Visual gate + ARM standalone (goldens untouched — all
three overlays are off in the gate).

STOP: a pass reorder; a site whose draw does not run in the latched-2D
window (then it is not this defect — say so and leave it).
