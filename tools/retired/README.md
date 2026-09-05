# Retired tools

⚠ One of the six below (`generate_textures.cpp`) was retired on a rationale
later shown to be false — see its bullet and board B247.

These six top-level tools were retired per Will's decision (2026-07-28,
documented in `docs/decisions.md`), executed by `refactor/P26_phase0_gles2_links.md`
(Phase 0 of `refactor/desktop_gl_elimination_plan.md`). All six shared the
same Class-B mechanical link bug (missing explicit `EGL/GLESv2/drm/gbm` link
set against the GLES2-patched static Irrlicht archive — see the plan's §2
Class B and §3 tools table) but had no live usage found beyond old bug
reports, so rather than porting the link fix they were retired outright.
Their CMake targets have been removed; the sources below are kept for
reference only and are **not built**. Git history (pre-P26) has the last
buildable version of each, including its CMake target block, if one is ever
needed again.

- **`uv_analysis.cpp`** — character model UV wrapping analysis, used to
  assess feasibility of composite entity atlases (superseded by
  `entity_composite_builder`, which is still maintained).
- **`analyze_hum_model.cpp`** — ad hoc diagnostic dump of a specific
  human-race model's geometry/animation data during early model-loading
  debugging.
- **`test_animation.cpp`** — standalone manual harness for exercising
  character animation playback outside the full client; superseded by
  `test_graphics`/`test_asset_loading` GTest coverage.
- **`bsp_coordinate_finder.cpp`** — diagnostic tool for working out BSP tree
  coordinate system conventions during zone-geometry development.
- **`extract_zone_lines.cpp`** — one-off extraction of zone-line bounding
  boxes to JSON; superseded by `zone_line_editor`, which is still
  maintained.
- **`generate_textures.cpp`** — offline procedural texture generator.
  ⚠ **Its retirement rationale, "no found consumer of its output anywhere in
  the tree or docs", is now known to be WRONG** (T400, board B247). T398
  established that the 64 BMPs under `data/textures/` — which this tool
  wrote — are 64 of the 67 entries in the shared `willeq.eqe1` pack, read on
  every zone load. Retiring the producer while its output kept shipping is
  what let `particle_atlas.bmp` sit for a year as a 24-bit, alpha-less file
  that made every particle in the game a flat opaque square (board B225).
  T400 replaced the particle-atlas half with `tools/particle_atlas_builder`
  (a live target, pinned by `test_particle_atlas_alpha`); the other 63 BMPs
  still have no build recipe. See B247 before assuming anything here is
  dead.

All six are referenced only in one prior bug report
(`refactor/F04_ucs_guild_gm.md`, noting they failed to link — unrelated to
that unit) with no other live usage instructions found
(`refactor/desktop_gl_elimination_plan.md` §3).
