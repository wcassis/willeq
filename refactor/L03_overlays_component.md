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
