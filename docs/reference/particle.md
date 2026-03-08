# WillEQ GLES 2.0 Renderer — Implementation Handoff: Unified Particle System

## Project Context

WillEQ is an EverQuest client (`github.com/wcassis/willeq`) built on Irrlicht 1.8.5 with a custom native OpenGL ES 2.0 renderer. The renderer targets ARM Mali and other embedded GPUs, with the Mali-400 on the Orange Pi One as the performance baseline.

### Current Performance Baseline

- **Orange Pi One** (Allwinner H3, Mali-400 single fragment core): 45-48fps sustained in simple scenes at 1280x720@16bpp
- Target framerate: 30fps, leaving ~50% performance headroom for additional features
- Draw call budget: under 100-150 per frame on Mali-400
- Texture memory: under 200MB with 256x256 uncompressed; significantly lower with ETC1
- Pre-allocated particle budget: 1024 particles (~48KB)

### Hardware Targets

All support GLES 2.0. Mali-400 is the baseline:

- **Orange Pi One**: Allwinner H3, Mali-400 (1 core), 512MB RAM
- **Orange Pi Zero**: Allwinner H2+/H3, Mali-400MP2 (2 cores), 256MB RAM — lowest memory target
- **Raspberry Pi 1**: Broadcom BCM2835, VideoCore IV, 256/512MB RAM
- **MK808**: Rockchip RK3066, Mali-400MP4 (4 cores), 1GB RAM
- **Odroid XU4**: Exynos 5422, Mali-T628 MP6 (Midgard), 2GB RAM — GLES 3.1
- **Mali-G310 device**: Valhall, single core — GLES 3.2
- **Various Android 4.4 devices**: Galaxy Tab 10.1, MK808, Verizon 7" tablet, Android STB

### Existing Renderer Capabilities

- Native GLES 2.0 via EGL/DRM on Linux SBCs
- ETC1 hardware-decoded texture compression
- Texture atlasing for draw call batching
- Per-vertex lighting via custom shaders
- Working zone fog (linear and exponential, parameterized per zone)
- Stencil buffer (8-bit, D24S8 packed)
- Framebuffer objects
- Front-to-back opaque sorting (being implemented concurrently)
- Stencil-based portal occlusion (being implemented concurrently)

### Existing Culling Pipeline

1. PVS (EQ native zone data)
2. Frustum culling with spatial structure
3. Software occlusion buffer (128x64)
4. Front-to-back opaque sorting (in progress)
5. Stencil portal occlusion for indoor zones (in progress)

### Game Content Scope

Classic EverQuest through Velious expansion. Original character models only (no Luclin replacements). EQ spell effect DDS textures are available for use. All zone geometry is static.

### Previous Particle Approaches (replaced by this system)

- **CPU particle system (pre-GLES renderer)**: Proved too CPU heavy due to Lima driver per-draw-call overhead. Each particle was an individual draw call.
- **Billboard overlay for weather**: Single screen-aligned billboard with scrolling texture. Too uniform, no depth or parallax, looks like a screen filter rather than 3D precipitation.

Both approaches are being replaced by this unified point-sprite and batched-quad particle system.

---

## System Architecture Overview

The particle system is a unified architecture serving three feature areas in order of implementation priority:

1. **Fire effects** — torches, campfires, braziers (ambient zone effects at fixed world positions)
2. **Weather** — rain, snow (camera-relative volume spawning)
3. **Spell effects** — all player spell visuals across all classes (event-driven, data-defined)

All three share the same particle manager, memory pool, update loop, and rendering path. They differ only in emitter configuration and motion patterns.

### Design Principles

- **Pre-allocated memory**: Fixed particle pool, no runtime allocation
- **Minimal draw calls**: One draw call per blend mode per renderer type (point sprite vs batched quad)
- **Data-driven**: Effect definitions are parameterized configs, not hardcoded behavior
- **Two renderers**: Point sprites (`GL_POINTS`) for small/numerous particles, batched quads for larger/shaped particles
- **Additive blending where possible**: Avoids sort-order dependency, covers fire, lightning, holy, magical glow effects
- **Alpha blending with back-to-front sort when needed**: Smoke, poison clouds, snow, shadow effects

---

## Core Components

### Particle Structure

Each particle requires minimal per-instance data:

```cpp
struct Particle {
    // Core state — 48 bytes
    float position[3];      // world position
    float velocity[3];      // units per second
    float color[4];         // current RGBA
    float size;             // current point size or quad half-extent
    float age;              // seconds since spawn
    float maxLifetime;      // seconds until death

    // Spawn parameters for interpolation — 24 bytes
    float colorStart[4];    // color at birth
    float colorEnd[4];      // color at death
    float sizeStart;        // size at birth
    float sizeEnd;          // size at death

    // Motion parameters — 16 bytes
    float phase;            // random phase for oscillation (snow drift, orbital motion)
    float angularVelocity;  // for orbital/helical motion (radians per second)
    float radius;           // current orbital radius
    float param;            // general purpose (wind response, drag, etc.)

    // Metadata — 8 bytes
    uint16_t emitterID;     // which emitter owns this particle
    uint16_t textureIndex;  // region within atlas (UV offset selector)
    uint8_t  flags;         // renderer type (point/quad), blend mode, alive/dead
    uint8_t  motionType;    // linear, radial, orbital, burst, stream
    uint16_t padding;
};
// Total: ~96 bytes per particle
// 1024 particles = ~96KB
```

This is larger than the minimal 48 bytes mentioned in our earlier discussion because it includes interpolation endpoints and motion parameters to avoid per-particle lookups back into emitter definitions during the update loop. The tradeoff is worthwhile — 96KB for 1024 particles is still negligible and the update loop becomes a tight linear scan with no indirection.

### Particle Manager

The particle manager owns the fixed pool and orchestrates update and render:

```
ParticleManager
  ├── Particle pool[1024] — pre-allocated, fixed size
  ├── Active particle count
  ├── Free list (indices of dead particles for reuse)
  ├── Emitter registry (active emitters and their configs)
  ├── Point sprite VBO (dynamic, updated each frame)
  ├── Batched quad VBO (dynamic, updated each frame)
  ├── Point sprite shader program
  ├── Batched quad shader program
  ├── Texture atlas handles
  │
  ├── update(float deltaTime)
  │     1. Update emitters (spawn new particles)
  │     2. Update all live particles (age, position, color, size interpolation)
  │     3. Kill expired particles, return to free list
  │     4. Sort alpha-blended particles back-to-front (skip for additive)
  │
  ├── render()
  │     1. Upload point sprite particle data to VBO
  │     2. Draw additive point sprites (one draw call)
  │     3. Draw alpha-blended point sprites (one draw call)
  │     4. Upload batched quad particle data to VBO
  │     5. Draw additive quads (one draw call)
  │     6. Draw alpha-blended quads (one draw call)
  │     // Total: up to 4 draw calls maximum for entire particle system
  │
  ├── spawnEmitter(EmitterConfig, position, target?, duration?)
  ├── killEmitter(emitterID)
  └── setGlobalWind(vec3)  // affects weather and fire
```

### Emitter Configuration

Data-driven emitter definition. The same structure serves fire, weather, and spell effects:

```cpp
struct EmitterConfig {
    // Spawning
    float spawnRate;              // particles per second (0 = burst mode, spawn count immediately)
    int   burstCount;             // for burst mode: how many particles at once
    float emitterLifetime;        // how long this emitter stays active (0 = permanent, e.g. torches)

    // Spawn volume
    uint8_t spawnShape;           // POINT, BOX, SPHERE, RING, LINE
    float spawnExtents[3];        // half-extents for BOX, radius for SPHERE/RING, length for LINE

    // Initial velocity
    float velocityBase[3];        // base velocity direction and magnitude
    float velocitySpread[3];      // random spread added per-axis

    // Forces
    float gravity[3];             // acceleration (typically [0, -9.8, 0] or [0, +slight, 0] for rising)
    float drag;                   // velocity damping per second (0 = none, 1 = full stop)
    float windResponse;           // how much global wind affects this particle (0-1)

    // Motion pattern
    uint8_t motionType;           // LINEAR, RADIAL_EXPAND, ORBITAL, BURST, STREAM, CAMERA_RELATIVE
    float   motionParam1;         // e.g. orbital radius, expansion speed
    float   motionParam2;         // e.g. angular velocity, helix pitch

    // Appearance
    float colorStart[4];          // RGBA at birth
    float colorEnd[4];            // RGBA at death
    float sizeStart[2];           // min/max size at birth (randomized per particle)
    float sizeEnd[2];             // min/max size at death
    float lifetimeRange[2];       // min/max seconds

    // Rendering
    uint8_t rendererType;         // POINT_SPRITE or BATCHED_QUAD
    uint8_t blendMode;            // ADDITIVE or ALPHA
    uint8_t textureAtlas;         // which atlas page
    uint8_t textureRegions[4];    // possible texture region indices (randomized per particle)
    uint8_t textureRegionCount;   // how many regions to pick from

    // Triggers (for spell sequencing)
    uint8_t triggerType;          // IMMEDIATE, ON_HIT, ON_CAST_COMPLETE, DELAYED
    float   triggerDelay;         // seconds after spell start
};
```

### Motion Patterns

Five motion patterns cover all needed behaviors. Implemented in the particle update loop:

**LINEAR** — Default. Particle moves along its velocity vector, affected by gravity and drag. Used for: fire particles rising, rain falling, bolt trails, impact burst scatter.

**RADIAL_EXPAND** — Particle moves outward from its spawn center in the horizontal plane. Initial velocity is computed as a radial direction from the emitter center with random angular distribution. Used for: heal rings, nova effects, AoE indicators, shockwaves.

**ORBITAL** — Particle orbits a center point (the emitter position, which may be attached to a character). Position is computed as `center + radius * [cos(angle), 0, sin(angle)]` with angle advancing by `angularVelocity * deltaTime`. Radius and vertical offset can change over lifetime for helical/spiral effects. Used for: buff auras, bard songs, druid effects, necromancer swirls, column effects.

**BURST** — All particles spawn simultaneously with outward velocity from a point, sphere, or hemisphere. Similar to LINEAR after spawn but specifically for instantaneous multi-particle events. Used for: spell impact splashes, melee procs, resurrection burst.

**STREAM** — Particles spawn along a line segment between two points (caster → target) with slight random offset from the line. Used for: beam/channel spells. Spawn position is randomized along the line, velocity is slight outward drift perpendicular to the line.

**CAMERA_RELATIVE** — Special mode for weather. Spawn volume is positioned relative to the camera each frame, not at a fixed world position. Particles that exit the volume are recycled to the leading edge. The emitter's world position is updated to the camera position each frame before spawning. Used for: rain, snow.

---

## Rendering

### Point Sprite Renderer

For small, numerous particles. One vertex per particle.

**Vertex attributes uploaded per frame:**

```
attribute vec3 a_position;    // world position
attribute vec4 a_color;       // pre-interpolated RGBA
attribute float a_size;       // pre-interpolated size
attribute vec2 a_texOffset;   // UV offset into atlas for this particle's texture region
```

**Vertex shader:**

```glsl
attribute vec3 a_position;
attribute vec4 a_color;
attribute float a_size;
attribute vec2 a_texOffset;

uniform mat4 u_viewProjection;
uniform float u_pointSizeScale;   // based on screen height for resolution independence
uniform vec3 u_fogColor;
uniform float u_fogNear;
uniform float u_fogFar;
uniform int u_fogMode;            // 0 = linear, 1 = exponential
uniform float u_fogDensity;

varying vec4 v_color;
varying vec2 v_texOffset;
varying float v_fogFactor;

void main() {
    vec4 clipPos = u_viewProjection * vec4(a_position, 1.0);
    gl_Position = clipPos;

    // Perspective-scaled point size
    gl_PointSize = clamp(a_size * u_pointSizeScale / clipPos.w, 1.0, u_maxPointSize);

    v_color = a_color;
    v_texOffset = a_texOffset;

    // Per-vertex fog (matches zone fog applied to scene geometry)
    float dist = length((u_viewProjection * vec4(a_position, 1.0)).xyz);
    if (u_fogMode == 0) {
        v_fogFactor = clamp((u_fogFar - dist) / (u_fogFar - u_fogNear), 0.0, 1.0);
    } else {
        v_fogFactor = clamp(exp(-u_fogDensity * dist), 0.0, 1.0);
    }
}
```

Note: Computing fog distance from the camera position directly (`length(u_cameraPos - a_position)`) may be simpler and more consistent with how zone geometry fog is computed. Use whichever method the zone geometry fog shader uses for visual consistency.

**Fragment shader:**

```glsl
precision mediump float;

uniform sampler2D u_particleAtlas;
uniform vec2 u_atlasRegionSize;   // UV size of one region in atlas (e.g., 0.25 for 4x4 atlas)
uniform vec3 u_fogColor;
uniform float u_rotationAngle;    // for rain streak rotation (0 for non-rain)

varying vec4 v_color;
varying vec2 v_texOffset;
varying float v_fogFactor;

void main() {
    vec2 uv = gl_PointCoord;

    // Optional UV rotation for angled rain
    if (u_rotationAngle != 0.0) {
        uv -= 0.5;
        float s = sin(u_rotationAngle);
        float c = cos(u_rotationAngle);
        uv = vec2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
        uv += 0.5;
    }

    // Map gl_PointCoord to atlas region
    vec2 atlasUV = v_texOffset + uv * u_atlasRegionSize;

    vec4 texel = texture2D(u_particleAtlas, atlasUV);
    vec4 color = texel * v_color;

    // Apply fog
    color.rgb = mix(u_fogColor, color.rgb, v_fogFactor);

    gl_FragColor = color;
}
```

Note on the rotation branch: A branch on a uniform is free since all fragments take the same path. No divergence penalty. However, if preferred, this could be split into two shader programs (rain vs non-rain) to avoid the branch entirely. On Mali-400 the uniform branch is fine.

**Point size query at init:**

```cpp
float pointSizeRange[2];
glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, pointSizeRange);
maxPointSize = pointSizeRange[1]; // typically 256-512 on Mali-400
```

Clamp all particle sizes to this range.

**u_pointSizeScale calculation:**

```cpp
// Makes particles appear the same physical screen size at reference distance
// regardless of render resolution (640x480 vs 1280x720)
float u_pointSizeScale = screenHeight / referenceHeight; // referenceHeight e.g. 720.0
```

### Batched Quad Renderer

For larger particles that need non-square shapes, specific orientation, or sizes exceeding the hardware point size limit. Four vertices per particle, two triangles.

**Vertex layout per quad:**

Each particle expands to 4 vertices. Positions are computed on the CPU each frame using the camera's right and up vectors:

```
topLeft     = center + (-right + up) * halfSize
topRight    = center + ( right + up) * halfSize
bottomLeft  = center + (-right - up) * halfSize
bottomRight = center + ( right - up) * halfSize
```

For velocity-aligned quads (bolt projectiles, rain streaks), replace the right/up vectors with directions derived from the particle's velocity:

```
forward = normalize(velocity)
right = normalize(cross(forward, cameraForward))
up = forward  // stretch along velocity direction
```

This orients the quad along the direction of travel, creating elongated streaks for fast-moving particles.

**Index buffer:** Pre-allocated static index buffer for 256 quads (1024 vertices, 1536 indices). Pattern repeats: `[0,1,2, 2,3,0]` offset by 4 per quad. This never changes — upload once at init.

**Vertex attributes:**

```
attribute vec3 a_position;    // pre-computed corner position
attribute vec2 a_texCoord;    // UV within atlas
attribute vec4 a_color;       // same as point sprite
```

**Shaders:** Simpler than point sprites — standard textured quad with fog and color multiplication. No `gl_PointCoord` or `gl_PointSize` needed.

### Rendering Order

Within the main render loop:

```
1. Clear framebuffer (color + depth + stencil)
2. Render opaque zone geometry (front-to-back sorted, portal occluded)
3. Render opaque objects/characters (front-to-back sorted)
4. Render particle system:
   a. glDepthMask(GL_FALSE)              // disable depth writes for all particles
   b. glEnable(GL_BLEND)
   c. Bind point sprite shader
   d. glBlendFunc(GL_ONE, GL_ONE)        // additive
   e. Draw additive point sprites        // fire, sparks, magical glows — ONE DRAW CALL
   f. Bind batched quad shader
   g. Draw additive quads                // bolt projectiles, flame billboards — ONE DRAW CALL
   h. glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)  // alpha blend
   i. Bind point sprite shader
   j. Draw alpha point sprites (sorted)  // snow, smoke, poison — ONE DRAW CALL
   k. Bind batched quad shader
   l. Draw alpha quads (sorted)          // large smoke puffs, cloud effects — ONE DRAW CALL
   m. glDepthMask(GL_TRUE)
   n. glDisable(GL_BLEND)
5. Render transparent zone geometry (water, glass — back-to-front sorted)
6. Render UI
```

Total draw calls for entire particle system: up to 4 maximum, regardless of how many effects are active.

Particles have depth testing enabled (they occlude behind walls and terrain) but depth writing disabled (they don't occlude each other or scene geometry). This is standard for transparent/blended particle rendering.

### Texture Atlases

Organize particle textures into atlas pages by visual family. Each atlas is a power-of-two texture (256x256 or 512x512) with ETC1 compression, divided into a grid of equal-sized regions.

**Suggested atlas layout (4x4 grid = 16 regions per page):**

**Atlas 0 — Fire/Warm:**
- Soft radial glow (generic warm)
- Flame wisp shape
- Ember dot (small, bright)
- Smoke puff (gray, for alpha-blended fire smoke)
- Heat shimmer
- Campfire large flame
- Torch flame elongated
- Orange spark
- (remaining slots for DDS spell textures: fire bolt, fire ring, etc.)

**Atlas 1 — Ice/Cold:**
- Soft radial glow (generic cool)
- Ice crystal shard
- Frost mote
- Snow dot (soft white circle)
- Snowflake variant A
- Snowflake variant B
- Cold mist wisp
- (remaining slots for DDS spell textures: ice bolt, ice comet, frost ring)

**Atlas 2 — Nature/Poison/Disease:**
- Green glow
- Leaf shape
- Vine curl
- Poison bubble
- Disease drip
- Thorn
- (remaining slots for DDS spell textures)

**Atlas 3 — Holy/Shadow/Magic:**
- White-gold star burst
- Golden ring segment
- White radiance glow
- Purple-black wisp
- Dark mote
- Blue-purple rune
- Arcane sparkle
- (remaining slots for DDS spell textures)

**Atlas 4 — Weather/Ambient:**
- Rain streak (vertical bright line on transparent background)
- Raindrop splash (small ring)
- Generic soft circle (used for many ambient effects)
- Dust mote

At 256x256 with a 4x4 grid, each region is 64x64 — sufficient for particle textures. With ETC1 at 4bpp, each atlas is 32KB. Five atlases = 160KB total. Negligible on all targets.

---

## Implementation Phase 1: Fire Effects (Torches, Campfires, Braziers)

### Goal

Replace any existing static or missing fire visuals at zone light source positions with point sprite particle fire. Validate the entire particle system pipeline end-to-end.

### Emitter Placement

EQ zone data contains light source positions. Torches, campfires, and braziers are at known fixed positions. The particle manager creates permanent emitters (lifetime = 0 / infinite) at these positions during zone load. Emitters for lights outside the current PVS/frustum can be deactivated to avoid updating invisible particles.

### Torch Emitter Configuration

```
spawnRate: 12 particles/sec
spawnShape: POINT
spawnExtents: [0.3, 0.1, 0.3]    // slight horizontal randomization around torch position
velocityBase: [0.0, 2.5, 0.0]    // rise upward
velocitySpread: [0.5, 0.5, 0.5]  // random spread
gravity: [0.0, 0.3, 0.0]         // slight additional upward acceleration (heat convection)
drag: 0.3
windResponse: 0.2                 // slight wind influence for outdoor torches
motionType: LINEAR
colorStart: [1.0, 0.9, 0.5, 0.9] // bright yellow-white
colorEnd: [0.8, 0.2, 0.0, 0.0]   // fade through red to transparent
sizeStart: [1.5, 2.5]            // randomized
sizeEnd: [3.0, 4.0]              // grow slightly as they rise and diffuse
lifetimeRange: [0.3, 0.6]        // short-lived
rendererType: POINT_SPRITE
blendMode: ADDITIVE
textureAtlas: 0 (fire)
textureRegions: [soft_glow, flame_wisp]
textureRegionCount: 2
```

At steady state: ~5-8 live particles per torch. Visually: small bright particles rising from the torch, fading from yellow-white through orange to red, growing slightly as they rise, then disappearing.

### Campfire Emitter Configuration

Uses two emitters at the same position — a main flame emitter and a secondary ember/spark emitter:

**Main flame:**
```
spawnRate: 20 particles/sec
spawnShape: BOX
spawnExtents: [0.8, 0.1, 0.8]    // wider spawn area than torch
velocityBase: [0.0, 3.0, 0.0]
velocitySpread: [0.8, 1.0, 0.8]
gravity: [0.0, 0.5, 0.0]
drag: 0.2
windResponse: 0.3
motionType: LINEAR
colorStart: [1.0, 0.95, 0.6, 1.0]
colorEnd: [0.9, 0.15, 0.0, 0.0]
sizeStart: [2.0, 3.5]
sizeEnd: [4.0, 6.0]
lifetimeRange: [0.4, 0.7]
rendererType: POINT_SPRITE
blendMode: ADDITIVE
textureAtlas: 0
textureRegions: [soft_glow, flame_wisp, campfire_large]
textureRegionCount: 3
```

**Ember/sparks:**
```
spawnRate: 5 particles/sec
spawnShape: BOX
spawnExtents: [0.5, 0.0, 0.5]
velocityBase: [0.0, 5.0, 0.0]    // faster upward
velocitySpread: [2.0, 2.0, 2.0]  // wide scatter
gravity: [0.0, -3.0, 0.0]        // arcing — rise then fall
drag: 0.1
windResponse: 0.5
motionType: LINEAR
colorStart: [1.0, 0.7, 0.2, 1.0] // bright orange
colorEnd: [0.5, 0.1, 0.0, 0.0]   // dim red fade
sizeStart: [0.5, 1.0]            // small
sizeEnd: [0.3, 0.5]              // shrink as they cool
lifetimeRange: [0.8, 1.5]        // longer lived, drift further
rendererType: POINT_SPRITE
blendMode: ADDITIVE
textureAtlas: 0
textureRegions: [ember_dot]
textureRegionCount: 1
```

At steady state: ~12-18 flame particles + ~5-8 ember particles per campfire. The two-layer effect creates depth — a bright hot core with sparks arcing outward above it.

### Performance Expectation

A dungeon zone with 20 torch sources and 2 campfires:
- Torches: 20 × 7 avg = 140 particles
- Campfires: 2 × 20 avg = 40 particles
- Total: ~180 particles, all additive blended
- Draw calls: 1 (single additive point sprite batch)
- Impact on Mali-400: negligible

### Interaction with Zone Lighting

Fire emitters should be placed at positions that correspond to point light sources in the zone. The particle fire provides the visual, the existing per-vertex zone lighting provides the illumination on surrounding geometry. No additional lighting work needed — the fire particles are purely visual.

### Optimization: Culling Inactive Emitters

During the update phase, skip emitters whose positions are outside the current frustum or PVS. Don't just skip rendering — skip spawning and updating. This prevents invisible torches from consuming particle pool slots and CPU time. When an emitter re-enters visibility, it naturally fills up over a few frames as new particles spawn, which looks like a natural fade-in.

---

## Implementation Phase 2: Weather (Rain, Snow)

### Goal

Replace the current billboard overlay weather system with 3D point sprite precipitation that exists in world space with depth, parallax, and natural variation.

### Camera-Relative Spawning (CAMERA_RELATIVE motion type)

Weather emitters use a special spawn mode where the spawn volume is anchored to the camera position each frame rather than a fixed world location. Implementation:

Each frame before spawning:
1. Update the emitter's world position to the current camera position
2. Spawn new particles within the volume relative to this position
3. Existing particles continue moving in world space (they don't follow the camera)
4. When a particle's age exceeds lifetime, OR it falls below ground level, OR it exits the spawn volume horizontally by more than a margin, recycle it: reset position to a random point in the top layer of the spawn volume at the camera's current position, reset age to 0, assign new random attributes

This creates the illusion of infinite precipitation. As the player moves, new particles spawn at the leading edge and old particles are left behind and recycled.

### Spawn Volume Sizing

**Rain:**
- Volume: 60 wide × 40 deep × 20 tall (units relative to camera, above camera)
- Active particle count: 300-500
- Match volume depth to fog far distance — particles beyond fog are invisible, don't spawn them

**Snow:**
- Volume: 80 wide × 60 deep × 30 tall
- Active particle count: 200-300
- Larger volume because snowflakes are visible at greater distance (slower, larger, more contrast)

On the 256MB Orange Pi Zero at 640x480, reduce these counts: 200 rain, 150 snow. Still convincing at lower resolution.

### Rain Emitter Configuration

```
spawnRate: continuous recycling (maintain target count of 400 live particles)
spawnShape: BOX (camera-relative)
spawnExtents: [30.0, 10.0, 20.0]    // half-extents
velocityBase: [0.0, -25.0, 0.0]     // fast downward
velocitySpread: [1.5, 3.0, 1.5]     // slight variation
gravity: [0.0, -5.0, 0.0]           // additional downward acceleration
drag: 0.0
windResponse: 0.8                    // strong wind influence
motionType: CAMERA_RELATIVE
colorStart: [0.7, 0.75, 0.85, 0.6]  // slightly blue-gray, semi-transparent
colorEnd: [0.7, 0.75, 0.85, 0.2]    // fade slightly over lifetime
sizeStart: [2.0, 4.0]               // randomized — variation is critical
sizeEnd: [2.0, 4.0]                 // constant size for rain
lifetimeRange: [0.4, 0.8]           // short — fast recycling
rendererType: POINT_SPRITE
blendMode: ADDITIVE                  // rain brightens slightly against dark surfaces
textureAtlas: 4 (weather)
textureRegions: [rain_streak]
textureRegionCount: 1
```

**Rain UV rotation:** Set the `u_rotationAngle` uniform based on the global wind vector projected onto the screen plane. When wind blows from the west, rain streaks angle east. Compute once per frame:

```cpp
float windAngle = atan2(globalWind.x, -globalWind.y); // angle from vertical
// Pass as uniform to point sprite fragment shader
```

**Variation keys for natural-looking rain:**
- Randomize velocity per-drop by ±15%: prevents uniform curtain appearance
- Randomize alpha per-drop across 0.3-0.8: creates varying density perception
- Randomize size per-drop by ±50%: larger drops appear closer, smaller drops appear farther, enhancing depth
- Vary lifetime by ±40%: drops disappear at different heights, preventing a visible "floor"

### Snow Emitter Configuration

```
spawnRate: continuous recycling (maintain target count of 250 live particles)
spawnShape: BOX (camera-relative)
spawnExtents: [40.0, 15.0, 30.0]
velocityBase: [0.0, -3.0, 0.0]      // slow descent
velocitySpread: [0.5, 1.0, 0.5]
gravity: [0.0, -0.5, 0.0]           // very gentle
drag: 0.3                            // air resistance — snowflakes decelerate
windResponse: 1.0                    // very responsive to wind
motionType: CAMERA_RELATIVE
colorStart: [1.0, 1.0, 1.0, 0.7]    // bright white
colorEnd: [1.0, 1.0, 1.0, 0.3]      // fade over lifetime
sizeStart: [2.0, 6.0]               // wide size variation — key for depth illusion
sizeEnd: [2.0, 6.0]
lifetimeRange: [2.0, 5.0]           // long-lived — slow drift
rendererType: POINT_SPRITE
blendMode: ALPHA                     // snow should occlude, not brighten
textureAtlas: 4 (weather) or 1 (ice)
textureRegions: [snow_dot, snowflake_a, snowflake_b]
textureRegionCount: 3               // random selection per flake adds variety
```

**Snow drift oscillation:** In the particle update loop, apply a per-particle lateral sine wave:

```cpp
float drift = sin(particle.age * particle.phase * 2.0 + particle.phase * 6.28) * 0.8;
particle.position[0] += drift * deltaTime;
particle.position[2] += cos(particle.age * particle.phase * 1.5 + particle.phase * 3.14) * 0.5 * deltaTime;
```

Each snowflake has a random `phase` value (0-1), so they all drift at different frequencies and phases. This creates the characteristic wandering descent of real snowflakes. The effect is computed per-particle on the CPU — a few multiplies and a sin/cos per particle per frame, which is cheap for 250 particles.

**Snow twinkle:** Modulate alpha with a slow sine wave for light-catching effect:

```cpp
float twinkle = 0.7 + 0.3 * sin(particle.age * 3.0 + particle.phase * 6.28);
particle.color[3] = baseAlpha * twinkle;
```

**Snow size-speed correlation:** Larger snowflakes should fall slower. During spawn:

```cpp
particle.size = randomRange(sizeMin, sizeMax);
float sizeFactor = (particle.size - sizeMin) / (sizeMax - sizeMin); // 0 = smallest, 1 = largest
particle.velocity[1] *= (1.0 - sizeFactor * 0.4); // large flakes fall 40% slower
```

This naturally creates depth layering — large slow flakes feel close, small fast flakes feel far.

### Weather Integration with Fog

Apply the same fog calculation used for zone geometry to weather particles (this is already in the point sprite vertex shader above). Precipitation naturally fades into fog at distance, which:
- Reduces visible pop-in of new particles at the volume edges
- Creates realistic depth perception (heavy near, fading to fog far)
- Means the spawn volume doesn't need to extend beyond the fog distance — match it

### Weather Integration with Zone Lighting

Multiply weather particle color by the zone's ambient light color. Night rain should be dark and barely visible. Snow in a bright Velious zone should be brilliant white. Snow near a torch should warm slightly.

```cpp
// During particle color update:
vec3 particleColor = baseColor * zoneAmbientColor;
// Optional: if near a light source, add warmth
// (simple distance check against known light positions, done on CPU)
```

### Weather Activation

Weather is enabled per zone from zone data. The particle manager receives a weather configuration (rain/snow/none, intensity, wind direction) during zone load and activates/deactivates the camera-relative weather emitter accordingly. Zone transitions should ramp weather particle count up/down over 1-2 seconds rather than appearing/disappearing instantly.

### Performance Expectation

- Rain: 400 particles, 1 draw call (additive point sprites)
- Snow: 250 particles, 1 draw call (alpha point sprites, sorted back-to-front)
- Both weather types share draw calls with other particles of the same blend mode
- CPU cost: particle update for 250-400 particles with simple motion math is sub-millisecond on the H3
- GPU cost on Mali-400: minimal — small point sprites at medium density, most screen area unaffected

---

## Implementation Phase 3: Spell Effects

### Goal

Implement player spell visuals across all classes using the same particle infrastructure. Effects are data-driven, defined per spell ID, and composed of one or more emitters with timed triggers.

### Spell Effect Definition Structure

Each spell effect is a list of emitter configurations with timing and targeting:

```cpp
struct SpellEffectDef {
    uint32_t spellID;
    const char* name;                     // for debug/logging
    int emitterCount;
    SpellEmitterDef emitters[8];          // max 8 emitters per spell (generous)
};

struct SpellEmitterDef {
    EmitterConfig config;                 // same emitter config as fire/weather
    uint8_t triggerType;                  // IMMEDIATE, ON_HIT, ON_CAST_COMPLETE, DELAYED
    float triggerDelay;                   // seconds after spell start
    uint8_t attachPoint;                  // CASTER, TARGET, PROJECTILE_PATH, GROUND_TARGET
    float positionOffset[3];             // offset from attach point (e.g., [0, 1, 0] for waist height)
};
```

### Spell Visual Archetypes

Seven reusable patterns that cover all classic/Kunark/Velious spells:

**1. BOLT_PROJECTILE** — Travels from caster to target

Composed of:
- 1 batched quad: the bolt itself (velocity-aligned billboard, DDS texture)
- 1 point sprite emitter: trailing particles spawned along the path
- 1 point sprite emitter (trigger: ON_HIT): impact burst at target

The bolt is a special single-particle quad that interpolates position from caster to target over a duration. The trail emitter is attached to the bolt's current position and spawns backward-facing particles that linger after the bolt passes.

**2. EXPANDING_RING** — Radiates outward horizontally from a point

Single point sprite emitter with RADIAL_EXPAND motion. Particles spawn at center with random angular distribution, move outward, fade. Used for heals, stuns, mez, many buff landings.

**3. VERTICAL_COLUMN** — Particles rising or falling around a character

Single point sprite emitter with ORBITAL motion. Particles orbit the target/caster position at varying heights and radii. Can spiral upward (buffs) or downward (debuffs). Height range and orbital direction define the visual.

**4. TARGETED_SPLASH** — Brief burst at a point

Single point sprite emitter with BURST motion (trigger: ON_HIT or IMMEDIATE). burstCount particles spawn simultaneously with outward velocity, scatter and fade. Duration under 1 second.

**5. SUSTAINED_AURA** — Persistent subtle effect

Single point sprite emitter with ORBITAL motion, permanent duration (tied to buff/song duration). Very low particle count (8-12), slow orbit, low opacity. Must be performance-cheap as it persists during gameplay.

**6. CONE_AREA** — Fan or circle on the ground

Point sprite emitter with RADIAL_EXPAND motion, constrained to a horizontal plane and angular range. For AoE indicators and breath weapons.

**7. BEAM_STREAM** — Continuous stream between two points

Point sprite emitter with STREAM motion. Particles distributed along the caster→target line with slight perpendicular drift. Active while the spell is channeling.

### Spell Effect Examples (Data Definitions)

**Cleric Heal (Complete Heal, etc.):**
```
emitters:
  - trigger: ON_CAST_COMPLETE
    attachPoint: TARGET
    config:
      motionType: RADIAL_EXPAND
      spawnShape: RING
      spawnExtents: [0.5, 0, 0]     // small initial ring at feet
      burstCount: 15
      velocityBase: [3.0, 1.5, 0.0]  // expand outward and slightly up
      colorStart: [1.0, 0.95, 0.7, 0.9]   // warm gold-white
      colorEnd: [1.0, 0.85, 0.4, 0.0]
      sizeStart: [2.0, 3.0]
      sizeEnd: [4.0, 6.0]
      lifetimeRange: [0.5, 0.8]
      blendMode: ADDITIVE
      textureRegions: [golden_glow, star_burst]

  - trigger: ON_CAST_COMPLETE
    attachPoint: TARGET
    positionOffset: [0, 0.5, 0]      // waist height
    config:
      motionType: ORBITAL
      spawnRate: 8
      emitterLifetime: 1.0
      motionParam1: 1.5              // orbital radius
      motionParam2: 4.0              // angular velocity (rad/s)
      velocityBase: [0.0, 2.0, 0.0]  // drift upward while orbiting
      colorStart: [1.0, 1.0, 0.8, 0.7]
      colorEnd: [1.0, 0.9, 0.5, 0.0]
      sizeStart: [1.5, 2.5]
      sizeEnd: [0.5, 1.0]
      lifetimeRange: [0.6, 1.0]
      blendMode: ADDITIVE
      textureRegions: [golden_glow]
```

**Wizard Ice Comet:**
```
emitters:
  - trigger: IMMEDIATE
    attachPoint: PROJECTILE_PATH       // travels caster → target
    config:
      rendererType: BATCHED_QUAD       // the comet itself
      motionType: LINEAR
      spawnRate: 0
      burstCount: 1
      velocityBase: computed from caster→target direction × speed
      colorStart: [0.8, 0.9, 1.0, 1.0]
      colorEnd: [0.8, 0.9, 1.0, 1.0]  // constant while traveling
      sizeStart: [3.0, 3.0]
      lifetimeRange: [computed from distance/speed]
      blendMode: ADDITIVE
      textureRegions: [ice_shard]      // DDS ice texture

  - trigger: IMMEDIATE
    attachPoint: PROJECTILE_PATH       // follows the comet
    config:
      rendererType: POINT_SPRITE
      motionType: LINEAR
      spawnRate: 40                     // dense trail
      velocityBase: [0.0, 0.5, 0.0]   // slight upward drift after spawn
      velocitySpread: [1.0, 0.5, 1.0]
      drag: 0.5
      colorStart: [0.7, 0.85, 1.0, 0.8]
      colorEnd: [0.3, 0.5, 0.8, 0.0]
      sizeStart: [1.0, 2.0]
      sizeEnd: [2.5, 4.0]
      lifetimeRange: [0.3, 0.6]       // linger briefly after bolt passes
      blendMode: ADDITIVE
      textureRegions: [frost_mote, soft_glow_cool]

  - trigger: ON_HIT
    attachPoint: TARGET
    config:
      rendererType: POINT_SPRITE
      motionType: BURST
      burstCount: 25
      spawnShape: SPHERE
      spawnExtents: [0.5, 0.5, 0.5]
      velocityBase: [0.0, 2.0, 0.0]
      velocitySpread: [4.0, 3.0, 4.0]  // scatter outward
      gravity: [0.0, -6.0, 0.0]        // ice shards arc and fall
      colorStart: [0.9, 0.95, 1.0, 1.0]
      colorEnd: [0.4, 0.6, 0.9, 0.0]
      sizeStart: [1.0, 2.0]
      sizeEnd: [0.5, 1.0]
      lifetimeRange: [0.5, 1.0]
      blendMode: ADDITIVE
      textureRegions: [frost_mote, ice_crystal]

  - trigger: ON_HIT
    attachPoint: TARGET
    positionOffset: [0, 0, 0]         // ground level
    config:
      motionType: RADIAL_EXPAND
      burstCount: 12
      velocityBase: [4.0, 0.0, 0.0]   // expand horizontally
      colorStart: [0.6, 0.8, 1.0, 0.6]
      colorEnd: [0.3, 0.5, 0.8, 0.0]
      sizeStart: [2.0, 3.0]
      sizeEnd: [5.0, 7.0]
      lifetimeRange: [0.6, 1.0]
      blendMode: ADDITIVE
      textureRegions: [frost_mote]
```

**Necromancer Lifetap:**
```
emitters:
  - trigger: ON_HIT
    attachPoint: TARGET
    config:
      motionType: BURST
      burstCount: 10
      colorStart: [0.4, 0.0, 0.2, 0.8]    // dark red-purple
      colorEnd: [0.1, 0.0, 0.05, 0.0]
      sizeStart: [1.5, 2.5]
      lifetimeRange: [0.3, 0.5]
      blendMode: ADDITIVE
      textureRegions: [dark_wisp]

  - trigger: ON_HIT
    attachPoint: CASTER                      // life returns to caster
    triggerDelay: 0.3                        // slight delay after hit
    config:
      motionType: ORBITAL
      spawnRate: 10
      emitterLifetime: 0.8
      motionParam1: 1.0                     // tight orbit
      motionParam2: 6.0                     // fast spin
      velocityBase: [0.0, 1.5, 0.0]
      colorStart: [0.6, 0.0, 0.1, 0.7]     // dark red
      colorEnd: [0.2, 0.0, 0.05, 0.0]
      sizeStart: [1.0, 1.5]
      sizeEnd: [0.3, 0.5]
      lifetimeRange: [0.4, 0.7]
      blendMode: ADDITIVE
      textureRegions: [dark_wisp, dark_mote]
```

**Bard Song (persistent aura while singing):**
```
emitters:
  - trigger: IMMEDIATE
    attachPoint: CASTER
    positionOffset: [0, 1.0, 0]            // waist height
    config:
      motionType: ORBITAL
      spawnRate: 3                           // very sparse — must be cheap
      emitterLifetime: 0                     // permanent until song ends
      motionParam1: 1.2                      // orbit radius
      motionParam2: 2.0                      // slow gentle orbit
      colorStart: [0.5, 0.7, 1.0, 0.4]      // soft blue (varies by song)
      colorEnd: [0.3, 0.5, 0.8, 0.0]
      sizeStart: [1.5, 2.0]
      sizeEnd: [2.5, 3.5]
      lifetimeRange: [1.5, 2.5]             // long-lived, few at a time
      blendMode: ADDITIVE
      textureRegions: [soft_glow_cool, arcane_sparkle]
```

Steady state: 6-8 particles. One draw call (shared with all other additive point sprites). Negligible performance cost. Color changes based on song type — war songs red-orange, mana songs blue, haste songs yellow, resist songs green.

**Melee Proc (weapon effect):**
```
emitters:
  - trigger: IMMEDIATE
    attachPoint: TARGET
    positionOffset: [0, 1.5, 0]            // chest height on target
    config:
      motionType: BURST
      burstCount: 6                          // very few — procs happen often
      spawnShape: SPHERE
      spawnExtents: [0.3, 0.3, 0.3]
      velocitySpread: [3.0, 2.0, 3.0]
      colorStart: varies by proc type
      colorEnd: [transparent]
      sizeStart: [0.8, 1.5]
      sizeEnd: [0.3, 0.5]
      lifetimeRange: [0.15, 0.3]            // very brief
      blendMode: ADDITIVE
      textureRegions: varies by proc type
```

6 particles, gone in 0.3 seconds. Effectively free.

**Resurrection:**
```
emitters:
  - trigger: ON_CAST_COMPLETE
    attachPoint: TARGET                      // the corpse
    positionOffset: [0, 0, 0]
    config:
      motionType: ORBITAL
      spawnRate: 40                          // dense burst
      emitterLifetime: 2.0
      motionParam1: 0.5→2.0                 // radius expands over emitter lifetime
      motionParam2: 5.0                      // fast upward spiral
      velocityBase: [0.0, 4.0, 0.0]         // strong upward
      colorStart: [1.0, 1.0, 0.9, 1.0]      // brilliant white-gold
      colorEnd: [1.0, 0.9, 0.5, 0.0]
      sizeStart: [2.0, 3.0]
      sizeEnd: [4.0, 6.0]
      lifetimeRange: [0.8, 1.5]
      blendMode: ADDITIVE
      textureRegions: [star_burst, golden_glow, white_radiance]

  - trigger: ON_CAST_COMPLETE
    attachPoint: TARGET
    config:
      motionType: RADIAL_EXPAND
      burstCount: 20
      triggerDelay: 1.5                      // ring expands near end of column
      velocityBase: [6.0, 0.5, 0.0]
      colorStart: [1.0, 0.95, 0.7, 0.9]
      colorEnd: [1.0, 0.85, 0.4, 0.0]
      sizeStart: [3.0, 5.0]
      sizeEnd: [6.0, 8.0]
      lifetimeRange: [0.6, 1.0]
      blendMode: ADDITIVE
      textureRegions: [golden_glow]
```

Peak: ~60-80 particles for 2 seconds. Infrequent event, worth the budget for dramatic impact.

**Gate/Teleport:**
```
emitters:
  - trigger: IMMEDIATE
    attachPoint: CASTER
    positionOffset: [0, 0, 0]               // ground ring
    config:
      motionType: RADIAL_EXPAND
      spawnRate: 15
      emitterLifetime: cast_time             // matches cast duration
      velocityBase: [2.0, 0.0, 0.0]         // slow expand
      colorStart: [0.5, 0.3, 0.8, 0.6]      // purple arcane
      colorEnd: [0.3, 0.1, 0.6, 0.0]
      sizeStart: [2.0, 3.0]
      sizeEnd: [3.0, 5.0]
      lifetimeRange: [0.8, 1.2]
      blendMode: ADDITIVE
      textureRegions: [arcane_sparkle, blue_purple_rune]

  - trigger: IMMEDIATE
    attachPoint: CASTER
    positionOffset: [0, 0.5, 0]
    config:
      motionType: ORBITAL
      spawnRate: 12
      emitterLifetime: cast_time
      motionParam1: 1.0
      motionParam2: 8.0                      // fast spin, intensifying
      velocityBase: [0.0, 3.0, 0.0]
      colorStart: [0.6, 0.4, 1.0, 0.8]
      colorEnd: [0.3, 0.1, 0.7, 0.0]
      sizeStart: [1.5, 2.0]
      sizeEnd: [1.0, 1.5]
      lifetimeRange: [0.3, 0.5]
      blendMode: ADDITIVE
      textureRegions: [arcane_sparkle]
```

On cast complete: all particles killed simultaneously (don't fade — abrupt disappearance reads as teleportation). Implement as: when spell completes, set all particles from this emitter to `age = maxLifetime` so they're cleaned up in the next update. Or simply set alpha to 0 and let normal cleanup handle it. The key is visual abruptness.

### Blend Mode Batching Strategy

Organize all active spell particles by blend mode, not by spell:

```
Frame N during a group fight:
  Active effects:
    - Cleric heal ring (additive, gold, atlas 3)
    - Wizard ice bolt trail (additive, blue, atlas 1)
    - Bard song aura (additive, blue, atlas 3)
    - Necro DoT tick (additive, purple, atlas 3)
    - 10 torches (additive, orange, atlas 0)

  Draw calls:
    1. Additive point sprites: ALL of the above in one call
       (different atlas pages require separate calls — so potentially
        1 call per active atlas page among additive particles)
```

In practice, consolidating to a single uber-atlas for point sprite particles (all the soft glows, motes, and sparkles on one page) would reduce this to truly one draw call for all additive point sprites. Reserve the per-family atlases for batched quad textures where the DDS spell textures need more resolution and visual distinctness.

### Worst-Case Performance Budget

Busy group fight scenario — 6 players, bard, multiple mobs, simultaneous spells:

| Source | Particles | Duration |
|---|---|---|
| 6 player buff auras | 60 | persistent |
| 1 bard song | 8 | persistent |
| 2-3 active spell effects | 90 | 0.5-2s each |
| DoT ticks | 20 | momentary |
| Melee procs | 10 | momentary |
| 15 dungeon torches | 120 | persistent |
| **Total peak** | **~310** | |

Well under the 1024 budget. Draw calls: 2-4 depending on atlas page spread and blend modes. CPU update cost: sub-millisecond for 310 particles with simple motion math. GPU cost on Mali-400: negligible fragment work for small additive point sprites.

### Spell Definition Storage

Store spell effect definitions in a simple JSON or config format loaded at startup:

```json
{
  "spell_effects": [
    {
      "spell_id": 12345,
      "name": "Ice Comet",
      "emitters": [
        {
          "trigger": "IMMEDIATE",
          "attach": "PROJECTILE_PATH",
          "renderer": "BATCHED_QUAD",
          "motion": "LINEAR",
          "blend": "ADDITIVE",
          "atlas": "ice",
          "texture": "ice_shard",
          "color_start": [0.8, 0.9, 1.0, 1.0],
          "color_end": [0.8, 0.9, 1.0, 1.0],
          "size_start": [3.0, 3.0],
          "size_end": [3.0, 3.0],
          "lifetime": [0.8, 0.8],
          "speed": 40.0
        },
        {
          "trigger": "IMMEDIATE",
          "attach": "PROJECTILE_PATH",
          "renderer": "POINT_SPRITE",
          "motion": "LINEAR",
          "blend": "ADDITIVE",
          "spawn_rate": 40,
          "atlas": "ice",
          "texture": ["frost_mote", "soft_glow"],
          "color_start": [0.7, 0.85, 1.0, 0.8],
          "color_end": [0.3, 0.5, 0.8, 0.0],
          "size_start": [1.0, 2.0],
          "size_end": [2.5, 4.0],
          "lifetime": [0.3, 0.6],
          "velocity_spread": [1.0, 0.5, 1.0],
          "drag": 0.5
        },
        {
          "trigger": "ON_HIT",
          "attach": "TARGET",
          "renderer": "POINT_SPRITE",
          "motion": "BURST",
          "blend": "ADDITIVE",
          "burst_count": 25,
          "atlas": "ice",
          "texture": ["frost_mote", "ice_crystal"],
          "color_start": [0.9, 0.95, 1.0, 1.0],
          "color_end": [0.4, 0.6, 0.9, 0.0],
          "size_start": [1.0, 2.0],
          "size_end": [0.5, 1.0],
          "lifetime": [0.5, 1.0],
          "velocity_spread": [4.0, 3.0, 4.0],
          "gravity": [0.0, -6.0, 0.0]
        }
      ]
    }
  ]
}
```

This allows adding new spell effects or tuning existing ones without recompiling. The Claude Code session implementing spell effects should build a loader for this format and a small library of effect definitions covering the major spell archetypes.

---

## Implementation Order Summary

### Phase 1: Core Infrastructure + Fire
1. Implement Particle struct and ParticleManager with fixed pool
2. Implement point sprite renderer (shader, VBO upload, draw)
3. Implement EmitterConfig and basic LINEAR motion
4. Create fire texture atlas (or temporary procedural soft-glow texture)
5. Place torch/campfire emitters at zone light positions
6. Validate: torches rendering in a dungeon zone, one draw call, stable performance

### Phase 2: Weather
1. Implement CAMERA_RELATIVE motion type with recycling logic
2. Implement rain emitter config with UV rotation
3. Implement snow emitter config with drift oscillation and size-speed correlation
4. Create weather texture atlas
5. Integrate with zone fog (vertex shader fog in particle shader)
6. Integrate with zone ambient lighting
7. Remove old billboard overlay weather system
8. Validate: rain in Innothule, snow in Eastern Wastes, natural parallax and variation

### Phase 3: Spell Effects
1. Implement remaining motion types: RADIAL_EXPAND, ORBITAL, BURST, STREAM
2. Implement batched quad renderer for bolt projectiles and large billboard elements
3. Implement SpellEffectDef and SpellEmitterDef with trigger system
4. Implement PROJECTILE_PATH attach point (interpolating emitter position caster→target)
5. Create spell texture atlases from DDS assets + generated soft textures
6. Build spell effect JSON loader
7. Author effect definitions for spell skills (evocation, conjuration, alteration, abjuration, divination) and spell types (heal, buff, debuff, nuke, bolt, rain, targeted aoe, player based aoe, damage over time, create item, teleport) 
8. Spell effects should be a combination of GL_POINT particles, flat quads, textured quads, etc while staying within performance/polygon/fps budgets
8. Validate: multiple simultaneous spell effects in a group fight scenario, total draw calls ≤ 4

### Ongoing
- Profile each phase on Mali-400 baseline, ensure 30fps target is maintained
- Test on other hardware targets as they become available
- Expand spell effect library by authoring new JSON definitions (data, not code)


A few things worth noting for the session:
The particle struct ended up at 96 bytes per particle rather than the 48 byte minimum we discussed earlier. The extra space stores interpolation endpoints and motion parameters inline so the update loop is a tight linear scan without chasing pointers back to emitter definitions. At 1024 particles that's still under 100KB — not worth optimizing further.
The spell effect JSON definitions toward the end are the ones most likely to need iteration after initial implementation. The exact color values, spawn rates, sizes, and lifetimes will need tuning once you can see them in-game. Having them in a data file rather than compiled code makes that iteration loop much faster.
One thing not in the doc that might be worth mentioning to the session — if the atlas-per-family approach results in more draw calls than you'd like (one per active atlas page), consolidating all point sprite particle textures into a single uber-atlas would guarantee one draw call for all additive point sprites regardless of what effects are active. The per-family split is really only important for the batched quad textures where you want higher resolution DDS spell art.

