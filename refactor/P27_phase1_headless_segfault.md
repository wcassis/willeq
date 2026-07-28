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

### Backtrace (gdb, `-DCMAKE_BUILD_TYPE=Debug` scratch reconfigure of
`build-gles2-tests` — needed for line info, reverted to the P26 empty
build type before the final verification runs below)

```
Thread 1 "test_inventory_" received signal SIGSEGV, Segmentation fault.
RaceModelAnimationHeadlessTest_ModelAnimatesProperly_Headless_Test::TestBody
    (this=0x5555569d9a90)
    at tests/test_inventory_model_view.cpp:375
375   auto* node = loader_->createAnimatedNode(rc.raceId, rc.gender,
                                                smgr_->getRootSceneNode());
```
Inspecting the fixture at the crash frame: `smgr_ = 0x0`, `driver_ = 0x0`,
`device_ = 0x5555569dcb00` (non-null). `SetUp()`'s
`irr::createDeviceEx(params)` with `params.DriverType = EDT_NULL` returned a
device object, but that device's scene manager and video driver were both
never created — the very first line of the test body (`smgr_->...`)
dereferences a null `ISceneManager*`.

### Root cause — class (a), confirmed by the backtrace

`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` (the x86 fork of the patched
Irrlicht device, copied into `third_party/irrlicht-gles2-x86` by
`scripts/build-gles2-x86.sh`) is missing `EDT_NULL` handling everywhere
stock Irrlicht devices have it:

- **Constructor**: unconditionally ran the full DRM/GBM/EGL probe
  (`initDRM()`/`initGBM()`/`initEGL()`/`initEGLSurfaceless()`) and
  `initEvdev()` regardless of `DriverType`. This is why the plan's own
  finding — `libEGL warning: failed to open /dev/dri/... Permission denied`
  appearing for an `EDT_NULL`-only test — happens: the device probes real
  hardware even when no driver was requested. Stock `CIrrDeviceLinux.cpp`
  guards every one of these steps with
  `if (CreationParams.DriverType != video::EDT_NULL)`.
- **`createDriver()`**: the `switch (CreationParams.DriverType)` had cases
  for `EDT_OPENGL`/`EDT_OGLES2`/`EDT_BURNINGSVIDEO`/`EDT_SOFTWARE` but none
  for `EDT_NULL` — it fell to `default:` (logs an error, leaves
  `VideoDriver` null). Stock `CIrrDeviceLinux.cpp` and
  `CIrrDeviceConsole.cpp` both have `case video::EDT_NULL: VideoDriver =
  video::createNullDriver(...)`.
- Because `VideoDriver` stayed null, the constructor's
  `if (VideoDriver) createGUIAndScene();` never ran, so the scene manager
  was never created either.
- The device is *not* closed by the caller: `Irrlicht.cpp`'s
  `createDeviceEx()` only closes-and-drops a device with a null video
  driver `if (params.DriverType != video::EDT_NULL)` — i.e. it explicitly
  expects `EDT_NULL` devices to have made their own null driver and
  considers a null-video-driver `EDT_NULL` device intentional. That
  invariant held for every stock device; this fork silently broke it.

Confirmed the ARM shared file (`docker/irrlicht-drm/CIrrDeviceFB.cpp`) has
byte-for-byte the identical gap (same constructor pattern, same
`createDriver()` switch with no `EDT_NULL` case) — it's the fork's shared
origin, not something introduced only on x86.

This is class **(a)**: "EDT_NULL creation path differs in the patched
Irrlicht (missing device/driver stubs vs desktop's full build)" — not (b)
or (c). Nothing in `RaceModelLoader`/`EQAnimatedMeshSceneNode` or the test
fixture is at fault; `EQT_HAS_GLES2` branches in our own code
(`constrained_texture_cache.cpp`, `gpu_upload_thread.cpp`) are never
reached — the crash happens before any of our production code runs, on the
very first Irrlicht call the test fixture makes after `SetUp()`.

### Fix

`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` and
`docker/irrlicht-drm/CIrrDeviceFB.cpp` (identical shape in both):
1. Wrapped the DRM/GBM/EGL/X11/evdev probing in the constructor with
   `if (CreationParams.DriverType != video::EDT_NULL) { ... }`, mirroring
   stock Irrlicht's guard.
2. Added `case video::EDT_NULL: VideoDriver =
   video::createNullDriver(FileSystem, CreationParams.WindowSize); break;`
   to `createDriver()` (declared in the already-included `CIrrDeviceStub.h`
   base class header — no new includes needed).

Why this location is correct: the root cause is squarely "a genuinely
missing null-device capability" in the patched Irrlicht fork (plan's
explicit acceptable-fix category), not our production code or the test.
The fix is a minimal, behavior-scoped addition — it only changes what
happens when `DriverType == EDT_NULL`, a code path the DRM/GLES2 production
rendering path (`EDT_OGLES2`) never takes and the x86 surfaceless-EGL
fallback never takes either (both require a non-null `DriverType`). No
existing driver/rendering behavior is touched. Per the plan's rule 3, this
patch is captured directly in the canonical source of truth
(`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` /
`docker/irrlicht-drm/CIrrDeviceFB.cpp`, copied fresh into
`third_party/irrlicht-gles2-x86` by `scripts/build-gles2-x86.sh` on every
`--irrlicht-only` rebuild — verified by rebuilding the archive and
confirming the fix took effect), so it survives rebuilds without a
separate patch-script change.

### Deviation — discovered pre-existing X11 link gap (out of the
segfault's root cause, fixed anyway to reach a clean baseline)

While rebuilding the full `build-gles2-tests` tree to run the required full
`ctest` baseline, 8 targets failed to link with `undefined reference to
XPending/XNextEvent/...` from `CIrrDeviceFB_X11.cpp`: `model_viewer`,
`s3d_dump`, `entity_composite_builder`, `generate_surface_map`,
`zone_line_editor`, `test_asset_loading`, `test_door_loading`,
`test_graphics` — exactly P26's 8 "Ported" Class B targets. Isolated with
`git stash` + a from-scratch Irrlicht rebuild: **reproduces identically
with the pristine (unmodified) Irrlicht fork**, i.e. this is unrelated to
the segfault fix and pre-existing — P26 added `EGL GLESv2 drm gbm` to each
target's link line (matching `willeq_core`'s `LINK_LIBS` block) but missed
that P25's windowed-EGL backend in `CIrrDeviceFB.cpp` unconditionally
references X11 symbols (`willeq_core`'s own `LINK_LIBS` block *does*
conditionally add `X11` for `NOT EQT_DRM` at CMakeLists.txt:538-542 — the
8 per-target blocks never got that same addition). Not caused by my
segfault fix, but it silently regressed the GLES2 tree's buildable-suite
count below the P26 baseline and blocked this unit's required "both
trees at P26 baseline" proof, so fixed it: added the identical `IF(NOT
EQT_DRM) TARGET_LINK_LIBRARIES(<target> X11) ENDIF()` guard (copying the
established pattern) to all 8 targets in `CMakeLists.txt` and
`tests/CMakeLists.txt`. Purely additive link-library lines, zero code or
behavior change, same mechanical pattern P26 already used elsewhere.

### Both trees' final numbers

| Tree | `cmake --build` | `ctest` | Notes |
|---|---|---|---|
| Desktop (`build/`, system Irrlicht, `EQT_GLES2=OFF`) | Clean | **1234/1235** | Only failure: `AudioManagerTest.PreloadCommonSounds` (documented host exception) |
| GLES2 (`build-gles2-tests/`, patched static Irrlicht, `EQT_GLES2=ON EQT_DRM=OFF`) | Clean (was: 8 targets failing per the X11 deviation above) | **1234/1235** | Same single documented exception; `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless` now passes (was the only non-documented failure) |

Both trees now match the P26 baseline's healthy count (1233/1235) *plus*
this unit's fix (the previously-failing 1 test now passes), i.e. 1234/1235
with the one pre-existing documented exception — no regressions, no new
undocumented failures.

### 5x-consecutive-pass proof

GLES2 tree (`build-gles2-tests/bin/test_inventory_model_view
--gtest_filter=RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`),
5 runs: `[ PASSED ] 1 test.` all 5 times, no segfault (previously 100%
reproducible).

Desktop tree (`build/bin/test_inventory_model_view`, same filter), 5 runs:
`[ PASSED ] 1 test.` all 5 times (was already passing; confirms no
regression from the fix).

### Additional verification (full 6-step bar, desktop tree)

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build --output-on-failure` — 1234/1235, only
   `AudioManagerTest.PreloadCommonSounds`.
3. `bash scripts/build-gles2-x86.sh` — clean (`build-gles2/` +
   patched `third_party/irrlicht-gles2-x86/lib/libIrrlicht.a`).
4. Visual gate — **N/A, not run**: the fix touches only the `EDT_NULL`
   path (never exercised by any real driver — `EDT_OGLES2` production
   rendering and the x86 `EDT_OGLES2`-via-surfaceless-EGL dev path both
   require a non-null `DriverType`) plus pure link-library additions in
   CMakeLists (no code change). No rendering-behavior surface exists to
   gate; goldens untouched.
5. `bash scripts/build-arm-noble.sh` — clean; confirms the shared
   `docker/irrlicht-drm/CIrrDeviceFB.cpp` change (used unmodified by the
   ARM cross-compile) doesn't break the DRM/GLES2 production path. The
   change there is inert for ARM in practice — ARM only ever constructs
   the device with `EDT_OGLES2`, never `EDT_NULL` — but is included so any
   future ARM-side null-driver test (or a shared-code reader) gets the
   same guarantee stock Irrlicht gives.
6. `bash scripts/refactor/check_file_sizes.sh fable` — `ok` for both
   changed `.cpp` files (1391 and 1202 lines, both under the 1500 warn
   ceiling).

### Deviations summary

- Discovered and fixed a pre-existing (not segfault-related) X11 link gap
  in 8 CMakeLists targets, required to prove the "both trees at P26
  baseline" bar — see above.
- Used a scratch `-DCMAKE_BUILD_TYPE=Debug` reconfigure of
  `build-gles2-tests` to get line-number backtraces (plan explicitly
  allows this); reverted to the P26 empty build type before the final
  verification build/ctest runs reported above.
- No Pi, no eqemu (per dispatch scope). No goldens touched. No
  driver-architecture change; no rendering-behavior change on any real
  driver path (`EDT_OGLES2` code paths are untouched).

### Final commit

See the commit on `task/P27-phase1-segfault` immediately following this
plan-file update (commit hash reported in the executor's final message).
