# B962 -- every GEM buffer on an owner row; the 2x explained and fixed

GitLab #280 (class A). Branch `task/B962-gpu-buffer-attribution`. Raw evidence:
`/mnt/projects/fable/review/B962/` (its README says what each directory is).
Read `refactor/B955/README.md` first: it built the `gpu_buffers` row this unit
decomposes. No budget, golden, baseline or gate assertion changed.

## 1. The tool: a per-owner GEM census

- `scripts/pi/lima_bo_census.c` (LD_PRELOAD, diagnostic only) keeps every live
  Lima GEM buffer in a fixed in-process table: size, heap flag, whether Mesa
  mapped it (`DRM_IOCTL_LIMA_GEM_INFO` offset matched to `mmap64`/`munmap`),
  the OWNER TAG the creating thread had pushed and the memory-ledger SCOPE open
  on it. `B962_GEMDUMP=<file>`: the client asks it to dump the table at every
  `recordPoint()`. The event log (`B955_BOLOG`) now starts with a
  `# clock realtime= monotonic=` line, so a binary without the dump call can be
  folded at its `[memgpu]` points too (`gem_owner_table.py --events`).
- Tags: `include/client/core/gem_owner_census.h` (`eqt::memory::GemTag`,
  header-only, dlsym once; one null check without the shim) in the client, and
  `irr::video::GemTagScope` in `COpenGLES2Driver.h` around every driver entry
  point that can make Mesa allocate (texture storage, RTT, buffer data, every
  draw that streams client arrays, clear, present, shader compile). The zone
  draw is `draw:zone`, the EGL device `egl:device`, the upload thread's
  context `egl:upload-thread-context`.
- `scripts/pi/gem_owner_table.py DUMP --client-log LOG` folds a dump into the
  owner table and reconciles it with the `[memgpu]` row at the same point.
- `scripts/pi_rendered_zone_sweep.sh --bo-census SHIM.so` runs an UNCAPPED
  client under the shim (secure execution drops LD_PRELOAD) with
  `net.core.rmem_max` raised to the 1 MiB `SO_RCVBUFFORCE` would force (on this
  Pi it already was), restored at cleanup; the dump lands in each zone's dir.

## 2. What the 2x was (qeynos2 `steady_60s`, tagged census, MiB)

Before = the same binary with `WILLEQ_GPU_BUFFERS_LEGACY=1`
(`include/client/graphics/gpu_buffers_legacy.h`), which restores all three
pre-fix behaviours; it reproduces the fable-tip binary's figures
(`pi_before_fable`, untagged event census: 36.70 vs 36.94).

| owner | before | after |
|---|---|---|
| textures: EQE1 ETC1 main | 18.25 (577 BOs) | 7.78 (287) |
| textures: EQE1 ETC1 alpha companions | 7.39 (259) | 4.25 (128) |
| textures: `ui_atlas` (v2 RGBA8888) | 0.34 | 0.25 |
| textures: RGBA (font, cursor, placeholder), light LUT, two 1x1 fallbacks | 0.09 | 0.09 |
| window surface depth/stencil (1280x720 D24S8) | 3.52 | 3.52 |
| Mesa stream uploader, 1 MiB buffers (client-array draws: 2D UI, entities, zone pools) | 2.00 | 2.00 |
| untagged raw-GL draws (scope `irrlicht_scene`: 1 MiB stream buffer + varyings; not decomposed) | 1.30 | 1.34 |
| Lima draw-time BOs (varying outputs in flight, shader binaries, PP streams) | 1.24 | 0.90 |
| Lima PLB + small, context 1 | 0.51 | 0.51 |
| Lima per-flush (PP stream cache) | 0.12 | 0.12 |
| GPU upload thread's second context (PLB, streams; + 2 tile-heap BOs) | 0.50 | -- |
| its VBO for the duplicate player region | 0.02 | -- |
| **sum of regular BOs** | **35.28** | **20.75** |
| GP tile heaps (16 MiB maximum each; pages grow on demand) | 4 BOs | 2 BOs |
| residual = gpu_buffers - regular BOs (frame dumps 1.53 + heap pages) | 1.66 | 1.60 |
| **`[memgpu]` gpu_buffers (memcg shmem)** | **36.94** | **22.35** |

Every byte is on a row; the residual is the rig's own frame dumps (named by
the summarizer) plus the heaps' grown pages (~0.1 MiB).

### The defect: two GEM buffers per texture (willeq's)

`ConstrainedTextureCache::createETC1Storage()` uploaded each EQE1 texture's mip
chain with the default `GL_NEAREST_MIPMAP_LINEAR` filter in place and set the
real filter afterwards. Mesa's `allocate_full_mipmap()` treats that default as
"no mipmaps", so it allocates LEVEL 0 ALONE (256x256 ETC1: 32,768 B), puts each
further level in a buffer of its own, and at first use allocates the full
chain (45,056 B) and copies -- the level-0-only buffer stays. Measured: 401 of
436 qeynos2 textures held two buffers (176 of the 256x256 ones exactly
32,768 + 45,056). And the MAIN texture's levels 1..n are never read at all:
`COpenGLES2Driver::applyMaterialRenderState()` binds unit 0 with plain
`GL_LINEAR`/`GL_NEAREST` on every draw (T275), and the 2D path keeps the
`GL_LINEAR` the upload set.

Fix: the main texture sets `GL_LINEAR` first and uploads level 0 only; the
alpha companion sets its `GL_NEAREST_MIPMAP_NEAREST` (mip-complete) or
`GL_NEAREST` FIRST, so Mesa allocates the chain once (or level 0 only for a
short chain, which `GL_NEAREST` samples at level 0 only). Same for
`createV2Storage()`. One buffer per texture now (0 of 416 with more). The
B944 restore path builds through the same function. Pixels: unit 0 never
samples above level 0 (Lima: `PIPE_TEX_MIPFILTER_NONE` -> `max_lod = min_lod`),
the companion keeps its full chain.

### The other willeq defects

- **The player region built twice in pooled mode.** `setupMinimalZoneCollision()`
  rebuilt the player's BSP region as a scene node although the T504 pools
  already hold it (drawn from them, collision read from them): an undrawn
  mesh, an animated-texture registration and a VBO. That VBO was the 1-7 KB of
  `logical_buffers` at `steady_60s`. Skipped when the pools are on
  (`ZoneLifecycleComponent::zonePoolsEnabled()`, now the one predicate Step 4
  uses too).
- **A second GL context for nothing.** The GPU upload thread's shared EGL
  context (created right after shader compile) exists only for the legacy
  per-region path's VBO uploads -- with pools on, its only work was the
  duplicate region above. Its Lima context cost 0.50 MiB of PLB/streams plus
  two tile-heap BOs. Not created when the pools are on.

### Tried and reverted: the pools in one shared static VBO

The obvious reading of "zone geometry is not in hardware buffers" was to put
the pools' vertices in one static VBO (suballocated, one GEM buffer). Measured
(`pi_census_fixed`, `pi_fixed`, binary `f26b59ea`): +1.1 / +4.3 / +4.2 MiB of
GEM (qeynos2 / gfaydark / growthplane), NO fall in the stream-upload buffers
(the 2D UI and the entities keep them busy), larger varying-output BOs, and
qeynos2 60 -> 30 fps, gfaydark 30 -> 20 (`fps_median`, capped runs). Removed
(`11f1f431`). So `vboEboBytes` ~0 is the truth for this client: it uploads no
static vertex/index buffers, it streams them, and those bytes are the named
"Mesa stream uploader" row.

## 3. The tally (`[memgpu]`)

- `logical_textures` is now the textures' STORAGE (`COGLES2Texture::
  getStorageBytes()`, set by the uploader: level 0 for a main texture, the
  chain or level 0 for a companion) instead of `gpuTextureBytes`. That removes
  B948's double count (the main texture wrapped with main + alpha bytes) from
  the row WITHOUT touching `gpuTextureBytes`, which the visual gate baselines
  exactly -- B948 (#266) stays open for the gated figure.
- `logical_depth_stencil` (appended last): the window surface's
  depth/stencil from the default framebuffer's bits (`depthStencilBytesFor()`).
- `logical_buffers` is unchanged (0 in pooled mode, correctly).
- `rendered_zone_summarize.py`: `gpu_depth_stencil_steady`, `gpu_unnamed_steady`
  (= `gpu_buffers_net_steady - logical`: the driver's own buffers plus
  page/tile rounding), and the failure `gpu_unnamed_over_bound` above
  **10 MiB** (fixed: 5.0-7.7; before: 11.4-17.3 -- re-summarizing the
  `pi_legacy` runs fails all three, `pi_final` passes all three).

## 4. Numbers -- rendered client on the Pi, 1280x720, own cgroup, MiB

Capped runs (no shim), `steady_60s`. Before = `pi_before_fable` (fable tip
`4f3e37cd`, CI build) and `pi_legacy` (this binary, legacy switch); after =
`pi_final` (`11f1f431`).

| zone | gpu_buffers ready before -> after | gpu_buffers steady | net of frame dumps | owned | owned + gpu_buffers |
|---|---|---|---|---|---|
| qeynos2 | 33.96 -> 18.93 | 36.70 -> 22.52 | 35.16 -> 20.98 | 42.58 -> 39.04 | 79.28 -> 61.55 |
| gfaydark | 28.44 -> 16.59 | 33.84 -> 22.89 | 31.31 -> 20.38 | 58.00 -> 56.66 | 91.84 -> 79.56 |
| growthplane | 22.95 -> 14.34 | 25.44 -> 18.88 | 22.65 -> 16.15 | 64.73 -> 66.06 | 90.17 -> 84.94 |

`owned` moves with run-to-run RssAnon noise (the fix is outside RssAnon; the
legacy runs of this binary read 43.73 / 60.53 / 66.15).

GPU buffers vs the tally after: qeynos2 22.35 = textures (storage) 11.67 +
depth 3.52 + unnamed 5.63 + frame dumps 1.53 (census run).

Frame rate (`fps_after_ready`, mean / median, capped): qeynos2 58.09/60 ->
58.43/60, gfaydark 28.18/30 -> 28.18/30, growthplane 20.00/20 -> 19.86/20.

## 5. B963 (#281)

The client does not have the benchmark's shape: it creates no static
vertex/index buffers at all in pooled mode (the census has no `vbo:` row after
the fix); its small (<= 4 KiB) buffers are draw-time varying outputs and PP
streams from Lima. The benchmark's per-buffer VBOs remain a benchmark-only
difference.

## Deviations

- **The Pi binaries were built locally**, not by CI: `downloads.sourceforge.net`
  answered 522 for hours (B970, #288), so CI `build-arm` failed at the Irrlicht
  layer. Built with a scratch copy of `docker/Dockerfile.arm-noble` that copies
  the Irrlicht 1.8.5 zip (sha256 effb7bee..., `/mnt/projects/irrlicht-1.8.5.zip`)
  and libpng 1.6.37 (GitHub tag tarball) from a local build context instead of
  wget, image `willeq-arm-noble-b962` (the shared image was not retagged),
  published by hand to `review/B962/arm/<sha>/` (strip, `mesa_nollvm_select.sh`,
  `git archive` config/data, `BUILD_INFO` says so). CI `build-arm` was
  re-triggered once SourceForge answered again.
- One shim, three uses: the fable-tip baseline has no tags (event log only).
- Commit trailer `Co-Authored-By: Claude Fable 5.1` as dispatched.

## Not verified

- Pixel identity on the Pi: the rig's three dumped frames are not fixed poses
  (two runs of the SAME code differ in ~910,000 of 921,600 pixels), so no Pi
  pixel claim is made. x86: the visual gate (all five zones, goldens) passes in
  CI (pipeline 415) and locally except the host-only CPU p95 ceiling (iceclad).
- Frame time below vsync granularity: only the client's own FPS lines (1 s,
  vsync-quantised) were compared; no per-frame timer.
- The untagged `irrlicht_scene` rows (1.3 MiB in qeynos2: one 1 MiB stream
  buffer and ~0.3 MiB of varyings) come from raw-GL draws outside the driver's
  entry points (the particle point sprites draw that way); not tagged.
- Heap BO page growth is bounded by the residual only (~0.1 MiB here).
- The census runs' own RssAnon carries the shim's 2.6 MB table (BSS) -- their
  `owned` is not comparable with capped runs.
- 800x600 and any zone beyond the three.
