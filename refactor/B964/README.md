# B964 -- impostor billboards, part 2: the production port

GitLab #282 (class B, lane pi). Plan: `refactor/B964_impostor_port.md`. Branch
`task/B964-impostor-port`. Part 1 (measurement, the design this ports):
`refactor/B952/README.md`. Raw evidence (logs, CSVs, crop sheets, tours, the
published ARM builds): `/mnt/projects/fable/review/B964/` (its README says what
each file is).

**Will's rulings (chat, 2026-09-30), recorded in `docs/decisions.md`:** "I agree
with the proposed imposter implementation. proceed"; "Your recommended impostor
budget sounds fine" -> 128^2 impostors, a 20 MiB `tex.impostor` budget.

## What was built

| piece | where |
|---|---|
| The rules, device-free: the blessed cap with a page as the allocation unit, the cell table (allocate / reuse / LRU demotion), the tier policy (distance floor + governor-driven full-model limit, pinned, hysteresis), the view-angle refresh, the per-frame unit budget | `include/client/graphics/impostor/impostor_policy.h`, `src/client/graphics/impostor/impostor_policy.cpp`, `tests/test_impostor_policy.cpp` (12 cases) |
| The cache: one 128^2 scratch render target (RGBA8 + DEPTH16, `addRenderTargetTexture()`, so it has Lima's multisample attachment); each capture `glCopyTexSubImage2D`'d into a 128^2 cell of a 1024^2 RGBA8 **atlas page** (64 cells, a raw GL texture wrapped with `gles2WrapTexture()`) | `include/client/graphics/impostor/impostor_cache.h`, `src/client/graphics/impostor/impostor_cache.cpp` |
| The draw: one scene node, **one `drawVertexPrimitiveList` per page**, camera-facing quads rebuilt at render time; its own GLSL ES program (constant 0.5 alpha threshold, RGB / alpha, the entity shader's fog); name tags of withheld entities re-registered | `include/client/graphics/impostor/impostor_batch_node.h`, `src/client/graphics/impostor/impostor_batch_node.cpp` |
| The pass: candidates, tiers, capture units, draw state, the full-model cap, the row, the `[impostor]` / `[impostor-frames]` lines; runs at the end of `updateMainThreadEntityState()` (frame phase 3, **before `beginScene()`**) | `src/client/graphics/entity_impostors.cpp` |
| Withholding an impostored model: T309's `setWithheldFromRenderList` + a new `setSkinningSuspended` (OnAnimate keeps the animator, skips the per-vertex skinning) | `include/client/graphics/eq/animated_mesh_scene_node.h` / `.cpp` |
| Hooks: despawn frees the cell; `updateEntityAppearance()` keeps it and marks it for re-capture; a zone change frees every page | `src/client/graphics/entity_lifecycle.cpp` |
| Frame inputs (governor, target, group names, loading screen) | `src/client/graphics/frame/renderer_frame_input.cpp` |
| The simulation worker's visibility cap grows by the cell capacity when impostors are on | `src/client/graphics/sim/renderer_sim_bridge.cpp`, `src/client/graphics/zone_load/renderer_zone_load_env.cpp` |
| The `[memledger-owner]` owner-row registry (header-only, a report, never an allocation), printed at every lifecycle point | `include/client/core/memory_owner_rows.h`, `src/client/core/memory_ledger.cpp` (`recordPoint`) |
| The budget: `ConstrainedRendererConfig::impostorTextureMemoryBytes` = 20 MiB and `impostorTierDistance` = 80 on `orangepi` (C++ preset and `config/constrained_presets.json`), pinned in both orangepi tests; 0 on every other preset (pinned too) | `include/client/graphics/constrained_renderer_config.h`, `src/client/graphics/constrained_renderer_config.cpp`, `tests/test_constrained_renderer.cpp` |
| Evidence tools: `impostor_pixel_compare pixel` (model vs impostor through `EntityRenderer`, per race/gender/distance, 3 billboard variants) and `drawcost` (N models vs N billboards, `--budget` to force the shared path); the crowd rigs (x86 client, Pi sweep) and their table scripts | `tools/hwcaps/impostor_pixel_compare.cpp`; `scripts/b964/{make_tour.py,crowd_run.sh,pi_crowd.sh,crowd_table.py,pixel_summary.py}`; `scripts/pi_rendered_zone_sweep.sh --stop-point` |

### The rules, as built

- **Cap.** `(n + 1) * 4 s^2 + 6 s^2 <= budget` with a page as the allocation
  unit: a page is allocated only while `(pages + 1) * 4 MiB + 96 KiB <= budget`.
  20 MiB = 4 pages = **256 unique cells** (the per-cell rule alone would admit
  318; the page rounding is what batching costs). Past it a spawn draws the
  shared cell of its race/gender/body-texture key; with no cell left for that
  key either, the least-recently-drawn unique cell is demoted and its spawn
  goes shared. Despawn frees a unique cell; unused shared cells are released
  every 30 frames; a zone change frees all pages.
- **Tiers.** Beyond `impostorTierDistance` (80, B952's tier) an entity always
  wants its impostor. Inside it the full-model limit follows the production
  `FrameBudgetGovernor`: every 15 frames (half its 30-frame window) -2 when it
  is Red or its rolling average is over target, +2 when Green and under
  0.85 x target, held otherwise, never above `maxVisibleEntities`; 10-frame
  boundary hysteresis within 3 slots of the limit (B952's). The target and the
  group members are pinned to the model. **The benchmark's private
  per-entity-ms predictor was not ported:** the governor publishes one rolling
  frame time, not a per-entity cost, and the step rule is the README design
  (B952, "Production port", point 1). At 5 frames per step the x86 crowd run
  swung the limit 40 -> 0 -> 40 inside 5 s and re-captured 277 times on the
  flips, hence 15.
- **The full-model cap is kept.** When impostors are on the worker's
  visibility cap is raised by the cell capacity, and the pass re-applies the
  old cap to everything that is not drawn as an impostor (full-tier
  candidates, candidates whose impostor is not captured yet, placeholders,
  corpses, boats): pinned then nearest first, at most `maxVisibleEntities`
  are drawn, impostors take no slot. Without this rule the Pi drew every
  streaming placeholder (qeynos2 1280x720: 20 fps with impostors on against 27
  off, `review/B964/pi/crowd/`).
- **Units.** A capture is a unit run before `beginScene()`; per frame Green 3 /
  half a frame budget, Yellow or Red 2 / a quarter, checked before each unit,
  never zero (an impostor is how the governor leaves Red). Creation first
  (nearest first), refreshes of an already-drawn impostor after.
- **Refresh triggers, never a timer.** (a) `updateEntityAppearance()`
  (`EntityAppearanceChanged` / `OP_WearChange`): the cell survives the rebuild
  and the old capture is drawn until the new one; (b) a tier flip back to the
  impostor when the view angle moved past the threshold or the animation
  changed since the capture (else the capture is reused: `flip_reused`);
  (c) the relative view angle (camera->entity direction minus the entity's
  yaw) past **30 degrees** (12 views per turn; chosen, not measured). Shared
  cells are not angle-refreshed (their capture is another spawn's).
- **Classification.** A non-original LOD feature on a baseline capability: off
  under `WILLEQ_BASELINE=1` (no cache, no row, no visibility-cap change: the
  pass returns at its first line), on by default only on `orangepi`.
  `WILLEQ_IMPOSTORS=0` turns off this feature alone (the A/B in every table
  below). `WILLEQ_IMPOSTOR_FINISH=1` glFinishes before the units so their
  timing is their own work (measurement only).

## Pixel evidence (no verdict -- per-pixel numbers)

`impostor_pixel_compare pixel`: per race (13) x gender, the entity built by
`EntityRenderer::createEntity()` with the zone shader the client's entities
use, held at one o01 frame, straight ahead at 40/80/160 units; the full model
(`forceImpostorTier(2)`) against the production impostor
(`forceImpostorTier(1)`, captured by `ImpostorCache`, drawn by
`ImpostorBatchNode`), background (40,40,60), fog 180-300 (the orangepi client's
300-unit clip). Billboard variants: **0 = shipped** (constant 0.5 threshold,
RGB / alpha, entity fog), **1 = B952's billboard** (built-in alpha test, no
un-premultiply, no fog), **2 = shipped without fog**. Medians over the 26
variants; shares of the model's footprint; dark-edge = impostor pixels with
luma < 24 where the model's is >= 24 (the #274 fringe), summed.

| platform | res | dist | variant | model px | impostor px | height px | silhouette mismatch | diff > 32 | interior diff med / p90 | dark-edge px |
|---|---|---|---|---|---|---|---|---|---|---|
| x86 | 800x600 | 40 | 0 | 1180 | 1183 | 86 | 6.3 % | 14.0 % | 5 / 27 | 1519 |
| x86 | 800x600 | 40 | 1 | 1180 | 1183 | 86 | 6.3 % | 14.6 % | 5 / 28 | 1751 |
| x86 | 800x600 | 80 | 0 | 292 | 299 | 44 | 5.7 % | 8.8 % | 4 / 19 | 265 |
| x86 | 800x600 | 80 | 1 | 292 | 299 | 44 | 5.7 % | 9.0 % | 4 / 20 | 326 |
| x86 | 800x600 | 160 | 0 | 75 | 74 | 22 | 1.9 % | 4.3 % | 3 / 13 | 50 |
| x86 | 800x600 | 160 | 1 | 75 | 74 | 22 | 1.9 % | 5.0 % | 3 / 16 | 55 |
| x86 | 1280x720 | 40 | 0 | 1712 | 1721 | 104 | 6.7 % | 13.4 % | 5 / 28 | 2254 |
| x86 | 1280x720 | 80 | 0 | 418 | 430 | 51 | 4.9 % | 8.1 % | 4 / 18 | 373 |
| x86 | 1280x720 | 160 | 0 | 108 | 111 | 26 | 2.7 % | 5.7 % | 3 / 14 | 63 |
| **Pi** | 800x600 | 40 | 0 | 1180 | 1189 | 86 | 6.5 % | 13.8 % | 5 / 25 | 1684 |
| **Pi** | 800x600 | 40 | 1 | 1180 | 1189 | 86 | 6.5 % | 14.2 % | 5 / 28 | 1910 |
| **Pi** | 800x600 | 80 | 0 | 292 | 296 | 44 | 6.2 % | 9.4 % | 4 / 18 | 333 |
| **Pi** | 800x600 | 80 | 1 | 292 | 296 | 44 | 6.2 % | 9.8 % | 4 / 20 | 394 |
| **Pi** | 800x600 | 160 | 0 | 75 | 74 | 22 | 3.5 % | 6.1 % | 3 / 13 | 52 |
| **Pi** | 800x600 | 160 | 1 | 75 | 74 | 22 | 3.5 % | 6.1 % | 3 / 14 | 62 |

All rows: `review/B964/{x86,pi}/pixel_summary.md`; variant 2 equals variant 0
everywhere inside the fog start (180). Heights match B952's (87/43/21 at
800x600). On the Pi the impostor's footprint matches the model's (1189 vs 1180
px at 40 units; B958's Lima inversion does not recur), and the silhouette
mismatch is within 0.5 points of x86 at 40/80 units and 1.6 at 160.

- **#274 (the dark fringe).** Dividing RGB by the sampled alpha cuts the
  dark-edge pixels by 13-19 % against B952's billboard (x86 40 units 1751 ->
  1519, x86 80 units 326 -> 265, Pi 80 units 394 -> 333); it does not remove
  them. What the remainder is (dark texels in the capture itself, or the
  minification) is not decomposed.
- **Fog -- the choice, with numbers.** Inside the fog (fog 180-300, models at
  200 and 260 units, x86 800x600):

  | dist | variant | interior diff med / p90 | diff > 32 |
  |---|---|---|---|
  | 200 | 0 (entity fog formula on the billboard) | **3 / 12** | 4.3 % |
  | 200 | 1 (built-in, no fog) | 20 / 26 | 7.1 % |
  | 200 | 2 (no fog) | 20 / 26 | 6.7 % |
  | 260 | 0 | **1 / 5** | 3.8 % |
  | 260 | 1 | 82 / 102 | 101.4 % |
  | 260 | 2 | 82 / 102 | 100.6 % |

  The billboard gets **exactly the full model's fog**: the entity vertex
  shader's linear factor on `length(mWorldViewProj * pos)` evaluated at the
  billboard's vertices, with the driver's fog range and colour (which the
  zone shader is synced from). B952's "fog made it worse" was the built-in
  alpha-test shader's fog, which is a different formula; the capture itself is
  fog-free (an orthographic clip position is ~1 unit long).
- **x86 interior differences are larger than B958's** (median 4-5 vs 1-2):
  this test lights the model with the zone shader (sun, ambient, per-pixel
  player light) and its textures minify differently at the capture's 2-3
  texels per pixel; B952/B958's test drew unlit built-in materials.
- **No CI golden can move.** The goldens are `gles2_zone_benchmark`'s frames;
  the benchmark never constructs an `EntityRenderer` (grep `tools/hwcaps/zone_bench*`)
  and does not read the impostor fields. Local `visual_gate.sh`: every golden
  frame and metric matched; its only misses are host-load CPU p95 (iceclad
  16.9 > 9.5 ms, timorous 27.8 > 24.2 ms) and iceclad's memory-coverage
  allocator slack 35.7 % > 35 % (B937's dev-u24 shape). CI's gate is the
  record.

## Draw cost vs N (one draw per page)

`impostor_pixel_compare drawcost`, 1280x720, AA 4 (the client's orangepi
recipe), N entities 100-160 units ahead, models (tier 2) then impostors (tier
1, created by the scheduled units), 60 measured frames each, glFinish-bracketed
(the entity pass + the scene). No zone.

| platform | N | models p50 / p95 ms | impostors p50 / p95 ms | billboard draws | pages | tex.impostor B | creation frames | unit ms avg / max | worst frame of units ms |
|---|---|---|---|---|---|---|---|---|---|
| x86 | 1 | 1.54 / 4.57 | 1.34 / 3.99 | 1 | 1 | 4,292,608 | 1 | 4.59 / 4.59 | 5.5 |
| x86 | 50 | 28.27 / 32.49 | 2.32 / 2.71 | 1 | 1 | 4,292,608 | 17 | 1.66 / 4.86 | 9.4 |
| x86 | 200 | 69.33 / 81.41 | 4.29 / 6.07 | 4 | 4 | 16,875,520 | 67 | 1.49 / 4.86 | 9.4 |
| **Pi** | 1 | 16.62 / 16.92 | 16.63 / 17.38 | 1 | 1 | 4,292,608 | 1 | 2.97 / 2.97 | 11.6 |
| **Pi** | 50 | 89.94 / 133.16 | 16.63 / 16.70 | 1 | 1 | 4,292,608 | 17 | 1.50 / 2.97 | 14.5 |
| **Pi** | 200 | 516.07 / 555.72 | **26.90 / 27.41** | 4 | 4 | **16,875,520** | 67 | 1.43 / 2.97 | 14.5 |

- 200 billboards in **4 draws** cost the Pi ~10 ms over its 16.6 ms vblank
  floor; B952 measured one draw per billboard at ~0.12 ms each (200 -> 46 ms).
- `tex.impostor` at N = 200 unique: 4 pages + scratch = 16.09 MiB, under the
  blessed 20 MiB.
- Creation: 3 units per Green frame, 67 frames for 200; the most capture work
  in one frame was 9.4 ms (x86) / 14.5 ms (Pi), under a 33.3 ms frame budget.
- **The shared path** (`--budget 4292608` = one page = 64 cells, N = 200, x86):
  38 unique + 26 shared cells (13 races x 2 genders), 26 demotions, 136 shared
  claims, all 200 drawn as impostors in 1 draw at 5.99 / 12.94 ms; the row is
  exactly the budget (`review/B964/x86/drawcost_budget1page/`).

## The client, crowd runs

Rigs: x86 `scripts/b964/crowd_run.sh`, Pi `scripts/b964/pi_crowd.sh` (the
rendered Pi sweep, own memory cgroup, LLVM-less Mesa, local eqemu lane). The
tour (`make_tour.py`): the GM `#spawn`s N player-race NPCs (14 races x 2
genders x 4 body textures) on a 3-unit grid 4-46 units ahead of the safe
point, **zones to `arena` and back** (post-load spawns are built one step per
Green frame and the Pi is rarely Green -- 0 of 200 built in 4 min at 1280x720,
filed #291 / B973; a zone LOAD builds them), then for `far` steps 110 units
back (crowd 115-160 units away), faces the crowd, idles 40 s, walks 30 units
toward it and back (24 s), holds 15 s, quits. `near` stands at the safe point
(crowd 4-46 units). Variants: `imp` impostors on, `off` `WILLEQ_IMPOSTORS=0`.
Frame times are the `[impostor-frames]` windows (interval between two impostor
passes = the frame) in the idle / moving phases, median of the 5 s windows;
`off` runs have only the client's own `FPS:` lines. Memory at `steady_60s`
(after the reload).

### Orange Pi, 800x600 (the measurement of record)

PI_TABLE_PLACEHOLDER

### Orange Pi, 1280x720, before the full-model-cap rule (the finding that produced it)

ARM build `e75552f0` (the raised visibility cap without the rule), qeynos2, no
zone hop (the crowd stayed placeholders: 0 of 200 built): impostors on
`fps_med` **20**, impostors off **27** -- every streaming placeholder drew. The
same tour at 800x600 on `4a81a6b1` (the rule in): 31 vs 31 `fps_med`, 28 of
200 built. (`review/B964/pi/crowd/`; the 800x600 pre-hop run directories were
overwritten by the hop session -- the two numbers are from the sweep log lines
quoted here.)

### x86 (llvmpipe) -- not performance evidence

`review/B964/x86/crowd/table.md` (18 runs, three zones x
`imp0,imp50far,imp200far,imp200near,off200far,base0`, ARM-build-loaded host,
load average 15-20) ran BEFORE the full-model-cap rule and without the zone
hop; `review/B964/x86/hoptest/` is qeynos2 `imp200far` with both. A 200-NPC
crowd puts the x86 client at 2.0-2.3 GiB RssAnon, ~2 GiB of it `gpu_driver`,
**with impostors on or off** -- llvmpipe-side, the Pi stays at 21 MiB (filed
#292 / B974). x86 capture units ran 57-500 ms on average in that matrix
(llvmpipe flushes the previous frame's deferred rasterization at the capture's
first FBO read: with `WILLEQ_IMPOSTOR_FINISH=1` the same run measures 8.4 ms
per unit, `review/B964/x86/crowd_smoke3/`).

| run (x86, after the rule, hop) | idle p50 / p95 ms | impostors | cells | pages | tex.impostor | RssAnon | gpu_driver | owned |
|---|---|---|---|---|---|---|---|---|
| qeynos2 imp200far | 16.7 / 16.8 | 144 | 179 | 3 | 12.09 MiB | 894 MiB | 809 MiB | 85.6 MiB |

## Governor behaviour observed

GOV_PLACEHOLDER

## Findings filed

- **#291 (B973, A)** post-load entity builds are Green-gated: on the Pi a
  crowd spawned after zone-in stays placeholder cubes (0 of 200 built in 4 min,
  qeynos2 1280x720; 28 at 800x600).
- **#292 (B974, B)** x86 llvmpipe: a 200-NPC crowd costs ~2 GiB of
  `gpu_driver`, impostors on or off; not on the Pi.
- (Already filed by another unit, hit here: **#288 (B970)**, CI `build-arm`
  cannot build while downloads.sourceforge.net is down -- see Deviations.)

## Deviations

- **The budget is a `ConstrainedRendererConfig` field
  (`impostorTextureMemoryBytes`), not a `memoryLedgerBudgets.tex_impostor`
  key.** `memoryLedgerBudgets` keys are allocator categories
  (`eqt::memory::Category`), and impostor pixels are not allocator memory
  (Lima GEM on the Pi, llvmpipe's heap inside `gpu_driver` on x86); a new
  category would change the gate's ledger schema. It is declared and pinned the
  way B943's per-subsystem budgets are (C++ preset, JSON, both orangepi tests,
  0 on the other presets).
- **The ARM builds were made locally** (`scripts/build-arm-noble.sh`'s two
  docker steps with Irrlicht and libpng taken from local copies --
  `/mnt/projects/irrlicht-1.8.5.zip`, libpng 1.6.37 from GitHub -- into an own
  image tag `willeq-arm-noble-b964`), because downloads.sourceforge.net
  answered 522 all session (CI `build-arm` jobs 1937/1938 failed in the
  Irrlicht layer, #288). Published with `scripts/pi/publish_arm_build.sh` into
  `review/B964/arm/<sha>/` (not `ci-arm/`), plus a stripped, prefix-rpath'd
  `impostor_pixel_compare-nollvm`.
- **The per-entity-ms predictor** of the benchmark was replaced by the
  governor step rule (above), as the B952 design said.
- **x86 crowd matrix predates two fixes** (the cap rule, the zone hop); kept as
  recorded, the hop test re-runs the key case.
- Commit trailer: `Co-Authored-By: Claude Opus 5.5` (the model that did the
  work), not the `Claude Fable 5.1` named in the dispatch.

## Not verified

- The 30-degree view-angle threshold and the 15-frame step are chosen, not
  derived from a measurement of visible error or oscillation.
- Animated poses: the impostor is the pose at capture time; a walking NPC
  slides in a frozen pose until a refresh trigger fires (no animation trigger
  beyond the flip check).
- Shared cells use one spawn's capture for every claimant (size, equipment and
  helm differences between claimants are not reproduced); the shared path was
  exercised by the drawcost tool and the unit tests, not by a live crowd (no
  live run exceeded 256 cells).
- Camera pitch: billboards are vertical; looking down at a nearby impostor
  shows a flat card (the tier distance keeps impostors >= 80 units unless the
  governor narrows inside it).
- Group pinning matches the group window's member NAMES against entity names.
- The impostor pages on the Pi as GEM: the row is the logical size; the memcg
  `gpu_buffers` row moves with them (table above) but no per-BO census was
  taken here (B955 measured each 128^2 texture as exactly its pixels).
- x86 frame times (shared, loaded host) and every x86 crowd number before the
  cap rule.
