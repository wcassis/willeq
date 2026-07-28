# P24 — Desktop-GL elimination survey + migration plan

**Slot: slot1. x86 only — no Pi, no eqemu. REPORT/PLAN unit: no
production code changes.** Sibling: P25 (slot2, X11-EGL window backend
in docker/irrlicht-drm-x86/) — do not touch its territory.

Standing decision (docs/decisions.md 2026-07-28): GLES2 is the only 3D
renderer; the desktop-GL path is to be eliminated. This unit produces
the accurate inventory and the phased migration plan the elimination
units will execute from. Wrong inventory = wasted downstream units, so
verify claims by building, not by grepping alone.

## Deliverable

A new committed doc `refactor/desktop_gl_elimination_plan.md` with:

1. **Inventory (evidence-backed)**:
   - Every CMake target that links system Irrlicht / X11 / Xxf86vm /
     desktop GL, per configuration (default desktop build vs
     `EQT_GLES2_IRRLICHT_DIR` build). Configure BOTH trees and diff the
     actual target lists; don't trust the CMakeLists text alone.
   - Which ctest tests exist only in the desktop build vs both. The
     reference numbers: desktop build registers ~1235; get the real
     GLES2-tree number by configuring `-DEQT_BUILD_TESTS=ON` with
     `EQT_GLES2_IRRLICHT_DIR` pointed at the built patched Irrlicht
     (build it via scripts/build-gles2-x86.sh first if absent) and
     RUNNING ctest there. Classify every delta test: (a) links desktop
     GL directly, (b) links system Irrlicht only incidentally,
     (c) genuinely tests desktop-GL behavior.
   - Tools inventory: model_viewer, generate_textures, zone_line_editor,
     etc. — for each: what it links, who uses it (grep scripts/, docs/,
     refactor/ history), verdict candidate (port / retire / already
     GLES2-clean).
   - Source-level desktop-GL references: EDT_ driver-type branches
     (EDT_BURNINGSVIDEO — note AnimatedTreeManager is already-known
     dead code on GLES2, P20 finding), `EMT_*` fixed-function reliance,
     raw GL calls outside the driver, `#ifdef` families.
   - External consumers: /mnt/projects/willeq-benchmark (Will's other
     session benchmarks against the DESKTOP build + full ctest — its
     migration note belongs in the plan).
2. **Phased migration plan**, each phase sized as an executor unit with
   its own verification bar, ordered so the tree stays green
   throughout. Expected shape (validate, don't assume): (i) make the
   GLES2 tree carry the full test suite; (ii) port-or-retire tools;
   (iii) remove desktop-GL CMake branches + system-Irrlicht find logic;
   (iv) delete dead source paths (BURNINGSVIDEO/AnimatedTreeManager
   etc.); (v) docs/CLAUDE.md updates. Flag anything that must WAIT for
   P25 (windowed dev use) to land first.
3. **Risk register**: tests that could silently lose coverage,
   behaviors only desktop GL exercised, the benchmark-clone impact,
   golden/gate invariants (all goldens are GLES2 — should be
   unaffected; verify the gate never touches the desktop build).

## Rules

- Territory: `refactor/desktop_gl_elimination_plan.md` + this plan's
  Review section ONLY. You may configure/build scratch trees anywhere
  under the worktree (clean up) but commit NO code, NO CMake changes.
- Verify-by-build discipline: every "links X" claim in the inventory
  comes from an actual configure/build/ldd/nm observation, not just
  reading CMakeLists. Cite the observation in the doc.
- ctest in the GLES2 tree may surface NEW failures (tests never run
  against the patched Irrlicht before) — that's exactly the data the
  plan needs; record them, do not fix them.

## Verification

- The committed plan doc, with the inventory tables and the per-phase
  unit breakdown. Standard bar steps that involve building are
  satisfied by your scratch builds; no production diff exists to
  verify otherwise. Working tree clean apart from the two committed
  docs.

## Review

Full inventory + phased plan committed as `refactor/desktop_gl_elimination_plan.md`.
Evidence: pre-existing desktop tree `build/` (system libIrrlicht.so.1.8,
1235 ctest tests), plus a fresh scratch tree `build-gles2-tests/`
(`EQT_GLES2=ON`, `EQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86`,
`EQT_BUILD_TESTS=ON`, `WITH_RDP=ON`) configured, built (`-j24 -- -k`), and
ctested this unit, then removed (scratch, gitignored).

**Headline counts** (~90 real targets total, both trees share the same
CMake target list):
- Class A (already GLES2-clean, build clean in both trees): ~76 targets —
  `willeq`/`willeq_core`, all hwcaps/GLES2 probes, all non-Irrlicht-linking
  tools, all ~63 non-graphics tests, `test_rdp_server`/`test_rdp_input_handler`.
- Class B (link-broken in the GLES2 tree, mechanical one-line-per-target
  fix — missing explicit EGL/GLESv2/drm/gbm): **14 targets** — 11 tools
  (`model_viewer`, `generate_textures`, `zone_line_editor`, `s3d_dump`,
  `uv_analysis`, `analyze_hum_model`, `test_animation`,
  `bsp_coordinate_finder`, `extract_zone_lines`, `generate_surface_map`,
  `entity_composite_builder`) + 3 tests (`test_asset_loading`,
  `test_door_loading`, `test_graphics`).
- Tools verdicts: 5 active (model_viewer, entity_composite_builder,
  generate_surface_map, zone_line_editor, s3d_dump) → port; 6 near-zero
  external references (uv_analysis, analyze_hum_model, test_animation,
  bsp_coordinate_finder, extract_zone_lines, generate_textures) → retire
  candidates pending Will's go/no-go.

**GLES2-tree ctest result**: 1118/1235 registered (delta = exactly the 3
Class-B test binaries' `GTEST_DISCOVER_TESTS` `_NOT_BUILT` placeholders,
120 test cases). Of the runnable 1118: 1112 passed, 3 expected NOT_BUILT
placeholders, `AudioManagerTest.PreloadCommonSounds` (known host ALSA
exception), `RDPServerFrameTest.UpdateFrameAfterStart` (SEGFAULT under
`-j4` only — reran 3x isolated on both trees, passes every time; same
already-documented `RDPServerFrameTest.*` parallel-execution flake family
as `UpdateFrameMultipleTimes`), and one genuinely new failure:
`RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless` — SEGFAULT,
100% reproducible on the GLES2 tree, 100% passing on desktop (3x each,
both trees). `gdb` backtrace puts the crash directly in `TestBody()` under
an `EDT_NULL` (no GPU/window) fixture — not an EGL/render-thread issue on
its face, needs its own investigation unit.

**Surprises that change phase ordering**:
1. The 14 Class-B failures are the *second* occurrence of an
   already-documented pattern (`refactor/deferred.md`'s 2026-07-27
   `IdentityMaterial`/Release-build entry, same ~7-tool membership) —
   these tool targets have never declared a complete, backend-agnostic
   Irrlicht link-dependency set. This promotes "fix the 14" to Phase 0
   (mechanical, low-risk, do first) rather than folding it into a later
   "port tools" phase.
2. `RaceModelAnimationHeadlessTest` is a hard blocker on ever claiming
   "the GLES2 tree carries the full suite" — added as Phase 1
   (investigation-only unit) ahead of flipping CI default.
3. `/mnt/projects/willeq-benchmark` (external harness, pinned at `b68c32a`)
   has a same-session-authored `build_errors_response.md` explicitly
   telling its operator "the desktop-GL build ... is the right default"
   — directly contradicted by the 2026-07-28 decision. Needs a guidance
   update shipped in lockstep with whichever phase removes the desktop-GL
   CMake path (Phase 2/3), not left implicit.
4. Phase 3 (CMake branch removal) must wait on sibling unit P25
   (X11-EGL window backend) landing, or x86 loses its only windowed dev
   path.
5. Source sweep found no fixed-function-pipeline reliance to delete beyond
   the already-known `EDT_BURNINGSVIDEO`/`AnimatedTreeManager` dead code —
   `EMT_*` material-type enum usage (29 files) is a false positive, shared
   identically by both drivers.

Working tree clean apart from the two committed docs (verified via
`git status --short`).
