# B938 — placed objects keep a compact per-placement record instead of a full scene node

Issue #256. After B936 (#254) shares one mesh per object geometry, the largest
`objects` owner was the per-PLACEMENT state: the Irrlicht `IMeshSceneNode` each
placement got (transform, absolute matrix, name string, children lists, a
copied `SMaterial` per mesh buffer) plus the renderer's four parallel
per-object vectors. `obj.instances` held about 2.2 KB per placement.

This unit replaces it with `PlacedObjectRecord`
(`include/client/graphics/placed_object_meshes.h`, **168 B**, pinned by
`tests/test_placed_object_record.cpp`), read by BOTH draw paths. The node is
allocated only in the rare no-PVS zone, where the scene-graph pass still needs
it; in every PVS zone (all gate zones, all ledger-measured zones) the record
holds the sole mesh reference and the node is null.

## What changed

- `PlacedObjectRecord` is the ONLY per-placement state: shared mesh pointer,
  world matrix, position, BSP region, world-space box, scene-graph flag, name,
  and the optional node handle. The four renderer-parallel vectors
  (`objectPositions_` / `objectRegions_` / `objectBoundingBoxes_` /
  `objectInSceneGraph_`) are deleted from `RendererContext` and the class; the
  data lives in the record, indexed by placement.
- Load step 6 builds the record with the record's `world` matrix computed by
  the EXACT idiom the node used to get its relative transform
  (`setRotationDegrees` + `setTranslation`, then `*= scale` only when the scale
  is not (1,1,1)), so the manual draw path reads byte-identical bytes.
- Reference accounting: PVS zones `objMesh->grab()` into the record (dropped
  in zone teardown); non-PVS zones the node holds the mesh reference and the
  record holds a node reference. Teardown drops exactly the one the record
  holds.
- Consumers read the record: manual draw path (`renderer_zone_draw.cpp`), sim
  bridge snapshot + visibility apply (`renderer_sim_bridge.cpp`), diagnostics
  (memory report, scene dump, wireframe/fog/lighting/shader-swap toggles,
  manual-zone-draw detach), the tree-wind fixup and wind material swaps
  (`renderer_zone_load_env.cpp`), and the texture-eviction guard
  (`test_object_node_texture_guard.cpp` now covers the node-less PVS shape).
- New `tests/test_placed_object_record.cpp` pins `sizeof == 168` and
  `offsetof(node) == 160`, the per-placement culling field types, the
  raw-pointer lifetime invariants, movability and the defaults.

## Per-pixel (acceptance 2)

Rendered frames are unchanged. The only draw-path change is the source of the
world matrix and the mesh pointer, and both are byte-identical to the old
node's `getRelativeTransformation()` / `getMesh()` (same three-setter idiom,
same shared mesh). Verified by the CI visual gate, which diffs decoded pixels
against the pre-B938 goldens at **exact-zero-diff**:

| gate | zones | result | pipeline |
|---|---|---|---|
| `visual_gate` | qeynos2, gfaydark, befallen, iceclad, timorous | **PASS, 0 pixel mismatch** | #526 (SHA 3153348f) |

The benchmark's own `--objects` fixed-pose runs are NOT the parity evidence for
this unit: `gles2_zone_benchmark --objects` draws objects through the tool's
own `ZoneBench` loader, which never constructs the client's `IrrlichtRenderer`
object code B938 changes (`zone_bench_memory.cpp:4`: "This benchmark tool
never constructs an IrrlichtRenderer"). That run was B936's parity instrument
for B936's own benchmark change. For B938 the gate (the client's own rendered
frames, real zone load, manual PVS draw path) is the per-pixel comparison.

## CI test pinning per-placement bytes (acceptance 3)

`tests/test_placed_object_record.cpp` (ctest `test_placed_object_record`,
registered in `tests/cmake/world_geometry.cmake`):

- `sizeof(PlacedObjectRecord) == 168` (static_assert + EXPECT_EQ);
- `offsetof(PlacedObjectRecord, node) == 160`;
- field types: `mesh`/`node` raw pointers, `position` `vector3df`, `region`
  `size_t`, `box` `aabbox3df`, `inSceneGraph` `bool`;
- move-constructible (the load path's `push_back(std::move(record))` idiom);
- defaults are the unloaded state.

A growth of the struct is a red test, not a drift. On origin/fable the test
does not exist (RED there by absence); the branch adds it.

## x86 measured (acceptance 1, x86 half)

`scripts/client_ledger_capture.sh` (eqemu VM lane, Release build, GLES2
llvmpipe, `WILLEQ_BASELINE=1`, `WILLEQ_LEDGER_OWNERS=objects`,
`--hold 75`, level 60), head 3153348f. Evidence:
`~/kb-evidence/b938-placement-records/x86-ledger/` (`run_meta.txt`,
`<zone>/objects/client.log`); the CI `client-ledger-live` job re-runs the same
rig. `steady_point` rows, `owner=startup.obj.instances`:

| zone | placements | `obj.instances` B936 (x86) | `obj.instances` B938 | per placement B936 → B938 | scene-graph nodes B938 |
|---|---:|---:|---:|---:|---:|
| growthplane | 4,333 | 9,606,528 | 3,266,208 (6,447 blocks) | 2,217 → 754 B | 10 |
| trakanon | 2,758 | 6,502,704 | 2,179,600 (4,746 blocks) | 2,358 → 790 B | 26 |
| overthere | 2,879 | 6,735,264 | 2,250,848 (5,042 blocks) | 2,339 → 782 B | 13 |
| gfaydark | 2,829 | 5,053,568 | 777,936 (286 blocks) | 1,786 → 275 B | 180 |

The per-placement node is gone from the scene graph (growthplane: 4,333 nodes
→ **10 total** scene-graph nodes; the dump lists the camera, sun, sky and
object lights only). The residual in the `obj.instances` row is NOT per-
placement state: the row also charges the per-object **light nodes**
(`ILightSceneNode`, created for every torch/lantern/lamp placement — 107 in
qeynos2 alone, unchanged by this unit and present in B936's baseline row too,
which B936's README describes as "the scene node, its transform and material
copies, **the object light** and the renderer's per-object vectors"). The
per-placement record itself is 168 B (the CI pin), and in a PVS zone the
record holds the sole mesh reference — no node at all. `obj.instances` fell
6.3 MB in growthplane (88 % of the 9.7 MB `objects` category before, 97 % of
its reduction), 4.3–4.5 MB in the other three.

(Per-placement arithmetic on the raw row is therefore a lower bound on the
record's own size only once the light nodes are separated out; the record size
is pinned directly by the test, which is the acceptance-3 instrument.)

## Pi measured (acceptance 1, Pi half)

`scripts/pi_rendered_zone_sweep.sh --allow-vm-lane` on the CI ARM publish
(`ci-arm/3153348f…`, `willeq-nollvm`, Mesa nollvm prefix), `WILLEQ_BASELINE=1`,
`WILLEQ_LEDGER_OWNERS=objects`, default 240 s hold, level 60. Evidence:
`~/kb-evidence/b938-placement-records/pi-ledger2/`. The sweep ended with the
snapshot restored byte-identically, the lane down and both locks released
("locks released; eqemu lock now: ''; pi lock now: ''", sweep.log 01:46:42).

`obj.instances` B938 is the `[memledger-startup] owner=startup.obj.instances`
row at `steady_60s` in each zone's `client.log`; the B936 column is B936's Pi
A/B leg-B owner rows (B936 README "Orange Pi A/B"). `objects` and owned
(RssAnon − `gpu_driver`) are the client's own `[memledger]` steady row.

| zone | `obj.instances` B936 (Pi) | `obj.instances` B938 (Pi) | `objects` B938 | owned (RssAnon − gpu_driver) B938 | verdict |
|---|---:|---:|---:|---:|---|
| growthplane | 6,590,544 | 2,657,984 (6,447 blocks) | 2,782,680 | 68.8 MB | pass |
| trakanon | 4,467,192 | 1,762,472 (4,746 blocks) | 2,780,200 | 70.9 MB | fail — `gpu_unnamed_over_bound` (18.07 MiB > 10.00), the known condition of B988/#307 (Lima BO cache ON by default); the zone reached 15/15, steady_60s, 60 fps |
| gfaydark | 3,331,272 | 648,792 (286 blocks) | 974,352 | 58.1 MB | pass |
| overthere | (B936: not reached) | — | — | — | fail — no `ready` (phase 11/15, 300 s cap), on both attempts; the B940 known condition |

`obj.instances` fell 3.9 MB in growthplane, 2.7 MB in trakanon and 2.7 MB in
gfaydark — the same per-placement node that vanished on x86. The trakanon
row's verdict is the known B988/#307 condition (unnamed-GPU GEM over its
10 MiB bound with the BO cache on); it is NOT a B938 failure and the guard is
untouched. The block counts match the x86 run's block counts exactly
(6,447 / 4,746 / 286), as expected: the allocation count is platform-
independent (the record's own size is the pinned 168 B, and the row's block
count is the placement count plus the light nodes).

## Tests

- `test_placed_object_record` (new): 7/7.
- `test_object_node_texture_guard`: 3/3 (now covers the node-less PVS shape).
- `test_placed_object_meshes`: 7/7. `test_object_collision_set` (B909b): 8/8.
  `test_sim_bridge_component`: 9/9.
- Full ctest suite on CI: **5,195 tests, 0 failures** (54 skips = missing
  captures/assets), hygiene label green, gate green — pipeline #526.
- Collision (B909b) structures and shared animated meshes (B936) keep working:
  their tests are green in the suite, and the gate renders zones that use both.

## Not verified

- **The per-object light nodes.** They are the residual in the `obj.instances`
  row (a per-placement light node each, ~KB-scale). B938 does not touch them
  (they are a separate owner concern), and a "per placement" figure derived
  from the raw row therefore includes them. Separating them (a distinct owner
  row, or a compact record) is future work, not claimed here.
- **Non-PVS zones on x86/Pi.** Every zone in the gate manifest and in the
  ledger is a PVS zone (manual draw enabled); the node-carrying record shape
  is exercised by the unit tests and the build, not by a rendered zone (there
  is no shipped no-PVS zone to render).
- **Pi steady rows for all four zones.** overthere does not reach `ready` on
  the Pi (phase 11/15, 300 s cap, both the first attempt and the retry — the
  B940 known condition), so the Pi table carries it as a no-row, exactly as
  B936's Pi table did.
- **The trakanon `objects`/owned B938-vs-B936 comparison** is drawn from two
  different rigs (B936's A/B collision rig with its uinput key sequence
  versus this sweep's default tour), so only the `obj.instances` owner row —
  the one this unit moves — is compared directly; the `objects`/owned columns
  are B938's own readings, not a delta.
