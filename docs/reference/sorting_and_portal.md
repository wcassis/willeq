# WillEQ GLES 2.0 Renderer — Implementation Handoff: Front-to-Back Sorting & Portal Occlusion

## Project Context

WillEQ is an EverQuest client (`github.com/wcassis/willeq`) built on Irrlicht 1.8.5. We are implementing a native OpenGL ES 2.0 renderer to replace the previous path that routed desktop OpenGL 2.1 through Mesa's Lima driver on ARM Mali hardware. The GLES 2.0 renderer has already yielded a 50-60% performance improvement (30fps → 45-48fps) on the primary development target, an Orange Pi One (Allwinner H3, Mali-400 single fragment core) at 1280x720@16bpp.

The game targets classic EverQuest content through the Velious expansion (original models only, no Luclin-era replacements). Target framerate is 30fps.

### Hardware Targets

All hardware supports GLES 2.0. The Mali-400 is the performance baseline:

- **Orange Pi One**: Allwinner H3, Mali-400 (1 fragment core), 512MB RAM
- **Orange Pi Zero**: Allwinner H2+/H3, Mali-400MP2 (2 fragment cores), 256MB RAM
- **Raspberry Pi 1**: Broadcom BCM2835, VideoCore IV, 256/512MB RAM
- **MK808**: Rockchip RK3066, Mali-400MP4 (4 fragment cores), 1GB RAM
- **Odroid XU4**: Exynos 5422, Mali-T628 MP6 (Midgard, 6 cores), 2GB RAM — supports GLES 3.1
- **Mali-G310 device**: Valhall architecture, single core — supports GLES 3.2
- **Various Android 4.4 devices**: Galaxy Tab 10.1, Verizon 7" tablet, Android STB

### Current Renderer Capabilities (confirmed working on Mali-400)

- Native GLES 2.0 via EGL/DRM on Linux SBCs
- ETC1 hardware-decoded texture compression (converted from original DXT1 assets; 90% of textures have no alpha, remaining 10% use a separate alpha texture atlas)
- Texture atlasing for draw call batching
- Per-vertex lighting via custom shaders
- CMA-optimized contiguous memory allocation (avoids GPU buffer copies)
- Animated textures (simple multi-frame loops)
- Mipmapping, bilinear filtering, NPOT texture support
- Stencil buffer (8-bit, D24S8 packed — confirmed via EGL config with `EGL_STENCIL_SIZE, 8`)
- Framebuffer objects (core GLES 2.0, not extension-based)
- Render-to-texture support

### Existing Culling Pipeline

The renderer already implements several culling stages:

1. **PVS (Potentially Visible Set)**: Uses EQ's native zone PVS data to determine sector/region visibility
2. **Frustum culling**: Camera frustum rejection with spatial data structure
3. **Software occlusion buffer**: 128x64 CPU-side depth buffer for occluder testing against object AABBs

### Key Constraint: Draw Call Budget

The Mali-400 performs best with fewer than 100-150 total draw calls per frame. Every optimization should aim to reduce or avoid adding draw calls. Driver overhead per draw call is significant on the single-core Allwinner H3 CPU.

---

## Implementation 1: Front-to-Back Opaque Sorting

### Problem

Irrlicht 1.8.5 does **not** sort opaque geometry front-to-back. It categorizes scene nodes into render buckets (solid, transparent, shadow) and sorts transparent nodes back-to-front for correct alpha blending, but solid/opaque nodes render in scene graph traversal order, which is arbitrary relative to the camera. This means the Mali-400's tile-based hidden surface removal and early-Z rejection are not operating optimally — distant geometry may be shaded before nearby geometry populates the depth buffer, resulting in wasted fragment work.

### Why This Matters on This Hardware

The Mali-400 is a tile-based deferred renderer. It processes the framebuffer in 16x16 pixel tiles and performs per-tile hidden surface removal before fragment shading. If the depth buffer already contains nearby geometry when distant geometry is submitted, the hardware can reject hidden fragments before they reach the fragment shader. Without front-to-back sorting, this rejection is largely random and a significant amount of overdraw occurs. On a GPU with a single fragment processor at 720p, fill rate is the primary bottleneck, so minimizing wasted fragment work directly translates to performance headroom.

### Implementation Approach

This should be implemented within the GLES 2.0 renderer's draw submission path, after culling but before issuing draw calls.

**Step 1: Compute sort keys for visible opaque objects.** After PVS, frustum, and occlusion culling have produced the set of visible objects, compute a distance value for each opaque object. Use the squared distance from the camera position to the object's AABB center — no need for a square root since we're only comparing relative distances.

```
sort_key = (cam.x - obj_center.x)^2 + (cam.y - obj_center.y)^2 + (cam.z - obj_center.z)^2
```

**Step 2: Sort opaque draw list by sort key, ascending (nearest first).** A simple `std::sort` on the draw list is fine. The draw list should be in the range of tens to low hundreds of items after culling, so sort cost is negligible.

**Step 3: Submit opaque draws in sorted order.** Render nearest objects first so their depth values populate the depth buffer before distant geometry is submitted.

**Step 4: Submit transparent draws in reverse order (back-to-front, farthest first).** Irrlicht already does this for its transparent bucket — preserve this behavior. Transparent objects must be drawn after all opaque geometry and sorted back-to-front for correct alpha blending.

### Refinements

- **Material/texture batching as secondary sort key**: Within similar depth ranges, grouping objects that share the same texture atlas page or shader reduces state changes. A combined sort key encoding depth in the high bits and material ID in the low bits can achieve both goals in a single sort. However, on the Mali-400, minimizing overdraw via depth ordering is more important than minimizing state changes, so depth should be the primary sort axis.
- **Zone terrain**: Zone BSP geometry (walls, floors, ceilings) should be included in the sort. Terrain chunks that are close to the camera are the most valuable early depth contributors since they cover large screen areas.
- **Static sort optimization**: For static geometry that doesn't move, the sort order only changes when the camera moves. You could cache the sorted order and re-sort only when the camera has moved beyond a threshold, but given the small draw list size this is likely unnecessary.

### Expected Impact

On tile-based Mali hardware with a single fragment core, front-to-back sorting typically reduces fragment workload by 20-40% in scenes with significant depth complexity. Indoor zones with layered rooms and corridors benefit most. The improvement is "free" in the sense that it adds negligible CPU cost (sorting a small list) while reducing GPU fragment work.

### Validation

Compare frame times on the same scene with and without front-to-back sorting. A zone like Lower Guk or Kael Drakkel with overlapping geometry layers visible from certain camera positions should show a measurable improvement. You can also use a debug visualization that colors fragments by overdraw count (render each fragment as an additive blend of a flat color — brighter areas = more overdraw) to visually confirm the reduction.

---

## Implementation 2: Stencil-Based Portal Occlusion

### Problem

EQ's indoor zones use a BSP/portal architecture. Rooms are connected by portals (doorways, corridors, windows). When the player stands in one room, geometry in adjacent rooms is only visible through the portal openings. Without portal-based rendering, the entire visible set from the PVS is rendered, including room geometry that is technically in the PVS but actually hidden behind walls from the camera's perspective (PVS is conservative — it includes everything *potentially* visible, not just what's *actually* visible through portals from the current camera position).

### Why Stencil-Based Portal Rendering

The stencil buffer allows us to mask rendering to only the screen-space region visible through each portal. Fragments that fall outside the portal silhouette are rejected by the stencil test before the fragment shader runs, saving fill rate. On the Mali-400, this is significant in dungeon zones where narrow doorways and corridors mean large portions of adjacent rooms are not actually visible — potentially 50-80% of a room's screen coverage could be masked by the stencil.

### Confirmed Hardware Support

The Mali-400 has an 8-bit hardware stencil buffer. Our EGL configuration requests it as part of a D24S8 packed depth/stencil format, which costs no additional memory over depth-only. The renderer's feature detection confirms stencil support is available:

```
[2026-02-20 16:51:58.849] [DEBUG] [GRAPHICS] [GL]   Stencil buffer: yes
```

### Algorithm

The rendering proceeds recursively from the room the camera is in, through visible portals into adjacent rooms.

**Phase 1: Determine the current room.** Use the camera position against the BSP to determine which room/sector the player is in.

**Phase 2: Render the current room normally.** Draw all geometry in the current room with no stencil masking (or with the stencil set to a base value). This room is fully visible.

**Phase 3: For each portal in the current room that passes frustum culling:**

1. **Clear/set the stencil for this portal.** Render the portal polygon (the doorway/opening quad) with:
   - Color writes disabled (`glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE)`)
   - Depth writes disabled (`glDepthMask(GL_FALSE)`)
   - Stencil test: always pass
   - Stencil op: replace stencil value with a unique portal ID (or simply increment)
   
   This writes the portal's screen-space silhouette into the stencil buffer.

2. **Render the room beyond the portal** with stencil testing enabled:
   - Stencil test: pass only where stencil equals the portal's ID
   - Re-enable color writes and depth writes
   - Draw all geometry in the adjacent room
   
   Fragments outside the portal opening fail the stencil test and are discarded before fragment shading.

3. **Recursion**: If the adjacent room has its own portals leading to further rooms, and those portals are visible within the current stencil mask, recurse. The stencil mask naturally narrows with each level of recursion (a portal seen through a portal).

**Phase 4: Restore state.** After all portal rendering is complete, clear the stencil buffer (or the relevant bits) and proceed with transparent geometry, particles, etc.

### Stencil Value Management

With an 8-bit stencil buffer, you have values 0-255. A simple approach:

- **Value 0**: Default / not visible through any portal
- **Values 1-N**: Each active portal in the current frame gets an incrementing value
- **Recursion**: For portals-through-portals, use `GL_INCR` on the stencil op at each recursion level. A fragment must pass through all portal levels to reach the required stencil value. This naturally handles nested portal visibility.

In practice, EQ zones rarely have more than 2-3 levels of portal nesting visible at once, so stencil value exhaustion is not a concern.

### Recursive Rendering with Stencil Nesting

For nested portals (room A → portal → room B → portal → room C), use an incrementing stencil approach:

```
Render Room A (stencil ref = 0, test = ALWAYS)
  For each portal P1 from Room A:
    Render P1 polygon (stencil op = INCR, ref doesn't matter, test = EQUAL to current level)
    Render Room B (stencil test = EQUAL to 1)
      For each portal P2 from Room B visible within P1's mask:
        Render P2 polygon (stencil op = INCR, test = EQUAL to 1)
        Render Room C (stencil test = EQUAL to 2)
    Render P1 polygon again (stencil op = DECR) to "pop" the stencil level
```

The increment/decrement approach acts like a stack. Each portal entry increments, each portal exit decrements, so the stencil value at any pixel reflects how many portals deep that pixel is visible through.

### Recursion Depth Limit

Cap recursion at 3-4 levels. Beyond this, the portals are small on screen and the geometry behind them is minimal. The draw call cost of additional recursion levels outweighs the fill-rate savings from stencil masking at that point. This also bounds the worst case for draw calls contributed by the portal system.

### Integration with Existing Culling Pipeline

Portal rendering integrates into the existing culling pipeline as follows:

1. **PVS** determines the set of rooms/sectors that are potentially visible — this is the outer bound
2. **Portal stencil rendering** further restricts which rooms are actually rendered and masks the screen region for each room — this operates within the PVS set
3. **Frustum culling** is applied to each portal polygon before recursing through it — if the portal isn't in the view frustum, skip that branch entirely
4. **Software occlusion buffer** can be applied to objects within each room after the portal mask is established
5. **Front-to-back sorting** (Implementation 1) applies within each room's draw list

### Integration with Front-to-Back Sorting

Within each room rendered through a portal, the opaque geometry for that room should be sorted front-to-back relative to the camera before submission. The combination is especially powerful: the stencil mask prevents shading fragments outside the portal opening, and front-to-back sorting minimizes overdraw within the visible region.

### Performance Considerations for Mali-400

- **Portal polygon rendering** (the stencil write passes) are very cheap — color and depth writes are disabled, so only the stencil buffer is updated. These are essentially free on the fragment side.
- **Each room rendered through a portal** is an additional set of draw calls. Keep the recursion depth limited so this doesn't blow the draw call budget.
- **Stencil clear**: Rather than clearing the entire stencil buffer between frames with `glClear(GL_STENCIL_BUFFER_BIT)`, you can clear it as part of the combined depth/stencil clear you're already doing at frame start. On Mali-400 with D24S8, this is a single operation.
- **Zone-specific enablement**: Portal rendering only benefits indoor/dungeon zones with portal geometry. Outdoor zones with wide-open BSP regions don't have useful portals. Detect zone type and skip the portal system for outdoor zones, falling back to PVS + frustum + occlusion buffer only.

### Which EQ Zones Benefit Most

Portal occlusion has the highest impact in zones with narrow portals and dense room geometry:

- **Classic**: Befallen, Blackburrow, Lower Guk, Upper Guk, Najena, Solusek's Eye, Cazic-Thule temple interior, Permafrost
- **Kunark**: Sebilis, Howling Stones, Karnor's Castle, Mines of Nurga, Droga
- **Velious**: Kael Drakkel interior, Temple of Veeshan, Thurgadin, Crystal Caverns, Velketor's Labyrinth

These are also the zones most likely to stress the Mali-400, since indoor zones tend to have the highest depth complexity (overlapping rooms, multi-level geometry). The portal occlusion directly addresses the primary bottleneck.

### Validation

Test in a dungeon zone with a clear portal scenario (e.g., standing in a room in Lower Guk looking through a single doorway). Compare frame times with and without the portal stencil system. The debug overdraw visualization mentioned in the front-to-back sorting section will also show the stencil mask's effect — areas outside portal openings should show zero fragment work for room geometry beyond the portal.

---

## Implementation Order

1. **Front-to-back sorting first.** It's simpler, affects all zones (indoor and outdoor), and provides immediate measurable improvement. It also establishes the sorted draw submission infrastructure that the portal system builds on.

2. **Portal occlusion second.** It's more complex, requires integration with EQ's zone portal data, and primarily benefits indoor zones. Implement it once sorting is working and validated.

### Testing Baseline

Current performance on Orange Pi One (Mali-400, single fragment core):
- 45-48fps sustained in simple scenes at 1280x720@16bpp
- Target framerate: 30fps
- Polygon counts: 5k-10k typical
- Texture memory: under 200MB with 256x256 uncompressed; significantly lower with ETC1 at 128x128
- Draw call budget: aim for under 100-150 per frame

Record frame times for specific camera positions in specific zones before and after each implementation to quantify the improvement.

A few things I'd flag for the Claude Code session:
The front-to-back sorting should be straightforward to implement since it's mostly about inserting a sort step into the draw submission path. The portal occlusion is architecturally more involved — it'll need access to EQ's zone portal data structures, so pointing the session at wherever willeq parses the zone BSP/portal info will save a lot of time.
Also, the stencil nesting approach with increment/decrement is clean on paper but can be tricky to debug if the stencil state gets out of sync (missed decrement on an early-out path, for instance). Worth having the session build in a debug mode that visualizes the stencil buffer contents as a color overlay so you can see exactly what's being masked.
