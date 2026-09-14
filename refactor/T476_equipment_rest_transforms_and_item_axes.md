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

Slot2, `task/T476-equipment-rest-transforms`, base `a2602187`. No server, no Pi.
Media: `review/T476/README.md`. **STOP hit for B661** (task 3): no axis rule was
written. B660 is fixed and verified. No image golden moved.

### 1. Census (task 1)

`test_equipment_item_census` (new, `tests/test_equipment_item_census.cpp`) walks
all 17 archives in index priority order. It writes one CSV row per IT actor when
`WILLEQ_T476_CENSUS_CSV` is set:
`review/T476/census/equip_census_before.csv` and `equip_census_after_b660.csv`
(1,149 rows each). It records the route, what each 0x14 reference points at, the
rest and centre flags, bounds about the origin, and the principal axis with its
sign and ratio.

| index items (1,081) | chain | chain, skinned | name only | none |
|---|---|---|---|---|
| before | 736 | -- | 266 | 79 |
| after | 735 | 346 | 0 | 0 |

All 1,149 actors after: 772 chain, 377 skinned. The one index item that moved from
"chain" to "chain, skinned" is gequip IT154, which has 5 bone meshes and 1 skinned
mesh.

### 2. B660: why the fallback items missed the chain, and the fix

- **The shape is the same for all 266 name-only items, and for the 79 unresolved
  ones:** 0x14 -> 0x11 -> 0x10 with no bone `modelRef`. Every one has a
  non-identity rest orientation and a zero mesh centre.
  - Example, IT4 (gequip): `IT4_HS_DEF` has flags **0x202** and 6 bones.
    `IT4_DMSPRITEDEF` has centre (0,0,0), bounds 0 and 4 vertex pieces.
  - frogequip IT4840: 102 bones, 2 pieces.
- **Cause:** bit 0x200 of a 0x10 means a list after the bones: a count, that many
  0x2D refs (the skinned meshes), then one uint32 each. `parseFragment10()` stopped
  after the bones and never read it. So the mesh was reachable only by the name
  rule, and it drew with each vertex piece at its bone's own origin.
- **The 79 unresolved actors** have the same shape with no `IT###_`-named mesh, so
  the name rule found nothing either. They resolve now.
- **Evidence the layout is right:** after the fix, the list's refs resolve to at
  least one mesh for all 377 skinned actors (census `skin=` column). For the 286
  whose mesh is named `<actor>_DMSPRITEDEF`, the tests compare every vertex against
  an independent computation and all match. Whether the pieces of the other 91 fit
  their skeleton was not checked separately; a mesh whose pieces do not fit is left
  at identity.
- **Fix:**
  - `SkeletonTrack::meshRefs` (`wld_loader.h`), filled in `parseFragment10()` with
    bounds checks.
  - `resolveEquipmentActorGeometry()` follows the list. `appendMesh()` takes each
    piece's bone rest transform: T·R·S accumulated from the roots, the same walk as
    `appendBone()`. A mesh whose pieces do not fit keeps identity.
  - The name rule stays as the fallback and now catches nothing in the index.
  - All four loader paths (two indexers, two extractors) already call this function,
    so the zone-load, prep-worker and on-demand paths all changed together.

### 3. B661: STOP -- the principal-axis rule cannot be derived from the data

The plan's STOP: "the principal-axis rule cannot be derived from the data for a
class of items". Evidence, from the after-fix census:

- **There is no frame in the data to derive it from.** 0x14 and 0x2D carry no
  transform, and skinned items have only bone rest poses, which are now applied. The
  mesh's own axes are the only frame an item has. Choosing a different one for some
  items is exactly the invented rotation the plan forbids.
- **"Longest axis" has no natural cut.**
  - The ratio of longest to next extent is continuous (bins of 0.25: 327, 146, 86,
    62, 76, 50, 29, 17, 15, 13, 20, ... 116 at >= 6), with no empty bin.
  - 621 of 1,081 items are below 2, so their "principal axis" is decided by small
    differences.
  - A threshold would have to be chosen by argument.
- **The shields cannot all keep still under any such rule.** IT200 X- (1.46), IT201
  X- (1.00), IT202 X- (1.18), IT203 Z+ (1.003). A per-axis rule would turn IT203
  differently from the other three, and all four are pinned not to move.
- **The class a rule would change is not a held-item class.**
  - 460 items are elongated (ratio >= 2): X+ 289, Z- 99, X- 46, Z+ 10, Y- 9, Y+ 7.
  - The 109 elongated Z items are mostly armour-family archives (lgequip 56,
    lgequip_amr 29, lgequip2 13, gequip 5, vequip 3, frogequip 2, gequip2 1).
  - Only 4 of those 109 are used by any database item.
- **The sign rule is as weak.** Among the 460 elongated items, the short end's share
  of the long end fills every bin from 0.0 to 1.0 (3, 34, 140, 69, 44, 40, 37, 19,
  13, 32, 29). For 61 items it rounds to 0.9 or 1.0: the origin sits near the middle, so
  "the grip is the end the origin sits on" picks no end.
- **Classes reported, untouched:** elongated Z (IT10, IT69, IT73, IT71, IT136 and the
  armour families), elongated Y (IT4560–IT4563, IT2361, IT2483, IT2784), and elongated
  X- (IT10402 and the gequip5/gatesequip families). IT10's before/after capture is
  byte-identical.
- **Still owed:** the oracle. How the official client holds an IT10 item (T472 Leg B,
  set B/C) settles whether IT10 is wrong at all. Until then, an axis change would be
  a guess.

### 4. Tests: red before, green after

| test | red | green |
|---|---|---|
| `HeldItemTest.SkinnedItemIsPlacedByItsSkeletonRestPose` (new) | FAIL on the pre-fix tree: IT4 max error 4.57 | PASS |
| `HeldItemTest.EverySkinnedItemIsPlacedByItsSkeletonRestPose` (new) | FAIL: 282 of 286 wrong | PASS |
| `EquipmentItemCensus.EveryItActorIsClassified` (new) | FAIL with the fix reverse-applied from an exported patch: 266 name-only, 79 none, 0 skinned | PASS |
| `HeldItemTest.*` (T472's seven, including the blade, lantern and shield pins) | PASS | PASS |
| `FirstPersonWeaponAttachmentTest.*` (2) | PASS | PASS, canary 0.709 (unchanged, > 0) |
| `BoneAttachmentTest.*` (14) | PASS | PASS |

The two new held-item tests compute the expected vertices a different way from
the loader: the rest world through the file's own PARENT table rather than the
child walk, and the mesh found by name. They compile against the pre-fix tree.
Logs: `review/T476/logs/`.

### 5. Captures (task 4)

`ui_inventory_capture` gained three T476 captures: IT4, IT157 and IT10 in the
primary hand, all model ids that are not also item ids. Before used the fix
reverse-applied; after used the fix. Of 19 PNGs, only IT4 and IT157 differ; the
other 17 are byte-identical, including IT1, IT48, IT202 and the two-handed item.
Sheet: `review/T476/sheets/T476_paperdoll_before_left_after_right_4x.png`. Not
assessed.

### 6. Verification

Builds strictly in sequence, each confirmed by its own exit file.

| step | result |
|---|---|
| 1 `cmake --build build -j24` | PASS, exit 0 (after re-applying the fix from the exported patch) |
| 2 `ctest -j6` | PASS: 4,777 of 4,777 (252.8 s), `visual_gate` included (35.6 s) |
| goldens for touched TUs | no statement golden covers the loader, the WLD parser or the capture tool. `FileSizeCeiling`, `DocCounts` and `ZeroCallers` pass; `tests/goldens` untouched |
| 3 `build-gles2-x86.sh` | PASS, exit 0 (run after step 1, before ctest) |
| 4 visual gate | PASS inside ctest; no image golden moved |
| 5 `build-arm-noble.sh` | PASS, exit 0 (run after ctest) |
| 6 `check_file_sizes.sh fable` | PASS: 6 files, 0 warn, 0 fail. The new untracked `tests/test_equipment_item_census.cpp` is 191 lines |

### 7. Board

- B660 **closed**, with the census and the three pins.
- B661 narrowed: STOP evidence recorded, oracle capture owed.
- No new entries; B700–B709 unused.

### 8. Deviations (flagged)

1. **B661 not implemented:** the plan's STOP condition, above. No axis change and
   no per-item table.
2. **`wld_fragments_skeleton.cpp` / `wld_loader.h` are outside the named
   territory.** The cause of B660 is in the 0x10 parser. The change is additive:
   one new field that no other consumer reads. No other 0x10 consumer
   (race/character models, zone objects) was changed or checked for the same list.
3. **New test target** `test_equipment_item_census` (`tests/cmake/ui_rendering.cmake`).
4. **`tools/hwcaps/ui_inventory_capture.cpp`** gained three captures, as in T472.
