# T476 — held items drawn through the name fallback get no rest transforms (B660); items whose long axis is not +X (B661)

Class A (visual, unconfirmed live), **no lock, no Pi**, slot2. Read T472's
Review (`refactor/T472_held_items_and_model_facing.md`), `review/T472/README.md`,
and its tests (`tests/test_held_item_attachment.cpp`, `test_bone_attachment.cpp`,
`test_first_person_weapon_attachment.cpp`).

## The leads (T472)

- **B660:** `resolveEquipmentActorGeometry()` follows actor → skeleton → DAG →
  DMSPRITEDEF and applies the skeleton's rest transforms plus the mesh centre.
  Items that still resolve only through the old `IT<n>_` name fallback get
  neither — T472 estimated ~287 of them. Those items may sit off the grip.
- **B661:** `computeHeldItemTransform()` assumes an item's long axis is +X
  (true for IT1 and the shields T472 checked). IT10's long axis is Z, so it is
  held sideways.

## Tasks

1. **Census across all 17 `*equip*.s3d` archives** (offline, the loader +
   `wld_dump`): for every IT model, record which path resolves it (actor chain vs
   name fallback), whether rest transforms/mesh centre apply, and the mesh's
   principal (longest-extent) axis and its sign. Write the table to
   `review/T476/` (CSV) and summarise counts in the Review.
2. **B660:** find why the fallback items do not resolve through the actor chain
   (no actor? different reference shape?) and extend the resolution so every
   item that HAS rest data gets it; items with genuinely none keep the fallback
   and are listed. Pre-fix failing test on a representative fallback item with
   rest data.
3. **B661:** derive the grip frame per item from the DATA, not a per-item table
   of invented rotations: e.g. map the mesh's principal axis to the bone's long
   axis in `computeHeldItemTransform()` (with the sign fixed by where the grip /
   mesh origin lies, T472's pommel-vs-crossguard rule). Pin with geometric tests
   for IT10 and at least one item per distinct principal-axis class the census
   finds; IT1 and the shields must not move (their tests stay green).
4. Offline before/after paperdoll captures (T472's `ui_inventory_capture` set)
   for the changed classes, `review/T476/`, README. Never self-assess.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
touched TUs; visual gate (STOP if an image golden moves); ARM; file sizes.
Board B700–B709; strike/narrow B660/B661.

File territory: equipment model loader, `equipment_actor_geometry.cpp`,
`bone_attachment.h` / `computeHeldItemTransform()`, `character_model_view.cpp`
attach copy, `tools/model_viewer` attach, held-item tests, `review/T476/`.
**Not** the camera component / `camera_rig.h` / `/camera` (T474, slot1),
**not** `renderer_gpu_upload.cpp` / pending-texture queues (T475, slot3).

STOP: the principal-axis rule cannot be derived from the data for a class of
items (report the class and its evidence — no invented per-item rotations); an
image golden moves.

## Review

(executor fills)
