# B997 — fewer draws per frame on the Pi: close the 10 MiB `gpu_unnamed_steady` guard (issue #317)

**Class B (memory / frame-time).** **Status: step 2 (draw census) complete;
the census below is the evidence for step 3.** Step 1 baseline:
`refactor/B997/BASELINE.md` (committed separately). Step 3 (draw reduction) is
next and is gated on the scene-graph lever characterized below.

The card was re-scoped 2026-10-09 (after B972-FT / Will's ruling (d), MR !94):
the lever is **draws per frame**, not buffering. Cost-sheet fact 5
(`docs/reference/lima_cost_sheet.md`): `lima_update_varying` allocates a fresh
~4 KiB varying BO **per draw**, VBO and client-array alike, and the BO stays
in-flight for a few frames (the churn depth). So the per-draw varying-BO pool
— the part of `gpu_unnamed_steady` a draw cut can touch — scales with draws
per frame. This card removes draws; it does not convert buffer types (that is
explicitly out of scope, and B962 measured a static zone VBO as the anti-pattern).

## Step 1 — baseline (fable ≥ 1b0f8645, ring + glFlush)

`BASELINE.md`. At `steady_60s`, Pi crowd tour, imp200far, 800×600, cache-on,
lane 2, `--idle 180 --hold-after 75`:

| zone | `gpu_unnamed_steady` (MiB) | pp jobs/f | wait ms/f | imp p50 (ms) | fps_med |
|---|---|---|---|---|---|
| qeynos2 | **17.63** | 2.26 | 0.054 | 25.6 | 30 |
| gfaydark | **20.08** | 2.30 | 0.053 | 31.2 | 25 |

Card expected ~17.1 / ~22.1, ~2.2 jobs/f — within run-to-run spread (see
BASELINE.md). The 10 MiB guard (`gpu_unnamed_over_bound`) reads RED: 7.63 / 8.69
MiB over.

## Step 2 — draws per frame by site (the census)

### How it was measured

A gated, off-by-default per-site census (`EQT_DRAW_CENSUS=1`, commit
`9f184921`, `include/client/graphics/draw_census.h` + 4 call sites) splits the
driver's generic 3D client-array pool (`draw3d:client` in `gem_owners`) by site,
in 120-frame windows on the render thread. It was run on **both** the x86
GLES2 tree (cheap leg, surfaceless EGL, no `WILLEQ_BASELINE`) and the **real Pi
client** (the faithful leg) in the same imp200far crowd tour.

**Why x86 is a faithful leg here (validated, not assumed):** the production
zone path is the pooled (T504) merge, and its per-generation `gen->buf` is
**never registered in the driver's `HWBufferMap`** (the comment at
`renderer_zone_draw.cpp:532`). So the pooled zone-merge draw takes the
**client-array** fallback path on *both* x86 and the Pi, and the split is
platform-consistent: `zone_draws` → `draw:zone` tag, `scene_graph` → the
generic `draw3d:client` pool, and `win_3d_vbo = 0` on both (impostors/nametags
use their own VBO rings + named pools, excluded from the client pool). The two
platforms agree to within run-to-run noise (below).

Sites:
- **`zone_draws`** — pooled `renderMerged()` gather-draws (+ the placeable
  objects that share the sorted zone list, drawn via `drawMeshBuffer`).
- **`entity_body`** — entity body mesh buffers
  (`EQAnimatedMeshSceneNode::render`, `drawMeshBuffer` on a buffer not in the
  HWBufferMap → client-array fallback).
- **`scene_graph`** — the derived residual
  `win_3d_client − (zone_draws + entity_body)`: every other 3D client-array
  draw the client does not directly instrument — **PVS doors + placed objects +
  equipment + target overlays**, i.e. the per-node `CMeshSceneNode` draws that
  `smgr->drawAll()` issues (renderer_frame_render.cpp:177).

`win_3d_vbo` (the driver's 3D VBO-draw total) is **0** on both platforms: the
only VBO 3D draws are the B972-FT impostor / B975 nametag rings, which draw
into *named* pools and are excluded from the `draw3d:client` client pool.

### Table: draws per frame (crowd-in steady, the guard's sampling point)

At `steady_60s` the 200-NPC crowd is in PVS range (candidates ~160), which is
what the reference legs measure (the `--idle 180` gotcha, BASELINE.md). The
driver `[drawcensus]` window at that instant and the per-site `[drawsite]`
window paired to it:

| zone | site | draws/frame | x86 | Pi | notes / what splits it |
|---|---|---|---|---|---|
| qeynos2 | zone_draws | **20** | 20 | 20 | pooled `renderMerged`: 1 run per (materialKey, materialId), **1 gather-draw per generation** in the run. Split by `materialKey` (16-bit texture-group id) and by the **65 535-vertex u16 index bucket** (`kMaxVertsPerBucket`). |
| qeynos2 | entity_body | **13** | 13 | 13 | one `drawMeshBuffer` per in-range entity body mesh buffer (the crowd models are mostly impostor pages, NOT here). |
| qeynos2 | scene_graph | **42** | 43 | 41 | `smgr->drawAll()` residual: PVS doors (~29 in-graph at steady) + placed objects + equipment + overlays, one `CMeshSceneNode` draw each. |
| qeynos2 | **win_3d_client** | **75** | 76 | 74 | the `draw3d:client` pool. `win_3d_vbo` = 0. |
| gfaydark | zone_draws | **38** | 38 | 38 | same mechanism; 1 429 regions / 32 materials / 32 generations (larger zones → more runs). |
| gfaydark | entity_body | **0** | 0 | 0 | gfaydark's crowd is all impostor pages. |
| gfaydark | scene_graph | **120** | 121 | 120 | PVS doors (~78 in-graph) + **2 829 placed objects** (the "Sequential fixup" pass) + equipment + overlays. |
| gfaydark | **win_3d_client** | **158** | 159 | 158 | the `draw3d:client` pool. `win_3d_vbo` = 0. |

**The scene-graph residual is the dominant lever** — 42/f on qeynos2 (56 % of
its draws), **120/f on gfaydark (76 % of its draws)**. The zone-merge (the
original B997 target) is only 20–38/f; entity_body is 0–13/f.

The post-crowd (idle, crowd PVS-culled) floor is lower: qeynos2 53/f
(zone 24 + scene 29), gfaydark 91/f (zone 38 + scene 53). The guard samples
crowd-in (`steady_60s`), so the crowd-in row is what counts.

### The memory mechanism (what `gpu_unnamed_steady` is, per zone)

`gpu_unnamed = gpu_buffers_net − logical` (`scripts/pi/rendered_zone_summarize.py`):
the client's own logical tally excludes the **per-draw varying-BO churn pools**
and the other Mesa driver allocations, so all of it counts toward the guard.
Decomposed from `gem_owners.tsv` @ steady_60s:

| zone | `gpu_unnamed` (MiB) | varying-BO pool (draws touchable) | other unnamed (Mesa fixed) |
|---|---|---|---|
| qeynos2 | 15.89 | **10.61** (d3c 9.28 + dzone 0.30 + vbo 1.03) | 5.28 |
| gfaydark | 18.69 | **14.90** (d3c 10.07 + dzone 3.81 + vbo 1.02) | 3.79 |

The varying-BO pool is one ~4 KiB BO per in-flight draw (fact 5). Its size
distribution (from `gem_owners`): **q2 1 548 `draw3d:client` BOs @ mean 6.1 KiB
(1 411 @ 4 KiB)**, **gd 1 366 @ mean 7.5 KiB (1 289 @ 4 KiB)** + the
`draw:zone` gather BOs (gd 81 @ 48 KiB — larger because the zone generations
are bigger). So the pool is proportional to in-flight 3D client-array draws,
and a draw cut removes its BO directly.

**"other unnamed"** (5.28 / 3.79 MiB) is the non-draw Mesa floor: per-frame
post-processing stream uploads, the shader-binary cache, the BO cache, and the
tile heap. **This part is not touchable by a draw cut.** It is *below* the
10 MiB target on both zones (q2 5.28, gd 3.79), so in principle the target is
reachable — but only if the varying-BO pool is cut hard enough:

| zone | unnamed | target | must cut | of which varying-BO pool | must cut (as % of pool) |
|---|---|---|---|---|---|
| qeynos2 | 15.89 | 10.0 | 5.89 | 10.61 | **56 %** |
| gfaydark | 18.69 | 10.0 | 8.69 | 14.90 | **58 %** |

That is ~38 fewer draws/frame on qeynos2 and ~91 on gfaydark — i.e. the
**entire scene-graph residual (42 / 120 per frame)** must almost entirely
disappear, plus part of the zone/entity share. (The pool is not exactly
∝ draws/frame: the per-BO size and churn depth vary, so treat 56/58 % as a
lower bound on the draw cut needed, not an exact one.)

### What splits the scene-graph residual (the step-3 target)

From the census `client.log` door records and the `steady_60s` memledger:

- **qeynos2:** 60 doors total, **~29 in-graph at steady** (PVS-culled the rest);
  models are diverse (DOOR1 ×48, BARREL3 ×16, PAWHOOK ×12, HHCELL ×8, …).
  Placed objects: 478 (fixup pass), 160 trees. Equipment/overlays the rest.
- **gfaydark:** 79 doors total, **~78 in-graph** (nearly all PVS-visible in the
  tour region); models FAYDOOR ×92-equivalent, FAYDOOR2A/FAYDOOR2, FELE2, …
  Placed objects: **2 829** (2 317 trees) — this is what pushes the residual to
  120/f. Equipment/overlays the rest.

So the residual is **per-node `CMeshSceneNode` draws** — mostly doors + placed
objects (trees, crates, props) + equipment + target overlays — one `smgr->drawAll()`
draw each, each allocating its own varying BO. The batching opportunity is a
shared per-material client-array batch (gather the visible doors/objects/equipment
of one material into one draw), the same shape as the zone-merge's pooled loop.
This is a **new batch-draw path** (pixel-identical by construction, proven by the
x86 `visual_gate` byte-identical PNGs before it goes to the Pi), not a
buffer-type change.

### Evidence

- `~/kb-evidence/b997-draw-count/BASELINE.md` — step 1.
- `~/kb-evidence/b997-draw-count/x86census-20261009T184539Z/` — x86 census leg
  (surfaceless EGL, `EQT_DRAW_CENSUS=1`, no `WILLEQ_BASELINE`).
- `~/kb-evidence/b997-draw-count/picensus-9f184921-20261009T190536Z/` — Pi
  census leg (census build `9f184921`, lane 2, `--env EQT_DRAW_CENSUS=1`,
  `--bo-census` B1006 shim). `zones/<zone>/client.log` carries `[drawcensus]`
  + `[drawsite]`; `zones/<zone>/gem_owners.tsv` carries the pool tags.
- Census instrument: commit `9f184921` (`draw_census.h` + the 4 call sites).

## Step 3 — reduce draws (in progress)

The census says the lever is the scene-graph residual (42 / 120 per frame).
Next: a pixel-identical per-material client-array batch of the visible
PVS doors + placed objects + equipment + overlays (the zone-merge's pooled
loop is the template), proven byte-identical on x86 (`scripts/ci/visual_gate.sh`)
before the Pi A/B. Zone-merge generation buckets are **not** reducible "for no
visual reason" — the 65 535-vertex u16 index space is the constraint, not a
spurious split. One change per commit, each with its draws/frame + `gpu_unnamed`
delta; the Pi A/B columns are `gpu_unnamed_steady`, draws/frame,
`gem_pp_jobs_per_frame` (≤ 2.5), imp p50.

If the measured reductions do not reach 10 MiB: report the residual (the
5.28 / 3.79 MiB Mesa floor above is the irreducible part a draw cut cannot
reach) with its mechanism and the per-change table, and block for Will's
ruling rather than reaching for a buffering change.
