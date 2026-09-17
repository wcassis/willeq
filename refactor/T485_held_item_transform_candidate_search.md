# T485 — the held-item correction chosen by search against the oracle, not by argument (B588, B722–B725)

Class A (visible; Will reported held items misoriented on the Pi, 2026-09-13),
**offline (no lock)**, slot1. Found by the 2026-09-14 audit of T472/T476/T478
(`review/AUDIT-0914/README.md` §held items). Read first: T472 Review
(`refactor/T472_*.md` §2, the derivation of C = diag(-1,1,-1)), T476 Review,
T478 Review §4 and its sheets `review/T478/sheets/T478_held_{A,B,C,IT10}_idle_oracle_top_fable_bottom.png`,
`docs/CLAUDE.md` (heading conventions), `match-the-oracle-dont-infer-it` rule
in `docs/decisions.md`.

## The finding

`computeHeldItemTransform()` (`include/client/graphics/eq/bone_attachment.h:118-136`)
applies one rigid correction C to the point bone's frame. C was derived by
argument from one R_POINT axis at one o01 frame plus shield winding. T478's
fresh oracle grabs disagree with it on EVERY orientation observation: set A
short sword (official: blade diagonally down-and-forward from the fist; fable:
straight down), set C 2H sword (official: roughly horizontal in front of the
waist; fable: vertical, near the ground), IT10 arrow (official: down along the
right leg; fable: horizontal). Three items with three different long axes all
disagree the same way, so this is one wrong rigid map, not per-item axes.
The tests `HeldItemTest.OneHandedBladeHangsBelowTheGripAtIdle`,
`ShieldFrontFacesAwayFromTheBody` and
`FirstPersonWeaponAttachmentTest.ThirdPersonIdleBladeHangsBelowTheGrip`
(`tests/test_first_person_weapon_attachment.cpp:265`) pin the ARGUMENT; a
correct fix will turn them red and they must be rewritten against the oracle.

## Tasks

1. **Candidate enumeration, offline.** Enumerate the 24 proper signed
   permutation matrices for C (plus identity, and the current one). For each,
   render set A (short sword IT10649), set C (2H IT10004) and IT10 on the
   T478 character at o01 idle through the OFFLINE path that already exists
   (the paperdoll RTT / `tools/hwcaps/ui_inventory_capture` or the model
   viewer — whichever renders `entity_equipment.cpp`'s world attachment code,
   not a copy of it). One contact sheet per candidate, all three items.
2. **Score against the oracle.** The oracle frames are
   `review/T478/oracle/*` (idle, Chase and Tether). Their azimuths are not
   matched to fable's (B651), so score the ITEM DIRECTION IN THE BODY FRAME:
   for each item, measure the angle of its long axis relative to the arm/leg
   in both clients (prose descriptions in T478 README §4 are the fallback,
   but measure where you can — the arrow along the leg and the 2H
   horizontal-at-waist are clear geometric tests). The candidate that
   satisfies all three at once is the answer; if none does, STOP and report
   the best two with sheets — do not invent a fourth transform.
3. **Test identity first** (candidate C = I): if identity matches, fable's
   R_POINT world frame itself is wrong (a leaf bone with no skinned vertices,
   so a rest-composition error there is invisible in body skinning). Dump
   R_POINT's world frame at o01 and compare with the forearm's skinned vertex
   direction before touching C.
4. Apply the winner in `computeHeldItemTransform()` only (one owner). Rewrite
   the three argument-pinning tests to pin the oracle facts (blade
   down-and-forward, 2H horizontal at waist, arrow along the leg), red before
   the change. The three `place`/`placeAttachment` lambdas
   (`entity_equipment.cpp` ~200, `character_model_view.cpp:594`,
   `model_viewer_equipment.cpp:34`) must all consume the one function — fold
   the split into the shared function so they cannot drift.
5. Set B (shield): `review/T478/logs/oracle_setB_db.log` shows garbage
   inventory rows beside the shield write; B723 is contaminated evidence.
   Do NOT act on B723; say so on the board. Board B588 currently says the
   lantern is not drawn — false since T472; strike/narrow it.

## Proof

Standard verification bar, builds strictly in sequence. Before/after contact
sheets for sets A, C, IT10 next to the oracle crops in `review/T485/` with a
README that describes, never judges. No golden under `tests/goldens` should
move (held items are not in the gate zones' fixed camera); if one does, STOP.
ARM build; file sizes; docs: `docs/module_map.md` gains the missing row for
`equipment_actor_geometry.cpp`, `src/client/graphics/CLAUDE.md` gains a
held-item paragraph naming the one owner.

File territory: `include/client/graphics/eq/bone_attachment.h`,
`src/client/graphics/entity_equipment.cpp`, `character_model_view.cpp`,
`model_viewer_equipment.cpp`, held-item tests, `review/T485/`, the two docs.
**Not** the camera (T486 may touch `camera_controller.cpp`), not the loaders
(T479 branch, slot2), not the texture cache.

STOP: no single candidate satisfies all three items; the fix needs a live
oracle re-grab (lock) — report which item and why.

## Review

Executor: slot1, branch `task/T485-held-item-oracle-search`, base `a5c4f1f5`. Offline
throughout (no lock, no server, no Pi). Media: `review/T485/` (README describes,
never judges).

### 1. The search (tasks 1-2)

`tools/hwcaps/held_item_candidate_search` (new, `build/bin/`) builds a Human male NPC
through `EntityRenderer::createEntity()` + `updateMainThreadEntityState()` -- the
production in-world path, the `HeldItemTest` fixture's idiom -- at `o01` with one
item in the primary slot (IT10649 set A, IT10004 set C, IT10), lets the production
placement run, then re-places the item node with each of the 24 proper signed
permutations through `placeHeldItem(node, bone, nullptr, 1, candidate)` -- the one
function, not a copy -- and captures three cameras per candidate and item (216 PNGs,
`review/T485/candidates/` has one sheet per candidate with the T478 oracle crops as
the top row). Per candidate and item it measures the item's long axis (the vertex
farthest from the grip) in the body frame and the angle to the oracle target
(`review/T485/data/candidates.tsv`):

| candidate | item X -> | Y -> | Z -> | IT10649 to forward | IT10004 to forward | IT10 to down |
|---|---|---|---|---|---|---|
| c00 identity | +X | +Y | +Z | 69.2 | 73.7 | 73.5 |
| c02 T472 (before) | -X | +Y | -Z | 112.9 | 112.1 | 112.8 |
| c17 | -Z | -X | +Y | 24.3 | 23.5 | 93.4 |
| c18 | -Z | +X | -Y | 24.3 | 23.4 | 86.6 |
| **c20 (shipped)** | **-Z** | **+Y** | **+X** | **24.3** | **23.4** | **23.1** |
| c23 | -Z | -Y | -X | 24.3 | 23.5 | 156.9 |
| c12 / c13 / c21 | +Y / -Y / +Z | +Z / -Z / -Y | +X | 97.5 / 82.6 / 155.7 | 97.0 / 82.9 / 156.5 | 23.1 |
| every other | | | | 67-156 | 68-157 | 67-157 |

Only the four X -> -Z candidates put a blade forward; only the four Z -> +X hang the
arrow down; c20 is the one candidate in both sets, and with those two columns fixed
det +1 forces Y -> +Y (the Y-mirrored version has det -1 and is not a rotation), so
the winner is unique among proper rotations. It keeps the item's Y, so the shields'
painted face (item +Y by winding and normals, T472) still points outward as a
consequence. The 2H crossguard (item Z) is vertical under c20: `itemZ_up` 0.92.

**Oracle side (task 2).** The official frames' azimuths are unmatched (B651), so the
facts were taken from `review/T478/oracle/*_User2_*` (a side view) and `*_Tether_*`,
cropped at 3x and 6x (`review/T485/oracle_crops/`): set A's blade runs forward from
a grip at BELT height, level to slightly up; set C's 2H likewise with its crossguard
vertical (`C_user2_zoom.png`: crossguard vertical at the belt, blade forward, pommel
back); IT10's arrow hangs from the hand down along the leg to the foot. Measured
angles in image space were not attempted beyond that: the cameras are unknown, and
the qualitative facts already separate the 24 candidates by 40-130 degrees.

### 2. Task 3: identity and R_POINT's frame

Identity (c00) does not match (blade up, 69 deg from forward), so the plan's "the
bone frame itself is wrong" branch was tested the other way round: the dump in
`review/T485/data/rpoint_frames.txt`. On the Human male at `o01` frame 0, EQ model
space (x forward, y left, z up):

| segment | direction |
|---|---|
| `humbi_r -> humfo_r` (upper arm, shoulder to elbow) | (-0.39, -0.34, -0.86): down and back |
| `humfo_r -> humfi_r` (forearm, elbow to fingers) | (0.96, 0.07, -0.27): forward, slightly down |
| R_POINT X | (0.28, 0.03, 0.96): up |
| R_POINT Y | (-0.07, 1.00, -0.01): character-left |
| R_POINT -Z | (0.96, 0.06, -0.28): along the forearm, forward |

The elbow is bent and the hand is at the belt -- which is exactly where the official
client's grip sits in the User 2 frames, so the pose is not in question. R_POINT's
frame is consistent with the forearm: -Z runs down the forearm to the fingers, X is
perpendicular to it. **T472's premise "R_POINT's X is up the forearm" was false**
(X is up, full stop), and negating X is what hung the blade straight down. The
`o01` loop is 49 frames and swings R_POINT (X from (0.28, 0.03, 0.96) at frame 0
to (0.90, -0.16, 0.40) at frame 24, origin z 1.25 -> 0.44); the numbers above and
the captures are at the frame the fixture's five `OnAnimate(200 + 16t)` calls land
on, and the tests' thresholds (0.7 forward, |up| < 0.6, arrow -0.85) hold there.

### 3. The change (task 4)

- `include/client/graphics/eq/bone_attachment.h`: `HeldItemCorrection` (column j =
  image of item axis j) + `kHeldItemCorrection` = c20; `computeHeldItemTransform(bone,
  corr = kHeldItemCorrection)`; **`placeHeldItem(node, bone, playerYawFix, playerScale,
  corr)`** -- the one placement, carrying T301's player-bake compensation (the
  rotation/position split and the un-scaled position that the three lambdas each
  had a copy of).
- `entity_equipment.cpp`, `ui/character_model_view.cpp`,
  `tools/model_viewer/model_viewer_equipment.cpp`: the three `place`/`placeAttachment`
  lambdas are one-liners onto `placeHeldItem()`.
- `tools/hwcaps/held_item_candidate_search.cpp` (new, CMake target next to
  `ui_inventory_capture`): the instrument; `corr` is exposed on the two functions
  for it only.

### 4. Tests: red before, green after

Rewritten against the oracle facts and run against T472's C first (build 2), then
against c20 (build 3):

| test | before (T472 C) | after (c20) |
|---|---|---|
| `HeldItemTest.OneHandedBladePointsForwardAndLevelAtIdle` (was `OneHandedBladeHangsBelowTheGripAtIdle`; IT10649, 3 player + 2 NPC headings) | RED: forward -0.282, |up| 0.959 | green |
| `HeldItemTest.TwoHandedBladeIsLevelInFrontOfTheWaistAtIdle` (new; IT10004, crossguard vertical) | RED: forward -0.281, |up| 0.959, crossguard 0.284 | green |
| `HeldItemTest.ArrowHangsDownTheLegAtIdle` (new; IT10) | RED: dir.Y +0.283 | green |
| `FirstPersonWeaponAttachmentTest.ThirdPersonIdleBladePointsForward` (was `...HangsBelowTheGrip`) | RED at 0 / 135 / 270 deg | green; draw-rule canary 0.7085 unchanged |
| `BoneAttachmentTest.ShippedCorrectionIsAQuarterTurnAboutTheItemsY`, `HeldItemAtAnIdentityBoneIsAQuarterTurnAboutTheItemsY` (was `...HalfTurn...`) | RED | green |
| `BoneAttachmentTest.HeldItemTransformMatchesTheEqSpaceCompositionForARotatedBone` (now over c20, T472's C and identity through the explicit parameter), `PlaceHeldItemSetsTheNodeFromTheOneTransform` (new: NPC and player paths on an EDT_NULL node) | -- | green |
| `HeldItemTest.ShieldFrontFacesAwayFromTheBody` | green | green (see deviations) |
| `HeldItemTest.LanternHangsBelowTheLeftHand` | green | green |

### 5. Set B (task 5)

Not acted on. `review/T478/logs/oracle_setB_db.log` shows the shield's inventory
write beside garbage rows; B723 is marked contaminated on the board and a clean
re-grab is owed before any shield work. B588's "lantern is not drawn" half had been
false since T472; the entry is struck (T472 the lantern, T485 the blade).

### 6. Verification

(filled in below)

### 7. Board

Closed B588, B661, B662, B724, B725; B723 marked contaminated; added B755 (item 2894
maps to IT10009 in `willeq.db` while T478 read IT10004 off the server) and B756
(the blade's pitch is ~24 deg below the oracle's level-to-slightly-up; a signed
permutation cannot close it).

### 8. Deviations (flagged)

- **`ShieldFrontFacesAwayFromTheBody` did not turn red and was not rewritten.** The
  plan expected all three argument pins to go red; this one pins "item Y kept", and
  the winner keeps Y (forced by det +1 once X and Z are placed), so it stays green.
  Its comment now says it is a DATA pin (winding), not an oracle one -- the oracle
  has not been seen holding a shield (B723).
- **The oracle angles were not measured numerically.** The plan asked to "measure
  where you can"; with unknown camera azimuths (B651) an image-space angle is not a
  body-frame angle, and the qualitative facts separate the candidates by 40+
  degrees. What could not be settled that way is boarded as B756 (the ~24 deg
  pitch residual), not absorbed.
- **The paperdoll (`ui_inventory_capture`) was not used as the instrument**; the
  new tool renders the in-world entity through `EntityRenderer` instead, which is
  closer to what Will sees and closes B662. The paperdoll consumes the same
  `placeHeldItem()`, so it moves with it.
- **Item ids.** T478's set C is item 2894, which `willeq.db` maps to IT10009, not
  IT10004; the plan named IT10004 and that is what was searched (B755).
