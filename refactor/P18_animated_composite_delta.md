# P18 — Root-cause the animated-composite whole-silhouette rendering delta

Pre-authorized batch (Will, 2026-07-27). Slot: wt/slot2, branch
task/P18-animated-composite-delta. x86-ONLY (no Pi, no server).
Protocol: refactor/EXECUTOR_PROTOCOL.md. Parallel: P17 (slot1,
production zone/eviction files), P19 (slot3, Pi runs) — benchmark
CODE edits are yours if needed, keep tight.

## Motivation

P12b measured composite vs multi-buffer UNDER ANIMATION: pose/
silhouette identical but 17-29% of pixels differ at LOW magnitude
(mean ~0.1-2/255). P16's seam fix did NOT move it — inspection shows
the diff spans nearly the WHOLE model silhouette, not a seam. Static
renders are pixel-identical. So something in the merged-single-buffer
ANIMATED path shades slightly differently.

Suspects (check in order):
1. **Lighting/material state on the merged buffer**:
   buildCompositeMeshFromGeometry sets its own SMaterial (note
   Material.Lighting=false and EMT_SOLID) — diff against the
   multi-buffer path's per-part materials (lighting flags, material
   type, vertex-color usage).
2. **Normals**: are normals carried/recomputed identically through the
   merged path during applyAnimation (per-vertex normal handling in
   the instance-mesh skinning)?
3. Vertex color/alpha attribute divergence in the merge.

## Design

1. Reproduce with the P12b aligned method (canonical replay frames);
   confirm the magnitude/extent baseline.
2. Bisect by state: force-match one variable at a time (e.g. override
   the composite material's lighting flags to match multi-buffer's;
   compare normals numerically on a sampled vertex set between paths
   at the same anim frame).
3. Fix in the composite mesh-build path (production file —
   animation-adjacent; P17 is in zone/eviction files, no overlap);
   bar: animated composite vs multi-buffer diff drops to the FP class
   (scattered singles) or a quantified justified residual; static
   stays pixel-identical; standard bar.
4. STOP: unattributed after the suspect list + 2 focused bisections →
   narrowed evidence.

## Review

**Outcome: STOP with narrowed evidence** — root cause not fixed this pass. No
production code changes are committed by this unit; two experiments were run,
found either inconclusive-or-regressive, and reverted (verified via `git diff`
showing byte-identical files to the `fable` baseline before build/commit).

### Reproduction

Reproduced P16's Z35 method exactly: `build-gles2/bin/gles2_zone_benchmark
--zone qeynos2 --eq-path /mnt/projects/EverQuestP1999 --entities 6 --seed 42
--animate --path-file benchmarks/paths/qeynos2_seed42_1000f.path --frames 40
--warmup 5 --res 1024 768`, composite (`--composite-scenegraph --eqe1-path
/mnt/projects/EverQuest_ETC1`) vs baseline (same, no composite flags), diffed
with the same differing_pct/mean_abs_delta/max_channel_delta method as P16's
`p16_z35_seam_diff.json`. Confirmed the P16 baseline numbers are current
(17.2%/29.3%/26.0% differing_pct at frames 15/25/35, mean deltas 0.1-2/255,
but **max_channel_delta is 182-224/255** — i.e. the "low magnitude" framing in
the plan's motivation describes the *mean*, not the *max*; a meaningful
minority of pixels differ by a lot, which reframed the search away from pure
FP-noise theories toward something structural).

### Suspect 1 — merged-buffer material state

`buildCompositeMeshFromGeometry` sets `Material.Lighting=false` explicitly;
`buildMeshFromGeometry` (multi-buffer) sets the same for textured buffers.
Traced the runtime path further: `EntityRenderer::createEntity`
(`entity_renderer.cpp` ~line 599-618) **unconditionally overrides**
`mat.Lighting = false` on every material of every animated entity whenever a
GLSL shader is active (`shaderMaterialSolid_ >= 0`, true for both composite
and multi-buffer in this benchmark) — so the per-buffer `Lighting` flag set by
either mesh builder is dead code at runtime; it cannot be the divergence
source. **Ruled out.**

Found one *real* asymmetry while auditing material state: `buildCompositeMeshFromGeometry`
explicitly sets `TextureWrapU/V = ETC_CLAMP_TO_EDGE` (P16's tile-cap design
requires it — tiling is pre-baked into physical sheet copies + a CPU-side
`fmod`, so the GPU must never wrap). `buildMeshFromGeometry` (multi-buffer)
leaves Irrlicht's default, which is `ETC_REPEAT` (confirmed in
`third_party/irrlicht-gles2-x86/include/SMaterialLayer.h`). Hypothesis: REPEAT
wrap pollutes every mip level >0 with wraparound-blended border texels (a real
OpenGL mipmap-generation behavior), producing a subtle whole-texture color
shift that only shows once a triangle is minified enough to sample a coarser
mip — i.e. camera-distance/animation dependent, invisible in a fixed
close-up static shot, which would explain both "static is pixel-identical"
and "affects the whole silhouette under animation."

**Tested and reverted**: patched `buildMeshFromGeometry` to scan each
buffer's actual UV extent (mirroring the composite builder's own
`tilesU`/`tilesV` logic) and only force `CLAMP_TO_EDGE` where the geometry's
UVs stay inside `[0,1]` (leaving `REPEAT` for buffers whose UVs genuinely tile
beyond `[0,1]`, matching the composite builder's own tiling detection).
Result: **regression, not a fix** — differing_pct went from 17.2/29.3/26.0%
to **92.5/82.9/45.2%** at frames 15/25/35 (mean/max deltas also moved,
recorded in the scratch dir, not committed). This means wrap-mode changes
have an outsized effect in this renderer, in a direction inconsistent with
the mip-pollution theory being the dominant term — plausibly because
`ITexture*` objects are shared/cached across many mesh buffers (skin/cloth
textures reused across body parts and even across entities via
`meshBuilder_`'s cache), and GL texture wrap parameters are a property of the
**texture object**, not the draw call; if the renderer's state-caching layer
skips reapplying `glTexParameteri` when the bound texture hasn't changed
(plausible given the documented "state dedup" pattern for program/texture
binds in `src/client/graphics/CLAUDE.md`), two buffers sharing one texture
but wanting different wrap modes would silently stomp on each other in
draw-order-dependent ways — which would explain a large, order-sensitive
regression far bigger than a mip-blend artifact. **Change reverted** (`git
diff` confirms `mesh_building.cpp` is back to the `fable` baseline); the
underlying wrap-mode/texture-sharing interaction is flagged, not fixed —
diagnosing it needs instrumenting the GLES2 driver's texture-param cache,
out of this unit's remaining budget.

### Suspect 2 — normals through instance-mesh skinning

Built a direct, empirical test rather than reasoning from code alone: added a
temporary env-var-gated diagnostic to `EQAnimatedMeshSceneNode::applyAnimation()`
that dumps Pos/Normal for the first 60 ORIGINAL vertex indices (keyed by
original vidx via `vertexMapping`, so composite's 1-buffer and multi-buffer's
13-buffer layouts are directly comparable) the first time a node reaches a
chosen skeletal animation frame. Ran the benchmark twice — once with
`--composite-scenegraph`, once without — same `--path-file`/seed/entities, same
target frame, `--test-races 1` (human). **Result: every dumped vertex's
Pos.X/Y/Z and Normal.X/Y/Z is bit-identical between the two runs** (only
`bufferCount` differs: 13 vs 1, confirming the two runs really did take
different code paths). This directly confirms `applyBoneTransformsToMeshImpl`
(the shared transform helper both `EQAnimatedMesh` and `EQAnimatedMeshSceneNode`
call) produces identical per-vertex output regardless of buffer layout —
consistent with reading the code, which computes position/normal from
`originalVertices`/`vertexMapping` (both populated identically, in original
geometry-vertex order, by both mesh builders) with no buffer-count-dependent
branch in the math itself. **Ruled out** — diagnostic reverted (`git diff`
confirms `animated_mesh_scene_node.cpp` is back to the `fable` baseline).

### Suspect 3 — vertex color/alpha divergence

Code inspection: both `buildMeshFromGeometry` and `buildCompositeMeshFromGeometry`
hardcode `vert.Color = irr::video::SColor(255, 255, 255, 255)` unconditionally
for every vertex — no per-race/per-texture tint, no divergence possible.
**Ruled out** by inspection; not worth a runtime bisection given suspects 1-2
already consumed the unit's experimental budget.

### STOP rationale (per plan's own condition)

Suspect list exhausted (1 ruled out on the Lighting flag, tested-and-reverted
on wrap mode; 2 disproven empirically; 3 ruled out by inspection) plus 2
focused runtime bisections (wrap-mode patch, vertex-dump diagnostic) — per the
plan's STOP condition ("unattributed after the suspect list + 2 focused
bisections → narrowed evidence"), reporting narrowed evidence rather than
continuing to guess-and-check. **Narrowed direction for a follow-up unit**:
the wrap-mode experiment's disproportionate, regression-direction effect
points at texture-object state sharing/caching in the GLES2 driver
(`COGLES2Texture`/`COpenGLES2Driver`) as the most promising next lead —
specifically whether `glTexParameteri` (wrap mode) is reliably reapplied
per-draw-call when two `SMaterial`s share one underlying `ITexture*` but
request different wrap modes, and whether the composite path's single-buffer,
single-material-per-entity draw pattern vs multi-buffer's many-buffers/many-materials
(often sharing cached textures) pattern hits this differently. This was not
verified further (would require instrumenting the GLES2 driver directly,
outside this unit's file territory and budget) but is a concrete, falsifiable
next step, distinct from all three suspects in the original list.

### Verification

- No production code changes remain (both experiments reverted; `git diff`
  against `fable` for `src/client/graphics/eq/mesh_building.cpp` and
  `src/client/graphics/eq/animated_mesh_scene_node.cpp` is empty).
- `cmake --build build -j24`: clean, confirms the reverted tree still builds
  (ctest/arm-build/file-size steps skipped as moot — no code change to
  verify; the plan's verification bar is scoped to a fix being delivered).

### Deviations from the plan

- Delivered narrowed evidence per the plan's own STOP condition instead of a
  fix — the design's step 3 ("fix in the composite mesh-build path") was not
  reached with a working fix; the one fix attempted was measured to regress
  and was reverted rather than landed.
