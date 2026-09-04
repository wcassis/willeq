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
