# B958 + B954 -- the inverted alpha test on Lima, and the screenshot leak

GitLab #276 (B958, class B) and #272 (B954, class A). Branch
`task/B958-B954-lima-impostor-screenshot`. Raw evidence (probe logs, pixel-test
CSVs and logs, two crop sheets per Pi run, the Pi run scripts):
`/mnt/projects/fable/review/B958/` (its README says what each file is).

No budget, golden, baseline or gate assertion changed. The local visual gate
passes every golden frame; its only misses are iceclad's process-CPU p95 and
allocator-slack share, the host-only B937/B931 shape on this loaded dev box.

## B958 -- impostor billboards inverted on Lima

### Root cause

The driver's built-in alpha-test program (`AlphaTest3D`, used for every
`EMT_TRANSPARENT_ALPHA_CHANNEL_REF` material that is not a custom shader)
compiled a second fragment shader whenever `GL_OES_standard_derivatives` was
exposed, and both llvmpipe and Lima expose it:

```glsl
float threshold = clamp(0.5 - fwidth(texColor.a), 0.1, 0.5);
if (texColor.a < threshold) discard;
```

**Lima runs that discard inverted.** It discards exactly the fragments the
condition says to keep and keeps the ones it says to discard. The sampled
alpha, `fwidth()`, the threshold and the comparison are all correct. The
fault is the discard taken on a comparison whose operand depends on a
derivative. llvmpipe runs the same source correctly. That is why one driver
drew impostors on x86 and inverted them on the Pi.

The texture, the render-to-texture path, the FBO, the alpha companion unit,
the blend, depth and stencil state, and the program-binary cache are all
ruled out by measurement (below).

### Evidence -- `tools/hwcaps/gles2_alpha_test_probe`

One 64x64 RGBA texture: left half opaque (200,120,40,255), right half
(0,0,0,0). It is drawn as a centred 400x300 quad over a (40,40,60) clear at
800x600 and read back with the probe's own `glReadPixels`. Each half's
interior (52,256 px, 8 px off every edge and off the seam) is classified as
model / black / background / other. A correct alpha test gives **left =
model, right = background**.

Pi: Orange Pi One, Lima, kernel 6.12.58-sunxi. Two Mesa 25.2.8 builds, told
apart by the `libgallium` the process mapped, which the probe prints:
- "prefix": `/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so`, with no
  libLLVM mapped.
- "system": `libgallium-25.2.8-0ubuntu0.24.04.1.so` plus `libLLVM.so.20.1`.

x86: llvmpipe, Mesa 25.2.8 (LLVM 20.1.2). Each probe run makes 3
invocations: prefix run 1 compiles fresh in an empty directory, prefix run 2
loads the driver's programs from the `config/shader_cache` that run 1 wrote,
and system run 1 compiles fresh. Every count below held in all three
invocations and in both raw program forms ("fresh" = compiled, "binary" =
after a `glGetProgramBinaryOES` -> `glProgramBinaryOES` round trip).

| draw | x86 llvmpipe | Pi, before the fix (prefix + system) |
|---|---|---|
| driver `EMT_TRANSPARENT_ALPHA_CHANNEL_REF`, texture uploaded by `addTexture(IImage)` / raw GL + `gles2WrapTexture` / render target | left model 52,256, right bg 52,256 | **left bg 52,256, right black 52,256** (all three textures) |
| driver `EMT_TRANSPARENT_ALPHA_CHANNEL` (blend) | left model, right bg | left model, right bg |
| driver `EMT_SOLID` | left model, right black | left model, right black |
| raw copy of the base FS (`a < 0.5`) | correct | correct |
| raw copy of the derivatives FS | correct | **inverted (bg / black)** |
| alpha written as colour, no discard | left 255, right 0 | left 255, right 0 |
| the base comparison written as colour (red = discard) | left green, right red | left green, right red |
| the derivatives comparison written as colour | left green, right red | left green, right red |
| threshold / `fwidth(a)` written as R / G | (127.5, 0) | (127.5, 0) |

To isolate the ingredient (probe run `probe2_isolate`), each shader below
keeps one part of the derivatives FS:

| variant | Pi (prefix and system) |
|---|---|
| `fwidth(a)` computed and written out, discard on `a < 0.5` | correct |
| discard on `a < uThreshold` (a uniform, 0.5) | correct |
| derivatives threshold tested as `step(threshold, a) < 0.5` | correct |
| derivatives threshold, discard moved after the `gl_FragColor` write | **inverted** |
| threshold from `fwidth(vTexCoord.x) * 0.001` (derivative of a different value) | **inverted** |

So the trigger is `discard` on `x < y` where `y` depends on a derivative.
Neither derivatives in the shader nor a non-constant operand is enough on its
own. All of the above is the shader compiler, not willeq state.

### The fix

`docker/irrlicht-drm/COGLES2Shaders.cpp`: one constant-threshold FS
(`if (texColor.a < 0.5) discard;`) for `AlphaTest3D` and `AtlasAlpha3D` on
every device. The fwidth variants and `init()`'s extension switch are
deleted. This is the same source the zone shaders already use on every GLES2
build (`zone_shader.cpp`, whose comment blamed `fwidth()` and now names the
measured cause). The Pi and x86 now compile identical sources.

`step()` would also work on today's Lima (table above), and it would keep
x86 bit-identical. It was not chosen because it still relies on a compiler
pattern the Pi gets wrong for the plain `<`. The constant threshold has no
such dependence.

After the fix, probe run `probe3_fixed` gives `driver_alpha_ref` left model
52,256 and right bg 52,256 for all three textures. That holds on the prefix
fresh compile, the prefix cached load (6 programs loaded from the cache) and
the system Mesa.

### The B952 pixel test, before / after

`scripts/b952/pixel_test.sh`, 26 race/gender variants, 40/80/160 units,
64/128/256 impostors, qeynos2 fixed pose, LLVM-less prefix.
- Before: CI ARM build of `0bd24260` (B952 + the probe, pre-fix driver).
- After: CI ARM build of `3379b062` (the same plus both fixes).
- 1280x720 before is B952's own session-4 run of the same test
  (`review/B952/pi/pixel/`).
- x86 after: the same commit, `build-gles2`, llvmpipe.

Medians over the 26 variants at size 128:

| run | dist | full-model px | impostor px | interior (both cover) / full | silhouette mismatch / footprint | diff > 32 / footprint | interior diff med / p90 |
|---|---|---|---|---|---|---|---|
| Pi before, 800x600 | 40 | 1293 | 2072 | 2.0 % | 251.7 % | 239.5 % | 16 / 47 |
| Pi after, 800x600 | 40 | 1293 | 1289 | 96.0 % | 7.3 % | 10.3 % | 2 / 14 |
| x86 after, 800x600 | 40 | 1292.5 | 1287.5 | 96.0 % | 6.8 % | 8.4 % | 2 / 15 |
| Pi before, 800x600 | 80 | 324 | 535 | 0.7 % | 252.5 % | 252.0 % | 8 / 16 |
| Pi after, 800x600 | 80 | 324 | 322 | 97.6 % | 5.0 % | 5.3 % | 1 / 6 |
| x86 after, 800x600 | 80 | 324 | 323.5 | 97.3 % | 4.1 % | 4.8 % | 1 / 6 |
| Pi before, 800x600 | 160 | 79 | 129.5 | 0.0 % | 253.5 % | 241.4 % | 0 / 0 |
| Pi after, 800x600 | 160 | 79 | 79.5 | 98.2 % | 4.9 % | 4.9 % | 2 / 9 |
| x86 after, 800x600 | 160 | 79 | 79 | 98.1 % | 3.0 % | 3.9 % | 2 / 9 |
| Pi before (B952 s4), 1280x720 | 80 | 460 | 745 | 0.8 % | 256.4 % | 252.9 % | 10 / 28 |
| Pi after, 1280x720 | 80 | 460 | 462 | 97.2 % | 5.3 % | 5.5 % | 1 / 6 |

All sizes and both resolutions: `pixel_summary.py` over the CSVs in
`review/B958/{pi,x86}/*/`. Across all nine distance x size cells at 800x600,
Pi-after minus x86-after is at most 3.6 percentage points for differing pixels,
2.6 for differing by more than 32, and 3.1 for silhouette mismatch (each as a
share of the footprint). The impostor footprint is within 5.5 px. The two GPUs
are not compared per pixel; the frames are not expected to be bit-equal.

**The fix also moves x86 impostor pixels.** B952's x86 run used the
derivatives FS. Against it, the constant threshold keeps fewer fringe texels:
at 80 units, 128^2, the impostor footprint went 335.5 -> 323.5 px and the
silhouette mismatch 6.1 % -> 4.1 %. This is the dark fringe of #274 (B956),
which narrows but does not go away. No visual-gate golden moved (see the
local gate above). CI's gate is the check of record.

## B954 -- `createScreenShot()` leaked the whole frame

### Root cause

`COpenGLES2Driver::createScreenShot()` read the frame into `new u8[w*h*4]`
and returned
`createImageFromData(ECF_A8R8G8B8, size, pixels, true /*ownForeignMemory*/, false /*deleteMemory*/)`.
`CImage(..., ownForeignMemory=true, deleteForeignMemory=false)` adopts the
pointer, and `CImage::~CImage` frees it only `if (DeleteMemory)`. So every
call leaked one frame however the caller dropped the image.

### The fix

`deleteMemory=true` (the buffer came from `new[]`, and `CImage` frees with
`delete[]`), plus a `delete[]` when no image is made.
`CNullDriver::createImageFromData` returns null only for render-target-only
formats, which `ECF_A8R8G8B8` is not.

### Leak proof -- `tests/test_screenshot_leak.cpp`

The test takes 16 screenshots at 512x384 (786,432 B per frame) on a headless
GLES2 device. Each one is beginScene -> endScene -> createScreenShot -> drop,
which is the RDP capture's shape.

| driver | glibc in-use growth | ledger growth | RssAnon growth | test |
|---|---|---|---|---|
| fixed | 0 B | 0 B | 0 B | pass |
| `deleteMemory=false` (red check) | 12,648,448 B (= 16 x 790,528, one frame + chunk overhead each) | 12,648,448 B | 12,648,448 B | **fail** |

A second case reads the centre pixel of a cleared frame back: (10,20,30). The
image still owns readable pixels after the fix.

### The other readback paths

- `IrrlichtRenderer::saveScreenshot()` (`/screenshot`, `--dump-frames-dir`) has
  its own GLES2 branch. It reads into a `std::vector` and copies into an image
  (`createImageFromData(..., ownForeignMemory=false)` copies), then drops it.
  No leak. Its fallback goes through `createScreenShot()`, which is fixed.
- `RdpComponent::captureFrameForRDP()`: `createScreenShot()` -> `lock()` /
  `unlock()` -> `drop()`. The leak was the driver's, and it is fixed.
- `COGLES2Texture::lock()` / `unlock()`: `unlock()` deletes the lock buffer and
  its conversion copy. No leak. Its consumers are the B952 pixel test and the
  tools; the paperdoll RTT is never locked (blitted with `draw2DImage`).
- Every tool caller of `createScreenShot()` (`gles2_zone_benchmark` dump
  paths, `zone_bench_frame.cpp`, `zone_bench_entity_viewer.cpp`,
  `gles2_composite_audit`, `gles2_shader_diagnostic`, `ui_inventory_capture`,
  `ui_panel_capture`, `held_item_candidate_search`, `model_viewer`) drops its
  image, so all of them were leaking through the driver and none now.

## New findings (filed)

- **#277 (B959, B)**: the GLES2 program-binary cache is written to
  `config/shader_cache` relative to the CWD, not through
  `ResolveCachePathForWrite()`. B952's runs left 46 binaries in
  `wt/audit-impostor/config/` on NFS. With no `config/` directory the save
  silently does nothing.
- **#278 (B960, B, unconfirmed)**: which production draws reached the built-in
  AlphaTest3D on the Pi before this fix, and so drew inverted. Entities and
  zones use custom alpha-test materials; the census was not taken.

Also observed, not filed: `AtlasAlpha3D` is compiled at driver init but no
code path selects it (`useProgram(EOGLES2SP_ATLAS_ALPHA3D)` has no caller).

## Deviations

- **An experimental branch, `task/B958-exp`**, was pushed. It is this branch
  plus `task/B952-impostor-measure` (not yet on `fable` when the unit began)
  and was used only for the CI ARM builds the Pi runs needed. Its x86 jobs were
  cancelled each time. B952 merged into `fable` mid-unit (`651243b8`), and this
  branch now merges `gitlab/fable`, so the exp branch adds nothing; it can be
  deleted.
- **CI's `build-arm` builds a fixed target list**
  (`docker/Dockerfile.arm-noble`), so the probe had to be added to it
  (target + copy). As in B952, the benchmark and the probe were copied from
  the job's build tree, stripped, and given the Mesa prefix with
  `scripts/pi/mesa_nollvm_select.sh` into `ci-arm/<sha>/bin/` beside CI's
  `willeq`.
- **Pi session: 12 invocations.** That is probes x 3 runs x 3 invocations,
  pixel test before 800x600, and pixel test after at 800x600 and 1280x720. The
  1280x720 "before" row is B952's session 4, not re-run.
- **Commit trailer**: `Co-Authored-By: Claude Opus 5.5`, the model that did
  the work, not the `Claude Fable 5.1` named in the dispatch.

## Not verified

- The Mesa source-level cause, i.e. which ppir pass turns the discard around.
  The probe pins the shader shape that triggers it. Not reported upstream.
- Mesa versions other than 25.2.8, and other Mali-400 / Lima kernels.
- ~~Production reach of the built-in AlphaTest3D on the Pi (#278).~~ Answered
  by B960 (`refactor/B960/README.md`).
- That no CI visual-gate golden moves: verified locally (all golden frames
  pass), with CI pending at hand-off.
- RDP capture end to end (no RDP client was connected). The leak is proven at
  `createScreenShot()`, which is its only allocation.
- The B952 pixel test on x86 before the fix was not re-run here. Its numbers
  are B952's (`review/B952/x86/pixel/`).
