# B952 -- impostor billboards, part 1: the impostor memory row and the impostor size

GitLab #270. Plan: `refactor/B952_impostor_measure.md`. Branch
`task/B952-impostor-measure`. Raw evidence (logs, memory JSON, CSVs, crop
sheets): `/mnt/projects/fable/review/B952/` (its README says what each file is).

Everything here is the benchmark (`tools/hwcaps/`); no production code, budget,
golden, baseline or gate assertion changed. The impostor budget below is a
PROPOSAL for Will to bless -- nothing sets it.

## What the benchmark does now

| piece | where |
|---|---|
| `ImpostorCache`: one scratch render target (FBO + DEPTH16) for every impostor render; an impostor is a colour texture only, swapped in as the scratch's colour attachment (with the scratch's own sample count -- Lima renders it through `GL_EXT_multisampled_render_to_texture`, 4 samples) and wrapped with the driver's existing `gles2WrapTexture()` bridge. No driver change. The pre-B952 shape (one `addRenderTargetTexture()` each: RGBA8 + its own DEPTH16 + FBO) is `--impostor-fbo per`. | `zone_bench_impostor.{h,cpp}` |
| Capture: orthographic, along the horizontal camera->entity direction, at the entity's world transform (the lights the full model gets); the billboard is the capture frustum, facing the camera along the same direction. Before B952 the capture looked from the model's +X side, the image was rotated 180 degrees and the quad was a fixed 6 x 8 units. | same |
| `--impostor-size 64/128/256`, `--impostor-mode unique/shared` (shared keys on race + gender + texture variant), `--impostor-refresh N`, `--adaptive-tier-dist D` (beyond D always an impostor, taking no composite slot; the cap/frame-budget limit applies inside it), `--impostor-scene far/mixed`, `--entity-genders 0/1/both` | `zone_bench_options.cpp` |
| Creation and refresh run in a pre-pass before `beginScene()`, bracketed by `glFinish()`: the times are completed GPU work, and no FBO switch lands inside the frame. | `prepareImpostorFrame()` |
| The `tex.impostor` row: `[memledger-owner] ... category=textures_driver owner=tex.impostor live=<bytes> textures= claimants=` beside the steady `[memledger]` point, `[impostor-mem]` (ledger/RssAnon/RssShmem/gpu_driver/driver-tally/system-Shmem/DRM-fdinfo deltas across every creation batch), `[impostor-timing]`, and an `impostor` object in `--memory-json` (absent when no impostor exists, so the gate's JSON is unchanged). Each texture is counted once; `claimants` names how many entities draw it. | `printImpostorReport()`, `impostorJson()` |
| `--impostor-pixel-test DIR`: per race/gender variant at 40/80/160 units, the background alone, the full model (same idle pose, same lights, per-buffer materials) and the impostor at 64/128/256; per-pixel numbers + a 4x crop sheet (full / 64 / 128 / 256) per variant and distance. | `zone_bench_impostor_test.cpp` |
| Drivers: `scripts/b952/impostor_run.sh` (one run), `matrix.sh` (`full` = 78 x86 runs, `pi` = 10), `pixel_test.sh`, `summarize.py`, `marginal.py`, `pixel_summary.py`. Fixed-pose paths: `benchmarks/paths/{qeynos2,gfaydark}_impostor_fixedpose_300f.path` (the gate's first pose per zone at pitch 0). | `scripts/b952/` |

Scene of every timed run: `--entities N` cycling races 1-12, 128, 130 x both
genders, animated, re-placed by `--impostor-scene` in rows in front of the pose
(`far`: all from 92 units = 1.15 x the 80-unit tier; `mixed`: half from 10 units,
half from 92), `--adaptive-lod --lod-dist 80 --adaptive-tier-dist 80`, the
manifest's productionConfig flags, 800x600, 150 measured + 10 warmup frames.
**Variants loaded: 26** (13 races x 2 genders). Race 130 (Vah Shir) has no model
in this client data set (`/mnt/projects/EverQuestP1999`), both genders; a full
Titanium install would give 28. Texture variant is 0 for every entity (the
benchmark places no armour), so `shared` = one texture per race/gender here.

## Orange Pi (Lima, Mesa 25.2.8 LLVM-less prefix, `gles2_zone_benchmark-nollvm`)

Session 3, 2026-09-30, CI ARM build of `9dd2c020` (`build-arm` job 1735),
`gallium=1 llvm=0` mapped in every run. qeynos2 unless named. Byte columns are
MiB; `d` = the change across the impostor creation batches.

| run | textures | claimants | row (logical) | driver tally | d gpu_driver | d textures_driver KiB | d RssAnon | d system Shmem | RssAnon | gpu_driver | owned | create ms / impostor | frame p50 / p95 ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| N=1, 128, unique | 1 | 1 | 0.16 | 0.12 | 0.02 | 0.6 | 0 | 1.26 | 60.98 | 22.28 | 38.70 | 18.9 | 24.0 / 24.6 |
| N=200, 64, unique | 200 | 200 | 3.15 | 3.14 | 0.19 | 80.1 | 0 | 6.24 | 61.00 | 22.45 | 38.55 | 6.7 | 45.8 / 56.0 |
| N=200, 128, unique | 200 | 200 | 12.59 | 12.56 | 0.19 | 80.0 | 0 | 15.79 | 61.00 | 22.45 | 38.55 | 7.0 | 47.3 / 57.5 |
| N=200, 256, unique | 200 | 200 | 50.38 | 50.25 | 0.19 | 80.1 | 0 | 53.93 | 60.97 | 22.45 | 38.52 | 7.7 | 48.3 / 58.7 |
| N=200, 256, unique, **pre-B952 RTT each** | 200 | 200 | 75.00 | 50.00 | 0.75 | 79.6 | **-9.34** | **172.96** | 52.19 | 22.96 | 29.23 | 13.8 | 48.6 / 58.6 |
| N=200, 128, shared | 26 | 200 | 1.72 | 1.69 | -0.04 | 14.5 | 0 | 3.93 | 60.95 | 22.21 | 38.73 | 8.1 | 48.0 / 53.5 |
| N=200, 128, unique, refresh 30 | 200 | 200 | 12.59 | 12.56 | 0.19 | 80.0 | 0 | 15.81 | 60.96 | 22.45 | 38.51 | 6.9 | 47.0 / 57.2 |
| N=200, 128, unique, mixed | 199 | 199 | 12.53 | 12.50 | 0.19 | 79.6 | 0 | 15.13 | 60.93 | 22.45 | 38.48 | 7.1 | 50.1 / 69.3 |
| gfaydark N=200, 128, unique | 200 | 200 | 12.59 | 12.56 | 0.18 | 80.0 | 0 | 15.85 | 62.22 | 20.47 | 41.75 | 6.6 | 58.1 / 68.0 |
| gfaydark N=200, 128, unique, mixed | 199 | 199 | 12.53 | 12.50 | 0.18 | 79.6 | 0 | 14.95 | 62.26 | 20.47 | 41.79 | 7.1 | 62.0 / 82.5 |

Read off the table:

- **The impostor pixels are in system Shmem, not in RssAnon, RssShmem or
  `gpu_driver`.** Lima stores textures in shmem-backed GEM objects the process
  does not map. `gpu_driver` moves 0.19 MiB for 200 textures (the driver's CPU
  objects, ~1 KB each), RssAnon and RssShmem do not move at all, and the DRM fds
  publish no `drm-*` memory keys in `fdinfo` on this kernel (6.12.58-sunxi,
  checked on all six /dev/dri fds). So `owned = RssAnon - gpu_driver` cannot see
  impostor memory on the Pi; the only signal is the system-wide Shmem delta,
  which is noisy by construction (tmpfs writes count; the runs wrote only their
  log lines during a batch). Filed as **#273 (B955)**.
- **Measured Shmem vs the logical row.** Fixed first-use cost: 1.26 MiB (the N=1
  run: 0.16 MiB of textures + scratch). Marginal per texture, (N=200 - N=1)/199:
  64^2: ~26.2 KB (logical 16.4 KB), 128^2: 76.6 KB (logical 65.5 KB), 256^2:
  ~277 KB (logical 262 KB) -- about 10 KB per texture on top of its pixels
  (64^2 and 256^2 subtract the 128^2 N=1 run, so they are approximate). The
  driver's own tally (`gles2GetGpuTextureMemoryUsage`) tracks the logical row,
  not the Shmem.
- **The pre-B952 shape costs 2.3x its logical size on Lima and swapped the Pi.**
  200 x (RGBA8 + DEPTH16 + FBO) at 256^2: 172.96 MiB of Shmem (~906 KB per
  impostor for 393 KB of logical attachments); the process's own RssAnon fell
  9.34 MiB during the batch (pages pushed to swap). The shared scratch holds the
  same 200 impostors in 53.93 MiB: **-119 MiB**.
- **Creation**: 6.7-7.7 ms per impostor with 4x MSAA (x200 = a 1.35-1.55 s stall
  if created in one frame); 13.8 ms each in the pre-B952 shape; 18.9 ms for the
  very first one (first FBO use).
- **Refresh**: re-rendering all 200 (128^2) = 284.7 ms per pass (1.42 ms per
  impostor); every 30 frames that is 8.9 ms per frame amortised, but it lands as
  one ~285 ms hitch on the refresh frame (p95 does not show it: 5 of 150 frames).
- **Frame time is draw-call bound, not texture bound**: 1 billboard 24.0 ms p50,
  200 billboards 45.8-48.3 ms at every size, and 48.0 ms when they share 26
  textures -- about 0.12 ms per billboard draw, independent of size. One draw per
  billboard is the cost; batching billboards (an impostor atlas) is what would
  change it, not the texture size.
- **The tier split in `mixed`**: 3.4 composites / 196.6 impostors on average --
  the frame-budget limit (33.3 ms x 0.85, rolling window 30) holds the composite
  count at ~3 on both platforms because the zone plus 200 billboards is already
  over budget. Entities inside the 80-unit tier are impostored when the budget
  says so, down to ~10 units.

Voided Pi sessions (kept in `review/B952/pi/voided_*`, not used above): session
1 ran without `willeq.db` resolvable (CWD not the repo root, zero models
loaded); session 2's shared-FBO rows rendered into an incomplete FBO (the plain
`glFramebufferTexture2D` swap against Lima's multisampled scratch --
`incomplete_fbo=200`, blank impostors; fixed in `9dd2c020`, llvmpipe was never
affected); session 3's pixel tests were OOM-killed by the `createScreenShot()`
leak (**#272, B954**). The Pi pixel evidence is session 4 (below).

## x86 (llvmpipe, dev-u24)

78 runs, qeynos2 + gfaydark x N 1/50/200 x 64/128/256 x unique/shared x
far/mixed, plus the pre-B952 shape and refresh runs:
`review/B952/x86/runs_summary.md` (all rows), `x86/marginal.md`. N=200, far,
qeynos2:

| size / mode | textures | row (logical) MiB | d gpu_driver MiB | d RssAnon MiB | RssAnon MiB | gpu_driver MiB | owned MiB | create ms / imp | frame p50 / p95 ms |
|---|---|---|---|---|---|---|---|---|---|
| 64 unique | 200 | 3.15 | 46.30 | 33.41 | 165.91 | 143.38 | 22.53 | 8.1 | 52.5 / 68.5 |
| 128 unique | 200 | 12.59 | 69.72 | 53.34 | 180.60 | 170.74 | 9.86 | 8.4 | 56.9 / 68.1 |
| 256 unique | 200 | 50.38 | 107.59 | 91.08 | 218.14 | 209.04 | 9.10 | 9.5 | 51.8 / 67.0 |
| 256 unique, pre-B952 RTT each | 200 | 75.00 | 132.08 | 114.92 | 242.39 | 233.66 | 8.73 | 11.0 | 44.7 / 63.3 |
| 128 shared | 26 | 1.72 | 16.14 | 8.90 | 134.86 | 113.27 | 21.59 | 24.9 | 68.5 / 76.9 |

- On llvmpipe the "GPU" memory IS in the process: every impostor lands in
  `gpu_driver`, and each texture costs its pixels plus ~210-290 KB of llvmpipe
  state (marginal per texture from `x86/marginal.md`: 229 KB at 64^2, 351 KB at
  128^2, 548 KB at 256^2, qeynos2 unique far). The scratch saves exactly the
  depth buffers: 132.08 - 107.59 = 24.5 MiB = 200 x 128 KiB.
- x86 frame times are NOT evidence: dev-u24 was shared with other executors'
  builds during the matrix (load average 21-32 on 40 cores at 19:31). The
  memory columns are deterministic run to run.

## Pixel evidence for the size (x86; the Pi's is below)

26 variants, clear-colour background (40,40,60), idle pose, same lights.
Full tables: `review/B952/x86/pixel/pixel_summary.md`. Medians over variants:

| res | dist | model height px (min-max) | predicted | size | pixels differing / footprint | differing by >32 / footprint | interior diff med / p90 | silhouette mismatch / footprint |
|---|---|---|---|---|---|---|---|---|
| 800x600 | 40 | 87 (77-97) | 85.5 | 64 / 128 / 256 | 103.7 / 97.4 / 93.2 % | 15.6 / 11.7 / 9.4 % | 3/18, 3/15, 2/14 | 12.2 / 9.1 / 6.5 % |
| 800x600 | 80 | 43 (38-47) | 42.7 | 64 / 128 / 256 | 95.3 / 85.6 / 78.6 % | 10.0 / 6.4 / 4.4 % | 2/8, 1/6, 1/6 | 9.0 / 6.1 / 4.4 % |
| 800x600 | 160 | 21 (19-24) | 21.4 | 64 / 128 / 256 | 99.5 / 88.2 / 81.6 % | 11.0 / 6.8 / 3.3 % | 2/16, 2/9, 1/6 | 8.7 / 4.7 / 2.4 % |
| 1280x720 | 40 | 104 (91-116) | 102.5 | 64 / 128 / 256 | 103.2 / 97.1 / 93.0 % | 15.0 / 11.5 / 9.2 % | 3/18, 3/15, 2/14 | 11.6 / 8.8 / 6.9 % |
| 1280x720 | 80 | 52 (46-58) | 51.2 | 64 / 128 / 256 | 95.8 / 86.0 / 79.2 % | 10.0 / 7.0 / 5.0 % | 2/8, 1/6, 1/6 | 8.8 / 6.5 / 4.4 % |
| 1280x720 | 160 | 25 (23-28) | 25.6 | 64 / 128 / 256 | 101.5 / 90.5 / 80.9 % | 11.5 / 5.6 / 4.0 % | 2/13, 2/10, 1/7 | 10.1 / 5.0 / 3.7 % |

(the "differing" share can exceed 100 % because the impostor's footprint is
larger than the model's: its silhouette pixels count too.)

- The measured on-screen height matches the arithmetic
  `H x model_height / (2 D tan 30 deg)` within 2 px everywhere (the model is
  ~6.7 units tall at the median).
- Nearly every pixel differs, but by little: the interior (pixels both cover)
  differs by a median of 1-3 and a p90 of 6-18. The large differences are on the
  silhouette: the largest single-pixel difference is exactly 60 at 80 and 160
  units for every size, which is the background (40,40,60) against near-black
  -- the dark fringe of **#274 (B956)** (bilinear sampling against the capture's
  (0,0,0,0) clear with a hard 0.5 alpha test; the captured texture itself is
  binary alpha, 10,353 texels at 0 and 6,031 at 255 in a 128^2 census).
- Decomposition: `B952_FULL_NOFOG=1` (full model without fog) gives identical
  numbers, so fog plays no part at this pose; `B952_BB_FOG=1` (fog on the quad)
  moves the 160-unit interior median from 2 to 44 -- the built-in alpha-test
  shader's fog does not match the entity shader's.
- `--impostor-fbo per` gives bit-identical numbers to the shared scratch on x86
  (checked on race 1 at every distance/size): the swap changes memory, not
  pixels.

**Size arithmetic (no verdict).** The capture maps 1.04 x the model's height
onto the texture's rows: 61.5 / 123 / 246 texel rows for 64 / 128 / 256.
Against the measured on-screen height:

| distance | model px 800x600 / 1280x720 | texel rows per screen px, 64 | 128 | 256 |
|---|---|---|---|---|
| 40 | 87 / 104 | 0.71 / 0.59 | 1.41 / 1.18 | 2.83 / 2.37 |
| 80 (tier) | 43 / 52 | 1.43 / 1.18 | 2.86 / 2.37 | 5.72 / 4.73 |
| 160 | 21 / 25 | 2.93 / 2.46 | 5.86 / 4.92 | 11.7 / 9.84 |

At and beyond the 80-unit tier every size has at least one texel row per screen
pixel at both resolutions; below 1 (magnified) happens only for 64 inside the
tier -- which the frame-budget limit does produce (the `mixed` rows above
impostor entities down to ~10 units).

## Budget candidates (proposed -- Will blesses, nothing sets them)

Row = the logical bytes the benchmark prints (`textures x size^2 x 4 + scratch
6 x size^2`), counted once per texture. Pi-measured = the system Shmem delta
(above). x 1.25 = the headroom factor in the dispatch.

| candidate | logical row | x 1.25 | Pi measured (Shmem) | x 1.25 |
|---|---|---|---|---|
| 200 unique @ 64 | 3,301,376 B = 3.15 MiB | 3.94 MiB | 6.24 MiB | 7.80 MiB |
| 200 unique @ 128 | 13,205,504 B = 12.59 MiB | 15.74 MiB | 15.79 MiB | 19.74 MiB |
| 200 unique @ 256 | 52,822,016 B = 50.38 MiB | 62.97 MiB | 53.93 MiB | 67.41 MiB |
| shared fallback, 26 race/sex @ 64 | 26 x 16,384 + 24,576 = 450,560 B = 0.43 MiB | 0.54 MiB | -- | -- |
| shared fallback, 26 race/sex @ 128 | 26 x 65,536 + 98,304 = 1,802,240 B = 1.72 MiB | 2.15 MiB | 3.93 MiB | 4.91 MiB |
| shared fallback, 28 race/sex (full Titanium) @ 128 | 28 x 65,536 + 98,304 = 1,933,312 B = 1.84 MiB | 2.30 MiB | -- | -- |
| pre-B952 as built: 200 x 256 RTT each | 78,643,200 B = 75.00 MiB | -- | 172.96 MiB | -- |

The cap check the design asks for, as arithmetic: with a row budget B and
texture size s, unique impostors fit while `(unique + 1) x 4 s^2 + 6 s^2 <= B`;
at B = 15.74 MiB and s = 128 that is 250 unique spawns. Past it, a new spawn
draws the shared texture of its race/sex/variant key (one per key: 26-28 at
variant 0; each armour texture variant in use adds one per race/sex). On the Pi
~10 KB per texture of driver overhead sits on top of the logical row (Shmem
marginal above), which the row does not see.

## Production port -- what part 2 needs (design, not built)

1. **Where the tier decision lives.** `EntityRenderer`'s per-frame pass
   (`EntityRenderer::updateMainThreadEntityState`, called from
   `renderer_frame_input.cpp`) is the consumer: per visible entity it chooses
   full model vs impostor. The boundary comes from the production
   `FrameBudgetGovernor` (`ctx_.governor`, `frame_budget_governor.h`): Green
   widens the composite count by a step, Yellow holds, Red narrows it; its
   rolling average (`getAverageFrameTimeMs`/`getBudgetRatio`) replaces the
   benchmark's private `avgBaseZoneMs`/`avgPerEntityMs` predictor. The
   benchmark's hysteresis (10 frames) and the 3-slot boundary band carry over.
   The benchmark measured that on both platforms the budget is already spent by
   the zone plus the billboards themselves, so the governor will hold the
   composite count low: the tier distance must be a floor (beyond it always an
   impostor), not the only rule.
2. **Creation and refresh are background work the governor schedules.** They
   run before `beginScene()` (never an FBO switch inside the frame on a tiler),
   N per frame from the governor's remaining budget (`getRemainingBudgetMs`,
   a `LoadTaskWeight::Light` task): 7 ms per impostor on the Pi, so ~2-3 per
   frame, not 200 at once (1.4 s). Until an entity's impostor exists it stays
   the full model.
3. **Refresh triggers**, not a timer: `OP_WearChange` (arrives as
   `GameEventType::EntityAppearanceChanged` -> `IrrlichtBridge` ->
   `EntityRenderer::updateEntityAppearance`, `entity_lifecycle.cpp`) marks that
   entity's impostor dirty; a tier flip (composite -> impostor) re-captures at
   the current angle and pose; a view-angle change past a threshold re-captures
   (the capture is taken along the camera->entity direction). A full refresh of
   200 is a 285 ms hitch on the Pi.
4. **The cap check** is in the impostor cache: before creating a unique
   texture, `row + 4 s^2 <= budget` (the blessed row); else the entity is keyed
   on race/gender/variant and shares. A shared texture is counted once with its
   claimants (the shared-block rule). When unique textures are freed (entity
   despawn, zone-out) shared claimants may move back to unique.
5. **Accounting.** The row goes on the ledger as its own line under
   `textures_driver` (`tex.impostor`, live = logical bytes, blocks = textures,
   claimants). On the Pi the pixels are in GEM/shmem, outside RssAnon and
   `gpu_driver` -- #273 (B955) has to give GPU buffer memory a row before the
   impostor row can be reconciled against anything but itself.
6. **Draw cost.** One billboard = one draw on Lima (~0.12 ms each). An impostor
   atlas (e.g. 128^2 cells in a 2048^2 RGBA8 page = 256 cells, 16 MiB) would
   batch them into one draw per page -- a memory trade the same row governs.
7. **Driver shape to use**: the shared scratch with the colour swap (this
   unit), with the scratch's sample count; never one `addRenderTargetTexture()`
   per impostor (2.3x on Lima). `COpenGLES2Driver::createScreenShot()` must be
   fixed (#272) before any capture code reads frames back.

## Findings filed

- **#272 (B954, A)** `COpenGLES2Driver::createScreenShot()` leaks the whole
  frame per call (RDP capture: every frame while a client is connected).
- **#273 (B955, A)** GPU buffer memory on the Pi (Lima GEM/shmem) is on no
  ledger row and outside `owned`; no DRM fdinfo memory keys.
- **#274 (B956, B)** the impostor dark silhouette fringe.

## Deviations

- **Irrlicht not symlinked from `main`.** `main/third_party/irrlicht-gles2-x86`
  was built 2026-09-17 and lacks the B944 residency bridges
  (`gles2TextureReleaseStorage` & co.), so every graphics target failed to link
  against it. The worktree built its own (`scripts/build-gles2-x86.sh
  --irrlicht-only`) from its own `docker/irrlicht-drm`. `main`'s copy is stale
  for any tip after B944.
- **Four Pi sessions, not two.** Sessions 1 (10 runs, no `willeq.db`) and 2
  (10 runs + 1 pixel test, incomplete FBO) were voided and are kept as such;
  session 3 (10 runs + 2 pixel tests OOM-killed by #272) supplied the tables;
  session 4 (2 pixel tests) the Pi pixel evidence. Each session stayed within
  12 invocations; the Pi lock was taken and released around each.
- **The ARM benchmark binary** is not published by CI (`ci-arm/<sha>/` carries
  only `willeq`): it was copied from the `build-arm` job's own tree on dev-u24
  right after jobs 1735/1740 (the binary's strings carry the commit's new
  options), stripped, and given the Mesa prefix with
  `scripts/pi/mesa_nollvm_select.sh`.

## Not verified

- Pi timings with other Pi load: none observed, but the Pi swaps (241 MB swap,
  35-43 MB in use between runs); the pre-B952 row swapped mid-run.
- The system Shmem delta as a GEM measure is system-wide; that nothing else on
  the Pi wrote tmpfs during a creation batch (except the run's own log) is not
  proven. The deltas sit 3.1-3.7 MiB above the driver's own tally at N=200
  (6.24 vs 3.14, 15.79 vs 12.56, 53.93 vs 50.25 MiB), which the fixed 1.26 MiB
  plus ~10 KB per texture accounts for; that split is an inference from two
  points (N=1 and N=200), not a per-object measurement.
- Texture variants other than 0 (no armour placed), so the shared fallback's
  key count for a real zone population is not measured.
- Impostors of animated (non-idle) poses; the capture always takes the first
  frame of `o01`.
- The composite tier in the benchmark draws every body buffer with the first
  buffer's material (the pre-existing P08c stand-in); the pixel test's full
  model does NOT use it (per-buffer materials).
- x86 frame times (shared host, see above).
- 1280x720 on the Pi for the timed runs (the matrix is 800x600; the pixel test
  runs both).
- The official client is not an oracle here (impostors are a willeq feature).
