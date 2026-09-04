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

## Review

Branch `task/T335-debug-overlay-siblings`, from `fable` at `a61d52bb`.

### The helper

`include/client/graphics/world_line_batch.h` — `EQT::Graphics::WorldLineBatch`,
a header-only RAII guard constructed from `RendererContext&` and the caller's
line material. It is the ONE place T333's fix is spelled; the measurement (the
`COpenGLES2Driver::setOrthoProjection()` `Is2DMode` latch, and why the restore
material's TYPE is load-bearing) moved into its header comment.

Constructor: saves `ETS_VIEW`/`ETS_PROJECTION`/`ETS_WORLD`, calls
`setMaterial(lineMaterial)`, then installs `ctx.captured3DView` /
`ctx.captured3DProj` gated on `ctx.Frame().have3DTransforms()` —
`drawTargetOutline()`'s idiom, plus a null check on `ctx.frameComponent` so a
bare, never-`Init()`'d renderer (the test idiom) cannot deref a null component.

Destructor: restores the three transforms literally, then re-establishes the
2D-safe render state through the only public door to
`setCull()`/`setDepthTest()`/`setDepthWrite()` — `setMaterial()` with
`BackfaceCulling = false`, `ZBuffer = ECFN_NEVER`, `ZWriteEnable = false`,
`MaterialType = EMT_TRANSPARENT_ALPHA_CHANNEL`. The type is the load-bearing
part: `applyMaterialRenderState()` switches on `MaterialType` for the depth
write and ignores `ZWriteEnable`, and this type's branch is the depth-write-off
one. Non-copyable. Because the restore is a destructor, an early return out of
a draw body can no longer skip it — which the hand-written T333 version could.

Four call sites were in scope. **Three now use it; the fourth is STOPped, below.**

### Site 1 — `OverlaysComponent::drawNavmeshOverlay()` (already fixed by T333)

Refactored onto the guard: the three `savedView/Proj/World` locals, the
`have3DTransforms()` block and the whole restore tail were deleted and replaced
by `WorldLineBatch batch(ctx_, lineMaterial);`. Behaviour identical — same
order of operations, same restore material.

Pre-fix failing run: reproduced by neutering `WorldLineBatch` (constructor
`setMaterial()` only, empty destructor), which is exactly the pre-T333 code
shape, and rebuilding. `build/bin/test_navmesh_overlay_draw`, exit 1:

```
[ RUN      ] NavmeshOverlayDrawTest.NavmeshTrianglesReachTheFramebuffer
tests/test_navmesh_overlay_draw.cpp:180: Failure
Expected: ((int)c.getGreen()) > (100), actual: 0 vs 100
bottom edge of the navmesh triangle drew nothing at (400,540) -- got 0,0,0
... left edge (240,300) -- got 0,0,0
... right edge (560,300) -- got 0,0,0
[  FAILED  ] NavmeshOverlayDrawTest.NavmeshTrianglesReachTheFramebuffer (187 ms)
[ RUN      ] NavmeshOverlayDrawTest.TheTwoDUIDrawnAfterTheOverlayStillAppears
tests/test_navmesh_overlay_draw.cpp:198: Failure
Expected: ((int)c.getRed()) > (200), actual: 0 vs 200
the 2D UI drawn after drawNavmeshOverlay() was wiped -- got 0,0,0
[  FAILED  ] NavmeshOverlayDrawTest.TheTwoDUIDrawnAfterTheOverlayStillAppears (68 ms)
```

Post-fix: **4/4 pass**, unchanged from T333.

### Site 2 — `OverlaysComponent::drawMapOverlay()` (Ctrl+M)

`WorldLineBatch batch(ctx_, lineMaterial);` replaces the bare
`setMaterial(lineMaterial)`. Both `setTransform(ETS_WORLD, ...)` calls (terrain
identity, then the placeable rotate/mirror transform) are untouched — the guard
saved the world transform before either ran.

New test seam, mirroring T333's:
`OverlaysComponent::seedMapOverlayTriangleForTest(v1, v2, v3, colour, isPlaceable)`
plus `mapOverlayTriangleCount()`. `drawMapOverlay()`'s only production filler is
`updateMapOverlay()`, which needs a loaded zone's collision mesh; the pixel test
has a real GLES2 device but no zone.

New test `tests/test_map_overlay_draw.cpp` — four cases in
`test_navmesh_overlay_draw.cpp`'s exact shape (the two symptoms, the
transform-restore contract, the empty-list no-op). Pre-fix, exit 1:

```
[  FAILED  ] MapOverlayDrawTest.MapTrianglesReachTheFramebuffer
tests/test_map_overlay_draw.cpp:145: Failure
Expected: ((int)c.getGreen()) > (100), actual: 0 vs 100
bottom edge of the map overlay triangle drew nothing at (400,540) -- got 0,0,0
... left edge (240,300) -- got 0,0,0
... right edge (560,300) -- got 0,0,0
[  FAILED  ] MapOverlayDrawTest.TheTwoDUIDrawnAfterTheOverlayStillAppears
tests/test_map_overlay_draw.cpp:161: Failure
Expected: ((int)c.getRed()) > (200), actual: 0 vs 200
the 2D UI drawn after drawMapOverlay() was wiped -- got 0,0,0
```

Post-fix: **4/4 pass**. So Ctrl+M drew nothing and wiped the UI, exactly as
B054 predicted from the code, and now does neither.

### Site 3 — `PlayerCameraComponent::drawCollisionDebugLines()` (`collisionDebug`)

Territory note: the board and this contract disagreed on the file. The function
lives in `src/client/graphics/player/renderer_player_collision.cpp` (moved there
by J33), not `renderer_camera.cpp`; the board entry is right and the contract's
header line is stale. `renderer_camera.cpp` was NOT touched — slot3 owns it this
round.

`WorldLineBatch batch(ctx_, lineMaterial);` replaces the bare
`setMaterial(lineMaterial)`; the `setTransform(ETS_WORLD, matrix4())` that
followed is unchanged. The line ageing/erasure loop is untouched.

New test seam `PlayerCameraComponent::seedCollisionDebugLineForTest(...)` plus
`collisionDebugLineCount()` — the production filler `addCollisionDebugLine()` is
private and is reached only from the movement-collision path, which needs a
loaded zone's collision map.

New test `tests/test_collision_debug_draw.cpp`, same four cases; the empty-list
case additionally pins that expiry still culls a line. Pre-fix, exit 1:

```
[  FAILED  ] CollisionDebugDrawTest.CollisionDebugLinesReachTheFramebuffer
tests/test_collision_debug_draw.cpp:148: Failure
Expected: ((int)c.getGreen()) > (100), actual: 0 vs 100
bottom line of the collision debug triangle drew nothing at (400,540) -- got 0,0,0
... left line (240,300) -- got 0,0,0
... right line (560,300) -- got 0,0,0
[  FAILED  ] CollisionDebugDrawTest.TheTwoDUIDrawnAfterTheOverlayStillAppears
tests/test_collision_debug_draw.cpp:164: Failure
Expected: ((int)c.getRed()) > (200), actual: 0 vs 200
the 2D UI drawn after drawCollisionDebugLines() was wiped -- got 0,0,0
```

Post-fix: **4/4 pass**.

Both new binaries are registered in `tests/cmake/renderer_guards.cmake` beside
`test_navmesh_overlay_draw`, self-skip without a usable GLES2 device, and need
no EQE1 asset and no server.

### Site 4 — the `frustumDebugDraw_` block — **STOPPED, and why**

`renderer_frame_render.cpp` was NOT modified. The contract's golden question was
checked before any edit was attempted, and the answer is that **no fix to this
block is possible without altering what the golden parses.**

`tests/test_frame_render_order.cpp` does not compare a pass *ordering*. It text-
parses every `void|bool FrameComponent::` body in that TU, expands
`processFrameRender()` recursively into a flat list of NORMALISED STATEMENT
STRINGS, and then:

* `GoldenIsPresentAndTheRightSize` asserts the list is exactly **305** entries;
* `ComposedDrawOrderMatchesThePreG05Golden` compares it to
  `tests/goldens/frame_render_order_g05.txt` **position by position, by exact
  text**;
* `EveryPassIsCalledExactlyOnceAndNoneIsOrphaned` pins the pass count at
  exactly **16**.

The frustum block's statements are in the golden verbatim, entries 120–128:

```
120 if (frustumDebugDraw_ && ctx_.frustumCuller) {
121 irr::video::SMaterial debugMat;
...
127 ctx_.driver->setMaterial(debugMat);
128 ctx_.driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
```

`keep()` drops only blank lines, `//`-comment lines, preprocessor lines and
brace-only lines. A `WorldLineBatch` declaration is a statement, so **adding**
one changes the count (305 → 306). **Replacing** `setMaterial(debugMat);` with
it keeps the count but changes entry 127's text, which case 4 compares exactly.
Moving the block into a new `FrameComponent` method keeps the composed text
(the call expands) but makes the pass count 17. Moving it to another TU deletes
its statements from the composed list entirely. There is no fourth route: the
golden pins the block's source text, not just its position.

So the STOP fired, on the ground the orchestrator named in advance. The frustum
debug draw still carries the defect and B054 stays OPEN for that one site with
the blocker recorded. It needs a decision that is not this unit's to make:
either re-bless `frame_render_order_g05.txt` (which its own failure message
forbids — "it is a record of the PRE-G05 draw sequence and is not re-blessed to
accommodate a code change"), or narrow the golden so it pins the pass ORDER
rather than every statement's text. **AWAITING WILL.** Note the cost is bounded
— the frustum overlay is off by default and has no live evidence — but the
golden as written also forbids any future correctness fix anywhere in
`renderer_frame_render.cpp`, which is the larger finding here.

### Golden-parse confirmation

`renderer_frame_render.cpp` is byte-identical to `fable`; `git status
--porcelain` never listed it. `build/bin/test_frame_render_order` passes 4/4,
and `tests/goldens/frame_render_order_g05.txt` is untouched. No pass was
reordered.

### Step results

| step | result |
|---|---|
| `cmake --build build -j24` | exit **0** |
| `ctest --test-dir build -j6 --output-on-failure` | exit 8 — **3771/3772**, sole failure `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA one) |
| `test_navmesh_overlay_draw` | **4/4**, unchanged |
| `test_map_overlay_draw` (new) | pre-fix **2 of 4 FAIL**; post-fix **4/4** |
| `test_collision_debug_draw` (new) | pre-fix **2 of 4 FAIL**; post-fix **4/4** |
| `test_frame_render_order` | **4/4**, golden byte-identical |
| `scripts/build-gles2-x86.sh` | exit **0** |
| `scripts/ci/visual_gate.sh` standalone | **PASS**; `git status --porcelain` shows no golden |
| `scripts/build-arm-noble.sh` | exit **0** |
| `scripts/refactor/check_file_sizes.sh --all` | exit 0 — 870 files, **870 ok, 0 warn, 0 fail** |

STOPs: **one fired** — the `frustumDebugDraw_` site, blocked by
`frame_render_order_g05.txt` (above). No pass reorder was attempted. No site
was found to run outside the latched-2D window: all four are in
`FrameComponent::renderDebugOverlays()`, after `smgr->drawAll()`.
