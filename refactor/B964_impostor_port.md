# B964 -- impostor billboards, part 2: the production port

GitLab #282 (class B, lane pi). Branch `task/B964-impostor-port`, worktree
`wt/audit-impostor2`, cut from `gitlab/fable` 4f3e37cd. Follows B952 (#270,
measurement, `refactor/B952/README.md`) and B958 (#276, the Lima alpha test).

Will's rulings (chat, 2026-09-30): "I agree with the proposed imposter
implementation. proceed"; "Your recommended impostor budget sounds fine" ->
128^2 impostors, a 20 MiB `tex.impostor` budget.

## Scope

1. **Impostor cache** (`include/client/graphics/impostor/`,
   `src/client/graphics/impostor/`): one scratch render target (128^2 colour +
   DEPTH16) for every capture; the capture is copied into an **atlas page**
   (1024^2 RGBA8 = 64 cells of 128^2). Pages are the texture storage, so
   `tex.impostor` counts pages + scratch. Cells are keyed unique (spawn id) or
   shared (race / gender / body texture variant). Cap check before a new page:
   `(pages + 1) * pageBytes + scratchBytes <= budget` -- the blessed
   `(n + 1) * 4 s^2 + 6 s^2 <= B` with the allocation unit a page (64 cells).
   With no free cell a new spawn takes its shared key; with no cell for the
   shared key either the least-recently-drawn unique cell is demoted (its
   spawn goes shared). Despawn and zone change free cells.
2. **Budget**: `ConstrainedRendererConfig::impostorTextureMemoryBytes`
   (JSON `impostorTextureMemoryBytes`), 20 MiB on `orangepi` in both the C++
   preset and `config/constrained_presets.json`, pinned in
   `tests/test_constrained_renderer.cpp` the way B943 pinned its budgets; 0 =
   impostors off (every other preset). `impostorTierDistance` beside it.
3. **Tier policy** (`EntityRenderer`, new TU `entity_impostors.cpp`, pure logic
   in `impostor_policy.{h,cpp}`): beyond the tier distance always an impostor;
   inside it the production `FrameBudgetGovernor` narrows/widens the full-model
   limit (Red narrows, Green widens, +-2 per step, hysteresis 10 frames per
   entity, 0.85 safety margin on the rolling average); target and group
   members pinned to the full model; an entity stays the full model until its
   impostor exists. The full-model count stays bounded by
   `maxVisibleEntities`; the visibility cap handed to the simulation worker
   grows by the impostor cell capacity when impostors are on.
4. **Creation / refresh** are units scheduled by the governor state before
   `beginScene()` (from `updateMainThreadEntityState`, phase 3): a few per
   frame, never a batch. Refresh triggers: `updateEntityAppearance`
   (`EntityAppearanceChanged` / `OP_WearChange`), a tier flip to impostor, a
   view-angle change past a documented threshold. No timer.
5. **Draw**: one scene node draws every billboard of a page in one
   `drawVertexPrimitiveList` call (camera-facing quads, own GLSL ES program:
   constant 0.5 alpha threshold per B958, un-premultiplied RGB for the #274
   fringe, the entity shader's fog formula). Impostored entities are withheld
   from the render list (T309's seam) and their skinning is suspended; name
   tags are re-registered by the batch node.
6. **Baseline**: impostors are classed as a non-original LOD feature:
   `WILLEQ_BASELINE=1` turns them off (no cache, no row), so a baseline run is
   the pre-B964 client.
7. **Accounting**: `[memledger-owner] ... owner=tex.impostor` at every
   lifecycle point (a generic owner-row registry in the ledger), the pages and
   scratch are in the driver's `gpuTextureBytes` tally (so in the `gpu_buffers`
   logical column), and on the Pi in the memcg `gpu_buffers` row.

## File territory

New: `include/client/graphics/impostor/*`, `src/client/graphics/impostor/*`,
`src/client/graphics/entity_impostors.cpp`, `tests/test_impostor_policy.cpp`,
`tools/hwcaps/impostor_pixel_compare.cpp`, `scripts/b964/*`, `refactor/B964/`.
Edited: `entity_renderer.h`, `entity_lifecycle.cpp`, `entity_updates.cpp`,
`animated_mesh_scene_node.{h,cpp}` (skinning suspend flag),
`constrained_renderer_config.{h,cpp}`, `config/constrained_presets.json`,
`tests/test_constrained_renderer.cpp`, `memory_ledger.{h,cpp}` (owner rows),
`renderer_sim_bridge.cpp` + `renderer_zone_load_env.cpp` (visibility cap),
`renderer_frame_input.cpp` (frame inputs), `CMakeLists.txt`,
`tests/CMakeLists.txt`, docs. Not touched: `renderer_frame_render.cpp` (G05
golden), the benchmark (`tools/hwcaps/zone_bench_*`, so no gate golden can
move), any other budget.

## STOP conditions

- A visual-gate golden moves.
- WILLEQ_BASELINE=1 output differs from the pre-B964 client.
- The Pi cannot draw the billboard program correctly (B958 class).
- Pi or eqemu lock held by another unit for the whole session (report, do not
  steal).

## Verification bar

Full suite + hygiene + local visual gate (goldens untouched), ARM CI build,
file-size check; x86 and Pi measurements (qeynos2 / gfaydark / growthplane,
crowd N=50/200 via `#spawn`); pixel numbers at 40/80/160 units; hitch and
refresh counts; README with tables and "Not verified".
