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

(executor fills in: headline counts — targets/tests/tools per class,
GLES2-tree ctest result, surprises, and anything that changes the
expected phase ordering)
