# B936 — vertex-animated placeables share one mesh per geometry (GitLab #254)

Branch `task/B936-shared-animated-placeables`, from `fable` @ `a5068aee`.
Worktree `/mnt/projects/fable/wt/audit-sway`. Review media and logs:
`/mnt/projects/fable/review/B936/`. Results: `refactor/B936/README.md`.

## Scope

In the Kunark and Velious zones almost every placed object is a
vertex-animated plant or grass tuft (growthplane 4,314 of 4,333 placements
over 4 geometries, B909b). `ZoneLoadComponent::loadZoneStep6_Objects()` shares
one `IMesh` per `ZoneGeometry` (T504) but excludes vertex-animated geometries,
"because their vertices are rewritten every frame", and registers one
`VertexAnimatedMesh` (frame binding + a `size_t` per vertex) per placement.
The renderer then rewrites every placement's vertices on every keyframe
change (`SimBridgeComponent::applySimulationOutput`).

Step 1 tests the T504 reason before undoing it. What the code does today:
the animation is DISCRETE keyframes (fragment 0x37: `frames[k].positions`,
one `delayMs` per geometry), no interpolation; the worker advances one
counter per registered entry, every counter starts at frame 0 with 0 ms
elapsed (`updateVertexAnimData`) and receives the same delta every tick
(`computeVertexAnimations`). So every placement of one geometry shows the
SAME keyframe at every instant: the private meshes hold identical bytes at
all times, and the exclusion buys nothing.

Fix: animated geometries are shared exactly like static ones: one `IMesh`
per geometry, one `VertexAnimatedMesh` per MESH (not per placement), one
rewrite per keyframe change per geometry. Per placement: the scene node
(transform + its material copies), as for a static object. The visual result
is the same by construction (same keyframe, same bytes); it is proven
mechanically below.

Why not a vertex-shader sway (the preferred option in the dispatch): the
animation is not a procedural sway, it is recorded keyframe positions. A
shader would need every keyframe resident on the GPU and a way to select one
per draw; the Mali-400 vertex processor has no texture units (no vertex
texture fetch), so that means one attribute set per keyframe per geometry,
which is more memory than the one shared buffer, for a CPU cost that is
already O(distinct geometries) once shared (4 rewrites per keyframe change in
growthplane instead of 4,314). The CPU path that remains is exactly the
"shared source + one buffer per geometry" the dispatch allows.

The benchmark half (#247): `gles2_zone_benchmark --objects` builds a private
mesh per placement for EVERY object (T504's sharing was never ported to it),
which is why growthplane's `placeableObjectsBytes` is 28.7 MB. Sharing there
changes `placeableObjectsBytes` in all five gate zones' memory-coverage
baselines (an exact-match field), i.e. it needs a baseline bless by Will.
It therefore ships on a SEPARATE branch, `task/B936b-bench-shared-objects`
(stacked on this one, because it reuses the shared keyframe helper), with its
own MR that stays open for Will. This branch touches no benchmark code and no
gate input.

## File territory

- NEW `include/client/graphics/placed_object_meshes.h`,
  `src/client/graphics/placed_object_meshes.cpp` (binding + keyframe apply,
  shared by the client and the benchmark)
- `src/client/graphics/zone_load/renderer_zone_load_objects.cpp` (step 6
  sharing and registration, step 9 animated list, owner labels)
- `src/client/graphics/sim/renderer_sim_bridge.cpp` (apply)
- `include/client/graphics/renderer_types.h` (`VertexAnimatedMesh`)
- `tests/test_placed_object_meshes.cpp` + its cmake registration
- `scripts/client_ledger_capture.sh` (`objects` mode),
  `scripts/pi/collision_ab.sh` (`--ledger-owners`)
- docs: `refactor/B936/README.md`, `refactor/BOARD.md`, `docs/module_map.md`,
  B909b's header comment in `object_collision_set.h`
- Branch B only: `tools/hwcaps/zone_bench_loader.cpp` (+ options) — sharing,
  `--object-anim-frame K`

## STOP conditions

- The placements of one geometry are found NOT to be in phase in fable (any
  per-placement phase, random start or per-node delta) → sharing would change
  the picture; STOP and report.
- A per-pixel diff between private and shared meshes at a fixed keyframe that
  cannot be explained → STOP.
- Anything else that writes a placement's mesh (fixup pass, texture
  animation, debug swaps) turns out to need a per-placement copy → STOP.
- Pi frame time more than 3 % worse than fable in a measured zone → report
  with the per-phase numbers.
- Any budget, golden, baseline or gate assertion would have to change on this
  branch → STOP (branch B's baseline move is reported, not blessed).
- A lock held by another owner → do not run on that lane.

## Verification bar

1. Root cause with numbers: bytes per animated placement (vertex + index
   arrays, mesh objects, the per-vertex mapping), the 4 growthplane
   geometries once each, CPU rewrite volume per keyframe change.
2. Unit test (CI): two placements of one animated geometry own ONE vertex
   array; one registration per mesh; the keyframe apply writes exactly the
   keyframe positions (+ centre offset) through the mapping.
3. Pixel parity (branch B binary, same process, x86 llvmpipe): private vs
   shared meshes at fixed keyframes and fixed poses, max abs diff and
   differing-pixel count per frame; and fable vs branch B with no keyframe.
4. Full suite, hygiene, local `visual_gate.py` (goldens untouched).
5. x86 rendered client (VM lane, owner `b936`), growthplane + gfaydark,
   `WILLEQ_LEDGER_OWNERS=objects`, fable vs branch.
6. Pi A/B vs fable (CI ARM build), growthplane, trakanon, overthere,
   gfaydark: `objects` + owner rows, RssAnon, owned = RssAnon − gpu_driver vs
   64 MiB, idle/moving frame time; benchmark `placeableObjectsBytes` on the
   Pi from branch B's ARM build.
   Bar: client `objects` < 40 MiB and benchmark `placeableObjectsBytes`
   < 24 MiB in every measured zone on both platforms; Pi frame time within
   3 % of fable or better.
7. Locks released and stacks down on every exit path.

## Review

(filled at hand-back; numbers in `refactor/B936/README.md`)
