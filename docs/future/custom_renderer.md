# Custom Renderer — Irrlicht Removal Plan

## Problem Statement

WillEQ uses Irrlicht 1.8.5 as its graphics engine, but has already replaced most of its core functionality with custom implementations: GLES2 driver, DRM/KMS device, skeletal animation, frustum culling, portal/PVS visibility, UI windowing, and asset loading. What remains is Irrlicht's scene graph, math types, mesh/material data structures, and `drawAll()` render loop — all of which add overhead and act as a black box that blocks instrumentation and performance work.

### What Irrlicht Provides Today (Actually Used)

| Feature | Irrlicht Component | WillEQ Usage |
|---------|-------------------|--------------|
| Scene graph | `ISceneManager`, `ISceneNode` hierarchy | Node parent-child transforms, `setPosition`/`setRotation`/`setScale` |
| Render loop | `smgr_->drawAll()` | 7-pass traversal (camera, light, sky, solid, shadow, transparent, effects) |
| Math types | `vector3df`, `matrix4`, `quaternion`, `aabbox3df`, `rect<>`, `dimension2d<>` | 2,115 usages across 86 files |
| Mesh types | `SMesh`, `SMeshBuffer`, `S3DVertex`, `IMeshBuffer` | 374 usages across 25-40 files |
| Materials | `ITexture`, `SMaterial`, `SColor`, material type enums | 2,815 usages across 68 files |
| Text rendering | `CTextSceneNode`, `CGUIFont`, `CGUISpriteBank` | Name tags (40 per frame), chat, HUD text |
| Event receiver | `IEventReceiver`, `SEvent` | Input routing (lightweight, 17 usages) |
| Timer | `ITimer::getTime()` | Frame timing (21 usages across 5 files) |
| Device | `IrrlichtDevice` | Window/context lifecycle |

### What WillEQ Already Replaced

| Component | Custom Implementation | Irrlicht Equivalent (Unused) |
|-----------|----------------------|------------------------------|
| GPU driver | `COpenGLES2Driver` (1,941 lines) | OpenGL/D3D drivers (~20K lines) |
| Device/display | `CIrrDeviceFB` DRM/KMS (~1,000 lines) | X11/Win32 device |
| Skeletal animation | `EQAnimatedMesh` + `SkeletalAnimator` (SIMD NEON/SSE2) | `ISkinnedMesh`, `animateJoints()` |
| Frustum culling | `FrustumCuller` (92 lines) | Scene graph auto-culling |
| Visibility | `PortalSystem` stencil + PVS bitvector (324 lines) | Nothing (Irrlicht has no portal system) |
| UI windows | 40+ `WindowBase` classes, `WindowManager` | `IGUIEnvironment` (19,689 lines, unused) |
| Zone rendering | Manual front-to-back sort via `RenderPassTimer` hook | Scene graph BFS order |
| Asset loading | Custom S3D/WLD/EQG loaders | 18 mesh format loaders (all unused) |
| Shaders | 6 built-in GLSL ES 1.0 programs + zone shader system | Fixed-function pipeline |

### Irrlicht Dead Weight

Irrlicht 1.8.5 is 204,693 lines across 488 files. Of that, ~27.8% (57,000+ lines) is completely dead code for WillEQ: DirectX 8/9 renderers, software renderer, 18 unused mesh format loaders, 10 unused image loaders, the entire GUI widget toolkit, unused scene node types, and unused animator types. The library is linked as a precompiled system package with no selective compilation.

## Performance Overhead from Irrlicht

### Scene Graph Traversal (Per Frame)

`smgr_->drawAll()` executes a fixed 7-pass traversal:

1. `ESNRP_CAMERA` — reset matrices
2. `ESNRP_LIGHT` — light registration + sorting
3. `ESNRP_SKY_BOX` — sky rendering
4. `ESNRP_SOLID` — zone geometry + entities
5. `ESNRP_SHADOW` — shadow rendering
6. `ESNRP_TRANSPARENT` — transparent entities
7. `ESNRP_TRANSPARENT_EFFECT` — particle effects

Per pass, Irrlicht calls `OnAnimate()` on every node (including invisible ones), then `OnRegisterSceneNode()` on every visible node to categorize by material transparency, then fires LightManager hooks (`OnRenderPassPreRender`, `OnNodePreRender`, `OnNodePostRender`, `OnRenderPassPostRender`) per node. For 40 visible entities, that's 160+ virtual calls just for hooks, plus animate/register on every node in the graph.

### Virtual Dispatch Overhead

Each entity render calls `setTransform()`, `setMaterial()`, `drawMeshBuffer()` through the `IVideoDriver` virtual interface per mesh buffer. A humanoid entity with 4 equipment pieces has ~8-12 mesh buffers. With 40 visible entities: **40 × 10 buffers × 3 virtual calls = 1,200 virtual dispatches/frame** just for entity rendering.

Additional overhead per `drawMeshBuffer()`:
- Hashmap lookup (`HWBufferMap.find(mb)`) per call
- `restore3DProjection()` recomputes matrix products from cached transforms
- `glVertexAttribPointer` × 4 per draw call even with identical vertex formats
- No cross-buffer batching of similar-topology buffers

### Zone Mesh Redundancy

Zone mesh nodes are registered in the scene graph but rendered manually via the `RenderPassTimer` hook in front-to-back order. The nodes are set invisible so `drawAll()` skips their actual rendering, but they still participate in tree traversal and node registration overhead.

### Material Transparency Check (Per Frame, Per Node)

`OnRegisterSceneNode()` iterates all materials on every visible node every frame to check `rnd->isTransparent()`, even though materials never change after load. This is pure waste.

### Visibility Decoupling

The SimulationWorker computes visibility (frustum + PVS) on a separate thread, but `drawAll()` still traverses all scene nodes regardless. Visibility results are applied after the fact (hiding nodes), not before (skipping traversal). Invisible nodes still incur full Irrlicht traversal overhead.

## Replacement Architecture

### Core Principle: Flat Render List, Not Scene Graph

EQ scenes are simple: static zone geometry (~100-200 mesh buffers), ~50 entities, ~20 doors, particles, sky. A flat `std::vector<DrawEntry>` sorted by (shader program, texture, VBO) replaces the entire scene graph. No tree traversal, no per-node virtual dispatch, no material categorization.

```
struct DrawEntry {
    uint8_t  shaderProgram;   // sort key 0
    uint16_t textureId;       // sort key 1
    uint32_t vboHandle;       // sort key 2
    uint32_t eboHandle;
    uint32_t indexOffset;
    uint32_t indexCount;
    glm::mat4 worldTransform;
    // material state (blend, depth, cull) encoded as flags
};
```

Sort once per frame, iterate linearly: bind shader if changed, bind texture if changed, bind VBO if changed, `glDrawElements`. State changes drop from thousands to dozens.

### Math Type Migration

Replace `irr::core::*` with GLM (already a dependency):

| Irrlicht | GLM Replacement |
|----------|----------------|
| `vector3df` | `glm::vec3` |
| `vector2df` | `glm::vec2` |
| `matrix4` | `glm::mat4` |
| `quaternion` | `glm::quat` |
| `aabbox3df` | Custom `AABB` (min/max vec3) |
| `rect<s32>` | Custom `Rect` or `glm::ivec4` |
| `dimension2d<u32>` | `glm::uvec2` |
| `SColor` | `uint32_t` (ARGB packed) or `glm::u8vec4` |
| `SColorf` | `glm::vec4` |

### Mesh/Buffer Types

Replace `SMesh`/`SMeshBuffer`/`S3DVertex` with GPU-ready structs:

```
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    uint32_t  color;  // RGBA packed
};

struct MeshBuffer {
    GLuint vbo;
    GLuint ebo;
    uint32_t indexCount;
    uint16_t textureId;
    uint8_t  materialFlags;  // blend mode, depth test, cull face
};
```

### Texture/Material System

Replace `ITexture`/`SMaterial` with thin GL wrappers. Most of `SMaterial`'s 20+ fields (fog, wireframe, lighting, texture layers 1-3, etc.) are unused. Only need: texture handle, blend mode, depth test/write, cull face, shader program ID.

### Transform System

Replace Irrlicht's scene node parent-child transforms with explicit transform computation. Entities need: world position + rotation → mat4. Equipment attachment: parent bone mat4 × local offset. Camera: view matrix from position + target. All computable with GLM directly.

## Work Chunks

### Chunk 1: Benchmark Framework (Prerequisite)

**Goal**: Build a zone benchmark tool that measures per-phase GPU timing, draw calls, state changes, and triangle counts against the current Irrlicht-based renderer. This establishes baselines before any changes.

**Scope**:
- New executable or `--benchmark` flag that loads a zone with simulated entities
- Configurable: entity count (0/10/25/50/100), features on/off (portal, PVS, sort, particles, per-pixel lighting), resolution
- Per-frame CSV output: total frame time, zone draw time, entity draw time, UI draw time, draw call count, state change count, triangle count, visible entity count, visible region count
- Warmup period (discard first N frames), steady-state measurement (M frames)
- No gameplay networking required — spawn fake entities at known positions with looping animations

**Key files**: `src/client/graphics/irrlicht_renderer.cpp` (frame timing hooks), `src/client/graphics/simulation_worker.cpp` (visibility stats), `docker/irrlicht-drm/COpenGLES2Driver.cpp` (draw call counting)

**Deliverable**: Baseline CSV data for qeynos2 at various entity counts and feature combinations on Orange Pi.

### Chunk 2: Math Type Migration

**Goal**: Replace all `irr::core::*` math types with GLM equivalents. Mechanical refactoring — no behavioral changes.

**Scope**:
- Create `include/client/graphics/math_types.h` with typedefs and conversion helpers
- Migrate `vector3df` → `glm::vec3` (heaviest usage: ~1,200 occurrences)
- Migrate `matrix4` → `glm::mat4`
- Migrate `quaternion` → `glm::quat`
- Migrate `aabbox3df` → custom `AABB` struct
- Migrate `rect<s32>`, `dimension2d<u32>`, `position2d<s32>` → simple structs or GLM types
- Migrate `SColor`/`SColorf` → packed uint32_t / glm::vec4
- Update all 86 affected files
- Ensure build compiles and all tests pass after each sub-batch

**Approach**: Do this in sub-batches by type (all vector3df first, then matrix4, etc.) to keep each change reviewable. The `irr::core::` prefix makes grep-and-replace straightforward. Some Irrlicht methods like `matrix4::transformVect()` need GLM equivalents (`glm::vec3(mat * glm::vec4(v, 1.0))`).

**Key risk**: `matrix4` is row-major in Irrlicht, column-major in GLM. Verify all matrix multiply order and `getTranslation()` / `setTranslation()` usage.

**Files affected**: ~86 files (2,115 occurrences). Start with `irrlicht_renderer.h/.cpp` (highest concentration), then `entity_renderer.cpp`, `simulation_worker.cpp`, `camera_controller.cpp`, `door_manager.cpp`, and work outward.

### Chunk 3: Custom Mesh and Material Types

**Goal**: Replace `SMesh`, `SMeshBuffer`, `S3DVertex`, `IMesh`, `IMeshBuffer`, `ITexture`, `SMaterial` with custom GPU-ready structs.

**Scope**:
- Define `Vertex`, `MeshBuffer`, `Mesh`, `Texture`, `Material` structs
- Replace mesh building in `zone_geometry.cpp`, `mesh_building.cpp`, `animated_mesh_creation.cpp`
- Replace texture references throughout (262 `ITexture` usages in `irrlicht_renderer.cpp` alone)
- Replace material usage (currently keyed on Irrlicht's `E_MATERIAL_TYPE` enums for shader selection)
- Update `constrained_texture_cache.cpp`, `detail_texture_atlas.cpp`, `detail_chunk.cpp`
- GPU buffer creation moves from Irrlicht's `createHardwareBuffer()` to direct `glGenBuffers`/`glBufferData`

**Key files**: `src/client/graphics/eq/zone_geometry.cpp` (28 mesh type usages), `src/client/graphics/eq/animated_mesh_scene_node.cpp` (28 usages), `src/client/graphics/detail/detail_chunk.cpp` (21 usages), `docker/irrlicht-drm/COpenGLES2Driver.cpp` (VBO management)

**Dependency**: Chunk 2 (math types) should be done first so vertex structs use GLM types.

### Chunk 4: Direct Render List (Replace drawAll)

**Goal**: Replace Irrlicht's 7-pass `smgr_->drawAll()` with a single-pass sorted draw list. This is the highest-impact single change for rendering performance.

**Scope**:
- Build `RenderList` class that collects `DrawEntry` structs from all visible objects
- Zone geometry: already manually sorted front-to-back via `RenderPassTimer` hook — lift this into the new render list directly
- Entities: collect visible entity mesh buffers into the render list (replaces `OnRegisterSceneNode` + `render()` virtual dispatch chain)
- Doors: same pattern as entities
- Sky: render first (or with depth test off), no sorting needed
- Transparent pass: separate list, sorted back-to-front
- Sort render list by (shader, texture, VBO) to minimize state changes
- Single linear iteration: bind state only on change, `glDrawElements` per entry

**Integration with visibility**: The SimulationWorker already computes PVS bitvectors and frustum culling results in `SimulationOutput`. Feed these directly into render list construction — only add visible objects. No more traversing invisible nodes.

**What this eliminates**:
- All 7 `drawAll()` render passes
- All `OnAnimate()` / `OnRegisterSceneNode()` / `render()` virtual calls
- All LightManager hook overhead (160-320 virtual calls/frame)
- Per-frame material transparency categorization
- Scene node registration/deregistration

**Key files**: `src/client/graphics/irrlicht_renderer.cpp` (main render loop at ~line 10143-10535, `RenderPassTimer` hook at ~line 5730-5890), `include/client/graphics/irrlicht_renderer.h` (scene manager references)

**Dependency**: Chunk 3 (custom mesh types) should be done first so the render list operates on custom types, not Irrlicht mesh buffers.

### Chunk 5: Entity Rendering Without Scene Nodes

**Goal**: Render entities directly without Irrlicht scene node wrappers. Entities become data (transform + mesh buffers + animation state) rather than objects in a scene graph.

**Scope**:
- `EntityVisual` struct holds: world transform (mat4), mesh buffer handles (GLuint VBO/EBO), animation state, visibility flag
- `SkeletalAnimator` already computes bone transforms and applies them to vertices (with SIMD) — this is independent of Irrlicht
- Remove `EQAnimatedMeshSceneNode` and `EQAnimatedMesh` Irrlicht subclasses
- Equipment attachment: compute attachment transform from parent bone matrix, no scene node parenting needed
- Entity name tags: move to the batched text renderer (see UI plan)
- Visibility: `FrustumCuller` + PVS already work independently of Irrlicht

**What this eliminates**:
- `IAnimatedMeshSceneNode` virtual dispatch per entity
- Scene node `OnAnimate()` traversal for entities
- `addAnimatedMeshSceneNode()` / `remove()` lifecycle management
- Parent-child node transform propagation (replaced by explicit mat4 multiply)

**Key files**: `src/client/graphics/entity_renderer.cpp` (241 `irr::` usages), `include/client/graphics/eq/animated_mesh_scene_node.h`, `src/client/graphics/eq/animated_mesh_scene_node.cpp`

**Dependency**: Chunks 2-4 (math types, mesh types, render list).

### Chunk 6: Remove Scene Manager and Device Dependencies

**Goal**: Remove `ISceneManager`, `IVideoDriver`, `IrrlichtDevice` and replace with direct EGL/GLES2 context management.

**Scope**:
- Device lifecycle: `CIrrDeviceFB` already manages DRM/KMS/EGL. Extract the EGL context setup, DRM page flip, and evdev input polling into standalone code (no Irrlicht base class).
- Video driver: `COpenGLES2Driver` already implements all GL calls. Extract into standalone class that doesn't inherit from `CNullDriver`.
- Scene manager: fully replaced by the render list (Chunk 4).
- Timer: replace `device_->getTimer()->getTime()` with `std::chrono::steady_clock` (5 files, 21 usages).
- Remove Irrlicht library linkage from CMakeLists.txt.
- Remove all `#include <irrlicht.h>` (106 files).

**Key files**: `docker/irrlicht-drm/CIrrDeviceFB.cpp` (DRM device), `docker/irrlicht-drm/COpenGLES2Driver.cpp` (GLES2 driver), `CMakeLists.txt` (library linkage)

**Dependency**: All previous chunks. This is the final removal step.

### Chunk 7: Instrumentation Layer

**Goal**: Add comprehensive per-frame metrics that were impossible with Irrlicht's black box.

**Scope**:
- GPU timing queries (`GL_EXT_disjoint_timer_query` if available, otherwise CPU-side `glFinish` bracketing) per render phase: zone draw, entity draw, sky, transparent, particles, UI, text
- Draw call counter: increment per `glDrawElements`/`glDrawArrays` call
- State change counter: increment per shader bind, texture bind, VBO bind, blend mode change
- Triangle counter: sum index counts / 3
- Per-frame stats struct written to ring buffer, optionally dumped to CSV
- HUD overlay showing real-time stats (draw calls, tris, state changes, phase timings)
- `/perf` slash command to toggle overlay, `/benchmark` to dump CSV

**Key files**: New `include/client/graphics/render_stats.h`, integrated into the render list iteration loop.

**Dependency**: Chunk 4 (render list) at minimum. Best done after Chunk 6 (full removal) so all rendering goes through instrumented paths.

## Execution Order

```
Chunk 1 (Benchmark)     — standalone, do first for baselines
    ↓
Chunk 2 (Math types)    — mechanical refactoring
    ↓
Chunk 3 (Mesh/material) — depends on math types
    ↓
Chunk 4 (Render list)   — highest-impact change, depends on mesh types
    ↓
Chunk 5 (Entities)      — depends on render list
    ↓
Chunk 6 (Remove Irrlicht) — final cleanup, depends on everything
    ↓
Chunk 7 (Instrumentation) — can start after Chunk 4, finalize after Chunk 6
```

Each chunk should leave the project in a buildable, runnable state. Run the benchmark (Chunk 1) after each subsequent chunk to measure the impact.

## Risk Notes

- **Matrix conventions**: Irrlicht `matrix4` is row-major, GLM `mat4` is column-major. Every matrix multiply and transform must be verified during Chunk 2. Write a test that compares Irrlicht and GLM results for key operations (transform vertex, multiply matrices, extract translation/rotation).
- **Irrlicht internal state**: Some Irrlicht code (particularly `CNullDriver` base class methods) maintains internal state that the GLES2 driver depends on (texture tracking, primitive counts, transform cache). When extracting the driver in Chunk 6, audit all `CNullDriver` method calls in `COpenGLES2Driver`.
- **Scene node lifetimes**: Irrlicht manages node memory via `grab()`/`drop()` reference counting. When removing scene nodes (Chunk 5), ensure all entity/door/particle lifetime management is handled explicitly.
- **Text rendering**: Name tags currently use `CTextSceneNode` which participates in the scene graph. Must be replaced before the scene graph is removed (Chunk 5). This is covered in the UI plan (`docs/future/ui_fixes.md`).
