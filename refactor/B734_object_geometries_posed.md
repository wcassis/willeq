# B734: S3DZone::objectGeometries holds skinned meshes un-posed, keyed by MESH name

Closed on branch `task/kb-b734-object-geometries-posed` (issue #71).

## Defect

`S3DLoader::loadObjects()` (`src/client/graphics/eq/s3d_loader_objects.cpp`)
stored every 0x36 geometry from `<zone>_obj.s3d` into
`S3DZone::objectGeometries` under its MESH name with only the center offset
applied. A skinned object geometry — vertex pieces naming the bones of the
0x10 skeleton that lists it (flag 0x200, T476) — has a degenerate center and
bounds in the file (thedeep's `STOR301_DMSPRITEDEF`: 189 verts, 1 piece,
Center (0,0,0), Bounds (0,0,0)-(0,0,0)), so the center offset was a no-op and
the map held the geometry with every piece still at its bone's origin: an
un-posed torch. And the key was the MESH name (`STOR301`), while the
placement (0x15) and `DoorManager::findDoorMesh` use the ACTOR name
(`TDTORCH301`), which only the loose substring fallback (and not even that —
`STOR301` does not contain `TDTORCH301`) could find.

## Fix

`loadObjects()` now detects skinned geometries (vertex pieces + a 0x10 whose
meshRefs resolve to the geometry's fragment index), finds the ACTOR names that
reference that skeleton through the same 0x14 -> 0x2D -> 0x11 -> 0x10 chain
that `parseFragment14` records, and stores the REST-POSE geometry — produced by
T476's `resolveEquipmentActorGeometry` (reused, not a second skinning path) —
under the actor name, with the mesh name aliased to the SAME
`shared_ptr<ZoneGeometry>`. One copy. `S3DZone::getMemoryUsage()` now dedupes
aliased keys (100 %-accounting rule: a shared block is counted once).

## Evidence (all [measured], this host, Release, thedeep + bothunder + qeynos2)

- RED: `refactor/B734/red_output.txt` — 3 failing tests at the base commit
  (actor name not a key; degenerate (0,0,0) bounds; findDoorMesh miss).
- GREEN: `refactor/B734/green_output.txt` — the same 3 pass after the fix;
  TDTORCH301 bounds x[-0.568,0.565] y[-0.492,0.490] z[0.906,4.067].
- `objectGeometries` per-key `getFullMemoryUsage` census,
  `memory_before.txt` vs `memory_after.txt`:
  - thedeep: 157,538 -> 171,968 bytes (20 -> 21 keys; the skinned raw
    geometry is replaced by the rest-pose one, +1 actor key, mesh name
    aliased; 20 distinct geometry pointers both before and after)
  - bothunder (torch-heavy, 20 skinned keys): 1,798,921 -> 3,169,268 — the
    rest-pose geometry also carries the skeleton's bone meshes, which the
    un-posed storage omitted
  - qeynos2 (no skinned objects): 255,702 -> 255,702, unchanged
- No second copy: `distinctGeomPtrs` is unchanged per zone (20/98/52).
- `test_door_loading` (7), `test_asset_loading` (14) and `test_graphics` (63)
  pass; hygiene ctest: FileSizeCeiling, DocClaims, DocCounts, DocCitations,
  CommentClaims, BoardToIssuesHygiene pass. (TexturesReproducible /
  PackReproducible / DbReproducible fail only because their generator tools
  were not built in this 4 GiB-capped worker; CI builds them.)

## Pinned by

`tests/test_door_loading.cpp`:
- `B734ObjectGeometriesTest.SkinnedObjectGeometryIsAtRestPose` (AC1+AC2:
  actor-name exact key, 189 verts, non-degenerate bounds, STOR301 alias is
  the same pointer)
- `B734ObjectGeometriesTest.NoSkinnedGeometryStoredUnPosed` (no skinned
  geometry may sit at (0,0,0)-(0,0,0) bounds)
- `DoorManagerNullDeviceTest.FindDoorMeshByActorName` (AC2 witness:
  `findDoorMesh("TDTORCH301")` resolves and returns the torch: 189-vertex
  torch mesh + skeleton bone meshes = 222 verts [measured])

All are headless (EDT_NULL) and asset-gated by `tests/asset_requirement.h`
(SKIP loudly when the EQ client files are absent).
