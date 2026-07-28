# P27 — Desktop-GL elimination Phase 1: headless animation segfault

**Slot: slot1. x86 only — no Pi, no eqemu. Sole agent this dispatch.**

The last blocker before the GLES2 tree can be certified to carry the
full suite (refactor/desktop_gl_elimination_plan.md Phase 1):
`RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`
SEGFAULTS 100% reproducibly in the GLES2 tree and passes 100% in the
desktop tree (verified 3x each by P24, re-confirmed by P26).

## Known facts (P24/P26 — trust, then extend)

- Fixture is `EDT_NULL` (no GPU, no window); gdb puts the crash
  directly in `TestBody()`. The test is the headless variant of
  ModelAnimatesProperly wired into ctest by P12b.
- GLES2-tree configure: own tree with
  `-DEQT_GLES2=ON -DEQT_DRM=OFF
  -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86
  -DEQT_BUILD_TESTS=ON -DWITH_RDP=ON` (P26's recipe; the static
  patched Irrlicht must already be built — run
  scripts/build-gles2-x86.sh first if third_party is missing).
- The GLES2 tree differs from desktop in TWO ways — the patched static
  Irrlicht (only the FB/DRM device + GLES2 driver compiled; desktop
  ships all devices/drivers) AND the `EQT_HAS_GLES2` compile define
  activating different branches in OUR code (e.g. gpu_upload_thread
  paths in constrained_texture_cache). Either can be the divergence;
  identify WHICH before fixing.

## Work

1. Reproduce in the GLES2 tree; full gdb backtrace with line numbers
   (build the test target with -g if the default config strips too
   much — a scratch reconfigure is fine).
2. Root-cause. Plausible classes (verify, don't assume): (a) EDT_NULL
   creation path differs in the patched Irrlicht (missing device/
   driver stubs vs desktop's full build); (b) an `EQT_HAS_GLES2`
   branch in our code dereferences GLES2-only state that a null-driver
   context never initializes; (c) test fixture assumes something only
   the desktop lib provides. The crash being "in TestBody" suggests
   (b)/(c) over (a), but confirm with the backtrace.
3. Fix at the true cause. Acceptable fix locations: the test/fixture,
   our production code (a missing null-guard on a GLES2-only path is a
   REAL bug worth fixing properly), or a minimal patched-Irrlicht stub
   IF the root cause is a genuinely missing null-device capability —
   in that last case keep the patch minimal and document it in the
   patch set (docker/irrlicht-drm-patch.py flow) so rebuilds keep it.
4. Prove: the test passes 5x consecutively in BOTH trees; the full
   suite in both trees stays at the P26 baseline (1233/1235 with only
   the documented exceptions).

## Rules

- Territory: the test + fixture files, our production code where the
  root cause genuinely lives, patched-Irrlicht patch flow only per
  rule 3. No goldens, no shared-ARM-driver behavior changes (if the
  fix touches docker/irrlicht-drm/ shared files, the ARM build must
  still pass and the change must be null-driver-scoped — explain why
  it can't affect the Pi's DRM path).
- STOP: root cause demands driver-architecture changes beyond a
  null-guard/stub, or the fix would alter rendering behavior on any
  real driver path (report with the backtrace + analysis instead).

## Verification

- Full 6-step bar (known exceptions per protocol) on the DESKTOP tree
  + full ctest on the GLES2 tree (P26 recipe), both at baseline.
- The 5x-consecutive pass proof in both trees.
- If shared driver files touched: build-arm-noble PASS + rationale.

## Review

(executor fills in: backtrace, root cause with the (a)/(b)/(c)
classification, fix + why that location is correct, both trees'
numbers, deviations)
