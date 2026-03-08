# WillEQ Shader Overview

All shader source code is embedded as C++ string literals (no external `.glsl`/`.vert`/`.frag` files). The codebase has three shader systems targeting two GPU backends: a built-in GLES2 driver layer, a `ZoneShaderManager` for custom zone/entity materials, and a standalone particle renderer.

---

## 1. Rendering Backends

| Backend | GLSL Version | Compile Guard | Target Hardware |
|---------|-------------|---------------|-----------------|
| GLES2 | GLSL ES 1.00 | `EQT_HAS_GLES2` | Mali 400 (Orange Pi One), future Android 4.4 |
| Desktop GL | GLSL 1.20 | `!EQT_HAS_GLES2` (with `EQT_HAS_DRM`) | Desktop GPU via Mesa/Lima |

The GLES2 path uses `attribute`/`varying`/`precision` qualifiers and explicit vertex attribute binding. The desktop GL path uses `#version 120` with built-in `gl_Vertex`, `gl_Normal`, `gl_Color`, `gl_TexCoord[]`, `gl_MultiTexCoord*`.

### Precision Strategy (Mali 400)

All vertex shaders use `precision highp float` (FP32 on Mali 400's vertex processor). All fragment shaders use `precision mediump float` (FP16 on Mali 400's fragment cores). This matches the hardware's asymmetric precision architecture.

---

## 2. Built-in GLES2 Driver Programs (`COGLES2Shaders`)

**Files:** `docker/irrlicht-drm/COGLES2Shaders.h` + `.cpp`

Six built-in shader programs managed by `COGLES2ShaderManager`. These handle the core Irrlicht rendering pipeline before any custom materials are applied.

### Program Inventory

| ID | Name | VS | FS | Purpose |
|----|------|----|----|---------|
| 0 | Solid3D | `SOLID3D_VS` | `SOLID3D_FS` | Zone geometry, entities, doors |
| 1 | AlphaTest3D | `SOLID3D_VS` | `ALPHA3D_FS_*` | Vegetation, transparent objects |
| 2 | AtlasSolid3D | `ATLAS_SOLID3D_VS` | `SOLID3D_FS` | Atlas zone geometry (opaque) |
| 3 | AtlasAlpha3D | `ATLAS_SOLID3D_VS` | `ATLAS_ALPHA3D_FS_*` | Atlas geometry (dual-texture alpha) |
| 4 | UI2D | `UI2D_VS` | `UI2D_FS` | All 2D UI elements |
| 5 | Color2D | `COLOR2D_VS` | `COLOR2D_FS` | Solid-color 2D rectangles, lines, debug overlays |

### 2.1 Color2D (Program 5)

The simplest program. Transforms a 2D position and passes vertex color through.

**Vertex shader:** Multiplies `aPosition` by `mWorldViewProj`, passes `aColor` as `vColor`.

**Fragment shader:** Outputs `vColor` directly. No texturing, no lighting.

**Attributes used:** `aPosition` (0), `aColor` (2).

### 2.2 UI2D (Program 4)

Textured 2D quads with color modulation for all UI elements.

**Vertex shader:** Same MVP transform as Color2D, plus passes `aTexCoord0` through to the fragment shader.

**Fragment shader:** `gl_FragColor = texture2D(uTexture, vTexCoord) * vColor`. Simple texture-times-color multiplication.

**Attributes used:** `aPosition` (0), `aColor` (2), `aTexCoord0` (3).

### 2.3 Solid3D (Program 0)

The main 3D rendering program. Per-vertex lighting with 8 point lights, directional sun, ambient, tinting, and linear fog.

**Vertex shader (`SOLID3D_VS`):**
- Transforms position by `mWorldViewProj` for clip space
- Transforms position and normal to world space via `mWorld`
- Computes **directional sun light**: `max(dot(worldNormal, -sunDir), 0.0) * sunColor`
- Computes **8 point lights** (unconditional loop): each uses quadratic attenuation `1/(c + l*d + q*d^2 + epsilon)` with standard Lambertian `max(NdotL, 0.0)`
- Combines: `vColor = (ambient + sunLight) * tintColor * vertexColor + pointLighting` (note: point lights are additive, not multiplied by tint — this prevents torchlight from being suppressed at night)
- Computes **linear fog factor**: `clamp((fogEnd - fogDist) / (fogEnd - fogStart), 0, 1)` where `fogDist = length(clipPos.xyz)`

**Fragment shader (`SOLID3D_FS`):**
- `lit = texture * vColor`
- `gl_FragColor = mix(fogColor, lit, fogFactor)` — fog factor of 1.0 means no fog

**Attributes used:** All 5 (`aPosition`, `aNormal`, `aColor`, `aTexCoord0`).

**Varyings:** `vColor` (vec4), `vTexCoord` (vec2), `vFogFactor` (float) — 3 varyings total.

### 2.4 AlphaTest3D (Program 1)

Identical VS to Solid3D. Fragment shader adds `discard` for alpha-tested transparency.

**Two FS variants**, selected at init time based on `GL_OES_standard_derivatives` support:

| Variant | Alpha Test | Notes |
|---------|-----------|-------|
| Base | `if (texColor.a < 0.5) discard` | Hard threshold |
| Derivatives | `threshold = clamp(0.5 - fwidth(texColor.a), 0.1, 0.5)` | Adaptive threshold for smoother vegetation edges. Clamped to [0.1, 0.5] to ensure binary alpha masks (0/1 with no gradient) still discard properly |

### 2.5 AtlasSolid3D (Program 2)

Same lighting pipeline as Solid3D, but reads precomputed atlas UVs from `aTexCoord1` instead of original UVs from `aTexCoord0`. Reuses `SOLID3D_FS`.

**Key difference in VS:** `vTexCoord = aTexCoord1` (atlas UV pre-baked on CPU during mesh building).

**Attributes used:** All 5, including `aTexCoord1` (4).

### 2.6 AtlasAlpha3D (Program 3)

Same VS as AtlasSolid3D. Fragment shader performs **dual-texture alpha testing**: reads RGB from `uTexture` and alpha from `uAlphaTexture` (separate ETC1-compressed alpha page).

```glsl
float alpha = texture2D(uAlphaTexture, vTexCoord).r;
if (alpha < 0.5) discard;
vec4 texColor = texture2D(uTexture, vTexCoord);
```

This dual-texture approach is required because ETC1 (the only texture compression supported by Mali 400) has no alpha channel. RGB and alpha are stored as separate ETC1 textures.

Also has a derivatives variant with the same adaptive threshold as AlphaTest3D.

### Built-in Program Infrastructure

**Vertex attribute binding** (fixed indices, bound before linking):

| Index | Name | Type | Content |
|-------|------|------|---------|
| 0 | `aPosition` | vec3 | Vertex position |
| 1 | `aNormal` | vec3 | Vertex normal |
| 2 | `aColor` | vec4 | Vertex color (RGBA) |
| 3 | `aTexCoord0` | vec2 | Primary texture coordinates |
| 4 | `aTexCoord1` | vec2 | Atlas precomputed UVs |

**Uniform caching:** `SOGLES2ProgramUniforms` struct caches `glGetUniformLocation` results per program at init time. Locations are queried once and reused across all frames.

**Shader binary caching:** Supports `GL_OES_get_program_binary` extension. Compiled program binaries are saved to disk (FNV-1a hash of GPU ID + source as filename) and restored on next startup, skipping recompilation.

---

## 3. Zone Shader Manager (`ZoneShaderManager`)

**Files:** `include/client/graphics/zone_shader.h` + `src/client/graphics/zone_shader.cpp`

Custom Irrlicht materials registered via `addHighLevelShaderMaterial()`. These extend the built-in programs with per-pixel player lighting, wind animation, and an active material switching system. Each backend (GLES2 / desktop GL) has its own complete set of shader source strings, selected at compile time.

### Material Type Inventory

| Material | VS | FS | Callback | Purpose |
|----------|----|----|----------|---------|
| Solid | Standard | Solid | `ShaderCallback` | Opaque zone geometry |
| AlphaTest | Standard | Alpha | `ShaderCallback` | Vegetation, transparent objects |
| AtlasSolid | Atlas | AtlasSolid | `AtlasShaderCallback` | Atlas zone geometry (opaque) |
| AtlasAlpha | Atlas | AtlasAlpha | `AtlasShaderCallback` | Atlas geometry (dual-texture alpha) |
| WindAlphaTest | Wind | Alpha | `WindShaderCallback` | Tree objects with wind animation |
| LW Solid | LW Standard | LW Solid | `ShaderCallback` | Opaque, no per-pixel player light |
| LW AlphaTest | LW Standard | LW Alpha | `ShaderCallback` | Alpha-test, no per-pixel player light |
| LW AtlasSolid | LW Atlas | LW Solid | `AtlasShaderCallback` | Atlas opaque, no per-pixel player light |
| LW AtlasAlpha | LW Atlas | LW AtlasAlpha | `AtlasShaderCallback` | Atlas alpha, no per-pixel player light |
| LW WindAlpha | LW Wind | LW Alpha | `WindShaderCallback` | Wind trees, no per-pixel player light |

"LW" = Lightweight. These variants are GLES2-only.

### 3.1 Standard Shaders (GLES2)

**Vertex shader (`VERTEX_SHADER_SRC`):**

Similar to `SOLID3D_VS` but with a key difference: **point light 0 (player light) is deferred to the fragment shader** for per-pixel evaluation. The VS computes lights 1-7 per-vertex and passes `vWorldPos` + `vWorldNormal` to the FS.

```glsl
// Point lights 1-7 per-vertex (light[0] = player light, computed per-pixel in FS)
if (uNumPointLights > 1) {
    for (int i = 1; i < 8; i++) { ... }
}
```

The lighting combination is:
```glsl
vColor = (baseLighting * tintColor) * vertexColor + pointLighting
```
Point lights are additive (not multiplied by tint), so they remain visible at night.

**Varyings:** 5 total — `vColor` (vec4), `vTexCoord` (vec2), `vFogFactor` (float), `vWorldPos` (vec3), `vWorldNormal` (vec3).

**Fragment shader (`FRAGMENT_SHADER_SOLID_SRC`):**

Per-pixel player light evaluation:
```glsl
if (uPlayerLightColor.x + uPlayerLightColor.y + uPlayerLightColor.z > 0.0) {
    // Quadratic-attenuated Lambertian point light
    pLight = uPlayerLightColor * NdotL * attenuation;
}
lit = (texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a)
```

The conditional branch on color sum avoids the `length()`, `normalize()`, and division operations when the player light is off (`/plight` toggle or no light equipped).

**Alpha-test fragment shaders:** Same per-pixel player light logic, plus `discard` on alpha < 0.5. The derivatives variant uses `fwidth()` but is disabled on Lima driver (which returns degenerate values).

### 3.2 Atlas Shaders (GLES2)

Same as standard shaders but with `vTexCoord = aTexCoord1` (precomputed atlas UV). The atlas alpha FS uses dual-texture sampling like the built-in `ATLAS_ALPHA3D_FS`.

### 3.3 Wind Shader (GLES2)

Extends the standard VS with **vertex displacement for tree animation**. Wind-specific uniforms:

| Uniform | Type | Source |
|---------|------|--------|
| `uWindTime` | float | Elapsed time in seconds |
| `uWindParams` | vec4 | (baseStrength, baseFreq, gustStrength, gustFreq) |
| `uMeshYBounds` | vec2 | (minY, maxY) in local mesh space |

**Wind displacement algorithm:**
1. Compute normalized height within mesh: `(vertex.y - minY) / (maxY - minY)`
2. Apply influence curve: no sway below 30% height, quadratic ramp above: `influence = ((h - 0.3) / 0.7)^2`
3. Generate per-tree seed from world-space mesh center: `fract(sin(x * 12.9898 + z * 78.233) * 43758.5453) * 2pi`
4. Compute displacement on X and Z axes: base sway + gust modulation

```glsl
float sway = sin(windTime * baseFreq * 2pi + seed);
float gust = sin(windTime * gustFreq * 2pi + seed * 0.7) * 0.5 + 0.5;
float strength = baseStr + gustStr * gust;
pos.x += influence * strength * sway;
pos.z += influence * strength * sin(windTime * baseFreq * 2pi + seed + pi/2);
```

The 90-degree phase offset on Z creates elliptical motion rather than linear oscillation.

### 3.4 Lightweight Shaders (GLES2 only)

Optimized variants that eliminate `vWorldPos` and `vWorldNormal` varyings (6 fewer floats interpolated per fragment). All 8 point lights are computed per-vertex with standard Lambertian (single loop, no split between VS and FS).

The FS is trivial — identical to the built-in GLES2 programs:
```glsl
vec4 lit = texColor * vColor;
gl_FragColor = mix(uFogColor, lit, vFogFactor);
```

**Varyings:** 3 total — `vColor` (vec4), `vTexCoord` (vec2), `vFogFactor` (float). This fits comfortably within Mali 400's varying interpolator limits.

Lightweight variants exist for all 5 material types (solid, alpha, atlas solid, atlas alpha, wind).

### 3.5 Desktop GL Shaders (GLSL 1.20)

Functionally equivalent to the GLES2 standard shaders but using OpenGL 2.1 conventions:

- `gl_Vertex`, `gl_Normal`, `gl_Color`, `gl_MultiTexCoord0/1` instead of explicit attributes
- `gl_TexCoord[0]` built-in varying for texture coordinates (comment notes Mali 400/Lima may route these through dedicated high-precision interpolation hardware)
- All 8 point lights in a single VS loop guarded by `if (uNumPointLights > 0)`
- No lightweight variants (desktop has plenty of varying capacity)
- Per-pixel player light uniforms are passed to the FS but the desktop path currently uses the same computation

### 3.6 Active Material Switching

`ZoneShaderManager` provides `getActive*()` methods that return either the per-pixel or lightweight material variant based on a runtime toggle (`/plight` command):

```cpp
irr::s32 getActiveSolid() const {
    if (perPixelPlayerLight_ || materialLWSolid_ < 0) return materialSolid_;
    return materialLWSolid_;
}
```

Default is lightweight (per-pixel player light OFF).

---

## 4. Shader Callbacks

Three callback classes implement `IShaderConstantSetCallBack` to upload uniforms to the GPU each time a material is applied.

### 4.1 ShaderCallback

Used by solid and alpha-test materials. Handles:
- **Per-node uniforms** (change every draw call): `mWorldViewProj`, `mWorld`
- **Per-frame uniforms** (set once per frame, skipped on subsequent nodes): Sun direction/color, ambient, tint, fog params, 8 point light arrays, player light params

GLES2 path caches `glGetUniformLocation` results on first invocation (`-2` sentinel = unresolved). Frame skip optimization uses `frameId_` counter.

Desktop GL path uses `services->setVertexShaderConstant("name", ...)` — string-based lookup, no manual caching needed.

### 4.2 AtlasShaderCallback

Extends ShaderCallback with atlas page texture binding:
- `OnSetMaterial()` extracts page indices from `material.MaterialTypeParam` (RGB page) and `MaterialTypeParam2` (alpha page)
- Binds atlas page textures to `GL_TEXTURE0` (RGB) and `GL_TEXTURE1` (alpha) via raw `glBindTexture` calls
- Tracks `lastBoundRgbTex_`/`lastBoundAlphaTex_` to skip redundant binds when consecutive regions use the same atlas page

### 4.3 WindShaderCallback

Extends ShaderCallback with wind-specific uniforms:
- `OnSetMaterial()` extracts mesh Y bounds from `MaterialTypeParam`/`MaterialTypeParam2`
- Per-node: `uMeshYBounds` (different per tree mesh)
- Per-frame: `uWindTime`, `uWindParams`

---

## 5. Particle Renderer (`UnifiedParticleRenderer`)

**File:** `src/client/graphics/environment/unified_particle_renderer.cpp`

Standalone GLES2-only shader program for weather and spell effect particles using GL point sprites.

### 5.1 Vertex Shader (`POINT_SPRITE_VS`)

Uses separate `uView` and `uProj` matrices (not combined MVP).

**Point size calculation:**
```glsl
gl_PointSize = clamp(pointSize * screenHeight / clipPos.w, 1.0, 100.0);
```
World-space particle size stored in `aTexCoord0.x`; perspective division by clip W gives screen-space scaling.

**Fog:** Linear fog computed in eye space (`length(eyePos.xyz)`), matching zone geometry fog convention.

**Per-particle data packing:**

| Attribute | Index | Content |
|-----------|-------|---------|
| `aPosition` | 0 | World-space position (vec3) |
| `aColor` | 2 | Particle color with alpha fade (vec4) |
| `aTexCoord0` | 3 | .x = world-space size, .y = rotation angle |
| `aTexCoord1` | 4 | Atlas sub-region offset (vec2) |

### 5.2 Fragment Shader (`POINT_SPRITE_FS`)

**Point coordinate rotation** for angled particles (rain streaks):
```glsl
if (vRotation != 0.0) {
    pc -= 0.5;  // Center at origin
    pc = vec2(pc.x * c - pc.y * s, pc.x * s + pc.y * c);  // 2D rotation
    pc += 0.5;  // Back to [0,1]
}
```

**Atlas sampling:** Maps rotated `gl_PointCoord` [0,1] to atlas sub-region: `uv = atlasOffset + pointCoord * atlasRegionSize`. Atlas is a 4x4 grid (`atlasRegionSize = 0.25, 0.25`).

**Fog handling:** Multiplicative fade (`color.rgb *= vFogFactor`) instead of `mix()`. This is physically correct for light-emitting particles with additive blending — `mix()` would inject fog color into every particle even with `GL_ONE, GL_ONE` blend, producing visible dots on dark backgrounds.

**Early discard:** `if (color.a < 0.004) discard` — eliminates fully transparent fragments for tile-based GPU efficiency.

### 5.3 Blend Modes

Particles are sorted into two batches, each drawn with a single `glDrawArrays(GL_POINTS)` call:

| Mode | Blend Func | Use Case |
|------|-----------|----------|
| Additive | `GL_ONE, GL_ONE` | Fire, spell glows, light particles |
| Alpha | `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` | Smoke, rain, dust |

Depth test is ON (particles occlude behind walls) but depth write is OFF (particles don't occlude each other).

---

## 6. Material Renderers (`COGLES2MaterialRenderer`)

**Files:** `docker/irrlicht-drm/COGLES2MaterialRenderer.h` + `.cpp`

Thin wrapper classes that declare transparency behavior for the GLES2 driver's material system. Actual GL state setup (blend, depth, cull) is handled in `COpenGLES2Driver::applyMaterialState`.

| Class | Transparent? | Notes |
|-------|-------------|-------|
| `COGLES2MaterialRenderer_SOLID` | No | Default opaque |
| `COGLES2MaterialRenderer_TRANSPARENT_ALPHA_CHANNEL` | Yes | Alpha blending |
| `COGLES2MaterialRenderer_TRANSPARENT_ALPHA_CHANNEL_REF` | No | Alpha test via `discard` — not truly transparent |
| `COGLES2MaterialRenderer_TRANSPARENT_ADD_COLOR` | Yes | Additive blending |
| `COGLES2MaterialRenderer_TRANSPARENT_VERTEX_ALPHA` | Yes | Vertex alpha blending |

---

## 7. Uniform Reference

### Global Uniforms (all 3D programs)

| Uniform | Type | Frequency | Description |
|---------|------|-----------|-------------|
| `mWorldViewProj` | mat4 | Per-node | Combined world-view-projection matrix |
| `mWorld` | mat4 | Per-node | World transform (for lighting in world space) |
| `uSunDir` | vec3 | Per-frame | Directional light direction |
| `uSunColor` | vec3 | Per-frame | Directional light color |
| `uAmbientColor` | vec3 | Per-frame | Scene ambient color |
| `uTintColor` | vec3 | Per-frame | Day/night tint multiplier |
| `uFogStart` | float | Per-frame | Fog start distance |
| `uFogEnd` | float | Per-frame | Fog end distance |
| `uFogColor` | vec4 | Per-frame | Fog color (FS uniform) |
| `uTexture` | sampler2D | Per-frame | Texture unit 0 |
| `uLightPos[8]` | vec3[8] | Per-frame | Point light world positions |
| `uLightColor[8]` | vec3[8] | Per-frame | Point light colors |
| `uLightAtten[8]` | vec3[8] | Per-frame | Point light attenuation (constant, linear, quadratic) |

### Per-pixel Player Light Uniforms (zone shader standard variants only)

| Uniform | Type | Description |
|---------|------|-------------|
| `uPlayerLightPos` | vec3 | Player light world position |
| `uPlayerLightColor` | vec3 | Player light color (zero = disabled) |
| `uPlayerLightAtten` | vec3 | Player light attenuation (c, l, q) |
| `uNumPointLights` | int | Active point light count |

### Atlas-specific Uniforms

| Uniform | Type | Description |
|---------|------|-------------|
| `uAlphaTexture` | sampler2D | Alpha page texture (unit 1), for dual-ETC1 |

### Wind-specific Uniforms

| Uniform | Type | Description |
|---------|------|-------------|
| `uWindTime` | float | Elapsed time in seconds |
| `uWindParams` | vec4 | (baseStrength, baseFreq, gustStrength, gustFreq) |
| `uMeshYBounds` | vec2 | Per-mesh (minY, maxY) in local space |

### Particle-specific Uniforms

| Uniform | Type | Description |
|---------|------|-------------|
| `uProj` | mat4 | Projection matrix (separate from view) |
| `uView` | mat4 | View matrix (separate from projection) |
| `uScreenHeight` | float | Screen height in pixels (for point size scaling) |
| `uAtlasRegionSize` | vec2 | Atlas sub-region size (0.25, 0.25 for 4x4 grid) |

---

## 8. Lighting Model Summary

| Component | Computation | Location |
|-----------|------------|----------|
| Sun (directional) | Lambertian: `max(dot(N, -sunDir), 0) * sunColor` | VS |
| Ambient | Constant: `uAmbientColor` | VS |
| Day/night tint | Multiplied into base lighting: `(ambient + sun) * tintColor` | VS |
| Point lights 1-7 | Lambertian + quadratic attenuation, additive (not tinted) | VS |
| Player light (0) | Lambertian + quadratic attenuation, per-pixel | FS (standard) or VS (lightweight) |
| Vertex color | EQ-baked vertex colors, multiplied after tint | VS |
| Fog | Linear: `mix(fogColor, litColor, fogFactor)` | FS |

**Attenuation formula:** `1.0 / (constant + linear * d + quadratic * d^2 + 0.0001)`

The epsilon terms (`0.001` on distance, `0.0001` on denominator) prevent division-by-zero and infinity when a light is at the vertex position.

**Inactive light handling:** `clearPointLights()` sets attenuation to `(1, 0, 0)` for all slots, making inactive lights contribute `lightColor * NdotL * 1.0` — but since `lightColor` is zeroed, the contribution is zero without wasting precision on near-infinity attenuation values.

---

## 9. Architecture Notes

### Shader Program Count

| System | Programs | Notes |
|--------|----------|-------|
| Built-in GLES2 | 6 | Color2D, UI2D, Solid3D, AlphaTest3D, AtlasSolid3D, AtlasAlpha3D |
| Zone shader (GLES2) | 10 | Standard(2) + Atlas(2) + Wind(1) + LW variants(5) |
| Zone shader (Desktop GL) | 5 | Standard(2) + Atlas(2) + Wind(1) |
| Particle renderer | 1 | Point sprite program |
| **Total (GLES2)** | **17** | |
| **Total (Desktop GL)** | **11** | Built-in programs not used on desktop |

### VS/FS Reuse Patterns

Several programs share vertex or fragment shaders:
- `SOLID3D_FS` is reused by Solid3D, AtlasSolid3D, and corresponding zone shader solids
- `SOLID3D_VS` is reused by both Solid3D and AlphaTest3D
- `ATLAS_SOLID3D_VS` is reused by both AtlasSolid3D and AtlasAlpha3D
- Lightweight FS variants are shared across standard and wind materials
- Desktop GL solid and atlas solid share the same FS

### Per-frame Optimization

Zone shader callbacks use a `frameId_` counter to avoid re-uploading per-frame uniforms (sun, fog, lights) on every draw call within a frame. Only per-node uniforms (`mWorldViewProj`, `mWorld`) are uploaded for every node.

### Binary Cache

The `COGLES2ShaderManager` supports `GL_OES_get_program_binary` for caching compiled shader binaries to disk. Cache keys are FNV-1a hashes of `gpuId + vsSource + fsSource`. This eliminates shader compilation latency on subsequent startups — significant on the Mali 400 where compilation can take several seconds.

### Mali 400 Design Considerations

1. **All-per-vertex lighting in VS:** The 8-point-light loop with `length()`, `normalize()`, and division runs on the Mali 400 vertex processor (FP32, 1 core). Fragment cores (FP16, 4 cores) get a trivial FS (texture sample + multiply + fog mix).
2. **Lightweight variants:** Reduce varying count from 5 to 3, saving interpolation bandwidth on the tile-based architecture.
3. **ETC1 dual-texture alpha:** Mali 400's only hardware-compressed format. RGB and alpha stored separately, sampled in FS from two texture units.
4. **`gl_TexCoord[]` on desktop GL:** Comments note that Mali 400's Lima driver may route built-in texture coordinate varyings through dedicated high-precision interpolation hardware, avoiding FP16 blocky artifacts on large triangles.
5. **fwidth() disabled on Lima:** The Lima driver's standard derivatives implementation returns degenerate values, so the adaptive alpha threshold is skipped for zone shader alpha-test materials (though it's offered for the built-in programs if the extension reports as available).
