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

`IOutputRenderer` virtual overrides: `setPlayerSpawnId`, `setPlayerPosition`
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
