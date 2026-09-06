# Graphics Rendering

## Components

**IrrlichtRenderer** (`include/client/graphics/`, `src/client/graphics/`)
- `IrrlichtRenderer` - Main renderer. Batch L
  (`refactor/L_renderer_component_extraction.md`) extracted it into ELEVEN
  components, each `class EQT::Graphics::<Name>Component` living in its own
  directory under `src/client/graphics/` and `include/client/graphics/`; what
  is left of the class itself is four TUs in `src/client/graphics/renderer/`
  (`irrlicht_renderer.cpp`, `renderer_init.cpp`, `renderer_entities.cpp`,
  `renderer_event_receiver.cpp`), moved there by L12. Consult
  `docs/module_map.md` for which TU holds which body — do not scan.

  | component | directory | what it owns |
  |---|---|---|
  | `RdpComponent` | `rdp/` | the `WITH_RDP` server, capture and input path (L01, the pilot) |
  | `Eqe1ArchiveComponent` | `eqe1/` | EQE1 flat-pack open/resolve/index/report + the `EQT_HAS_DRM` fire-glow icospheres (L02) |
  | `OverlaysComponent` | `overlays/` | map/navmesh overlays, target outline, mouse targeting/LOS, HUD, fog, loading screen, paperdoll (L03) |
  | `DiagnosticsComponent` | `diagnostics/` | dumpScene, scene breakdown, frame-timing profiling, memory reports, the runtime debug toggles (L04) |
  | `PlayerCameraComponent` | `player/` | `updatePlayerMovement()`, movement collision and ground height, and the ONE first-person camera formula (L05) |
  | `SlashCommandsComponent` | `slash/` | `processSlashCommand()`, the registry-3 renderer command chain (L06) |
  | `SimBridgeComponent` | `sim/` | `SimulationWorker` marshaling; the worker itself is still the renderer's (L07) |
  | `ZoneStreamComponent` | `zone_stream/` | lazy/background mesh load, progressive load, PVS depth and neighbourhood (L08) |
  | `ZoneLifecycleComponent` | `zone_lifecycle/` | unloadZone, zone environment, zone draw, GPU upload drain (L09) |
  | `ZoneLoadComponent` | `zone_load/` | `loadZoneSequential()` and its twenty steps, plus the T109 entity load gate (L10, gate folded in by L12) |
  | `FrameComponent` | `frame/` | `processFrame()` and its four phases, the input drain, the static UI panel state, `measureSection()` (L11, stopwatch handed over by L12) |

  A component holds `RendererContext& ctx_` (shared renderer state bound by
  reference, plus a pointer to every sibling: `ctx_.Frame()`,
  `ctx_.ZoneLoad()`, …) and, except `RdpComponent`, an `IrrlichtRenderer&
  owner_` for what has not moved yet. L13 closed that handle down to 151
  uses (81 public methods + 70 in thirteen thread- or lifetime-bound stays);
  it has drifted up with normal work since — T366's census read 163, T371's
  read 165 and T388's reads **174**, so re-measure rather than quote a number;
  a member owned by another component is reached as
  `ctx_.<Owner>().<accessor>()`. See the closed **B050** — read it before assuming a member belongs
  where you find it. `initLoadingScreen`, `initDeferredGraphics`,
  `processFrame`, `shutdown` and `run` stay on `IrrlichtRenderer`:
  `Application` and `IrrlichtBridge` must not learn a component exists.
- `CameraController` - FPS-style camera with free/follow/first-person modes
- `EntityRenderer` - Character/NPC 3D model rendering with GLSL shaders, split across `entity_renderer.cpp` + 12 `entity_*.cpp` TUs (J03)
- `RaceModelLoader` - Loads character models from S3D archives by race ID
- `DoorManager` - Door object rendering and state management
- `ZoneShader` - GLSL ES shader source and callbacks; J09 moved the sources and callbacks into private headers beside `zone_shader.cpp` (`zone_shader_sources_gles2.h`, `zone_shader_callbacks.h`) — include them from `zone_shader.cpp` and nowhere else. T326 deleted the desktop-GL companion sources header: its build configuration was retired by P30/P32, so nothing in the acceptance bar parsed it.
- `TextureAtlas` - Atlas tile layout (256px slots, 248px inner, 4px padding)
- `ConstrainedRendererConfig` - Hardware-tier presets (OrangePi, low, medium, high)
- `ConstrainedTextureCache` - LRU-evicting texture cache with memory budget
- `PortalSystem` - AABB-derived portal extraction and stencil-based portal occlusion
- `SpellVisualFX` - Spell casting effects. **Only three of these are reached
  from the game** (`src/client/bridge/irrlicht_bridge.cpp`): `createCastGlow` /
  `removeCastGlow`, `createSpellComplete` and `createImpact`. The projectile,
  buff-aura, rain and ground-circle entry points have **no production caller**
  — their only callers are `tools/model_viewer/model_viewer_spell_test.cpp`
  (the manual FX harness) and the T381 unit test, so no buff aura and no spell
  projectile is ever created in a real session. Board **B161** (census re-taken
  T388, decision deferred to **B165**/**B162**); the class header says the same
  thing. Since T381 the FX layer is keyed on `EQ::SpellVisualVocabulary`
  (`noteSpellVocabulary()`), pushed across the bridge by value on the two
  spell-cast events — `setSpellDatabase()` and the `SpellDatabase` pointer are <!-- RETIRED-OK: tombstone for the route T381 deleted -->
  gone (B160), and T384 added `travelType` to that vocabulary.
- `ParticleManager` - Unified point-sprite particle system for weather and spell effects (GLES2)
- `SpellEffectsConfig` - JSON-configurable spell particle presets (`config/spell_effects.json`)
- `GPUUploadThread` - Async texture/VBO uploads via shared EGL context (separate from render thread)
- `BackgroundThreadPool` - Shared thread pool for deferred asset loading with unified `WorkPriorityKey` (PVS depth, asset type, distance)

**GLES2 Driver** (`docker/irrlicht-drm/`)
- `COpenGLES2Driver.h/.cpp` - Native GLES2 `IVideoDriver` (extends `CNullDriver`)
- `COGLES2Texture.h/.cpp` - Texture management, native ETC1, FBO render targets
- `COGLES2Shaders.h/.cpp` - 6 built-in GLSL ES 1.0 shader programs, uniform cache
- `COGLES2MaterialRenderer.h/.cpp` - Material type renderers (blend, depth, cull state)

**EQ File Loaders** (`include/client/graphics/eq/`)
- `pfs.cpp/h` - PFS/S3D archive reader
- `wld_loader.cpp/h` - WLD fragment parser (0x03-0x36 fragments); J05 moved the per-fragment parsers into six `wld_fragments_*.cpp` TUs sharing `wld_internal.h`
- `s3d_loader.cpp/h` - High-level zone/model/character loader
- `dds_decoder.cpp/h` - DXT1/DXT3/DXT5 texture decompression
- `zone_geometry.cpp/h` - Irrlicht mesh builder with texture support

## Renderer Tiers

GLES2 (`COpenGLES2Driver`) is the only 3D renderer, full stop — as of P32
(Phase 4, `docs/decisions.md` 2026-07-28 addendum) the desktop-GL
elimination is COMPLETE: `RenderingBackend::Software`/`::OpenGL`,
`EDT_BURNINGSVIDEO`/`EDT_OPENGL` client-side branches, `AnimatedTreeManager`,
and the ARM `--no-gles2` Lima build were all removed/retired. `--renderer`
only accepts `gles2`; `software`/`opengl` now fail CLI parsing fast.
Headless/VNC/RDP no longer imply the software driver — they run the same
GLES2 driver via Mesa's surfaceless EGL (llvmpipe), same as the interactive
default; only `--egl-x11` (P25) switches to a windowed EGL surface.

| Tier | Driver | GPU | Reachable via | Status |
|------|--------|-----|----------------|--------|
| GLES2 | Custom `EDT_OGLES2` (`COpenGLES2Driver`) | Mali 400 (ARM), llvmpipe (x86, surfaceless/windowed EGL) | default (no flag needed); `--renderer gles2` is an explicit, equivalent no-op | Live — the only renderer, no fallback |

Constrained presets (voodoo1/voodoo2/tnt/orangepi) are renderer-independent
(P32): they set memory/feature budgets only and always run on GLES2 — the
old preset→renderer coupling (voodoo1/voodoo2 implying
`RenderingBackend::Software`, tnt implying `::OpenGL`) was removed.

The GLES2 tier uses an all-shader pipeline with 6 built-in programs:
- `Solid3D` / `AlphaTest3D` — Non-atlas zone geometry, entities, doors (per-vertex lighting + fog)
- `AtlasSolid3D` / `AtlasAlpha3D` — Atlas zone geometry (precomputed UV in texcoord1)
- `UI2D` — All 2D UI (textured quads with color modulation)
- `Color2D` — 2D rectangles, lines, debug overlays

All VS use `precision highp float` (FP32 on Mali 400 vertex processor). All FS use `precision mediump float` (FP16 on Mali 400 fragment cores).

Custom shader materials registered by `zone_shader.cpp` use the GLES ES 1.0 sources (`zone_shader_sources_gles2.h`), which is the only shader source set left — `EQT_HAS_GLES2` is defined by every supported configure. The `#ifdef`/`#else` gates in `zone_shader.cpp` were kept as-is by T326; the `#else` arms no longer resolve, which is correct for a configuration P30's CMake gate already rejects.

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

**Static VBOs and material-sorted draw** (GLES2): Zone geometry is uploaded once at zone load via `GL_STATIC_DRAW` VBOs, eliminating per-frame CPU→GPU DMA. Draw calls are sorted by material key `(MaterialType << 16) | TextureID` to minimize `glUseProgram` + `glBindTexture` state changes, with front-to-back order preserved within each material group. `TextureID` is a stable FNV-1a hash of the texture name (`textureGroupId()`, H02b/Q02), not a texture pointer — the old pointer-derived key made draw order depend on ASLR-influenced heap addresses, nondeterministic run-to-run.

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

**EQE1 flat-pack archives — the ALL-OR-NOTHING rule and the T205–T214 fixes** (`docs/decisions.md` 2026-08-25):
- EQE1 has no S3D fallback: a missing EQE1 texture is a build/coverage defect and the cache's `exit(1)` on it is correct behaviour (Will).
- T205 (`3b456a1b`): `ConstrainedTextureCache::clear()` destroyed the UI atlas texture (`ui_atlas`, loaded from `willeq.eqe1`) on every zone unload; the first render frame after a zone load then dereferenced it — the SIGSEGV that T201–T204 chased. Process-lifetime textures are no longer destroyed by `clear()`.
- T207 (`ed14c4de`): EQE1 lookups resolve through a name→archive index (`eqe1_texture_index_builder`, `openArchiveContaining()`) instead of linear-probing every archive; 37 opens/24 unused/149MB → 13/0/46MB on the Pi (T208 `604ef722`), zone-in time unchanged (load time is not a work item — see decisions).
- T214 (`3e4e8a95`): `unloadZone()` retains archives named by `isPersistentEQE1Archive()` (`willeq.eqe1`, the client's own pack opened once at init) instead of destroying every loader; `EQT_EQE1_REQUEST_LOG_DIR` dumps every distinct EQE1 name requested and the archive that served it (`6f2f22f6`). An archive is legitimately open iff it is the zone's own (`{zone}`/`{zone}_obj`/`{zone}_chr`) or a lookup needed it — zero lookups on a zone-own archive is normal, not waste (`e70e3347`).
- T266: the EQE1 candidate set is no longer frozen at zone load. A race that reaches the entity prep queue after load queues its archive (`addSpawnedRaceEQE1Candidate`), and a name no candidate holds opens its first indexed owner (`EQE1TextureIndex::resolve`, rule R2) instead of returning nothing -- which, under all-or-nothing, was a client exit for any post-load spawn (T263: GM-spawned race 195 in qeynos2). Names in NO archive stay FATAL. Such opens are logged `late-open` and tagged in the `EQT_EQE1_REQUEST_LOG_DIR` TSV's new `how` column.
- T284: composite entity sheets are no longer bulk-opened. `loadZoneStep0_EQE1()` used to open EVERY `.eqe1` in the `--composite-entities` directory; with the shipped layout (composite dir == `--eqe1-path`) that mapped all 1506 packs / 4.44 GiB of the ETC1 tree at step 0. A sheet name now resolves through the same index-driven open every other EQE1 lookup uses (`openArchiveContaining()` -> `EQE1TextureIndex::resolve`, T207/T266) and opens exactly one pack -- measured 498 packs / 66,330,912 B -> **1 pack / 174,856 B** for a single-race composite run. T281 made this possible by indexing the 498 sheet packs. A composite directory that is NOT the indexed `--eqe1-path` is covered by `openCompositeSheetEQE1()` (sheet packs are self-named, so the name names its pack). Nothing on the Pi changed here -- decision #3 is Will's.
- Composite entities stay OFF on the `orangepi` preset (`enableCompositeEntities == false`). **The T195/T197 address-space rationale is DEAD and must not be cited again** — T284 deleted the eager scan that caused it and T289 measured VmPeak 3,128 MB -> 575 MB on the Pi. The decision stands on RESIDENT cost instead: +13.4 MB for one zone's seven composite races, scaling with the number of distinct resident races, on a 483 MB board. Will chose not to spend that budget; see `docs/decisions.md` 2026-08-27 "Composite entities stay OFF on the orangepi". Not a knob to revisit, and not for load-time reasons.

**Region meshes are always built eagerly at zone load** (`loadZoneStep4_Regions()`), not lazily/progressively. A `ConstrainedMeshCache` class existed for lazy/progressive region-mesh loading with LRU eviction under a memory budget (24 MB on the `orangepi` preset) but was **removed T67 (2026-08-11): it was never constructed in any shipped build.** The only statement that ever touched its owning `unique_ptr` as an lvalue was a `.reset()` sitting behind a null check on that same pointer, so it was always null for the entire process lifetime, and every one of its ~40 call sites was a read behind a guard that never passed. There was also no way to turn it on: the derived budget field (`meshMemoryBytes`) was computed per preset and consumed by nothing. If Pi memory pressure is ever measured to need this (P10b's single "71 evictions" observation was never reproduced — P13/P13b measured zero evictions across four later configurations), the implementation is fully recoverable, not reimplemented from scratch: last shipped at commit `49844c72` (2026-03-08, "PVS direct index lookup, SimWorker copy elimination, DRM page flip fix, stderr buffering") as `constrained_mesh_cache.h`/`constrained_mesh_cache.cpp` (formerly under `include/client/graphics` and `src/client/graphics` respectively, both deleted by T67) — `git log --all -- '*constrained_mesh_cache*'` finds the commit in one command. Its one genuinely-live piece, the mesh-size estimator, was relocated verbatim to `estimateMeshSizeBytes()` in `include/client/graphics/eq/zone_geometry.h` / `src/client/graphics/eq/zone_geometry.cpp` rather than deleted.

**Zone source data lifecycle**: After zone loading completes:
1. Texture pixel data (`releaseTexturePixelData()`) is released when `releaseTextureDataAfterUpload` is set (true whenever a preset has a nonzero `totalMemoryBudgetBytes` — `voodoo1`/`voodoo2`/`tnt`/`orangepi`), unconditionally of anything mesh-related. Retained otherwise (e.g. the default/unconstrained preset).
2. Character model data (`clearCharacterData()`) is released in `hideLoadingScreen()` — `RaceModelLoader` independently loads `_chr.s3d` into its own cache, making the zone source copy redundant.
3. Combined zone geometry vectors (vertices/triangles) are released in `initDeferredEnvironmentSystems()` when the detail system is disabled. Runtime bounds checks use cached `zoneBounds*_` member variables instead.

**`/pmem` diagnostics**: Shows tracked memory across all subsystems (texture cache, mesh cache, GPU textures, atlas pages, S3D zone source data, entity renderer, audio, etc.) compared against RSS. Texture cache is NOT added to the tracked total because its textures are already counted in GPU Textures (created via `driver_->addTexture()`).

## Zone Loading Thread

Zone loading runs on a dedicated **LoadingThread** that owns the GL context during the load. This keeps the main thread free to pump the network and handle packets while the loading screen is displayed.

**Architecture:**
- `LoadingThread` (`include/client/graphics/loading_thread.h`) — spawns a thread, transfers GL context ownership, runs `loadZoneSequential()`, renders loading screen progress
- `LoadingStatus` — shared atomic struct for thread-safe progress communication (step number, percentage, status text)
- `GLContextHandles` — platform-specific GL context handles (EGL on ARM/GLES2, GLX on desktop) for cross-thread transfer
- `loadZoneSequential()` — sequential zone loading function that replaces the old async state machine. It runs **twenty** `loadZoneStepN_*()` bodies (`include/client/graphics/zone_load/zone_load_component.h`), not thirteen: the numbering runs 0..13 but steps 8a-8f and 12b are separate functions (re-counted T371; the component table above already said twenty)

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
5. Fallback: colored placeholder cubes (a race with no `race_models.json` entry — T213 `b6d0576f` aliased post-Titanium races 415 → RAT and 457 → CLM/CLF; the runtime reads races from `data/willeq.db`, so `race_models.json` edits need a `willeq_db_builder` rebuild)

## Graphics Tools

- `model_viewer` - Character model viewer with spell effect test mode (category/class/level filtering, cast animations, particle previews)
- `zone_atlas_builder` - Offline ETC1-compressed texture atlas generator for GLES2 (`--zone <name>` or `--all`)
- `s3d_dump` / `s3d_extract` - S3D archive analysis and extraction
- `wld_dump` - WLD file content analyzer
- GPU capability tools: `gpu_texture_formats`, `gles2_etc1_benchmark`, `egl_image_sharing_test`
- Shader benchmarks: `gles2_shader_perpixel_benchmark` (FS optimization variants), `gles2_program_switch_benchmark` (per-light-count programs, switching cost, program count pressure)

### The shared client pack `willeq.eqe1`

`/mnt/projects/EverQuest_ETC1/willeq.eqe1` is fable's OWN pack (not an S3D
conversion): the UI chrome atlas plus the weather/particle/cursor textures.
It is opened once at init and retained across zone unloads
(`isPersistentEQE1Archive()`), and it is READ LIVE by every worktree's ctest
and by the visual gate, so it is never edited in place.

It has exactly two input sources and no residue (T398, board B230):

1. `tools/ui_atlas_builder` — fully procedural, opens no file. Its
   `ui_atlas.bmp` / `ui_atlas.json` become the pack entries `ui_atlas`,
   `ui_atlas_alpha` (the ETC1 alpha companion) and the raw entry
   `ui_atlas.json`.
2. The BMPs checked in under `data/textures/`.

Rebuild it with `scripts/build_willeq_pack.sh` — it stages the two input
sets, runs `eqe1_builder --bmp-dir ... --name willeq`, and installs the
result by writing a temp file in the destination directory and `mv`-ing it
over the pack, so a concurrent slot never reads it half-written. The ETC1
encode is deterministic (T279), so the rebuild is byte-identical; the
`PackReproducible` ctest test (label `hygiene;slow`) proves it against the
pinned `data/willeq.eqe1.sha256`. **A deliberate input change updates that
hash file and reinstalls the pack in the same commit.**

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
```

**Available graphics integration tests:**
- `test_zoning_graphics_integration.cpp` - Zone transitions with graphics enabled, LoadingPhase verification, camera collision safety

**Not an integration test despite the historical name** —
`test_inventory_model_view.cpp` is a headless character-model
loading/animation suite (`RaceModelLoader` + `EQAnimatedMeshSceneNode`
under an `EDT_NULL` device): no EQEmu server, no `DISPLAY`, auto-discovered
by ctest. T54 (2026-08-10) deleted the inventory-window-paperdoll fixture
it used to also hold (`InventoryModelViewTest`, unconditionally skipped —
its subject was `WindowManager`/`InventoryWindow`, deleted in 0f25cb57).
**The paperdoll itself was rebuilt by T60** on the static-panel UI:
`eqt::ui::CharacterModelView` renders the player's live `EntityAppearance`
into an offscreen RTT and `renderInventoryPopup()` blits it at
`InventoryLayout::paperdollRect()` — see `refactor/T60_paperdoll_restore.md`
and, for the atlas-ordering defect that hid it in the real client until
T294, `refactor/T294_paperdoll_atlas_verify.md`. **T409 (board B219) moved
the rect to the official client's own `IW_CharacterView` placement** — 85x168
at (166,57) in a 428x460 window, mapped onto `centerPopup` the same way
`coinRect()` maps the purse — so it no longer spans the whole gap between the
equipment columns and the coin column no longer lands on it. The RTT is sized
from that rect by `InventoryLayout::paperdollRenderTargetSize()`; nothing
hardcodes 304x256 any more. Run it like any other discovered test:
`./bin/test_inventory_model_view` or `ctest -R RaceModelAnimationHeadlessTest`.
Static-panel UI input/hit-testing coverage (also new, T54) lives in
`tests/test_static_ui_panels.cpp`, same auto-discovered/no-device-needed
category for the pure input tests (an `EDT_NULL`-device tail covers
`renderInventoryPopup`).

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

`scripts/gdb_core_bt.sh [core_file] [binary]` extracts a full backtrace
(all threads, registers, frame args) from a core dump — no zone-specific
helper script exists in the tree.

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
