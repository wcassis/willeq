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
