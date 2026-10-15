# Graphics Rendering

## Components

**IrrlichtRenderer** (`include/client/graphics/`, `src/client/graphics/`)
- `IrrlichtRenderer` - Main renderer. Batch L
  (`refactor/L_renderer_component_extraction.md`) extracted it into ELEVEN
  components, each `class EQT::Graphics::<Name>Component` living in its own
  directory under `src/client/graphics/` and `include/client/graphics/`; what
  is left of the class itself is five TUs in `src/client/graphics/renderer/`
  (`irrlicht_renderer.cpp`, `renderer_init.cpp`, `renderer_entities.cpp`,
  `renderer_event_receiver.cpp`, moved there by L12, and B998's
  `renderer_pre_zone.cpp`, the pre-zone UI frame), beside T460's
  `software_cursor_texture.cpp` (the protected DRM cursor acquisition, a free
  function). Consult
  `docs/module_map.md` for which TU holds which body — do not scan.

  | component | directory | what it owns |
  |---|---|---|
  | `RdpComponent` | `rdp/` | the `WITH_RDP` server, capture and input path (L01, the pilot) |
  | `Eqe1ArchiveComponent` | `eqe1/` | EQE1 flat-pack open/resolve/index/report + the `EQT_HAS_DRM` fire-glow icospheres (L02) |
  | `OverlaysComponent` | `overlays/` | map/navmesh overlays, target outline (= inverted hull + depth pre-pass, no stencil, B1064, `target_outline_pass.h`), mouse targeting/LOS, HUD, fog, loading screen, paperdoll (L03) |
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
- `CameraController` - places the third-person cameras with BSP (or triangle-selector) collision: `placeRigCamera()` for the orbit-rig cameras (Overhead, User Defined 1/2, Tether; T461) and `placeChaseCamera()` for Chase, which is the same rig placement plus T464's eye-region ceiling clamp. T474 (board B650) deleted `setFollowPosition()` and the follow-distance API: Chase is a rig at T466's oracle fit (`kChaseRig`, pitch 6, dist 4.0, look-at 7.0 above the feet), scaled by the player's per-race eye height. The mode set and the rig geometry are `include/client/graphics/player/camera_mode.h` / `camera_rig.h`; first person is `PlayerCameraComponent::repositionFirstPersonCamera()`. T461 deleted its never-called free-fly `update()` (there is no Free mode); B533 (2026-10-11, MR !138) deleted the remaining fly/orbit CameraController setters and state (the two setupCamera() init writes in irrlicht_renderer.cpp; per-name table in refactor/B533/README.md)
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
- `ParticleManager` - Unified point-sprite particle system for weather and spell effects (GLES2). Since B995 (#315) the Options window's Display > Particles pages reach it as `ParticleKnobs` (`include/client/graphics/environment/particle_options.h`): the sim bridge hands `FrameComponent::particleOptionKnobs()` over every frame, the worker scales spawns by the family's density, and both renderers apply opacity / near clip per particle through `applyParticleKnobs()`; every particle carries its family (`UnifiedParticle::getFamily()`). The knobs only reduce; with no Options applied (the visual gate's benchmark) they are the identity
- 3D target ring (B994, #314) - `include/client/graphics/overlays/target_ring.h`: `TargetIndicator.ini`'s four-circle con-coloured ring under the target, `OverlaysComponent::drawTargetRing()` right after the outline; the GLES2 driver reads `S3DVertex::Color` as raw B,G,R,A bytes on this path, so the ring writes its tint B<->R swapped (measured, pinned by `TargetRingPixels`)
- `SpellEffectsConfig` - JSON-configurable spell particle presets (`config/spell_effects.json`)
- `GPUUploadThread` - Async region VBO uploads via shared EGL context (separate from render thread) -- legacy per-region path only: NOT created when the zone pools are on (B962; its second Lima context cost 0.5 MiB of GEM for nothing). Its texture request types were deleted by T475 (board B670): texture loading is synchronous (T04), and a texture missing at mesh build is not re-textured later (B671)
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

Zone lights (indices 1-7: torches, campfires) are computed per-vertex in the VS using standard Lambertian `max(NdotL, 0.0)` with quadratic attenuation. Player light (index 0) is computed per-pixel in the FS when `/plight` is enabled. **Its vector is formed in the VS** (B967, #285): `vPlayerLightVec = (uPlayerLightPos - worldPos) / 64` at FP32, a small light-relative varying, because the FS (FP16 on Mali-400) cannot subtract two world coordinates -- at |x| ~ 7,000-9,500 one FP16 step is 4-8 units, which banded the light on both GPUs and put the band edges in different places on each. **Its shape is the per-vertex family** (B968, #286): `color * max(N.L^, 0) / (c + l*d + q*d²)`, N.L^ from one `inversesqrt` (no FS `normalize()` of the normal, the VS already normalizes), the attenuation from a 256-texel FP16 LUT indexed LINEARLY in d over the light's range R (where it falls to 1/255, P07d's demotion threshold), last texel 0 -- so it never rises with distance and is 0 from R on. The GLSL lines and the LUT live ONCE in `include/client/graphics/player_light_shader.h`, spliced into all six player-light FS and the three per-pixel VS and drawn by `gles2_player_light_probe`'s `ship` variant; nets `test_player_light_lut`, `PlayerLightProbeMonotonic`. Mali-400 PP: 9 instructions against B965's 10 (`refactor/B967/README.md`). When `/plight` is disabled, lightweight shaders are used (all lights per-vertex, trivial FS). The renderer auto-swaps all zone mesh materials between per-pixel and lightweight variants via `swapZoneMeshMaterials()`. Point light contributions are additive — NOT multiplied by `uTintColor` (night darkening) or `aColor` (EQ baked vertex colors), which would suppress them to invisibility at night.

## Mali 400 Shader Optimization Rules

Benchmarked with `gles2_shader_perpixel_benchmark` and `gles2_program_switch_benchmark`:

- **FS branches are extremely expensive** even on uniforms. A simple `if (color > 0)` on Mali 400 fragment cores costs ~4.5ms at 1280x720 with only 224 triangles. Always use branchless shaders or specialized programs instead of runtime `if` checks.
- **`length()` and `normalize()` are expensive in FS** — each requires `sqrt`/`inversesqrt`. Consolidate to a single `inversesqrt(dot(v,v))` when possible.
- **VS branches and light count are free** — the Mali 400 vertex processor handles loops and branches efficiently. 0 VS lights vs 7 VS lights: identical performance.
- **Program switching cost is negligible** — ~0.08ms per `glUseProgram` switch. Switching between 2-8 programs within a frame costs <1ms total.
- **Total compiled program count has zero impact** — 9, 18, or 27 compiled programs show identical render performance. No instruction store pressure observed.
- **The FS ALU is FP16 even under `precision highp`, and the driver does not say so** (B965, `tools/hwcaps/gles2_player_light_probe`). Lima reports `GL_HIGH_FLOAT` and `GL_MEDIUM_FLOAT` as fp32 (127,127,23) in the FS and defines `GL_FRAGMENT_PRECISION_HIGH`, yet a `highp` FS gives bit-for-bit the same pixels as the `mediump` one. Any FS value past 65,504 is +inf: a squared world distance overflows at 256 units. Keep range-sensitive arithmetic in the VS or pre-scaled, and never feed a value that can reach inf to `texture2D()` — with `LINEAR` + `CLAMP_TO_EDGE`, Lima samples an infinite coordinate at the texture's NEAR end (llvmpipe at the far end). `clamp(x, 0.0, 1.0)` on the coordinate is the fix (Pi GPU p50 +2.5..4.7 % per frame in the gate zones; pre-scaling the vector instead cost more). llvmpipe also quantizes a `mediump` world-position varying (near world coordinate 7,000 the light moved in visible steps on BOTH GPUs) but does not overflow, so the overflow showed only on the Pi. B967 removed the world-position varying from the player light.
- **An FP16-quantized varying never matches across GPUs at its step lines** (B969, `refactor/B969/README.md`).
  - Both GPUs round the interpolated `mediump` varying to FP16 per pixel. llvmpipe interpolates in fp32 first,
    so its step lines are straight. Lima interpolates differently, so near each rounding midpoint it flips by one
    FP16 step, in saw-toothed bands up to ~20 px wide at grazing angles.
  - `varying highp` in the FS changes nothing on Lima: bit-identical pixels.
  - A varying that carries world coordinates (|x| > 2,048: steps of 2+ units) therefore makes Pi-vs-x86 diffs
    that no x86-invariant shader edit can remove.
  - Pass small, relative quantities instead, as the player light now does (`vPlayerLightVec`, formed in the VS:
    B967, #285, re-blessed by Will 2026-10-01). timorous_f2 Pi vs x86 went 3,175 -> 15 px > 32.

## Zone Rendering Optimizations

**Static VBOs and material-sorted draw** (GLES2): on the legacy per-region-node path (`WILLEQ_ZONE_POOLS=0`, or a preset without `mergeZoneDraws`) zone geometry is uploaded once at zone load via `GL_STATIC_DRAW` VBOs. The orangepi preset draws the T504 pools instead, from client-side arrays that Mesa streams each frame -- a static VBO for them was measured worse on the Pi (B962). Draw calls are sorted by material key `(MaterialType << 16) | TextureID` to minimize `glUseProgram` + `glBindTexture` state changes, with front-to-back order preserved within each material group. `TextureID` is a stable FNV-1a hash of the texture name (`textureGroupId()`, H02b/Q02), not a texture pointer — the old pointer-derived key made draw order depend on ASLR-influenced heap addresses, nondeterministic run-to-run.

**Front-to-back sorting** (`manualZoneDrawEnabled_`): Zone region meshes are removed from Irrlicht's scene graph rendering and drawn manually in front-to-back order for early-Z rejection on tile-based GPUs. The injection point is `RenderPassTimer::OnRenderPassPreRender(ESNRP_SOLID)` — fires after CAMERA pass sets up matrices. Toggle at runtime with `/sort`.

- Sorted by nearest-AABB-edge squared distance from camera
- Enabled automatically for PVS zones on GLES2 (also works on desktop GL)
- Zone nodes are set invisible so `drawAll()` skips them; manual draw uses `driver_->setMaterial()` + `driver_->drawMeshBuffer()`
- **Critical**: Must use `node->getPosition()` not `node->getAbsolutePosition()` — Irrlicht's `OnAnimate()` skips invisible nodes, so `AbsoluteTransformation` is never computed for hidden nodes

**Stencil portal occlusion** (`portalOcclusionEnabled_`): In indoor/dungeon zones, uses the stencil buffer to mask rendering to only what's visible through doorway portals. Off by default — toggle with `/portal`.

- `PortalSystem` (`portal_system.h/.cpp`) extracts portal quads from adjacent BSP region AABBs at zone load
- Recursive stencil masking: INCR on portal quad → draw room → recurse → DECR (max depth 3)
- Uses raw GL calls (`glStencilFunc`/`glStencilOp`/`glColorMask`) not driver wrapper, restores to defaults after (keeps `SOGLES2State` cache in sync)
- Depth-stencil format in EGL (`CIrrDeviceFB.cpp`): since B971 the surface is D24S8 only when `enableStencilBuffer` is set (portal on); otherwise D16 and no stencil -- nothing but the portal may depend on a stencil
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

**Constrained texture cache** (`ConstrainedTextureCache`): LRU-evicting texture cache whose ceiling is the preset's `textureMemoryBytes` (read it from `config/constrained_presets.json`, do not quote it). Textures are created via `driver_->addTexture()` / `gles2WrapTexture()` and tracked by byte size.

**Eviction keeps the texture OBJECT** (B944, GitLab #262). Under the ceiling the cache releases an evicted texture's GL storage (and its ETC1 alpha companion's) and keeps the `ITexture` alive as a non-resident entry; the driver's first bind (`COGLES2Texture::getOpenGLTextureName()`, which `getDriverTextureHandle()` and `lock()` also go through) calls back into the cache, which re-uploads the same pixels with the first upload's own code and evicts something older if it must. So **a raw `ITexture*` held by a material, a mesh cache or a manager never dangles across an eviction, and nothing swaps it for the placeholder** — before B944 every eviction destroyed the object, which is what crashed `gles2_zone_benchmark` at a 20 MiB ceiling and, under ASan, the client itself (a cached `EQAnimatedMesh` material in `RaceModelLoader`). Restore sources: an EQE1 entry restores from its mapped pack (character packs included, late-opened ones too — loaders live until `unloadZone()`). An S3D-path (decode) entry restores from the **`TextureOrigin`** its caller handed `getOrLoad()` (B947, GitLab #265; `include/client/graphics/texture_origin.h`): the S3D loaders stamp every `TextureInfo` with the archive it was read from (`PfsArchive::sharedPath()`, plus the member when it is not the lower-cased name), so character (`*_chr.s3d`, `global*_chr.s3d`, `global*_amr.s3d`), equipment (`gequip*.s3d`), zone, object and sky textures name their exact member, and the file-read textures (`data/textures/*.bmp`, SpellEffects) name their file; the name-keyed `ArchiveTextureSource` over the zone's `.s3d`/`_obj.s3d`/`sky.s3d` (Step 1, the benchmark's loader) is the fallback. A restore re-decodes only bytes whose size + FNV-1a-64 match the first decode, so a name two archives share cannot restore the wrong pixels. **A GLES2 entry with no usable source is PINNED, never destroyed**: it stays resident, counts toward the usage and is skipped by the LRU for the rest of the zone (`getPinnedTextureCount()`, one WARN per name; zero is the target — `registerTexture()` externals, which have no production caller, are the only by-design case). Process-lifetime textures (`ui_atlas`, item icon sheets, cursor, creature/tumbleweed atlases) are `protectTexture()`d: never evicted, never released. The destroy-on-eviction path, and with it the `TextureEvictionListener` guards' placeholder swap, survives only for textures of a non-GLES2 driver (the `EDT_NULL` unit tests). `clear()` (zone unload) destroys everything unprotected, companions included. Evidence: `refactor/B944/README.md`, `refactor/B947/README.md`; nets: `tests/test_texture_eviction_residency.cpp`, `tests/test_texture_eviction_character_restore.cpp`.

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

**The memory ledger (T491, 2026-09-14)** — `include/client/core/memory_ledger.h`,
`eqt::memory`. The `/pmem` figures above are per-subsystem *estimates*: each
subsystem tallies what it believes it holds, and T293 measured that as only
10–24% of RSS. Will's ruling ("I want 100% accounting of memory use and I want
it controlled") replaced that with attribution **at the allocator**. The ledger
TU replaces `malloc`/`free`/`operator new` and charges every block's whole glibc
chunk to whatever `eqt::memory::Category` the CALLING THREAD is scoped to.

- **Opening a scope is the entire API you need in renderer code**:
  `eqt::memory::MemoryScope s(eqt::memory::Category::ZoneMesh);` at the top of a
  function attributes everything it and its callees allocate, on this thread,
  until it returns. Scopes nest and restore in LIFO order; a thread's default is
  set once at its entry point with `setThreadCategory()`. A block is always
  credited back to the category it was CHARGED to, whatever scope frees it, so
  mismatched scopes cannot drive a category negative.
- **Irrlicht needs no third_party patch.** Its containers allocate through
  `irrAllocator::internal_new`, which resolves to our `operator new` at link
  time, so driver and scene-graph allocations land in whichever scope called
  into Irrlicht. That is why the zone-load steps and the render loop set
  `Category::IrrlichtScene`.
- **Lifecycle points**, not polls: `recordPoint("<point>")` emits one
  `[memledger]` line with every category's live/peak bytes, `RssAnon`, and the
  coverage split. The client records startup, login_world, zone_load_start,
  zone_parse, spatial_bsp, mesh_build_textures, objects, entities,
  zone_load_complete, ready, steady_60s, zone_change_before/after_unload and
  shutdown; `gles2_zone_benchmark` mirrors the same schema so one parser reads
  both (`scripts/ci/memory_cap_hunt.sh`).
- **RssAnon is COVERED, not partitioned**, and the reason is measured: glibc's
  `arena` can hold free pages that are not resident. The four buckets are
  attributed (the categories) + `heap_uncharged` (glibc's own internal
  allocations, which an in-executable interposer cannot see) + `malloc_slack`
  (free lists and fragmentation) + `unaccounted` (`RssAnon - arena`: thread
  stacks, direct mmap, llvmpipe's JIT pages). Only `unaccounted` and
  `heap_uncharged` are exact; only `unaccounted` is what more attribution work
  can never reduce. The CI gate asserts the unaccounted share, the slack share
  and each category against its `memoryLedgerBudgets` preset entry.
- **Cost**: measured at ~45 ns per malloc/free pair over glibc's ~18 ns on x86
  (`MemoryLedger.HookOverheadPerAllocationIsSmall` prints it every run).
  Compile it out with `-DEQT_MEMORY_LEDGER=OFF` — then no symbol is replaced and
  `MemoryScope` is an empty struct.
- **The target is 32–64 MB RSS** (Will, 2026-09-17), with the T293 256 MB
  ceiling now an interim bound. Every report prints the gap to 64 MB by bucket,
  largest first; the gate fails on the ceiling and prints the target gap.
- **GPU buffers: the `gpu_buffers` row (B955, `include/client/core/gpu_buffer_census.h`).**
  On the Orange Pi every texture, VBO, depth buffer and render target is a Lima
  GEM object: shmem pages the kernel pins for the process. They are in no
  RssAnon, RssShmem or `gpu_driver` figure (`gpu_driver` is the driver's CPU
  heap), and Lima on the Pi's 6.12 kernel publishes no `drm-*` fdinfo keys. A
  driver that does publish them (Panfrost on the ROCK 4D) gives the row its
  `drm-resident-memory`, never `drm-total-memory` (B1032): the total counts
  every created buffer whether or not it has pages, and Mesa's
  `panfrost_open_device` creates a 128 MiB growable tiler heap
  (`PAN_BO_GROWABLE`, `src/gallium/drivers/panfrost/pan_device.c`) that the
  kernel backs only as the tiler faults it in -- 171 MB total against 38 MB
  resident at qeynos2, `review/B1032/README.md`. The
  row reads them from the process's OWN memory cgroup (`memory.stat` shmem),
  which is exact only when the client is alone in it: `scripts/pi/rendered_zone_run.py
  --own-cgroup` (`systemd-run --user --scope`), the default in
  `scripts/pi_rendered_zone_sweep.sh`. Outside such a cgroup the row falls back
  to the CPU-mapped DRM BOs in `/proc/self/maps` (`drm_maps_lower_bound`: GPU-only
  buffers such as render targets are never mapped). On llvmpipe the "GPU" is
  heap inside the process, already charged to `gpu_driver`: the row then
  reports the driver's tally (`logical_in_gpu_driver`) and is NOT added to
  owned. Every `recordPoint()` prints a `[memgpu]` line beside `[memledger]`:
  the row, its source, the renderer's logical tally (`gpuTextureBytes` +
  `vboEboBytes`, published by `src/client/graphics/diagnostics/gpu_buffer_tally.cpp`
  from the GL-owning thread) and the residual between them, the cgroup's
  kernel bytes, the mapped bytes, and `owned` (B978: RssAnon − gpu_driver +
  gpu_buffers when the row is outside RssAnon — the 64 MB target figure; the
  line ends `owned_rss_anon=<RssAnon-gpu_driver> owned=<B>`, and the old
  `owned_plus_gpu_buffers` field was removed by B978, pinned absent by the
  tests). Not on the row: the display controller's scanout
  framebuffers (CMA, allocated by the display device -- the rigs read them from
  debugfs as root, `proc_mem_snapshot.py`'s `gpu.scanout_fb_bytes`) and a frame
  the client dumps into a tmpfs (charged to the same cgroup; the summarizer
  subtracts it). Measurements and the residual's composition:
  `refactor/B955/README.md`.
- **Every GEM buffer has an owner (B962, `refactor/B962/README.md`).** The
  diagnostic shim `scripts/pi/lima_bo_census.c` records each Lima buffer with
  the owner tag the creating thread pushed (`eqt::memory::GemTag`,
  `include/client/core/gem_owner_census.h`; `irr::video::GemTagScope` in the
  driver) and dumps the live set at every ledger point;
  `scripts/pi/gem_owner_table.py` folds it into an owner table against the
  `[memgpu]` row (`pi_rendered_zone_sweep.sh --bo-census`). **A new GL call that
  allocates GPU storage gets a tag.** The `[memgpu]` tally is the textures'
  STORAGE (`COGLES2Texture::getStorageBytes()`, not the gated
  `gpuTextureBytes`) plus `logical_depth_stencil`; what it does not name
  (`gpu_unnamed_steady`: Mesa's stream uploads, Lima's draw-time BOs, PLB,
  rounding) fails the Pi rig above 10 MiB. Two rules it established: **an
  EQE1 texture sets its final minification filter BEFORE its levels are
  uploaded** (with the default filter Mesa allocates level 0 alone and then the
  chain again -- two buffers per texture), and **unit 0 uploads level 0 only**
  (it is never mip-filtered). The client streams its zone vertices (the pools
  are client arrays); a shared static VBO for them measured MORE GEM and half
  the frame rate, so the zone contributes ~0 to `vboEboBytes`; what the tally
  does carry is the name-tag batch's VBO/EBO storage (B975, through
  `gles2AdjustRawBufferBytes`). `WILLEQ_GPU_BUFFERS_LEGACY=1`
  restores the pre-B962 allocation pattern for an A/B.
- **Lima's BO cache is ON by default; `nobocache` is opt-in (B988, Will
  2026-10-05).** `configureLimaBufferPolicy()`
  (`include/client/graphics/lima_buffer_policy.h`, called before
  `createDeviceEx`) leaves `LIMA_DEBUG` untouched unless
  `WILLEQ_LIMA_NOBOCACHE=1`, which appends `nobocache` (B986's policy: other
  flags kept, idempotent, `all` respected); a caller's own
  `LIMA_DEBUG=nobocache` works without the switch. Renderer init logs one
  `[GL] Lima buffer policy:` INFO line with the effective policy. Measured
  same-binary on the Pi: `nobocache` saves 1.5–3.5 MiB owned standing still
  (9–20 MiB in 200-NPC crowds) but doubles scene draw (gfaydark 7.7 -> 15.5
  ms, qeynos2 3.3 -> 6.0 ms). With the cache on, the 10 MiB unnamed-GPU guard
  fails on crowd/zone-hop tours until B972 (#290) removes the retained buffers
  at the source; the guard is not raised. Record:
  `refactor/B986_pi_gpu_guard.md`.

## Entity name tags (B975)

`EntityRenderer::createNameTagNode()` is the factory. `NameTagNode` keeps
Irrlicht's `ITextSceneNode` interface and registration condition, including
visibility and impostor re-registration. `NameTagBatchNode` orders the
registered tags far-to-near, projects with one proj × view product per frame,
and draws compact glyph vertices through the GLES2 path. A batchable run
within the index limit takes one draw; fallback glyphs split runs.
The compact index prefix grows only at need and is reused across chunks and
frames; draw length follows the used vertex prefix, not retained storage.
CPU vertex storage stays sized between runs and grows once per tag, so glyph
emission does not repeat vector bookkeeping for every corner.
Immutable atlas coordinates are cached once per glyph using the driver's
same arithmetic; this adds no per-tag storage.
The driver's state-only 2D entry point avoids a bootstrap client-array draw.
Bounded VBO/EBO storage is reused; one slack vertex prevents whole-resource
renaming on Lima. Writes remain synchronized, never orphaned or unsynchronized.
GPU bytes are included in the driver's logical buffer tally and reported
separately from CPU bytes in the name-tag owner row; destruction and idle-window
trimming release the storage.
`BitmapGlyphTable` is shared with UI text. `WILLEQ_NAMETAG_BATCH=0` selects
the original per-node path for A/B verification. The `[nametags]` rows and
`ent.nametag_batch` memory owner report the batch's cost and storage.
`test_name_tag_batch` and `nametag_drawcost` check pixel identity, changes to
text and draw counts; measured evidence and limits are in `refactor/B975/README.md`.
The DRM benchmark applies the same Lima buffer policy as the client
(`configureLimaBufferPolicy()`; since B988 that is the BO cache ON unless
`WILLEQ_LIMA_NOBOCACHE=1`), so its default timing is the client's default
path. B975's accepted 1914 / 1955 us were measured under `nobocache`.

## Impostor billboards (B964, the production port of B952)

Distant characters are drawn as camera-facing billboards of a capture of
themselves. Will approved B952's design and blessed the numbers (2026-09-30):
**128^2 impostors, a 20 MiB `tex.impostor` row**. A non-original LOD feature:
**on only when the preset gives it a budget** (`impostorTextureMemoryBytes`,
`orangepi` only) and **off under `WILLEQ_BASELINE=1`** (no cache, no row, the
visibility cap unchanged). `WILLEQ_IMPOSTORS=0` turns off this feature alone
(the A/B switch).

- **Pieces.** `include/client/graphics/impostor/impostor_policy.h` (the rules,
  device-free, `tests/test_impostor_policy.cpp`), `impostor_cache.h` (atlas
  pages + the capture), `impostor_batch_node.h` (the draw), and
  `src/client/graphics/entity_impostors.cpp` (`EntityRenderer::updateImpostors()`,
  the per-frame pass at the end of `updateMainThreadEntityState()`, i.e. frame
  phase 3, **before `beginScene()`** -- every capture's FBO switch is outside
  the frame).
- **Storage.** One scratch render target (128^2 RGBA8 + DEPTH16, from
  `addRenderTargetTexture()`, so it carries Lima's multisample attachment)
  takes every capture; the capture is `glCopyTexSubImage2D`'d into a cell of an
  **atlas page** (1024^2 RGBA8, 64 cells, a raw GL texture wrapped with
  `gles2WrapTexture()`). The pages are the storage: `tex.impostor` = pages x
  4 MiB + the 96 KiB scratch. **Never one `addRenderTargetTexture()` per
  impostor** (B952: 2.3x its logical size in Lima GEM, and it swapped the Pi).
- **The cap** is Will's `(n + 1) * 4 s^2 + 6 s^2 <= budget` with a page as the
  allocation unit: a page is allocated only while it and the scratch fit, so 20
  MiB = 4 pages = 256 unique cells. Past that a spawn draws the shared cell of
  its race/gender/body-texture key; with no cell left for that key either, the
  least-recently-drawn unique cell is demoted (its spawn goes shared). Despawn
  frees a unique cell; a zone change frees every page.
- **Tiers.** Beyond `impostorTierDistance` (80) an entity is always its
  impostor; inside it the production `FrameBudgetGovernor` moves a full-model
  limit (Red or over target: -2, Green under 0.85 x target: +2, every 15
  frames, at most `maxVisibleEntities`), with B952's 10-frame boundary
  hysteresis; the target and group members are pinned to the model. An entity
  that wants its impostor but has none yet stays a model while full-model slots
  last and is not drawn past them (before B964 the visibility cap culled it).
  When impostors are on, the simulation worker's visibility cap grows by the
  cell capacity and the pass re-applies the old cap itself to everything NOT
  drawn as an impostor (models, placeholders, corpses, the not yet captured):
  pinned then nearest first, at most `maxVisibleEntities` are drawn; impostors
  take no slot (without it the Pi drew every streaming placeholder).
- **Units.** Creation and refresh are captures scheduled a few per frame
  (`unitBudgetFor()`: Green 3 / half a frame budget, Yellow/Red 2 / a quarter,
  never zero -- an impostor is how the governor leaves Red); a second unit
  starts only if the measured average unit still fits. Refresh triggers,
  **never a timer**: `updateEntityAppearance()` (`EntityAppearanceChanged` /
  `OP_WearChange`; the cell survives the rebuild and the old capture is drawn
  until the new one), a tier flip back to the impostor when the view angle or
  the animation changed since the capture, and a view angle past
  `kViewAngleRefreshDeg` (30, chosen, not measured).
- **The draw.** `ImpostorBatchNode` draws every billboard of a page in **one**
  `drawVertexPrimitiveList` (B952: ~0.12 ms per billboard draw on the Pi). Its
  own GLSL ES program: constant 0.5 alpha threshold (B958: never a
  derivative-dependent `discard` on Lima), RGB divided by the sampled alpha
  (the capture clears to (0,0,0,0), so bilinear sampling premultiplies the
  edge texels -- the #274 fringe), and **the entity shader's own fog formula**
  at the billboard's vertices (B952's built-in-shader fog did not match the
  entity shader; this one does -- `refactor/B964/README.md` has the numbers).
  Impostored models are withheld from the render list (T309's seam) and their
  skinning suspended (`EQAnimatedMeshSceneNode::setSkinningSuspended`); their
  name tags are re-registered by the batch node.
- **Accounting.** `[memledger-owner] ... category=textures_driver
  owner=tex.impostor live=<bytes> pages= cells= unique= shared= ...` at every
  lifecycle point (`include/client/core/memory_owner_rows.h`, a report, never
  an allocation); the pages are in the driver's `gpuTextureBytes` tally and, on
  the Pi, in the memcg `gpu_buffers` row. `[impostor]` / `[impostor-frames]`
  lines every 5 s carry the tier counts, units and frame-time percentiles.
- **Evidence tools.** `tools/hwcaps/impostor_pixel_compare` (`pixel`: model vs
  impostor per race/gender/distance through `EntityRenderer`; `drawcost`: N
  models vs N billboards), `scripts/b964/crowd_run.sh` (x86 client crowd runs)
  and `scripts/b964/pi_crowd.sh` (the Pi sweep with a GM-spawned crowd).

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
- Zone load snapshot created on game thread, loading thread started on render thread (D21b handoff). Since B917 the handoff is `eqt::ZoneLoadHandoff` (`include/client/zone_load_handoff.h`): the render thread TAKES the snapshot (the loader reads a private copy), every load carries a generation, and the game thread finalises only the load it is waiting for — a zone change that lands mid-load (death to bind, a kick, a GM summon) is queued behind the running load, which is finished and discarded

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
- `held_item_candidate_search` (T485) - renders every proper signed permutation of the held-item correction on an `EntityRenderer`-built Human male at o01 and measures each item's long axis in the body frame; see "Held items" below

## Held items (weapons, shields, lanterns at the point bones)

**One owner: `include/client/graphics/eq/bone_attachment.h`.**
`computeHeldItemTransform(bone)` is the matrix that takes an equipment mesh
(Irrlicht coordinates, centre already added by `src/client/graphics/eq/equipment_actor_geometry.cpp`)
into the entity's model frame -- in EQ space `B * C`, the point bone's world
transform times `kHeldItemCorrection`, with the item's origin (its grip) on the
bone -- and `placeHeldItem(node, bone, playerYawFix, playerScale)` is the ONE
routine that writes it onto a scene node. The three consumers --
`src/client/graphics/entity_equipment.cpp` (in-world, passing T301's player-bake compensation),
`src/client/graphics/ui/character_model_view.cpp` (the paperdoll) and
`tools/model_viewer/model_viewer_equipment.cpp` -- all call `placeHeldItem()`;
none may keep a local rotation/position split or a per-slot Euler offset.

**`kHeldItemCorrection` is the IDENTITY.** An item's axes go straight to the
point bone's. Two units shipped a turn here and both were wrong: T472's
`diag(-1, 1, -1)` (a half turn about the item's Y) and T485's first pick (a
quarter turn about it). Will ruled on the sheets, 2026-09-17: *"ALL held items
are wrongly oriented in both. In the after sheet the item's long axis FOLLOWS
the forearm; it must be PERPENDICULAR to the forearm, a fist grip, so it is off
by 90 degrees. In the before sheet they are off by 180 degrees."* Those two
statements meet at the identity and nowhere else, because a rotation about the
item's Y is a signed permutation only at 0/90/180/270.

**The criterion, and why it is worth more than the ones it replaced.** Score a
candidate by the angle between the item's long axis and the FOREARM
(`humfo_r -> humfi_r`). MEASURED: R_POINT's -Z *is* the forearm direction, dot
1.001 at all 41 frames of `o01` -- so item and forearm are both fixed in the
bone frame and that angle is **the same at every frame**. The two readings it
replaced were not: "the blade points body-forward" swung 50 degrees over the
same loop, and at the bent-elbow frame the old sheets were captured at,
body-forward and the forearm nearly coincide. That is exactly how a correction
laying the blade ALONG the forearm came to score well against it. A held-item
target that is not frame-independent is the smell; check it against
`review/T485/data/score_perpendicular.py` before believing it.

Shipped values (identity, `o01` frame 25 -- fable's arm-hanging frame, the one
to compare against the official idle, which hangs the arm at the side): blade
88.9 deg to the forearm, 2H blade 89.0, 2H crossguard along the forearm axis,
arrow 1.2 (the arrow's long axis is `-Z_item`, a different item axis from a
blade's `+X`, so the one correction sends it out past the fingers -- which is
what "hangs down the leg" looks like when the arm hangs), lantern 27.7.
`tools/hwcaps/held_item_candidate_search --frame 25` regenerates all of it; it
PAUSES the animation for the sweep, because otherwise `drawAll()` re-animates
the node between candidates and no two are compared at the same pose.

Do not re-derive this from one bone axis, and do not re-target it on a reading
of a still. If an item looks wrong, add it to the search tool's `--items` and
re-run against a fresh oracle grab.

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
