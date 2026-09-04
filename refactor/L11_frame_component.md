# L11 — `FrameComponent` (family: `renderer_frame.cpp` + `_input` + `_render` — the per-frame loop, input drain, UI panel state)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–32).
**Runs ALONE, LAST before L12.** Survey at `cfadb78e`: 1,857 lines, 9
bodies, **24 single-family members — the UI panel state**
(`hotbarState_`, `spellGemState_`, `buffBarState_`, `groupPanelState_`,
`spellbookState_`, `skillsPopupState_`, `inventoryState_`, `lootState_`,
`uiLayout_`, `chatPanelState_`, `newUIEnabled_`, …); re-measure.

⚠ **This is the render/game thread boundary.** `processFrame` /
`processFrameInput` drain `eventReceiver_` (render thread) and the bridge
(game thread → render thread events). The 11 friend fixtures
(`SimulationOutputLifetime` ×6 — L07 left them on the renderer;
`SpellGemKeyRace` ×3, `ReplyToTellKey` ×2) reach frame-family members.
Rules: intersect each fixture's member set with the move set (correction
29); a fixture that names a moving member is retargeted at its call sites
with assertions untouched, or the member stays and is reached via
`owner_` (correction 31) — record each. No sync primitive, queue, or
ownership changes (STOP).

Entry points `Application` calls (`processFrame`, and `run`/`shutdown`
stay on `IrrlichtRenderer` — core) keep forwarders (named callers:
`application_frame.cpp`). Text readers: `tests/test_ui_hide_command.cpp`
and `tests/test_static_ui_scroll.cpp` read `renderer_frame_input.cpp` /
`renderer_frame_render.cpp` / `renderer_event_receiver.cpp` AS TEXT and
locate bodies by owner spelling — correction 21: widen to both spellings,
re-prove with a planted break; keep the basenames under
`src/client/graphics/frame/` and repoint the path constants.

Siblings reached (after L02–L10): `owner_.Slash()` (`processSlashCommand`),
`owner_.PlayerCamera()` (`updatePlayerMovement`, `applyFirstPersonPitchClamp`,
`drawCollisionDebugLines`, `setPlayerPosition`), `owner_.Overlays()` (~12
draw/update calls), `owner_.Diagnostics()` (`measureSection` stayed on the
renderer per L04 — leave it), `owner_.SimBridge()` (`applySimulationResults`,
`postSimulationInput`), `owner_.ZoneStream()` (`buildDeferredObject`,
`processFrameProgressiveLoad`, `queueEntityPrepRequests`),
`owner_.ZoneLifecycle()` (`drainGPUResults`, `processCompletedUploads`,
`processOneGPUResult`, `buildZonePlaceholder`, `setupHCMapCollision`),
`owner_.Eqe1()` (`renderFireGlowIcospheres`, `updateFireGlowLighting`).
The slash component reaches the 24 UI members as `owner_.X_` today — after
this unit they are `owner_.Frame().X()` getters or public members; record
the choice (prefer the smallest public surface).

Predicted header delta: −9 declarations −24 members −(nested types that
move: `hudCachedState_`'s type went with L03? measure) −banners
+(forwarders ≈ 2) +(scaffolding 4..6) ≈ **−30 or better**.

Territory: `include/client/graphics/frame/frame_component.h` (new),
`src/client/graphics/frame/` (git mv of the three TUs, basenames kept),
`irrlicht_renderer.h`, `renderer_init.cpp`, root `CMakeLists.txt`, the new
test + cmake, the two text-reader tests (path constants + spelling
widening only), fixture call sites (if forced), allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in `application_frame.cpp`,
`irrlicht_bridge.cpp`, the components named. Visual gate + ARM NOT
skippable.

STOP: a thread-ownership or sync change; a fixture's observable changes;
the visual gate diffs any golden.


## Review

Executed in `wt/slot2` on `task/L11-frame-component`, branched from `fable` at
`69c6164c`. Every verification step passed. **Neither the thread-ownership STOP
nor the fixture-observable STOP fired, no golden moved, and no moved body line
was added, deleted or modified beyond the mechanism's substitutions.**

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,590 -> 1,567 (-23)**;
allowlist lowered to 1,567 in the same commit. `FrameComponent` takes **9
bodies** (the survey's 9 — `measureSection()` is a tenth body in the family's
TU that deliberately did NOT move) and **31 members plus the two tier-interval
constants**. Residual on the three TUs: **395/400, 512/513, 396/397 code
lines, zero removed, and every addition an include or the out-of-line
ctor/dtor.** Suite **3,731 / 3,732**, the ALSA quirk the only failure. Visual
gate PASS, no golden touched. GLES2 exit 0, ARM exit 0.

### The two STOP conditions, and why neither fired

**1. Thread ownership — read line by line, since this IS the boundary.**
Nothing about a thread, a queue, a mutex or a buffer lifetime changed:

| crossing | before | after |
|---|---|---|
| `processFrame()` entry | render thread, from `application_frame.cpp:181` and `IrrlichtRenderer::run()` | identical — the forwarder on `IrrlichtRenderer` is a one-line call into the component, which is a plain member with the same lifetime |
| the receiver drain (`drainActions`, `wasLeftButtonClicked`, `getMouseWheelDelta`, `popKeyEvent`) | render thread, through `eventReceiver_` | identical — `eventReceiver_` is a `RendererContext` core member, did NOT move, and is reached as `ctx_.eventReceiver`. The mutex-guarded `bridgeQueue_` (T312) is never touched except through the receiver's public API |
| the T319 chat prefill | game thread parks it in `pendingChatPrefill_` under `chatPrefillMutex_`; render thread takes it in `processChatInput()` | identical — **both the member and the mutex stayed on `IrrlichtRenderer`**, and the moved body reaches the text through `owner_.takeChatPrefill()`, the accessor the header requires as the only access path |
| the SimulationWorker | `simBridge_->applySimulationResults()` / `postSimulationInput()`; the worker handle is on `IrrlichtRenderer` since L07 | identical — `owner_.SimBridge().…` |
| the UI panel state | written by the game thread through `irrlicht_bridge.cpp`, read by the render thread in phase 4, no lock | identical — same objects, same lifetime, same absence of a lock, only the receiver expression changed |

L07 correction 31 for the fifth consecutive unit: `owner_`/`ctx_` is the
ANSWER to a thread-ownership question, not a workaround for one.
`FrameComponentTest.ProcessFrameOnANotRunningRendererStopsThroughTheForwarder`
and `…PlayerInputIsIgnoredWhileChatIsFocusedThroughOwner` are the executable
form of the two rows that could have moved and did not.

**2. Fixture observables.** No assertion in any of the 11 friend fixtures was
touched; every edit is a receiver retarget. See the intersection table below.

### The fixture intersection (batch correction 29) — 11 friends, 2 affected

Intersecting each fixture's member set with the unit's MOVED member set,
before any edit:

| fixture (friend lines) | members it names | intersection with the move set | disposition |
|---|---|---|---|
| `SimulationOutputLifetimeTest_*` (6 lines) | `usePvsCulling_`, `regionPvsDepthMap_`, `portalVisibleRegions_`, `particleRenderBuffer_`, `currentFrameSimResults_`, `particleManager_` | **empty** | **untouched — not one line changed.** L07 measured all six as staying; this unit moved none of them |
| `SpellGemKeyRaceTest` + its two `_Test` lines (3) | `eventReceiver_` (the fixture installs it), `processChatInput()` | **empty on members**; the METHOD moved | friend lines stay (`eventReceiver_` is still a private `IrrlichtRenderer` member). **2 call sites** retargeted to `renderer.Frame().processChatInput()`. Assertions untouched; 2/2 pass |
| `ReplyToTellKeyTest` (1) | `eventReceiver_` (fixture), plus `chatPanelState_`, `processChatInput()`, `handleActionResult()` in the TEST_F bodies (all public, so friendship never covered them) | **`chatPanelState_` moves** | friend line stays (still needed for `eventReceiver_`). **14 call sites** retargeted: 12 `renderer.Frame().chatPanelState_`, 2 `renderer.Frame().processChatInput()`. Assertions untouched; 5/5 pass |

**Not one `friend class` line moved, was added, or was removed.** The whole
exposure was 16 receiver expressions across two files. Correction 29's promise
held exactly: intersecting first turned a flagged eleven-fixture risk into two
mechanical edits, and the intersection was non-empty in precisely the one place
L07 correction 26 predicted it would be (`chatPanelState_`).

Three MORE tests drive moved methods without being friends at all and were
retargeted the same way: `test_inventory_toggle.cpp` (1 site),
`test_debug_hotkey_arms.cpp` (7), `test_ui_hide_command.cpp` (2).

### Text-matching guards: THREE, not two — and the third was found by grep, not by the plan

The plan named `test_ui_hide_command.cpp` and `test_static_ui_scroll.cpp`.
A K03 (c) grep of `tests/` for the three basenames found a **third**:
`tests/test_debug_hotkey_arms.cpp:132`, whose four literals name
`diagnostics_->toggle*()` — a receiver this unit rewrites to
`owner_.Diagnostics().toggle*()`. **That guard would have gone red on the build
and, worse, could have been "fixed" by pointing it at the new spelling only,
which is exactly the blindness correction 21 exists to prevent.** All three are
widened to accept BOTH spellings and to assert exactly one matches:

| guard | path repointed to | spellings accepted |
|---|---|---|
| `UIHideSourceInvariants.StaticUiBlockIsGatedOnAllUIHidden` | `src/client/graphics/frame/renderer_frame_render.cpp` | `if (newUIEnabled_ && uiRenderer_ && !allUIHidden_)` / `if (owner_.newUIEnabled_ && owner_.uiRenderer_ && !owner_.allUIHidden_)` |
| `ScrollSourceInvariants.TheFrameRoutesTheWheelIntoTheStaticUI` | `src/client/graphics/frame/renderer_frame_input.cpp` | `staticUIInput_->processWheel(` / `ctx_.staticUIInput->processWheel(`, and the same pair for `processScrollKey(` |
| `DebugHotkeyArmSourceInvariants.AllFourArmsExistInProcessCommonInput` | `src/client/graphics/frame/renderer_frame_input.cpp` | per arm: `case RA::X: diagnostics_->toggleX(); break;` / `case RA::X: owner_.Diagnostics().toggleX(); break;` |

`ScrollSourceInvariants.NothingElseConsumesTheWheelAccumulator` needed only the
path: it counts the bare method name `getMouseWheelDelta()`, which survives the
receiver change (`eventReceiver_->` -> `ctx_.eventReceiver->`) untouched — and
that is exactly why a guard should count the method, not the expression around
it.

**Re-proved with five planted breaks, both outcomes on both arms**, each run
against the built binary and then reverted (by file copy, never `git stash`):

| planted break | expectation | result |
|---|---|---|
| A: delete `&& !allUIHidden_` from the phase-4 guard (found == 0) | FAIL | **FAILED** ✓ |
| B: add the owner-spelling as a comment beside the real line (found == 2) | FAIL | **FAILED** ✓ |
| C: rename `processWheel(` to `processWheelX(` (found == 0) | FAIL | **FAILED** ✓ |
| D: add the `ctx_.staticUIInput->processWheel(` spelling beside the real one (found == 2) | FAIL | **FAILED** ✓ |
| E: gut the `RA::ToggleHUD` arm (found == 0 for that arm) | FAIL | **FAILED** ✓ |

All five reverted; `diff` against the pre-break copy is empty for both TUs, and
all three guards pass on the restored tree.

### The UI-member surface decision (the plan's "record the choice")

**Chosen: public data members on `FrameComponent`, no new accessors.** The 16
static-UI panel members were ALREADY public on `IrrlichtRenderer` under the
comment "Public so IrrlichtBridge can update them directly"; five more
(`uiLayout_`, `uiRenderer_`, `newUIEnabled_`, `allUIHidden_`, `staticUIInput_`)
were private with external reachers in `renderer_init.cpp`,
`SlashCommandsComponent` and `DiagnosticsComponent`.

| option | new named API | call-site shape |
|---|---|---|
| **public members (chosen)** | **zero** | pure receiver retarget: `X.member_` -> `X.Frame().member_` |
| accessor per member | +10 method names at least, and three of them would have to hand out a reference to a `unique_ptr` because `renderer_init.cpp` **assigns into** `uiRenderer_`, `staticUIInput_` and `newUIChatBuffer_` | same edit plus a rename |

The public-member form is strictly the smaller surface and the smaller diff, so
it is what shipped. The four inline accessors on `IrrlichtRenderer` that read
moving members (`isAllUIHidden`, `isNewUIEnabled`, `setNewUIEnabled`,
`getUIRenderer` — K03 (d) / K04 (f)) stayed as one-line forwarders because each
has real callers, and they are what keeps `slash/`, `diagnostics/` and the two
UI tests reading the same bool through a stable name.

### Members: 42 via `ctx_`, ~60 via `owner_`, 31 moved

Measured on CODE LINES ONLY, by a tokenising pass that respects string literals
and preprocessor lines (K01 (4) / K08 (w)) — a whole-file regex would have
rewritten the T230/T235/T319 history comments in `processChatInput()`, the
T67 Priority-3/4/7/10 removal notes in `processFrame()` and the P10b/P23
particle-race note in phase 4, all of which quote old code verbatim.

- **31 moved.** 21 UI/static-UI state (`cachedPlayerStats_`, `cachedTargetInfo_`,
  `chatPanelState_`, `newUIChatBuffer_`, `inventoryState_`, `hotbarState_`,
  `spellGemState_`, `buffBarState_`, `castingBarState_`, `groupPanelState_`,
  `petPanelState_`, `spellbookState_`, `skillsPopupState_`, `lootState_`,
  `xpBarState_`, `staticUIInput_`, `uiRenderer_`, `uiLayout_`, `newUIEnabled_`,
  `allUIHidden_`, `pendingUIClick_`) and 10 private frame-loop members with
  **zero readers outside this family** (`runTier2_`, `runTier3_`,
  `tier3DeltaAccum_`, `windTime_`, `pendingScreenshot_`, `hudAnimTimer_`,
  `polygonBudgetExceededFrames_`, `constrainedStatsLogCounter_`,
  `frustumDebugDraw_`), plus `kTier2Interval`/`kTier3Interval` (K08 (v): a
  class constant a mover uses travels with it — both are referenced only by
  `processFrame()`).
- **42 on `RendererContext`** (`ctx_.X`): `animatedTextureManager`,
  `boidsManager`, `bridge`, `camera`, `cameraController`, `collisionMap`,
  `config`, `constrainedTextureCache`, `currentHour`, `currentMinute`,
  `currentPvsRegion`, `detailManager`, `device`, `doorManager`, `driver`,
  `entityPrepReady`, `entityPrepWorker`, `entityRenderer`, `eventReceiver`,
  `fireEffectsEnabled`, `frustumCuller`, `governor`, `objectInSceneGraph`,
  `objectNodes`, `occlusionCuller`, `particleManager`, `playerX`, `playerY`,
  `playerZ`, `portalOcclusionEnabled`, `portalSystem`, `progressiveLoadingActive`,
  `regionBoundingBoxes`, `regionMeshNodes`, `renderDistance`, `skyRenderer`,
  `smgr`, `treeWindController`, `usePvsCulling`, `weatherEffects`, `zoneBspTree`,
  `zoneShader`.
- **~60 shared, reached through `owner_.X_`.** The dominant one is
  `frameTimings_` (198 references) with `frameTimingsAccum_` (50),
  `frameTimingsSampleCount_` and `frameTimingEnabled_`: all four are shared with
  `DiagnosticsComponent` (93 references in `diagnostics_component.cpp`), so
  §Mechanism 3 keeps them on `IrrlichtRenderer`. Also `sectionStart_`,
  `frameStart_`, `frameNumber_`, `frameCount_`, `currentFps_`, `lastFpsTime_`,
  `chatInputFocused_`, `playerConfig_`, `cameraMode_`, `guienv_`,
  `renderPassTimer_`, `zoneReady_`, `occlusionCulledRegions_`,
  `sortedZoneDrawList_`, `deferredObjects_`, the two texture-rebuild queues, the
  four overlay rotation/mirror flags, `showMapOverlay_`/`showNavmeshOverlay_`,
  `portalDebugDraw_`/`stencilDebugDraw_`, `loadingScreenVisible_`/
  `loadingProgress_`/`loadingText_`, `captured3DView_`/`captured3DProj_`/
  `have3DTransforms_`, `characterModelView_`, `softwareCursorTexture_`,
  `spellVisualFX_`, `particleRenderBuffer_`, `zonePlaceholderNode_`,
  `lastPlaceholderBuildPos_`, `entityPrepScanCounter_`, `playerHeading_`,
  `lastPolygonCount_`, the five fire-glow members, `icosphereProgram_`,
  `forcePvsUpdate_`, `sceneProfileEnabled_`/`sceneProfileFrameCount_`,
  `renderCostAvgUs_`/`essentialSimCostAvgUs_`, `currentTargetId_`/
  `currentTargetName_`, `initialized_`.

**Six `IrrlichtRenderer` methods that stayed** are called as `owner_.X()`:
`isRunning()`, `measureSection()` (35 sites), `takeChatPrefill()`,
`getNearestWorldObject()`, `toggleInventory()`, `saveScreenshot()`.
**Eight sibling components** are reached through the accessors, exactly the
list the plan predicted plus `owner_.Rdp()` (the `WITH_RDP` frame capture),
which the plan did not name.

Three receiver checks (K10 (t)) that would otherwise have produced wrong
"shared" verdicts: `frameStart_` also exists on `FrameBudgetGovernor` and on
`tools/hwcaps/zone_bench_metrics.h`; `frameCount_` on `SoundBuffer`;
`windTime_` on `ZoneShaderManager`. All three `IrrlichtRenderer` members are
frame-only. Two of them moved; `frameStart_` did not — see Deviations.

### Substitutions applied, and the two special cases

Four classes, all mechanical, applied by a tokenising pass:
`X_` -> `ctx_.X` (42 names), `X_` -> `owner_.X_`, `Foo()` -> `owner_.Foo()` (6
names), `component_->` -> `owner_.Component().` (8 names).

- **Correction 38/41 (nested type in a body) fires exactly once**:
  `frameTimingsAccum_ = FrameTimings();` in `processFrame()` becomes
  `owner_.frameTimingsAccum_ = IrrlichtRenderer::FrameTimings();`. That single
  line is what makes axis 2 of K13 correction (ii) live for this component.
- **Correction 42 (`this`) does not fire at all**: there is not one use of
  `this` in any of the three TUs, as a capture or as an argument.

### `measureSection()` did NOT move — the plan's instruction, followed literally

The plan says "`owner_.Diagnostics()` (`measureSection` stayed on the renderer
per L04 — leave it)". `measureSection()` and `sectionStart_` therefore stay on
`IrrlichtRenderer`; the body stays in `frame/renderer_frame.cpp` (where L04 put
it) as an `IrrlichtRenderer::` definition inside the moved family's TU, and the
35 call sites in the moved bodies reach it as `owner_.measureSection()`.

This is deliberately NOT what the source comment above that body predicts —
`renderer_frame.cpp` says "L11 takes it with the rest of the frame family", and
it is single-family by every measurement (`sectionStart_` has zero references
outside these three TUs, and no file outside them calls `measureSection`). The
plan's instruction wins; the comment is left in place as the record of L04's
decision, and the disagreement is flagged here rather than resolved silently.
**L12 can move both in one substitution pass** (`owner_.measureSection()` ->
`measureSection()`, plus the declaration and one member) if that is the wanted
end state.

### Call sites retargeted (105 across 8 files) — computed before the first build

| file | sites | what |
|---|---|---|
| `src/client/bridge/irrlicht_bridge.cpp` | **54** | `renderer_->X_` -> `renderer_->Frame().X_` for 15 of the panel members. **Call-site only; the K11 rule is bent here by the plan's own territory grant** ("CALL-SITE-ONLY edits in … `irrlicht_bridge.cpp`") — see Deviations |
| `src/client/graphics/slash/renderer_slash_commands.cpp` | 23 | `owner_.X_` -> `owner_.Frame().X_` (`/scroll`, `/newui`, `/newbook`, the skills and loot commands) |
| `src/client/graphics/renderer_init.cpp` | 8 | the static-UI construction block + the ctor wiring line |
| `src/client/graphics/renderer_entities.cpp` | 4 | `inventoryState_` -> `frame_->inventoryState_` inside `openInventory`/`closeInventory`/`isInventoryOpen` |
| `src/client/graphics/diagnostics/diagnostics_debug.cpp` | 3 | `owner_.allUIHidden_` -> `owner_.Frame().allUIHidden_` in `toggleAllUI()` |
| `tests/test_reply_to_tell_key.cpp` | 14 | receiver only |
| `tests/test_debug_hotkey_arms.cpp` | 7 | receiver only |
| `tests/test_spell_gem_key_race.cpp` / `test_ui_hide_command.cpp` / `test_inventory_toggle.cpp` | 2 / 2 / 1 | receiver only |

`src/client/application.cpp`, `application_frame.cpp`, `application_zone_loading.cpp`
and `graphical_mode.cpp` are **untouched** — `processFrame()`'s forwarder held.

### Predicted vs measured header delta

| | lines |
|---|---|
| plan | −30 or better |
| predicted before the first edit, at +3..+6 per construct (L01 correction 3) | **−31** |
| measured | **−23** (1,590 -> 1,567; `git diff --numstat` 34 added / 57 removed) |

The prediction was optimistic by 8 lines and the arithmetic is exact.
**57 removed** = 13 (the nine phase declarations, their banner and the
surrounding blanks) + 1 (`frustumDebugDraw_`) + 10 (the frame-phase block:
`pendingUIClick_`, `runTier2_`, `runTier3_`, the tier banner, the two
constants, `tier3DeltaAccum_`, `windTime_` and two blanks) + 1
(`pendingScreenshot_`) + 1 (`allUIHidden_`) + 1 (`hudAnimTimer_`) + 3 (the
polygon/stats counters and their line) + 3 (`uiRenderer_`/`uiLayout_`/
`newUIEnabled_`) + 21 (the public UI panel block with its two banners) + 3
(accessor bodies rewritten as forwarders, counted on both sides).
**34 added** = 1 include + 6 (the `processFrame` forwarder under a 5-line
caller note) + 6 (the `Frame()` accessor, its 4-line comment and a blank) + 8
(the friend block: blank, 6-line note, `friend class`, member) + 3 (the
forwarder accessor bodies) + 10 (six one- and two-line transition notes at the
removal sites).

**Batch correction 40 held and is again the whole margin.** Every line of
argument — the 60-line threading statement, the forwarder table with its
callers, the member census, the sibling-accessor list, the public-surface
rationale — went into `frame/frame_component.h` (**244 lines**, the largest
component header in the batch and deliberately so, because this is the thread
boundary). A first pass with that prose in the god header would have measured
roughly **+35**.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping
(`FrameComponent::` -> `IrrlichtRenderer::`; `owner_.<Family>().` -> `<family>_->`
for the eight sibling accessors; `owner_.<method>(` -> `<method>(` for the six
that stayed; `IrrlichtRenderer::FrameTimings(` -> `FrameTimings(`;
`ctx_.<name>` -> `<name>_` for all 42 context members, longest-name-first per
K04 (i); `owner_.` -> empty), then comments stripped by a tokenising pass that
respects string literals (K08 (w)), blank lines dropped, both sides compared as
multisets.

`renderer_frame.cpp`: **395 old code lines vs 400 new.**

```
removed (0):
added (5):
  #include "client/graphics/frame/frame_component.h"
  #include "client/chat/chat_message_buffer.h"
  IrrlichtRenderer::FrameComponent(RendererContext& ctx, IrrlichtRenderer& owner)
      : ctx_(ctx), owner_(owner) {}
  IrrlichtRenderer::~FrameComponent() = default;
```

`renderer_frame_input.cpp`: **512 old vs 513 new.**
`renderer_frame_render.cpp`: **396 old vs 397 new.**

```
removed (0) in each:
added (1) in each:
  #include "client/graphics/frame/frame_component.h"
```

(the two `IrrlichtRenderer::FrameComponent` lines are the normaliser's own name
mapping applied to the new ctor/dtor, not a real spelling.)

**Zero moved body lines modified, added or deleted.** The comment multiset
confirms it: `renderer_frame.cpp` **87 -> 92** (the 4-line ctor rationale and
the new include's trailing note), `renderer_frame_input.cpp` **187 -> 187**,
`renderer_frame_render.cpp` **169 -> 169**. The only "differences" the
line-level comment diff reports are four trailing comments whose CODE half was
substituted (`// drain, never accumulate`, `// EQ→Irrlicht`, the two
`// Irrlicht Z -> EQ Y` axis notes) — the comment text is byte-identical in
every case. Every historical note survived: the T230 hotbar-race and T235
spell-gem-race archives, the T319 prefill contract, the T67 Priority-3/4/7/10
removal rationale, the P10b/P23 particle-race history, the P33 tree-wind note
and the P52 GPU-phase brackets.

The three TUs keep `#include "client/graphics/irrlicht_renderer.h"` (they need
the complete type for `owner_.`), so L01 correction 4's ~60-transitive-include
rediscovery did NOT bite — eighth consecutive handle-using component.

### The one compile iteration: an incomplete type behind a moved `unique_ptr`

The first build failed with **one** error, at `renderer_frame.cpp:45` (the
out-of-line destructor): `invalid application of 'sizeof' to incomplete type
'eqt::ui::ChatMessageBuffer'`. `newUIChatBuffer_` moved with the panel block,
and `irrlicht_renderer.h` only ever forward-declared its type — the god header
never instantiated the deleter because `IrrlichtRenderer`'s own destructor is
out-of-line in a TU that does include it. Fixed by adding
`#include "client/chat/chat_message_buffer.h"` to the TU that holds the
component's destructor; second compile clean. This is batch correction 35 in a
new shape: the ctor/dtor being out-of-line is necessary but not sufficient —
**the TU that holds them must see every moved smart-pointer member's complete
type**, and a member the god header only forward-declared does not come with
one for free.

### The new test (L01 correction 9)

`tests/test_frame_component.cpp`, **9 tests, all pass**, registered in
`tests/cmake/renderer_guards.cmake` next to `test_zone_load_component` (same
`IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` guard, same bare never-`Init()`'d
`IrrlichtRenderer` idiom, with a real `RendererEventReceiver` installed through
the PUBLIC `Context()` accessor rather than a friend declaration — which is
itself the `ctx_` binding proof). Both binding paths are pinned by MUTATION
through public accessors only (L01 correction 6):

- **The moved members through both routes**:
  `AllUIHiddenIsOneBoolSeenThroughBothRoutes`,
  `NewUIEnabledIsOneBoolSeenThroughBothRoutes` and
  `UIRendererForwarderReadsTheMovedMember` each write or read the member on the
  component and read it back through the `IrrlichtRenderer` forwarder. Without
  these, a forwarder that accidentally read a stale copy would be invisible.
- **`owner_` and a sibling in one call**:
  `CommonInputToggleReachesTheRenderersOwnStateThroughOwner` runs the real
  `processCommonInput()` arm for F11 — component body -> `owner_.Diagnostics()`
  -> back to the component's own `allUIHidden_` — and
  `InventoryToggleCrossesTheRendererAndTheComponent` runs the other direction
  (`owner_.toggleInventory()`, an `IrrlichtRenderer` body that writes
  `frame_->inventoryState_`).
- **The thread statement, executable**:
  `PlayerInputIsIgnoredWhileChatIsFocusedThroughOwner` drives the T319 prefill
  through `requestChatPrefill()` (game-thread half) and `processChatInput()`
  (render-thread half) and then proves the `owner_.chatInputFocused_` read at
  the top of `processPlayerInput()` still gates the switch;
  `ProcessFrameOnANotRunningRendererStopsThroughTheForwarder` makes the exact
  call `application_frame.cpp:181` makes.
- `AccessorReachesAConstructedComponent` — the ctor wiring, without which the
  `processFrame` forwarder and all four UI accessors are production null
  dereferences.

**Every test passed on its first run** after the fixture gained the receiver;
no production line was changed for the test.

### §5: nothing deleted, nothing boarded

All nine moved methods have live callers, so §Mechanism 5's zero-caller delete
never applied. K12 (ff) checked anyway: the three TUs do contain `pushIntent`
calls (vendor/trainer/door/world-object/volume/chat-submit/player-geometry) but
no `pushEvent` or `GameEventType::`, and no method was a deletion candidate, so
no sole-publisher risk existed.

### Text readers and path citations of the moved TUs (K03 (c) / correction 27)

Greped `tests/`, `tests/cmake/`, `scripts/`, `tools/`, `docs/`,
`refactor/BOARD.md`, root `CMakeLists.txt` and every `CLAUDE.md` for all three
basenames.

- **Three tests parse them** — the widening table above. `tests/cmake/*.cmake`
  names none of them; `scripts/` has no reader.
- **`refactor/BOARD.md` carried three LIVE path citations** (B043's
  `processCommonInput` entry, the zone-line entry and the
  `processFrameRender` size entry) — all repointed to
  `src/client/graphics/frame/…`. `DocCitations` passes.
- Bare-basename mentions in `docs/decisions.md`, `tools/particle_race_stress.cpp`,
  `tests/test_bridge_queue_race.cpp`, `test_read_and_reset_ownership.cpp`,
  `test_player_camera_component.cpp`, `test_zone_lifecycle_component.cpp` and
  `src/client/graphics/CLAUDE.md` still resolve: **the basenames were
  deliberately kept.** No line of any of them changed.
- Historical unit plans under `refactor/` (J31, T235, T313, T315, L04…) cite
  the old paths as a record of what those units did at the time; they are
  archives, not live citations, and `DocCitations` does not flag them. Left
  alone, exactly as prior L units left theirs.
- `docs/module_map.md` — the single `renderer_frame.cpp` row (a bare filename,
  the K05 (l) shape) was replaced by three rows with full repo-relative paths
  and no "(was …)" prose, plus a new `FrameComponent` row in the component
  table.
- `ReachabilitySweep` and `DuplicateStateStores` walk
  `include/client/**/*_component.h` by suffix (K05 (k) / K09 (aa)), so
  `frame_component.h` joined their input set with no file-list edit; both pass.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0.** Two compile iterations: the first failed with ONE error (the incomplete `ChatMessageBuffer` above); after the include, clean. No build was ever killed at the 600 s ceiling, so the exit-143 absence/truncation scan never applied |
| `ctest --test-dir build -j6 --output-on-failure` | **3,731 / 3,732 passed**, 1 failure: `AudioManagerTest.PreloadCommonSounds` — the only permitted one. (3,722/3,723 before the new test was added, same single failure) |
| `test_frame_component` (new) | **9 tests, all pass** |
| the eleven friend fixtures | `test_simulation_output_lifetime` **6/6 pass, file untouched**; `test_spell_gem_key_race` **2/2**; `test_reply_to_tell_key` **5/5** — all assertions unchanged |
| the three text guards | `test_ui_hide_command` **7/7**, `test_static_ui_scroll` **all pass**, `test_debug_hotkey_arms` **6/6**, each re-proved with a planted break (five in total, both outcomes) |
| named guards | `ReachabilitySweep`, `DuplicateStateStores`, `FileSizeCeiling`, `DocCitations`, `MergedHelp`, `FirstPersonCameraSingleFormula`, `T124GuardsTest`, `ObjectNodeTextureGuard`, `BridgeQueueRaceTest`, `ReadAndResetOwnership`, `InventoryToggle`, `ZoneLoadComponentTest`, `ZoneLifecycleComponentTest`, `SlashCommandsComponentTest` — all pass |
| `scripts/build-gles2-x86.sh` | **exit 0**, first invocation, zero errors |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), 3 zones + 3 memory-coverage runs, every CPU p95 under ceiling (qeynos2 6.882 vs 10.108, gfaydark 5.437 vs 10.672, befallen 6.152 vs 10.948). `git status --porcelain` shows **no golden path** — nothing re-blessed. This unit moved the whole per-frame draw path, so the gate is the pixel-level proof that a 1,879-line move changed nothing |
| `scripts/build-arm-noble.sh` | **exit 0**, first invocation, zero errors, `build-arm-noble/bin/willeq` valid 32-bit ARM ELF. Not skippable: `renderer_frame_render.cpp` carries an `#ifdef EQT_HAS_DRM` fire-glow block (batch correction 9) that only this build compiles |
| `scripts/refactor/check_file_sizes.sh --all` | **859 checked, 859 ok, 0 warn, 0 FAIL**, with `irrlicht_renderer.h` lowered 1,590 -> 1,567 |
| residual multiset | **395/400, 512/513, 396/397; removed 0, added 5/1/1**; comment multiset 87->92, 187->187, 169->169 |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,590 | **1,567** (−23) |
| `src/client/graphics/renderer_frame.cpp` -> `src/client/graphics/frame/renderer_frame.cpp` | 527 | 537 |
| `src/client/graphics/renderer_frame_input.cpp` -> `.../frame/renderer_frame_input.cpp` | 755 | 756 |
| `src/client/graphics/renderer_frame_render.cpp` -> `.../frame/renderer_frame_render.cpp` | 597 | 598 |
| `include/client/graphics/frame/frame_component.h` | — | 244 (new) |
| `tests/test_frame_component.cpp` | — | 150 (new) |

### Deviations

1. **`irrlicht_bridge.cpp` learned that a component exists** (54 call sites,
   `renderer_->X_` -> `renderer_->Frame().X_`). This bends the K11 rule, and it
   is done because the plan's Territory line grants it explicitly
   ("CALL-SITE-ONLY edits in `application_frame.cpp`, `irrlicht_bridge.cpp`")
   and because the alternative — leaving 16 UI members on `IrrlichtRenderer`
   under §Mechanism 3 — would have made the header delta roughly −2 and left
   the frame family owning none of the state the plan names as its own. **No
   bridge LOGIC changed: every one of the 54 edits is a receiver expression,
   and `git diff --numstat` on that file is 54 added / 54 removed.**
   `Application` is untouched. Flagged, not silently absorbed.
2. **`measureSection()`/`sectionStart_` did not move**, against the prediction
   written into `renderer_frame.cpp` by L04 and against every measurement
   (single-family, zero external callers). The plan's "leave it" is followed
   literally; see the section above for the one-pass fix if L12 wants it.
3. **`frameStart_` is write-only and was left alone.** It is set twice in
   `processFrame()` and READ NOWHERE in the tree (the other `frameStart_`
   hits are `FrameBudgetGovernor`'s and `zone_bench_metrics.h`'s own members —
   K10 (t)); the elapsed-time arithmetic in the same function uses the LOCAL
   `frameStart`. It is a B034-shaped dead member, not this unit's territory to
   delete, and is recorded here rather than moved into the component as dead
   state (K09 (y)'s rule read forward).
4. **A third text-matching guard was found that the plan did not name**
   (`test_debug_hotkey_arms.cpp`). Widened and re-proved like the other two.

### Corrections to the Batch L template

**45. Batch correction 35 is not sufficient on its own: the TU that holds the
out-of-line ctor/dtor must see the COMPLETE TYPE of every moved smart-pointer
member, and a member the god header only forward-declared does not come with
one.** `newUIChatBuffer_` is a `std::unique_ptr<eqt::ui::ChatMessageBuffer>`
that lived on `IrrlichtRenderer` for years with nothing but a forward
declaration, because `IrrlichtRenderer`'s own destructor is out-of-line in a TU
that includes the real header. Moving the member into a component whose
destructor lives elsewhere instantiated the deleter in a TU that did not. **The
check is mechanical and should be done before the first build: for every moved
`unique_ptr`/`shared_ptr` member, confirm the destination TU includes the
pointee's definition, not just the god header's forward declaration.** This was
this unit's only compile error.

**46. Intersect the fixture set FIRST and the flagged risk usually collapses —
but grep the whole of `tests/` for the moved basenames anyway, because the
plan's list of text readers will be short by one.** The eleven friend fixtures
this plan flagged cost sixteen receiver edits and zero assertions; the
unflagged `test_debug_hotkey_arms.cpp` cost a widened guard and five planted
breaks. **The expensive coupling was the one nobody wrote down**, and it was
found by a two-second grep for the TU basenames, which correction 27 already
requires and which should be run BEFORE the fixture intersection, not after.

**47. When an extraction moves members that another layer already reads as
PUBLIC data, the K11 rule ("Application and IrrlichtBridge must not learn a
component exists") is in direct tension with §Mechanism 3, and the plan — not
the executor — must settle it.** This plan did, by granting `irrlicht_bridge.cpp`
call-site edits in its Territory line, and the unit is only worth running
because of that grant: without it the header delta is roughly −2. **Every
remaining god-class extraction should state, in the plan, which of the two
rules wins for each externally-visible member block**, because the answer
changes the unit's value by an order of magnitude and cannot be derived from
the mechanism.

**48. "Smallest public surface" for a state-bag component means PUBLIC MEMBERS,
not accessors, when the members were already public and when an external caller
ASSIGNS INTO a smart-pointer member.** Ten accessor names (four of them
returning a mutable reference to a `unique_ptr` so `renderer_init.cpp` can
`make_unique` into it) is a strictly larger and uglier API than twenty-one
public members that keep their existing names and existing usage. The measure
is NEW NAMED API, not the `public:` keyword count.
