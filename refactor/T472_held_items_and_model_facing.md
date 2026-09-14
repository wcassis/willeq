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

(executor fills)
