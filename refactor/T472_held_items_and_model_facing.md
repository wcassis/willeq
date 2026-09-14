# T472 — held items: the lantern is not drawn, the blade points the wrong way, the model faces the camera at the Chase view (B586, B587, B588)

Class A, **Leg A offline (no lock)**, slot2; Leg B (lock) is a later dispatch.
Read T466's Review (`refactor/T466_oracle_cameras_and_held_items.md`),
`review/T466/README.md` and the sheets under `review/T466/sheets/` (look at
`T466_heldA_sword_lantern_oracle_top_fable_bottom.png` first), BOARD B586,
B587, B588, and the attachment code the Pi triage located:
`entity_equipment.cpp` (R_POINT/SHIELD_POINT/L_POINT attach, 180° X / X+Z
fixes, the player-path yaw fix and un-scaled bone position),
`character_model_view.cpp` (the paperdoll's copy), `bone_attachment.h` (shared
maths), `tests/test_bone_attachment.cpp`,
`tests/test_first_person_weapon_attachment.cpp`.

## What T466 saw (descriptive — Will judges)

- **Short Sword:** oracle blade hangs DOWN from the right fist; fable's stands
  UP above the fist.
- **Gloomingdeep Lantern** (left hand): oracle at the hip in the left hand;
  **fable draws nothing.**
- **Model facing (B587):** at the camera that reproduces the oracle's Chase
  frame, fable shows the character's FACE where the oracle shows the back of
  the head. ⚠ The 90° player/NPC heading offset is load-bearing
  (`docs/CLAUDE.md`, memory `heading-90-offset-load-bearing`) — establish
  whether this is the model yaw, the camera azimuth (Will ACCEPTED the mirrored
  first-person azimuth as-is, standing decision #8 — do not change the
  first-person camera), or the T466 rig fit itself, before touching any
  heading code; report and STOP if the answer is "the 90° offset".

## Leg A tasks (offline, x86)

1. **Lantern not drawn.** Trace the equip path for a secondary/held
   non-weapon: item model id (IT number) from the wire → `data/item_models.json`
   / equipment model index → mesh load from `*equip*.s3d` → attach point
   (L_POINT vs SHIELD_POINT for a non-shield) → visibility. Find where it
   drops. Pre-fix failing test at that seam with the real item/model data.
2. **Blade orientation.** From the S3D data itself (the actor's attach-point
   bone frame, the item mesh's local axes — `wld_dump` / the loader), derive
   what transform makes the blade hang down from R_POINT, and reconcile with the
   existing 180° X fix (why it is there: T295/T300/T309 first-person history).
   The first-person weapon (T309, attack-animation draw rule) must not regress —
   `test_first_person_weapon_attachment` stays green and gains a third-person
   orientation assertion. Pin the orientation by a geometric test (blade tip
   below the grip in the idle pose for a model-space down vector), not by
   eyeballing.
3. **Model facing.** Compute, for the T466 Chase-matched `/camera` values
   (`review/T466/` records them), where fable's camera sits relative to the
   player's facing direction and compare to the oracle's (back of head = camera
   behind). Identify which of model yaw / rig azimuth convention / fit is off.
   Fix only if it is not the load-bearing offset and not the accepted
   first-person azimuth; otherwise STOP with the analysis.
4. x86 `build-gles2` still captures (no server needed only if the tree has an
   offline model view — `tools/model_viewer` exists: use it with the same
   race/gender/items) before/after for Will, `review/T472/`, README. Never
   self-assess.

## Leg B (later, lock)

Oracle + fable captures for sets B (1H blunt + shield) and C (2H), attack
animations (video, stepped), and the post-fix fable set A with the matched
camera.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
touched TUs; visual gate (entity goldens: if one moves because a held item now
draws or turns, STOP and report — Will blesses, never self-bless); ARM; file
sizes. Board B660–B669; strike/narrow B586–B588.

File territory: `src/client/graphics/entity*` equipment/attachment,
`bone_attachment.h`, `character_model_view.cpp` attach copy, equipment model
loader, item model data, related tests, `review/T472/`. **Not** the camera
component / hotkeys / `/camera` (T471, slot1), **not** doors / zone loading
(T470, slot3).

STOP: the facing defect is the 90° offset or the accepted first-person azimuth;
an image golden moves.

## Review

Leg A, slot2, `task/T472-held-items-model-facing`, base `4f94ba16`. No server,
no Pi. Media: `review/T472/README.md`. **STOP conditions: not hit.** The facing
defect is neither the load-bearing 90-degree offset nor the accepted first-person
azimuth, and no image golden moved.

### 1. Lantern (B588, half 1): where it drops

- The drop is at indexing, not at attach. T466's run logs `Skipping equipment
  model ref: entity 274 (Testchar) equipment 48 (secondary) modelId=48`.
  - The player spawn carries IT48 directly.
  - `getModelRef(48)` was null because `EquipmentModelLoader` kept a mesh only if
    its name contained `IT48_`.
- IT48 is hierarchical. `IT48_ACTORDEF` (0x14) points at the 0x11 ref 4599, which
  names `IT48_HS_DEF` (0x10). Its bone `LANTERN01_DAG` (rest quat (0, 0.676, 0,
  0.737), shift (0.12, 0, -0.42)) holds 0x2D 1541, which is `LANTERN01_DMSPRITEDEF`
  (167 vertices). No mesh in that chain is named `IT48_*`.
- A census over the 17 `*equip*.s3d` archives counts 1,149 IT actors:
  - 1,041 match by name;
  - 773 resolve through the reference chain;
  - **19 resolve by the chain only and were never indexed**: IT2, IT46–48, IT62,
    IT97, IT140–142, IT146, IT148–151, IT154, IT160, IT410, IT411, IT10402;
  - 89 resolve neither way.
- **Fix:** `resolveEquipmentActorGeometry()` in a new TU,
  `src/client/graphics/eq/equipment_actor_geometry.cpp`.
  - It walks 0x14 → 0x2D → 0x36, and 0x14 → 0x11 → 0x10 → bone `modelRef` with
    the accumulated rest transforms (T·R·S, the animator's order).
  - It adds each mesh's centre and remaps texture indices together with their
    masked, invisible and animation flags.
  - The name rule is kept as the fallback.
  - It is used by all four former prefix loops: two indexers and two extractors,
    covering the zone-load, prep-worker and on-demand paths.
  - `WldLoader::getSkeletonRefs()` was added.

### 2. Blade (B588, half 2): the transform, derived from the data

Measured on a Human male at idle (`o01`), in EQ space:

| quantity | value |
|---|---|
| R_POINT X axis | (0.28, 0.03, 0.96): up the forearm |
| R_POINT Y axis | (-0.07, 1.00, -0.01): character-left |
| L_POINT X axis | (0.14, 0.03, 0.99) |
| SHIELD_POINT Y axis | points away from the chest |

- **Item long axis = +X.** IT1 spans x -1.37..1.37 about its centre (0.865,
  -0.025, -0.195), and -0.51..2.24 about its origin. The crossguard is the widest
  section, at +0.29. So the origin is in the grip, and the centre was the "grip
  offset" the heuristic approximated (1.10).
- **The old primary base `(180,0,0)` never touched the blade direction.** Adding
  180 to Irrlicht's X Euler angle equals a local half turn about X, which leaves
  X unchanged. The blade therefore followed R_POINT's X, which points up.
  Measured pre-fix: tip 2.2 above the grip at every heading, player and NPC.
- **X must be negated. Y vs Z was settled by shields.** Front textures are
  `kiteshield-m.bmp`, `qguard-front.bmp` and `shield round-metal/wood.bmp`; the
  backs are `*back.bmp`. On IT200–IT203 the front faces item +Y by both winding
  (sums +8.5..+9.2) and vertex normals (+17.8..+35.8).
  - Winding and normals are otherwise useless: the plates are double-sided,
    6 faces each way.
  - SHIELD_POINT +Y points outward, so Y must be kept.
- **The transform:** item-to-bone correction C = diag(-1, 1, -1), a half turn
  about the item's Y; world = B·C; origin on the bone.
  `computeHeldItemTransform()` in `bone_attachment.h` implements it as S·B·C·S.
- **Reconciliation with the old code:**
  - The 180° X base (T295/T300/T309 era) was never a blade fix; it only mirrored
    the blade's flat.
  - T65's `(180,0,180)` was not a local composition at all: the Z term applies in
    the parent frame. It happened to make secondaries visible.
  - Deleted: `extractBoneRotation`, `computeWeaponGripOffset`,
    `computeShieldGripOffset`, `computeSecondaryItemBaseRotation` and the
    paperdoll's offset members. Zero callers remain under
    src/include/tests/tools/benchmarks/docker; one tombstone comment is left.
- **The three copies are converged:** `entity_equipment.cpp`,
  `character_model_view.cpp` and `tools/model_viewer/model_viewer_equipment.cpp`.
  - Player bake: T301's yaw compensation and un-scaled position are kept, now
    composed with a matrix.
  - Incidental: the secondary shield test now asks `isShield` of the MODEL id,
    because a player's appearance carries a database item id.
- **First-person draw rule not regressed.**
  `HeldWeaponIsHeadingInvariantInTheCameraFrame` stays green at all five
  headings. Canary margin 0.847 → 0.709 units in front of the eye (must stay > 0).

### 3. Facing (B587): analysis and verdict

- **The camera is right.** `computeCameraRigPose` forward (sin h, cos h) with
  h = `90 - s` equals the server forward swapped into client (x, y) at every
  heading, consistent with T466's world fit. The first-person camera was not
  touched.
- **The per-type rotations are right.** The player path (node `90 - s` plus the
  +90 vertex bake) and the NPC path (`s`) draw identical hands at equal animation
  time: 1e-4 in the probe, <0.02 in the test. At that yaw the az-0 rig sees the
  BACK. The load-bearing flip is therefore correct and was not touched.
- **The defect: two writers of the player node's heading.**
  - `updatePlayerEntityPosition()` passes the flipped heading, but only when the
    player moves, turns or rides a lift.
  - The zone-in build (self row) and the `ZoneProcessClientUpdate` self echo pass
    the raw server heading.
  - T466's Testchar never moved, so he wore the bake over an unflipped heading:
    about 135° wrong at s=160. That reproduces all three T466 frames (az 0 face,
    az 90 back, az 270 chest).
- **Verdict:** model yaw on the stationary player node, not the offset and not
  the azimuth. **Fixed:** `EntityRenderer::syncLocalPlayerHeading()` (the node
  wears `ctx_.playerHeading`), called in
  `FrameComponent::processFrameSimulation()` after
  `flushPendingUpdatesForAnimations()` and before `updateMainThreadEntityState()`.
  No conversion site changed.

### 4. Tests: red before, green after

| test | red (fix reverse-applied from an exported patch; sync body forced to an early return) | green |
|---|---|---|
| `HeldItemTest.EveryActorWhoseReferenceChainDrawsIsIndexed` | FAIL: 19 not indexed | PASS |
| `HeldItemTest.LanternMeshBuildsOnTheZoneLoadAndTheOnDemandPaths` | FAIL: ref null | PASS |
| `HeldItemTest.OneHandedBladeHangsBelowTheGripAtIdle` | FAIL: tip +1.18 above the hand; grip 1.10 off | PASS |
| `HeldItemTest.LanternHangsBelowTheLeftHand` | FAIL: no node | PASS |
| `HeldItemTest.ShieldFrontFacesAwayFromTheBody` | FAIL: dot -0.84 | PASS |
| `HeldItemTest.StationaryPlayerIsDrawnAtTheRendererHeading` | FAIL: hands 1.98/2.63 off; camera sees the face | PASS |
| `HeldItemFrameOrder.PlayerHeadingIsSyncedAfterUpdatesAndBeforeEquipment` | FAIL | PASS |
| `FirstPersonWeaponAttachmentTest.ThirdPersonIdleBladeHangsBelowTheGrip` (new) | FAIL: tip 3.93 vs grip 3.80 | PASS |
| `FirstPersonWeaponAttachmentTest.HeldWeaponIsHeadingInvariantInTheCameraFrame` | PASS | PASS |

- `test_bone_attachment` has 3 new algebra pins (composition, origin, proper
  rotation plus Euler round trip); 14/14 pass.
- Logs: `review/T472/logs/redgreen_{red,green}_*.log`.

### 5. Verification

| step | result |
|---|---|
| 1 `cmake --build build -j24` | PASS, exit 0 |
| 2 `ctest -j6` | 4755/4756 on the first run. The one red, `check_build_tree_orphans`, was this unit's deleted probe binary still in `build/bin`; after deleting it the test passes alone. So 4756/4756. |
| goldens for touched TUs | `MovementOrder` (G06), `DocCounts`, `ZeroCallers` and `FileSizeCeiling` pass. No statement golden covers `renderer_frame_input.cpp`, `entity_*` or the loader. |
| 3 `build-gles2-x86.sh` | PASS, exit 0 (run after step 1) |
| 4 visual gate | PASS inside ctest (32.7 s); `tests/goldens` untouched |
| 5 `build-arm-noble.sh` | PASS, exit 0 (run after ctest) |
| 6 `check_file_sizes.sh fable` | PASS: 20 files, 0 warn, 0 fail |

### 6. Board

- Narrowed: B587 and B588 (fixed offline; live oracle proof owed to Leg B).
- B586 unchanged.
- New: B660 (name-rule fallback items drawn without rest DAGs, ~287), B661
  (IT10's long axis is Z), B662 (no scripted in-world held-item capture).

### 7. Deviations (flagged)

1. **`src/client/graphics/frame/renderer_frame_input.cpp` is outside the named
   territory.** One call and a comment. It belongs to neither T470 nor T471 (not
   the camera component). It is the only per-frame seam holding
   `ctx_.playerHeading` outside PlayerCameraComponent, and it is pinned by a
   source-order test.
2. **`tools/hwcaps/ui_inventory_capture.cpp`** gained two T472 captures, and two
   stale comments were updated. **Captures are the paperdoll, not the world view:**
   `tools/model_viewer` is interactive only (B662). The Short Sword's IT number
   was not available offline, so IT1 stands in.
3. **`WldLoader` got one getter** (`getSkeletonRefs`).
4. **Mesh centres are now added for every equipment mesh**, including name-rule
   fallback items. Before, no equipment mesh had its centre; this moves every
   held item's placement, by design. Items whose long axis is not +X will look
   different again (B661).
