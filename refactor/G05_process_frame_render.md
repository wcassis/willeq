# G05 — `processFrameRender`: the render passes become one private pass per method

Batch: `refactor/G_oversize_methods.md` (corrections 1–17). **Dispatch
after L13 merges** (L13 rewrites receivers inside `src/client/graphics/frame/`);
two-wide with G04 is fine once it does. Measured at `dfa3738f`:
`FrameComponent::processFrameRender` in `src/client/graphics/frame/renderer_frame_render.cpp`
(~546 lines, sequential render passes with `measureSection` timing
brackets). Re-measure mechanically: list the passes the timing brackets
and banners already delimit.

## Shape

1. Each pass becomes `void FrameComponent::renderX()` (sky, zone geometry,
   entities, effects, overlays, HUD, RDP capture, …) — verbatim slice
   INCLUDING its `measureSection` bracket, so the timing categories are
   unchanged; `processFrameRender` becomes the ordered list. Locals that
   cross passes (the frame's camera/frustum/visibility results) become a
   `FrameRenderState` struct passed by reference or members of the
   component — record each (STOP on a lifetime change; STOP on anything
   that touches the render/game thread boundary).
2. Order is load-bearing (depth, blending, overlay-on-top): an order test
   parses the TU for the pass-call sequence against a golden extracted
   BEFORE the change; size asserted; faults planted in code (swap two
   pass calls; drop one).
3. ⚠ Text readers: `tests/test_ui_hide_command.cpp` and
   `tests/test_static_ui_scroll.cpp` read this TU by path and locate
   bodies by spelling (L11 widened them) — re-read them; if they locate
   `processFrameRender`'s body or a pass inside it, widen + re-prove
   (correction 21); `test_debug_hotkey_arms` reads the sibling input TU.
4. Proof: line multiset; the driving tests pass with assertions
   untouched; **the visual gate standalone is the behavioural proof**
   (goldens untouched).

Predicted: `processFrameRender` 546 → ≈ 25–35 lines; `frame_component.h`
gains ≈ 12 declarations.

STOP: a cross-pass local whose lifetime changes; a thread-boundary
change; the visual gate diffs a golden; a pinned test's assertion.

Territory: `src/client/graphics/frame/renderer_frame_render.cpp`,
`include/client/graphics/frame/frame_component.h`, the new test + golden
+ cmake, the two text-reader tests (widening only), `docs/module_map.md`.
Graphics files change: visual gate + ARM NOT skippable.

## Review

Executed 2026-09-04 in `wt/slot2`, branch `task/G05-process-frame-render` off
`fable` at `c1917296`. Structural, move-only, zero behaviour change.
**No STOP condition hit.**

### Correction 22 first: the exit census, with loop depth

Before choosing "passes", every `return`/`continue`/`break` in the body was
enumerated mechanically with its enclosing loop depth (G07's rule):

| line | loop depth | exit |
|---|---|---|
| 54 | 0 | `return false;  // Signal loading screen was shown` |
| 424 | 1 | `if (n < 3) continue;` — the portal vertex-count cull |
| 430 | 1 | `if (dx*dx + dy*dy + dz*dz > maxDistSq) continue;` — portal distance cull |
| 594 | 0 | `return true;` |

**Four exits, and the shape is a PIPELINE, not a search.** Both `continue`s sit
at loop depth 1 inside the portal-debug `for (size_t pi ...)` at line 421, which
lives wholly inside one region and never escapes it; neither is an exit from the
method. The only early `return` is the *first* statement region of the body (the
loading-screen guard, before `beginScene()`), so it stays in the driver and no
pass carries a bool. G07's STOP does not apply: no bool phase list was needed
and none was introduced.

### Re-measurement at the tip (correction 1: mechanical, not the plan's numbers)

The plan said ~546 lines. At `c1917296` `FrameComponent::processFrameRender`
spans **lines 50-595 of a 598-line
`src/client/graphics/frame/renderer_frame_render.cpp`** — signature 50, closing
brace 595, body **544 lines**. The file's own timing brackets delimit the passes:
**thirteen `frameTimings_.X = measureSection();` assignments** at lines 108, 133,
196, 258, 276, 281, 481, 486, 495, 544, 549, 573, 590.

### The cut: sixteen passes, at the measureSection brackets

**The bracket wins over the banner where they disagree.** This is the one place
G05 departs from G04's "take the file's banners": a banner cut that put the fire
glow (a `// Fire glow:` banner, lines 260-271) in the particles pass would have
moved it out of the `frameTimings_.boids` bucket it has always been measured in,
because it sits *after* line 258's `particles` assignment. Every pass therefore
ends at a `measureSection()` line, and the three passes with no bracket
(`runSceneProfileIfScheduled`, `collectScenePassTimings`,
`checkFrameBudgetsAndLogStats`) are the regions between brackets. **All thirteen
timing categories span byte-for-byte the code they spanned before.**

| # | pass | orig lines | len | timing bucket it closes |
|---|---|---|---|---|
| — | `processFrameRender` (driver) | 51-55, 592-594 | 26 body | — |
| 1 | `runSceneProfileIfScheduled` | 57-64 | 8 | (none) |
| 2 | `beginSceneAndDrawWorld(float)` | 66-108 | 43 | `sceneDrawAll` |
| 3 | `collectScenePassTimings` | 110-122 | 13 | (reads renderPassTimer) |
| 4 | `renderFootprints` | 124-133 | 10 | `footprintRender` |
| 5 | `checkFrameBudgetsAndLogStats` | 135-187 | 53 | (none) |
| 6 | `renderTargetIndicator` | 189-196 | 8 | `targetBox` |
| 7 | `renderParticleEffects` | 198-258 | 61 | `particles` |
| 8 | `renderFireGlowAndBoids(float)` | 260-276 | 17 | `boids` |
| 9 | `renderWeatherEffects` | 278-281 | 4 | `weatherRender` |
| 10 | `renderDebugOverlays(float)` | 283-481 | 199 | `debugOverlays` |
| 11 | `renderEntityCastingBars` | 483-486 | 4 | `castingBars` |
| 12 | `renderLegacyGui` | 488-495 | 8 | `guiDrawAll` |
| 13 | `renderStaticUI` | 497-544 | 48 | `staticUI` |
| 14 | `renderZoneLineOverlays` | 546-549 | 4 | `zoneLineOverlay` |
| 15 | `renderFrameTail` | 550-573 | 24 | `postRender` |
| 16 | `endSceneAndSwap` | 575-590 | 16 | `endScene` |

- **`processFrameRender`: 546 lines -> 28** (signature, the 3-line loading-screen
  guard with its `return false;`, 16 calls, the checkpoint-12 `LOG_TRACE`,
  `FlushThreadLog();`, `return true;`, braces and blanks). Predicted 25-35: hit.
- `renderer_frame_render.cpp`: 598 -> **667** (+69, every line attributed below).
  `frame_component.h`: 308 -> **336** (+28: 16 declarations, a 12-line comment
  block). Predicted "≈ 12 declarations" — **16**, because the file has thirteen
  timing brackets plus three bracket-less regions. `check_file_sizes.sh --all`:
  865 ok, 0 warn, 0 fail.
- **All nine conditional regions still live wholly inside one pass**, none
  straddles a cut: `__linux__` 172-186 in pass 5; `EQT_HAS_GLES2` 191-195 in
  pass 6, 243-257 in pass 7, 397-453 and 458-479 in pass 10; `EQT_HAS_DRM`
  261-271 in pass 8; `WITH_RDP` 552-554 in pass 15.

### Cross-pass locals (the unit's headline STOP condition)

Enumerated mechanically — every declaration in the pre-change body with the line
of its last use — **before** any struct was designed (correction 19):

| local(s) | declared | last use | crosses a cut? |
|---|---|---|---|
| `clearColor` | 67 | 72 | no (pass 2) |
| `weatherMult` | 88 | 95 | no (pass 2) |
| `visibleEntities`, `totalEntities`, `tmuUsed/tmuLimit`, `hitRate`, `evictions`, `fbiUsed`, `fbiLimit` | 147-159 | 169 | no (pass 5) |
| `f`, `vm`, `rss`, `ps` | 175-179 | 183 | no (pass 5) |
| `fogStart`, `fogEnd`, `fogCol`, `screenH` | 248-251 | 254 | no (pass 7) |
| `debugMat` | 293 | 299 | no (pass 10) |
| `pvsRegion` | 303 | 306 | no (pass 10) |
| `camX/camY/camZ` | 309 | 331 | no (pass 10) |
| `color`, `pvsVisible`, `closestX/Y/Z`, `ddx/ddy/ddz`, `dist`, `inFrustum`, `corners` | 313-354 | 378 | no (pass 10) |
| `portalData`, `camEqX/Y/Z`, `maxDistSq`, `lineBuf`, `lineCount`, `pushLine`, `portal`, `n`, `dx/dy/dz`, `isAdjacentToCamera`, `r/g/b`, `vj` | 398-439 | 451 | no (pass 10) |
| `levelColors`, `fullScreen` | 460-472 | 475 | no (pass 10) |
| `cursor`, `pos`, `destPos`, `srcRect` | 558-562 | 563 | no (pass 15) |

**Not one local crosses a cut.** So there is **no `FrameRenderState` struct**,
nothing became a member, nothing became a parameter, and **no lifetime
changed** — the plan's headline STOP condition is discharged mechanically, the
same way G04's was. The three passes that need the frame delta take it as
`float deltaTime` (passes 2, 8, 10 — `windTime_ +=`, `fireGlowTime_ +=`,
`drawCollisionDebugLines(deltaTime)`); everything else the passes share was
already a member (`frameTimings_`, `sectionStart_`, `have3DTransforms_`,
`lastPolygonCount_`, `allUIHidden_`, the panel states) or reached through
`ctx_`, exactly as before.

**No thread-boundary change.** All sixteen passes are private, called only from
`processFrameRender()`, which runs on the render thread as it always did. No
synchronisation primitive, queue, buffer lifetime or thread ownership is
touched; `ctx_.particleRenderBuffer` and the `eqtGPUPhaseBegin/End` pairs move
verbatim inside their passes, and the two GPU phases still open and close in the
same order (`ParticlesUI` opens in pass 7, closes in pass 15; `Swap` opens and
closes in pass 16).

### Proof (a): the line multiset

`git show fable:...renderer_frame_render.cpp` vs the new file, every line
`strip()`ed, `Counter` difference both ways:

```
REMOVED: 5 lines   <- ALL FIVE ARE COMMENT LINES IN THE FILE HEADER
    4  the stale "processFrameRender is 546 lines ... J31 deliberately did NOT
       touch (move-only). All nine of this file's conditional regions ... live
       wholly inside it." block, rewritten for the new shape
    1  the last line of the test_ui_hide_command WARNING, extended in place
ADDED:   74 lines
   16  }                                          (one per new pass)
   16  void FrameComponent::renderX(...) {        (one per new pass)
   16  renderX();                                 (the driver's ordered list)
   22  the G05 banner + the extended WARNING + "// The ordered list."
    2  //  (comment separators)
    2  (blank separators)
```

**Zero CODE lines removed, in either direction.** Every one of the 544 original
body lines is present, verbatim, exactly once. Residual outside those buckets:
**0**.

### Proof (b): the order golden and the eight planted faults

`tests/goldens/frame_render_order_g05.txt`: **305 entries**,
`<ordinal> <normalised source line>`, extracted mechanically from
`git show fable:src/client/graphics/frame/renderer_frame_render.cpp` at
`c1917296` — **from the pre-change code, before a line of it was touched**.
Normalisation (mirrored exactly in the test): strip whitespace; drop blank,
`//`-comment, preprocessor and brace-only lines.

**Why a text parse and not a runtime observation** (G03 correction 9): *there is
no container*, and `processFrameRender()` cannot be run in a unit test at all —
it needs a live Irrlicht device, a GL context, a scene manager, a loaded zone and
a driver that can `beginScene()`/`endScene()`. The TU **is** the order.

`tests/test_frame_render_order.cpp` (registered in `tests/cmake/core_utils.cmake`;
no link deps, `WILLEQ_REPO_ROOT` is global from `tests/CMakeLists.txt`) parses
every `void|bool FrameComponent::<name>(` at column 0, then **composes**: walk
`processFrameRender()`'s body in order; a line that is exactly `renderX();` or
`renderX(deltaTime);` naming a function defined in the TU expands *recursively*
to that function's normalised body; every other line is emitted as itself. Four
cases:

1. `GoldenIsPresentAndTheRightSize` — non-empty and **== 305**, in its own case
   (correction 5 / K11 ee).
2. `EveryPassIsCalledExactlyOnceAndNoneIsOrphaned` — 16 calls, no pass called
   twice, no `FrameComponent::` method defined in the TU left unreached.
3. `ProcessFrameRenderIsOnlyTheOrderedList` — the anti-regrowth clause
   (G04 correction 16): the driver may contain only pass calls, the three
   loading-screen guard lines, the checkpoint-12 `LOG_TRACE`, `FlushThreadLog();`
   and `return true;`, and fewer than 40 statements.
4. `ComposedDrawOrderMatchesThePreG05Golden` — position by position; the failure
   message prints the three statements either side of the divergence in both
   lists.

**Eight plants, all in the CODE (except the empty-golden trap), eight catches;
source and golden byte-identical afterwards (`diff` clean both files):**

| plant | outcome |
|---|---|
| **the call-order fault** (correction 12) — swap `renderLegacyGui` ⇄ `renderStaticUI` in the ordered list; no pass body edited at all. This is the real regression: the old `guienv_` draw and the FPS counter would paint *under* the static panels instead of over them | **FAILS**: `draw order diverges at statement 246` |
| **drop a pass call** — delete `renderWeatherEffects();` | **FAILS** ×3: `expected sixteen render passes`, `renderWeatherEffects() is defined ... but never called`, `draw statement count changed: 305 -> 302` |
| **delete a real statement** inside a pass — the boids `render()` line | **FAILS**: `305 -> 304` |
| **add an invented statement** inside a pass — `lastPolygonCount_ = 0;` after `drawZoneLineBoxLabels()` | **FAILS**: `305 -> 306` |
| **move a statement across a cut** (G04 correction 20) — hoist `ctx_.driver->endScene();` out of `endSceneAndSwap` to the top of `renderFrameTail`, i.e. swap *before* the cursor, screenshot and RDP capture. Count unchanged, only the position moves | **FAILS**: `draw order diverges at statement 282` |
| (the empty-file trap) truncate the golden to 0 bytes | **FAILS** ×2 including case 1 — nothing passes vacuously |
| **the text guard, reachability half** — delete `renderStaticUI();` from the driver, leaving the guarded block spelled correctly in a pass nothing calls | **FAILS** in `test_ui_hide_command`: `the static UI pass (renderStaticUI()) is defined but never called from processFrameRender()` |
| **the text guard, spelling half** — drop `&& !allUIHidden_` from the static-UI guard | **FAILS** in `test_ui_hide_command`: `the static UI panels are drawn without consulting allUIHidden_` |

### Text-reader check (corrections 7 and 13: run the grep even when the plan's list looks complete)

`grep` for `renderer_frame_render` / `processFrameRender` / `frame_component`
across `tests/`, `scripts/`, `tools/` and `docs/`. The plan named three tests;
the grep found **one** that reads THIS TU, two that read the *sibling* input TU,
and four prose mentions:

| file | reads | disposition |
|---|---|---|
| `tests/test_ui_hide_command.cpp:164` | **this TU**, by path, for the static-UI guard spelling | **WIDENED** (below) |
| `tests/test_static_ui_scroll.cpp:703` | `renderer_frame_input.cpp` only | untouched — not this unit's TU |
| `tests/test_debug_hotkey_arms.cpp:145` | `renderer_frame_input.cpp` only | untouched |
| `tests/test_player_camera_component.cpp:40`, `tests/test_zone_lifecycle_component.cpp:147` | prose comments naming the input TU | untouched |
| `tests/test_frame_component.cpp`, `tests/cmake/renderer_guards.cmake` | include/build the header; drive phase-1 input, never phase 4 | untouched, **9 cases green, zero assertion edits** |
| `docs/module_map.md:195,218,219` | lists all three TUs | row 219 updated |

**The widening (correction 21, fourth sighting; correction 16 — name the matcher
that would have weakened it).** `UIHideSourceInvariants.StaticUiBlockIsGatedOnAllUIHidden`
matched `if (newUIEnabled_ && uiRenderer_ && !allUIHidden_)` anywhere in the
file. The block did not change spelling and did not change file, so that matcher
**still passes** — which is exactly the problem: after G05 the block is inside
`FrameComponent::renderStaticUI()`, and *a pass nothing calls still spells the
condition*. The old matcher would have gone green over a dead static-UI pass and
the whole `/hideui` leg with it. The test now also resolves the enclosing
`void FrameComponent::<name>(` above the guard and asserts that
`    <name>();` appears in the file — i.e. that the driver calls it. Both halves
were proved by planting the break each one exists to catch (rows 7 and 8 above);
neither subsumes the other.

### Verification

| step | result |
|---|---|
| `cmake --build wt/slot2/build -j24` | **EXIT 0**, first attempt, no exit-143 (no post-143 sweep needed) |
| `ctest --test-dir wt/slot2/build -j6 --output-on-failure` | **3755/3756 passed**; sole failure the known host quirk `AudioManagerTest.PreloadCommonSounds`. 3756 tests (was 3752 — the 4 new order cases) |
| `bash scripts/build-gles2-x86.sh` | **EXIT 0** |
| **visual gate standalone** (`bash scripts/ci/visual_gate.sh`) | **EXIT 0, `=== VISUAL GATE: PASS ===`** — qeynos2, gfaydark, befallen verified plus the three memory-coverage runs. `git status --porcelain` afterwards lists **no golden**: only the six files this unit owns. **This is the behavioural proof: the frame those three zones paint is pixel-identical to the pre-split frame.** |
| `bash scripts/build-arm-noble.sh` | **EXIT 0** |
| `check_file_sizes.sh --all` | **checked 865 files: 865 ok, 0 warn, 0 fail** |
| hygiene re-run after the `docs/module_map.md` edit | `FileSizeCeiling` + `DocCitations` **2/2 passed** |
| line multiset | 5 removed (all five comment lines in the file header), 74 added, every line attributed; **zero code lines removed**; residual **0** |
| planted faults | 7 plants in the code + the empty-golden trap, **8/8 caught**; TU and golden restored byte-identical |

### Deviations from the plan

1. **Sixteen passes, not "≈ 12"** — the count is the file's thirteen
   `measureSection()` brackets plus the three bracket-less regions between them
   (scene profile, the renderPassTimer readback, the polygon-budget/stats block).
   No pass was invented and none was merged.
2. **The cut points are the timing brackets, not the banners** (G04's rule 14
   inverted for this file). Where the two disagree the bracket wins, because the
   plan's own constraint — "verbatim slices INCLUDING their `measureSection`
   brackets, so the timing categories are unchanged" — is violated the moment a
   banner cut moves a statement into a different bucket. The concrete case is the
   `// Fire glow:` banner at 260: banner-cutting would have put it in
   `frameTimings_.particles`; it has always been measured in
   `frameTimings_.boids`, and it stays there.
3. **No `FrameRenderState` struct.** The plan anticipated one for "the frame's
   camera/frustum/visibility results"; the locals census found those are not
   locals at all — `captured3DView`/`captured3DProj`, `currentPvsRegion`,
   `regionBoundingBoxes`, `occlusionCulledRegions` and `have3DTransforms_` are
   already members or `ctx_` fields and were before G05. Not one of the 50-odd
   real locals outlives its pass.

### Corrections to the Batch G template

- **(24) When the method carries measurement brackets, the BRACKET is the cut,
  not the banner.** G04's correction 14 says take the file's banners; that is
  right for a method whose only structure is banners, and wrong for one whose
  regions are already delimited by a stopwatch. A banner cut that lands on the
  wrong side of a `measureSection()` silently re-buckets timing — a behaviour
  change in the diagnostics, invisible to a line multiset (the line is still
  there, verbatim, exactly once) and invisible to an order golden (the order is
  unchanged). Enumerate the brackets first and let the banners fill in only the
  gaps between them.
- **(25) A text guard that matches a SPELLING must gain a REACHABILITY clause the
  moment the body it guards moves into a function of its own.** This is the
  specific way correction 21 fails on a G unit: the guard keeps passing, because
  the spelling and the file are both unchanged — the split moved the block into a
  pass, and a pass nothing calls still spells the condition. Widen by resolving
  the enclosing definition from the match position and asserting the driver calls
  it, then plant the break for *each* half separately; a single plant cannot
  distinguish the two.
- **(26) Do the loop-depth exit census even when the body obviously looks like a
  pipeline.** G05's took one pass and turned a plausible worry (four exits, two
  of them `continue`s) into a settled fact: both `continue`s are at loop depth 1
  inside a single region's own `for`, and the one early `return` is the *first*
  statement of the body, so the driver keeps it and no pass needs a bool. Cheap,
  mechanical, and it is what tells a pipeline from G07's search.
