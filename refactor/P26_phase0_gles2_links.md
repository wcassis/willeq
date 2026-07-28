# P26 — Desktop-GL elimination Phase 0: link fixes + tool retirement

**Slot: slot1. x86 only — no Pi, no eqemu.** Sibling: P25 (slot2,
X11-EGL backend, touches docker/irrlicht-drm-x86 + its own build
wiring) — if you both touch `CMakeLists.txt`, keep to your own target
blocks; the orchestrator resolves any merge overlap.

Executes Phase 0 of `refactor/desktop_gl_elimination_plan.md` (read it
first — §4 Phase 0 and the §3 tools table), amended by Will's decision
(2026-07-28): the six retire-candidate tools are RETIRED, not fixed —
"can be ignored/archived".

## Work

1. **Retire six tools**: `uv_analysis`, `analyze_hum_model`,
   `test_animation`, `bsp_coordinate_finder`, `extract_zone_lines`,
   `generate_textures`. Remove their CMake targets; move their
   top-level sources from `tools/` to `tools/retired/` with a short
   `tools/retired/README.md` (what each did, retired per decisions.md
   2026-07-28, git history has the buildable versions). Do not delete
   shared sources other targets still compile.
2. **Fix the remaining 8 Class-B targets** (5 kept tools:
   `model_viewer`, `generate_textures`→retired so NOT it —
   the kept five per the plan: `model_viewer`, `entity_composite_builder`,
   `generate_surface_map`, `zone_line_editor`, `s3d_dump`; 3 tests:
   `test_asset_loading`, `test_door_loading`, `test_graphics`): add the
   explicit backend link set the plan prescribes so each links in BOTH
   the desktop tree and the GLES2 tree (`EQT_GLES2_IRRLICHT_DIR` +
   `EQT_BUILD_TESTS=ON`). Follow the plan's prescription; where it
   matches the existing conditional idiom (APPLE/EQT_DRM/EQT_GLES2
   branches used by model_viewer etc.), keep that idiom.
3. **Update the roadmap doc**: mark Phase 0 executed, record Will's
   retirement decision in the §3 table, adjust the ctest-registration
   expectations (the 3 test binaries now build in the GLES2 tree; the
   retired tools drop out of both trees' target counts).
4. Also update the two stale references the retirement creates, if
   any: grep scripts/, docs/, CLAUDE.md files for the six retired tool
   names; fix or annotate (do not rewrite historical refactor/ docs —
   they're banner-historical).

## Verification

- Desktop tree: full 6-step bar (known ctest exceptions per protocol).
- GLES2 tree: configure `EQT_GLES2_IRRLICHT_DIR` + tests ON, build
  `-k`, and run ctest — the 3 previously-NOT_BUILT test binaries now
  register and their ~120 cases run. Expected remaining GLES2-tree
  failures: the known host exceptions plus the DOCUMENTED Phase-1
  blocker `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`
  (segfault — do NOT fix or investigate it, it's the next unit; just
  confirm it's still the only genuine failure).
- Report both trees' final ctest numbers explicitly.

## Rules

- Territory: `CMakeLists.txt` (tool/test target blocks only),
  `tools/` moves, the roadmap doc, small doc-reference fixes. No
  production src/ changes, no driver files, no goldens.
- STOP: a "mechanical" link fix turns out to need source changes in a
  target (report which and why instead).

## Review

(executor fills in: per-target link additions, retirement moves, both
trees' ctest numbers, doc updates, deviations)
