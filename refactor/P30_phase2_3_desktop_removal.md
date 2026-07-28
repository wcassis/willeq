# P30 — Desktop-GL elimination Phases 2+3: GLES2 default + branch removal

**Slot: slot1. x86 only — no Pi, no eqemu.** Siblings RUNNING: P28
(slot2, eq.h + 12 client TUs — do NOT touch those files), P29 (slot3,
no source). Your territory: CMakeLists.txt / tests/CMakeLists.txt,
scripts/, the build-instruction sections of the root CLAUDE.md, the
verification-bar section of refactor/EXECUTOR_PROTOCOL.md, the roadmap
doc, plus ONE named cosmetic source fix (below). Nothing else in src/.

Executes `refactor/desktop_gl_elimination_plan.md` §6 Phases 2 and 3
(read §5-§7 first; Phases 4/5 are NOT in scope — no source-path
deletions, no full doc sweep). Prereqs all met: Phase 0/1 executed,
P25 landed (windowed dev path exists), GLES2 tree certified 1234/1235.

## Work

1. **Phase 2 — GLES2 config becomes the primary tree.** The canonical
   dev/test configure is now the GLES2 one (patched static Irrlicht,
   `EQT_GLES2=ON`, tests on). Update: root CLAUDE.md "Build Commands"
   (plain `mkdir build && cmake ..` guidance → the GLES2-config
   equivalent, including the one-time
   `scripts/build-gles2-x86.sh --irrlicht-only` bootstrap if
   third_party is absent — verify that flag exists, else document the
   actual bootstrap), EXECUTOR_PROTOCOL.md's verification bar steps
   1-2 (build/ctest now mean the GLES2-config tree), and any script
   that assumes `build/` is the desktop tree (grep scripts/ and
   scripts/ci/). Keep `build-gles2/` (the client+benchmark tree the
   build script produces) and its consumers (visual gate) unchanged.
2. **Phase 3 — remove desktop GL from CMake** per the roadmap's §5
   inventory: the system-Irrlicht FIND_PATH/FIND_LIBRARY fallback
   (~lines 84-94), every `X11 Xxf86vm GL` desktop link branch (main
   LINK_LIBS ~549, model_viewer, gles2_zone_benchmark's desktop
   ELSE(), the 8 P26/P27 per-target blocks' desktop halves, tests/),
   the APPLE branches IF they are desktop-GL-only (verify; Apple has
   never been a supported target here — if removal is ambiguous, keep
   and note). `EQT_GLES2_IRRLICHT_DIR` becomes effectively mandatory:
   configure without a usable patched Irrlicht must FAIL EARLY with a
   clear message naming the bootstrap command — not fall back to
   desktop, not fail at link time.
3. **Cosmetic fix** (roadmap Phase 3 note): the misleading
   `Display backend: X11/GLX` log message (roadmap §1) — fix the
   string at its single site now that its branch is being touched.
   Name the file in the Review; if it lives in a file P28 owns
   (eq.h/the 12 TUs), SKIP it and say so.
4. Update the roadmap doc: Phases 2+3 marked EXECUTED with what
   actually happened.

## Rules

- The retired-desktop configure path must be GONE, not half-alive:
  after this unit, `-DEQT_GRAPHICS=ON` without the patched Irrlicht
  errors out clearly; there is no code path that links system
  libIrrlicht. `EQT_GRAPHICS=OFF` (headless compile) is P28's concern
  — do not touch its semantics, and expect merge-time coordination if
  you both edit option-plumbing lines: keep your diff surgical.
- Do NOT delete dead source paths (AnimatedTreeManager,
  EDT_BURNINGSVIDEO branches etc.) — Phase 4, not yours.
- The willeq-benchmark clone guidance and TESTING_CHARTER wording are
  the ORCHESTRATOR's to update at merge time — note them, don't touch
  them.
- STOP: anything forces a behavior change in the GLES2/ARM rendering
  paths, or the mandatory-Irrlicht configure gate can't be made clean.

## Verification (the bar itself changes in this unit — apply the NEW bar)

- GLES2-config full tree (tests on): build clean + ctest at the P27
  baseline (1234/1235, ALSA exception only).
- `bash scripts/build-gles2-x86.sh` clean; visual gate PASS, goldens
  untouched.
- `bash scripts/build-arm-noble.sh` clean (ARM CMake path must be
  unaffected by the removals — it never used the desktop branches;
  verify, don't assume).
- Negative test: a configure attempt with no patched Irrlicht
  available fails early with the documented message (show it).
- `check_file_sizes.sh` vs fable: no new FAILs.

## Review

(executor fills in: removal inventory vs roadmap §5 [each item:
removed/kept+why], the new configure UX + bootstrap docs, the negative
test output, all verification numbers, deviations)
