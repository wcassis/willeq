# Retired tools

⚠ A sixth tool, `generate_textures.cpp`, used to be listed here. It was
retired on a rationale later shown to be FALSE and has been **un-retired**
(T403, board B260): it is now the live target `tools/generate_textures.cpp`,
the build recipe for all 64 BMPs under `data/textures/`. See the note at the
bottom of this file.

These five top-level tools were retired per Will's decision (2026-07-28,
documented in `docs/decisions.md`), executed by `refactor/P26_phase0_gles2_links.md`
(Phase 0 of `refactor/desktop_gl_elimination_plan.md`). All of them shared the
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

All five are referenced only in one prior bug report
(`refactor/F04_ucs_guild_gm.md`, noting they failed to link — unrelated to
that unit) with no other live usage instructions found
(`refactor/desktop_gl_elimination_plan.md` §3).

## The one that came back: `generate_textures`

P26 retired it on the recorded rationale *"no found consumer of its output
anywhere in the tree or docs"*. **That rationale was wrong.** T398 established
that the 64 BMPs under `data/textures/` — which this tool writes — are 64 of
the 67 entries in the shared `willeq.eqe1` pack, read on every zone load.
Retiring the producer while its output kept shipping is what let
`particle_atlas.bmp` sit for a year as a 24-bit, alpha-less file that made
every particle in the game a flat opaque square (board B225), because
`src/client/graphics/texture_generators.cpp` — the 926-line TU that defines
every one of those textures — compiled in **no CMake target at all**.

T403 restored it as `tools/generate_textures.cpp` (target `generate_textures`,
`EQT_GRAPHICS` only) and pinned it: `scripts/build_textures.sh --check`, run
by the `TexturesReproducible` ctest test, regenerates all 64 into a temp
directory and asserts they match `data/textures/`. 63 of 64 were
byte-identical at T403; `detail_atlas.bmp` was exempt and hash-pinned only
(board B265). **T427 closed B265** by regenerating it — the exemption list is
EMPTY now and the check reports `65 byte-identical, 0 drifted, 0 exempt`.

**The lesson for this directory**: "no found consumer" is a claim about the
CODE, and these tools produce DATA. Before retiring a tool that writes a file,
grep for the FILE, not just the tool.
