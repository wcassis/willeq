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

## Review — T335b

Branch `task/T335b-frustum-site`, from `fable` at `6c70490a` (T335's branch
merged). This closes the site T335 STOPped on, under the orchestrator's ruling
that the G05 golden pins a SEQUENCE and learns a reviewed edit **in place at the
positions it touches**, exactly as T337 taught the movement golden T332's
insertion. Never regenerated from the tip; no pixel golden re-blessed (none
changes).

### The production edit — one statement, count-neutral

`src/client/graphics/frame/renderer_frame_render.cpp`, inside the existing
`frustumDebugDraw_` block in `FrameComponent::renderDebugOverlays()`. No new
pass, no pass moved, no statement moved between passes:

```
-        ctx_.driver->setMaterial(debugMat);
+        WorldLineBatch batch(ctx_, debugMat);
         ctx_.driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
```

Plus the `#include` (a preprocessor line — `keep()` drops it) and an in-place
comment (dropped too). That is the whole cost: the guard **replaces** the
statement it supersedes rather than sitting beside it, and its restore is a
destructor, which costs no statement at all. So the composed statement list
changes by **one entry's text and nothing else** — count still 305, order
untouched.

The guard is declared in the `if` block's scope, so its destructor runs at that
block's closing brace: after the last `draw3DLine()` of the last region box and
before the map overlay two statements later. `debugMat` is unchanged
(`ZBuffer = ECFN_ALWAYS`, `ZWriteEnable = false`, thickness 2) — the batch
applies exactly the material the block always asked for, then hands the 2D-safe
state back.

### The golden learns it IN PLACE

`tests/goldens/frame_render_order_g05.txt`, **one line changed**, at its
position:

```
-127 ctx_.driver->setMaterial(debugMat);
+127 WorldLineBatch batch(ctx_, debugMat);
```

Entries 120–126 and 128 (the rest of the block) are byte-identical, every other
entry is byte-identical, and the file is still 305 lines. `git diff --stat` on
the golden is `1 +, 1 -`.

`tests/test_frame_render_order.cpp`: the bare `305u` literal became a named
`kGoldenSize` carrying the T337 documentation shape — a comment block that says
the golden is the pre-G05 sequence PLUS the reviewed edits listed beneath it,
each named by position, and that it is never regenerated. The first (and so far
only) entry reads *"T335b: WorldLineBatch guard in the frustum block, entries
120–128"*, and records that entry 127 changed text while the count stayed 305
and why. `GoldenIsPresentAndTheRightSize`'s failure message was rewritten to
match: it no longer says "is not re-blessed", it says a genuine change is taught
IN PLACE at the positions it touches and named there, and is never REGENERATED
from the tip. The parser, the normaliser and all four cases are otherwise
untouched.

### Re-plant — both faults still caught

The TU was copied aside first; after both restores `diff` against the copy
reports **IDENTICAL** and `git status --porcelain` lists the TU only for this
unit's intended edit. No `git checkout --`, no stash.

| plant | outcome |
|---|---|
| **drop the guard line** — delete `WorldLineBatch batch(ctx_, debugMat);`, which is the real regression this unit exists to prevent (the overlay goes back to drawing nothing and wiping the UI) | **FAILS**, exit 1: `draw statement count changed: 305 -> 304` (`composed.size()` 304 vs `golden.size()` 305). 3 passed, 1 failed |
| **swap two pass calls** — `renderWeatherEffects();` ⇄ `renderDebugOverlays(deltaTime);` in `processFrameRender()`, no pass body edited | **FAILS**, exit 1: `draw order diverges at statement 116 — want: LOG_TRACE(... "POST-DRAW checkpoint 7: weather"); got: if (ctx_.PlayerCamera().playerConfig().collisionDebug) ...` — the debug-overlay pass now sits where weather did. 3 passed, 1 failed |
| restore both | **4/4 PASSED** |

So teaching the golden one entry did not blunt it: the count clause catches a
dropped statement and the position clause catches a reordered pass.

### The pixel test — `tests/test_frustum_debug_draw.cpp`

Same harness and frame shape as the other three (real headless GLES2 device,
framebuffer readback, `beginScene -> a 2D draw -> the pass -> the 2D UI ->
endScene`), four cases: the two symptoms, the transform-restore contract, and
the guards-are-unchanged no-op case.

**The seam is three inline methods on `FrameComponent`**, and inline is
deliberate: the frustum block is not a function and cannot be made one (the
golden pins its statements), so the test drives the whole pass — and a seam
DEFINED in `renderer_frame_render.cpp` would be parsed as a `FrameComponent`
body by `test_frame_render_order.cpp` and reported as an orphaned pass. So
`frustumDebugDraw()`, `seedFrustumDebugBoxForTest(regionIdx, eqBbox)`,
`frustumDebugBoxCount()` and `renderDebugOverlaysForTest(deltaTime)` all live in
the header. Every other overlay in the pass is left at its default OFF
(`collisionDebug`, `showMapOverlay`, `showNavmeshOverlay`, `stencilDebugDraw`),
so the only geometry that can reach the framebuffer is the frustum wireframe.

One region box, EQ `(-0.8,-0.8,-0.8)..(0.8,0.8,0.8)`, with identity captured
transforms; the block's `EQ(x,y,z) -> Irr(x,z,y)` mapping puts its two drawn
faces on one screen-space square with edges at x=80/720 and y=60/540. A
default-constructed `FrustumCuller` holds six zeroed planes, so `testAABB()`
reports the box inside and it takes the GREEN "rendered" branch — the branch a
live Ctrl+V session shows most of.

Pre-fix run (`WorldLineBatch` neutered to `setMaterial()`-only with an empty
destructor, which is the pre-T333 code shape), exit 1 — **3 of 4 fail**:

```
[ RUN      ] FrustumDebugDrawTest.RegionBoundingBoxEdgesReachTheFramebuffer
tests/test_frustum_debug_draw.cpp:153: Failure
Expected: ((int)c.getGreen()) > (100), actual: 0 vs 100
bottom edge of the frustum debug box drew nothing at (400,540) -- got 0,0,0
... left edge (80,300) -- got 0,0,0
... right edge (720,300) -- got 0,0,0
[  FAILED  ] FrustumDebugDrawTest.RegionBoundingBoxEdgesReachTheFramebuffer (177 ms)
[ RUN      ] FrustumDebugDrawTest.TheTwoDUIDrawnAfterTheOverlayStillAppears
tests/test_frustum_debug_draw.cpp:169: Failure
Expected: ((int)c.getRed()) > (200), actual: 0 vs 200
the 2D UI drawn after the frustum debug block was wiped -- got 0,0,0
[  FAILED  ] FrustumDebugDrawTest.TheTwoDUIDrawnAfterTheOverlayStillAppears (68 ms)
[ RUN      ] FrustumDebugDrawTest.TheToggleAndTheEmptyBoxListAreStillNoOps
tests/test_frustum_debug_draw.cpp:232: Failure
Expected: ((int)c.getRed()) > (200), actual: 0 vs 200
[  FAILED  ] FrustumDebugDrawTest.TheToggleAndTheEmptyBoxListAreStillNoOps (60 ms)
```

That third failure was **not predicted and is worth recording**: the material
setup sits BEFORE the `for` loop over `regionBoundingBoxes`, so with the toggle
on and an EMPTY box list the block drew no line yet still installed a 3D
material — and pre-fix that alone wiped the UI. A live user who pressed Ctrl+V
outside a loaded zone lost the UI and saw nothing appear, with no way to
connect the two. The test comment now says so.

Post-fix: **4/4 pass**. Registered in `tests/cmake/renderer_guards.cmake` beside
its three siblings; self-skips without a usable GLES2 device; needs no EQE1
asset and no server.

### Step results

| step | result |
|---|---|
| `cmake --build build -j24` | exit **0**. One earlier build hit **exit 143** (the 10-minute tool ceiling, not a compiler fault); the protocol sweep ran — 9 zero-byte files under `build/bin` deleted, `ar t` gave `libwilleq_core.a` 321 members and `libEtcLib.a` 15, both intact — and the resumed foreground build finished exit 0 |
| `ctest --test-dir build -j6 --output-on-failure` | exit 8 — **3787/3788**, sole failure `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA one). +16 cases over T335's 3772: the four new frustum cases and the discovery split |
| `test_frame_render_order` | **4/4**; both re-plants fail as required, restore returns 4/4 |
| `test_frustum_debug_draw` (new) | pre-fix **3 of 4 FAIL**; post-fix **4/4** |
| `test_navmesh_overlay_draw` / `test_map_overlay_draw` / `test_collision_debug_draw` | **4/4 each**, unchanged |
| `scripts/build-gles2-x86.sh` | exit **0** |
| `scripts/ci/visual_gate.sh` standalone | **PASS**. The only golden in `git status --porcelain` is `frame_render_order_g05.txt` — this unit's deliberate one-line, in-place edit. **No pixel golden touched**: all four overlays are off in the gate |
| `scripts/build-arm-noble.sh` | exit **0** |
| `scripts/refactor/check_file_sizes.sh --all` | exit 0 — 873 files, **873 ok, 0 warn, 0 fail** |

STOPs: **none fired.** No pass was reordered and no pass was added; the
statement sequence changed by exactly one entry's text, which the golden now
records at its position. B054 is fully closed.
