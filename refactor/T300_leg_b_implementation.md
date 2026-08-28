# T300 Leg B — first person gets the real mechanism (the code change)

Executor unit, slot1, branch `task/T300-legB`, cut from `fable` @ `c194bc16`.
Contract: `refactor/T300_firstperson_real_mechanism.md`, **Leg B only**. No
server, no oracle, no Pi — Leg A owns eqemu concurrently in slot2.

**The mechanism, as implemented (revised twice — read §4 before anything
else):** first person is the ORDINARY player model, drawn normally with its
equipment, camera AT THE EYES, and the body is kept out of frame by a **clamped
down-pitch**. There is no second render pass, no geometry filter, no hidden
node, no standoff — and, after T300 Leg A, **no near-plane hiding of any kind**:
first person clips exactly as third person does. Leg A measured the official
client clipping nothing at all, and its size sweep (5x body, zero player
geometry, ground 34 units below still drawn) refuted both the fixed and the
size-derived plane this leg had shipped in turn.

---

## 1. What was deleted

**Files removed outright:**

| file | what it was |
|---|---|
| `src/client/graphics/viewmodel_renderer.cpp` (311 lines) | the T296 pass: its own view/projection, its own 0.05 near plane, the mid-frame depth clear, `computeViewmodelPlacement()`'s bounding-box fit, the cached arm box, `getPlayerViewmodelParts()` |
| `include/client/graphics/viewmodel_transform.h` (112) | the pass's frame algebra (`RotY(h)` arms, `RotY(-90) * relative` equipment) |
| `include/client/graphics/viewmodel_tuning.h` (34) | `fillFraction` 0.85, `verticalBias` -0.80, near plane 0.05, min clearance |
| `tests/test_viewmodel_pass.cpp` (365) | the pass's headless coverage |

**Symbols removed from surviving files:**

* `IrrlichtRenderer::renderFirstPersonViewmodel()` + its call site in
  `renderer_frame.cpp`; `getLastViewmodelDrawCallCount()`; the four
  `viewmodel*` members (`viewmodelTuning_`, `viewmodelArmBoxBaked_`,
  `viewmodelArmBoxHeadingY_`, `viewmodelArmBoxValid_`,
  `lastViewmodelDrawCallCount_`).
* `EntityRenderer::ViewmodelParts` + `getPlayerViewmodelParts()`.
* `EQAnimatedMeshSceneNode::setDeferredToViewmodelPass()` /
  `isDeferredToViewmodelPass()` / `deferredToViewmodelPass_`, and the
  `!deferredToViewmodelPass_` condition in `OnRegisterSceneNode()`.
* **T74's whole arm-bone geometry filter** — `setFirstPersonMode()`,
  `isFirstPersonMode()`, `buildArmBoneIndices()`,
  `applyFirstPersonIndexFilter()`, `hasValidFirstPersonGeometry()`,
  `getArmBoneCount()`, `getArmBoneIndices()`, `armBoneIndices_`,
  `armBonesBuilt_`, `firstPersonMode_`, `hasValidFirstPersonGeometry_`
  (240 lines out of `animated_mesh_scene_node.cpp`). The contract names "the
  arm bone filter added for it"; under the settled mechanism there is nothing
  to select and no fallback to fall back to, and leaving it in would have left
  a second, unreachable answer to "what does first person draw".
* **T76's `kFirstPersonCameraStandoffUnits` (0.3, BACKWARD)** and
  **T75's `kFirstPersonPitchDownRad` (12 degrees)**.
* T76's HEAD-bone eye-height preference inside the eye-height accessor (see §3).
* T296's late-attach equipment hide in `entity_equipment.cpp` (it existed only
  because the pass drew a second copy of the weapon).
* Four tests in `test_inventory_model_view.cpp` whose subject is gone:
  `FirstPersonArmFilter_PerRace_Headless`,
  `FirstPersonMode_IsPerNode_NotShared_Headless`,
  `FirstPersonArmFilter_FallbackWhenNoArmBones_Headless`,
  `ArmVsCameraGeometryMeasurement_Headless` (T76's pullback search).

**Trackers/docs struck** (pin-every-fix): `refactor/deferred.md`'s FIRST PERSON
sections (new T300 section at the top; the T296 section re-titled SUPERSEDED,
body intact per that file's own rule; the T295 section's forward pointer
corrected), `docs/decisions.md` (new REVERSED entry above the viewmodel
decision, which is re-titled SUPERSEDED), `refactor/T296_viewmodel_pass.md`
(SUPERSEDED banner), `README.md` (two places that described first person as
"camera clips into arm geometry (T76)").

**Withheld, not hidden — why the re-enable is not a naive re-hide.** T296 kept
the player node VISIBLE and withheld it from the render list because Irrlicht
skips `OnAnimate()` on invisible nodes, so hiding it ships a frozen skeleton.
The fix here does not re-hide anything: the node is visible AND registered,
exactly like every other entity, so it animates and draws by the normal path.
`setPlayerFirstPersonMode()` now does two things only — record the mode, and
hide the floating **name tag**, which is a camera-facing billboard the near
plane does not clip.

## 2. The camera formula, now

New home for the numbers and the math:
**`include/client/graphics/first_person_camera.h`** (header-only, no Irrlicht
dependency), read by exactly one `.cpp`
(`IrrlichtRenderer::repositionFirstPersonCamera()`).

```
camera position = (playerX_, playerZ_ + eyeAboveNodeOrigin + playerConfig_.eyeHeight, playerY_)
look direction  = heading (playerHeading_/512 * 2pi), pitched (playerPitch_ - 3.65) degrees
horizontal FOV  = 62.8 degrees   (set as vertical via the camera's aspect ratio)
near plane      = 1.0, the world's, in every mode (Leg A: the client clips nothing)
down-pitch      = clamped at 90 - vFOV/2 -- this is what hides the body (section 4)
```

| term | before | after | source |
|---|---|---|---|
| forward standoff | 0.3 **backward** | **0.0** | T298 A3: 0.00 +/- 0.4 |
| lateral | 0 | 0 | T298 A3: 0.00 +/- 0.05 |
| eye | `playerZ_ + 3.6527` | unchanged (see §3) | T298 A3: 6.75 +/- 0.20 above server z |
| fixed pitch | 12.0 deg down | **3.65 deg down** | T298 A3: 3.65 +/- 0.15 |
| hFOV | never set -> Irrlicht default 72 deg **vertical** ~= **88 deg horizontal** at 4:3 | **62.8 deg horizontal** (49.2 vertical at 4:3) | T298 A3: 62.8 +/- 0.3 |
| near plane | 1.0 (world default) | **1.0 in every mode** — first person clips no differently from third | Leg A: the official client clips nothing |

`applyCameraProjectionForMode()` is the new near-plane/FOV owner. It is called
from `setCameraMode()`, `cycleCameraMode()` and from
`repositionFirstPersonCamera()` itself. **It must be called on the mode
transitions and not only from the reposition helper**, because that helper
early-returns outside first person and so can never restore the world's 1.0
plane and 72-degree FOV on the way OUT. Third person and free camera are
therefore bit-identical to before this unit — which is why the visual gate's
goldens are untouched and passing.

## 3. `playerZ_`: which frame each number lives in

Done first, before touching eye height, per the contract. **The eye height did
not change, and that is the finding, not an omission.**

* `playerZ_` is the player **NODE ORIGIN**, which is the server's Z.
  `entity_renderer.cpp` places the node at `setPosition(x, z + modelYOffset, y)`
  with `modelYOffset = 0`, and its own comment says *"Server Z is the geometric
  MODEL CENTER"*. The origin is INSIDE the body: the Human male base mesh spans
  `Y = [-3.05, +3.65]` about it.
* `getPlayerEyeHeightFromFeet()` — the name was a lie, and
  `repositionFirstPersonCamera()`'s comment (*"playerZ_ is feet position"*)
  repeated it. Renamed **`getPlayerEyeHeightAboveNodeOrigin()`**, with the two
  frames spelled out at the declaration, the definition and the call.
  The `[FP-CAMERA]` log line no longer prints `feet=`; it prints
  `nodeOriginZ=` and `eyeAboveOrigin=`.
* **The value is unchanged at `bbox.MaxEdge.Y` = 3.6527 for a Human male.**
  3.65 above the node origin is **6.70 above the mesh's own feet**, and T298
  measured the official client's eye at **6.75 +/- 0.20 above the server z**
  with the ground ring plane 0.10 below that z — i.e. the official client
  stands the model with its feet at the server z and puts the eye at the crown.
  **3.65 and 6.75 are the same eye in two frames.** "Correcting" 3.65 to 6.75
  raises the camera 3.05 units, out through the top of the head; that is the
  regression the contract warned about, and it is now pinned by
  `FirstPersonCameraFormula.EyeSitsAtTheNodeOriginPlusEyeHeightNotPlusBoth`.
* **T76's HEAD-bone preference is removed.** It returned 3.10 (0.55 lower), its
  only rationale was vertical framing for the retired viewmodel pass, and it
  disagrees with the official measurement by more than that measurement's error
  bar. It also never fired live — T295 logged `headBoneIdx=-1` /
  `source=BBOX_FALLBACK` in every session on Testchar's model. The bbox top is
  now the single unconditional answer.

**⚠ Open, found while settling this and NOT fixed here.** fable's
`collisionZOffset` is `bboxHeight/2` (3.35 for a Human male), not the origin-to-
feet distance (`-MinEdge.Y` = 3.05). Ground snapping therefore places the model
**0.30 units above** its own collision ground, so fable's eye sits ~7.00 above
ground where the official client measured 6.65-6.75 above the ring plane. In
the frame that matters for this unit — the eye relative to the BODY the near
plane must swallow — fable already agrees to 0.05. The 0.30 belongs to whoever
fixes ground snapping; it moves every entity in the world and is not a camera
change.

## 4. Hiding the body: a clamped down-pitch (the near plane is REFUTED)

**Second revision, after T300 Leg A reported. The near-plane hypothesis — both
the fixed constant and the size-derived version — is dead, and this is the
contained edit the previous revision was structured for.**

What Leg A measured in the official client:

* **It clips nothing.** The camera was teleported onto a fixed NPC in 0.5-unit
  steps; at d=0 the NPC's head and shoulders render sharply with no cut. The
  near plane is under 1 unit. Ground 6.75 below the eye renders at max
  down-pitch.
* **The player's own arm, hand and shoulder ARE drawn** during a halberd swing,
  running off the frame's **bottom-right CORNER**. There is no straight cut
  across the middle of any frame in the session — which is what a near plane
  would have produced, and what T299 inferred from lower-resolution evidence.
* **The size sweep kills both plane variants at once.** At `#size 30` (5x), 417
  rendered frames of emotes with the UI hidden show zero player geometry while
  the ground 34 units below is still drawn. A fixed ~7-unit plane would be poked
  through by a 5x body; a size-scaled plane (~35 units) would have erased that
  ground.

**The mechanism is a clamped down-pitch.** The body hangs directly below the
eye, and first-person pitch is clamped so the frustum's lower edge never passes
the nadir — so you can never look far enough down to see yourself, while a swing
rotates the arm and weapon UP across a frame edge into view. Leg A measured max
down-pitch at roughly 65 degrees, bracket 55-75.

**Removed:** `firstPersonNearPlaneFromBodyRadius()`,
`kFirstPersonNearPlaneMargin`, `kFirstPersonNearPlaneFallbackUnits`,
`EntityRenderer::getPlayerBodyRestRadius()` (no remaining caller),
`firstPersonAppliedNearPlane_`, and the per-race derived-plane tests.
**`applyCameraProjectionForMode()` now sets `kWorldNearPlaneUnits` (1.0) in
EVERY mode** — first person clips no differently from third. Only the FOV stays
mode-specific, which is why third person is still byte-identical and the
goldens hold.

### The clamp, as derived

```
maxDownPitch = 90 degrees - vFOV/2        // firstPersonMaxDownPitchDegrees()
minStoredLookPitch = -(maxDownPitch - kFirstPersonPitchDownDegrees)
```

90 - vFOV/2 is exactly the angle at which the frustum's lower edge reaches the
nadir. At fable's 62.8-degree hFOV and 4:3 that is **vFOV 49.20 -> clamp
65.40 degrees**, inside Leg A's 55-75 bracket and within 0.05 of the 65.45 its
own re-derived FOV (hFOV 62.71, vFOV 49.1 at 1024x768) implies.

**Why derived rather than hardcoded at 65:** the form is automatically
scale-invariant, race-invariant and immune to shrink/grow, because a body's
ANGULAR position relative to the eye does not depend on how big the body is. It
needs no per-race table and no runtime size term of any kind — which is exactly
what neither plane variant could deliver. It also tracks the lens: widen the FOV
and the clamp tightens by itself.

**⚠ SAID PLAINLY: 90 - vFOV/2 IS AN INFERENCE CONSISTENT WITH THE MEASUREMENT,
NOT ITSELF A MEASURED ANGLE.** Leg A measured the clamp only as "roughly 65,
bracket 55-75"; this form was chosen because it lands inside that bracket and
matches Leg A's own description of the lower edge sitting at the nadir. **A
dedicated measurement of the clamp is the outstanding follow-up**, and nothing
in this leg has been judged on a screen.

The clamp applies to the **TOTAL** down-pitch — the mouse-look angle plus the
fixed 3.65-degree bias — because it is the resulting frustum that must not pass
the nadir. `firstPersonMinLookPitchDegrees()` is the same limit expressed in the
stored mouse-look frame, so the accumulator and the formula clamp the same
thing; a test asserts they agree.

### Which pitch path is clamped, and how I know it is the live one

`playerPitch_` has **exactly one writer in the tree**:
`IrrlichtRenderer::updatePlayerMovement()` in
`src/client/graphics/renderer_player_physics.cpp` (grep for `playerPitch_`
returns the declaration, this write, and read-only uses in the camera formula
and its log line). That function is the per-frame local-movement/mouse-look
path — the same one T295 caught carrying a private duplicate of the camera
formula, i.e. demonstrably the path that runs when the player actually looks
around, not a path only tests reach. The clamp is applied there, immediately
after the existing +/-89 gimbal clamp and gated on
`cameraMode_ == CameraMode::FirstPerson`.

It is applied in **three** places on purpose, which is one limit expressed
three times, not three formulas:

1. **At the accumulator** (the live path above) — keeps the STORED value in
   range so the control never develops an invisible dead zone the player has to
   drag back through before the view moves.
2. **Inside `computeFirstPersonCamera()`** — the guarantee at the point of use.
   No caller can get around it; a test feeds the formula -1000 degrees and
   asserts the resulting total pitch still stops at the clamp.
3. **In `applyFirstPersonPitchClamp()`**, called every frame before the draw and
   on mode changes — because entering first person from a third-person view
   that was pitched further down than the clamp allows has to be corrected, and
   nothing else would notice.

The source-parsing "one formula" test was updated to permit exactly one second
reader (`renderer_player_physics.cpp`) of `firstPersonMinLookPitchDegrees` and
nothing else.

### The UP side: no hiding clamp needed

The body hangs **below** the eye, so looking up moves it further out of frame;
Leg A measured only the down side, and there is nothing for an up-side clamp to
hide. The existing +/-89 limit is kept and is now named
`kFirstPersonMaxUpPitchDegrees` so it cannot be confused for part of the hiding:
it is a gimbal guard that stops the look-at target degenerating when it sits
exactly above the eye. A test pins that a fully-up look still produces a
non-degenerate target.

### ⚠ MEASURED HERE, AND IT DOES NOT AGREE WITH THE CLAMP — the first thing Leg C should check

`FirstPersonPitchClampKeepsTheBodyOutOfFrame_Headless` sweeps down-pitch against
real S3D meshes and reports the angle at which the player's own **resting**
geometry first enters the frustum:

| race | body enters frame at (1x) | at 5x | mid-swing vertices in frame at level pitch |
|---|---|---|---|
| Human male | 39.5 deg | 11.0 | 216 |
| Human female | 35.5 | 27.25 | 113 |
| Barbarian male | 41.0 | 6.25 | 160 |
| Dwarf male | 8.5 | 0 | 190 |
| Dark Elf female | 35.75 | 5.75 | 93 |
| Troll male | **0 — in frame at LEVEL pitch** | 0 | 165 |
| Iksar male | **0 — in frame at LEVEL pitch** | 0 | 1669 |

Every one of these is short of the 65.40-degree clamp. Taken at face value,
**the clamp alone does not hide fable's body**, whatever it does in the official
client. That is reported, not asserted, for two reasons:

1. The measurement assumes the baked player mesh's `+Z` is the camera's forward
   at heading 0 (`setIsPlayerNode()` + rotation 0 -> `RotY(-90)`, T298 section
   3). That mapping is derived, not confirmed against a live frame. If it is
   wrong, "forward" here is really "sideways" and shoulder width is being read
   as forward reach — which would explain resting extents of 2.7-3.0 units in
   front of the eye.
2. fable's models, animations and eye height (the crown of the head, not eye
   level) are not the official client's, so an angle measured there does not
   have to reproduce here.

**This is a concrete prediction for Leg C to confirm or refute: at max
down-pitch, does fable show the player's body?** If it does, the candidates in
order are the eye height (crown vs eye level), that axis assumption, and the
clamp angle itself. The mid-swing column is the other half of the same
prediction — that geometry IS supposed to be visible.

### Still contained if the mechanism moves again

Everything that hides the player is behind two names:
`firstPersonMaxDownPitchDegrees()` in the header and
`IrrlichtRenderer::applyFirstPersonPitchClamp()` (its only production caller,
at one per-frame call site). The camera placement formula, the eye-height frame
work, the FOV fix and all the deletions are independent of it — as this second
revision demonstrated in practice.

## 5. Tests

**Nothing headless can close this feature, and nothing here claims to.** No
test in this suite rasterizes: they measure geometry and parse source. They
cannot show that the body is clipped, that a swing is visible, or that the
projection reached the GPU. T296 shipped a depth clear that was silently
compiled out by a wrong `#ifdef` and **every headless test still passed** —
that is the standing proof of this limit, and it is written into both test
files. **T300 Leg C's live video is the gate; Will judges the look.**

New/changed coverage:

* `tests/test_first_person_camera_single_formula.cpp` — five tests pin the
  clamp: `DownPitchClampIsDerivedFromTheFov` (clamp + half-FOV is exactly the
  nadir at every aspect ratio, inside Leg A's 55-75 bracket, within 0.5 of the
  65.45 its FOV implies, and tightening as the lens widens),
  `DownPitchClampIsIndependentOfModelSize` (**Will's requirement, asserted**:
  the derivation takes no size term at all, and the formula honours the clamp
  even when fed -1000 degrees), `StoredMouseLookFloorMatchesTheTotalClamp` (the
  accumulator's floor and the formula's total limit are the same angle in two
  frames — otherwise the control gets a dead zone), `UpSideIsNotPartOfTheHiding`
  (+/-89 kept as a gimbal guard, a fully-up look stays non-degenerate), and
  `FirstPersonClipsNoDifferentlyFromThirdPerson`. Plus the source-parsing
  invariants updated for the new tunables (the "one TU" rule now allows the
  defining header and zero readers for a constant consumed inside it), the
  deleted-symbol guard extended to `renderFirstPersonViewmodel`,
  `ViewmodelParts`, `setDeferredToViewmodelPass`, `computeViewmodelPlacement`,
  `buildArmBoneIndices`, `applyFirstPersonIndexFilter`,
  `hasValidFirstPersonGeometry`, `getArmBoneIndices`; plus **six new numeric
  tests** that call the formula: no standoff at any heading, the eye-frame trap,
  3.65 deg fixed pitch, mouse-look pitch adding to it, 62.8 deg hFOV round-
  tripped through three aspect ratios (and no NaN on a zero aspect), and the
  near plane clearing a standing human. A returning standoff constant fails.
* `tests/test_inventory_model_view.cpp` —
  `FirstPersonPitchClampKeepsTheBodyOutOfFrame_Headless` replaces the four
  deleted arm-filter tests (and the derived-plane test that briefly stood in
  their place), against real S3D meshes: it sweeps down-pitch and reports the
  angle at which each race's resting body enters the frustum, at 1x and at 5x
  scale, plus how much mid-swing geometry is in frame at level pitch. **Those
  numbers do not agree with the clamp — see §4's table and the reasons they are
  reported rather than asserted.** What it does assert is that scaling the model
  UP can only bring the body in EARLIER (the 1.0 near plane is the sole
  scale-dependent term left, and it can only clip less as the model grows).

## 6. Verification (by EXIT STATUS)

(Re-run in full after each revision; these are the post-Leg-A numbers.)

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **EXIT=8** — `99% tests passed, 1 failed out of 3227`; the single failure is `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host exception |
| `bash scripts/build-gles2-x86.sh` | **EXIT=0** |
| `bash scripts/ci/visual_gate.sh` | **EXIT=0, `=== VISUAL GATE: PASS ===`**, goldens untouched (`git status` shows no image changes) |
| `bash scripts/build-arm-noble.sh` | **EXIT=0** |
| `bash scripts/refactor/check_file_sizes.sh fable` | 2 FAILs, **both pre-existing**: `entity_renderer.cpp` 3284 (was 3313 — **shrank 29**), `irrlicht_renderer.cpp` 3104 (was 3066 — **+38 lines**, deviation noted below). No new FAIL. |

**Deviation:** `irrlicht_renderer.cpp` gained 38 lines while already over the
3000-line limit (it was FAILing before this unit; no new FAIL, and
`entity_renderer.cpp` shrank by 29). `applyCameraProjectionForMode()`,
`applyFirstPersonPitchClamp()` and `getFirstPersonVerticalFov()` belong with the
camera they configure; the standoff/pitch constants and their long comments
moved out to the new header, which is why the net is as small as it is. Flagged
rather than absorbed; splitting that TU is its own unit.

## 7. What this leg could NOT settle

1. **The clamp angle itself.** 90 - vFOV/2 = 65.40 is an inference consistent
   with Leg A's "roughly 65, bracket 55-75", not a measured angle. A dedicated
   measurement is the outstanding follow-up.
2. **Whether the clamp actually hides fable's body.** This leg's own geometry
   measurement says NO for every race (§4's table; Troll and Iksar are in frame
   at level pitch), with two named reasons the measurement may be misframed. It
   is a concrete prediction for Leg C, and the first thing to look at.
3. **Whether shrink/grow reaches the renderer.** It does not — fable's handlers
   are stubs (`spell_effects.cpp:883-891`), so the effect has no visible result
   on player or NPC. The clamp does not care (it takes no size term), but the
   defect stands on its own and is on the board.
4. **Anything visual at all.** Nothing in this leg was rendered to a screen.
