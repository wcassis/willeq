# WillEQ Shader Permutation Handoff — Light Count Variants

## Problem Statement

The current GLES2 Standard vertex shader computes 8 point lights in a loop guarded by `if (uNumPointLights > 1)`. The Mali-400's fragment processor does not support true dynamic branching — conditionals are flattened, meaning both sides of every `if` execute and the result is selected afterward. This causes the compiled shader to emit code for all 8 lights plus the branch overhead, exceeding the Mali-400's 512-instruction limit when combined with the per-pixel player light logic and other material features (wind, atlas UVs, etc.).

The current workaround is a binary choice: built-in shaders (all 8 lights per-vertex, no per-pixel player light, fits in 512) or Standard shaders (per-pixel player light with branching loop for lights 1-7, exceeds 512 or incurs a 10fps penalty). Attempting to add conditionals like `if (lightEnabled[i])` doubles the instruction count due to branch flattening and also exceeds 512.

The Lightweight (LW) shader variants work because they compute all 8 lights in a straight unconditional loop with no per-pixel split — the fragment shader is trivial (texture × color + fog). These fit comfortably under 512.

## Solution: Compile-Time Light Count Permutations

Instead of one Standard VS with a runtime-conditional loop, generate separate shader programs for each active vertex light count (0 through 7). Each variant has a fixed unrolled loop with no branching — the 2-light variant literally only contains math for 2 lights. All variants stay well under 512 instructions.

Light 0 is always the player light and is always deferred to the fragment shader for per-pixel evaluation, matching the current Standard shader behavior. The vertex shader only computes lights 1 through N.

### Instruction Budget Estimates

Per-light vertex shader cost (Lambertian + quadratic attenuation): ~20-25 instructions (vec3 subtract, length, normalize, dot, attenuation division, multiply-accumulate).

| Variant | VS Lights | Estimated VS Instructions | Status |
|---------|-----------|--------------------------|--------|
| 0 vertex lights | None (player-only) | ~40-50 (transforms + fog) | Well under 512 |
| 1 vertex light | Light 1 | ~65-75 | Well under 512 |
| 2 vertex lights | Lights 1-2 | ~90-100 | Well under 512 |
| 3 vertex lights | Lights 1-3 | ~115-125 | Well under 512 |
| 4 vertex lights | Lights 1-4 | ~140-150 | Under 512 |
| 5 vertex lights | Lights 1-5 | ~165-175 | Under 512 |
| 6 vertex lights | Lights 1-6 | ~190-200 | Under 512 |
| 7 vertex lights | Lights 1-7 | ~215-225 | Under 512 |

Wind variants add ~30-40 instructions for displacement. Still under 512 even at 7 lights.

## Architecture

### Current Program Inventory (17 total GLES2)

From the shader overview:

- Built-in GLES2: 6 programs (Color2D, UI2D, Solid3D, AlphaTest3D, AtlasSolid3D, AtlasAlpha3D)
- Zone shader Standard: 5 programs (Solid, AlphaTest, AtlasSolid, AtlasAlpha, WindAlphaTest)
- Zone shader Lightweight: 5 programs (LW Solid, LW AlphaTest, LW AtlasSolid, LW AtlasAlpha, LW WindAlpha)
- Particle renderer: 1 program

### New Program Inventory

The 5 Standard zone shader programs each become 8 variants (0-7 vertex lights). The LW variants, built-in programs, and particle renderer are unchanged.

| System | Programs | Notes |
|--------|----------|-------|
| Built-in GLES2 | 6 | Unchanged |
| Zone shader Standard | 40 | 5 material types × 8 light counts |
| Zone shader Lightweight | 5 | Unchanged |
| Particle renderer | 1 | Unchanged |
| **Total GLES2** | **52** | Up from 17 |

The binary cache (`GL_OES_get_program_binary`) means compilation only happens once per variant ever, not per session. Runtime memory for 52 linked programs is trivial.

### Fragment Shaders Are Unchanged

The per-pixel player light fragment shader is identical regardless of how many vertex lights the VS computed. The FS receives `vColor` (which includes accumulated vertex lighting from however many lights the VS processed) and adds the per-pixel player light contribution. No FS changes needed.

The same applies to the alpha-test, atlas, and atlas-alpha FS variants — all unchanged.

## Implementation Details

### 1. VS Source Generation

Replace the 5 static Standard VS string literals with a generator function. The function takes the vertex light count and material flags (atlas, wind) and produces a VS source string with an unrolled loop.

**Key file locations:**
- Zone shader header: `include/client/graphics/zone_shader.h`
- Zone shader implementation: `src/client/graphics/zone_shader.cpp`
- GLES2 VS source strings are embedded as C++ string literals in the zone shader .cpp file, selected at compile time by `EQT_HAS_GLES2`

**Generator pseudocode:**

```cpp
std::string buildStandardVS(int numVertexLights, bool isAtlas, bool isWind) {
    std::string src;
    
    // Precision header
    src += "precision highp float;\n";
    
    // Attributes — same as current Standard VS
    src += "attribute vec3 aPosition;\n";
    src += "attribute vec3 aNormal;\n";
    src += "attribute vec4 aColor;\n";
    src += "attribute vec2 aTexCoord0;\n";
    if (isAtlas) {
        src += "attribute vec2 aTexCoord1;\n";
    }
    
    // Uniforms — matrices, sun, ambient, tint, fog
    src += "uniform mat4 mWorldViewProj;\n";
    src += "uniform mat4 mWorld;\n";
    src += "uniform vec3 uSunDir;\n";
    src += "uniform vec3 uSunColor;\n";
    src += "uniform vec3 uAmbientColor;\n";
    src += "uniform vec3 uTintColor;\n";
    src += "uniform float uFogStart;\n";
    src += "uniform float uFogEnd;\n";
    
    // Light uniforms — only declare array size actually needed
    // Array size is numVertexLights + 1 because index 0 is the player light
    // (reserved, not used in VS, but keeps indexing consistent)
    // If numVertexLights == 0, still need array size 1 for the player light slot
    int lightArraySize = numVertexLights + 1;
    if (lightArraySize < 1) lightArraySize = 1;
    src += "uniform vec3 uLightPos[" + std::to_string(lightArraySize) + "];\n";
    src += "uniform vec3 uLightColor[" + std::to_string(lightArraySize) + "];\n";
    src += "uniform vec3 uLightAtten[" + std::to_string(lightArraySize) + "];\n";
    
    // Wind uniforms if needed
    if (isWind) {
        src += "uniform float uWindTime;\n";
        src += "uniform vec4 uWindParams;\n";
        src += "uniform vec2 uMeshYBounds;\n";
    }
    
    // Varyings — same as current Standard VS (5 total)
    src += "varying vec4 vColor;\n";
    src += "varying vec2 vTexCoord;\n";
    src += "varying float vFogFactor;\n";
    src += "varying vec3 vWorldPos;\n";
    src += "varying vec3 vWorldNormal;\n";
    
    // Main function
    src += "void main() {\n";
    
    // Wind displacement (if applicable) — same algorithm as current WindVS
    if (isWind) {
        // ... existing wind displacement code ...
        // operates on aPosition, writes to local vec3 pos
        // see section 3.3 of shader_overview.md for full algorithm
        src += "  vec3 pos = aPosition;\n";
        src += "  // [wind displacement applied to pos here]\n";
        src += "  gl_Position = mWorldViewProj * vec4(pos, 1.0);\n";
        src += "  vec3 worldPos = (mWorld * vec4(pos, 1.0)).xyz;\n";
    } else {
        src += "  gl_Position = mWorldViewProj * vec4(aPosition, 1.0);\n";
        src += "  vec3 worldPos = (mWorld * vec4(aPosition, 1.0)).xyz;\n";
    }
    
    src += "  vec3 worldNormal = normalize((mWorld * vec4(aNormal, 0.0)).xyz);\n";
    
    // Sun + ambient (same as current)
    src += "  float sunNdotL = max(dot(worldNormal, -uSunDir), 0.0);\n";
    src += "  vec3 baseLighting = uAmbientColor + uSunColor * sunNdotL;\n";
    
    // Unrolled point light loop — NO BRANCHES, NO LOOP VARIABLE
    if (numVertexLights > 0) {
        src += "  vec3 pointLighting = vec3(0.0);\n";
        for (int i = 1; i <= numVertexLights; i++) {
            std::string idx = std::to_string(i);
            src += "  {\n";
            src += "    vec3 lightDir = uLightPos[" + idx + "] - worldPos;\n";
            src += "    float dist = length(lightDir) + 0.001;\n";
            src += "    lightDir /= dist;\n";
            src += "    float NdotL = max(dot(worldNormal, lightDir), 0.0);\n";
            src += "    float atten = 1.0 / (uLightAtten[" + idx + "].x + "
                        "uLightAtten[" + idx + "].y * dist + "
                        "uLightAtten[" + idx + "].z * dist * dist + 0.0001);\n";
            src += "    pointLighting += uLightColor[" + idx + "] * NdotL * atten;\n";
            src += "  }\n";
        }
    }
    
    // Combine lighting — matches current formula:
    // vColor = (baseLighting * tintColor) * vertexColor + pointLighting
    // Point lights are additive, not multiplied by tint
    if (numVertexLights > 0) {
        src += "  vColor = vec4((baseLighting * uTintColor) * aColor.rgb + pointLighting, aColor.a);\n";
    } else {
        src += "  vColor = vec4((baseLighting * uTintColor) * aColor.rgb, aColor.a);\n";
    }
    
    // Texture coordinates
    if (isAtlas) {
        src += "  vTexCoord = aTexCoord1;\n";  // atlas precomputed UVs
    } else {
        src += "  vTexCoord = aTexCoord0;\n";
    }
    
    // Fog — same as current: linear, distance = length(clipPos.xyz)
    src += "  float fogDist = length(gl_Position.xyz);\n";
    src += "  vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);\n";
    
    // Pass world pos/normal for per-pixel player light in FS
    src += "  vWorldPos = worldPos;\n";
    src += "  vWorldNormal = worldNormal;\n";
    
    src += "}\n";
    
    return src;
}
```

**IMPORTANT:** The above is pseudocode showing the structure. The actual wind displacement algorithm should be copied verbatim from the current `WIND_VERTEX_SHADER_SRC` — see section 3.3 of the shader overview for the full algorithm (height normalization, influence curve, per-tree seed, base sway + gust modulation with elliptical motion).

### 2. Program Registration in ZoneShaderManager

**Current structure (from zone_shader.h):** The manager stores one material ID per material type:

```cpp
irr::s32 materialSolid_;
irr::s32 materialAlphaTest_;
irr::s32 materialAtlasSolid_;
irr::s32 materialAtlasAlpha_;
irr::s32 materialWindAlpha_;
// plus LW variants
irr::s32 materialLWSolid_;
// ... etc
```

**New structure:** Replace each single material ID with an array indexed by vertex light count:

```cpp
static const int MAX_VERTEX_LIGHTS = 7;  // lights 1-7, light 0 is player (per-pixel)

// Standard variants indexed by vertex light count [0..7]
irr::s32 materialSolid_[MAX_VERTEX_LIGHTS + 1];
irr::s32 materialAlphaTest_[MAX_VERTEX_LIGHTS + 1];
irr::s32 materialAtlasSolid_[MAX_VERTEX_LIGHTS + 1];
irr::s32 materialAtlasAlpha_[MAX_VERTEX_LIGHTS + 1];
irr::s32 materialWindAlpha_[MAX_VERTEX_LIGHTS + 1];

// LW variants unchanged — single material ID each
irr::s32 materialLWSolid_;
irr::s32 materialLWAlphaTest_;
irr::s32 materialLWAtlasSolid_;
irr::s32 materialLWAtlasAlpha_;
irr::s32 materialLWWindAlpha_;
```

**Registration loop during init:**

```cpp
for (int n = 0; n <= MAX_VERTEX_LIGHTS; n++) {
    std::string vs = buildStandardVS(n, /*isAtlas=*/false, /*isWind=*/false);
    materialSolid_[n] = driver->addHighLevelShaderMaterial(
        vs.c_str(), "main", video::EVST_VS_1_1,
        FRAGMENT_SHADER_SOLID_SRC, "main", video::EPST_PS_1_1,
        &shaderCallback_, video::EMT_SOLID);
        
    materialAlphaTest_[n] = driver->addHighLevelShaderMaterial(
        vs.c_str(), "main", video::EVST_VS_1_1,
        FRAGMENT_SHADER_ALPHA_SRC, "main", video::EPST_PS_1_1,
        &shaderCallback_, video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
    
    std::string atlasVS = buildStandardVS(n, /*isAtlas=*/true, /*isWind=*/false);
    materialAtlasSolid_[n] = driver->addHighLevelShaderMaterial(
        atlasVS.c_str(), "main", video::EVST_VS_1_1,
        FRAGMENT_SHADER_SOLID_SRC, "main", video::EPST_PS_1_1,
        &atlasShaderCallback_, video::EMT_SOLID);
        
    materialAtlasAlpha_[n] = driver->addHighLevelShaderMaterial(
        atlasVS.c_str(), "main", video::EVST_VS_1_1,
        FRAGMENT_SHADER_ATLAS_ALPHA_SRC, "main", video::EPST_PS_1_1,
        &atlasShaderCallback_, video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
    
    std::string windVS = buildStandardVS(n, /*isAtlas=*/false, /*isWind=*/true);
    materialWindAlpha_[n] = driver->addHighLevelShaderMaterial(
        windVS.c_str(), "main", video::EVST_VS_1_1,
        FRAGMENT_SHADER_ALPHA_SRC, "main", video::EPST_PS_1_1,
        &windShaderCallback_, video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
}
```

### 3. Material Selection API

**Current API (from zone_shader.h):**

```cpp
irr::s32 getActiveSolid() const {
    if (perPixelPlayerLight_ || materialLWSolid_ < 0) return materialSolid_;
    return materialLWSolid_;
}
```

**New API — add vertex light count parameter:**

```cpp
irr::s32 getActiveSolid(int vertexLightCount = 0) const {
    if (!perPixelPlayerLight_ && materialLWSolid_ >= 0)
        return materialLWSolid_;  // LW path unchanged, ignores light count
    int idx = (vertexLightCount < 0) ? 0 : 
              (vertexLightCount > MAX_VERTEX_LIGHTS) ? MAX_VERTEX_LIGHTS : 
              vertexLightCount;
    return materialSolid_[idx];
}

// Same pattern for getActiveAlphaTest(), getActiveAtlasSolid(), 
// getActiveAtlasAlpha(), getActiveWindAlpha()
```

The calling code (entity/zone rendering) already knows the active light count from the light sorting step. It passes this count when requesting the material.

### 4. Uniform Upload Changes in ShaderCallback

**Current behavior:** `ShaderCallback` uploads `uLightPos[8]`, `uLightColor[8]`, `uLightAtten[8]` as flat arrays of 8 elements every frame.

**Two options for the new behavior:**

**Option A (simplest, recommended):** Keep uploading all 8 slots regardless of variant. The shader variant only reads indices 0 through N, so the extra uploads are harmless. The `uNumPointLights` uniform can be removed from Standard variants since the light count is now baked into the shader. The per-frame uniform skip optimization (`frameId_`) continues to work as-is.

Note: `glUniform3fv` with count=8 for arrays sized smaller than 8 in the shader will generate a GL error on some drivers. If this is an issue, use Option B.

**Option B (slightly more work):** Track the current variant's light count and only upload that many array elements. This saves a few `glUniform3fv` calls per frame but requires the callback to know which variant is active.

```cpp
// Option B: upload only active lights
void ShaderCallback::OnSetConstants(IMaterialRendererServices* services, s32 userData) {
    // ... per-node uniforms (mWorldViewProj, mWorld) as before ...
    
    if (frameId_ != currentFrame_) {
        // per-frame uniforms
        // ... sun, ambient, tint, fog as before ...
        
        int lightCount = currentVertexLightCount_ + 1; // +1 for player light at index 0
        services->setVertexShaderConstant("uLightPos", lightPosData, lightCount);
        services->setVertexShaderConstant("uLightColor", lightColorData, lightCount);
        services->setVertexShaderConstant("uLightAtten", lightAttenData, lightCount);
        
        // player light FS uniforms unchanged
        services->setPixelShaderConstant("uPlayerLightPos", ...);
        services->setPixelShaderConstant("uPlayerLightColor", ...);
        services->setPixelShaderConstant("uPlayerLightAtten", ...);
        
        frameId_ = currentFrame_;
    }
}
```

**Recommendation:** Start with Option A for simplicity. If GL errors appear from oversized array uploads, switch to Option B.

**IMPORTANT:** The `uNumPointLights` uniform is still needed for the LW variants (they use it to guard their loop) and for the Standard FS (the per-pixel player light check `if (uPlayerLightColor.x + ... > 0.0)` doesn't use it, but verify). If it's only used in the Standard VS conditional that we're eliminating, it can be removed from Standard variants. Check all shader sources to confirm before removing.

### 5. Draw Call Sorting by Program

For optimal performance, sort entity draw calls by active light count to minimize `glUseProgram` switches:

```cpp
// In the entity rendering pass
struct EntityBucket {
    std::vector<Entity*> entities;
};
EntityBucket buckets[MAX_VERTEX_LIGHTS + 1];

// Bucket entities by vertex light count
for (Entity* entity : visibleEntities) {
    int n = entity->activeVertexLightCount; // lights 1-N, excluding player light 0
    n = clamp(n, 0, MAX_VERTEX_LIGHTS);
    buckets[n].entities.push_back(entity);
}

// Draw bucketed — one program switch per light count tier
for (int n = 0; n <= MAX_VERTEX_LIGHTS; n++) {
    if (buckets[n].entities.empty()) continue;
    // Material selection happens inside Irrlicht's draw pipeline via getActiveSolid(n) etc.
    for (Entity* entity : buckets[n].entities) {
        entity->setMaterialType(shaderManager->getActiveSolid(n)); // or appropriate material
        entity->render();
    }
}
```

**Interaction with front-to-back sorting:** Sort primarily by program (light count bucket), secondarily by depth within each bucket. Entities at similar distances tend to share similar light counts (same nearby torches), so the two sorting criteria rarely conflict.

In practice, most frames will have 3-4 active buckets (most entities have 1-3 lights affecting them), resulting in only 3-4 program switches per frame for entities.

### 6. Binary Cache Compatibility

The existing `GL_OES_get_program_binary` cache uses FNV-1a hash of `gpuId + vsSource + fsSource` as the cache key. Since each light count variant produces a different VS source string, each variant gets a unique cache key automatically. No changes needed to the caching system.

The first run after this change will recompile all 52 programs (the old 17 cached binaries won't match the new source hashes). Subsequent runs will load from cache as before.

### 7. Desktop GL Path

The desktop GL path (GLSL 1.20, `!EQT_HAS_GLES2`) does not have the 512-instruction limit and its GPU likely supports true dynamic branching. The permutation approach can still be applied for consistency, or the desktop path can keep its current single-variant approach with the runtime `uNumPointLights` guard. This is a low priority — the Mali-400 GLES2 path is the target.

If applying permutations to desktop GL, the generator needs a separate code path for GLSL 1.20 conventions (`gl_Vertex`, `gl_Normal`, `gl_Color`, `gl_MultiTexCoord0/1`, `gl_TexCoord[]` varyings).

## Testing Plan

1. **Compile all 52 programs at startup.** Verify no compilation errors on Mali-400 via Lima. Check shader info logs for warnings.

2. **Instruction count verification.** If Lima exposes `GL_PROGRAM_BINARY_LENGTH` or similar, compare binary sizes across variants to confirm the light count correlates with program size. Alternatively, check for the specific 512-instruction-exceeded error that was occurring before.

3. **Visual correctness.** Compare rendering output with the current LW (all per-vertex) path. With `/plight` enabled, the Standard permutation path should produce identical results to what the current Standard shaders would produce if they didn't exceed the instruction limit.

4. **Performance profiling.** Measure frame times with the permutation path vs the current LW path. The permutation path adds per-pixel player light cost in the FS but should otherwise be similar. The key metric is whether the 10fps penalty from the old branching approach is eliminated.

5. **Light count distribution.** Log the distribution of vertex light counts across entities per frame in a few representative zones (outdoor, dungeon, city). This validates that the common case is 1-3 lights and the 6-7 light variants are rare.

6. **Edge case: 0 vertex lights.** Test the variant where only the per-pixel player light is active (no vertex point lights). This occurs outdoors at night or in zones with no nearby point light sources. The VS should produce only sun + ambient lighting, and the FS handles the player light.

7. **Edge case: `/plight` toggle.** Verify that toggling per-pixel player light off still falls back to the LW variants correctly. The `getActive*()` methods should return LW material IDs when `perPixelPlayerLight_` is false, regardless of light count parameter.

## Files to Modify

| File | Changes |
|------|---------|
| `include/client/graphics/zone_shader.h` | Replace single material ID members with arrays. Add light count parameter to `getActive*()` methods. Add `buildStandardVS()` declaration. Add `MAX_VERTEX_LIGHTS` constant. |
| `src/client/graphics/zone_shader.cpp` | Add `buildStandardVS()` generator function. Replace single `addHighLevelShaderMaterial` calls with registration loops. Update `getActive*()` implementations. Remove or keep `uNumPointLights` uniform based on analysis. |
| Entity rendering code (location TBD) | Pass active vertex light count to `getActive*()` when selecting materials. Optionally add draw call bucketing by light count. |
| `ShaderCallback` (in zone_shader.cpp) | Option A: no changes. Option B: adjust light array uniform upload size. |

## Summary of What Does NOT Change

- All 6 built-in GLES2 programs (Color2D, UI2D, Solid3D, AlphaTest3D, AtlasSolid3D, AtlasAlpha3D)
- All 5 Lightweight zone shader variants
- All fragment shaders (Standard solid, alpha, atlas solid, atlas alpha)
- The particle renderer
- The shader callback `frameId_` per-frame skip optimization
- The binary cache system
- The `clearPointLights()` inactive light handling (attenuation set to (1,0,0), color zeroed)
- The light sorting system (light 0 = player, sorted by distance/influence)
- The wind displacement algorithm (just embedded in the generated VS)
- The atlas UV handling (`aTexCoord1`)
- The dual-texture ETC1 alpha approach
- The `OES_standard_derivatives` adaptive alpha threshold
- The per-pixel player light fragment shader logic
- The Desktop GL path (unless explicitly ported)
