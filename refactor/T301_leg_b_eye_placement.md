# T301 Leg B — Anatomical, pose-stable eye placement

Contract: `refactor/T301_first_person_eye_placement.md`, Leg B. No server.
Branch `task/T301-legB`, based on `fable` @ `ffb7e4ca`.

---

## 1. The head anchor exists, and it is not what anyone looked for

`getBoneIndexByName("HEAD")` returned -1 live (T295, `source=BBOX_FALLBACK`)
and T300 concluded the lever was disabled. **It was misspelled.** No EQ
skeleton has ever contained a bone called `HEAD`. Every playable skeleton
names its head bone `<modelCode>he...`, lowercase, race-prefixed.

Established by loading all 30 shipped playable race/gender models through
`RaceModelLoader` against the real client tree and dumping every bone name
and its skinned vertex cluster. **All 30 resolve, and all 30 carry real
geometry on that bone (58–916 vertices).**

| skeleton | head bone | skeleton | head bone | skeleton | head bone |
|---|---|---|---|---|---|
| HUM/HUF | `humhe`/`hufhe` | DAM/DAF | `damhe`/`dafhe` | HOM/HOF | `homhe`/`hofhe` |
| BAM/BAF | `bamhe`/`bafhe` | HAM/HAF | `hamhe`/`hafhe` | GNM/GNF | `gnmhe`/`gnfhe` |
| ERM/ERF | `ermhe`/`erfhe` | DWM/DWF | `dwmhe`/`dwfhe` | IKM/IKF | `ikmhe`/`ikfhe` |
| ELM/ELF | `elmhe`/`elfhe` | TRM/TRF | `trmhe`/`trfhe` | KEM/KEF | `kemhe`/`kefhe` |
| HIM/HIF | `himhe`/`hifhe` | OGM/OGF | `ogmhe`/`ogfhe` | FRM/FRF | `frmhehead`/`frfhehead` |

Two traps, both real in the shipped data and both pinned by tests:

- **`<code>head_point` is an attachment point**, not the head — it hangs a
  helm and carries no geometry. It also begins with `<code>he`. So does
  `frmhehead_point`. Every `*_point` name is rejected.
- **`bamhe2`** (Barbarian male) is a second head part. The shortest surviving
  remainder wins, so plain `bamhe` is taken.

The bone ORIGIN is not the anchor either — it is the neck joint, at the base
of the skull, wrong in the opposite direction from the crown. The eye is
derived from the head bone's **own vertex cluster**. There is no hand-written
per-race table anywhere in this path.

### The beard trap (found by measuring, not by reasoning)

A naive `minZ + f*(maxZ - minZ)` over the head cluster fails on the Dwarf
male: `dwmhe` owns his **beard**, which hangs to bone-local Z **-2.29**
against a crown at **+0.85**. That put the "eye" at model Z 1.48 — **2.27
below the crown, in his chest** (a Human male's is 0.63). Halfling, Gnome and
Troll hair does a milder version of the same.

The head joint is bone-local Z 0 by construction and a skull reaches about as
far below the joint as above it, so the lower bound is
`max(minZ, -maxZ)`. For the Dwarf that discards the beard (eye moves 1.48 →
**2.51**); for Troll (`[-0.53, 1.11]`), Iksar (`[0.01, 0.74]`) and Froglok
(`[-0.60, 0.60]`), whose clusters are already tighter than symmetric, it
changes nothing.

---

## 2. The pose decision: **posture-driven** (Leg A refuted my first answer)

`kFirstPersonEyeSource = EyePoseSource::PostureDriven`.

**This leg first shipped `AnimatedPose`**, argued from "an eye inside the
skull cannot be bobbed through, so the defect closes structurally". **Leg A
Q3 measured it and the argument is wrong.** The actor pinned against a wall
holding forward produced **one distinct rendered frame in 12 seconds with zero
pixel change in his own view**, while the observing client watched his crown
oscillate between rows 372 and 389 over ~10 gait cycles — a 0.16-unit head
bob. The official eye does not follow the skeleton at all.

The argument for following was not merely unsupported, it was **moot**: the
official client tolerates the theoretical bob-through because it never draws
the body. I was solving a problem that the real mechanism makes irrelevant.

But it is not a rest-pose eye either — **sitting drops it 3.6 units, to
0.47 H**. So the eye is driven from the **discrete posture state**, which is a
third option neither of my branches implemented.

`EntityVisual::poseState` is exactly the right input and already exists: it
arrives on `SpawnAppearance` and is **deliberately never written by movement
updates** (its own comment says so), which is precisely the "discrete posture,
never the animated pose" distinction Leg A's measurement draws. No new
plumbing was needed.

`EyePoseSource` is a three-way enum, all three branches compiled, so the
comparison stays real and reversible. `AnimatedPose` is retained **with its
refutation recorded next to it**, so nobody re-derives my original argument
and re-flips it.

### Crouching and lying are UNMEASURED

Leg A measured standing and sitting only. Both other postures take the **sit**
drop, flagged as unmeasured in the source and asserted as such in a test.
Inventing two numbers that read as measured would be worse than an honest
reuse. A prone eye is certainly lower than a seated one; measuring both is an
outstanding follow-up.

## 3. Leg A's numbers, as set

| constant | value | source |
|---|---|---|
| `kEyeHeightFractionOfStandingHeight` | `0.925` | MEASURED (cross-check only — see below) |
| `kCrownAboveEyeUnits` | `0.60` | MEASURED — **what the eye is keyed to** |
| `kEyeHeightFractionOfSkull` | `0.574` | solved so a Human male lands at crown − 0.60 |
| `kEyeForwardOffsetUnits` | `1.0` | MEASURED +0.9 ± 0.25; **chosen at the top of Leg A's 0–1.0 range** |
| `kFirstPersonMaxDownPitchDegrees` | `94.9` | MEASURED (was 65 interim; T300's 55–75 bracket refuted) |
| `kSitEyeDropFractionOfHeight` | `0.455` | MEASURED (0.925 H → 0.47 H) |
| `kFirstPersonEyeSource` | `PostureDriven` | MEASURED (Leg A Q3) |

### Why the eye is keyed to crown − 0.60 and not to 0.925 H

**Leg A's two height numbers disagree.** 0.925 H puts a Human male's eye at
model Z 3.17; crown − 0.60 puts it at 3.07. The difference is the **frame**:
fable stands the model *centred* on the server z, the official client stands it
*feet-on-z* — a ~3-unit disagreement about placement that T300 already
documented — so "standing height" does not name the same span in the two
clients. Leg A's own figures show the seam: the sit drop is 0.455 H and is
reported as **3.6 units**, which implies H = 7.9 where this mesh's crown-to-feet
span is **6.67**.

crown-minus-eye is measured **inside the head** and is immune to that question,
so it is what the skull fraction is solved against. It is one global constant;
every race's value still comes from its own head cluster.

**0.925 H is then asserted as an independent per-race cross-check, and it
agrees well.** Measured eye height as a fraction of each mesh's own
crown-to-feet span:

- **26 of 30 combinations land within ±0.03 of 0.925.**
- Outliers: Dwarf male −0.111, Halfling male −0.048, Gnome male −0.041, Troll
  male −0.030 — precisely the races whose **beard or hair is skinned to the
  head bone**, which drags the mesh's own floor down and inflates H. Explained,
  not mysterious.

Two measurements taken by completely different methods — Leg A's screen-space
feet-ladder fit against the official client, and a head-vertex-cluster
derivation against the shipped meshes — agreeing to ±0.03 on 26 races is the
strongest evidence this leg has that the eye is now in the right place.

### The forward offset: I picked the top of the range, and why

Leg A measured **+0.9 ± 0.25 forward**, which **disagrees with T298's
0.00 ± 0.4 at ~1.9 sigma**, and the rotation-parallax test that would settle it
could not be run. Leg A's instruction was to treat it as the range 0–1.0 and
pick the value that keeps the face out of frame.

**I chose 1.0, the far end.** Moving the eye forward carries it toward and then
through the face plane, so the further forward it sits the more of the
player's own face falls *behind* the camera where it cannot be drawn at all.
The failure being avoided — `review/T300/T300_C_12`, a screenshot of the
player's own face — is caused by an eye too far **back**, so the far end of the
range is the safe end and T298's 0.0 reintroduces the risk. Applied along the
**look ray**, so it needs no mesh-axis knowledge (the trap that broke T300 Leg
B). For a Human male the head centroid is 0.17 behind the origin, so the total
is **+0.83** from the vertical axis — inside the range, within 0.07 of Leg A's
central +0.9.

This supersedes T300's `NoStandoffInAnyDirection` test, which is replaced by
one asserting the offset is purely forward with zero lateral component (T298's
lateral 0.00 ± 0.05 stands; Leg A did not contradict it).

### The clamp reaches PAST the nadir, and that broke the camera

94.9° is ~5° past straight down. **A look-at camera on Irrlicht's default
`(0,1,0)` up vector degenerates as the look ray nears the nadir and flips as it
crosses**, so the measured value was unusable as-is. `computeFirstPersonCamera()`
now returns an explicit up vector:

```
U = (-sinH*sinP, cosP, -cosH*sinP)
```

It is exactly `(0,1,0)` at level pitch (so no ordinary view moves), becomes
`(sinH, 0, cosH)` at the nadir, and continues smoothly with a negative Y
component past it. `IrrlichtRenderer` applies it and **restores `(0,1,0)` when
leaving first person**, or third person would inherit the roll — the visual
gate's goldens are third-person renders. Unit length at every pitch and
perpendicular to forward at every pitch, both asserted.

A second, subtler consequence: **`atan2(dy, |horizontal|)` folds about −90**, so
every existing test that measured pitch that way silently inverted its clamp
comparison once the clamp passed the nadir (−92.65 reads as −87.35). The tests
now project onto the heading to keep the sign. That fold is a live trap for
anything else that measures this camera's pitch.

## 4. Files

New:
- `include/client/graphics/first_person_eye.h` — the constants, the switch,
  the anchor survey, and the pure derivation API.
- `src/client/graphics/first_person_eye.cpp` — name resolution, cluster
  accumulation, eye derivation. Pure; no Irrlicht.
- `src/client/graphics/eq/animated_mesh_eye.cpp` —
  `EQAnimatedMeshSceneNode::getFirstPersonEyeOffset()`. Split out of
  `animated_mesh_scene_node.cpp` (already large) per `check_file_sizes.sh`.
- `tests/test_first_person_eye_anatomy.cpp` — 15 tests.

Changed:
- `include/client/graphics/entity_renderer.h` /
  `src/client/graphics/entity_renderer.cpp` —
  `getPlayerEyeHeightAboveNodeOrigin()` (the bbox crown) **deleted**, replaced
  by `getPlayerFirstPersonEyeOffset()` returning a three-axis offset plus a
  `derivedFromHead` flag. The crown survives only as a logged FALLBACK for a
  model with no head anchor; it fires for none of the 30 shipped combinations.
- `include/client/graphics/first_person_camera.h` — clamp is a constant, FOV
  parameter removed, eye offset plumbed through.
- `src/client/graphics/irrlicht_renderer.cpp`,
  `src/client/graphics/renderer_player_physics.cpp` — call sites.
- `tests/test_first_person_camera_single_formula.cpp` — the FOV-derivation
  test is replaced by one that pins the clamp is a CONSTANT and that widening
  the lens does not move it.
- `tests/test_inventory_model_view.cpp` — **T300 Leg B's
  `FirstPersonPitchClampKeepsTheBodyOutOfFrame_Headless` and its per-race
  entry-angle table are STRUCK**, with the reasons in place so they are not
  restored: the axis assumption's error changes sign between races, and every
  angle was measured from the crown rather than from an eye.

### The 90-degree offset is reproduced, not harmonized

The PLAYER node bakes its Y rotation into the vertices with a **+90** offset
and renders with a translation-only world matrix;
`getBoneWorldPosition()` applies the raw `nodeRot.Y` and is therefore off by
90 degrees for the player node specifically. It could not be reused. Both
conventions are implemented separately in `animated_mesh_eye.cpp` with a
comment saying why. Players and NPCs genuinely do not share a default
rotation.

---

## 5. Measurements (headless, real shipped meshes)

All printed by the tests themselves, at Leg A's final constants.

### The eye lands where Leg A measured it

Human male: **crown − eye = 0.597** against Leg A's measured **0.60**. That is
what `kEyeHeightFractionOfSkull = 0.574` is solved for, and the test that
checks it is the one that catches the solve going stale.

Independent cross-check against Leg A's *other* number, the eye at 0.925 of
standing height, computed per race from each mesh's own crown-to-feet span:

| | |
|---|---|
| within ±0.03 of 0.925 | **26 of 30** combinations |
| Human male | 0.911 (−0.014) |
| Iksar male / female | 0.954 / 0.944 |
| Froglok male / female | 0.950 / 0.939 |
| worst outliers | Dwarf male 0.814 (−0.111), Halfling male 0.877, Gnome male 0.884, Troll male 0.895 |

Every outlier is a race whose **beard or hair is skinned to the head bone**,
which drags the mesh's own floor down and inflates H. Two measurements taken by
entirely different methods — Leg A's screen-space feet-ladder fit against the
official client, and a head-vertex-cluster derivation against the shipped
meshes — agreeing to ±0.03 on 26 races is the strongest evidence here that the
eye is now in the right place.

### The player's own head moves inside the near plane

Own-head vertices left **outside the ordinary 1.0-unit near plane**, crown eye
(what T300 shipped) vs head eye:

| race | crown | head | race | crown | head |
|---|---|---|---|---|---|
| Human male | 28 | **2** | Iksar male | 143 | **0** |
| Erudite male | 30 | **0** | Iksar female | 389 | **0** |
| Barbarian male | 19 | **0** | Froglok male | 55 | **0** |
| Human female | 0 | **0** | Froglok female | 54 | **0** |
| Troll male | 107 | 34 | Dwarf male | 116 | 83 |
| Troll female | 46 | 5 | Gnome male | 152 | 74 |
| Ogre male | 70 | 7 | Halfling male | 101 | 62 |
| Halfling female | 164 | 15 | Gnome female | 116 | 17 |

Zero for 18 of 30 and near-zero for more, including **both Iksars and both
Frogloks** — the races whose faces stuck out worst (389 and 143 vertices).
Every remaining residual is beard or hair skinned to the head bone, which
legitimately hangs more than a unit from the eye. **No race got worse.**

## 6. What these tests CANNOT do — and what Leg A settled

**Nothing in `tests/test_first_person_eye_anatomy.cpp` rasterizes.** No GPU, no
frame, no pixel. They cannot settle whether the player sees his own face,
whether the head bobs into frame, or whether the weapon is visible. T296
shipped a viewmodel pass whose depth clear was silently compiled out and every
headless test in this repo still passed.

The contract asked for a pin that "at max down-pitch no body geometry enters
the frustum for several races including Troll and Iksar". **That pin was never
shippable, and Leg A has now shown it was never true.**

My first pass reported the geometry: no clamp value can empty the down-frustum
band, because from any eye inside the head the body spans depression angles
from near 0 (the hands) to near 90 (the feet). For a Human male, hiding the
shoulders needs `C > 49.1` and hiding the feet needs `C < 48.4` — **empty by
about 0.7 degrees.**

**Leg A's control run explains it.** The measured clamp is 94.9°, past the
nadir, and an identical Human-male model teleported onto the actor's own
coordinates at that same clamp **fills 17.8% of the frame** with shoulders,
arms, hands, belt and hair. *The frustum contains the body. The official client
simply does not draw it.*

So band occupancy is no longer a figure of merit, and the test that asserted it
improved monotonically has been **converted into a pin on the refutation**: it
now asserts the band is **non-empty** for every race, so that a future unit
that pulls the clamp back under 90 and claims that hides the body is
contradicted by a test rather than by an argument. The per-race counts are
still printed, for the draw-rule leg.

(The monotonic assertion also had to go on its own merits: re-solving the eye
to Leg A's spec moved the Iksar male by 3 vertices out of ~1000 in the wrong
direction. Tuning to a dead metric to recover a green tick would have been
exactly the wrong instinct.)

**I did not implement the hiding rule.** The seam stays where T300 put it —
`kFirstPersonMaxDownPitchDegrees` and
`IrrlichtRenderer::applyFirstPersonPitchClamp()` — and the clamp is now
documented as a control-feel limit rather than the mechanism. The draw rule is
the other leg's to measure.

**T301 Leg E is still the gate for everything visual here.**

## 7. Verification

Run in a dedicated worktree — see the deviation below.

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (exit 0) |
| `ctest --test-dir build -j6` | **PASS** — 3246/3247; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| `scripts/ci/visual_gate.sh` | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| `scripts/refactor/check_file_sizes.sh fable` | **no new FAILs** — see below |

20 tests in `test_first_person_eye_anatomy.cpp` (5 new for Leg A's spec and
posture), 15 in `test_first_person_camera_single_formula.cpp`.

### Deviations

**1. My assigned slot was reassigned mid-unit.** `wt/slot2` is now on
`task/T306-daybreak-rdp` with **five modified files of another executor's
uncommitted work**; `slot1` holds `task/T307-draw-rule` and `slot3` holds
`task/T304-leg23` (dirty). Rather than clobber a peer's working tree to
reclaim a slot, I created a dedicated worktree at **`/mnt/projects/fable/wt/t3`**
on `task/T301-legB` and ran the full bar there. It needs its
`third_party/irrlicht-gles2-x86`, `data/eq_client`, `data/eqe1_client` and
`data/captures` links, which are gitignored build/fixture state — all four
were seeded. **Nothing outside `wt/t3` was written.** The worktree can be
removed with `git worktree remove wt/t3` once this branch is merged.

*(Note for anyone else landing in a fresh worktree: a missing `data/captures`
symlink does not make `test_zone_connection_flow` skip — it makes ctest **fail
at discovery** with a process timeout, which does not look like a missing
fixture at all.)*

**2. File sizes.** `entity_renderer.cpp` (3323) and `irrlicht_renderer.cpp`
(3125) FAIL the 3000-line ceiling. **Both were already over it on `fable`
(3211 and 3104).** All substantial new code went into the three new TUs
(`first_person_eye.cpp` 117, `animated_mesh_eye.cpp` 158, plus the test);
what landed in the two large files is comment and call-site plumbing.
`renderer_player_physics.cpp` carries the same pre-existing WARN at 1544.

## 8. Not my territory

- **Weapon visibility** (Leg C) — untouched. `animated_mesh_eye.cpp` adds a
  read-only accessor and changes no draw, no attachment and no cull.
- **A UI-hide command** (Leg D) — untouched.
- **The live gate** (Leg E) — nothing here has been judged on a screen, and
  this leg does not claim it has been.
- **The draw rule** (the new leg Leg A's control run opened) — deliberately
  NOT implemented. The hiding seam is left exactly where it was; the clamp is
  documented as a control-feel limit, not the mechanism.
