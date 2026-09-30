# B951 — shrink the collision map's remaining arrays (GitLab #269)

Branch `task/B951-collision-map-arrays` off fable `267e5a36`, worktree
`/mnt/projects/fable/wt/audit-hcmap`. Review media:
`/mnt/projects/fable/review/B951/`. Follow-on to B941 (#259,
`refactor/B941/README.md`).

## Why

After B941, emeraldjungle's `game_state` is still 34.8 MB on the Pi and the
collision map (`HCMap` + `RaycastMesh`) is 17.9 MB of it, 5.1x its 3.52 MB
`.map` file: vertices 4.40 MB, indices 7.42 MB, leaf triangle references
3.77 MB, nodes 1.68 MB, raycast marks 0.62 MB. Pi emeraldjungle owned
(RssAnon - gpu_driver) is +16.4 MB over 64 MiB. Will: memory first, frame rate
second; 100 % accounting; the original client ran these zones in 32 MB total.

## Scope

1. **Attribute** every array per zone (emeraldjungle, timorous, growthplane,
   gfaydark) on x86 with a standalone harness over the real loader: element
   widths, leaf-reference duplication (how many leaves list each triangle),
   node layout, and the V2 payload's own section sizes beside them.
2. **Fix by representation, same tree, same answers**:
   - the AABB tree is BUILT exactly as before (same splits, same bounds, same
     leaf sets) and then re-laid-out; nothing about which boxes a ray visits
     changes;
   - triangles are stored once, in leaf order (first leaf that lists them),
     so a leaf is a `[begin, end)` range of the triangles it owns plus a short
     list of references to triangles an earlier leaf owns (the ones that span
     a split);
   - vertex indices bit-packed at the width the zone needs
     (`bits(vertexCount-1)` per index, a triangle in `ceil(3*bits/8)` bytes);
     triangle references at `ceil(bits(triangleCount-1)/8)` bytes;
   - vertices renumbered in first-use order (locality; values unchanged);
   - nodes 36 -> 28 bytes: the left child is always the next node (the build
     is pre-order), so a node keeps its exact float bounds plus ONE word
     (right child, or leaf ordinal); leaf ranges in two per-leaf tables;
   - the per-triangle raycast mark array is dropped IF the ray benchmark says
     re-testing a shared triangle is not slower (a re-test cannot change the
     answer: same t, and a tie keeps the first hit's location);
   - the placeable flag becomes one bit per triangle (triangles are reordered).
3. **Accounting**: `gs.collision_map.*` owner rows for each final array under
   `WILLEQ_LEDGER_OWNERS=game_state`; the total stays in `game_state`.
4. **Identity**: every `GameStateIdentity` query digest unchanged. The
   triangle digest becomes an order-independent digest of the triangle
   multiset (sorted raw bits + placeable flag), computed with the OLD code
   (fable 267e5a36) and pinned — the triangle ORDER is the one thing that
   changes, and the sorted digest still covers every triangle's raw bits and
   flag with multiplicity. `tests/goldens/hc_map/*_rays.txt` untouched.

## File territory

`src/client/raycast_mesh*.cpp`, `include/client/raycast_mesh.h`,
`src/client/hc_map.cpp`, `include/client/hc_map.h`, the owner-label hook in
`src/client/zone/zone_entry.cpp`, `tests/test_game_state_identity.cpp`,
`tests/test_raycast.cpp` / `tests/test_hc_map.cpp` (API only),
`tests/cmake/world_geometry.cmake`, `CMakeLists.txt` (new TU lists),
`refactor/B951*`, `refactor/BOARD.md`, docs quoting the old numbers. Not
touched: any budget, golden (other than the triangle digest re-derivation
above), baseline or gate assertion.

## STOP conditions

- A query digest differs from the pre-B951 value and the difference cannot be
  justified: that change is withdrawn, not shipped with a looser test.
- The Pi ray benchmark or the Pi Player Movement phase is slower than fable by
  more than noise and no layout fixes it: report, do not ship a slower
  collision path silently.
- The Pi or eqemu lock is held by another owner: wait/report, never overwrite.

## Verification bar

Full ctest suite, hygiene, `check_file_sizes.sh fable`, local `visual_gate.py`
(iceclad slack ~35.4-35.7 % = B937, host-only), x86 owner tables before/after,
Pi A/B vs fable (`scripts/pi/collision_ab.sh --ledger-owners game_state`) on
emeraldjungle, growthplane, gfaydark (+ timorous if the zone link holds):
`game_state` + owner rows, RssAnon, owned vs 64 MiB, idle + moving frame time,
Player Movement phase, plus a Pi ray micro-benchmark old vs new. Bar:
emeraldjungle collision map well under 2x its `.map` file, queries identical,
Pi frame time within 3 % of fable or better, collision phase not slower.

## Review

Filled in `refactor/B951/README.md` (per-array attribution before/after, x86
owner tables, Pi A/B, ray benchmarks, identity, "Not verified"). Deviation:
the "well under 2x the `.map`" bar is NOT met (emeraldjungle 3.0x); the two
layouts that would go further made Pi rays slower and are put to Will as B957.
