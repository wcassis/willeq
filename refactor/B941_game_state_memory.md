# B941 — root-cause and shrink the `game_state` memory category (GitLab #259)

Branch `task/B941-game-state-memory` off fable `0b84eef6`, worktree
`/mnt/projects/fable/wt/audit-gamestate`. Review media:
`/mnt/projects/fable/review/B941/`.

## Why

`game_state` is the largest willeq-owned ledger category in the heavy
Kunark/Velious zones on the Orange Pi (B854 rendered sweep, fable 3c6ea8b3):
emeraldjungle 53.7 MB at `ready`, overthere 40.7, wakening 37.3, timorous 35.0,
skyfire 32.4, growthplane 26.4. The game-thread lifecycle points
(`game_pathfinder`, `game_zone_map`, `game_zone_sources`) put ~99 % of it on two
structures: the Detour navmesh (`PathfinderNavmesh::Load`) and the collision map
(`HCMap::Load` + `RaycastMesh`). Will's rulings: memory first, frame rate
second; 100 % accounting; budgets never raised; the navmesh is core (/follow
existed in the original client) and must be made to FIT, never excluded
(docs/decisions.md 2026-09-21).

## Scope

1. **Attribute** `game_state` to named owners: `StartupScope` labels (`gs.*`) at
   the load sites, printed by `WILLEQ_LEDGER_OWNERS=game_state`; a
   `game_state` mode for `scripts/client_ledger_capture.sh`. x86 owner table for
   emeraldjungle, timorous, growthplane, gfaydark, with the on-disk size of the
   `.nav` and `.map` beside each so the blow-up factor is visible.
2. **Fix by representation** (no capability dropped, queries identical):
   - `HCMap` V2: placeable triangles emitted as indexed geometry (each
     placement's used model vertices transformed once) instead of three private
     vertices per triangle.
   - `HCMap`/`RaycastMesh` load peak: free the compressed/inflated buffers as
     soon as they are consumed, exact-size the arrays, and hand the vertex/index
     arrays to the raycast mesh instead of copying them.
   - `PathfinderNavmesh::Load`: stream-inflate tile by tile (no whole-file
     compressed + inflated + per-tile copies at once), and after every tile is
     connected trim each tile's unused `dtLink` tail (links are allocated from a
     free list in index order and tiles are never removed, so the used links are
     a prefix; the trim keeps every index, so every query walks the same links in
     the same order).
3. **Pin identity**: a ctest test that loads real maps / navmeshes (skips when
   `/mnt/projects/maps` is absent) and a synthetic V2 map (always runs), and
   compares FindBestZ, CheckLOS/CheckLOSWithHit and navmesh FindPath /
   FindRoute old vs new over thousands of sampled points, bit-identical.

## File territory

`src/client/hc_map*.cpp`, `include/client/hc_map.h`, `src/client/raycast_mesh.cpp`,
`include/client/raycast_mesh.h`, `src/client/pathfinder_nav_mesh*.cpp`,
`include/client/pathfinder_nav_mesh.h`, owner labels only in
`src/client/zone/zone_entry.cpp` / `zone_component.cpp` /
`src/client/movement/movement_state.cpp`, new tests under `tests/`,
`scripts/client_ledger_capture.sh` + `scripts/client_ledger_table.py`
(game_state mode), `refactor/B941*`, `refactor/BOARD.md`, docs that quote the
old numbers. Not touched: any budget, golden, baseline or gate assertion.

## STOP conditions

- An identity comparison differs and the difference cannot be justified as a
  stated tolerance: that fix is withdrawn, not shipped with a looser test.
- The eqemu or Pi lock is held by another owner: wait/report, never overwrite.
- A fix moves an exact-match gate baseline: leave the baseline, report the new
  value, MR says it needs Will's bless.
- Pi frame time worse than fable by more than 3 %: report, do not claim the bar.

## Verification bar

Full suite + hygiene + local `visual_gate.py`; x86 client before/after owner
tables; Pi A/B vs fable (`scripts/pi/collision_ab.sh --ledger-owners
game_state`, uinput-driven movement) on emeraldjungle, timorous, growthplane,
gfaydark: `game_state` and owner rows, RssAnon, owned vs 64 MiB, idle and
moving frame time, plus a live pathing check. Locks released and stacks down on
every exit path.

## Review

Evidence, tables and the "Not verified" list: `refactor/B941/README.md`.

- Deviation: `include/common/util/ksm.h` deleted (its last user was
  `RaycastMesh`; ZeroCallers flagged its helpers), with its three B501
  EXEMPT rows in `scripts/refactor/check_zero_callers.py`.
- Deviation: `tests/test_hc_map.cpp` pinned the STORED vertex count of
  befallen / qeynos2, which this unit changes by design; the two numbers were
  updated (faces, bounds and pinned queries unchanged). No budget, golden,
  gate baseline or gate assertion was touched.
- Deviation: `scripts/client_ledger_capture.sh` now loads the collision map
  and navmesh (it loaded neither, so x86 `game_state` was ~0.3 MB) and gained
  `--tour` / `--client-arg`; `scripts/build-gles2-x86.sh` calls its stamp
  script by absolute path (it failed with exit 127 outside the repo root).
- B942 (#260) fixed on the branch; B945 (#263) filed, not fixed.
