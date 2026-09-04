# L06 — `SlashCommandsComponent` (family: `renderer_slash_commands.cpp`, one 593-line body `processSlashCommand`)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–23 apply).
Wave 2, two-wide with L07 (disjoint TUs; both edit `irrlicht_renderer.h`,
`renderer_init.cpp`, the allowlist and `module_map.md` at their own lines).
Measured at `1ea4a59c`.

One body, `processSlashCommand`, moves WHOLE (Batch G decides its
decomposition). It is a dispatcher: it reaches `diagnostics_` (13),
`playerCamera_` (4), `zoneShader_`, `eventReceiver_`, `bridge_` and the UI
panel state members `spellbookState_` (10), `skillsPopupState_` (5),
`inventoryState_`, `lootState_`, `uiLayout_`, `newUIEnabled_`,
`eq_commands_`. The panel-state members are the FRAME family's (L11 — the
24 UI members) — reach them as `owner_.X_` and record each; do not move
them. Sibling components are `owner_.Diagnostics().X()` etc. (K06/K07 r).
`getSimWorkerDebugInfo` is L07's — if L07 has merged by your tip, write
`owner_.SimBridge().getSimWorkerDebugInfo()`; else `owner_.getSimWorkerDebugInfo()`.

Callers: `processSlashCommand` is called from the frame family
(`renderer_frame_input.cpp` — measure) and possibly the bridge; the caller
retargets to `slash_->processSlashCommand(...)`. Zero forwarders expected
unless a bridge/application caller exists — name it or there is none
(correction 23).

Text readers: `tests/cmake/commands.cmake` defines
`RENDERER_COMMAND_SOURCE=…/renderer_slash_commands.cpp` — repoint;
`tests/test_merged_help.cpp:359` and `tests/test_player_camera_component.cpp:58`
name it in comments — verify; `test_merged_help` may READ the TU for the
help table (read it before moving).

Predicted header delta: −1 declaration −banner +(scaffolding 4..6) ≈ **−2
or worse** — this unit is not about the header; it is about the 593-line
body leaving the god class. Report the measured number; a small positive
delta is acceptable here ONLY if the body moved (say so, and ratchet to
the measured count — the allowlist may not go up; if it would, STOP and
report the arithmetic).

Territory: `include/client/graphics/slash/slash_commands_component.h`
(new), `src/client/graphics/slash/` (git mv), `irrlicht_renderer.h`,
`renderer_init.cpp` (ctor line), root `CMakeLists.txt`, `tests/cmake/commands.cmake`
(the define), `tests/test_slash_commands_component.cpp` + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the caller(s). Visual gate +
ARM NOT skippable.
