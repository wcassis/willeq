# B1065 (#385) -- NPC held items not at the hand attach points

Will, 2026-10-14 on the XU4 (fable ac32a25e): the qeynos guards' shields hang in
the air beside the left shoulder and their weapons are not in the hand
(`review/B1065/guard1.png`, `guard2.png`, outside the checkout). The B1064
executor reproduced the shield half on x86 llvmpipe
(`review/B1064/x86/b1065_front_after_run1_f100.png`), so this is client logic,
not the Midgard texture defect B1051 (#371).

## Mechanism (three defects, all on the held-item path)

1. **A standing NPC's held items were never placed.**
   `EntityRenderer::updateMainThreadEntityState()` placed equipment only for
   members of `activeEntities_` (`src/client/graphics/entity_updates.cpp:125`,
   placement at `:203` on ac32a25e). An NPC joins that set only when a position
   update carries a velocity (`entity_updates.cpp:370`); the only code that
   removes stationary entities, `updateInterpolation()`
   (`entity_interpolation.cpp`), has no caller. So a guard at its spawn point
   (Guard Gehnus, Guard Dunix) never had its sword or shield placed: both drew
   at the model origin with the identity rotation -- the "weapon held
   horizontally at hip level" in `guard2.png` and the shield lying flat across
   the hips in `review/B1065/x86/before_sideb_f040.png`. A patrolling NPC
   (Lieutenant Arathur) was placed while it walked. Fix: every in-scene entity
   with an equipment node is placed every frame (`entity_updates.cpp`, after
   the active loop).
2. **The point-bone lookup used the race row's code, not the skeleton's.**
   `entity_equipment.cpp:171` (ac32a25e) built the bone names from
   `RaceModelLoader::getRaceCode(raceId)`. A race drawn with its fallback model
   (race 71 QCM -> HUM where no QCM model is loaded, `config/race_models.json`)
   has `humr_point`, never `QCMR_POINT`, so nothing was found and nothing was
   placed. In qeynos2 the live run loaded the real QCM model, so this did not
   bite there (`review/B1065/x86/logs/*.held_items.txt`: `skeleton 'QCM' ...
   secondary IT200 (shield) -> qcmshield_point`), but it does in any zone where
   QCM falls back. Fix: `resolveHeldItemBones()` (`bone_attachment.h`) tries
   `CharacterSkeleton::modelCode` first, then the race code; the result is
   cached per (skeleton, secondary item) on `EntityVisual`.
3. **Held items were drawn with back-face culling OFF**
   (`entity_equipment.cpp:83`, `:117` on ac32a25e). IT200 is two skins a few
   hundredths apart; the painted face (`qguard-front.bmp`) has winding normal
   +Y_item, the back (`qguard-back.bmp`) -Y_item, and the BACK skin is the
   +Y-most surface (`review/B1065/x86/data/qcm_qeynos2_o01_bones.txt`). The
   shield is attached face-out (SHIELD_POINT's Y is character-left), but with
   culling off depth decides and the outside shows the dark back -- the dark
   slab "off the shoulder" in every x86 still. The official client shows the
   painted face from the guard's left. Fix: `kHeldItemBackfaceCulling` (true)
   in `bone_attachment.h`, used by both equipment materials.

The attach itself was already right: the shield hangs from SHIELD_POINT, a
child of the left FOREARM (`QCMFO_L_DAG -> QCMSHIELD_POINT_DAG`,
`docs/reference/s3d_contents_reference.md`), with the POSED world transform
(`SkeletalAnimator::getBoneStates()[i].worldTransform`), the T485 identity
correction and the item's origin (mesh centre added,
`equipment_actor_geometry.cpp`) on the bone. The weapon and shield paths do not
differ in the transform; they differed only in bone choice and both shared
defects 1-3. No offset was added.

## Oracle comparison (Guard Gehnus, qeynos2 130,312, race 71, IT10653 + IT200)

Official: lane 2 (lane 5's lock was held by `b888cc1-pilot4` for the whole
session), `scripts/eqemu/oracle/plans/b1065_shield_{front,side_a,side_b}.tsv`
with the new `# pose=` seed, character Testcaster. willeq: x86 llvmpipe,
`--constrained orangepi`, Testbuddy on lane 2 at the byte-identical pose
(`review/B1065/x86/tools/lane2_session.sh`), first person, `/hideui`.

| pose | official (`review/B1065/oracle/`) | willeq ac32a25e | willeq this branch | camera_match_score best (edge, mad) |
|---|---|---|---|---|
| front, 25 u, heading 256 | shield edge-on along the outside of the left forearm, painted face just visible; weapon in the right fist (`front_guard_crop_x2.png`) | no shield or weapon visible (both at the model origin, inside the body) (`x86/before_front_f040.png`) | shield along the outside of the left arm, painted face visible; weapon in the right fist (`x86/final_front_f032.png`) | 0.893 / 0.335 (frame 32) |
| guard's left, heading 384 | kite face-on, painted face outward, flat edge forward, point back, long axis ~horizontal, over the forearm (`side_b_guard_crop_x2.png`) | shield flat across the hips, back skin up (`x86/before_sideb_f040.png`) | painted face outward over the forearm, flat edge forward, long axis tilted up toward the front (`x86/final_sideb_f051.png`) | 0.864 / 0.298 (frame 51) |
| guard's right, heading 128 | weapon in the right hand (`side_a/13_view0.png`; the guard was fighting a rodent) | -- | weapon in the right hand (`x86/final_sidea_f033.png`) | 0.940 / 0.282 (frame 33) |

Sheets: `review/B1065/x86/B1065_front_official_before_after.png`,
`B1065_side_b_official_before_after.png` (official | before | placement fix
with culling off | final).

## Pins

- `HeldItemTest.StandingNpcsHeldItemsFollowTheirBonesEveryFrame` -- a standing
  NPC's sword and shield coincide with R_POINT / SHIELD_POINT at two idle
  frames and the shield moves with the forearm (red on ac32a25e).
- `HeldItemTest.FallbackModelRaceHoldsItsItemsAtItsOwnPointBones` -- race 71 on
  its HUM fallback skeleton (red on ac32a25e).
- `HeldItemTest.HeldItemsAreDrawnWithBackFaceCulling`.
- `BoneAttachmentTest.BothHandsWorldMatricesMatchTheHandComputedValues` -- a
  known forearm pose + SHIELD_POINT local and a known R_POINT -> the
  hand-computed Irrlicht matrices, and IT200's far edge.
- `BoneAttachmentTest.HeldItemBonesComeFromTheSkeletonsOwnModelCode`,
  `HeldItemBonesFallBackToTheRaceCodeAndToTheLeftHand`.

## Not verified / left open

- **The weapon half of the report.** On x86 the weapon of a MOVING guard was
  already in the hand (`review/B1064/x86/b1065_front_after_run1_f100.png`);
  the "small object near the right hand" / "weapon horizontal at hip level" in
  Will's stills match defect 1 (a standing guard's weapon at the model origin),
  but that is an inference from the mechanism, not a reproduction of his exact
  frame.
- **Pose.** The official idle hangs the forearm lower than fable's `o01`, so
  the shield sits higher on fable and its long axis is tilted up toward the
  front rather than near-horizontal (side sheet). This is the T485/B756
  open question (fable's `o01` vs the official idle), not an attach offset;
  no official bone dump exists to settle it.
- **Scale.** Bodies are skinned through bones carrying a ~1.11 scale, held
  items are placed rotation-only (`placeHeldItem()`), so items may be ~10 %
  small against the body; the side sheet suggests the official shield is
  larger relative to the torso. Not measured.
- **Back view.** `b1065_shield_back.tsv` put the camera behind a pillar
  (`review/B1065/oracle/back/13_view0.png`); no back golden.
- The NPC halves of the pre-existing `HeldItemTest` orientation pins
  (`OneHandedBlade...`, `ShieldFrontFacesAwayFromTheBody`, ...) passed on
  ac32a25e with the items NEVER placed -- they now measure placed items, but
  why they passed unplaced was not investigated.
- The paperdoll (`character_model_view.cpp`) and model viewer still draw
  items with culling off; not changed here.
