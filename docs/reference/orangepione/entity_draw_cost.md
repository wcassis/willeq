# Entity Draw Call Cost Analysis — Orange Pi One (Mali 400)

Benchmarked March 2026 using `gles2_zone_benchmark` on qeynos2 at 1280x720.
All tests with occlusion culling enabled, navmesh camera path, 3000 frames.

## Hardware

- Orange Pi One: Allwinner H3, 4x Cortex-A7, Mali 400 GPU, 512MB RAM
- Display: 1280x720 via DRM/KMS
- Driver: Mesa 25.2.8 Lima, OpenGL ES 2.0
- Textures: ETC1 compressed atlas (5 pages, 148 tiles)

## Zone-Only Baseline

| Metric | Value |
|--------|-------|
| Avg FPS | 85.0 |
| Avg CPU draw | 3.28 ms |
| Avg GPU sync | 8.48 ms |
| Avg draw calls | 47 |
| Avg state changes | 1 |
| Avg polygons | 1161 |

Zone rendering is atlas-batched: all zone geometry shares one texture atlas,
producing a single state change per frame. The GPU floor is ~8.5ms (tile
flush + page flip overhead).

## Entity Scaling (Unbatched — Current Production Behavior)

Each entity uses 5-15 mesh buffers (head, chest, arms, legs, hands, feet),
each with its own texture. No material sorting between entities.

| Entities | FPS | CPU ms | GPU ms | Draws | State Changes | Polys |
|----------|-----|--------|--------|-------|---------------|-------|
| 0 | 85.0 | 3.28 | 8.48 | 47 | 1 | 1161 |
| 20 | 58.0 | 7.17 | 10.07 | 113 | 66 | 3315 |
| 50 | 38.3 | 13.40 | 12.73 | 209 | 162 | 6516 |
| 100 | 23.2 | 24.88 | 18.22 | 372 | 324 | 11845 |

### Per-Entity Cost (Unbatched)

- Draw calls per entity: **3.25**
- State changes per entity: **3.24**
- CPU cost per entity: **0.22 ms**
- GPU cost per entity: **0.10 ms**
- Polygons per entity: **107**

CPU is the bottleneck. The GPU handles 12K polys easily; the 324 state
changes (glBindTexture per body part) consume the CPU budget.

## Draw Call Overhead

Measured via synthetic sweep and confirmed by entity scaling:

- **~92 μs per draw call** (CPU side, Irrlicht `setMaterial` + `drawMeshBuffer`)
- Includes: material comparison, GL state validation, VBO bind, uniform upload
- GPU cost per draw is minimal for small meshes (~107 polys)

For comparison, a 1999 hand-rolled GL engine would be 5-10μs per draw call.
Irrlicht's abstraction layer adds ~10x overhead per call.

## Batched Entities (Same-Material Merging)

Groups visible entity mesh buffers by material key (texture + shader),
merges vertices with baked world transforms into shared VBOs, one
`drawVertexPrimitiveList` per material bucket.

| Entities | FPS | CPU ms | GPU ms | Draws | State Changes |
|----------|-----|--------|--------|-------|---------------|
| 100 unbatched | 23.2 | 24.88 | 18.22 | 372 | 324 |
| 100 batched | 35.4 | 13.30 | 14.93 | 166 | 120 |

- **+53% FPS**, -46% CPU, -55% draws, -63% state changes
- 120 remaining state changes = 120 unique entity textures across 12 race models
- Each race has ~10 unique body-part textures; batching can't reduce below this

## Composite Entities (1 Draw Per Entity)

Simulates the original 1999 EQ client approach: each entity rendered as a
single draw call with one composite texture covering the entire body.
All mesh buffers merged per entity, one material.

| Entities | FPS | CPU ms | GPU ms | Draws | State Changes |
|----------|-----|--------|--------|-------|---------------|
| 100 unbatched | 23.2 | 24.88 | 18.22 | 372 | 324 |
| 100 batched | 35.4 | 13.30 | 14.93 | 166 | 120 |
| 100 composite | 46.0 | 7.50 | 14.25 | 72 | 26 |

- **+98% FPS** vs unbatched, +30% vs batched
- 26 state changes (zone atlas + ~25 visible entity texture switches)
- Per-entity cost: **42 μs CPU** — less than half the 92μs/draw overhead
  because entity meshes are small

## Raid Scenario Projections

Based on composite entity costs:

| Scenario | Entities | Est. Draws | Est. CPU | Est. FPS |
|----------|----------|------------|----------|----------|
| Solo | 5 | 52 | 3.5 ms | 82 |
| Group camp | 20 | 57 | 4.1 ms | 75 |
| Busy area | 40 | 67 | 5.0 ms | 65 |
| Full raid | 72 | 87 | 6.3 ms | 55 |
| Max visible | 100 | 97 | 7.5 ms | 46 |

All projections assume composite entity textures. Without them, a 72-person
raid would hit ~18 FPS (unbatched) or ~28 FPS (batched).

## Key Findings

1. **Entity rendering is draw-call bound, not polygon bound.** The Mali 400
   handles 12K polys in 14ms GPU time regardless of draw call count. The CPU
   cost of submitting 372 draws (25ms) is the bottleneck.

2. **State changes are the dominant CPU cost.** Each `glBindTexture` +
   `glUseProgram` switch costs ~92μs. Reducing state changes via material
   sorting, batching, or composite textures directly reduces CPU time.

3. **Composite entity textures are the highest-priority optimization.**
   Pre-baking all body-part textures into one texture sheet per
   race/appearance collapses entity rendering from 5-15 draws to 1 draw
   per entity. This alone recovers enough budget for 72-person raids at
   55+ FPS.

4. **The 1999 EQ client used composite textures.** Each character model was
   UV-mapped to a single texture sheet. The per-body-part multi-texture
   approach in the current WLD loader is an artifact of how the S3D file
   stores model data, not how the original client rendered it.

5. **Batching (same-material merging) helps but hits a floor.** With 120
   unique entity textures across 12 race models, batching reduces draws
   from 372 to 166 but can't go lower without texture consolidation.

## Benchmark Tool

All measurements taken with `gles2_zone_benchmark`:

```bash
# Zone baseline
./gles2_zone_benchmark --drm --zone qeynos2 --eq-path /path/to/EQ \
    --atlas-path /path/to/atlas --navmesh-path /path/to/nav \
    --res 1280 720 --frames 3000 --occlusion-cull

# Entity sweep (add --entities N)
... --entities 100

# Batched entities
... --entities 100 --batch-entities

# Composite entities (1 draw per entity)
... --entities 100 --composite-entities
```
