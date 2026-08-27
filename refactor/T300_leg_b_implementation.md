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
near plane      = derived from the player's own model size, first person ONLY (section 4)
```

| term | before | after | source |
|---|---|---|---|
| forward standoff | 0.3 **backward** | **0.0** | T298 A3: 0.00 +/- 0.4 |
| lateral | 0 | 0 | T298 A3: 0.00 +/- 0.05 |
| eye | `playerZ_ + 3.6527` | unchanged (see §3) | T298 A3: 6.75 +/- 0.20 above server z |
| fixed pitch | 12.0 deg down | **3.65 deg down** | T298 A3: 3.65 +/- 0.15 |
| hFOV | never set -> Irrlicht default 72 deg **vertical** ~= **88 deg horizontal** at 4:3 | **62.8 deg horizontal** (49.2 vertical at 4:3) | T298 A3: 62.8 +/- 0.3 |
| near plane | 1.0 (world default) | derived per frame from the player's model in first person; 1.0 restored on exit | section 4; T300 Leg A (pending) |

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

## 4. The near plane — DERIVED from the player's current model, not a constant

**Revised after the coordinator's follow-up; Will's objection was correct.** A
near plane expressed as a fixed distance in EQ units is wrong twice over:
races are all different sizes (my own static table already showed a Dwarf male
poking through a 7.0 plane while a Human female sat 0.9 inside it), and
shrink/grow effects change a model's scale at RUNTIME, so any fixed value can
be walked through mid-fight in either direction.

**What ships now:**

```
nearPlane = EntityRenderer::getPlayerBodyRestRadius()   // world units, live scale
          * kFirstPersonNearPlaneMargin                 // dimensionless, 1.02
```

* `kFirstPersonNearPlaneUnits` (a distance) is **gone**. The surviving constant
  is **`kFirstPersonNearPlaneMargin`, dimensionless** — the multiple of the
  player's own rest radius. `kFirstPersonNearPlaneFallbackUnits` (7.0) is used
  only while no player model has loaded, so the plane is never zero.
* `firstPersonNearPlaneFromBodyRadius(restRadius)` is the derivation, in the
  header, so production and tests cannot disagree.
* **Recomputed every frame**, not once at mode entry:
  `applyFirstPersonPlayerHiding()` runs from `renderer_frame.cpp` immediately
  before `smgr_->drawAll()` and re-applies the plane whenever the derived value
  moves by more than 0.001. A shrink or grow landing mid-first-person moves the
  plane the next frame; the `[FP-HIDE]` log line records every change with the
  radius and margin that produced it.

**The scale term, and whether it reflects the size spell effect — asked
directly, answered honestly: NO, and not because the term is wrong.**

* The term read is **the live scene node's own scale**,
  `visual.animatedNode->getScale()` (largest component, so a non-uniform scale
  cannot under-report). That is the value the GPU actually draws with, so it
  reflects **whatever last wrote it**: the race base scale and the server's
  spawn `size` field (`entity_renderer.cpp:492`,
  `scale *= serverSize / 6.0`), applied on both the synchronous
  `buildEntityMesh()` path and the deferred `NodeSetup` path.
* **fable has no runtime size-change path at all today.** `serverSize` is
  consumed only at entity creation; there is no size-update handler anywhere in
  `src/`, and the spell effect's own handlers are **stubs**:
  `SpellEffects::handleShrink()` / `handleGrow()`
  (`src/client/spell/spell_effects.cpp:883-891`) are one `LOG_DEBUG` each and
  write nothing. SPA 89 (`SpellEffect::ModelSize`) is decoded and dispatched
  (`spell_effects.cpp:399-408`) and then dropped on the floor.
* So the honest statement is: **the term is the right one and will track a size
  change the instant anything applies one, but nothing applies one today, so
  the claim "this reflects the size spell effect" is NOT demonstrable in fable
  as it stands.** That is a class-A defect in its own right — shrink/grow is a
  visible, commonly used effect that fable silently ignores — and it is now on
  the board. Any fix for it will land on `setScale()` and be picked up here for
  free.

**Why the base mesh's bounding box, and not the live pose.** `getPlayerBodyRestRadius()`
takes the furthest of the 8 base-mesh bbox corners from the eye point
(`bbox.MaxEdge.Y`), scaled. The rest radius must be **stable**: a pose-tracking
radius would grow during a swing and push the plane out over exactly the
geometry that has to stay visible. The base box spans every animation frame, so
it over-reports — measured below — and that slack is the price of stability and
of an O(1) per-frame check.

**Measured per race, this unit, against real S3D meshes**
(`FirstPersonNearPlaneSwallowsTheBody_Headless`; node scale 1, production
multiplies all of it by the live scale):

| race | eye | rest radius (furthest idle vertex) | bbox proxy (what production reads) | derived plane | attack (body only) |
|---|---|---|---|---|---|
| Human male | 3.653 | 6.873 | 7.375 | **7.523** | 6.976 |
| Human female | 3.083 | 6.139 | 6.324 | **6.450** | 6.150 |
| Barbarian male | 3.647 | 6.846 | 6.957 | **7.096** | 6.942 |
| Dwarf male | 3.745 | 7.092 | 7.488 | **7.638** | 8.151 |
| Dark Elf female | 3.096 | 6.160 | 6.467 | **6.597** | 6.165 |
| Troll male | 3.482 | 6.882 | 8.659 | **8.832** | 7.566 |
| Iksar male | 3.552 | 6.884 | 7.859 | **8.016** | 7.383 |

Every race is now swallowed by its own plane — including the Dwarf that broke
the fixed constant. The test asserts that for every race, and separately that
the bbox proxy never UNDER-reports the true furthest vertex (an under-report
would put the plane inside the body).

**⚠ UNVALIDATED, AND NOT TO BE PRESENTED OTHERWISE.** The margin 1.02 is a
placeholder chosen to sit just above the hard lower bound, and **the derivation
itself is a hypothesis**: that the official client's hiding scales with model
size is exactly one of the three possibilities T300 Leg A is now testing —
(a) one global near plane, (b) a size-derived plane, (c) player-specific
culling that is not a near plane at all. Only (b) is what this code assumes.
Nothing in this leg has been seen on a screen. Neither the margin nor the
derivation is settled until Leg A reports and the live gate (Leg C) runs.

**Structured so the mechanism can be swapped, not rewritten.** Everything that
hides the player lives behind two names: `firstPersonNearPlaneFromBodyRadius()`
in the header and `IrrlichtRenderer::applyFirstPersonPlayerHiding()` (its only
production caller). If Leg A comes back (c), those two bodies are what change —
per-object culling would be applied in the same seam, called from the same
per-frame point — and the camera formula, the eye-height frame work, the
deletions and the tests' structure are all untouched. Dropping in a measured
margin is a one-line edit to the constant.

## 5. Tests

**Nothing headless can close this feature, and nothing here claims to.** No
test in this suite rasterizes: they measure geometry and parse source. They
cannot show that the body is clipped, that a swing is visible, or that the
projection reached the GPU. T296 shipped a depth clear that was silently
compiled out by a wrong `#ifdef` and **every headless test still passed** —
that is the standing proof of this limit, and it is written into both test
files. **T300 Leg C's live video is the gate; Will judges the look.**

New/changed coverage:

* `tests/test_first_person_camera_single_formula.cpp` — `NearPlaneIsDerivedFromModelSizeNotFixed`
  pins the derivation itself: margin > 1 (a margin <= 1 puts the plane inside
  the body) and < 1.30 (a large margin swallows the swing too), the plane
  scaling with the radius in both directions (the shrink/grow case, asserted as
  an ordering so a future non-linear derivation still has to satisfy it), and
  "no model yet" never yielding a zero or negative plane. Plus the source-parsing
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
  filter tests, against real S3D meshes: for EVERY race, the plane derived from
  that race's own model must clear that race's resting body, and the cheap
  bbox proxy production reads must never under-report the true furthest vertex.
  Rest/attack/proxy/plane figures are printed per race (table above).

## 6. Verification (by EXIT STATUS)

(Re-run in full after the near-plane derivation change.)

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **EXIT=8** — `99% tests passed, 1 failed out of 3223`; the single failure is `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host exception |
| `bash scripts/build-gles2-x86.sh` | **EXIT=0** |
| `bash scripts/ci/visual_gate.sh` | **EXIT=0, `=== VISUAL GATE: PASS ===`**, goldens untouched (`git status` shows no image changes) |
| `bash scripts/build-arm-noble.sh` | **EXIT=0** |
| `bash scripts/refactor/check_file_sizes.sh fable` | 2 FAILs, **both pre-existing**: `entity_renderer.cpp` 3328 (was 3313 — +15, `getPlayerBodyRestRadius()`), `irrlicht_renderer.cpp` 3100 (was 3066 — **+34 lines**, deviation noted below). No new FAIL. |

**Deviation:** `irrlicht_renderer.cpp` gained 34 lines and `entity_renderer.cpp`
15, both while already over the 3000-line limit (both were FAILing before this
unit; no new FAIL). `applyCameraProjectionForMode()` and
`applyFirstPersonPlayerHiding()` belong with the camera they configure, and
`getPlayerBodyRestRadius()` belongs with the entity data it reads; the
standoff/pitch constants and their long comments moved out to the new header,
which is why the net is as small as it is. Flagged rather than absorbed;
splitting those TUs is its own unit.

## 7. What this leg could NOT settle

1. **The margin, and whether a size-derived plane is the mechanism at all.**
   Placeholder 1.02; Leg A is testing plane-vs-culling by growing/shrinking the
   character and walking flush into walls. If it reports per-object culling,
   the two-name seam in §4 is what gets replaced.
2. **Whether shrink/grow reaches the renderer.** It does not — fable's handlers
   are stubs (`spell_effects.cpp:883-891`). The derivation is ready for it; the
   effect is not implemented. Raised as a separate class-A defect.
3. **Whether the clip is acceptable to play with** (world geometry within
   ~7.5 units of the eye vanishing in first person). Live gate, Will's
   judgement.
4. **Anything visual at all.** Nothing in this leg was rendered to a screen.
