# T473 — a door whose mesh was built while its texture was pending is never re-textured (B640)

Class A (unconfirmed live), **no lock, no Pi**, slot3. Found by T470 (read its
Review, `refactor/T470_door_mesh_leaks_and_dead_loader.md`, and T468's; reuse
their seam in `tests/test_door_texture_eviction.cpp`). The Pi loads textures
under a tight budget and asynchronously, so a door built before its texture
lands is the likely case there, not an edge case.

## The lead

`DoorManager::findDoorMesh()` can build a real (non-placeholder) mesh while the
door's texture is still pending, and deliberately does not cache it (T470 row 2).
When the texture arrives, `rebuildSingleDoor()` has no branch for a door that
already has a real mesh — T470's throwaway probe measured `rebuilt=0`, mesh
unchanged, texture `nullptr` before and after. Result: that door draws
untextured (or with whatever the null texture binds to on GLES2) for the rest of
the zone.

## Tasks

1. Establish the texture-arrival path for doors: what signals a pending door
   texture has landed (the zone texture merge / `renderer_zone_merge`,
   `ZoneMeshBuilder::loadTextureFromBMP`, async upload results), and which
   door rebuild entry points it calls. Cite each hop.
2. Pre-fix failing test on the T468/T470 seam: build a door with its texture
   pending, deliver the texture through the real arrival path, assert the door's
   node material and the mesh it draws carry the real texture (and the mesh is
   then cached per T470's ownership rule).
3. Fix in the rebuild path (re-texture or rebuild a real-mesh-without-texture
   door), respecting T470's reference ownership (`cacheDoorMesh()`, caller-owned
   returns) and T468's guard. Add a refcount assertion so the fix cannot
   reintroduce a leak.
4. Census: other zone-geometry holders that can be built before their texture
   arrives and never re-textured (object/placeable meshes, region meshes — B620
   is the eviction sibling); board what you find (B670–B679), fix only doors.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
touched TUs; visual gate (door goldens must not move — if one moves because a
door now textures correctly, STOP and report, Will blesses); ARM; file sizes.
Board B670–B679; strike/narrow B640.

File territory: `src/client/graphics/door_manager*`, the door texture-arrival
hook, door tests. **Not** the camera component / hotkeys / `/camera` (T471,
slot1), **not** entity/equipment attachment (T472, slot2).

STOP: the fix needs a zone-lifecycle order change (report); an image golden
moves.

## Review

(executor fills)
