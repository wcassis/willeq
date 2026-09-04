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

## Review — STOPPED (STOP condition 2), no code moved

Executed in `wt/slot3` on `task/L05-player-camera-component`, branched from
`fable` at `fcda001a`. **The unit did not execute.** The plan's premise that
`tests/test_first_person_camera_single_formula.cpp` needs only its PATH
CONSTANTS repointed is FALSE as measured at this tip, so STOP condition 2 —
"the single-formula test's assertion would have to change" — fires before the
first `git mv`. Everything else in the plan re-measured clean; the survey below
is complete enough that a re-dispatch with a resolution can land in one pass.

### The STOP, exactly

Part 3 of the test, `FirstPersonCameraSingleFormula.TheOneBodyAppliesBothTunablesAndMouseLookPitch`,
does not merely locate a file. It locates a BODY by its fully-qualified
definition line and then asserts on the text of that body:

- `tests/test_first_person_camera_single_formula.cpp:194`
  `readFile(kGraphicsDir / kHelperTU)` — this is the path constant the plan
  anticipated; a `/ "player"` insert fixes it, and it is the ONLY sanctioned
  edit.
- `tests/test_first_person_camera_single_formula.cpp:197`
  `lines[i].find("void IrrlichtRenderer::" + kHelperName + "()")` — the
  moved body is `void PlayerCameraComponent::repositionFirstPersonCamera()`.
  The literal `"void IrrlichtRenderer::"` must change or the following
  `ASSERT_LT(start, lines.size())` fails. **This is not a path constant.**
- `tests/test_first_person_camera_single_formula.cpp:243-244`
  `EXPECT_NE(body.find("camera_->setPosition("), std::string::npos)` and the
  same for `camera_->setTarget(`. `camera_` is a `RendererContext` core member
  (5-9 families), so §Mechanism 3 substitutes it to `ctx_.camera`, and
  `ctx_.camera->setPosition(` does not contain the asserted substring. Both
  EXPECT_NEs break. They survive ONLY if `camera_` is reached as
  `owner_.camera_` instead — `owner_.camera_->setPosition(` does contain
  `camera_->setPosition(` — which is a deliberate violation of §Mechanism 3
  adopted purely to satisfy a text assertion.

### The deeper finding: Part 1's detector goes blind on the first component

`FirstPersonCameraSingleFormula.OnlyTheHelperTUDrivesTheCameraInFirstPerson`
(lines 105-127) flags any file under `src/client/graphics` that both mentions
`CameraMode::FirstPerson` and contains `camera_->setPosition(` /
`camera_->setTarget(`. That detector is written against the pre-Batch-L
spelling. The moment ANY renderer family reaches the camera as `ctx_.camera`,
a re-duplicated first-person formula written in component style is invisible
to it — the test keeps passing while the invariant it exists to defend is
unguarded. This is the T295/T300 regression net, four duplications deep in its
own recorded history, and Batch L erodes it silently. **It is not a defect this
structural unit may fix by choosing a substitution; it is a decision.**

### What did NOT block (all re-measured at `fcda001a`)

| check | result |
|---|---|
| `updatePlayerMovement` (806 lines, `renderer_player_physics.cpp:31`) | clean under the four substitutions. **No `this`, no lambda capture, no function-static keyed on the instance** — K06/K07 (n) and K11 (dd) never arise. STOP condition 1 does NOT fire. |
| Part 2 of the test (`TunablesAreReferencedFromOneTUOnly`) | uses `path.filename().string()` only; `graphicsSources()` is a `recursive_directory_iterator`. Keeping the basenames under `player/` satisfies it with zero edits. |
| `test_entity_collision_z_offset.cpp:9`, `test_bridge_queue_race.cpp:149` | name `renderer_player_physics.cpp` in COMMENTS only, as the plan said. Verified. |
| `tests/cmake/`, `scripts/`, `tools/` | no reader of any of the three filenames. The only build-side sites are `CMakeLists.txt:497,508,509`. |
| K12 (ff) publisher check | **no `pushEvent` and no `GameEventType::` in any of the three TUs.** Nothing that could be deleted is a sole publisher. |

### Measured survey (so a re-dispatch need not redo it)

**34 bodies**, as the plan predicted: 1 in `renderer_player_physics.cpp`
(`updatePlayerMovement`), 17 in `renderer_player_collision.cpp`, 16 in
`renderer_camera.cpp`. `include/client/graphics/irrlicht_renderer.h` is
**1,724** lines at this tip (allowlist line 2 agrees).

**Members: 44 referenced, 19 of them `RendererContext` core.** The 25
non-core divide as:

- **Single-family, MOVE (5):** `baseVision_`, `collisionDebugLines_` (with its
  private nested `struct CollisionDebugLine`, named nowhere else in the tree),
  `playerMovement_`, `playerPitch_`, `useIrrlichtCollision_`. The plan
  predicted 3; the extra two are `playerMovement_` and `baseVision_`, both
  **invisible to any body count because their only other users are inline
  accessors in the header** — K04 correction (f) / L03 correction 2 for the
  third unit running.
- **Shared, stay as `owner_.X_` (20):** `cameraMode_`, `chatInputFocused_`,
  `collisionManager_`, `currentVision_`, `doorCollisionData_`,
  `forcePvsUpdate_`, `objectRegions_`, `objectWorldTriangles_`,
  `playerConfig_`, `playerHeading_`, `playerSpawnId_`,
  `progressiveLoadStartTime_`, `regionWorldTriangles_`, `terrainOnlySelector_`,
  `zoneBoundsMinX_/MaxX_/MinY_/MaxY_/Valid_`, `zoneTriangleSelector_`.

**Inline accessors that move with their members (5), all with ZERO callers
tree-wide:** `getBaseVision()` (on `baseVision_`), and `isSwimming()`,
`setSwimUp()`, `setSwimDown()`, `setLevitating()` (on `playerMovement_`).
`setSwimUp`/`setSwimDown` are the **sole writers** of
`playerMovement_.swimUp`/`.swimDown`, which `updatePlayerMovement` reads live —
L03 correction 17's shape exactly, so they are keeps, not §5 deletes, and the
finding is boardable: **vertical swim control has no producer.**
`isFirstPersonMode()` and `getCameraMode()` read `cameraMode_`, which stays, so
they are not part of the move.

### The nested-type decisions (K05 j / L02 correction 10)

| nested type | verdict |
|---|---|
| `CollisionDebugLine` | **moves** with `collisionDebugLines_`; no user outside the family. |
| `DoorCollisionData` | **stays**; `doorCollisionData_` is read by `renderer_zone_lifecycle.cpp` and `renderer_zone_load_objects.cpp`. Qualify as `IrrlichtRenderer::DoorCollisionData` if a moved signature names it. |
| `enum class CameraMode` (`irrlicht_renderer.h:376`) | **can move neither way.** `cameraMode_` is shared (`renderer_entities.cpp:35`, `renderer_frame_input.cpp:376,378`, two header inlines) so it stays; but `setCameraMode(CameraMode)` moves, and a component header cannot name `IrrlichtRenderer::CameraMode` without including `irrlicht_renderer.h`, which already includes the component header. **Resolution measured and recommended: hoist the enum to `include/client/graphics/renderer_types.h` (namespace `EQT::Graphics`, next to `VisionType`, which is the identical precedent) and leave `using CameraMode = EQT::Graphics::CameraMode;` in the class.** Every existing spelling then still compiles untouched — `IrrlichtRenderer::CameraMode::Follow` at `application_zone_loading.cpp:268,378`, `renderer_slash_commands.cpp:126-130`, `tests/test_irrlicht_camera_command.cpp` (13 assertions) and `tests/test_zoning_graphics_integration.cpp:729` — and the literal `CameraMode::FirstPerson` that Part 1 of the single-formula test greps for is preserved verbatim. This is NOT the blocker; it is recorded because the plan asked the question. |

### `updateObjectLightColors` — decided: relocate sideways, do not take it

It reads `objectLights_`, `weatherEffects_` and `fireEffectsEnabled_` and
**nothing else** — all three are `RendererContext` core, none is player or
camera state. Its single caller is `src/client/graphics/irrlicht_renderer.cpp:427`.
It sits in `renderer_camera.cpp` by accident of history. This is L04
correction 5's case (`measureSection`) exactly: **relocate the 32-line body
verbatim to `src/client/graphics/irrlicht_renderer.cpp`, leave the declaration
on `IrrlichtRenderer`, change zero call sites**, and let L09 decide whether the
lighting family takes it. It must NOT become a `PlayerCameraComponent` method.

### Forwarders: 6, not the plan's ~8 — with the real caller of each

| forwarder | real caller(s) on an `IrrlichtRenderer` receiver |
|---|---|
| `setPlayerSpawnId` | `src/client/application_zone_loading.cpp:249,359`; `src/client/bridge/irrlicht_bridge.cpp:69,173` |
| `setPlayerPosition` | `src/client/application_zone_loading.cpp:270,380`; `src/client/bridge/irrlicht_bridge.cpp:24` |
| `setCameraMode` | `src/client/application_zone_loading.cpp:268,378`; `tests/test_zoning_graphics_integration.cpp:729` |
| `getCameraModeString` | `tests/test_irrlicht_camera_command.cpp:68,106`; and `src/client/graphics/overlays/overlays_hud_env.cpp:479` reaches it as `owner_.getCameraModeString()` (becomes `owner_.PlayerCamera().getCameraModeString()`) |
| `setSwimmingState` | `src/client/bridge/irrlicht_bridge.cpp:629` |
| `setVisionType` | `src/client/bridge/irrlicht_bridge.cpp:582` |

**`cycleCameraMode` is NOT a forwarder — it has zero callers on an
`IrrlichtRenderer` receiver.** The plan lists it among the four that stay. Its
apparent callers are `src/client/mode/graphical_mode.cpp:148,170`, which call
through `std::unique_ptr<output::IOutputRenderer>` and therefore reach
`output_renderer.h:322`'s empty default body `virtual void cycleCameraMode() {}`
— `IrrlichtRenderer` has no base class (L03 correction 13), so control never
arrives here. Its only real caller is `renderer_slash_commands.cpp:199`, in the
renderer, which retargets. This is **L04 correction 2's trap for the second
time**, and unlike L04's it is benign: `/camera cycle` is a live path, so the
implementation is reached — just never through `GraphicalMode`.

### Zero-caller methods (8) — all keeps, all boardable, none deleted

Verified zero callers tree-wide INCLUDING in-family (K03 a), comment- and
string-filtered, receiver-checked: `addRegionToCollision`, `addDoorToCollision`,
`addObjectToCollision`, `findBspRegionForPoint`, `clearCollisionDebugLines`,
`getFirstPersonVerticalFov`, `getCameraTransform`, `resetVisionToBase`.
(`renderer_zone_draw.cpp:198,1055` mention `addRegionToCollision` in prose only,
and the plan's in-renderer-caller list for it is wrong at this tip.)

Two of these are live-feature enable paths, not dead code, and §5 must not
silently take them (L03 correction 5):

- **`getCameraTransform()` — the audio listener is never given the camera
  transform.** The declaration's own comment says "Get camera position and
  orientation for audio listener"; nothing calls it. 3D audio positioning is
  wired at one end only (K15 qq).
- **`resetVisionToBase()` — vision never returns to the racial base.**
  `setVisionType()` is live (`irrlicht_bridge.cpp:582`) and only ever UPGRADES;
  the sole downgrade path has no caller, so an Ultravision buff is permanent
  for the session.

The other six are the progressive-collision registration API
(`addRegionToCollision`/`addDoorToCollision`/`addObjectToCollision`/
`findBspRegionForPoint`) plus two helpers. `renderer_zone_draw.cpp:197-198`
asserts in a comment that collision "is set up progressively via
setupMinimalZoneCollision() and addRegionToCollision() during the background
zone loading phases" — **only `setupMinimalZoneCollision()` is actually called**
(`renderer_zone_load_objects.cpp:544`). The comment documents a mechanism that
does not run.

Recommendation for all eight: L04's shape — keep the implementations, move them
into `PlayerCameraComponent` (public for the API, private for the in-family
helpers), delete the declarations from `irrlicht_renderer.h`, write no
forwarder, and board every finding.

In-family-only (private on the component, no forwarder, not §5 candidates):
`checkMovementCollision`, `checkCollisionIrrlicht`, `findGroundZIrrlicht`,
`findGroundZBspFiltered`, `addCollisionDebugLine`.

### Methods that stay behind and are reached as `owner_.X()`

Only two, both from `renderer_camera.cpp` / `renderer_player_collision.cpp`:
`isLoading()` (6 uses) and `rebuildRegionMesh()` (1 use). The scan used L04
correction 4's preceding-character rule rather than a non-overlapping regex, so
`if (isLoading())` was not swallowed.

### Predicted header delta (unchanged by the STOP)

−27 declarations (34 bodies − 6 forwarders − `updateObjectLightColors`, which
stays), −5 inline accessors, −8 member lines, −5 for the `CameraMode` enum block
(+1 for the `using` alias), −~15 banner lines that travel with the declarations
(L02 correction 11), +6 forwarders, +~15 lines of comment recording the findings
above (L04's "a unit that finds something pays more"), +17 scaffolding (include
+1, `PlayerCamera()` +4, `playerCamera_` +5, `friend class PlayerCameraComponent`
+7) ⇒ **≈ −40**, comfortably better than the plan's −30. Friendship is required
on BOTH axes of K13 (ii): 20 `owner_.X_` members and `IrrlichtRenderer::DoorCollisionData`.

### The three resolutions, with their costs — for the orchestrator

1. **Amend the test (smallest, and the honest one).** Change the `:197` literal
   to `"void PlayerCameraComponent::"` and the two `:243-244` body assertions
   from `camera_->` to `ctx_.camera->`, and generalise Part 1's detector to
   match `camera_->set…(` OR `ctx_.camera->set…(` so it keeps its teeth against
   component-style code. The invariant is unchanged; only its spelling moves
   with the code it guards. Cost: ~6 lines in one test. **This is a plan
   amendment Will or the orchestrator must sanction, because it is exactly what
   the dispatch forbade an executor to do unilaterally.**
2. **Substitute `camera_` to `owner_.camera_` throughout the family**, keeping
   the two `:243-244` assertions matching by substring, and change only the
   `:197` class literal. Cost: 1 test line, but a standing §Mechanism 3
   violation on a core member, adopted to please a text matcher — and Part 1's
   detector still goes blind for every OTHER family. Not recommended.
3. **Split `renderer_camera.cpp`**: leave `repositionFirstPersonCamera()` and
   `applyCameraProjectionForMode()` on `IrrlichtRenderer` in a
   `src/client/graphics/renderer_camera.cpp` that does not move, and take the
   rest. Zero test edits (Part 3's path join then needs no `player/` insert
   either). Cost: the camera half of the extraction does not happen, the
   component cannot own the camera, and L05 becomes a physics/collision unit
   with a camera remainder to re-plan. Recommended only if 1 is refused.

### Corrections to the Batch L template

1. **A text-reading test can pin a BODY, not just a file.** L03's and L04's
   text readers matched on basenames, so "keep the basename and repoint the
   path" was sufficient twice and the batch generalised it. This one matches on
   `"void IrrlichtRenderer::" + name + "()"` and on `camera_->` — the class name
   and the MEMBER SPELLING. **Before planning a move, grep the pinning test for
   the class name and for the members the mechanism is about to rewrite, not
   only for the filename.** Had this plan done so it would have carried a
   resolution instead of a STOP.
2. **§Mechanism 3's `ctx_.` substitution is itself observable to the text
   tests.** Every invariant guarded by grepping for `foo_` in
   `src/client/graphics` weakens the moment a family becomes a component. A
   census of such tests belongs in the L12 close-out, not in whichever unit
   trips over one.
3. **`IOutputRenderer`'s empty default bodies have now hidden a false forwarder
   premise in three consecutive units** (L03 `setCurrentTarget`, L04 the four
   toggles, L05 `cycleCameraMode`). Every remaining L plan should drop the
   forwarder column from its table and let the executor derive it.
