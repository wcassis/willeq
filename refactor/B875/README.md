# B875 — Chase camera constants: the fable pin, and the owed official-client fit

Issue #219. Branch `task/chase-constants-pin` (2026-09-29, offline unit: no
eqemu lane, no oracle rig). **The official-client fit is NOT done; #219 stays
open.** The plan for it is `FIT_PLAN.md` in this directory.

## What the expected values are (provenance)

`tests/chase_rig_expected.h` holds the committed literals: azimuth 0, pitch 6,
distance 4.0, look-at height 7.0 above the feet, fitted-character eye 6.023,
horizontal FOV 62.8.

- They are **fable's CURRENT constants** (`camera_rig.h` `kChaseRig`,
  `kChaseFitEyeAboveFeet`; `first_person_tunables.h`
  `kFirstPersonHorizontalFovDegrees`), copied as literals.
- They were chosen by T466/T474's image-similarity sweep of fable's renders
  against official stills — a fit T478 (board B721) found crop-dependent and
  left NOT SETTLED. That sweep is not a camera-pose measurement.
- They are corroborated **at rest** by one real zoned-in **fable** run:
  `refactor/B874/live/` (merged to fable from `task/pose-live-capture`), run
  `run-20260929T022314Z` — pitch 6.0000, azimuth 0.0001 deg, distance 4.0002
  against kChaseRig over 129 stationary rows. That run measured fable against
  its own constants.
- **They are NOT a measurement of the official client.** The pin catches
  accidental drift of fable's chase camera; it does not prove oracle parity.

## What already guarded kChaseRig before this unit

Measured, not read: each constant was changed in the tree, the tests below were
rebuilt and run, and the constant restored (table in "RED runs").

The honest answer is "more than issue #219 assumed, but unevenly". Before
this unit, the tests that went red (23 camera/chase test binaries run; the
unmodified tree was green in all of them):

| constant edited | existing tests that went red |
|---|---|
| distance 4.0 -> 4.001 | `CameraRigDefaults.ChaseIsTheOracleFit`, `CameraRigGeometry.TheChaseFitPutsTheCameraWhereT471ComputedIt` (test_camera_modes), `ChaseCameraFitTest.ChaseSitsAtTheOracleFitWithTheLookAtOnThePlayer` (by 0.00009 over its 1e-3 tolerance) |
| pitch 6.0 -> 6.01 | the same two in test_camera_modes, `ChaseCameraFitTest.ZoomActsOnTheChaseDistanceAndLeavesTheLookAtAlone` (by 0.00004 over 1e-3) |
| height 7.0 -> 7.001 | the same two, plus three `ChaseCameraFitTest` cases |
| azimuth 0.0 -> 0.01 | the same two, plus `ChaseCameraFitTest.CameraSetInChaseEditsTheChaseCamera` (only because the `/camera get` text prints `az=0.010`) |
| kChaseFitEyeAboveFeet 6.023 -> 6.024 | `CameraRigDefaults.ChaseIsTheOracleFit` only |
| horizontal FOV 62.8 -> 62.81 | `FpTunablesTest.TheDefaultsAreTodaysMeasuredNumbers` only |

So the constants were already pinned device-free (`test_camera_modes.cpp`,
EXPECT_FLOAT_EQ), and T474's `test_chase_camera_fit.cpp` checks a
component-level camera against literals at 1e-3, which a 0.001 edit clears
only barely or not at all. The renderer-level tests that derive their
expectation from kChaseRig (the B874 `PoseLogE2ETest`, `test_camera_mode_wiring`,
`test_camera_bsp_ceiling`) stayed green in every run, as expected. Nothing
checked the FOV of the rendered Chase camera.

## What this unit adds

| test | where | what it pins |
|---|---|---|
| `ChaseRigPinned.EachConstantEqualsItsCommittedValue` | `tests/test_chase_rig_pinned.cpp` | the four kChaseRig fields, kChaseFitEyeAboveFeet and the 62.8 FOV, each within 1e-5 of its literal |
| `ChaseRigPinned.ChaseIsHeadingRelativeAndInsideTheClamps` | same | Chase's azimuth frame is the player's heading; no clamp moves the rig |
| `ChaseRigPinned.ProductionPoseEqualsTheClosedFormOfTheExpectedValues` | same | `computeCameraRigPose(scaleChaseRigForEye(kChaseRig, eye), ...)` against the double-precision closed form of the LITERALS, 4 headings x 2 positions x 2 eyes (6.023 and the Dwarf's 3.58), 1e-4 |
| `ChaseRigPinned.TheRaceScaleIsUniformAboutTheFeet` | same | the scale law at the second eye, in numbers |
| `ChaseRigPinnedE2E.RenderedChaseCameraEqualsTheClosedFormOfTheCommittedValues` | `tests/test_pose_log_e2e.cpp` | the camera a REAL headless renderer (production `initLoadingScreen()` / `setPlayerPosition()` / `/camera chase` / `processFrame()` / `getPoseData()`, the B874 harness reused as a fixture subclass) places, 4 headings x 2 positions, 1e-4 on every coordinate plus recovered distance / pitch / azimuth / height |
| `ChaseRigPinnedE2E.RenderedChaseFieldOfViewIsTheCommittedValue` | same | the rendered Chase camera's horizontal FOV, 1e-3 deg |

No player model is loaded in the renderer harness, so its eye is the fitted
one and the race scale is the identity there; the second eye height is pinned
device-free. Loading a model would make the e2e test depend on the EQ client
files, which it deliberately does not.

## RED runs

Each row: the constant edited in the working tree, the listed binaries rebuilt
and run, then restored (`git diff` empty afterwards).

| edit | new tests red | the failing assertion (first) |
|---|---|---|
| distance 4.0 -> 4.001 | `ChaseRigPinned.EachConstantEqualsItsCommittedValue`, `.ProductionPoseEqualsTheClosedFormOfTheExpectedValues`, `.TheRaceScaleIsUniformAboutTheFeet`, `ChaseRigPinnedE2E.RenderedChaseCameraEqualsTheClosedFormOfTheCommittedValues` | kChaseRig.distance 4.000999928 vs 4 (tol 1e-5); rendered camY off by 0.000993, recovered distance 4.000998 (tol 1e-4) |
| pitch 6.0 -> 6.01 | the same four | kChaseRig.pitchDeg 6.0100002; rendered camZ off by 0.000694, recovered pitch 6.010001 deg (tol 0.002) |
| height 7.0 -> 7.001 | the same four | kChaseRig.height 7.000999928; rendered targetZ off by 0.00100, recovered height 7.001 |
| azimuth 0.0 -> 0.01 | the same four | kChaseRig.azimuthDeg 0.01; rendered camX off by 0.000702, recovered azimuth 0.0101 deg |
| kChaseFitEyeAboveFeet 6.023 -> 6.024 | the three `ChaseRigPinned` above (+ `ChaseRigFit.ThePoseModelIsTheProductionRig`) | kChaseFitEyeAboveFeet vs 6.023; the Dwarf-eye pose |
| horizontal FOV 62.8 -> 62.81 | `ChaseRigPinned.EachConstantEqualsItsCommittedValue`, `ChaseRigPinnedE2E.RenderedChaseFieldOfViewIsTheCommittedValue` | kFirstPersonHorizontalFovDegrees vs 62.8; rendered Chase hFOV 62.81 (tol 1e-3) |

The e2e test does not see an eye edit, by design: with no model loaded its
eye IS kChaseFitEyeAboveFeet, so the scale stays 1.

## MR !31 is superseded

`task/kb-chase-oracle-session` (MR !31) must not be merged as it is. Its
`refactor/B875/chase_fit_golden.json` is circular — the correspondences were
projected with the reference camera and the "fit" recovered the reference,
with zero delta on every axis — and it cites a pose at which no capture was
taken. Its `ChaseConstantsFitPinned` test only checks the JSON's structure: it
never compares a constant to kChaseRig and stays green with the distance at
4.001. This unit's tests replace it; nothing from that branch was copied.

## The solver (`tools/chase_fit/`)

`chase_rig_fit` fits azimuth, pitch, distance, height, horizontal FOV and
(from two eye heights) the race-scale exponent to manual correspondences.
`tests/test_chase_rig_fit.cpp` validates it without circularity:

- The synthetic ground truth (az 2.5, pitch 11, dist 6.5, height 5.8,
  hFOV 70) differs from kChaseRig on every axis (a test asserts it).
- 6 stills x 20 points, 0.7 px Gaussian noise, rounded to the 0.5 px click
  grid: recovered az 2.505, pitch 10.992, dist 6.498, height 5.798, hFOV
  70.006, RMS 0.96 px (bounds 0.1 / 0.1 / 0.05 / 0.05 / 0.2, truth within
  4 sigma). The same from a start AT kChaseRig and from a far start.
- kChaseRig on that data: RMS 117.9 px — it does not "explain" stills from
  another camera, and `rigExplainsStills()` says so.
- The eye-scale exponent is recovered from two eye heights (truth 1 -> 1.0006,
  truth 0 -> 0.0018) and refused with one.
- The MR !31 input — points projected noise-free with the camera, kChaseRig or
  the truth — is refused twice: off the click grid, and RMS 1e-12 px below the
  0.1 px floor ("circular"). With both guards removed from a scratch copy of
  the solver, `ChaseRigFit.CorrespondencesGeneratedWithACameraAreRefused`
  goes red (the tests are what keep the guards in).
- NOT closable in code: a projection ROUNDED to the grid passes both guards.
  FIT_PLAN.md's re-click audit is the guard for that.

`chase_rig_fit truth.tsv --check 0 6 4 7 62.8` on the same synthetic data:
two starts agree, verdict USABLE, fable's rig "does NOT explain the stills"
(exit 1).
