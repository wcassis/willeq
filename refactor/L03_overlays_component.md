# L03 — `OverlaysComponent` (family: `renderer_overlays.cpp` + `renderer_hud_env.cpp` — HUD, map/navmesh/zone-line overlays, targeting, paperdoll, fog)

Batch: `refactor/L_renderer_component_extraction.md`. Wave 1, three-wide
with L02/L04. Measured at `cfbbde57`: 1,685 lines, 35 bodies, 21
single-family members (the batch survey; re-measure — they are the overlay
triangle buffers, HUD cache, zone-line box nodes, target state).

⚠ **`IOutputRenderer` overrides stay as forwarders** (L01 correction 5):
`setCurrentTarget`, `clearCurrentTarget` are declared on `output_renderer.h`
and implemented by `console_renderer` / `null_renderer` too — the
`IrrlichtRenderer` override forwards to the component. Count them.
`setCurrentTargetInfo`, `markPaperdollDirty`, `setZoneLineBoundingBoxes`
are called from `irrlicht_bridge.cpp` / `application_zone_loading.cpp` —
bridge/application callers: forwarders too (the K11 rule), counted.
`handleMouseTargeting` is called from `graphics_input_handler.cpp` and
named in `renderer_event_receiver.h` — read both; the event receiver is on
the render thread and may hold a member-function pointer.

In-renderer callers (`renderer_frame_render.cpp` ~12, `renderer_frame_input.cpp`
~5, `renderer_zone_load*.cpp`, `renderer_zone_lifecycle.cpp`,
`renderer_entities.cpp`, `renderer_init.cpp` `setupHUD`, `irrlicht_renderer.cpp`
`setupFog`) retarget to `overlays_->X()`. Zero-external-caller methods
(`checkEntityLOS`, `createZoneLineBoxMesh*`, `getEntityAtScreenPos`,
`getMapOverlayColor`, `getNavmeshAreaColor`, `recreateZoneLineBoxSceneNodes`,
`removeZoneLineBoxSceneNodes`, `setZoneLineDebug`) are in-family or dead —
§5 with the in-family rule and the K12 (ff) publisher check.

Nested-typed members bite here (K05 j): `hudCachedState_`,
`zoneLineBoxNodes_`, `mapOverlayTriangles_`, `navmeshOverlayTriangles_`
are typed with nested types of `IrrlichtRenderer` — either the nested type
moves with the member (preferred, it is single-family) or is qualified.
Record each.

⚠ `tests/test_ui_hide_command.cpp` and `tests/test_static_ui_scroll.cpp`
read `renderer_frame_render.cpp` / `renderer_frame_input.cpp` /
`renderer_event_receiver.cpp` as text — NOT this family's TUs, but your
call-site edits touch the first two; run those tests and confirm they still
find their strings (do not move those files).

Predicted header delta: −35 declarations −21 members −(nested types that
move) −banners +(forwarders ≈ 6) +(scaffolding 4..6) ≈ **−50 or better**.

Territory: `include/client/graphics/overlays/overlays_component.h` (new),
`src/client/graphics/overlays/` (git mv of the two TUs), `irrlicht_renderer.h`,
`renderer_init.cpp` (ctor line), root `CMakeLists.txt`,
`tests/test_overlays_component.cpp` + cmake, allowlist, `docs/module_map.md`,
CALL-SITE-ONLY edits in the renderer TUs, `irrlicht_bridge.cpp`,
`application_zone_loading.cpp`, `graphics_input_handler.cpp`. Visual gate +
ARM NOT skippable. L02/L04 share `irrlicht_renderer.h`, `renderer_init.cpp`,
`renderer_frame_*.cpp` at other lines.

## Review

Executed in `wt/slot2` on `task/L03-overlays-component`, branched from `fable`
at `d13f8ddc`. Every verification step passed; nothing STOPped.

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,871 -> 1,798 (-73)**;
allowlist lowered to 1,798 in the same commit. `OverlaysComponent` takes **35
bodies** (34 moved + the moved inline accessor `setNavmesh`), **24
single-family members** and **4 nested types**. The residual over both moved
TUs is **zero moved body lines modified**; the only differences are the
sanctioned §5 deletion, the moved inline accessor and one include per file.
Suite 3,619 / 3,620, the ALSA quirk the only failure.

### Measured vs the plan's lead

| | plan (at `cfbbde57`) | measured (at `d13f8ddc`) |
|---|---|---|
| bodies | 35 | **35** (27 in `renderer_overlays.cpp`, 8 in `renderer_hud_env.cpp`) |
| single-family members | 21 | **24** |
| nested types moving | 4 named | **4**, all confirmed single-family |

The three extra members are **inline-accessor state the body count cannot
see** (K04 correction f): `loadingTitle_` (written only by the inline
`setLoadingTitle`, read only by `drawLoadingScreen`), `navmesh_` (written only
by the inline `setNavmesh`, read only by the navmesh overlay) and
`headingDebugText_` (a HUD text node touched only by `setupHUD`/`updateHUD`).
None appears in any `IrrlichtRenderer::` body table. **Grepping the header for
each moving member and collecting the inline functions that touch it is not
optional in Batch L either.**

Coupling as re-measured: the two TUs reference **81** `IrrlichtRenderer`
members -- 24 single-family (moved), **27** on `RendererContext` (`ctx_.X`),
**30** shared with another family but outside the core (`owner_.X_`, Batch K
Mechanism 3). Six `IrrlichtRenderer` methods that stayed behind are called as
`owner_.Foo()`: `isLoading`, `isUsingOldModels`, `getCameraModeString`,
`ensureBoidsTextureGuard`, `checkCollisionBspFiltered`,
`getWorldObjectAtScreenPos`.

### The nested-type decisions (K05 j)

All four nested types are referenced **only** by the overlays family, so each
moved WITH its member rather than being qualified:

| nested type | member | other users tree-wide |
|---|---|---|
| `HudCachedState` | `hudCachedState_` | none (one local in `updateHUD`) |
| `ZoneLineBoxNode` | `zoneLineBoxNodes_` | none; also the parameter type of `createZoneLineBoxMeshForNode`, which moved too |
| `MapOverlayTriangle` | `mapOverlayTriangles_` | none |
| `NavmeshOverlayTriangle` | `navmeshOverlayTriangles_` | none |

`TargetInfo` looked like a fifth and is **not** nested -- it lives in
`include/client/graphics/renderer_types.h` and `irrlicht_bridge.cpp` names it
as `EQT::Graphics::TargetInfo`. No qualification was needed anywhere.

### Predicted vs measured header delta

| | lines |
|---|---|
| predicted (this plan) | -50 or better |
| predicted before starting, with L01 correction 3's +3..+6 per construct | **-70** |
| measured | **-73** (1,871 -> 1,798; `git diff --numstat` 54 added / 127 removed) |

The scaffolding actually paid, at L01's rate: `Overlays()` +4 (accessor + a
2-line comment + a blank), `overlays_` +5 (member + a 3-line comment + a
blank), `friend class OverlaysComponent` +7 (line + a 5-line comment + a
blank), the include +1, and ~30 lines of comment on the six forwarder blocks
and on the three member blocks that had to be split. **L01's correction 3 is
right and is now calibrated: predicting at +3..+6 per construct landed within
3 lines on a unit ten times the pilot's size.** L02+ start from L01's +4
baseline, so only this unit's own scaffolding (the component pointer, the
accessor, the friend line, the include = 17 lines) was new.

### Forwarders: 7

| forwarder | why it stays |
|---|---|
| `setCurrentTarget` | the public target API `ConsoleRenderer`/`NullRenderer` also implement |
| `clearCurrentTarget` | same, plus `irrlicht_bridge.cpp:253` |
| `setCurrentTargetInfo` | `irrlicht_bridge.cpp:272` (the K11 rule) |
| `markPaperdollDirty` | `irrlicht_bridge.cpp:421` |
| `setZoneLineBoundingBoxes` | `application_zone_loading.cpp:202,313` |
| `setLoadingTitle` | `application.cpp:593` -- was an inline accessor; now a one-line forwarder |
| `setNavmesh` | see the deviation below |

Plus `Overlays()`, which is scaffolding rather than a forwarder.

**Correction to the plan's premise:** the plan says `setCurrentTarget` /
`clearCurrentTarget` are "`IOutputRenderer` overrides" on `IrrlichtRenderer`.
They are not. `class IrrlichtRenderer` has **no base class** (header L114);
`IOutputRenderer` is implemented by `ConsoleRenderer`, `NullRenderer` and
`GraphicalRenderer` only, and `IrrlichtRenderer` merely carries the same two
names as a parallel public surface. They were kept as forwarders anyway, on
the plan's recorded instruction (K06/K07 correction o) -- but a later L unit
must not assume the plan's `IOutputRenderer`-override list applies to
`IrrlichtRenderer`. **Measure the base class before costing "the interface
stays" forwarders.**

`handleMouseTargeting` was read before moving, as the plan required: it is
named in `renderer_event_receiver.h` and `graphics_input_handler.cpp` **only
in comments**, and its one real caller is
`IrrlichtRenderer::processFrameInput()` (`renderer_frame_input.cpp:111`), on
the render thread. No member-function pointer exists and no thread boundary is
crossed; the site became `overlays_->handleMouseTargeting(...)` on the same
thread as before.

### §5 deletions (4) and the one deviation

Deleted, all with zero callers tree-wide and no `pushEvent`/`GameEventType::`
in the body (K12 correction ff checked on each):

| deleted | note |
|---|---|
| `setZoneLineDebug()` | named by the plan. It was the **sole writer** of `inZoneLine_`, `zoneLineTargetZoneId_`, `zoneLineDebugText_`. The three members were kept and moved anyway, because `drawZoneLineOverlay()` is live (called every frame from `renderer_frame_render.cpp:546`) and reads them -- deleting them would have forced an edit inside a moved body, which is a STOP condition. The zone-line debug border therefore can never turn on. It could not before this unit either (nothing called the setter), so this is a disclosure, not a regression. **BOARD it.** |
| `isInZoneLine()` | inline accessor on `inZoneLine_`, zero callers |
| `isZoneLineVisualizationEnabled()` | inline accessor on `showZoneLineBoxes_`, zero callers |
| `getCurrentTargetInfo()` | inline accessor on `currentTargetInfo_`, zero callers |

`getCurrentTargetId()` was **left alone**: it also has zero callers on
`IrrlichtRenderer` (its four grep hits are `SpellManager::getCurrentTargetId`),
but its member `currentTargetId_` is two-family and stays on the renderer, so
it is not part of this move (K03 correction d cuts both ways). It is a lead
for whichever unit takes the frame family.

**DEVIATION from §5, recorded not absorbed: `setNavmesh()` was KEPT** (as a
forwarder + a moved body) despite zero callers tree-wide. `NavmeshChanged` is
a live event with **three publishers** (`movement_state.cpp` x2,
`zone_component.cpp`) and a bridge arm at `irrlicht_bridge.cpp:840` whose
entire body is `LOG_TRACE` under the comment "Handled by renderer if navmesh
visualization is active" -- which is false. This is K15 correction (qq)'s
shape exactly: a knob wired at one end only. Deleting the sink would turn a
one-line bridge fix into a two-file one, and K15 (pp) warns that a survey's
"zero references" expires the moment a later unit wires the member. **The
navmesh overlay (Ctrl+N) can never render today: `navmesh_` is permanently null.** Board
it alongside the zone-line finding.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping
(`OverlaysComponent::` -> `IrrlichtRenderer::`, `ctx_.X` -> `X_` for the 27
core members, `owner_.X_` -> `X_` for the 30 shared ones, `owner_.Foo` ->
`Foo` for the 6 stay-behind methods), then comment and blank lines dropped and
both sides compared as multisets.

`renderer_overlays.cpp` -> `overlays/overlays_component.cpp`: **739 old code
lines vs 738 new.**

```
removed (4):
  void IrrlichtRenderer::setZoneLineDebug(bool inZoneLine, uint16_t targetZoneId, const std::string& debugText) {
      inZoneLine_ = inZoneLine;
      zoneLineTargetZoneId_ = targetZoneId;
      zoneLineDebugText_ = debugText;
added (3):
  void IrrlichtRenderer::setNavmesh(PathfinderNavmesh* navmesh) {
      navmesh_ = navmesh;
  #include "client/graphics/overlays/overlays_component.h"
```

The two bodies also share `if (isLoading()) return;` and `}`, which the
multiset absorbs one-for-one -- one body left, one arrived.

`renderer_hud_env.cpp` -> `overlays/overlays_hud_env.cpp`: **494 old code
lines vs 495 new; removed 0, added 1** (the component's own `#include`).

**Zero moved body lines modified in either TU.** Both TUs keep
`#include "client/graphics/irrlicht_renderer.h"` -- they need the complete
type for `owner_.` -- so L01 correction 4's "dropping the renderer header
drops ~60 transitive includes" did NOT bite here: the build was clean on the
first compile, with no incomplete-type iteration at all. **A component that
keeps a live transition handle pays no include-rediscovery cost.**

### Call sites retargeted (27) and the friend line

24 in the renderer's own TUs plus 3 inside inline bodies in
`irrlicht_renderer.h` (`setRenderDistance` and `setFogThickness` call
`overlays_->setupFog()`; `setLoadingProgress` calls
`overlays_->drawLoadingScreen()`). Breakdown: `renderer_frame_render.cpp` 11,
`renderer_frame_input.cpp` 7, `renderer_zone_load_env.cpp` 2, and one each in
`renderer_zone_lifecycle.cpp`, `renderer_zone_load.cpp`, `renderer_init.cpp`
(`setupHUD`) and `irrlicht_renderer.cpp` (`setupFog`). Every one was named by
the compiler after the declarations came out of the header, and the compiler
named no others.

`friend class OverlaysComponent;` is required on the **first** axis of K13
correction (ii) -- 30 `owner_.X_` members and 2 private `owner_.Foo()` methods
(`checkCollisionBspFiltered`, `getWorldObjectAtScreenPos`). The second axis is
clear: the component names **no** `IrrlichtRenderer` static, nested type or
constant (`SKY_FAR_PLANE`, `rayIntersectTriangles`, `extractWorldTriangles`,
`isEntityBlockingProgressiveLoad`, `isPersistentEQE1Archive` -- none appears in
either TU), because all four nested types it needed moved with it.

Three member blocks had to be **split** rather than moved whole, because the
frame family touches part of each: `showMapOverlay_` / `mapOverlayRotation_` /
`mapOverlayMirrorX_`, `showNavmeshOverlay_` / `navmeshOverlayRotation_` /
`navmeshOverlayMirrorX_` and `characterModelView_` stayed on
`IrrlichtRenderer`; the rest of each block moved. Each split carries a comment
on both sides saying which half went where.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. **Zero compile errors on the moved code at any point** -- the only build failure in the unit was the new test missing `client/zone_lines.h`. Two builds hit the 600 s ceiling (exit 143) mid-link; the truncation scan found 6 killed-`ar` temp archives in `build/lib` and 7 partially linked test binaries, all deleted before the rebuild, and the post-rebuild scan reported 238 artifacts, 0 bad, with `libwilleq_core.a` and `bin/willeq` both present |
| `ctest --test-dir build -j6 --output-on-failure` | **3,619 / 3,620 passed**, 1 failure: `AudioManagerTest.PreloadCommonSounds` (the ALSA quirk) -- the only permitted one. `FileSizeCeiling`, `DocCitations`, all five `ReachabilitySweep` arms and `DuplicateStateStores` PASS |
| `test_overlays_component` (new) | **7 tests, all pass** |
| `test_ui_hide_command` | **7 tests pass** -- still finds its strings in `renderer_frame_render.cpp` / `renderer_frame_input.cpp`, which this unit edited call sites in |
| `test_static_ui_scroll` | **39 tests pass**, same |
| `scripts/build-gles2-x86.sh` | **exit 0** (the 3 GB static link needed three foreground invocations to clear the 600 s ceiling; zero errors in every log) |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`). `git status --porcelain` shows **no golden path** -- nothing re-blessed |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh --all` | **836 checked, 836 ok, 0 warn, 0 FAIL**, with `irrlicht_renderer.h` lowered 1,871 -> 1,798 |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,871 | **1,798** (-73) |
| `src/client/graphics/renderer_overlays.cpp` -> `src/client/graphics/overlays/overlays_component.cpp` | 1,042 | 1,057 |
| `src/client/graphics/renderer_hud_env.cpp` -> `src/client/graphics/overlays/overlays_hud_env.cpp` | 643 | 653 |
| `include/client/graphics/overlays/overlays_component.h` | -- | 189 (new) |
| `tests/test_overlays_component.cpp` | -- | 104 (new) |

### Corrections to the Batch L template

1. **A plan's `IOutputRenderer`-override list does not apply to
   `IrrlichtRenderer`.** It has no base class. Measure it before costing
   "interface stays" forwarders (details above).
2. **Inline accessors hid 3 of this unit's 24 members**, i.e. the survey's
   member count was 12% low. K04 correction (f) transfers to Batch L
   unchanged and every L plan should apply it before predicting a delta.
3. **A member block may have to be SPLIT.** Three of this family's blocks are
   part single-family and part two-family; the mechanism handles it, but a
   plan that costs "the map overlay block, -16" is wrong by the three members
   the frame family reads. Cost members, not blocks.
4. **Keeping the transition handle costs nothing and saves the include hunt.**
   Because both moved TUs still include `irrlicht_renderer.h` for `owner_.`,
   L01 correction 4 did not apply and the build was clean first time. Only a
   component that ends up with zero `owner_.` uses (L01's `RdpComponent`) pays
   the ~60-transitive-include rediscovery.
5. **§5 is not automatic for a SETTER that is the only enable path of a live
   feature.** Two turned up here (`setZoneLineDebug`, `setNavmesh`) and both
   are genuine defects rather than dead code -- an overlay that can never be
   switched on. One was deleted (the plan named it), one was kept. Whichever
   an L unit chooses, **it must be boarded**: a silent §5 delete converts a
   findable defect into an invisible one.
6. **The 600 s ceiling now bites the GLES2 link as well as the test link.**
   `build-gles2-x86.sh` needed three foreground invocations and
   `cmake --build build` two, each time with zero errors and only a killed
   link. Expect it on every L unit and budget the calls; the truncation scan
   must cover `build/lib/st*` (killed `ar` temporaries), which a
   `readelf`-over-`bin/` sweep does not see.
