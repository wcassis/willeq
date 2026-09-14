# T479 — the 0x10 skeleton mesh list T476 found: do character and zone-object models miss it too?

Class A (unconfirmed), **no lock, no Pi**, slot2. Read T476's
Review (`refactor/T476_equipment_rest_transforms_and_item_axes.md`) and its
census test (`tests/test_equipment_item_census.cpp`).

## The lead

T476 found that a 0x10 skeleton with flag 0x200 lists its skinned mesh AFTER
its bones, and `parseFragment10()` never read that list. For equipment this left
345 items reachable only by name, with every vertex piece drawn at its bone's
origin. T476 added `SkeletonTrack::meshRefs` and taught the equipment resolver
to follow it, but did not check the other loaders that read 0x10 skeletons:
character/race models (`global*_chr.s3d`, zone `*_chr.s3d`) and placeable zone
objects (`*_obj.s3d`).

## Tasks

1. **Census every 0x10 in the character and object archives.** Count how many
   have flag 0x200 with a non-empty mesh list, and, for each loader
   (character model builder, animated mesh, zone object/placeable loader), how
   the mesh is found today: by bone `modelRef`, by name, or not at all. Write a
   CSV to `review/T479/` and put the counts in the Review.
2. **For each loader whose models depend on the list** and resolve today by
   name or not at all, write a failing geometry test on a representative real
   model. Examples: vertex pieces placed at bone origins instead of the rest
   pose; a model silently missing. Then fix it by following `meshRefs`, the way
   T476's resolver does. If a loader already resolves correctly through another
   route, refute it with evidence.
3. **Take offline before/after captures** of any model that changes
   (`tools/hwcaps/ui_inventory_capture.cpp` or the equivalent headless path),
   with a README in `review/T479/`. Never self-assess.

## Proof

- Standard verification bar, builds strictly in sequence.
- G-batch goldens for the touched TUs.
- **Visual gate:** the gate zones draw character and object models. If an image
  golden moves, STOP and report; Will blesses.
- ARM build.
- File sizes.
- Board B730–B739.

File territory: `wld_loader*` / `wld_fragments_skeleton.cpp` (read-only unless a
field is missing), the character model builder, the animated mesh loader, the
zone object loader, related tests, `review/T479/`. **Not** the equipment resolver
(T476, merged), the Chase rig (T474, slot1), or test `/tmp` paths (T477, slot3).

STOP: an image golden moves.

## Review

(executor fills)
