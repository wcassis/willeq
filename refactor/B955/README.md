# B955 -- the `gpu_buffers` ledger row: GPU buffer memory outside RssAnon

GitLab #273 (class A). Branch `task/B955-gpu-buffer-ledger`. Raw evidence:
`/mnt/projects/fable/review/B955/` (its README says what each file is).

On the Orange Pi every texture, vertex/index buffer, depth buffer and render
target is a Lima GEM object: shmem pages the kernel pins for the process.
B952 found 200 impostor textures moving only the SYSTEM `Shmem` line. Will's
rule (`docs/decisions.md` 2026-09-14/17): 100 % accounting of all memory on the
Pi, driver and DRM included. This unit gives those bytes a row and measures it.
No budget, golden, baseline or gate assertion changed.

## 1. Where Lima's buffers are visible per process (measured, Pi)

Pi: kernel 6.12.58-current-sunxi, Lima, Mesa 25.2.8 LLVM-less prefix (B935).
"Reference" = the system `Shmem` delta and, where a census was taken, the
LD_PRELOAD BO census (`scripts/pi/lima_bo_census.c`: every
`DRM_IOCTL_LIMA_GEM_CREATE` / `GEM_CLOSE` with its size and flags).

| source | what it reports | per process? | matches the GEM bytes? |
|---|---|---|---|
| `/proc/<pid>/fdinfo/<drm fd>` `drm-*` keys | **nothing**: Lima on this kernel publishes no `drm-*` key at all, not even `drm-driver` (checked on all six `/dev/dri` fds of kmscube, the benchmark and the client) | -- | no (absent) |
| `/proc/<pid>/status` RssAnon / RssShmem | GEM pages never appear (RssShmem 0 throughout; B952: RssAnon flat across 200 impostors) | yes | no |
| ledger `gpu_driver` | the driver's CPU heap: +0.19 MiB for 200 impostors (`[impostor-mem] gpu_driver_delta=200960`) | yes | no |
| `/proc/<pid>/maps` / `smaps` `/dev/dri/renderD128` mappings | the BOs Mesa mapped for CPU access (uploads). `VM_PFNMAP`: `Size` only, `Rss` 0. Client qeynos2 `steady_60s`: 29.89 MiB in 1,004 mappings vs 36.98 MiB of GEM. Impostors: +2.47 MiB of the +15.35 MiB -- render targets and depth are never mapped | yes, no root | **lower bound only** |
| own memory cgroup `memory.stat shmem` (`systemd-run --user --scope`) | GEM pages are charged at `GEM_CREATE` (an ioctl, process context). kmscube 1,810,432 B vs system Shmem +1,772 kB. Client to `startup` (TEST-NET-1, no server): 19,132,416 B vs 19,087,360 B in the BO census's 187 live regular BOs (-45 KB). 200 impostors: +16,097,280 B vs census +16,084,992 B vs system Shmem +16,220 kB. Rendered client, all three zones: system Shmem - memcg shmem = 4.36-4.74 MiB against a pre-run baseline of ~4.4 MiB, except growthplane `steady_60s` (5.64 MiB) | **yes, when the process is alone in its cgroup** | **yes** |
| system `/proc/meminfo` `Shmem` | system-wide (tmpfs, everyone's GEM) | no | the cross-check |
| debugfs `dri/1c40000.gpu/{gem_names,clients}` | `gem_names` lists flink'd names only (empty); `clients` has no sizes | root | no |
| debugfs `dri/display-engine/framebuffer` | the SCANOUT colour buffers with the allocating process: 2 x 3,686,400 B at 1280x720 (CMA on the display device, imported into Lima by PRIME) -- not shmem, not in the memcg | root | a different pool (CMA) -- #279 |
| debugfs `dma_buf/bufinfo` | 0 objects (read with nothing running only) | root | not measured under load |
| `GALLIUM_HUD` / `LIMA_DEBUG` | not measured: the HUD draws into the frame, and neither has a per-buffer memory counter I could find | -- | not measured |

**Chosen source: the process's own memory cgroup**, read by the client itself at
every lifecycle point. DRM fdinfo stays first in line (a kernel that adds the
keys wins automatically); outside an exclusive cgroup the row falls back to the
mapped lower bound and says so (`source=drm_maps_lower_bound`). A wrapper
process inside the scope breaks exclusivity: the first TEST-NET run had
`timeout` in the scope (`cgroup_procs=2`) and correctly fell back. Heap BOs
(`LIMA_BO_FLAG_HEAP`, 16 MiB maximum each, four at startup: two per GL context)
grow pages on GPU demand (Lima's GP out-of-memory path); whether those pages
reach our cgroup is not verified. The
system-Shmem cross-check bounds that growth at 0.3 MiB in qeynos2/gfaydark and 1.2 MiB in growthplane at `steady_60s`.

## 2. The row

`include/client/core/gpu_buffer_census.h` / `src/client/core/gpu_buffer_census.cpp`:

- `snapshot()` fills `Snapshot::gpuBuffers` and `ownedPlusGpuBuffersBytes`; the
  `[memledger]` line carries `gpu_buffers=`, `gpu_buffers_src=`,
  `owned_plus_gpu_buffers=` (before `allocs=`, so every rig's category parse is
  unchanged); the JSON `ledger` object gains `gpuBuffersBytes`,
  `ownedPlusGpuBuffersBytes` and a `gpuBuffers` object.
- every `recordPoint()` prints a `[memgpu]` line: the row, its source, the
  logical tally beside it (`logical_textures` = the driver's `gpuTextureBytes`,
  `logical_buffers` = `vboEboBytes`), `residual`, `memcg_shmem`,
  `memcg_kernel`, `cgroup_procs`, `drm_mapped`/`drm_maps`, `drm_fds`,
  `fdinfo_total`/`fdinfo_resident`, `owned`, `owned_plus_gpu_buffers`.
- the logical tally is read on the GL-owning thread
  (`src/client/graphics/diagnostics/gpu_buffer_tally.cpp`: the loading thread's
  `sampleStep`, `Application::run()` before `startup`, the render loop once a
  second) and published to atomics; `GL_RENDERER` once decides software vs GPU.
- `[memmap]`: `/dev/dri` mappings are their own owner kind (`drm:<node>`) and
  printed although never resident.
- **x86/llvmpipe**: there is no GPU; the "buffers" are heap the ledger already
  charges to `gpu_driver`. The row reports the tally with
  `source=logical_in_gpu_driver`, `outside_rss_anon=0`, and
  `owned_plus_gpu_buffers == owned` -- no double counting. Measured
  (`review/B955/x86/`, benchmark 800x600): qeynos2 `steady` gpu_buffers 6.40 MiB
  inside gpu_driver 92.64 MiB, owned 9.81 MiB = owned + gpu_buffers; gfaydark
  6.42 MiB inside 87.76 MiB, owned 12.93 MiB.

**How it enters the 64 MB target -- a proposal, for Will.** "64 MB excludes
driver memory" excluded the `gpu_driver` heap: the driver's own state. GPU
buffers are not driver overhead -- they hold OUR textures, meshes and render
targets, are allocated because the client asked, shrink when the client
uploads less (B952's shared impostor scratch saved 119 MiB of exactly this),
and come out of the same 483 MB. Proposal: they count, i.e. the target is
judged on `owned + gpu_buffers`. Nothing CI asserts changed: `over_target`
stays on `owned`, and `owned_plus_gpu_buffers` is printed beside it.

## 3. Rigs and nets

- `scripts/pi/rendered_zone_run.py --own-cgroup` launches the client in its own
  `systemd-run --user --scope` (same pid; the log stays with the runner,
  outside the cgroup). `scripts/pi_rendered_zone_sweep.sh` does it by default
  (`--shared-cgroup` opts out) and copies the run back with `cp -rp`.
- `scripts/pi/proc_mem_snapshot.py` gains `gpu`: fdinfo `drm-*` keys, DRM
  mapped bytes and the per-mapping size histogram, the memcg, the scanout
  framebuffers this process allocated (debugfs, root) and system Shmem.
- `scripts/pi/rendered_zone_summarize.py` appends `gpu_buffers_src`,
  `gpu_buffers_ready/steady`, `gpu_logical_ready/steady`, `gpu_residual_steady`,
  `dumped_png_bytes_steady`, `gpu_buffers_net_steady`, `memcg_kernel_steady`,
  `drm_mapped_steady`, `owned_steady`, `owned_plus_gpu_buffers_steady`,
  `scanout_fb_bytes`, and FAILS a run whose `[memgpu]` row reads 0 at `ready`
  (`gpu_buffers_zero`). `scripts/pi/collision_ab_table.py` gains
  `gpu_buffers @steady (source)` and `owned + gpu_buffers @steady`.
- A frame the client dumps into the Pi's tmpfs is charged to its cgroup's
  shmem; the summarizer names those bytes (frames' mtimes up to the point) and
  subtracts them (`gpu_buffers_net_*`).
- Diagnostics: `scripts/pi/gpu_buffer_probe.sh` (any command in its own
  cgroup, monotonic-stamped log, snapshots, optional BO census) and
  `scripts/pi/lima_bo_census.{c,py}` (the shim and its fold; a setcap binary
  ignores `LD_PRELOAD` -- use an uncapped copy).
- `tests/test_gpu_buffer_census.cpp` (13 cases): fixtures CAPTURED on the Pi
  (`tests/data/b955/`: kmscube's maps, render-node fdinfo, memory.stat,
  cgroup, cgroup.procs) parsed into the row; the documented fdinfo keys; the
  decision order; llvmpipe not double counted; the row on the `[memledger]`
  line before `allocs=` and in the JSON.

## 4. Numbers -- the rendered client on the Pi

Sweep `review/B955/pi_sweep_2` (CI ARM build `d0356072`, `willeq-nollvm`,
1280x720, orangepi preset, local eqemu lane, Testchar at level 60 so
growthplane admits it, own cgroup; 3/3 pass). MiB.

| zone | point | RssAnon | gpu_driver | owned | gpu_buffers | frame dumps | logical (tex / buf) | residual | DRM mapped (maps) | memcg kernel |
|---|---|---|---|---|---|---|---|---|---|---|
| qeynos2 | ready | 62.67 | 19.16 | 43.51 | 34.10 | 0 | 17.33 / 0.000 | 16.77 | 29.39 (997) | 1.69 |
| qeynos2 | steady_60s | 61.78 | 19.18 | 42.59 | 36.98 | 1.53 | 17.33 / 0.001 | 19.65 | 29.89 (1,004) | 1.82 |
| gfaydark | ready | 80.08 | 19.00 | 61.08 | 28.43 | 0 | 13.86 / 0.000 | 14.57 | 23.73 (901) | 1.61 |
| gfaydark | steady_60s | 79.44 | 19.27 | 60.17 | 35.77 | 1.04 | 14.21 / 0.007 | 21.55 | 28.84 (1,050) | 1.93 |
| growthplane | ready | 84.14 | 18.35 | 65.79 | 22.72 | 0 | 12.81 / 0.000 | 9.91 | 17.95 (423) | 1.11 |
| growthplane | steady_60s | 84.15 | 18.36 | 65.79 | 25.12 | 2.70 | 12.81 / 0.003 | 12.31 | 17.04 (407) | 1.12 |

The first sweep (`review/B955/pi_sweep_1`, default level) repeated qeynos2 and
gfaydark within 0.1 MiB / 2 MiB (gfaydark steady 33.36 vs 35.77: the stream
buffers below vary with what was drawn) and lost growthplane to the min-level
kick (`wrong_zone (qeynos)`).

**A plain zone holds 22-37 MiB of GPU buffers `owned` has never counted** --
half of it beyond what fable believes it uploaded.

### Reconciliation (qeynos2 `steady_60s`, MiB)

gpu_buffers 36.98 = logical 17.33 + residual 19.65, and the residual is:

| part | MiB | how it is known |
|---|---|---|
| frame dumps the rig asked for, written into the cgroup's tmpfs | 1.53 | the PNGs' sizes and mtimes |
| unmapped GEM: the depth/stencil buffer | 3.52 | BO census: one 3,686,400 B BO, created with the GBM surface, never mapped |
| unmapped GEM: the rest (two GL contexts' PLB / tile-heap initial pages / streams, render targets) | 2.04 | memcg - mapped - dumps - depth; per-context BO set from the census (each context: 2 x 262,144 B, 2 x 16 MiB heap, 4 KiB streams; the second context appears at the shader-compile step) |
| Mesa's 1 MiB stream-upload BOs | 3.00 | mapping histogram: 1 at startup, 3 at `steady_60s` (gfaydark 7) |
| mapped BOs beyond the logical tally (per-texture storage + page rounding, shader and small driver BOs) | 9.56 | mapped 29.89 - logical 17.33 - stream 3.00; not decomposed per texture -- zone-time mapped BOs grew 2.06x the logical zone textures |

The `startup` point (before any zone) already holds 19.13 MiB of GEM
(TEST-NET census): the UI/icon/cloud textures (tally 10.42 MiB once published)
plus the depth buffer and both contexts' driver BOs. The two biggest
named-but-not-decomposed parts, and the second context, are #280.

### Impostor proof (B952's recipe, N=200 at 128, unique, far, qeynos2)

`review/B955/imp200_128/` (benchmark from this branch + B952, own cgroup, BO census):

| figure | ready -> steady (impostors created in between) |
|---|---|
| `[memgpu] gpu_buffers` (memcg) | 52,277,248 -> 68,374,528 B: **+16,097,280 B = +15.35 MiB** |
| BO census, live regular BOs | +16,084,992 B: **201 x 65,536 B** (the 200 impostor textures, exactly their pixels, + the scratch colour) + 3 x 1 MiB stream BOs + small |
| system Shmem (`[impostor-mem] sys_shmem_delta_kb`) | +16,220 kB = 15.84 MiB (B952: +15.79 MiB) |
| logical (`tex.impostor` row / `gpuTextureBytes` delta) | 13,205,504 / 13,172,736 B (12.59 MiB) |
| DRM mapped (the fallback) | +2,588,672 B -- would see 16 % |
| RssAnon / gpu_driver | 0 / +0.19 MiB |

The row sees them. B952's "~10 KB per texture on top" is not per texture: each
128x128 impostor is exactly 64 KiB of GEM; the excess is the stream buffers
that drawing 200 billboards pulls in.

## 5. owned vs owned + gpu_buffers (for Will)

Pi, `steady_60s`, MiB. "net" subtracts the rig's own frame dumps. The scanout
framebuffers (CMA, #279) are on neither side today.

| zone | owned (= what the 64 MB target is read on today) | gpu_buffers (net) | owned + gpu_buffers (net) | + scanout (CMA) | over 64 MiB: owned / owned + gpu (net) |
|---|---|---|---|---|---|
| qeynos2 | 42.59 | 35.45 | 78.04 | +7.03 | -- / 14.04 |
| gfaydark | 60.17 | 34.73 | 94.90 | +7.03 | -- / 30.90 |
| growthplane | 65.79 | 22.42 | 88.21 | +7.03 | 1.79 / 24.21 |

## Findings filed

- **#279 (B961, A)** the scanout framebuffers (CMA, 7.03 MiB per client at
  1280x720) and the cgroup's kernel bytes (1.1-1.9 MiB) are on no ledger row.
- **#280 (B962, A)** the client's GEM buffers are ~2x its logical tally (the
  table above): per-texture BO overhead, stream uploads, depth, two GL contexts;
  and `vboEboBytes` reads ~0 on the Pi although Step 4 uploads per-region
  hardware buffers (why: not verified).
- **#281 (B963, B)** on Lima the benchmark puts every static hardware buffer in
  its own BO: 9,355 BOs of 4 KiB = 36.5 MiB (+9.6 MiB kernel slab) for 1.73 MiB
  of vertex/index data in qeynos2 -- the Pi benchmark's GPU memory is not the
  client's.

## Deviations

- **The Pi benchmark for the impostor proof was built locally** (`scripts/build-arm-noble.sh`
  in a scratch worktree `wt/b955-proof` = this branch + `task/B952-impostor-measure`,
  before !60 merged; not pushed), stripped and given the Mesa prefix with
  `scripts/pi/mesa_nollvm_select.sh`. CI publishes only `willeq`.
- **The first `docker run` of the shim build ran the ARM image's ENTRYPOINT**
  (the whole client build, in-source, into this worktree); it was killed and
  the in-source artefacts deleted before anything was committed. The shim's
  header now spells `--entrypoint`.
- The `startup` point's logical tally is 0 in every Pi number above: the
  binary is CI build `d0356072`; the startup publish (`Application::run()`)
  was added after it. The TEST-NET census supplies the startup composition.
- Commit trailer: `Co-Authored-By: Claude Opus 5.5` (the model that wrote it),
  not the `Claude Fable 5.1` the dispatch named.

## Not verified

- Per-texture attribution of the 9.56 MiB of mapped BOs beyond the tally
  (needs a name -> BO census; #280), and why `vboEboBytes` is ~0 on the Pi.
- Heap-BO page growth: its cgroup charging is not verified; bounded only by the system
  Shmem cross-check (0.3-1.2 MiB here, growthplane the largest), not measured per BO.
- `cgroup_procs == 1` is the exclusivity test; a helper process that shares the
  scope and writes nothing would make the row fall back needlessly, one that
  writes tmpfs files while alone with the client is not detected (only the
  rig's own frame dumps are named).
- DRM fdinfo keys on a kernel that has them: only the parser is tested
  (documented key set), never a live Lima that exports them.
- `GALLIUM_HUD` / `LIMA_DEBUG` and debugfs `dma_buf/bufinfo` under load.
- 800x600 on the Pi client (the sweep runs 1280x720; the depth buffer and the
  scanout framebuffers scale with it).
