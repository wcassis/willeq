# B909b — placed-object collision triangles held once (GitLab #229, remainder)

Branch `task/B909b-object-tris`, from `fable` @ `f2bb6aad`. Worktree
`/mnt/projects/fable/wt/audit-objtris`. Review media and logs:
`/mnt/projects/fable/review/B909b/`.

## Scope

`sp.object_tris` (`ctx_.objectWorldTriangles`) is the largest remaining
`spatial` owner in the client: 3.8–5.3 MB on x86 (gfaydark, dreadlands,
timorous) and 40.2 MB for growthplane on the Orange Pi. It is built in
`ZoneLoadComponent::loadZoneStep9_Collision()` by running an Irrlicht triangle
selector over EVERY placed-object node and appending the world-space result to
a per-BSP-region vector: 36 B per triangle per instance.

Defect hypothesis to test first (step 1): instances share their mesh (T504
already builds ONE `IMesh` per distinct `ZoneGeometry`), so the per-instance
world-space copy multiplies one mesh's triangles by its instance count.

Fix, by sharing and not by dropping: collision reads each distinct object mesh
ONCE, in object space, from the mesh the draw path already holds (charged to
`objects`), plus a per-instance record (the same world matrix the selector
used, a world AABB and a mesh slot), grouped by BSP region. A ray against a
region first rejects instances whose world AABB (conservatively inflated) the
ray segment cannot reach, then transforms the surviving instances' vertices
with the SAME `matrix4::transformVect` the selector applied and runs the SAME
Moller–Trumbore step in the same order. So the triangles tested are bit-for-bit
the triangles the old copy held, in the same order, and every hit is
bit-identical. Vertex-animated objects (flags, banners), whose private mesh is
rewritten every frame, keep a private object-space snapshot taken at the same
point the old copy was, so their collision stays the frozen load-time pose it
was before.

`sp.region_bounds` (`std::map<size_t, aabbox3df>`) is NOT in scope: it has
readers in eight TUs, a camera-controller pointer to the map, and a
frame-render order golden. Left with a note; it does not keep `spatial` over
budget once the object copy is gone.

## File territory

- NEW `include/client/graphics/object_collision_set.h`,
  `src/client/graphics/object_collision_set.cpp`
- `src/client/graphics/zone_load/renderer_zone_load_objects.cpp` (step 9 build)
- `src/client/graphics/player/renderer_player_collision.cpp` (the one reader,
  `rayTestDoorsAndObjects`, and the clear in `setupMinimalZoneCollision`)
- `src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp` (clears)
- `include/client/graphics/irrlicht_renderer.h`,
  `include/client/graphics/renderer_context.h`,
  `src/client/graphics/renderer/renderer_init.cpp` (member type)
- `tests/test_object_collision_set.cpp`, `tests/cmake/world_geometry.cmake`
- build lists that name `door_collision.cpp` (the new TU goes beside it)
- docs: `refactor/B909b/README.md`, `refactor/BOARD.md`, `docs/module_map.md`

## STOP conditions

- Rays against the new structure differ from the old copy in any bit on x86
  and the difference cannot be explained as float evaluation order → STOP,
  report.
- The object meshes' CPU vertex/index arrays turn out NOT to stay resident for
  the visit (something releases or rewrites them) → fall back to an owned
  object-space copy per distinct mesh, and say so.
- Pi moving frame time more than 3 % worse than fable in any measured zone →
  report with the collision-phase numbers; do not tune by dropping collision.
- Any budget, golden, baseline or gate assertion would have to change → STOP.
- A lock held by another owner → do not run on that lane.

## Verification bar

1. Step 1 numbers for growthplane, trakanon, overthere, gfaydark: instances,
   distinct meshes, triangles per instance, bytes per triangle, sharing.
2. `test_object_collision_set`: 12,000+ rays, old copy (the production
   `extractWorldTriangles` over real scene nodes) vs new set — hit/miss, hit
   point, hit triangle, distance bit-identical; RED when the order or the
   transform is perturbed. Held-once census: no world-space copy per instance.
3. Full suite (`ctest`), hygiene, `visual_gate.py` locally.
4. x86 rendered client, VM lane, growthplane + gfaydark,
   `WILLEQ_LEDGER_OWNERS=spatial`, before/after owner rows.
5. Pi A/B (CI ARM build of this branch vs fable), uinput-driven movement,
   growthplane, trakanon, gfaydark: `spatial`, RssAnon, RssAnon−gpu_driver,
   moving/idle frame time, Player Movement µs.
   Bar: `spatial` < 4 MiB in every measured zone on both platforms, rays
   identical, Pi moving frame time within 3 % of fable or better.
6. Locks released, stacks down, on success and on failure.

## Review

Done; evidence and numbers in `refactor/B909b/README.md`. Deviations from
the plan:
- The hypothesis held for gfaydark only. In growthplane, trakanon and overthere
  86–99 % of placements are vertex-animated with a PRIVATE mesh each, so the
  per-mesh sharing alone left growthplane at 30.2 MB. Snapshots are now shared
  by content (same bytes, same triangles), which brought every measured zone
  under 1.3 MB. The private meshes themselves are a problem for `objects`:
  B936 (#254).
- The owner row is renamed `sp.object_tris` → `sp.object_instances`, because it
  no longer holds triangles for shared meshes.
- trakanon's first three Pi rounds read +3.26 % moving (multimodal frame
  rate on both binaries), so four more rounds were run: +2.38 % over all 7 + 7
  legs, with the collision phase −55 µs.
- Local `visual_gate` fails memory-coverage slack on this host (B937, #255);
  the CI gate passed the same commit.
