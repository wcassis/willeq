# T300 Leg B — first person gets the real mechanism (the code change)

Executor unit, slot1, branch `task/T300-legB`, cut from `fable` @ `c194bc16`.
Contract: `refactor/T300_firstperson_real_mechanism.md`, **Leg B only**. No
server, no oracle, no Pi — Leg A owns eqemu concurrently in slot2.

**The mechanism, as implemented:** first person is the ORDINARY player model,
drawn normally with its equipment, camera AT THE EYES, hidden by a near clip
plane large enough to swallow the body. There is no second render pass, no
geometry filter, no hidden node and no standoff. T299 settled this against two
client versions: nothing of the player renders at rest at ANY pitch — including
aimed straight down at feet 6.75 units below the eye — yet the SAME leg
reappears the instant a kick extends it, and the blade appears on every swing,
always cut cleanly with the hand and elbow never drawn. **Geometry that
reappears was clipped, not omitted.**

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
near plane      = kFirstPersonNearPlaneUnits, first person ONLY
```

| term | before | after | source |
|---|---|---|---|
| forward standoff | 0.3 **backward** | **0.0** | T298 A3: 0.00 +/- 0.4 |
| lateral | 0 | 0 | T298 A3: 0.00 +/- 0.05 |
| eye | `playerZ_ + 3.6527` | unchanged (see §3) | T298 A3: 6.75 +/- 0.20 above server z |
| fixed pitch | 12.0 deg down | **3.65 deg down** | T298 A3: 3.65 +/- 0.15 |
| hFOV | never set -> Irrlicht default 72 deg **vertical** ~= **88 deg horizontal** at 4:3 | **62.8 deg horizontal** (49.2 vertical at 4:3) | T298 A3: 62.8 +/- 0.3 |
| near plane | 1.0 (world default) | `kFirstPersonNearPlaneUnits` in first person; 1.0 restored on exit | T300 Leg A (pending) |

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

## 4. The near plane

**Parameter: `EQT::Graphics::kFirstPersonNearPlaneUnits`**, in
`include/client/graphics/first_person_camera.h`.
**Placeholder value: 7.0 EQ units. NOT a measurement.** Leg A is measuring it
live off a two-hander's truncation cut; relaying that number is a one-line edit
to that constant plus re-running the two tests named below.

The bracket the shipped evidence supports:

* **hard lower bound > 6.75** — T299 saw nothing of the body with the camera
  aimed straight down at feet T298 measured 6.75 units below the eye;
* **independent corroboration from fable's own meshes, measured this unit**: a
  Human male's resting body reaches **6.87** units from its own eye (max radial
  vertex distance over the idle animation, real S3D mesh data);
* **soft upper bound** — less than the reach of an extended weapon or kick,
  because those DO render. T299 could not size that reach; that is exactly what
  Leg A is solving for.

7.0 is the smallest round value clearing both the 6.75 bound and the 6.87
measurement. It clears a Human male's resting body by **0.13 units** — a thin
margin, deliberately not padded, because padding it would be inventing the
number Leg A is measuring.

**Measured per race (this unit, `FirstPersonNearPlaneSwallowsTheBody_Headless`,
max radial distance from the eye to the player's own vertices):**

| race | eye above node origin | AT REST (must be < plane) | IN ATTACK (may exceed — that is the visible swing) |
|---|---|---|---|
| Human male | 3.653 | **6.873** | 6.976 |
| Human female | 3.083 | 6.139 | 6.150 |
| Barbarian male | 3.647 | 6.846 | 6.942 |
| Dwarf male | 3.745 | **7.092 — NOT swallowed by a 7.0 plane** | 8.151 |
| Dark Elf female | 3.096 | 6.160 | 6.165 |
| Troll male | 3.482 | 6.882 | 7.566 |
| Iksar male | 3.552 | 6.884 | 7.383 |

**⚠ Open question this raises: one fixed plane is a human-sized number.** A
Dwarf's own body reaches beyond a 7.0 plane at rest, so a dwarf would see part
of himself. Whether the official client scales the plane per race is unmeasured
(T298/T299 measured a human). Flagged, not silently absorbed; decide with Leg
A's number in hand. Note also that these figures are in **unscaled model
units** — `createAnimatedNode()` applies a per-race node scale that this
measurement does not, so the real-world requirement moves with that scale.

**⚠ Cost of the mechanism, stated plainly.** A ~7-unit near plane is large
against a 6.70-unit character. World geometry within 7 units of the eye — a
wall the player stands against, the ground when looking steeply down — will
clip away in first person. That is a property of the mechanism the official
client uses, not something introduced here, and it is applied in first person
ONLY. Whether it is acceptable is Will's call on Leg C's video, not this leg's.

## 5. Tests

**Nothing headless can close this feature, and nothing here claims to.** No
test in this suite rasterizes: they measure geometry and parse source. They
cannot show that the body is clipped, that a swing is visible, or that the
projection reached the GPU. T296 shipped a depth clear that was silently
compiled out by a wrong `#ifdef` and **every headless test still passed** —
that is the standing proof of this limit, and it is written into both test
files. **T300 Leg C's live video is the gate; Will judges the look.**

New/changed coverage:

* `tests/test_first_person_camera_single_formula.cpp` — the source-parsing
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
  `FirstPersonNearPlaneSwallowsTheBody_Headless` replaces the four deleted arm-
  filter tests, against real S3D meshes: rest-vs-attack radial extents per race
  (table above), hard assertion on Human male, per-race figures reported rather
  than asserted because the per-race plane is an open question.

## 6. Verification (by EXIT STATUS)

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **EXIT=8** — `99% tests passed, 1 failed out of 3223`; the single failure is `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host exception |
| `bash scripts/build-gles2-x86.sh` | **EXIT=0** |
| `bash scripts/ci/visual_gate.sh` | **EXIT=0, `=== VISUAL GATE: PASS ===`**, goldens untouched (`git status` shows no image changes) |
| `bash scripts/build-arm-noble.sh` | **EXIT=0** |
| `bash scripts/refactor/check_file_sizes.sh fable` | 2 FAILs, **both pre-existing**: `entity_renderer.cpp` 3283 (was 3313 — shrank), `irrlicht_renderer.cpp` 3074 (was 3066 — **+8 lines**, deviation noted below). No new FAIL. |

**Deviation:** `irrlicht_renderer.cpp` gained 8 lines while already over the
3000-line limit. `applyCameraProjectionForMode()` is 15 lines and belongs with
the camera it configures; the net is small because the standoff/pitch constants
and their long comments moved out to the new header. Flagged rather than
absorbed; splitting that TU is its own unit.

## 7. What this leg could NOT settle

1. **The near-plane value.** Placeholder 7.0, bracket (6.87, weapon reach].
   Leg A owns it.
2. **Whether one plane fits every race.** A Dwarf's resting body exceeds 7.0.
   Needs Leg A's number and a product call.
3. **Whether the clip is acceptable to play with** (walls and ground vanishing
   within 7 units). Live gate, Will's judgement.
4. **Anything visual at all.** Nothing in this leg was rendered to a screen.
