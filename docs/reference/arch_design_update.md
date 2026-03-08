# willeq architecture — updated design

This document extends arch_design.md with clarifications, additional detail, and design
decisions made during review. It serves as the foundation for a comprehensive code audit
and refactoring effort.


## 1. Core principles

These principles are non-negotiable and apply everywhere in the codebase.

1. **Constrained limits are hard maximums.** Every subsystem must track its resource usage
   and never exceed its configured budget. There is no "best effort" — exceeding a limit
   is a bug.

2. **Gameplay never depends on background work.** The game is 100% playable with only
   HCMap, BSP, and door data loaded. Everything else (zone meshes, textures, entity models,
   lights, effects) is cosmetic enhancement. No thread stall, loading delay, or resource
   exhaustion may block gameplay.

3. **One unified priority system governs all work across all threads.** There are no
   per-thread priority schemes. Every thread consults the same priority ordering when
   choosing what to work on next.

4. **Placeholders are the universal fallback.** Any asset that fails to load, hasn't loaded
   yet, or was evicted is represented by a placeholder. Textures fall back to
   gradients/solid colors. Meshes fall back to colored cubes. The scene is always renderable.


## 2. Unified priority system

All work across all threads is ordered by a single, universal priority scheme with two axes.

### 2.1 Primary axis: distance from player (PVS depth)

The player's current PVS region is depth=0. Adjacent connected regions are depth=1. This
radiates outward until the configured maximum depth or resource exhaustion.

- Depth=0 work always runs before depth=1 work, regardless of asset type or thread.
- When the player moves to a new PVS region, all pending work is re-prioritized based on
  the new position. In-flight work completes (cannot cancel a half-decoded texture), but the
  next item selected is always based on current player position.
- This applies to loading AND eviction: load nearest first, evict farthest first.

### 2.2 Secondary axis: asset type (tiebreaker within same distance)

When multiple assets at the same PVS depth are pending, asset type determines order:

1. Zone mesh geometry — structural, most visually impactful
2. Zone textures — fills in the structure
3. Door models/textures — interactive, gameplay-adjacent
4. Entity meshes — NPCs/players (visible as cubes otherwise)
5. Entity textures — cosmetic detail on entities
6. Lights and effects — purely aesthetic

This ordering reflects visual impact and gameplay relevance. An untextured zone mesh is
more useful than a fully textured entity at the same distance.

### 2.3 How threads consume priority

Every thread pulls its next work item from the same priority ordering, filtered to the
work types appropriate for that thread:

- **Background thread**: file I/O, S3D parsing, texture decoding, mesh building, entity prep
- **GPU upload thread**: buffer uploads, texture uploads to GPU memory
- **Simulation thread**: entity state updates, animation, spatial queries
- **Render thread**: only work that cannot be offloaded (scene graph updates, draw calls)

No thread should be working on a depth=5 asset while there is pending depth=1 work
anywhere in the system that the thread is capable of performing.

### 2.4 Re-prioritization on player movement

When the player enters a new PVS region:

1. The priority ordering is recalculated based on new PVS distances.
2. All pending work items are re-sorted.
3. In-flight work is not interrupted — it finishes, then the next pick uses the new order.
4. Assets that were recently loaded but are now far away become LRU eviction candidates
   if memory pressure requires it.

This means fast travel (e.g., teleportation across the zone) naturally causes a full
re-prioritization: everything nearby the old position becomes low-priority or evictable,
and assets near the new position jump to the front of the queue.

### 2.5 Edge cases

**Fast travel:** Full re-prioritization. The LRU eviction handles freeing memory from the
old location. Loading begins from the new position outward. The player sees placeholders
until assets load — this is expected and correct.

**Resource exhaustion while stationary:** The lazy-loader stops loading when budgets are
full. Nearby assets are already loaded (they were prioritized first). Distant assets
remain as placeholders. If the player later moves toward them, re-prioritization may
cause closer assets to be evicted to make room.

**Adjacent zones sharing assets:** Cache items (textures, character models) persist across
zone transitions when appropriate. The LRU eviction naturally retains shared assets that
remain referenced and evicts zone-specific assets that are no longer needed.


## 3. Thread architecture

### 3.1 Thread roles

| Thread | Priority | Responsibility | Rules |
|--------|----------|---------------|-------|
| Render | Highest (real-time) | Draw calls, scene graph updates | Governor GREEN required; 1-frame-per-GREEN; no file I/O; no GPU memory allocation |
| GPU Upload | Second | GPU memory operations (texture upload, VBO upload) | Only thread allowed to allocate/free GPU memory; work items come from priority queue |
| Simulation | Third | Entity state, animation, spatial queries | One task at a time; blocking work moves to background |
| Background | Lowest | File I/O, decoding, parsing, mesh building, entity prep | One step per loop iteration; tasks broken into fine-grained steps |

### 3.2 Configurable thread counts

The number of simulation and background threads is configurable per hardware tier.

- **Low-end (Orange Pi):** 1 simulation thread, 1 background thread
- **Mid-range:** 1 simulation thread, 2 background threads
- **High-end:** 2 simulation threads, 4 background threads

When multiple background threads exist, they share a single priority-ordered work queue.
Each worker pulls the next highest-priority item. This requires thread-safe budget
accounting — atomic checks or a mutex around allocation/eviction to prevent overcommit
when multiple workers allocate simultaneously.

The render thread and GPU upload thread are always single-instance. These touch the GL
context and cannot be parallelized.

### 3.3 No ad-hoc thread creation

No subsystem may spawn its own threads. All concurrent work flows through the configured
thread pools. Systems that currently have their own threads (EntityPrepWorker, spell icon
loader, and any others identified during audit) must be refactored to submit work items
to the background thread's work queue.

This is a hard rule. It ensures:
- Thread count is predictable and configurable
- Priority ordering is respected globally
- Resource accounting is centralized
- No hidden concurrency bugs from untracked threads

### 3.4 Background thread task granularity

Background thread tasks must be broken into the smallest reasonable steps, with one step
executed per thread loop iteration. This prevents any single task from monopolizing the
thread and allows re-prioritization to take effect between steps.

Some operations cannot be subdivided (e.g., a single file read from an S3D archive). These
are acceptable as atomic steps even if they take 500ms+. The system tolerates this because
gameplay never depends on background thread output — the player continues playing with
placeholders.

The priority queue is checked between every step. A long-running multi-step task (e.g.,
decoding 20 textures for a region) can be interleaved with higher-priority work that
arrives mid-task.


## 4. Zone loading

### 4.1 Setup state (synchronous, all display modes)

Triggered by receiving the ClientUpdate packet with the player's spawn ID.

Loads synchronously before gameplay begins:
- HCMap data (zone collision geometry)
- BSP region data (PVS connectivity)
- Door data (positions, states)

This is the minimum required for gameplay. It blocks until complete. On all hardware tiers
this load is fast because it's lightweight data (no textures, no 3D meshes).

After setup completes, willeq transitions to the in-game state.

### 4.2 Automatic loading mode (3D graphical output)

Used for normal gameplay. The expected mode for most players.

1. Loading screen is shown.
2. Loading phases execute sequentially via the background thread work queue.
3. Governor GREEN and 1-frame-per-GREEN requirements do NOT apply during automatic loading.
4. PVS depth is configurable per hardware tier. On high-end machines, depth=ALL pre-loads
   the entire zone. On constrained devices, depth may cover half the zone or less.
5. When all configured phases complete, the loading screen is dismissed and the "instant
   scene" is rendered with all pre-loaded assets.
6. Any assets beyond the configured PVS depth are loaded via lazy-loading during gameplay.

### 4.3 Manual loading mode (3D graphical output)

Used for debugging and development.

1. No loading screen. The "instant scene" renders immediately using HCMap terrain geometry
   and placeholder cubes for all entities.
2. Loading phases are triggered individually via `/load <phase>` commands.
3. The game is fully playable throughout — manual loading is a development tool, not a
   player-facing workflow.

### 4.4 Unified loading code paths

Automatic and manual modes must use the same loading functions, distinguished by a mode
flag. The only behavioral difference is:
- Automatic: loading screen shown, phases advance automatically
- Manual: no loading screen, phases advance on command

This eliminates the current duplication between pre-loading and in-game loading routines.
Bug fixes apply once, to one code path.

### 4.5 The "instant scene"

The instant scene is the first frame of actual gameplay rendering. It is guaranteed to have:
- HCMap collision geometry (always available — loaded in setup state)
- BSP/PVS data (always available — loaded in setup state)
- Door data (always available — loaded in setup state)
- Zone meshes and textures up to the configured PVS depth (automatic mode)
- OR HCMap terrain with placeholders for everything (manual mode)

The instant scene is always renderable. Missing assets are represented by placeholders.
There is no state where the scene cannot be drawn.

### 4.6 In-game lazy loading

Once gameplay starts, all asset management uses a single strategy: lazy-load within
constraints.

The lazy-loader controls two things:
1. **Loading rate:** One item at a time per background worker thread (serialized within
   each worker). This prevents resource spikes and keeps frame times stable.
2. **Amount loaded:** Up to the configured PVS depth from the player's current position,
   bounded by memory budgets.

As the player moves, the lazy-loader:
- Re-prioritizes pending work based on new PVS distances
- Begins loading newly-in-range assets (nearest first, by asset type priority)
- Marks out-of-range loaded assets as eviction candidates in the LRU caches

### 4.7 Zone transitions

Zone transition code is mature and working. Key properties:
- Each zone event type is handled with its own logic
- Cache items (textures, character models) may persist across transitions when shared
- LRU eviction naturally handles cleanup — zone-specific assets lose references and get
  evicted as new zone assets load
- The setup state (HCMap/BSP/doors) runs synchronously for the new zone before gameplay
  resumes


## 5. Two-layer asset architecture: prep vs render

Asset management is split into two independent layers with placeholders bridging the gap.

### 5.1 Prep layer (background, distance-gated)

The prep layer controls what gets loaded from disk, decoded, cached in memory, and
potentially uploaded to GPU. It is bounded by per-asset-type PVS distance settings in the
constrained config:

- **Terrain prep distance** — how far (in PVS region hops) zone mesh geometry is pre-built
- **Object prep distance** — how far door/placeable models and textures are loaded
- **Entity prep distance** — how far NPC/player meshes and textures are loaded

These distances are tuned per hardware tier. On Orange Pi, they stay tight to fit the
~128 MB RAM budget. On a desktop, they can extend to cover the full zone.

The prep layer runs on background threads using the unified priority system (section 2).
It never blocks gameplay or rendering.

### 5.2 Render layer (per-frame, visibility-gated)

The render layer controls what gets drawn each frame from whatever is currently available —
prepped or not. It uses:

- Clip distance (far plane)
- PVS visibility (BSP region connectivity)
- Frustum culling (camera view cone)
- Portal/stencil occlusion (indoor zones)

The render layer operates independently of the prep layer. It does not wait for assets and
does not skip geometry that hasn't been prepped. Instead, it always renders the full visible
set using whatever representation is available.

### 5.3 Placeholders bridge the gap

The render distance can (and often does) exceed the prep distance. When the render layer
encounters an asset that hasn't been prepped, it renders a placeholder:

| Asset | Prepped state | Placeholder (not yet prepped) |
|-------|--------------|-------------------------------|
| Zone terrain | Full textured mesh | Height-based vertex coloring from HCMap data |
| Zone textures | Real textures | Gradient or solid color |
| Objects/doors | Real model + texture | Colored cube |
| Entity meshes | Real race model | Colored cube (race-tinted) |
| Entity textures | Real textures | Solid color matching race |
| Lights | Real point lights | Omitted (no contribution) |
| Effects | Real particles | Omitted |

This means:
- The scene is **always fully renderable** regardless of prep state
- The camera can move freely — placeholders appear at the edges of prep coverage
- As background loading catches up, placeholders are seamlessly replaced with real assets
- On constrained devices with tight prep distances, placeholder pop-in is expected and correct
- On powerful devices with wide prep distances, placeholders are rarely visible

### 5.4 Key invariant

**Prep distance does NOT constrain render distance.** The render layer draws everything
the visibility system says is visible. The prep layer determines the *quality* of what's
drawn (real asset vs placeholder), not *whether* it's drawn.


## 6. Rendering pipeline rules

### 6.1 Render thread

- Must have governor GREEN to do work
- May consume at most 1 frame of time per GREEN state
- Limited to operations that cannot be offloaded: draw calls, scene graph node
  visibility changes, reading back results from GPU upload thread
- No file I/O
- No GPU memory allocation/deallocation (this goes through GPU upload thread)
- No blocking waits on other threads

### 6.2 GPU upload thread

- Second highest priority
- Only active when GLES2 is enabled (owns the shared EGL context)
- The sole path for GPU memory operations: texture uploads, VBO creation, buffer updates
- Work items arrive from the priority queue (typically prepared by background thread)
- The render thread never blocks waiting for an upload — it renders with whatever is
  available (placeholders if the upload hasn't completed)

### 6.3 Resource availability contract

The render thread and GPU upload thread communicate through a ready/not-ready state per
asset. The contract is:

- **Not ready:** Render with placeholder (cube, gradient, solid color). No blocking, no
  waiting, no retry loop. The asset will arrive when priority and budgets allow.
- **Ready:** Render with the real asset.

This means the render thread never stalls on asset availability. The scene is always
drawable at full frame rate regardless of loading state.


## 7. Error handling and degradation

Every asset type has a defined fallback:

| Asset | Fallback on failure | Status |
|-------|-------------------|--------|
| Zone mesh | Height-based vertex coloring from HCMap data | Implemented |
| Zone texture | Solid color or gradient | Implemented |
| Entity mesh | Colored cube (race-tinted) | Implemented |
| Entity texture | Solid color matching race | Implemented |
| Door/object model | Colored placeholder cube | **TODO** |
| Light | Omitted (no light contribution) | Implemented |
| Effect/particle | Omitted | Implemented |
| Sound | Silent | Implemented |

Failures are logged but never escalate to crashes or gameplay interruption. A fully
degraded scene (all placeholders) is a valid and playable state.


## 8. Current codebase issues (for audit)

These are known issues to be addressed during the refactoring effort.

### 8.1 Duplicate loading code paths

Pre-loading (automatic mode) and in-game loading (lazy-load) have significant overlap and
duplication. Refactor into unified functions with a mode flag.

### 8.2 Lazy-loader rate control

The lazy-loader needs tight rate control: one item at a time per worker. The priority
system (section 2) must be fully integrated so that loading decisions respect both distance
and asset type ordering.

### 8.3 Unauthorized thread creation

Several subsystems create their own threads (EntityPrepWorker, spell icon loader, possibly
others). These must be identified during audit and refactored to use the background thread
work queue.

### 8.4 Dead code

The current branch has accumulated dead code from incremental changes. Audit should
identify and remove unreachable code, unused functions, and obsolete loading paths that
have been superseded.

### 8.5 Priority system implementation

The unified priority system described in section 2 does not currently exist as a coherent
implementation. The audit should identify all places where work is queued or scheduled and
map them to the unified priority model.

### 8.6 Budget accounting under concurrent workers

If configurable thread counts (section 3.2) are implemented, all budget checks
(texture cache, mesh cache, GPU memory) need thread-safe accounting. The audit should
identify all budget-checked allocations and verify they are safe under concurrent access.


## 9. Audit scope

The code audit should answer the following questions:

1. **Thread inventory:** What threads currently exist? Which are sanctioned (render, GPU
   upload, simulation, background) and which are ad-hoc (EntityPrepWorker, spell icon
   loader, others)?

2. **Work scheduling:** Where is work currently queued or scheduled? Is it using the
   priority system or ad-hoc ordering? What scheduling mechanisms exist (queues, callbacks,
   direct calls)?

3. **Budget tracking:** Which subsystems track their memory/resource usage? Are there
   subsystems that allocate without checking budgets? Are budget checks thread-safe?

4. **Loading code paths:** Map all paths from "asset needed" to "asset rendered." Identify
   duplication between pre-load and lazy-load paths. Identify which paths respect
   constrained limits and which bypass them.

5. **Placeholder coverage:** For every asset type, verify that a placeholder fallback
   exists and is used when the asset is unavailable. Identify any code paths that assume
   an asset is available without checking.

6. **Dead code:** Identify functions, classes, and code paths that are unreachable or
   superseded by the current architecture.

7. **Governor compliance:** Verify that all render thread work respects the governor GREEN
   and 1-frame-per-GREEN requirements. Identify any render thread code that does file I/O,
   blocking waits, or GPU memory allocation directly.
