# B997 — fewer draws per frame on the Pi: close the 10 MiB `gpu_unnamed_steady` guard (issue #317)

**Class B (memory / frame-time).** **Status: COMPLETE per Will's ruling (a)
of 2026-10-10 (#317 note 3233) — the draw-count lever is at its ceiling; the
10 MiB `gpu_unnamed_steady` guard stays RED on qeynos2 and gfaydark as a
DOCUMENTED KNOWN CONDITION (same class as B988/#307), the bound is not
re-scoped, no buffering change is allowed, and the real levers (varying stride,
per-frame vertex volume) are B1013 #334.** The per-material client-array batch
was built (gated off by default at step 3) and the Pi A/B is measured; the
measured result + mechanism are the step-4. The batch's own ship decision came
from the deterministic x86 door pixel A/B: **byte-identical on both zones →
the default is now ON** (ON unless `EQT_SG_BATCH=0`). See the "Known condition
(ruling a)" and A/B sections below. Step 1 baseline:
`refactor/B997/BASELINE.md`. Step 2 (draw census) below.

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
10 MiB target on both zones (q2 5.28, gd 3.79), so the target is reachable in
principle — by cutting the cuttable pool, not the floor.

**Reachability (rigorous, per site).** The cuttable pool is the *generic*
`draw3d:client` (untagged client-array draws), fed by the **scene_graph** and
**entity_body** sites — *not* the `draw:zone` pool (the zone-merge gathers,
whose BOs are large because the generations are large, and whose draw count is
the u16 65 535-vertex index constraint, not a reducible split). So the relevant
denominator for a scene-graph batch is the generic-d3c draw count (scene_graph
+ entity_body), not the total 3D client-array count. The **scene-graph batch**
removes only the **scene_graph** draws (doors + equipment + overlays from
`smgr->drawAll()`), collapsing that residual to **D_sg draws (one per distinct
material)**; it does *not* remove the `entity_body` crowd-model draws (q2 13/f
— a separate animated path) or the `zone` draws (u16-constrained). The pool cut
is `cut × (pool / generic_draws)`, where `generic_draws = scene_graph +
entity_body`. The census `client.log` door records give the distinct-model
count **measured**, not assumed (steady per-frame, aligned to `point=steady_60s`):

| zone | generic pool | steady draws (sg + ent) | measured distinct models |
|---|---|---|---|
| qeynos2 | 9.28 MiB | 54 (sg 41 + ent 13) | **5** doors/objects: DOOR1×48, BARREL3×16, DOOR2×4, TREE1, TREE6 |
| gfaydark | 10.07 MiB | 120 (sg 120 + ent 0) | **3** door models: FAYDOOR×92, FAYDOOR2A×22, FAYDOOR2×22 (+ trees) |

Varying D_sg (post-batch distinct scene-graph materials = door models measured
above + equipment/overlay materials not yet enumerated):

| zone | D_sg → unnamed (MiB) | | | | | |
|---|---|---|---|---|---|---|
| | D_sg=3 | D_sg=5 | D_sg=7 | D_sg=10 | D_sg=12 | D_sg=15 |
| qeynos2 | **9.36** ✅ | **9.70** ✅ | 10.05 ❌ | 10.56 ❌ | 10.91 ❌ | 11.42 ❌ |
| gfaydark | 8.87 ✅ | 9.04 ✅ | 9.21 ✅ | 9.46 ✅ | 9.63 ✅ | 9.88 ✅ |

**Verdict.** **gfaydark is comfortably reachable** — the residual is 120/f and
collapses to ~15 or fewer draws with margin (9.88 at D_sg=15). **qeynos2 is
reachable only if the residual collapses to ≤ 5 draws** (9.70 at D_sg=5;
10.05, *over*, at D_sg=7) — and the measured 5 in-graph door/object models put
it exactly at the edge, so **any equipment or target-overlay material that adds
a distinct draw pushes q2 over 10 MiB**. The irreducible floor after any batch
is the Mesa floor (5.28 / 3.79 MiB) + the `draw:zone` + `vbo:nametags` pools
(1.33 / 4.83 MiB, named-or-u16-constrained, not batchable here) — already under
10 MiB. The `entity_body` crowd models on qeynos2 (13/f) are a separate site a
door/object batch does not touch; on gfaydark the crowd is all impostor pages
so the scene-graph batch carries the whole cut. **The exact post-batch number
must come from the Pi A/B, not this linear model** — the census shows the lever
reaches the target on gfaydark with margin and on qeynos2 only in the knife-edge
case, and names the one open variable (q2's equipment/overlay material count,
and whether the 13/f entity crowd also needs batching) that a built+measured
batch settles.

**MEASURED (step 4) — this linear model is NOT borne out.** The built+measured
Pi A/B (step 4, below) shows the guard does **not** drop on either zone (q2
17.44→17.16, gd 17.71→18.84 MiB), despite the draw count falling (scene_graph
41→27 / 122→~19 per frame). Two reasons the model missed: (1) D_sg is the
distinct-**material** count the batch actually groups by (measured 14 / 4, not
the distinct-*model* 5 / 3 above), so the linear "cut" overestimates; (2) the
model's core assumption — a flat ~4 KiB varying BO per draw — is wrong: the BO
is **data-proportional** (sized to the vertex payload), so a gathered draw
conserves the byte pool it removes. The model is kept above as the step-2
census record; the step-4 measured result + mechanism is the authoritative one.

### What splits the scene-graph residual (the step-3 target)

From the census `client.log` door records and the `steady_60s` memledger:

- **qeynos2:** 60 doors total, **~29 in-graph at steady** (PVS-culled the rest);
  the census `client.log` door records show **5 distinct in-graph models** —
  DOOR1×48, BARREL3×16, DOOR2×4, plus TREE1/TREE6 (placed trees).
  Placed objects: 478 (fixup pass), 160 trees. Equipment/overlays the rest.
- **gfaydark:** 79 doors total, **~78 in-graph** (nearly all PVS-visible in the
  tour region); **3 distinct door models** — FAYDOOR×92, FAYDOOR2A×22,
  FAYDOOR2×22. Placed objects: **2 829** (2 317 trees) — this is what pushes
  the residual to 120/f. Equipment/overlays the rest.

So the residual is **per-node `CMeshSceneNode` draws** — mostly doors + placed
objects (trees, crates, props) + equipment + target overlays — one `smgr->drawAll()`
draw each, each allocating its own varying BO. The batching opportunity is a
shared per-material client-array batch (gather the visible doors/objects/equipment
of one material into one draw), the same shape as the zone-merge's pooled loop.
This is a **new batch-draw path** (pixel-identical by construction — see step 3
for the pixel channel, since this scene's crowd is not frame-locked and so the
x86 byte-diff cannot be the proof, unlike the zone-merge which had a
deterministic seed), not a buffer-type change.

### Evidence

- `~/kb-evidence/b997-draw-count/BASELINE.md` — step 1.
- `~/kb-evidence/b997-draw-count/x86census-20261009T184539Z/` — x86 census leg
  (surfaceless EGL, `EQT_DRAW_CENSUS=1`, no `WILLEQ_BASELINE`).
- `~/kb-evidence/b997-draw-count/picensus-9f184921-20261009T190536Z/` — Pi
  census leg (census build `9f184921`, lane 2, `--env EQT_DRAW_CENSUS=1`,
  `--bo-census` B1006 shim). `zones/<zone>/client.log` carries `[drawcensus]`
  + `[drawsite]`; `zones/<zone>/gem_owners.tsv` carries the pool tags.
- Census instrument: commit `9f184921` (`draw_census.h` + the 4 call sites).

## Step 3 — reduce draws (built + measured)

The census says the lever is the scene-graph residual (42 / 120 per frame).
Built: a pixel-identical per-material client-array batch of the visible PVS
doors (the scene-graph residual), proven by the x86 determinism probe + the
unit-test construction + the Pi A/B. Zone-merge generation buckets are **not**
reducible "for no visual reason" — the 65 535-vertex u16 index space is the
constraint, not a spurious split.

### The per-material client-array batch (built; step 3 gated it off, the final
leg flips the default ON)

At step 3 `EQT_SG_BATCH=1` (off by default) turned on a `SceneGraphBatch` that
gathers this frame's PVS-visible doors by **full SMaterial equality** and
emits one gathered client-array draw per material group at the SOLID-pre point,
baking each door's world matrix into its `S3DVertex` and drawing with identity
`ETS_WORLD` (the zone-merge's exact primitive, `COpenGLES2Driver.cpp:1484`).
The doors it takes are detached from the scene graph for the frame (the
`clearDoors` grab/remove idiom) so `smgr->drawAll()`'s SOLID list never
re-draws them, then re-attached after the SOLID pass. Opaque-only (a door with
any transparent buffer stays per-node), pixel-identical by construction
(rasterization-order-identical: same material + opaque under the Z-buffer +
the P09c-accepted transform-order reorder). Commit `aa8b8a49` + hygiene
`23e3903b`; unit test `tests/test_scene_graph_batch.cpp` (grouping, the
opaque-only guard, the row-major irrlicht `CMatrix4` transform bake — which
pins a real bug a column-vector bake would have introduced: doors collapse to
the origin). The x86 live-client frame-diff is non-deterministic (0/18 frames
identical across two baseline legs — the live-server crowd is not frame-locked),
so the pixel channel for this scene is the construction argument + the Pi A/B,
the same way the zone-merge itself was blessed (P09/P09c).

**The batch groups by full SMaterial, so the post-batch draw floor is the
distinct-MATERIAL count D_sg, not the distinct-MODEL count** the census
assumed. Measured on the x86 leg (lane 2, imp200far crowd tour, `EQT_SG_BATCH=1`):
qeynos2 28 visible doors → **14** groups; gfaydark 77 visible doors → **4**
groups. (The census's 5 / 3 *model* counts under-predict: a single model
carries several distinct materials — solid vs alpha-test, distinct textures.)

## Step 4 — Pi A/B (the measured result; the guard does NOT drop)

Rig: `scripts/b964/pi_crowd.sh`, same build (head `23e3903b`), qeynos2 +
gfaydark, imp200far, 800×600, cache-on, `--bo-census` shim, `--idle 180
--hold-after 75`, lane 2 (`EQEMU_HOST=172.16.200.98`). Two legs, the only
variable `EQT_SG_BATCH` (0 = the baseline behaviour on this build, 1 = batch on).
Same-build A/B, so every diff is exact.

### draws/frame — the lever worked

Per-frame 3D client-array draws at the guard's sampling point (crowd-in
steady), from the Pi client.log `[drawsite]` + `[sgbatch]`:

| zone | site | SG=0 | SG=1 | Δ | note |
|---|---|---|---|---|---|
| qeynos2 | scene_graph | 41.1/f | 27.1/f | **−14** | doors 28→14 gathers |
| qeynos2 | entity_body | 13.0/f | 13.0/f | 0 | untouched (separate path) |
| qeynos2 | zone_draws | 20.0/f | 20.0/f | 0 | u16-constrained |
| qeynos2 | **win_3d_client** | 74/f | 60/f | **−14** | |
| gfaydark | scene_graph | 122/f | ~19/f | **~−103** | doors 77→4 gathers |
| gfaydark | **win_3d_client** | 160/f | ~57/f | **~−103** | |

Post-crowd (idle) floor, the cleanest cut: qeynos2 scene_graph 29.0→15.0/f,
gfaydark 53.0→6.0/f. **The draw count fell on both zones** — the batch does
what the census predicted for draws. `[sgbatch]` on the Pi (platform-consistent
with x86): q2 `doors=28 groups=14 draws=14`, gd `doors=77 groups=4 draws=4`.

### gpu_unnamed_steady — the guard did NOT drop

| zone | SG=0 (MiB) | SG=1 (MiB) | Δ | guard (<10 MiB) |
|---|---|---|---|---|
| qeynos2 | 17.44 | 17.16 | **−0.28** | RED / RED |
| gfaydark | 17.71 | 18.84 | **+1.13** | RED / RED |

Frame-time is flat / no regression (the card's "report both numbers" clause):

| zone | pp jobs/f (SG=0→1) | wait ms/f | fps_med |
|---|---|---|---|
| qeynos2 | 2.23 → 2.24 | 0.055 → 0.057 | 29 → 30 |
| gfaydark | 2.31 → 2.26 | 0.053 → 0.055 | 24 → 27.5 |

pp jobs (the per-draw fixed cost) fell, wait_ms flat, fps flat-to-better — no
frame-time regression. But **the memory guard is RED on both zones, and the
byte pool did not cut proportionally the draws** (the linear reachability model
predicted −4.64 / −9.73 MiB; measured −0.28 / +1.13).

### The mechanism (why the draw cut doesn't cut the byte pool)

The census's reachability model (step 2) assumed fact 5 — "a fresh ~4 KiB
varying BO per draw" — as a **flat** per-draw cost, so "every draw you remove
removes its varying BO". The measured pool contradicts that: the Mesa varying
BO is **sized to the vertex payload a draw uploads, not flat 4 KiB**. The q2
varying-BO pool (gem_owners `draw3d:client`, steady):

| | SG=0 | SG=1 |
|---|---|---|
| draw3d:client total | 12.39 MiB / 1545 BOs / mean 8.2 KiB | 13.46 MiB / 1524 BOs / mean 9.0 KiB |
| size histogram 4 KiB | ×1377 | ×1380 |
| 8/12/16 KiB | ×335 | ×312 |

So collapsing K same-material doors into **one gather** removes K small
(~4 KiB) door draws and adds **one gather whose varying BO is sized to the
full gathered vertex payload** (larger, 8–12 KiB). The BO *count* falls by the
door share (1545→1524) but the BO *bytes* are conserved — the gather's payload
costs roughly what the K small door draws did. The draw count (and the
per-draw fixed cost: pp jobs, stream-slot churn) falls; `gpu_unnamed_steady`
(the byte pool) does not. The 5.28/3.79 MiB Mesa floor and the named `vbo:`
pools are untouched, as the census said.

**Conclusion.** The scene-graph batch is the right *draw-count* lever (it cut
scene_graph 41→27 and 122→~19 per frame, and pp jobs) but the **10 MiB
`gpu_unnamed_steady` guard is not closed on either zone** — the guard measures
the varying-BO *byte* pool, and a data-proportional gather BO does not shrink
that pool by removing draws. Will's ruling (a) of 2026-10-10 (#317 note 3233)
confirmed this is the terminal state of the card: the guard stays RED as a
documented known condition (below). Buffer-type changes remain out of scope
per the card.

Evidence: `~/kb-evidence/b997-draw-count/` — `pisgbatch-sg0-20261010T003000Z/`
(SG=0 baseline), `pisgbatch-sg1-20261010T004903Z/` (SG=1 batch on), each with
`zones/<zone>/results.tsv` (the columns above) + `client.log` (`[sgbatch]`,
`[drawsite]`) + `gem_owners.tsv` (the varying-BO pool). x86 A/B:
`x86sgbatch-onfixed-20261009T234519Z/` (q2) + `x86sgbatch-gd-20261009T2358Z/`
(gd).

## Known condition (ruling a) — the 10 MiB `gpu_unnamed_steady` guard stays RED

**Will's ruling, option (a), 2026-10-10 (#317 note 3233):** the draw-count
lever is at its ceiling. The 10 MiB `gpu_unnamed_steady` crowd-tour guard
stays **RED on qeynos2 and gfaydark as a documented known condition** — the
same class as the B988/#307 known condition (a guard row that reads red on
the crowd tours by a measured, named mechanism, while the card that owned the
residual closes on its own legitimate-result path). The bound is **not
re-scoped** and **no buffering change is allowed**; the real levers — varying
stride and per-frame vertex volume — are **B1013 #334**.

| zone | SG=0 (MiB) | SG=1 (MiB) | guard (<10 MiB) |
|---|---|---|---|
| qeynos2 | 17.44 | 17.16 | RED / RED |
| gfaydark | 17.71 | 18.84 | RED / RED |

(There is no standalone "known conditions" list in the tree where B988/#307 is
registered: B988/#307 is recorded as its own ruling entry in
`docs/decisions.md` (2026-10-05) and by the B972/B972-FT READMEs, so B997 is
recorded the same way — this section, `docs/decisions.md` (2026-10-10 entry)
and the BOARD entry. Nothing was invented to fit a list that does not exist.)

**Mechanism (why the draw cut cannot close the guard):** cost-sheet fact 5
(`docs/reference/lima_cost_sheet.md`) says `lima_update_varying` allocates a
fresh varying BO per draw — but the Pi A/B measured that the BO is sized to
the **vertex payload a draw uploads** (data-proportional, mean 8–12 KiB in the
q2 steady pool), not a flat ~4 KiB. Collapsing K same-material doors into one
gather removes K small door draws and adds one gather whose varying BO costs
roughly their total, so the BO *count* falls (1545→1524) while the BO *bytes*
are conserved. The draw count and the per-draw fixed cost (pp jobs,
stream-slot churn) do fall; the byte pool does not.

The scene-graph batch itself (`EQT_SG_BATCH`) is a separate, smaller question
than the guard: it cuts draws with zero memory change, so it ships on its own
pixel-identity evidence, not the guard. That evidence is the deterministic
x86 door pixel A/B below (the live-client frame diff is not a channel — 0/18
frames identical across two baseline legs, the crowd is not frame-locked).

## The deterministic x86 door pixel A/B for `EQT_SG_BATCH` (final leg, step 2)

The gate for shipping the batch ON: render the zone's doors through the
production door path at a fixed camera with fixed deterministic lighting,
once with `EQT_SG_BATCH=0` and once with `=1`, and the two frames must be
**byte-identical**.

- Instrument: `--sg-batch-ab` in `gles2_zone_benchmark`
  (`tools/hwcaps/zone_bench_door_batch_ab.{h,cpp}`); one leg per process, the
  leg selected by the `EQT_SG_BATCH` env var the production gate reads
  (`renderer_zone_lifecycle.cpp`). Pinned by the ctest
  `test_sg_batch_door_pixel_ab` (`tests/cmake/zone_integration.cmake`), which
  byte-compares the two PNGs and fails on any difference.
- Determinism: no server, no NPCs, `update(0.0)` (no animation clock); the
  doors-only population (nothing else in the scene graph), a fixed camera
  aimed at the fixture's central cluster (named in the run log), fixed
  lighting + fog zeroed (the SOLID shader's fog would add a per-run state
  variable). Doors are unlit (`Lighting=false` → `vColor = aColor`), so the
  output pixels are a function of texture + transform only.
- Door population: the committed fixtures `refactor/B997/data/doors_<zone>.txt`
  — the eqemu `doors` table rows (version 0, `CHAR_LENGTH(name) > 3`) the
  server serialises into `SpawnDoor`/`Door_Struct`, sourced from the lane-2
  mariadb: **qeynos2 = 70, gfaydark = 102** (the live client's `Received {}
  doors` log line reads 60/79 — the delta is the PVS/hidden split; the A/B
  registers the full fixture and the PVS gate draws the in-graph subset,
  exactly as production does).
- Proof lines per leg: `doors_in_graph` (the drawn population, must match
  between legs and be ≥ 2), `batched_nodes` / `batch_draws` (leg B must have
  actually batched — a vacuous 0-batch match is not a pass).

**MEASURED (2026-10-10, this head): BYTE-IDENTICAL on both zones.**

| zone | in-graph doors | leg A (per-node `drawAll`) | leg B (batch) | PNG compare |
|---|---|---|---|---|
| qeynos2 | 60 | 60 per-node client draws | **59 batched → 19 draws** | byte-identical |
| gfaydark | 78 | 78 per-node client draws | **77 batched → 4 draws** | byte-identical |

Frame content is non-vacuous (the PNGs are not both clear-colour): qeynos2
26,701 / 307,200 non-clear pixels (door textures), gfaydark 3,710 (a
further-out camera on that fixture's spread). Fixed pose: the camera is the
fixture's centroid + (45, 0, 25) EQ, looking at the centroid; 60° FOV, near 1,
far 300; the same pose is both the manual P/V matrices and a real active
camera node (see below). Leg B's one door not batched per zone is the
per-node fallback for a door with a non-S3DVertex or u16-overflow buffer —
the same by-construction exclusion production has.

Two harness facts that matter for the verdict (both fixed, documented here
because a future re-run must reproduce them):

1. **The batch must draw at the SOLID-pre point INSIDE `drawAll()`**, at the
   exact point production's `RenderPassTimer::OnRenderPassPreRender` hook fires
   (after the active camera sets the 3D P/V, before the SOLID pass). The
   harness installs a local `ILightManager` hook doing the same. Drawing the
   batch BEFORE `drawAll()` with the manual matrices is NOT pixel-equivalent:
   `CSceneManager::drawAll()` resets every transform to identity and re-derives
   P/V from the active camera, so a pre-`drawAll` batch and the per-node SOLID
   pass would see different projections.
2. **The per-node leg needs a real active camera node**, for the same reason —
   `drawAll()`'s SOLID pass projects through it. With no active camera the
   baseline leg projects with an identity view and renders a blank frame (the
   first A/B runs caught exactly this: leg A 100% clear, leg B doors — a
   false "not identical" from a harness defect, not a batch defect).

Decision per the card: **byte-identical → the default flips to ON** (batch on
unless `EQT_SG_BATCH=0`). The env read site
(`src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp`), this
README and `docs/decisions.md` (2026-10-10 entry) record the flip; the ctest
above keeps it byte-pinned on every CI run.

### The default-build census leg (x86, this head)

The card's other half of step 2: the x86 crowd census re-run with the
**DEFAULT build, no `EQT_SG_BATCH` env at all** — the shipped configuration.
Same rig as the step-2/3 x86 legs (`x86_census_default.sh`, lane 2,
`EQT_DRAW_CENSUS=1`, imp200far 200-tag tour, `--idle 180 --hold-after 75`,
snapshot restored byte-identically, stack down, lock released). The
`[drawsite]` lines are a rolling 120-frame window (window total ÷ 120 =
per-frame); crowd-phase rows:

| zone | SG=0 leg | SG=1 x86 leg | DEFAULT (no env) | Pi SG=1 (step 4) |
|---|---|---|---|---|
| qeynos2 scene_graph | 41.0 /f (x86 off leg) | 27.0 /f | **27.0 /f** | 27.1 /f |
| gfaydark scene_graph | 122 /f (Pi SG=0, step 4) | 19.0 /f (stable) | **19.0 /f** (stable) | ~19 /f (crowd) |

(Both x86 gd legs settle at 2,280/120 = 19.0/f in their stable crowd
windows; the post-crowd idle floor is 6.0/f in the default leg, matching
the step-4 idle row.) The default build reproduces the SG=1 leg within ±10 %
on both zones (q2 **exactly**: 3,240/120 = 27.0/f in all three legs; gd
stable window 19.0/f in both x86 legs), so the shipped default is the
batched configuration. (`[sgbatch]` in the default leg:
q2 `doors=28 groups=14 draws=14`, gd `doors=77 groups=4 draws=4` — same
collapse as the Pi SG=1 leg.) Evidence:
`~/kb-evidence/b997-draw-count/x86census-default-20261010T133623Z/`.
