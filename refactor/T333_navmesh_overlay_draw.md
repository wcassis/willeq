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

## Review

Branch `task/T333-navmesh-overlay-draw`, from `fable` at `bc22a791`.

### The measured cause

Not the triangle list (T329 already proved 112 triangles arrive), and not the
frame pass order. It is one piece of driver state, and it produced **both**
symptoms.

`COpenGLES2Driver::setOrthoProjection()` (docker/irrlicht-drm/COpenGLES2Driver.cpp)
is the driver's whole 2D entry: it overwrites `Matrices[ETS_PROJECTION]` with an
ortho screen matrix, identity-fills `ETS_VIEW`/`ETS_WORLD`, calls
`setDepthTest(false)` / `setDepthWrite(false)` / `setCull(false)`, and **latches
`Is2DMode = true`**. Its first statement is `if (Is2DMode) return;`. Only
`beginScene()`, `setRenderTarget()`, and `restore3DProjection()` (called from
`drawVertexPrimitiveList` and `drawMeshBuffer`) clear the latch — `setMaterial()`
and `setTransform()` do not, and neither does `draw3DLine()`.

By the time `renderDebugOverlays()` runs, 2D drawing has already happened this
frame — the same fact `drawTargetOutline()` documents ten lines above the code
under repair ("after `drawAll()`, `ETS_VIEW` and `ETS_PROJECTION` may have been
overwritten by 2D rendering (billboards, GUI nodes, overlays)"). So on entry to
`drawNavmeshOverlay()` the driver holds an ortho projection and the 2D latch.
Then:

**(a) Nothing draws.** The function set only `ETS_WORLD`. `draw3DLine()` computes
`wvp = PROJECTION * VIEW * WORLD`, so the navmesh's world-space vertices were
multiplied by an ortho matrix built in *pixel* units and landed far outside the
screen box.

**(b) The 2D UI vanishes.** `setMaterial(lineMaterial)` reaches
`applyMaterialRenderState()`, which does `setDepthTest(material.ZBuffer != ECFN_NEVER)`
— true — and `setCull(material.BackfaceCulling, GL_BACK)` — and
`SMaterial::BackfaceCulling` defaults to **true**. The static-UI pass that
follows draws through `draw2DImage`/`draw2DRectangle`, both of which call
`setOrthoProjection()` first — which early-returns, because the latch is still
set. `setCull(false)` therefore never runs, and every UI quad (wound clockwise
once the ortho matrix flips Y) is back-face culled away. That is the health
bars, the FPS counter, the chat panel and the hotbar, exactly as `review/T329/`
captured them.

Answering the contract's two questions directly: (a) no, the function restored
neither material nor transform, and yes, that is the "UI vanishes" symptom —
though the operative leftover is **culling**, not lighting or depth; (b) the
lines were drawn in world space *after* a 2D pass had already replaced the
view/projection with an ortho pair, which is the "nothing draws" symptom.

Both halves were **measured, not argued**. The new test reproduced both
headlessly on a real GLES2 device before any fix existed (transcript below), and
symptom (b) was isolated by a one-line experiment: setting
`lineMaterial.BackfaceCulling = false` and changing nothing else made the UI
rectangle reappear while the lines still did not draw. That is what names the
wipe as culling rather than depth.

### The fix

`OverlaysComponent::drawNavmeshOverlay()` only, matching the two post-`drawAll()`
3D draws in this tree that already work — `drawTargetOutline()` in the same file
and the portal wireframe batch in `renderer_frame_render.cpp`:

1. Save `ETS_VIEW`/`ETS_PROJECTION`/`ETS_WORLD` on entry.
2. Set `ETS_VIEW`/`ETS_PROJECTION` from `ctx_.captured3DView`/`ctx_.captured3DProj`
   (the pair captured during the `ESNRP_SOLID` pass), gated on
   `ctx_.Frame().have3DTransforms()` — `drawTargetOutline()`'s idiom exactly.
3. After the batch, restore all three transforms and re-establish the 2D-safe
   render state. `setMaterial()` is the only public door to
   `setCull()`/`setDepthTest()`/`setDepthWrite()`, so the restore is spelled as a
   material: `BackfaceCulling = false`, `ZBuffer = ECFN_NEVER`,
   `MaterialType = EMT_TRANSPARENT_ALPHA_CHANNEL`. The material *type* is
   load-bearing and commented in place — `applyMaterialRenderState()` switches on
   `MaterialType` for the depth-write and **ignores `ZWriteEnable`**, and this
   type's branch is the depth-write-off one. The result reproduces
   `setOrthoProjection()`'s 2D state exactly. Blending is set explicitly by every
   2D draw call, so it does not matter which way this leaves it.

**Nothing was reordered** — the STOP did not fire. `tests/goldens/frame_render_order_g05.txt`
is byte-identical and no visual golden was touched.

Header change: one documented test seam,
`OverlaysComponent::seedNavmeshOverlayTriangleForTest()` plus
`navmeshOverlayTriangleCount()`. `drawNavmeshOverlay()`'s only production filler
is `updateNavmeshOverlay()`, which needs a live Detour-backed `PathfinderNavmesh`;
the pixel test has a real GLES2 device but no navmesh.

### Deliberately NOT fixed — boarded as B054

`drawMapOverlay()` (Ctrl+M), `renderer_frame_render.cpp`'s `frustumDebugDraw_`
block, and `PlayerCameraComponent::drawCollisionDebugLines()` carry the identical
defect verbatim: same pass, same `setMaterial()` + world-space `draw3DLine()`,
same missing view/projection and missing restore. The contract said smallest
change and no behaviour change to any other overlay, and none of the three has
live evidence, so all three were left alone and boarded with the cause named.

### The pre-fix failing run

`build/bin/test_navmesh_overlay_draw` at the tip of the diff minus the fix
(seam and test present, `drawNavmeshOverlay()` unchanged), exit status 1:

```
[==========] Running 4 tests from 1 test suite.
[ RUN      ] NavmeshOverlayDrawTest.NavmeshTrianglesReachTheFramebuffer
/mnt/projects/fable/wt/slot2/tests/test_navmesh_overlay_draw.cpp:180: Failure
Expected: ((int)c.getGreen()) > (100), actual: 0 vs 100
bottom edge of the navmesh triangle drew nothing at (400,540) -- got 0,0,0

/mnt/projects/fable/wt/slot2/tests/test_navmesh_overlay_draw.cpp:180: Failure
Expected: ((int)c.getGreen()) > (100), actual: 0 vs 100
left edge of the navmesh triangle drew nothing at (240,300) -- got 0,0,0

/mnt/projects/fable/wt/slot2/tests/test_navmesh_overlay_draw.cpp:180: Failure
Expected: ((int)c.getGreen()) > (100), actual: 0 vs 100
right edge of the navmesh triangle drew nothing at (560,300) -- got 0,0,0

[  FAILED  ] NavmeshOverlayDrawTest.NavmeshTrianglesReachTheFramebuffer (1017 ms)
[ RUN      ] NavmeshOverlayDrawTest.TheTwoDUIDrawnAfterTheOverlayStillAppears
/mnt/projects/fable/wt/slot2/tests/test_navmesh_overlay_draw.cpp:198: Failure
Expected: ((int)c.getRed()) > (200), actual: 0 vs 200
the 2D UI drawn after drawNavmeshOverlay() was wiped -- got 0,0,0

[  FAILED  ] NavmeshOverlayDrawTest.TheTwoDUIDrawnAfterTheOverlayStillAppears (367 ms)
```

After the fix, 4 of 4 pass. The test is registered in
`tests/cmake/renderer_guards.cmake` and self-skips without a usable GLES2
device; it needs no EQE1 asset and no server.

### The live pair — OWED, and why

`/mnt/projects/fable/.eqemu-lock` read `T332 2026-09-04` for this unit's entire
run (slot1). No session was possible, so the ON/OFF pair the contract's Proof 2
asks for was not captured. `review/T333/README.md` records exactly what is owed,
the session parameters to use, and points at `review/T329/t329_live.sh` and the
two input findings that make a capture repeatable. B048 is struck as **code
fixed, live pair owed** rather than closed.

### Step results

| step | result |
|---|---|
| `cmake --build build -j24` | exit **0** |
| `ctest --test-dir build -j6 --output-on-failure` | exit 8 — **3763/3764**, sole failure `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA one) |
| `scripts/build-gles2-x86.sh` | exit **0** |
| `scripts/ci/visual_gate.sh` standalone | **PASS**; `git status --porcelain` shows no golden |
| `scripts/build-arm-noble.sh` | exit **0** |
| `scripts/refactor/check_file_sizes.sh --all` | exit 0 — 867 files, **867 ok, 0 warn, 0 fail** |
| new test, pre-fix | **FAILS** (transcript above) |
| new test, post-fix | **4/4 pass** |
| live ON/OFF pair | **OWED** — eqemu lock held by T332 |

STOPs: none fired.
