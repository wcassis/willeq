# L04 — `DiagnosticsComponent` (family: `renderer_diagnostics.cpp` + `renderer_debug.cpp` — memory reports, scene dumps, profiling, debug toggles)

Batch: `refactor/L_renderer_component_extraction.md`. Wave 1, three-wide
with L02/L03. Measured at `cfbbde57`: 1,735 lines, 28 bodies, 1
single-family member (`frameTimings_`/`frameTimingsAccum_` are
nested-typed — K05 j — measure), reads 40+ core members through the context
(read-mostly; this is the breadth test of `RendererContext`).

⚠ Forwarders that stay (count them): the `IOutputRenderer` overrides
`toggleHUD`, `toggleNameTags`, `toggleWireframe` (implemented by
`graphical_mode.cpp` callers via the interface); `dumpScene` and
`getMemoryReport` are called from `irrlicht_bridge.cpp` (bridge rule) and
named in `event_bus.h` (a comment — read it); `runSceneProfile` from
`application.cpp`. Everything called only from `renderer_slash_commands.cpp`
(L06, still on `IrrlichtRenderer`) and `renderer_frame*.cpp` retargets to
`diagnostics_->X()`.

⚠ `scripts/ci/test_eqe1_totalestimate_accounting.py` names
`getMemoryReportBreakdown()` and `renderer_diagnostics.cpp` in comments —
read it; if it parses the TU, repoint (K03 c). `isUsingOldModels` is named
in `race_model_loader.h` (a comment or a callback — read).

Zero-external-caller methods (`getMemoryReportBreakdown`,
`swapZoneMeshMaterials`, `toggleFog`, `toggleOldModels`) — in-family or dead;
§5 + K12 (ff) (`dumpScene` publishes `DiagnosticsSceneDump`? — check every
candidate for `pushEvent`).

Predicted header delta: −28 declarations −(1..3) members −banners
+(forwarders ≈ 6) +(scaffolding 4..6) ≈ **−20 or better**.

Territory: `include/client/graphics/diagnostics/diagnostics_component.h`
(new), `src/client/graphics/diagnostics/` (git mv of the two TUs),
`irrlicht_renderer.h`, `renderer_init.cpp` (ctor line), root `CMakeLists.txt`,
`tests/test_diagnostics_component.cpp` + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the renderer TUs,
`irrlicht_bridge.cpp`, `application.cpp`, `graphical_mode.cpp`. Visual gate
+ ARM NOT skippable. L02/L03 share `irrlicht_renderer.h`, `renderer_init.cpp`,
`renderer_frame_*.cpp`, `renderer_slash_commands.cpp` at other lines.
