# Graphics Rendering

## Components

**IrrlichtRenderer** (`include/client/graphics/`, `src/client/graphics/`)
- `IrrlichtRenderer` - Main renderer (~7000 lines), manages scene, visibility, lighting, UI
- `CameraController` - FPS-style camera with free/follow/first-person modes
- `EntityRenderer` - Character/NPC 3D model rendering with GLSL shaders
- `RaceModelLoader` - Loads character models from S3D archives by race ID
- `DoorManager` - Door object rendering and state management
- `ZoneShader` - GLSL shader source and callbacks (GL 2.1 and GLES2 variants)
- `TextureAtlas` - Atlas tile layout (256px slots, 248px inner, 4px padding)
- `ConstrainedRendererConfig` - Hardware-tier presets (OrangePi, low, medium, high)
- `ConstrainedTextureCache` - LRU-evicting texture cache with memory budget
- `ConstrainedMeshCache` - Lazy/progressive zone region mesh loading with memory budget
- `PortalSystem` - AABB-derived portal extraction and stencil-based portal occlusion
- `SpellVisualFX` - Spell casting effects: glow, projectile, impact, aura, rain, ground circle
- `ParticleManager` - Unified point-sprite particle system for weather and spell effects (GLES2)
- `SpellEffectsConfig` - JSON-configurable spell particle presets (`config/spell_effects.json`)

**GLES2 Driver** (`docker/irrlicht-drm/`)
- `COpenGLES2Driver.h/.cpp` - Native GLES2 `IVideoDriver` (extends `CNullDriver`)
- `COGLES2Texture.h/.cpp` - Texture management, native ETC1, FBO render targets
- `COGLES2Shaders.h/.cpp` - 6 built-in GLSL ES 1.0 shader programs, uniform cache
- `COGLES2MaterialRenderer.h/.cpp` - Material type renderers (blend, depth, cull state)

**EQ File Loaders** (`include/client/graphics/eq/`)
- `pfs.cpp/h` - PFS/S3D archive reader
- `wld_loader.cpp/h` - WLD fragment parser (0x03-0x36 fragments)
- `s3d_loader.cpp/h` - High-level zone/model/character loader
- `dds_decoder.cpp/h` - DXT1/DXT3/DXT5 texture decompression
- `zone_geometry.cpp/h` - Irrlicht mesh builder with texture support

## Renderer Tiers

| Tier | Driver | GPU | Use Case |
|------|--------|-----|----------|
| Software | Irrlicht `EDT_BURNINGSVIDEO` | None | Headless, VNC, RDP |
| Desktop GL | Irrlicht `EDT_OPENGL` (GL 2.1) | Desktop GPU | PC/laptop with X11 |
| GLES2 | Custom `EDT_OGLES2` (`COpenGLES2Driver`) | Mali 400, mobile | Orange Pi, Android |

The GLES2 tier uses an all-shader pipeline with 6 built-in programs:
- `Solid3D` / `AlphaTest3D` — Non-atlas zone geometry, entities, doors (per-vertex lighting + fog)
- `AtlasSolid3D` / `AtlasAlpha3D` — Atlas zone geometry (precomputed UV in texcoord1)
- `UI2D` — All 2D UI (textured quads with color modulation)
- `Color2D` — 2D rectangles, lines, debug overlays

All VS use `precision highp float` (FP32 on Mali 400 vertex processor). All FS use `precision mediump float` (FP16 on Mali 400 fragment cores).

Custom shader materials from `zone_shader.cpp` use GLES ES 1.0 variants when `EQT_HAS_GLES2` is defined (`attribute`/`varying` instead of `gl_Vertex`/`gl_TexCoord[]`, `precision` qualifiers).

## Hybrid Per-Vertex/Per-Pixel Lighting

Zone lights (indices 1-7: torches, campfires) are computed per-vertex in the VS using standard Lambertian `max(NdotL, 0.0)` with quadratic attenuation. Player light (index 0) is computed per-pixel in the FS when `/plight` is enabled, using OPT C math: single `inversesqrt` for light direction, quadratic-only attenuation `1/(constant + quadratic*d²)`, no FS `normalize()` of normal (VS already normalizes). When `/plight` is disabled, lightweight shaders are used (all lights per-vertex, trivial FS). The renderer auto-swaps all zone mesh materials between per-pixel and lightweight variants via `swapZoneMeshMaterials()`. Point light contributions are additive — NOT multiplied by `uTintColor` (night darkening) or `aColor` (EQ baked vertex colors), which would suppress them to invisibility at night.

## Mali 400 Shader Optimization Rules

Benchmarked with `gles2_shader_perpixel_benchmark` and `gles2_program_switch_benchmark`:

- **FS branches are extremely expensive** even on uniforms. A simple `if (color > 0)` on Mali 400 fragment cores costs ~4.5ms at 1280x720 with only 224 triangles. Always use branchless shaders or specialized programs instead of runtime `if` checks.
- **`length()` and `normalize()` are expensive in FS** — each requires `sqrt`/`inversesqrt`. Consolidate to a single `inversesqrt(dot(v,v))` when possible.
- **VS branches and light count are free** — the Mali 400 vertex processor handles loops and branches efficiently. 0 VS lights vs 7 VS lights: identical performance.
- **Program switching cost is negligible** — ~0.08ms per `glUseProgram` switch. Switching between 2-8 programs within a frame costs <1ms total.
- **Total compiled program count has zero impact** — 9, 18, or 27 compiled programs show identical render performance. No instruction store pressure observed.

## Zone Rendering Optimizations

**Static VBOs and material-sorted draw** (GLES2): Zone geometry is uploaded once at zone load via `GL_STATIC_DRAW` VBOs, eliminating per-frame CPU→GPU DMA. Draw calls are sorted by material key `(MaterialType << 16) | TextureID` to minimize `glUseProgram` + `glBindTexture` state changes, with front-to-back order preserved within each material group.

**Front-to-back sorting** (`manualZoneDrawEnabled_`): Zone region meshes are removed from Irrlicht's scene graph rendering and drawn manually in front-to-back order for early-Z rejection on tile-based GPUs. The injection point is `RenderPassTimer::OnRenderPassPreRender(ESNRP_SOLID)` — fires after CAMERA pass sets up matrices. Toggle at runtime with `/sort`.

- Sorted by nearest-AABB-edge squared distance from camera
- Enabled automatically for PVS zones on GLES2 (also works on desktop GL)
- Zone nodes are set invisible so `drawAll()` skips them; manual draw uses `driver_->setMaterial()` + `driver_->drawMeshBuffer()`
- **Critical**: Must use `node->getPosition()` not `node->getAbsolutePosition()` — Irrlicht's `OnAnimate()` skips invisible nodes, so `AbsoluteTransformation` is never computed for hidden nodes

**Stencil portal occlusion** (`portalOcclusionEnabled_`): In indoor/dungeon zones, uses the stencil buffer to mask rendering to only what's visible through doorway portals. Off by default — toggle with `/portal`.

- `PortalSystem` (`portal_system.h/.cpp`) extracts portal quads from adjacent BSP region AABBs at zone load
- Recursive stencil masking: INCR on portal quad → draw room → recurse → DECR (max depth 3)
- Uses raw GL calls (`glStencilFunc`/`glStencilOp`/`glColorMask`) not driver wrapper, restores to defaults after (keeps `SOGLES2State` cache in sync)
- D24S8 depth-stencil format configured in EGL (`CIrrDeviceFB.cpp`)
- Debug: `/stencil` overlays colored rects per stencil level, `/portal` shows portal wireframes

## GLES2 Performance Baseline (Orange Pi One, Mali 400)

Measured in qeynos2 (outdoor zone, Feb 2026) with full rendering pipeline active:

| Metric | Value | Notes |
|--------|-------|-------|
| **FPS** | 40–54 (avg ~46) | Well above 30 FPS target budget |
| **Polys/frame** | 8,700–9,800 | Against 80K budget cap |
| **Visible entities** | 36–40 | Capped at max 40, out of 148 total |
| **sceneDrawAll** | 5.6–8.6ms (settles ~6ms) | First frame 16ms (cold caches) |
| **Frame budget used** | ~6ms of 33.3ms | ~27ms headroom for CPU work |
| **Clip distance** | 300 units | |

The all-per-vertex lighting pipeline (8 lights in VS) and static VBO approach keep the GPU well under saturation. Performance drops are caused by CPU bottlenecks (stalls, init, visibility spikes), never the GPU. When diagnosing FPS issues, always look for code bugs first.

## Constrained Memory Management

On memory-limited devices (Orange Pi One: 512 MB shared RAM), the renderer uses budgeted caches and aggressive data lifecycle management to stay within ~128 MB RAM.

**Constrained texture cache** (`ConstrainedTextureCache`): LRU-evicting texture cache with a configurable memory budget (default 64 MB). Textures are created via `driver_->addTexture()` and tracked by byte size. On cache miss, textures are loaded from zone source data or re-decoded from S3D archives.

**Constrained mesh cache** (`ConstrainedMeshCache`): Lazy/progressive mesh loading for zone region meshes. Instead of building all ~1900 region meshes at zone load, meshes are built on-demand as the player moves. Regions are evicted when the cache exceeds its budget (default 24 MB). Rebuilds use `wldLoader->getGeometryForRegion()` and require zone texture pixel data to remain available for atlas assembly.

**Zone source data lifecycle**: After zone loading completes:
1. Texture pixel data (`releaseTexturePixelData()`) is released only when the mesh cache is NOT active (all meshes pre-built). When the mesh cache IS active, texture pixel data must be retained for progressive region mesh rebuilds.
2. Character model data (`clearCharacterData()`) is released in `hideLoadingScreen()` — `RaceModelLoader` independently loads `_chr.s3d` into its own cache, making the zone source copy redundant.
3. Combined zone geometry vectors (vertices/triangles) are released in `initDeferredEnvironmentSystems()` when the detail system is disabled. Runtime bounds checks use cached `zoneBounds*_` member variables instead.

**`/pmem` diagnostics**: Shows tracked memory across all subsystems (texture cache, mesh cache, GPU textures, atlas pages, S3D zone source data, entity renderer, audio, etc.) compared against RSS. Texture cache is NOT added to the tracked total because its textures are already counted in GPU Textures (created via `driver_->addTexture()`).

## Zone Loading Thread

Zone loading runs on a dedicated **LoadingThread** that owns the GL context during the load. This keeps the main thread free to pump the network and handle packets while the loading screen is displayed.

**Architecture:**
- `LoadingThread` (`include/client/graphics/loading_thread.h`) — spawns a thread, transfers GL context ownership, runs `loadZoneSequential()`, renders loading screen progress
- `LoadingStatus` — shared atomic struct for thread-safe progress communication (step number, percentage, status text)
- `GLContextHandles` — platform-specific GL context handles (EGL on ARM/GLES2, GLX on desktop) for cross-thread transfer
- `loadZoneSequential()` — 13-step sequential zone loading function that replaces the old async state machine

**Flow (initial zone load and re-zoning):**
1. Application (render thread) calls `startLoadingThread()` — transfers GL context to loading thread
2. Loading thread enters passive phase (renders loading screen at 0-45% while game thread completes network handshake)
3. Game thread receives zone data packets, calls `OnGameStateComplete()` which signals `graphicsLoadReady`
4. Loading thread enters active phase — runs `loadZoneSequential()` (45-100%), loading S3D archives, building meshes, uploading textures
5. Loading thread completes, Application calls `joinLoadingThread()` — GL context returns to render thread
6. Application signals game thread, which calls `OnGraphicsComplete()` to finalize

**Key design decisions:**
- Network runs on the game thread, rendering on the main thread (D21b)
- GL context is exclusively owned by one thread at a time (never shared)
- Loading thread renders the loading screen directly (no cross-thread render requests)
- Zone load snapshot created on game thread, loading thread started on render thread (D21b mutex handoff)

## Model Loading Order

`RaceModelLoader::getMeshForRace`:
1. Race-specific S3D file (e.g., `globalhum_chr.s3d`)
2. Zone-specific `_chr.s3d` file (e.g., `qeynos2_chr.s3d`)
3. Main `global_chr.s3d`
4. Numbered `global2-7_chr.s3d` files
5. Fallback: colored placeholder cubes

## Graphics Tools

- `model_viewer` - Character model viewer with spell effect test mode (category/class/level filtering, cast animations, particle previews)
- `zone_atlas_builder` - Offline ETC1-compressed texture atlas generator for GLES2 (`--zone <name>` or `--all`)
- `s3d_dump` / `s3d_extract` - S3D archive analysis and extraction
- `wld_dump` - WLD file content analyzer
- `generate_textures` - Offline procedural texture generator
- GPU capability tools: `gpu_texture_formats`, `gles2_etc1_benchmark`, `egl_image_sharing_test`
- Shader benchmarks: `gles2_shader_perpixel_benchmark` (FS optimization variants), `gles2_program_switch_benchmark` (per-light-count programs, switching cost, program count pressure)

## Graphics Integration Tests

Graphics integration tests require a running EQEmu server and X display. They are NOT auto-discovered by ctest because they need external dependencies.

**Requirements:**
- Running EQEmu server (login + world + zone servers)
- X display (use `DISPLAY=:99` with Xvfb for headless testing)
- EQ Titanium client files at configured `eq_client_path`
- Test config file: `/home/user/projects/claude/casterella.json`

**Running graphics integration tests:**
```bash
# Run from build directory
DISPLAY=:99 ./bin/test_zoning_graphics_integration
DISPLAY=:99 ./bin/test_inventory_model_view

# Run specific test
DISPLAY=:99 ./bin/test_inventory_model_view --gtest_filter="*ModelHasTextures*"
```

**Available graphics integration tests:**
- `test_zoning_graphics_integration.cpp` - Zone transitions with graphics enabled, LoadingPhase verification, camera collision safety
- `test_inventory_model_view.cpp` - Inventory window paperdoll model loading, textures, animation, weapons, render-to-texture

**Test config format** (`casterella.json`):
```json
{
  "clients": [{
    "character": "Casterella",
    "eq_client_path": "/path/to/EverQuestP1999",
    "host": "10.0.30.13",
    "port": 5998,
    "user": "test_user",
    "pass": "test_pass",
    "server": "ServerName",
    "maps_path": "/path/to/maps/base",
    "navmesh_path": "/path/to/maps/nav"
  }]
}
```

**Writing new graphics integration tests:**

1. Use `EQT_HAS_GRAPHICS` preprocessor guards
2. Skip if DISPLAY not set: `if (!std::getenv("DISPLAY")) GTEST_SKIP()`
3. Create client with `createClientWithGraphics()` helper
4. Wait for zone-in with `waitForZoneIn()` and `waitForZoneReady()`
5. Access renderer via `renderer_.get()` (test fixture owns it directly)
6. Process frames with `processFrames(count)` or `waitForWithGraphics(predicate)`

**Debugging crashes during integration tests:**

Use the GDB helper script to capture crash backtraces:
```bash
# Script sends keystrokes to trigger movement/zoning
./scripts/gdb_zone_crash.sh
# Output saved to gdb_crash_output.log
```

## Graphics Debug Commands

- `/sort` - Toggle front-to-back zone sorting (manual draw vs Irrlicht scene graph)
- `/portal` - Toggle stencil-based portal occlusion (indoor zones)
- `/stencil` - Toggle stencil buffer debug overlay (colored rects per level)
- `/plight` - Toggle per-pixel player light
- `/olight` - Toggle object/torch point lights
- `/zlight` - Toggle directional sun/ambient lighting
- `/fire` - Toggle fire particle effects
- `/frametiming` - Toggle frame timing profiler
- `/sky` - Toggle sky rendering
- `/togglegrass`, `/toggleplants`, `/togglerocks`, `/toggledebris` - Toggle detail objects
- `/season [spring|summer|fall|winter]` - Change seasonal details
- `/detail [low|medium|high|custom]` - Set rendering quality tier
