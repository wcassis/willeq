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

## 2. The animation-follow decision: **the eye FOLLOWS the animation**

`kFirstPersonEyeFollowsAnimation = true`
(`include/client/graphics/first_person_eye.h`).

**Justification.** Today's defect is that the run cycle lifts the animated
head above a static eye and the head bobs through frame (T300_C_20/21). A
rest-pose eye does not fix that — it moves the same defect 0.6 units down the
skull. An eye that rides the head bone's current transform is inside the skull
**in every frame of every animation by construction**, so the head can never
pass through the camera at all. The defect is closed structurally rather than
tuned away, which is worth more than the tuning it replaces given how many
times this feature has regressed.

**What is deliberately NOT followed: the head bone's ORIENTATION.** Only its
position is taken. The look direction stays entirely on the mouse, so the run
cycle's head yaw/roll never shakes the view. That is the line between "view
bob", which the official client has, and "the camera is parented to the
skull", which would be unplayable.

**It is one bool, and both branches are implemented and compiled.** If Leg A
Q3 reports the official client uses a rest-pose eye, flip it to `false`: the
rest branch takes the head bone's bind transform from a never-updated private
`SkeletalAnimator` and nothing else moves. The switch is the only difference
between the two paths — one `BoneMat4` selection in
`EQAnimatedMeshSceneNode::getFirstPersonEyeOffset()`.

---

## 3. What Leg A's numbers land on

Three constants and one bool, all in
`include/client/graphics/first_person_eye.h`. **Setting them is the change; no
formula moves.**

| constant | interim value | what Leg A supplies |
|---|---|---|
| `kEyeHeightFractionOfSkull` | `0.55` | the eye's height as a fraction of the model's own head, Leg A's model-relative spec |
| `kEyeForwardOffsetUnits` | `0.0` | any residual forward offset ON TOP of the head cluster's own forward position |
| `kFirstPersonMaxDownPitchDegrees` | `65.0` | the empirically measured max down-pitch (Leg A item 1) |
| `kFirstPersonEyeFollowsAnimation` | `true` | Leg A Q3 |

0.55 is the human-anatomy midpoint and is **not a measurement**; it is
labelled INTERIM in the source. Sanity cross-check: for a Human male it gives
model Z 3.04, 0.63 below the crown the old code used, and within 0.06 of the
3.10 T76's HEAD-bone preference reached by an unrelated route.

`kEyeForwardOffsetUnits` is applied along the **look ray**, which the camera
formula already computes from heading — so that constant needs no knowledge of
any mesh's baked axes. Each race's real forward position comes from the head
cluster's own centroid instead (Human male 0.17 *behind* the origin, Iksar
0.84 ahead, Froglok 1.48 — hunched models, not errors), which is why no axis
assumption is needed for it either.

### The clamp is now a separate, named constant — and the FOV parameter is GONE

`firstPersonMaxDownPitchDegrees(float verticalFovRadians)` became
`firstPersonMaxDownPitchDegrees()`, returning
`kFirstPersonMaxDownPitchDegrees`. The `verticalFovRadians` parameter was
**removed from `computeFirstPersonCamera()` as well**, deliberately: `90 -
vFOV/2` cannot be reintroduced by accident if the value it needs is no longer
in scope. `firstPersonMinLookPitchDegrees()` follows.

---

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

Printed by the tests themselves. Own-head vertices left **outside the ordinary
1.0-unit near plane**, crown eye (what T300 shipped) vs head eye:

| race | crown | head | race | crown | head |
|---|---|---|---|---|---|
| Human male | 28 | **0** | Iksar male | 143 | **0** |
| Erudite male | 30 | **0** | Iksar female | 389 | **0** |
| Barbarian male | 19 | **0** | Froglok male | 55 | **0** |
| Half Elf male | 20 | **0** | Froglok female | 54 | **0** |
| Troll male | 107 | 34 | Dwarf male | 116 | 89 |
| Ogre male | 70 | 7 | Gnome male | 152 | 69 |
| Halfling female | 164 | 10 | Halfling male | 101 | 58 |

Zero for 20 of 30, including **both Iksars and both Frogloks** — the races
whose faces stuck out worst. The residuals are beard and hair geometry
skinned to the head bone, which legitimately hangs more than a unit from the
eye. **No race got worse**, which is what the test asserts.

Own-body vertices in the clamped frustum's vertical band, crown vs head eye:
every race improves (e.g. Human male 515 → 364, Dwarf male 628 → 337, Iksar
female 853 → 531, Troll male 470 → 337).

---

## 6. What these tests CANNOT do — stated plainly

**Nothing in `tests/test_first_person_eye_anatomy.cpp` rasterizes.** No GPU,
no frame, no pixel. They cannot settle whether the player sees his own face,
whether the head bobs into frame, or whether the weapon is visible. T296
shipped a viewmodel pass whose depth clear was silently compiled out and every
headless test in this repo still passed. **T301 Leg E is the gate.**

Specifically, the contract asks for a pin that "at max down-pitch no body
geometry enters the frustum for several races including Troll and Iksar".
**That pin is not shippable headlessly and I did not fake it.** Deciding
whether a vertex is inside the frustum needs the camera's forward axis, which
is exactly what killed T300 Leg B's table (live vs headless 26.5 vs 39.5 for a
Human male, 60.6 vs 0 for a Troll — the error **changing sign between
races**). Re-deriving it here would have rebuilt the same broken measurement.

What IS asserted is the heading-independent part: the depression angle, which
needs only the up axis. And that measurement carries a finding:

> **The counts are not zero, and no clamp value can make them zero.** From any
> eye inside the head, the body spans depression angles from near 0 (the
> hands) to near 90 (the feet), so for every clamp `C` some part of the body
> lies in `[C - vFOV/2, C + vFOV/2]`. For a Human male, hiding the shoulders
> needs `C > 49.1` and hiding the feet needs `C < 48.4` — **the interval is
> empty by about 0.7 degrees.** Whether any of it is actually on screen
> depends on azimuth, which is live-only.

This is a concrete prediction for Leg A's item 2 ("does ANY of the official
player's own body appear at max down-pitch — the predicted mechanism says
'almost, but not quite'"). The geometry says *almost, but not quite* is
exactly right, and it says the residual is a sliver near the feet or the
shoulders rather than a comfortable margin. If Leg A confirms the official
client shows nothing at all, then something beyond the clamp is at work and
the eye fix alone will not close T300 Leg C's Q1.

---

## 7. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (exit 0) |
| `ctest --test-dir build -j6` | **PASS** — 3240/3241; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| `scripts/ci/visual_gate.sh` | **PASS**, goldens untouched (`git status` clean under `tests/goldens`) |
| `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| `scripts/refactor/check_file_sizes.sh fable` | **no new FAILs** — see deviation below |

Suite grew 3208 → 3241 (15 new tests here; the rest are the T302 tip's).

### Deviation

`check_file_sizes.sh` reports FAIL on `entity_renderer.cpp` (3308) and
`irrlicht_renderer.cpp` (3115). **Both were already over the 3000-line ceiling
on `fable` (3284 and 3104).** This unit adds +24 and +11 lines to them — all
comment and call-site plumbing; every piece of substantial new code went into
the three new TUs. `renderer_player_physics.cpp` is unchanged in length (1544,
the same WARN it carried before). No file crossed a threshold because of this
work.

---

## 8. Not my territory

- **Weapon visibility** (Leg C) — untouched. `animated_mesh_eye.cpp` adds a
  read-only accessor and changes no draw, no attachment and no cull.
- **A UI-hide command** (Leg D) — untouched.
- **The live gate** (Leg E) — nothing here has been judged on a screen, and
  this leg does not claim it has been.
