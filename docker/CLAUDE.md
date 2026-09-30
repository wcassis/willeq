# Docker Build Environment & DRM/GLES2 Driver

## Cross-Compilation

**Primary target**: Orange Pi One (Allwinner H3, ARMv7-A Cortex-A7, Mali 400 GPU)

| Dockerfile | Target | Architecture | Notes |
|-----------|--------|-------------|-------|
| `Dockerfile.arm-noble` | Orange Pi One | armhf (32-bit) | Primary build target; native GLES2 (`COpenGLES2Driver`) on DRM/KMS, Mesa/Lima supplying EGL |
| `Dockerfile.arm64-noble` | Rock64 (RK3328) | aarch64 (64-bit) | Same GLES2 path, 64-bit |
| `Dockerfile` | Desktop | x86_64 | Multi-stage: builder, runtime, vnc |
| `Dockerfile.arm-cross` | ARM (legacy) | armhf | Superseded by arm-noble |
| `Dockerfile.mesa-nollvm-armhf` | Orange Pi One | armhf (32-bit) | B932/B935: builds the SHIPPED Mesa 25.2.8 without LLVM (Lima only), plus patchelf for the rpath copy; driven by `scripts/pi/build_mesa_nollvm.sh`, measured in `refactor/B932/README.md` |

## The shipped Mesa prefix (B935)

Will, 2026-09-30: "ship the prefix" (`docs/decisions.md`, #253). The Pi's GL driver is an LLVM-less Mesa prefix
that ships with the client. The system Mesa packages are left alone.

- `scripts/pi/mesa_nollvm.env` names the version, the Pi install path (`MESA_NOLLVM_PI_PREFIX`, baked into the
  build) and the NFS store (`ci-arm/mesa/<version>/`). `scripts/pi/mesa_nollvm.sha256` pins the deterministic
  tarball.
- `scripts/pi/build_mesa_nollvm.sh [--ensure] [--write-pin]` builds in this image on dev-u24 only (never on the Pi,
  never on the Inspur box), packs, checks the pin, and installs into the store. `--ensure` is the idempotent
  no-build path that CI's publish step uses.
- `scripts/pi/mesa_nollvm_select.sh` writes the absolute DT_RPATH copy (`patchelf --force-rpath`, run in this
  image). CI publishes it as `ci-arm/<sha>/bin/willeq-nollvm`.
- The rigs, the opt-out (`WILLEQ_MESA=system`) and the maps proof are covered in `docs/ci.md`, "The shipped Mesa
  prefix".

**Build command**: `scripts/build-arm-noble.sh`
```bash
./scripts/build-arm-noble.sh                    # Graphics + Audio (default)
./scripts/build-arm-noble.sh --headless         # No graphics
./scripts/build-arm-noble.sh --no-audio         # No audio
./scripts/build-arm-noble.sh --t122-texprobe    # Texture-probe instrumentation
```

There is no `--no-gles2`. P32 (`docs/decisions.md` 2026-07-28 addendum)
retired the desktop-GL ARM build and deleted the flag; the script's own
usage text lists the three above. GLES2 (`COpenGLES2Driver`) is the only
ARM renderer and there is no fallback.

**Sysroot strategy**: Uses `debootstrap --foreign --arch=armhf` to create Noble (24.04) armhf sysroot with cross-compiled dependencies. Libraries are compiled with `arm-linux-gnueabihf-gcc` and `-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard`.

**Dependencies compiled in order**: fmt, jsoncpp, GLM (header-only), cereal (header-only), Irrlicht 1.8.5 (patched), OpenAL Soft, libsndfile

**Build cache**: `build-arm-noble/cache/` — persistent across builds for incremental compilation.

**Output**: `build-arm-noble/bin/willeq` (ELF 32-bit ARM, ~10MB stripped)

## DRM/GLES2 Driver (`irrlicht-drm/`)

Custom Irrlicht device and GLES2 driver for direct rendering on ARM SBCs without X11. 6,170 lines across 14 files at `9684644f` (10 driver/device sources plus `drm_mode_select.h`, `gl_call_counters.h`, `gpu_sync_probe.h`, `t122_texprobe.h`).

### CIrrDeviceFB (`CIrrDeviceFB.h/.cpp`)

DRM/KMS/GBM/EGL framebuffer device replacing Irrlicht's X11 device:

- **Initialization chain**: `initDRM()` → `initGBM()` → `initEGL()` — finds CRTC/connector, creates GBM device/surface, creates EGL display/context
- **Page flipping**: `drmPageFlip()` uses `DRM_MODE_PAGE_FLIP_EVENT` flag + `waitForFlipComplete()` to avoid `-EBUSY` fallback to synchronous `drmModeSetCrtc` (which blocks ~35ms)
- **Input**: Linux evdev (`/dev/input/event*`) for keyboard/mouse. VT keyboard disabled via `KDSKBMODE(K_OFF)` to prevent console layer from intercepting Ctrl+key combos
- **EGL accessors**: `getEGLDisplay()`, `getEGLSurface()`, `getEGLContext()`, `getEGLConfig()`, `getGBMDevice()` — used by `GPUUploadThread` to create shared EGL context

### COpenGLES2Driver (`COpenGLES2Driver.h/.cpp`)

Native GLES2 `IVideoDriver` extending Irrlicht's `CNullDriver`:

- **State tracking** (`SOGLES2State`): Blend, depth, cull, scissor, stencil, color mask, VBO/EBO, texture units, viewport, active program
- **Extensions** (`SOGLES2Extensions`): multisampled_render_to_texture (free MSAA on Mali), discard_framebuffer, standard_derivatives, get_program_binary (shader caching)
- **Rendering**: `drawMeshBuffer()`, `draw2DImage()`, `draw2DImageBatch()`, `draw2DRectangle()`, `draw3DLine()`
- **Shaders**: `addHighLevelShaderMaterial()`, uniform/attribute setters
- **Hardware buffers**: `createStaticHardwareBuffer()`, `registerExternalHWBuffer()` (for GPU upload thread)
- **Stencil**: `setStencilTest()`, `setStencilFunc()`, `setStencilOp()` — used by portal occlusion

### COGLES2Shaders (`COGLES2Shaders.h/.cpp`)

6 built-in GLSL ES 1.0 shader programs:
- `Solid3D` / `AlphaTest3D` — Non-atlas geometry (entities, doors, non-atlas zones)
- `AtlasSolid3D` / `AtlasAlpha3D` — Atlas zone geometry (UV in texcoord1)
- `UI2D` — 2D UI quads with color modulation
- `Color2D` — 2D rectangles, lines, debug overlays

### COGLES2Texture (`COGLES2Texture.h/.cpp`)

Texture management with native ETC1 support (`GL_ETC1_RGB8_OES`), FBO render targets.

### COGLES2MaterialRenderer (`COGLES2MaterialRenderer.h/.cpp`)

Material type renderers controlling blend, depth, and cull state per material.

### Patches

- `irrlicht-drm-patch.py` — Patches Irrlicht source to add DRM device and GLES2 driver
- `gl4es-mali400-patch.py` — Legacy GL4ES translation layer (superseded by native GLES2)
