# T301 Leg C — why the weapon is never visible in first person

Executor unit, slot3, branch `task/T301-legCD` (Legs C and D), cut from `fable`
@ `ffb7e4ca`. Contract: `refactor/T301_first_person_eye_placement.md`, Legs C
and D. No server; Leg A owns eqemu and the official client, Leg B owns
`first_person_camera.*` and the eye-height functions. Neither was touched.

## Verdict

**The weapon is not clipped, not culled, not hidden and not deferred. It is in
the wrong place.** The player's own equipment is attached at a yaw of
`(-2 * nodeRot.Y - 90)` degrees away from the hand that is supposed to be
holding it. At heading 0 that puts a Halberd **behind the camera**; at heading
90 it is 39 degrees off the look axis; the error is zero only near heading 45.
A first-person frustum is 62.8 degrees wide (+/- 31.4 from the look axis), so
the weapon falls outside it at nearly every heading — which is T300 Leg C's
285-frame, seven-exchange, zero-weapon negative.

**This is fable's own defect, in this leg's territory, and it is fixed here.**
It is NOT Leg B's: no eye height and no clamp value can bring a weapon that is
behind the camera into frame.

## The mechanism, with file:line

| # | fact | evidence |
|---|---|---|
| 1 | The PLAYER's animated node bakes a Y rotation of `(nodeRot.Y + 90)` degrees into its per-instance VERTICES | `src/client/graphics/eq/animated_mesh_scene_node.cpp:733-740` (`applyAnimation()`), applied at `:210-220` in `applyBoneTransformsToMeshImpl()` about the EQ Z axis |
| 2 | ...and then draws that node with a **translation-only** world transform, dropping the node's own rotation and scale | `animated_mesh_scene_node.cpp:776-786` (`render()`, `posOnlyTransform.setTranslation(...)`) |
| 3 | NPCs do neither: unbaked vertices, full `AbsoluteTransformation` | same two sites, the `else` arms |
| 4 | Equipment is an ordinary CHILD scene node of that same animated node, for players and NPCs alike | `src/client/graphics/entity_equipment.cpp:79` / `:110` (`smgr_->addMeshSceneNode(mesh, parentNode)`, `parentNode = visual.sceneNode`) |
| 5 | ...so it inherits `R(nodeRot.Y)` and the node's scale, which the player's body does not have | Irrlicht `ISceneNode::updateAbsolutePosition()` |
| 6 | The player node's rotation IS set, exactly like an NPC's | `src/client/graphics/entity_renderer.cpp:618` and `:1755` (`setRotation(0, -heading, 0)`) |
| 7 | Net: body at Irrlicht yaw `-(nodeRot.Y + 90)`, weapon at `+nodeRot.Y` | measured below |

Everything else the contract listed as a candidate was checked and cleared:

* **T296's late-attach hide is genuinely gone** — `entity_equipment.cpp:135-141`
  is a comment where it used to be, and `setPlayerFirstPersonMode()`
  (`entity_renderer.cpp:2775-2830`) now *forces* `setVisible(true)` on the node
  and on both equip nodes. Nothing in the tree reads `visual.isFirstPersonMode`
  except that assignment.
* **No per-node culling test** — `setAutomaticCulling` appears nowhere in
  `src/` or `include/`; body and weapon get Irrlicht's identical default.
* **Equipment transforms do run for the player** — `entity_renderer.cpp:1507`
  and `:2045` both call `updateEquipmentTransforms(visual)` on the `isPlayer`
  arm before `continue`, and `setPlayerSpawnId()` (`:2940`) inserts the player
  into `activeEntities_` with the comment "Player always needs to be in active
  set for equipment transform updates".
* **The camera formula is not implicated.** The body's baked yaw and the
  camera's look direction agree exactly: with `nodeRot.Y = -heading`, the model's
  own forward `+X` maps to `(sin h, 0, cos h)`, which is
  `computeFirstPersonCamera()`'s look ray (`first_person_camera.h:214-216`).
  The equipment is the odd one out, not the camera and not the body.

## The measurement

`tests/test_first_person_weapon_attachment.cpp` drives the real
`EntityRenderer` — real `createEntity(isPlayer=true)` / `setPlayerSpawnId()` /
`setPlayerEntityAnimation("c01")` / `updateMainThreadEntityState()`, real
`global_chr.s3d` and `gequip*.s3d` meshes — finds the equipment node by
**walking the scene graph**, and expresses its world position in the frame of
the real first-person camera. Human male, primary attack animation, one
position, five headings:

| heading | BEFORE: right / up / forward | yaw off the look axis | AFTER (fixed) |
|---|---|---|---|
| 0 | 0.678 / -1.930 / **-0.826** | **140.6 deg** (behind the camera) | 0.827 / -1.930 / 0.678, 50.6 deg |
| 45 | 0.827 / -1.930 / 0.678 | 50.6 deg | identical |
| 90 | -0.678 / -1.930 / 0.827 | -39.4 deg | identical |
| 180 | 0.678 / -1.930 / -0.826 | 140.6 deg | identical |
| 270 | -0.678 / -1.930 / 0.827 | -39.4 deg | identical |

Read the BEFORE column as a player turning on the spot: **his own weapon swings
a full 180 degrees around him between heading 0 and heading 90**, and spends
most of the circle behind his head. AFTER, it does not move relative to his
eyes at all — the three components agree to within 1.2e-5 across all five
headings.

The same divergence measured directly against the baked body geometry, before
the fix (attachment point vs. where the body draws that same `R_POINT` bone):

```
Human male h=0    attach=(0.539, 1.251, -0.842)  baked=(0.842, 1.251, 0.539)  sep=1.414
Human male h=45   attach=(0.977, 1.251, -0.214)  baked=(0.977, 1.251, -0.214) sep=6.7e-07
Human male h=90   attach=(0.842, 1.251,  0.539)  baked=(0.539, 1.251, -0.842) sep=1.414
Troll male h=0    attach=(0.968,-0.281, -2.904)  baked=(2.074,-0.201,  0.691) sep=3.762
```

## The fix

`entity_equipment.cpp`'s `updateEquipmentTransforms()` gains one lambda,
`placeAttachment()`, used by both the primary and the secondary slot. For an
NPC (`playerBake == false`) it is the pre-T301 two lines, bit for bit. For the
player it maps the bone-derived placement into the frame the baked body is
actually drawn in:

* position — the bone offset is un-scaled (the baked body is drawn without the
  node's scale) and rotated by
  `playerEquipmentYawCompensationDegrees(nodeRot.Y) = -2*nodeRot.Y - 90`;
  the grip offset stays in the child's own scaled units with the mesh it came
  from;
* rotation — the same compensation composed onto the item's rotation as a
  matrix (`playerYawFix * itemMat`), not by adding Euler components.

The derivation and the reason it exists live in
`include/client/graphics/eq/bone_attachment.h` next to the function, so the
next reader meets it where the math is.

## What this does NOT prove

* **It does not rasterize.** The test measures where the weapon is in the
  camera's frame. Whether a swing is SEEN also depends on the eye height (Leg B)
  and the down-pitch clamp. Note the `up = -1.93` column: at rest the grip sits
  nearly two units BELOW the crown-mounted eye and 50 degrees off the look axis
  — outside the 31.4-degree half-FOV. **A swing has to raise it into frame, and
  only Leg E's video can say whether it does.** Do not read this leg as a
  prediction that the weapon will now be visible; read it as "the weapon is
  finally in the player's hand, where the rest of the mechanism can act on it".
* **Third person changes too**, and that is the point: the player's weapon has
  been misplaced there as well, for every heading but 45 degrees. It was
  "plainly visible" in T300's control frame — visible, not correct. Nothing in
  the visual gate's goldens covers an equipped player, and the gate passes
  untouched.

## Two adjacent defects found, NOT fixed here (out of territory, reported)

1. **The player's race scale is dropped entirely.** `render()`'s
   translation-only transform discards the node scale, and `applyAnimation()`
   never applies it, so a player's body draws at 1.0x whatever
   `getRaceScale()` says while every NPC of the same race is scaled. Visible in
   the Troll rows above (`attach.Y = -0.281` vs `baked.Y = -0.201`, ratio 1.4).
   Invisible today only because Testchar is a Human (scale 1.0). Its natural
   home is `animated_mesh_scene_node.cpp`, not this leg.
2. **Hidden UI still eats mouse clicks** — `renderer_frame.cpp`'s
   `staticUIInput_->processInput()` runs regardless of `allUIHidden_`. Left
   alone deliberately (Leg D changed the draw gate only).

## Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (exit 0) |
| `ctest --test-dir build -j6` | **PASS** — 3234/3235, sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| new test fails without the fix | **YES** — verified by disabling the compensation and re-running: 6 EXPECTs fail, including "the weapon is BEHIND the first-person camera" |
| `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| `scripts/ci/visual_gate.sh` | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| `check_file_sizes.sh fable` | one FAIL, `irrlicht_renderer.cpp` 3115 > 3000 — **pre-existing** (3104 at `fable`); this unit added 11 lines of if-chain that have to live in the if-chain |
