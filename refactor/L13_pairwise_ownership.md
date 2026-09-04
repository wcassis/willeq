# L13 — Pairwise ownership: the two-component members move to the component that owns them (B050 clusters 1, 2, 5; the type hoists of cluster 3)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–53).
**Runs ALONE among renderer units.** Ruling (orchestrator, 2026-09-04): the
promotion threshold is NOT lowered to ≥2 — a 160-member `RendererContext`
renames the god class. Instead Batch K's §3 rule applies pairwise: **a
member used by two components moves to the one that WRITES it (or, if
both write, the one with more uses), and the other reaches it through
`ctx_.<Owner>().accessor()`.** That is what shrinks `irrlicht_renderer.h`
— L12's promotions left every declaration in place.

## Inputs (B050 at `58628347`)

- Cluster 1, the frame-timing blob (`frameTimings_` 200, `frameTimingsAccum_`
  135, `frameTimingsSampleCount_` 11, `lastPolygonCount_` 6, `currentFps_` 4):
  owner = `FrameComponent` (it accumulates); `DiagnosticsComponent` and
  `ZoneLifecycleComponent` read via `ctx_.Frame().timings()` (a const ref
  accessor) — measure who writes what first; `frameTimings_` is typed by
  a nested type (cluster 3) → hoist `FrameTimings` to `renderer_types.h`
  with a `using` alias in the class (L05 correction 22 shape).
- Cluster 2, 55 two-family members (`playerConfig_` 56 — player 46 / frame
  10 → player; `storedZoneEnvironment_` 37; `simulationWorker_` 26 — ⚠ the
  worker handle: NO ownership change, it stays on the renderer (L07 STOP);
  `cameraMode_` …): one decision line per member in the Review, then the
  move. Any member whose two users are on different THREADS stays (list
  them).
- Cluster 5, 16 single-component members still on the renderer (`sunLight_`
  16, `pendingGPUResults_` 6, `zoneLightsEnabled_` 5, …): move to their one
  component.
- Cluster 3 type hoists: `FireGlowLight`, `ZoneLightData` (⚠ two other
  same-named structs in the tree — namespace it or rename to
  `RendererZoneLightData`; measure the ambiguity), `RenderPassTimer` (a
  nested CLASS with a virtual base — hoist to its own header
  `render_pass_timer.h`), `DoorCollisionData`. Then the four members they
  type follow the pairwise rule.
- Clusters 4 and 6: reasoned keeps; untouched.

## Method

Per member: measure writers/readers per component (comment- and
string-filtered), decide owner, move the declaration into the owner's
header (private, with the smallest accessor the other side needs —
prefer `const T&` getters; a setter only where the other side writes),
rewrite the other side's `owner_.x_` to `ctx_.<Owner>().x()`, rewrite the
renderer's own remaining uses (init/shutdown/teardown) the same way.
Scripted, code lines only, multiset proofs per moved declaration block
are not meaningful here — the proof is: **zero behaviour change by test**,
the census before/after, and the header count.

Predicted: `irrlicht_renderer.h` 1,556 → **≈ 1,350 or better** (≈ 75
member declarations + their banners leave); `owner_.` 1,065 → **≈ 250**.
STOP: any thread/sync change; a member with cross-thread readers; a
fixture's observable changes; the visual gate diffs a golden.

## Territory

Every component header/TU under `src/client/graphics/<family>/` and
`include/client/graphics/<family>/`, `irrlicht_renderer.h`,
`renderer_context.h`, `renderer_types.h` (the hoists), `render_pass_timer.h`
(new), `src/client/graphics/renderer/*.cpp` (init/teardown call sites),
the renderer tests' call sites (assertions untouched; text guards widened
+ re-proved), allowlist, `docs/module_map.md`, `refactor/BOARD.md` (B050
re-measured). Visual gate + ARM NOT skippable.
