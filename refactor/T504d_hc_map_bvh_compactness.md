# T504d — hc_map raycast BVH compactness (plan for Will's review)

**Status:** APPROVED by Will 2026-09-22 ("proceed with T504d"); levers 1-4
IMPLEMENTED the same night. Measured (`run117` vs `run114`, steady_60s):
game_state gfaydark 15.2 → 13.2 MiB, dreadlands 21.9 → 20.4; the
`[raycastmesh]` census (gfaydark): nodes 3,083,976 → 1,982,556 B (56 → 36
per node: no vtable, index children), raycast marks 518,548 → 129,637 (one
byte per triangle, cleared on the 255-frame wrap), leaf refs 2,097,152 →
1,486,704 (shrink_to_fit), and the 1,555,644-byte face-normal table is gone
(normals computed per hit; it was built lazily on the first normal query, so
it is a saving for the session, not always visible at steady_60s). Identity
net: `test_hc_map` ray-set goldens (24x24 FindBestZ + LOS grid per map,
befallen/gfaydark/dreadlands/iceclad, `tests/goldens/hc_map/`, blessed from
the pre-change code) identical, the pinned befallen values identical,
`test_raycast` +1 (1000 raycasts across the byte wrap). Lever 5 not taken.

## What it is

`game_state` is the largest RETAINED client term in the heavy zones after
T504c (gfaydark 15.2 MiB, dreadlands 21-23 MiB, `run112`/`run114`), and
9.4 MiB of it is the hc_map raycast BVH (`createRaycastMesh`, hc_map.cpp:279):
the zone's collision / line-of-sight map, queried by FindBestZ, LOS and the
ground-follow. Trace (`run112`, gfaydark, `game_state:login_world`, live at
steady_60s):

| term | live | where |
|---|---|---|
| navmesh tiles | 5.19 MiB | `PathfinderNavmesh::Load` (Detour; not this plan) |
| vertices + indices copy | 3.0 + 0.5 MiB | `MyRaycastMesh` ctor (page-aligned, KSM-marked); hc_map binds VIEWS to these, so it is the only copy |
| leaf triangle lists | 2.44 MiB | `NodeAABB::split` → `leafTriangles.push_back` (one shared TriVector, grown by push_back) |
| BVH nodes | 1.49 MiB | `NodeAABB::split` → `new NodeAABB` (40 B: two pointers, 24 B bounds, leaf index) |
| face normals | in the 3.0 | `mFaceNormals`: 12 B per triangle, zero-filled at build, filled on first hit |
| raycast scratch | 2.0 MiB | a `TriVector` reached from `NodeAABB::raycast` (stack hashed at depth 5; the exact owner is the first measurement below) |

## Levers (estimates, to be measured before any is taken)

1. **`mLeafTriangles.shrink_to_fit()` after the build.** push_back growth
   leaves up to 2x slack; the final size is the sum over leaves of
   (1 + count). Expected 0-1.2 MiB, zero risk.
2. **Face normals on demand, no array.** The array is 12 B per triangle,
   zeroed at build and computed lazily per triangle on first hit; the
   cross product is 9 mul + 6 sub per test and the per-triangle hit test
   already reads the three vertices. Expected 0.5 MiB (gfaydark), and a
   3 x tcount memset less at load.
3. **Node layout.** 40 B per node with two heap pointers; a flat
   `std::vector<Node>` with `uint32_t` child indices (or the implicit
   left = i+1 / right = skip layout) is 32 B or 28 B and one allocation
   instead of ~39,000 (dreadlands). Expected 0.3-0.5 MiB plus the
   allocator's per-block overhead (~16 B x nodes = 0.6 MiB in the ledger's
   block_overhead).
4. **Raycast scratch.** Identify the 2.0 MiB `TriVector` that stays live:
   if it is a per-call vector sized to the whole leaf list, replace it with
   a stack-bounded traversal (the tree depth is capped by `maxDepth`);
   if it is a retained per-thread scratch, size it to the largest leaf.
   Expected up to 2.0 MiB.
5. **Index width.** Indices are `uint32_t`; zones have < 65,536 vertices
   only sometimes (iceclad has more), so a 16-bit index is not general --
   NOT taken.

Total expected: 3-4 MiB of the 9.4 in the heavy zones, all in `game_state`,
all retained for the session. Measured against `[memledger] game_state` at
steady_60s and the trace's per-site rows; the identity net is
`test_hc_map_*` (FindBestZ / LOS results must not change: the queries are
pure functions of the same triangles) plus a raycast-result comparison
before/after on a fixed ray set (the T478 oracle rays are a ready sample).

## Order

Measure (1: the exact owner of the 2.0 MiB scratch, at trace depth 8 with
the category armed at `zone_load_complete`) → levers 1, 2 (safe) → 4 → 3.

## STOP conditions

1. Any FindBestZ / LOS / raycast result differs on the fixed ray set.
2. A lever's measured saving is under 0.3 MiB (not worth its code).
