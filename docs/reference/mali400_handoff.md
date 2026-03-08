# WillEQ Rendering Pipeline — Mali 400 / OpenGL ES 2.0 Handoff Document

## Purpose

This document specifies the rendering architecture for WillEQ, a custom EverQuest client targeting ARM Mali 400/450 GPUs under Linux using the Lima (Mesa) open-source driver stack. It covers hardware constraints, the shader strategy, stencil buffer usage, frame structure, GP/PP scheduling, and per-pass resource budgets. Use it as the authoritative reference for implementing the renderer.

---

## 1. Hardware Target & Constraints

### 1.1 GPU: Mali 400 MP2 (Utgard Architecture)

- **Cores:** 1 GP (Geometry Processor / vertex shader), 2 PP (Pixel Processor / fragment shader)
- **Rendering model:** Tile-based. The screen is divided into 16×16 pixel tiles. Each tile is rendered to completion in fast on-chip SRAM before being written to main memory.
- **Tile count at 720p:** 1280×720 / 16×16 = 56,250 tiles per frame
- **PP tile distribution:** Tiles are split across both PP cores from a shared list. Mali 400 requires the userspace driver to assign tiles; Mali 450 has hardware load balancing (DLBU).
- **GP/PP pipelining:** GP and PP operate concurrently. The GP can process vertices for the next batch while the PP is still shading fragments from the current batch. Exploit this overlap — never serialize them.

### 1.2 Instruction Budgets

| Core | Hard Limit | Practical Ceiling | Notes |
|------|-----------|-------------------|-------|
| PP (fragment) | 512 instructions | ~400 instructions | Compiler overhead for register spilling, int→FP16 lowering eats headroom |
| GP (vertex) | Higher than PP | ~200–300 practical | Less constrained; use it aggressively to offload PP work |

### 1.3 Precision

- **GP (vertex shader):** FP32 — full precision, no issues.
- **PP (fragment shader):** FP16 only. `highp` qualifier has NO effect; it is silently ignored.
- **Integers:** Not supported in hardware. Lowered to FP16 by the compiler.
- **Texture lookups:** FP24 precision path available ONLY when texture coordinates come directly from varyings with NO math applied in the fragment shader. Any calculation on texture coordinates (even adding an offset) drops to FP16.
- **Implication:** Compute texture coordinates in the vertex shader and pass as varyings whenever possible to get the FP24 path.

### 1.4 Texture Constraints

- **ETC1 compression:** Supported. Does NOT support alpha channel. Use dual-texture approach for alpha (see Section 6).
- **NPOT textures:** Supported via `GL_OES_texture_npot`, but NPOT textures CANNOT use mipmaps or `GL_REPEAT` wrapping. Use power-of-two for all tiling/mipmapped textures.
- **Max texture size:** 4096×4096 (check `GL_MAX_TEXTURE_SIZE` at runtime).
- **Texture fetches per shader:** 4 is comfortable; 5+ is expensive. Each fetch stalls waiting for the texture unit.

### 1.5 Driver: Lima (Mesa)

- **API:** OpenGL ES 2.0 (97% dEQP conformance, on par with ARM blob)
- **Also exposes:** OpenGL 2.1 desktop (via Mesa/Gallium, partial)
- **Key supported extensions:**
  - `GL_OES_depth_texture` — required for shadow mapping
  - `GL_OES_element_index_uint` — 32-bit index buffers for large zone meshes
  - `GL_OES_vertex_half_float` — half-float vertex attributes to save bandwidth
  - `GL_OES_texture_npot` — non-power-of-two textures (with restrictions above)
  - `GL_EXT_texture_format_BGRA8888` — direct upload of BGRA data (useful for legacy EQ assets)
  - `GL_OES_compressed_ETC1_RGB8_texture` — ETC1 compression
  - `GL_EXT_occlusion_query_boolean` — occlusion queries for portal visibility testing
  - `GL_OES_standard_derivatives` — `dFdx`/`dFdy` in fragment shaders

---

## 2. Shader Strategy (The "Best Combo")

Different object types in the scene use different shader programs. The GP is often underutilized while the PP is the bottleneck, so push work to the vertex shader.

### 2.1 Shader Programs by Object Type

| Object Type | Techniques | PP Instructions | Texture Fetches | Notes |
|-------------|-----------|----------------|-----------------|-------|
| Zone geometry | Lightmap + normal map + 1 dynamic light + fog + shadow recv | ~60–80 | 4 | Heaviest shader |
| Characters | Skinned animation + normal map + 1 dynamic light + fog | ~50–70 | 3 | Skinning is GP-side |
| Terrain | Texture splat (3 tex) + lightmap + fog | ~80–110 | 4–5 | Use vertex color blend weights |
| Water surface | UV distortion + tint + fresnel + fog | ~30–40 | 2 | Stencil-masked |
| Spell decals | Projected texture + animation + additive blend | ~15–20 | 1 | Stencil-masked |
| Particles | Billboard sprite + fog | ~10–15 | 1 | Batch into single draw call |

### 2.2 Core Principle: Offload PP → GP

For EVERY shader, apply this checklist:

1. **TBN matrix** — compute in vertex shader, pass transformed light/view directions as varyings
2. **Fog factor** — compute distance and fog blend in vertex shader, pass as single float varying
3. **Shadow coordinates** — transform by light MVP in vertex shader, pass as varying (gets FP24 lookup path)
4. **Texture coordinates** — pass through unmodified from vertex attributes to get FP24 precision
5. **Billboard orientation** — compute rotated vertex positions in vertex shader
6. **Bone blending** — entirely in vertex shader

---

## 3. Stencil Buffer Usage

The stencil buffer is stored in tile SRAM alongside color and depth. Stencil operations are cheap as long as you don't switch FBOs (which flushes tile memory). All stencil uses below operate within the main framebuffer — no FBO switches.

### 3.1 Zone Portal Clipping — HIGH PRIORITY

EQ's indoor zones (Befallen, Lower Guk, Blackburrow) are rooms connected by doorways. Use stencil to prevent rendering geometry behind non-visible portals.

**Implementation:**
1. After CPU portal culling determines which portals are visible, render portal quads with:
   - Color write: OFF
   - Depth write: OFF
   - Stencil op: `GL_REPLACE` with unique reference value per portal (1, 2, 3...)
2. When rendering geometry for a room behind a portal, set stencil test to `GL_EQUAL` with that portal's reference value.
3. Fragments outside the portal opening fail the stencil test BEFORE the fragment shader runs — free culling.

**Performance impact:** In a dungeon, portals can cull 40–60% of screen pixels from expensive fragment shading. This is the single biggest optimization for indoor zones.

### 3.2 Water Surface Boundary — HIGH PRIORITY

**Implementation:**
1. Render water plane with color/depth write OFF, stencil `GL_REPLACE` with value 128.
2. Render water shader pass with stencil test `GL_EQUAL` 128, blending enabled.
3. For underwater camera: invert logic — stencil marks above-water region for refraction, tint entire view.

**Handles:** Shoreline intersection, underwater transitions, surface-only shader cost.

### 3.3 Spell AoE Volumes — MEDIUM PRIORITY

**Implementation:**
1. Render AoE projection volume (circle on ground) into stencil with unique reference per spell.
2. Render spell texture/animation only where stencil matches.
3. Use different stencil reference values for stacking AoEs (common in raids).

**Prevents:** Double-blending, decals bleeding onto walls, z-fighting.

### 3.4 Target/Selection Outline — MEDIUM PRIORITY

**Implementation:**
1. Render targeted character normally (writes to stencil with value 1).
2. Render same character scaled up ~5% with solid glow color, stencil test `GL_NOTEQUAL` 1.
3. Only the outline ring (the scaled-up portion not covered by the original) renders.

**Stencil value scheme:** 1 = hostile target, 2 = group member, 3 = friendly NPC. Different colored outlines per value.

### 3.5 UI Window Clipping — MEDIUM PRIORITY

Use stencil to define arbitrary clip regions for scrollable UI elements (chat, spell book, inventory). More flexible than scissor test for overlapping irregular windows.

### 3.6 Localized Fog Boundaries — LOW PRIORITY

Mark fog zones (Kithicor at night, Cazic-Thule) in stencil, apply enhanced fog density only in marked regions during post-pass. Avoids fragment shader branching.

---

## 4. Frame Structure (Detailed)

### Render Target: 720p (1280×720), 30fps target = 33.3ms budget

### 4.0 CPU Pre-Frame — Target: <3ms

```
1. Frustum cull zone BSP/octree → reject invisible branches
   Input:  50,000–80,000 triangles (full zone)
   Output: 25,000–35,000 candidates

2. Portal cull from player's room → walk visible portals, clip frustum
   Input:  25,000–35,000 candidates
   Output: 10,000–15,000 visible zone triangles

3. Sort opaque geometry front-to-back (centroid distance)
4. Batch by shader program, then by texture atlas within each shader
   Target: 30–60 zone draw calls, 5–10 character draw calls

5. Update uniforms: bone matrices, dynamic light position, time
```

### 4.1 Pass 1: Shadow Map — FBO: 256×256 depth texture

**Purpose:** Player character blob shadow on ground.

**GP:**
- Input: Player mesh (~1,500 triangles)
- Shader: Bone blending + light-space MVP transform. No varyings except `gl_Position`.
- Instructions: ~60–80 per vertex (4 bone influences × mat4 multiply)
- Time: ~0.3ms

**PP:**
- Shader: Empty (hardware writes depth automatically)
- Instructions: ~1–5
- Tiles: 256 tiles, 128 per PP core
- Time: <0.5ms

**FBO resolve:** 256×256 depth texture written to main memory. Small and fast.

**⚠️ This is the ONLY FBO switch in the frame. All subsequent passes use the main framebuffer.**

**Pass total: ~0.8ms**

### 4.2 Pass 2: Portal Stencil Marking — FBO: Main (720p)

**Purpose:** Write portal IDs into stencil buffer for later culling.

**GL state:**
```c
glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
glDepthMask(GL_FALSE);
glEnable(GL_STENCIL_TEST);
glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
// For each portal:
glStencilFunc(GL_ALWAYS, portal_id, 0xFF);
glDrawElements(...); // portal quad
```

**GP:** Trivial — 4–8 portal quads = 8–16 triangles. ~10–15 instructions. Negligible time.

**PP:** Empty shader; stencil hardware does the work. <0.3ms.

**CRITICAL:** Do NOT resolve the framebuffer. Stencil values persist in tile SRAM for the next pass.

**Pass total: ~0.3ms**

### 4.3 Pass 3: Zone Geometry — FBO: Main (stencil preserved)

**Purpose:** Render all static zone geometry.

**GL state:**
```c
glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
glDepthMask(GL_TRUE);
glEnable(GL_DEPTH_TEST);
glEnable(GL_STENCIL_TEST);
// Stencil test rejects pixels outside portal regions
```

**GP — Zone Vertex Shader:**

```glsl
// Attributes
attribute vec3 a_position;
attribute vec3 a_normal;
attribute vec3 a_tangent;
attribute vec2 a_texcoord0;   // diffuse + normal map UVs
attribute vec2 a_texcoord1;   // lightmap UVs

// Uniforms
uniform mat4 u_mvp;
uniform mat4 u_modelview;
uniform mat3 u_normal_matrix;
uniform vec3 u_light_pos_eye;
uniform mat4 u_light_mvp;     // for shadow mapping
uniform float u_fog_density;
uniform vec3 u_fog_color;

// Varyings
varying vec2 v_uv0;
varying vec2 v_uv1;
varying vec3 v_light_dir_tan;
varying vec3 v_view_dir_tan;
varying float v_fog_factor;
varying vec3 v_shadow_coord;
```

**GP instruction breakdown:**
| Operation | Instructions |
|-----------|-------------|
| MVP × position | ~4 |
| Modelview × position (eye-space) | ~4 |
| Normal matrix × normal | ~3 |
| Normal matrix × tangent | ~3 |
| Cross product for bitangent | ~3 |
| Build 3×3 TBN matrix | ~3 |
| Light vector × TBN | ~6 |
| View vector × TBN | ~6 |
| Fog distance (eye-space z) | ~2–4 |
| Fog factor (linear or exp) | ~2–4 |
| Shadow coord (light MVP × position) | ~4 |
| Varying assignments | ~6 |
| **GP total** | **~50–70** |

- Vertex count: ~15,000 visible zone vertices
- GP time: ~1–2ms

**PP — Zone Fragment Shader:**

```glsl
uniform sampler2D u_diffuse_atlas;
uniform sampler2D u_normal_map;
uniform sampler2D u_lightmap;
uniform sampler2D u_shadow_map;
uniform vec3 u_light_color;
uniform vec3 u_fog_color;

varying vec2 v_uv0;
varying vec2 v_uv1;
varying vec3 v_light_dir_tan;
varying vec3 v_view_dir_tan;
varying float v_fog_factor;
varying vec3 v_shadow_coord;

void main() {
    // Texture fetches
    vec4 diffuse = texture2D(u_diffuse_atlas, v_uv0);
    vec3 normal = texture2D(u_normal_map, v_uv0).rgb;
    vec3 lightmap = texture2D(u_lightmap, v_uv1).rgb;
    float shadow = texture2D(u_shadow_map, v_shadow_coord.xy).r;

    // Normal unpack
    normal = normalize(normal * 2.0 - 1.0);

    // Dynamic light (diffuse)
    float NdotL = max(dot(normal, normalize(v_light_dir_tan)), 0.0);
    vec3 dynamic = u_light_color * NdotL * shadow;

    // Combine
    vec3 lit = diffuse.rgb * (lightmap + dynamic);

    // Specular (Blinn-Phong)
    vec3 halfVec = normalize(v_light_dir_tan + v_view_dir_tan);
    float spec = pow(max(dot(normal, halfVec), 0.0), 16.0);
    lit += u_light_color * spec * shadow * 0.3;

    // Fog
    vec3 final_color = mix(lit, u_fog_color, v_fog_factor);

    gl_FragColor = vec4(final_color, 1.0);
}
```

**PP instruction breakdown:**
| Operation | Instructions |
|-----------|-------------|
| 4 texture fetches | ~16 (4 each, plus stall time) |
| Normal unpack + normalize | ~6 |
| Diffuse NdotL | ~8 |
| Shadow modulation | ~4 |
| Lightmap combine | ~4 |
| Specular half-vector + normalize | ~6 |
| pow(NdotH, 16) | ~8 (log→mul→exp expansion) |
| Specular combine | ~4 |
| Fog mix | ~3 |
| Output | ~1 |
| **PP total** | **~60–80** |

- Fragment count: ~921,600 pixels × 1.5x average overdraw = ~1,400,000 fragments
- Stencil culling saves ~40% in portaled zones → ~840,000 effective fragments
- Split across 2 PP cores = ~420,000 each
- With texture stalls: ~1–2ms PP time

**GP/PP overlap:** GP processes zone vertices while PP finishes Pass 1 shadow tiles. By the time the GP finishes, PP has resolved the shadow pass and is ready for zone tiles.

**Pass total: ~1.5ms (max of GP and PP, not sum)**

### 4.4 Pass 4: Characters — FBO: Main (same)

**Purpose:** Render skinned, lit, normal-mapped characters.

**GP — Character Vertex Shader:**

Everything from the zone GP, PLUS skeletal animation:

```glsl
// Additional attributes
attribute vec4 a_bone_weights;
attribute vec4 a_bone_indices;

// Additional uniforms
uniform mat4 u_bones[40];  // 30–40 bones typical for EQ humanoids

// Bone blending (pseudo-code)
mat4 skin_matrix = a_bone_weights.x * u_bones[int(a_bone_indices.x)]
                 + a_bone_weights.y * u_bones[int(a_bone_indices.y)]
                 + a_bone_weights.z * u_bones[int(a_bone_indices.z)]
                 + a_bone_weights.w * u_bones[int(a_bone_indices.w)];

vec4 skinned_pos = skin_matrix * vec4(a_position, 1.0);
vec3 skinned_normal = mat3(skin_matrix) * a_normal;
vec3 skinned_tangent = mat3(skin_matrix) * a_tangent;

// Then same TBN + lighting + fog as zone shader
```

**GP instruction breakdown:**
| Operation | Instructions |
|-----------|-------------|
| 4× bone matrix fetch + weight multiply | ~40 |
| Accumulate skinned position | ~16 |
| Skin normal and tangent (mat3 × vec3 × 2) | ~24 |
| MVP + TBN + lighting + fog (same as zone) | ~40 |
| **GP total** | **~90–120** |

- Vertex count: 5–10 characters × 1,500 verts = 7,500–15,000
- GP time: ~1.5–3ms

**PP — Character Fragment Shader:**

Same as zone but WITHOUT lightmap (characters move, so no baked light). Replace lightmap with ambient constant:

```glsl
vec3 ambient = u_ambient_color;
vec3 lit = diffuse.rgb * (ambient + dynamic);
// ... specular, fog same as zone
```

- Saves one texture fetch vs. zone shader
- PP total: ~50–70 instructions, 3 texture fetches
- Characters cover ~10–20% of screen = ~150,000–200,000 fragments
- PP time: ~0.5–1ms

**GP/PP overlap:** GP crunches character vertices while PP is still working zone geometry tiles. They overlap almost entirely.

**Pass total: ~2ms (GP-bound; PP finishes zone work concurrently)**

### 4.5 Pass 5: Water — FBO: Main (same)

**Step 5a: Stencil mark water plane**
```c
glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
glDepthMask(GL_FALSE);
glStencilFunc(GL_ALWAYS, 128, 0xFF);
glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
// Draw water plane quad
```
Time: Negligible.

**Step 5b: Render water with stencil test**
```c
glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
glDepthMask(GL_FALSE);  // water is transparent
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
glStencilFunc(GL_EQUAL, 128, 0xFF);
glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
// Draw water quad with water shader
```

**GP:** Animate UVs using `u_time` uniform. sin/cos use GP lookup table. ~20–30 instructions on 4 vertices.

**PP:**
```glsl
vec4 distortion = texture2D(u_water_distortion, v_uv_distorted);
vec2 offset = (distortion.rg - 0.5) * u_distortion_strength;
vec3 water_color = texture2D(u_water_texture, v_uv_distorted + offset).rgb;
float fresnel = 0.3 + 0.7 * v_fresnel_term;  // fresnel computed in VS
vec3 final_color = mix(water_color, u_water_tint, 0.4);
final_color = mix(final_color, u_fog_color, v_fog_factor);
gl_FragColor = vec4(final_color, u_water_alpha * fresnel);
```
- PP total: ~30–40 instructions, 2 fetches
- Water covers ~10–15% of screen; stencil rejects the rest
- PP time: ~0.3–0.5ms

**Pass total: ~0.4ms**

### 4.6 Pass 6: Spell AoE Decals — FBO: Main (same)

For each AoE (assume 2–3 active):

1. Stencil mark projection volume (color/depth off)
2. Render spell texture with stencil test, additive blend

```glsl
vec4 spell_tex = texture2D(u_spell_texture, v_uv0);
float pulse = sin(u_time * 3.0) * 0.3 + 0.7;
gl_FragColor = spell_tex * u_spell_color * pulse;
```

- PP total: ~15–20 instructions, 1 fetch per decal
- Tiny screen coverage (2–5% each), stencil-masked
- Total for all decals: <0.3ms

### 4.7 Pass 7: Particles — FBO: Main (same)

**CRITICAL: Batch all particles into ONE draw call.** Upload all particle vertices (50 particles × 4 verts = 200 verts) into a single VBO with per-particle data baked into vertex attributes (center, size, color, UV frame). 50 individual draw calls would cost 2–3ms in command overhead alone.

Render back-to-front with alpha blending.

**GP:** Billboard rotation + sprite sheet UV offset + fog. ~30–40 instructions per vertex, 200 vertices total. Negligible.

**PP:**
```glsl
vec4 particle = texture2D(u_particle_atlas, v_uv);
particle *= v_color;
vec3 final_color = mix(particle.rgb, u_fog_color, v_fog_factor);
gl_FragColor = vec4(final_color, particle.a * v_color.a);
```
- PP total: ~10–15 instructions, 1 fetch
- Low coverage: ~5–8%
- PP time: ~0.2–0.4ms

---

## 5. Frame Budget Summary

| Pass | GP Time | PP Time | Overlap? | Net Time |
|------|---------|---------|----------|----------|
| CPU pre-frame | — | — | No | ~2–3ms |
| 1. Shadow map | ~0.3ms | ~0.5ms | No (first GPU pass) | ~0.8ms |
| 2. Portal stencil | ~0ms | ~0.3ms | GP overlaps with PP1 | ~0.3ms |
| 3. Zone geometry | ~1.5ms | ~1.5ms | GP overlaps with PP2 | ~1.5ms |
| 4. Characters | ~2ms | ~0.8ms | GP overlaps with PP3 | ~2ms |
| 5. Water | ~0ms | ~0.4ms | GP overlaps with PP4 | ~0.4ms |
| 6. Spell decals | ~0ms | ~0.3ms | Trivial | ~0.3ms |
| 7. Particles | ~0ms | ~0.3ms | Trivial | ~0.3ms |
| **Subtotal (rendering)** | **~4ms** | **~4ms** | | **~7.5–10ms** |

**Remaining budget for post-pass + UI + driver overhead:** ~23ms at 30fps, ~6ms at 60fps.

### 5.1 Budget Assumptions

- Mali 400 MP2 clocked at 250–500MHz (typical for Allwinner A20/A64)
- DDR3 memory (bandwidth is the real bottleneck, not ALU)
- 720p output resolution
- Indoor dungeon zone with 50,000–80,000 total triangles, culled to 10,000–15,000
- 5–10 visible characters at ~1,500 triangles each
- 50 active particles
- 2–3 spell AoE decals
- Average overdraw 1.5x for dungeon geometry
- Stencil portal culling rejecting ~40% of screen-space fragments
- Texture stall multiplier: 3–4x over pure ALU time estimates

### 5.2 Risk Factors

- **Memory bandwidth:** The estimates above are ALU-optimistic. Real-world performance on low-end SoCs with slow DDR3 could be 1.5–2x worse. Profile on target hardware early.
- **Draw call count:** Each `glDrawElements` has fixed overhead. Stay under 100 draw calls per frame. Batch aggressively.
- **FBO switches:** Each switch flushes and resolves the entire tile buffer. The frame structure above has exactly ONE switch (after the shadow pass). Adding more passes (e.g., multi-pass bloom) is expensive.
- **Shader program switches:** Each switch flushes the PP pipeline. Sort draw calls by shader program to minimize switches. Expected: ~5–6 unique programs (zone, character, water, spell, particle, maybe terrain variant).
- **Uniform uploads:** Bone matrices for characters (40 × mat4 = 640 floats per character) are the largest uniform payload. Consider uniform buffer packing or reducing bone count.

---

## 6. Texture Strategy

### 6.1 Compression

- **All opaque textures:** ETC1 compressed. Already in use.
- **Textures with alpha (spell effects, foliage, transparent armor):**
  - Option A: Two ETC1 textures — one RGB, one with alpha packed into R channel. Costs an extra texture fetch in the fragment shader.
  - Option B: For simple punch-through transparency (tree leaves, fences), use ETC1 + a separate 1-bit alpha mask texture at reduced resolution.
  - Option C: Uncompressed RGBA4444 for small UI/effect textures where quality matters more than memory.

### 6.2 Atlasing

EQ's original textures are small (256×256 or less). Pack into 1024×1024 or 2048×2048 atlases:
- One atlas per zone material type (walls, floors, props)
- One atlas for character armor/skin textures
- One atlas for spell effect textures
- One atlas for particle sprites

Atlasing enables batching — all geometry sharing an atlas can be a single draw call.

### 6.3 Mipmaps

Already in use. Ensure ALL tiling textures are power-of-two for mipmap support. NPOT textures cannot have mipmaps on the Mali 400.

### 6.4 Lightmaps

- Generate offline (bake ambient + static light into lightmaps)
- Second UV set (a_texcoord1) for lightmap coordinates
- Resolution: 1–4 texels per world unit is sufficient for EQ-scale environments
- Pack all lightmaps into atlases to avoid texture switches

---

## 7. Vertex Buffer Strategy

### 7.1 Static Zone Geometry

- Upload once to a static VBO. Never modify.
- Use `GL_STATIC_DRAW` hint.
- Interleaved vertex format for cache efficiency:

```c
struct ZoneVertex {
    float    position[3];    // 12 bytes
    int16_t  normal[3];      // 6 bytes (half-float via GL_OES_vertex_half_float, renormalize in VS)
    int16_t  tangent[3];     // 6 bytes
    float    texcoord0[2];   // 8 bytes (need full precision for FP24 path)
    int16_t  texcoord1[2];   // 4 bytes (lightmap UVs, lower precision OK)
};                           // Total: 36 bytes per vertex
```

### 7.2 Character Meshes

- Static VBOs for mesh data (positions, normals, UVs don't change)
- Bone indices + weights as additional vertex attributes
- Bone matrices uploaded as uniforms per draw call
- Double-buffer dynamic VBOs if streaming animated vertex data

```c
struct CharacterVertex {
    float    position[3];    // 12 bytes
    int16_t  normal[3];      // 6 bytes
    int16_t  tangent[3];     // 6 bytes
    float    texcoord0[2];   // 8 bytes
    uint8_t  bone_indices[4]; // 4 bytes
    uint8_t  bone_weights[4]; // 4 bytes (normalized 0–255)
};                           // Total: 40 bytes per vertex
```

### 7.3 Particles

- Single dynamic VBO for ALL particles, rebuilt each frame
- Upload all 200 vertices (50 particles × 4 corners) in one `glBufferSubData`
- ONE draw call for all particles

```c
struct ParticleVertex {
    float   center[3];      // 12 bytes (particle world position)
    int16_t corner[2];      // 4 bytes (-1/+1 corner offsets)
    int16_t size[2];        // 4 bytes (width, height)
    uint8_t color[4];       // 4 bytes (RGBA tint)
    int16_t texcoord[2];    // 4 bytes (sprite sheet UV)
};                          // Total: 28 bytes per vertex
```

### 7.4 Index Buffers

- Use `GL_UNSIGNED_INT` (via `GL_OES_element_index_uint`) for zone meshes exceeding 65,536 vertices
- Use `GL_UNSIGNED_SHORT` for character meshes (always under 65K verts)

---

## 8. Draw Call Ordering

Within each pass, order draw calls to minimize state changes:

```
1. Sort by shader program (most expensive switch)
2. Within same program: sort by texture atlas
3. Within same atlas: sort front-to-back for opaque geometry (helps early-Z)
4. Transparent objects: sort back-to-front (required for correct blending)
```

**Target draw call counts per frame:**

| Category | Draw Calls | Notes |
|----------|-----------|-------|
| Shadow pass | 1 | Single player character mesh |
| Portal stencil | 3–4 | One per visible portal |
| Zone geometry | 30–60 | Batched by atlas |
| Characters | 5–10 | One per visible character |
| Water | 1–2 | Per water plane |
| Spell decals | 4–6 | Stencil mark + render per decal |
| Particles | 1 | ALL particles in one call |
| **Total** | **~50–85** | Target: stay under 100 |

---

## 9. Occlusion Queries for Portal Optimization

Lima supports `GL_EXT_occlusion_query_boolean`. Use it to enhance portal culling:

1. Before rendering a portal's room geometry, issue an occlusion query on the portal's bounding quad.
2. If zero pixels passed (portal fully occluded by nearer geometry), skip ALL draw calls for that room.
3. Query results lag by 1–2 frames — use the previous frame's result to decide this frame's rendering. Acceptable for slowly-moving camera.

This pairs with stencil portal culling: occlusion queries eliminate entire rooms at the CPU level (skip draw calls entirely), while stencil culling eliminates fragments at the GPU level (skip fragment shading).

---

## 10. Key Implementation Notes

### 10.1 Things That Will Bite You

- **`highp` does nothing in fragment shaders.** All fragment math is FP16. Design around it. Shadow map depth comparisons will have banding — pack depth into RGBA channels or use a small shadow map with aggressive bias.
- **`normalize()` is expensive in FP16.** It's roughly 6 instructions. Avoid normalizing in the fragment shader when you can normalize in the vertex shader and rely on varying interpolation being "close enough."
- **Integer math doesn't exist.** `int` in a fragment shader becomes FP16 with rounding errors. Avoid integer logic in fragment shaders.
- **FBO switches are very expensive.** The tiled renderer must resolve every tile to main memory, then reload when rendering resumes. The frame structure in this doc uses exactly one FBO switch (after the shadow pass). Resist adding more.
- **Branching in fragment shaders is terrible.** The Mali 400 PP evaluates both branches. Use step(), mix(), and clamp() instead of if/else.
- **Draw call overhead is real.** Each glDrawElements has CPU-side overhead that becomes dominant under 100+ calls. Batch everything possible.
- **The GP is usually idle.** If your frame is PP-bound (likely), you have GP headroom. Push more computation to vertex shaders.

### 10.2 Things That Work in Your Favor

- **Tile buffer stencil is free.** Stencil reads/writes happen in fast on-chip SRAM. Stencil tests rejecting fragments before the shader runs is pure win.
- **Overdraw is cheaper than on immediate-mode renderers.** The tiled architecture handles overlapping opaque geometry more efficiently, though complex fragment shaders on overlapping translucent geometry still hurt.
- **Lima exposes extensions the blob didn't.** Occlusion queries, desktop GL 2.1 support, and modern Linux graphics stack integration (zero-copy pipelines) are Lima advantages.
- **EQ's art style is perfect for this hardware.** Low-poly geometry, small textures, mostly opaque surfaces, limited transparency — it's exactly what the Mali 400 was designed for.

### 10.3 Profiling

- Use `MESA_LOADER_DRIVER_OVERRIDE=lima` environment variable to force the Lima driver.
- Use `LIMA_DEBUG=` environment variables (check Mesa docs for current flags).
- Frame timing: `eglSwapBuffers` blocking time indicates GPU-bound vs. CPU-bound.
- If GPU-bound: reduce fragment shader complexity or resolution first.
- If CPU-bound: reduce draw calls via batching, reduce CPU-side culling cost.

---

## 11. Shader Source Reference

Complete shader sources are provided inline in Section 4. The key programs to implement:

1. **`zone.vert` + `zone.frag`** — Lightmap + normal map + 1 light + shadow + fog (Section 4.3)
2. **`character.vert` + `character.frag`** — Skinned + normal map + 1 light + fog (Section 4.4)
3. **`water.vert` + `water.frag`** — Animated UV distortion + fresnel + fog (Section 4.5)
4. **`spell_decal.vert` + `spell_decal.frag`** — Projected texture + pulse animation (Section 4.6)
5. **`particle.vert` + `particle.frag`** — Billboard sprite + fog (Section 4.7)
6. **`shadow.vert` + `shadow.frag`** — Bone skinning + depth-only output (Section 4.1)
7. **`stencil_only.vert` + `stencil_only.frag`** — Passthrough transform, empty fragment (Section 4.2)

Total: 7 shader programs. Sort draw calls to minimize switches between them.

---

## Appendix A: GL State Cheat Sheet Per Pass

| Pass | Color Write | Depth Write | Depth Test | Stencil | Blend | Cull |
|------|------------|------------|------------|---------|-------|------|
| 1. Shadow | OFF | ON (depth FBO) | ON | OFF | OFF | BACK |
| 2. Portal stencil | OFF | OFF | OFF | WRITE (REPLACE) | OFF | OFF |
| 3. Zone geometry | ON | ON | ON | TEST (portal IDs) | OFF | BACK |
| 4. Characters | ON | ON | ON | WRITE (for outline) | OFF | BACK |
| 5a. Water stencil | OFF | OFF | ON | WRITE (128) | OFF | OFF |
| 5b. Water render | ON | OFF | ON | TEST (==128) | ALPHA | OFF |
| 6a. Spell stencil | OFF | OFF | ON | WRITE (per-spell) | OFF | OFF |
| 6b. Spell render | ON | OFF | ON | TEST (per-spell) | ADDITIVE | OFF |
| 7. Particles | ON | OFF | ON (read-only) | OFF | ALPHA | OFF |

## Appendix B: Stencil Value Assignments

| Value | Usage |
|-------|-------|
| 0 | Default / no portal (reject in portaled zones) |
| 1–16 | Portal room IDs |
| 17 | Hostile target outline |
| 18 | Group member outline |
| 19 | Friendly NPC outline |
| 32–63 | Spell AoE decals (one per active spell) |
| 128 | Water surface |
| 255 | Reserved / UI clipping |

## Appendix C: Extension Requirements

```c
// Required extensions — check at startup and fail gracefully
GL_OES_depth_texture            // shadow mapping
GL_OES_element_index_uint       // large zone meshes
GL_OES_vertex_half_float        // bandwidth-efficient vertex formats
GL_OES_compressed_ETC1_RGB8_texture  // texture compression
GL_OES_standard_derivatives     // dFdx/dFdy (if needed for effects)
GL_OES_depth24                  // 24-bit depth buffer

// Optional extensions — enable features if present
GL_EXT_occlusion_query_boolean  // portal occlusion optimization
GL_EXT_texture_format_BGRA8888  // direct BGRA upload for legacy assets
GL_OES_texture_npot             // non-power-of-two textures (limited)
GL_OES_texture_half_float       // FP16 texture formats
GL_OES_get_program_binary       // shader caching
```

