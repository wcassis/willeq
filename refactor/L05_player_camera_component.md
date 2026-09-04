# L05 — `PlayerCameraComponent` (family: `renderer_player_physics.cpp` + `renderer_player_collision.cpp` + `renderer_camera.cpp`)

Batch: `refactor/L_renderer_component_extraction.md` (§L01 template + the
Wave 1 corrections, folded before dispatch). **Runs ALONE after L02–L04
merge** — its callers span the frame, zone-load, entities, overlays and
slash families, and its two text-reader tests pin filenames.

## Measured at `d13f8ddc` (2026-09-03) — re-measure at your tip

2,152 lines, 34 bodies. `updatePlayerMovement` (806 lines) moves WHOLE
(Batch G decides its decomposition). Single-family members: 3 (survey);
nested-typed members `cameraMode_` (`enum class CameraMode`),
`doorCollisionData_`, `collisionDebugLines_` — K05 j: the nested types
move with the members or are qualified; `CameraMode` is ALSO named by
`IOutputRenderer::setCameraMode(CameraMode)` in `output_renderer.h`, so
check where `CameraMode` is actually declared (`renderer_types.h`? the
class?) before deciding.

### Forwarders that stay (count them)

Same-named methods that `IOutputRenderer`'s OTHER implementations carry (`IrrlichtRenderer` has NO base class — L03 correction 13; keep these as forwarders because of their bridge/application callers, not because of an interface): `setPlayerSpawnId`, `setPlayerPosition`
(the hottest bridge call — `irrlicht_bridge.cpp`, `bridge_component.cpp`,
`entities_component.cpp`, `zone_component.cpp`, `application_zone_loading.cpp`),
`setCameraMode`, `cycleCameraMode`. Bridge/application callers:
`setSwimmingState`, `setVisionType` (`irrlicht_bridge.cpp`),
`setCameraMode`/`setPlayerPosition`/`setPlayerSpawnId`
(`application_zone_loading.cpp`). `graphical_mode.cpp` / `graphical_renderer.cpp`
reach `cycleCameraMode`, `setCameraMode`, `getCameraModeString` through the
interface — read `graphical_renderer.h` (it may declare its own virtuals of
the same names; a same-named method on another class is not a caller —
K10 t).

### In-renderer callers → `playerCamera_->X()`

`renderer_frame_render.cpp` (`applyFirstPersonPitchClamp`,
`drawCollisionDebugLines`), `renderer_frame_input.cpp` (`updatePlayerMovement`,
`setPlayerPosition`), `renderer_event_receiver.cpp` + `graphics_input_handler.cpp`
(`updatePlayerMovement` — ⚠ the event receiver is the OTHER thread's
class; read how it reaches the renderer: a back-pointer, a callback, or a
member-function pointer; `camera_controller.h` and `renderer_event_receiver.h`
name it — comments or declarations?), `renderer_zone_draw.cpp`,
`renderer_zone_load*.cpp` (`addRegionToCollision`, `extractWorldTriangles`,
`rayIntersectTriangles`, `rayTestDoorsAndObjects`, `setupMinimalZoneCollision`),
`renderer_entities.cpp` (`repositionFirstPersonCamera`, `setPlayerRace`),
`renderer_overlays.cpp` → after L03, `overlays/…` (`checkCollisionBspFiltered`
— write `ctx_.PlayerCamera().checkCollisionBspFiltered()` there),
`renderer_hud_env.cpp` → after L03 (`getCameraModeString`),
`renderer_slash_commands.cpp` (5 methods; L06 later), `irrlicht_renderer.cpp`
(`findGroundZ`, `updateObjectLightColors`).

Zero-external-caller methods (`addCollisionDebugLine`, `addDoorToCollision`,
`addObjectToCollision`, `checkCollisionIrrlicht`, `checkMovementCollision`,
`clearCollisionDebugLines`, `findBspRegionForPoint`, `findGroundZBspFiltered`,
`findGroundZIrrlicht`, `getCameraTransform`, `getFirstPersonVerticalFov`,
`resetVisionToBase`): in-family or dead; §5 with the in-family rule and the
K12 (ff) publisher check. ⚠ `updateObjectLightColors` is in this family's
TUs but is a lighting concern — if it reads only `objectLights_` (core), it
may belong in L09; decide and record.

### Text readers — REPOINT, DO NOT RENAME

`tests/test_first_person_camera_single_formula.cpp` parses
`renderer_camera.cpp` (`kHelperTU`, line ~51) and `renderer_player_physics.cpp`
(`kFloorSecondReader`, ~147) BY NAME and asserts the single-formula
invariant across the two TUs (T301/T310). **Keep both basenames**
(`src/client/graphics/player/renderer_camera.cpp`,
`…/renderer_player_physics.cpp`, `…/renderer_player_collision.cpp`) and
repoint only the directory in the test's path constants; assertions
untouched. If the test resolves paths relative to `src/client/graphics/`,
that is the one edit. `test_bridge_queue_race.cpp`,
`test_entity_collision_z_offset.cpp`, `test_first_person_body_draw_rule.cpp`
name the TU in comments only — verify.

Predicted header delta: −34 declarations −3 members −(nested types that
move) −banners +(forwarders ≈ 8) +(scaffolding 4..6) ≈ **−30 or better**.

## Territory

`include/client/graphics/player/player_camera_component.h` (new),
`src/client/graphics/player/` (git mv of the three TUs, basenames kept),
`irrlicht_renderer.h`, `renderer_init.cpp` (ctor line), root `CMakeLists.txt`,
`tests/test_player_camera_component.cpp` + cmake,
`tests/test_first_person_camera_single_formula.cpp` (path constants only),
allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in every renderer TU,
component and bridge file the compiler names. Runs alone. Visual gate + ARM
NOT skippable — and this family is the first-person camera: the visual
gate's first-person goldens are the proof the move changed nothing.

## STOP conditions

- `updatePlayerMovement` needs any edit beyond the four substitutions → STOP.
- The single-formula test's assertion would have to change → STOP.
- The visual gate diffs any golden → STOP; never re-bless.

## Review

Executed in `wt/slot3` on `task/L05-player-camera-component`, branched from
`fable` at `fcda001a`. **The unit STOPPED on its first pass and was
re-authorised.** The STOP report and the full pre-execution measurement that
made the ruling possible are preserved verbatim in the commit that precedes
this one (`70abaf40`); this section records the executed unit. Every
verification step passed.

### The STOP, and the ruling that resolved it

The plan said `tests/test_first_person_camera_single_formula.cpp` needed only
its PATH CONSTANTS repointed. **That was false.** Its Part 3 locates a BODY,
not a file — `lines[i].find("void IrrlichtRenderer::" + kHelperName + "()")` —
and then asserts on the substrings `camera_->setPosition(` /
`camera_->setTarget(` inside it. Moving the family renames the body to
`PlayerCameraComponent::` and §Mechanism 3 rewrites `camera_` to `ctx_.camera`,
so three assertions break. Worse, Part 1's duplicate-formula detector greps all
of `src/client/graphics` for `camera_->setPosition(` next to
`CameraMode::FirstPerson`, so a re-duplicated formula written in component
style would have been invisible to it — a silent erosion of the T295/T300
regression net across all of Batch L, not just L05.

The orchestrator approved resolution 1: **widen the guard's INPUTS, never its
intent.** This is the same shape as `ReachabilitySweep` at K05 (k) and
`DuplicateStateStores` at K09 (aa) — a text-scanning guard learning a new file
or spelling. The invariant is untouched: exactly one first-person camera
formula, applying both tunables and the user's mouse-look pitch.

### The amended guard, and the deliberate-break proof

`tests/test_first_person_camera_single_formula.cpp`, +50/-9:

- **Part 3 locates the body under EITHER owner** (`IrrlichtRenderer::` or
  `PlayerCameraComponent::`) and asserts `definitionsFound == 1` — two
  definitions would be the duplication the test exists to stop.
- **The two body assertions accept either camera spelling and require exactly
  one per call.** A body that mixes `camera_->setPosition(` and
  `ctx_.camera->setPosition(` is reaching the camera two ways and fails.
  (The two are genuinely disjoint: `ctx_.camera` has no trailing underscore,
  so `"ctx_.camera->setPosition("` does NOT contain `"camera_->setPosition("`.)
- **Part 1's detector greps for both spellings**, so a component-style
  re-duplication is caught.
- One path join: `kHelperTUPath = kGraphicsDir / "player" / kHelperTU`. Every
  other comparison in the file is on the BASENAME, which is why keeping the
  three basenames was worth the constraint — Part 2 needed **zero** edits.

**Proof by deliberate break, as the ruling required.** A temporary
`src/client/graphics/l05_detector_probe_TEMP.cpp` containing a component-style
duplicate (`if (mode == (int)CameraMode::FirstPerson) { ctx_.camera->setPosition(0); ctx_.camera->setTarget(0); }`)
was added, the test run, then the file deleted and the test re-run:

```
--- WITH the probe present -------------------------------------------------
tests/test_first_person_camera_single_formula.cpp:147: Failure
Value of: offenders.empty()
  Actual: false
Expected: true
A second first-person camera formula has appeared. These files both branch on
CameraMode::FirstPerson and write camera_ directly; only renderer_camera.cpp's
repositionFirstPersonCamera() may do that. ...
    /mnt/projects/fable/wt/slot3/src/client/graphics/l05_detector_probe_TEMP.cpp:7
[  FAILED  ] FirstPersonCameraSingleFormula.OnlyTheHelperTUDrivesTheCameraInFirstPerson

--- probe DELETED ----------------------------------------------------------
[==========] 15 tests from 2 test suites ran. (2782 ms total)
[  PASSED  ] 15 tests.
```

The widened detector has teeth against exactly the case the old one had gone
blind to. **Before this unit, that probe file would have passed.**

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,724 -> 1,659 (-65)**;
allowlist lowered to 1,659 in the same commit. `PlayerCameraComponent` takes
**33 of the 34 bodies** (the 34th, `updateObjectLightColors`, relocated
sideways) plus **5 moved inline accessors**, **5 single-family members** and
**1 nested type**. Residual over all three moved TUs: **zero moved body lines
modified, zero added** beyond one `#include` each. Suite 3,652 / 3,653, the
ALSA quirk the only failure. Visual gate PASS with no golden touched — which
for this family, the first-person camera itself, is the proof the move changed
nothing.

**The build was clean on the FIRST compile, with zero errors at any point.**
The pre-build call census (K08 x, scanned with L04 correction 4's
preceding-character rule rather than a non-overlapping regex) named all 15
retarget sites and the compiler named no others.

### Measured vs the plan's lead

| | plan (at `d13f8ddc`) | measured (at `fcda001a`) |
|---|---|---|
| bodies | 34 | **34** (1 physics, 17 collision, 16 camera) |
| single-family members | 3 | **5** |
| forwarders | ~8 | **6** |
| header delta | -30 or better | **-65** |

**The two extra members are `playerMovement_` and `baseVision_`, both invisible
to any body count** — their only other users were inline accessors in
`irrlicht_renderer.h`. K04 correction (f) / L03 correction 2 now for the third
L unit running; it has never once failed to bite.

Members: 44 referenced, **19 on `RendererContext`** (`ctx_.X`), **20 shared and
reached through the transition handle** (`owner_.X_`), **5 moved**. The 20:
`cameraMode_`, `chatInputFocused_`, `collisionManager_`, `currentVision_`,
`doorCollisionData_`, `forcePvsUpdate_`, `objectRegions_`,
`objectWorldTriangles_`, `playerConfig_`, `playerHeading_`, `playerSpawnId_`,
`progressiveLoadStartTime_`, `regionWorldTriangles_`, `terrainOnlySelector_`,
`zoneBoundsMinX_/MaxX_/MinY_/MaxY_/Valid_`, `zoneTriangleSelector_`. Only two
`IrrlichtRenderer` methods that stayed behind are called as `owner_.Foo()`:
`isLoading()` (6 uses) and `rebuildRegionMesh()` (1).

### The nested-type decisions (K05 j / L02 correction 10)

| nested type | verdict |
|---|---|
| `CollisionDebugLine` | **moved** with `collisionDebugLines_`; no user outside the family. |
| `DoorCollisionData` | **stayed**; `doorCollisionData_` is read by `renderer_zone_lifecycle.cpp` and `renderer_zone_load_objects.cpp`. The one moved body that names it (`addDoorToCollision`) qualifies it as `IrrlichtRenderer::DoorCollisionData` — the third substitution class, and the second axis of the friend rule. |
| `enum class CameraMode` | **neither.** `cameraMode_` is shared, so it stayed; but `setCameraMode(CameraMode)` moved, and the component header cannot name `IrrlichtRenderer::CameraMode` without including `irrlicht_renderer.h`, which already includes the component's. **Resolution: hoisted to `include/client/graphics/renderer_types.h`, namespace `EQT::Graphics`, beside `VisionType` — the identical precedent — with `using CameraMode = EQT::Graphics::CameraMode;` left in the class.** Every existing spelling compiles untouched: `EQT::Graphics::IrrlichtRenderer::CameraMode::Follow` at `application_zone_loading.cpp:268,378`, the free functions at `renderer_slash_commands.cpp:126-130`, 13 assertions in `tests/test_irrlicht_camera_command.cpp` and one in `tests/test_zoning_graphics_integration.cpp:729`. It also preserves the literal `CameraMode::FirstPerson` that Part 1 of the single-formula guard greps for — the hoist and the guard widening had to be consistent, and were. |

**This is a new shape for Batch L: a nested type that can move neither with its
family nor stay put.** A member typed by it is shared, so §Mechanism 3 keeps
the member; but a moving signature needs the type nameable without the god
header. Hoisting to `renderer_types.h` + a class-scope `using` alias costs
+16 header lines and −5, changes zero call sites, and is now the recorded
answer for the next L unit that hits it.

### `updateObjectLightColors` — relocated sideways, not taken

It reads `objectLights_`, `weatherEffects_` and `fireEffectsEnabled_` and
**nothing else** — all three `RendererContext` core, none player or camera
state — and its ONE caller is `src/client/graphics/irrlicht_renderer.cpp:427`,
inside `updateTimeOfDay()`. It sat in `renderer_camera.cpp` by accident of the
J11 split. So the 35-line body moved **verbatim** to
`src/client/graphics/irrlicht_renderer.cpp:495`, directly under its caller; the
declaration stayed on `IrrlichtRenderer`; **zero call sites changed.** This is
L04 correction 5 (the `measureSection` case) for the second time, and it is
proved below by an exact diff on the receiving TU (K10 u). L09 decides whether
the lighting family finally takes it.

### Forwarders: 6, each with a named real caller

| forwarder | real caller(s) on an `IrrlichtRenderer` receiver |
|---|---|
| `setPlayerSpawnId` | `src/client/application_zone_loading.cpp:249,359`; `src/client/bridge/irrlicht_bridge.cpp:69,173` |
| `setPlayerPosition` | `src/client/application_zone_loading.cpp:270,380`; `src/client/bridge/irrlicht_bridge.cpp:24` |
| `setCameraMode` | `src/client/application_zone_loading.cpp:268,378`; `tests/test_zoning_graphics_integration.cpp:729` |
| `getCameraModeString` | `tests/test_irrlicht_camera_command.cpp:68,106`; and `src/client/graphics/overlays/overlays_hud_env.cpp:479` reaches it as `owner_.getCameraModeString()`, which the forwarder keeps working unchanged |
| `setSwimmingState` | `src/client/bridge/irrlicht_bridge.cpp:629` |
| `setVisionType` | `src/client/bridge/irrlicht_bridge.cpp:582` |

Plus `PlayerCamera()`, which is scaffolding rather than a forwarder.

**Correction to the plan: `cycleCameraMode` is NOT a forwarder.** The plan
lists it among the four that stay. It has **zero callers on an
`IrrlichtRenderer` receiver**: `src/client/mode/graphical_mode.cpp:148,170`
call it through `std::unique_ptr<output::IOutputRenderer>` and reach
`include/client/output/output_renderer.h:322`'s empty default body
`virtual void cycleCameraMode() {}`, because **`IrrlichtRenderer` has no base
class** (L03 correction 13). Its one real caller is
`renderer_slash_commands.cpp:199`, in the renderer, which now reaches the
component. `getCameraTransform` left the header for the same reason.

**This is the same trap for the third consecutive unit** (L03
`setCurrentTarget`, L04 the four toggles, L05 `cycleCameraMode`). Unlike L04's
it is benign — `/camera cycle` is live, so the implementation IS reached, just
never through `GraphicalMode`.

### §5: nothing deleted. Eight zero-caller methods kept and BOARDED (3 items)

Verified zero callers tree-wide INCLUDING in-family (K03 a), comment- and
string-filtered, receiver-checked: `addRegionToCollision`,
`addDoorToCollision`, `addObjectToCollision`, `findBspRegionForPoint`,
`clearCollisionDebugLines`, `getFirstPersonVerticalFov`, `getCameraTransform`,
`resetVisionToBase`. K12 (ff) checked and clear: **no body in any of the three
TUs contains `pushEvent` or `GameEventType::`** — the family publishes nothing,
so no deletion could have stranded an event.

Three of them are live-feature enable paths, not dead code (L03 correction 5),
and each is now a board item:

- **B045 (A) — the audio listener is never given the camera transform.**
  `getCameraTransform`'s own declaration comment said "for audio listener";
  nothing in `src/client/audio/` ever asks. K15 (qq)'s one-end-only knob.
- **B046 (A) — vision never resets, and vertical swim control has no
  producer.** `resetVisionToBase()` is the only downgrade path and
  `setVisionType()` only upgrades by design, so a vision buff is permanent for
  the session. `setSwimUp()`/`setSwimDown()` are the SOLE writers of
  `playerMovement_.swimUp`/`.swimDown`, which `updatePlayerMovement()` reads
  every frame — a swimming player cannot deliberately ascend or descend.
- **B047 (B) — the incremental collision-registration API has never run, and a
  comment claims it does.** `renderer_zone_draw.cpp:197-198` states collision
  "is set up progressively via `setupMinimalZoneCollision()` and
  `addRegionToCollision()` during the background zone loading phases"; only the
  former is called (`renderer_zone_load_objects.cpp:544`), once.

All eight kept as `PlayerCameraComponent` methods, declarations deleted from
`irrlicht_renderer.h`, **no forwarder written for any** — a forwarder for
surface nothing calls is what this batch exists to remove (L04's shape).
In-family-only helpers made private on the component instead:
`checkMovementCollision`, `checkCollisionIrrlicht`, `findGroundZIrrlicht`,
`findGroundZBspFiltered`, `addCollisionDebugLine`.

### `updatePlayerMovement` (806 lines) moved WHOLE — STOP condition 1 never fired

It needed **no edit beyond the standard substitutions**. It uses no `this` as
an identity key (K06/K07 n), captures nothing in a lambda (K11 dd) and holds no
instance-keyed function-static. The residual below is the proof: 583 old code
lines against 584 new, the difference being one `#include`.

### Predicted vs measured header delta

| | lines |
|---|---|
| plan | -30 or better |
| predicted at the STOP, with L01 correction 3's +3..+6 per construct | **-40** |
| measured | **-65** (1,724 -> 1,659; `git diff --numstat` 79 added / 144 removed) |

The prediction was **conservative by 25 lines**, and the reason is instructive:
it costed the *replacement* comment blocks at L04's "a unit that finds
something pays more" rate and then the findings turned out to be recordable in
the component header rather than in `irrlicht_renderer.h`. 206 lines of
`player_camera_component.h` carry the eight zero-caller notes, the CameraMode
rationale and the basename warning; none of that had to sit in the god header.
**Corollary for the remaining L units: prose about a component belongs in the
component's header, and a delta prediction that budgets it against the god
header will undershoot.**

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping
(`PlayerCameraComponent::` -> `IrrlichtRenderer::`; `ctx_.<name>` ->
`<name>_` for all 19 context members, longest-name-first per K04 (i);
`owner_.` -> empty; `IrrlichtRenderer::DoorCollisionData` ->
`DoorCollisionData`), then trailing `//` comments stripped outside string
literals (K08 w), blank lines dropped, both sides compared as multisets.

`renderer_player_physics.cpp` -> `player/renderer_player_physics.cpp`:
**583 old code lines vs 584 new.**

```
removed (0):
added (1):
  #include "client/graphics/player/player_camera_component.h"
```

`renderer_player_collision.cpp` -> `player/renderer_player_collision.cpp`:
**561 old vs 562 new.**

```
removed (0):
added (1):
  #include "client/graphics/player/player_camera_component.h"
```

`renderer_camera.cpp` -> `player/renderer_camera.cpp`:
**385 old vs 360 new.**

```
removed (26):  -- updateObjectLightColors(), relocated to irrlicht_renderer.cpp
  void IrrlichtRenderer::updateObjectLightColors(float deltaTime) {
  if (objectLights_.empty()) { ... }   [ + 24 more body lines, and 6 closing braces ]
added (1):
  #include "client/graphics/player/player_camera_component.h"
```

**Zero moved body lines modified in any of the three TUs.** The relocation is
proved on the receiving TU too (K10 u): `diff` of
`git show HEAD:src/client/graphics/renderer_camera.cpp` lines 375-409 against
`src/client/graphics/irrlicht_renderer.cpp` lines 495-529 **exits 0** — byte
identical, comments and all.

All three TUs keep `#include "client/graphics/irrlicht_renderer.h"` (they need
the complete type for `owner_.`), so **L01 correction 4's ~60-transitive-include
rediscovery did NOT bite** — L03 correction 4 and L04 correction 3 hold for a
third handle-using component, and this unit needed no include iteration at all.

### Call sites retargeted (15) and the friend line

Computed mechanically before the first build; the compiler named no others.

| file | sites |
|---|---|
| `src/client/graphics/renderer_slash_commands.cpp` | 4 (`buildFirstPersonCameraDumpLines`, `applyCameraProjectionForMode`, `repositionFirstPersonCamera`, `cycleCameraMode`) |
| `src/client/graphics/renderer_entities.cpp` | 2 (`setPlayerRace`, `repositionFirstPersonCamera`) |
| `src/client/graphics/renderer_frame_render.cpp` | 2 (`applyFirstPersonPitchClamp`, `drawCollisionDebugLines`) |
| `src/client/graphics/renderer_zone_load_objects.cpp` | 2 (`setupMinimalZoneCollision`, `extractWorldTriangles` — static, so `PlayerCameraComponent::`) |
| `src/client/graphics/renderer_zone_load_env.cpp` | 2 (`rayIntersectTriangles` — static; `rayTestDoorsAndObjects`) |
| `src/client/graphics/renderer_frame_input.cpp` | 1 (`updatePlayerMovement`) |
| `src/client/graphics/irrlicht_renderer.cpp` | 1 (`findGroundZ`, was `this->findGroundZ`) |
| `src/client/graphics/overlays/overlays_component.cpp` | 1 (`owner_.checkCollisionBspFiltered` -> `owner_.PlayerCamera().checkCollisionBspFiltered`, the form K06/K07 (r) prescribes now that L03 is on `fable`) |

`application.cpp`, `graphical_mode.cpp`, `irrlicht_bridge.cpp` and
`application_zone_loading.cpp` are **untouched** — the K11 rule held with zero
pressure, which is what the six forwarders bought.

**⚠ The event-receiver question the plan flagged is answered: there is no
thread boundary here.** `updatePlayerMovement` is named in
`renderer_event_receiver.h:120`, `camera_controller.h:31,36` and
`graphics_input_handler.cpp:40` **in comments only** — K10 (t)/L03's
`handleMouseTargeting` finding again. Its one real caller is
`IrrlichtRenderer::processFrameInput()` (`renderer_frame_input.cpp:97`), on the
render thread. No back-pointer, no callback, no member-function pointer exists.

`friend class PlayerCameraComponent;` is required on **both** axes of K13 (ii):
20 private `owner_.X_` members and one qualified private nested type
(`IrrlichtRenderer::DoorCollisionData`).

### Text readers of the moved TUs (K03 c / K11 ee)

Greped `tests/`, `tests/cmake/`, `scripts/`, `tools/`, `docs/` and
`CMakeLists.txt` for all three basenames. Only ONE file parses:
`tests/test_first_person_camera_single_formula.cpp` (amended above).
`tests/test_entity_collision_z_offset.cpp:9` and
`tests/test_bridge_queue_race.cpp:149` name `renderer_player_physics.cpp` in
**comments only**, as the plan said — verified, and both pass unchanged.
`tests/cmake/`, `scripts/` and `tools/` have no reader at all; only
`CMakeLists.txt:497,508,509` needed repointing.

### The test (L01 correction 9)

`tests/test_player_camera_component.cpp`, **9 tests, all pass**, registered in
`tests/cmake/renderer_guards.cmake` next to `test_diagnostics_component` (same
`IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` guard, same bare never-`Init()`'d
`IrrlichtRenderer` idiom). Both binding paths are pinned by MUTATION through
public accessors only (L01 correction 6):

- `SetPlayerPositionWritesTheCoreMembersThroughTheContext` and
  `ContextWriteIsSeenByTheComponentsReader` — the `ctx_` path, both directions:
  component write -> `Context()` read, and `Context()` write -> renderer read.
- `SetCameraModeWritesTheRenderersOwnMemberThroughTheOwnerHandle`,
  `CycleCameraModeAdvances...`, `CameraModeStringReadsTheOwnersMemberBack...` —
  the `owner_` path. The cycle test matters: `cycleCameraMode` has no forwarder,
  so this is now its only test route.
- `SwimmingStateRoundTrips...`, `VisionOnlyUpgradesAndResetsThroughTheComponent`
  — the two moved members, and the second of those is the executable statement
  of B046: vision goes up through the bridge forwarder and comes back down only
  via a method nothing in production calls.
- `AccessorReachesAConstructedComponent`,
  `BridgeAndApplicationForwardersReachTheComponent` — the ctor wiring and the
  remaining forwarders.

**One deviation inside the test, recorded:**
`SetPlayerPositionWritesTheCoreMembersThroughTheContext` sets `CameraMode::Free`
before calling. In the default `Follow` mode the moved body's next act is
`ctx_.cameraController->enableZoneInLogging()`, an **unguarded dereference** of
a `unique_ptr` only `Init()` fills; the body guards that path with
`isLoading()` alone. The first suite run SEGFAULTed there. The body is verbatim
from before this unit (the residual proves it) and the old code had the same
shape, so this is a property of the harness, not a regression — K09 correction
(z) read forward: an unguarded dereference tells you which paths the
never-`Init()`'d idiom can actually reach. Not boarded; production always
arrives through `isLoading()`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0, FIRST invocation, zero compile errors at any point.** No 600 s timeout, so no truncation scan was needed; `build/bin/willeq`, `build/bin/test_player_camera_component` and `build/lib/libwilleq_core.a` all present and valid |
| `ctest --test-dir build -j6 --output-on-failure` | **3,652 / 3,653 passed**, 17 skipped, **1 failure: `AudioManagerTest.PreloadCommonSounds`** — the only permitted one. (First run had 2: my new test's segfault, fixed above.) |
| the four named gates | `test_first_person_camera_single_formula` **15/15**, `test_first_person_body_draw_rule`, `test_entity_collision_z_offset`, `test_bridge_queue_race` all pass **unchanged**. Re-run together with the hygiene gates: **41/41 passed** — `FileSizeCeiling`, `DocCitations`, `ReachabilitySweep`, `DuplicateStateStores`, `CameraCommandTest` included |
| `test_player_camera_component` (new) | **9 tests, all pass** |
| `scripts/build-gles2-x86.sh` | **exit 0**, zero errors; `build-gles2/bin/willeq` and `gles2_zone_benchmark` both valid ELF |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), 3 zones + 3 memory-coverage runs, every CPU p95 under ceiling. `git status --porcelain` shows **no golden path** — nothing re-blessed. **This family IS the first-person camera; the gate is the proof the move changed no pixels.** |
| `scripts/build-arm-noble.sh` | **exit 0**, zero errors, `build-arm-noble/bin/willeq` valid ARM ELF |
| `scripts/refactor/check_file_sizes.sh --all` | **844 checked, 844 ok, 0 warn, 0 FAIL**, with `irrlicht_renderer.h` lowered 1,724 -> 1,659 |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,724 | **1,659** (-65) |
| `src/client/graphics/renderer_player_physics.cpp` -> `src/client/graphics/player/renderer_player_physics.cpp` | 839 | 840 |
| `src/client/graphics/renderer_player_collision.cpp` -> `src/client/graphics/player/renderer_player_collision.cpp` | 742 | 743 |
| `src/client/graphics/renderer_camera.cpp` -> `src/client/graphics/player/renderer_camera.cpp` | 571 | 534 |
| `src/client/graphics/irrlicht_renderer.cpp` | 908 | 955 (+47: `updateObjectLightColors` + its 12-line note) |
| `include/client/graphics/renderer_types.h` | — | +16 (the `CameraMode` hoist) |
| `include/client/graphics/player/player_camera_component.h` | — | 206 (new) |
| `tests/test_player_camera_component.cpp` | — | 158 (new) |

### Corrections to the Batch L template

**21. Text-matching guards must learn the component spelling, and be re-proved
with a deliberate break.** This is the correction the ruling asked for and the
most portable thing this unit found. A guard that scans source text for
`IrrlichtRenderer::` or for `foo_` is measuring the PRE-Batch-L spelling of the
code it protects. Every such guard silently stops guarding the moment its
family becomes a component — it does not fail, it passes vacuously, which is
strictly worse. Two rules follow:
   * **Before planning a move, grep the pinning test for the CLASS NAME and for
     every member the mechanism is about to rewrite, not only for the
     filename.** L03 and L04 both matched on basenames alone, so "keep the
     basename, repoint the path" worked twice and the batch generalised it into
     a false rule. L05's guard matched on `void IrrlichtRenderer::…` and on
     `camera_->`, and the plan did not look.
   * **A widened guard is not widened until a deliberate break proves it.**
     Adding a spelling to a matcher is a one-character-class change that is
     trivially wrong in a way no green run detects. Write the offending file,
     watch the test fail with a file:line, delete it, watch it pass. Both
     outcomes go in the Review.
   A census of the remaining text-scanning guards against Batch L's
   substitutions belongs in L12, not in whichever unit trips over one.

22. **A nested type can be un-moveable in both directions, and the answer is to
   hoist it to `renderer_types.h` with a class-scope `using` alias.**
   `CameraMode`'s member is shared (so §Mechanism 3 keeps it on the renderer)
   while its signature moves (so the component must name the type without the
   god header). Neither K05 (j) branch — "move it with the member" or "qualify
   it" — applies. The hoist changes zero call sites because
   `IrrlichtRenderer::CameraMode` stays a valid name, and it costs ~16 lines.
   `VisionType` in the same header is the precedent to point at.

23. **Prose about a component belongs in the component's header.** L05
   predicted −40 and landed −65 because it budgeted its findings' comment cost
   against `irrlicht_renderer.h` and then wrote them in
   `player_camera_component.h` instead. L04 correction's "a unit that finds
   something pays more" is right about the volume and wrong about the file.

24. **The `IOutputRenderer` name-collision trap has now cost three consecutive
   units a false forwarder.** L05 makes it `cycleCameraMode`. Every remaining L
   plan should DROP the forwarder column from its table entirely and require
   the executor to derive it from a receiver-aware caller scan — the plan's
   guess has been wrong every time and its cost is a forwarder that keeps dead
   surface alive in the header this batch exists to shrink.

25. **A clean first compile is achievable on a 2,150-line move.** L05 built
   with zero errors on the first invocation, and no build in the unit hit the
   600 s ceiling. The two things that bought it: L04 correction 4's
   preceding-character call scan (not a non-overlapping regex), and applying
   substitutions to CODE LINES ONLY with string literals protected — the moved
   TUs' comments still quote `playerX_`, `playerZ_` and `movementCallback_`,
   and a naive whole-file regex would have rewritten five historical notes
   (K01 correction 4).
