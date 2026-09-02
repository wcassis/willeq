#pragma once
// Desktop GL (GLSL 1.20) zone shader sources, split out of zone_shader.cpp by
// Batch J unit J09. PRIVATE internal header: these are file-local `static`
// definitions and this file must be included by zone_shader.cpp and NOTHING
// ELSE. See refactor/J09_zone_shader_split.md.

namespace EQT {
namespace Graphics {

#ifndef EQT_HAS_GLES2   // was the `#else` arm of zone_shader.cpp:1018

// ============================================================================
// Existing per-vertex lighting shaders (for non-atlas rendering)
// ============================================================================

// GLSL 1.20 vertex shader — fog, directional + point lighting, day/night tint
static const char* VERTEX_SHADER_SRC = R"(
#version 120

uniform mat4 mWorldViewProj;
uniform mat4 mWorld;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uTintColor;
uniform float uFogStart;
uniform float uFogEnd;

uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform vec3 uLightAtten[8];
uniform int uNumPointLights;

uniform vec3 uFireGlowLightPos[8];
uniform vec3 uFireGlowLightColor[8];
uniform float uFireGlowLightRadius[8];
uniform float uFireGlowLightIntensity[8];
uniform int uNumFireGlowLights;
uniform int uUnifiedLighting;

varying vec4 vColor;
varying float vFogFactor;

void main() {
    gl_Position = mWorldViewProj * gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;

    vec3 worldPos = (mWorld * gl_Vertex).xyz;
    vec3 worldN = normalize((mWorld * vec4(gl_Normal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    // Point lights (world space, guarded by light count)
    if (uNumPointLights > 0) {
        for (int i = 0; i < 8; i++) {
            vec3 lVec = uLightPos[i] - worldPos;
            float d = length(lVec) + 0.001;
            float atten = 1.0 / (uLightAtten[i].x
                                + uLightAtten[i].y * d
                                + uLightAtten[i].z * d * d + 0.0001);
            float nl = max(dot(worldN, normalize(lVec)), 0.0);
            lighting += uLightColor[i] * nl * atten;
        }
    }

    // Fire glow lighting (omnidirectional warm glow, quartic falloff)
    vec3 fogGlow = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        if (i >= uNumFireGlowLights) break;
        vec3 toEmitter = uFireGlowLightPos[i] - worldPos;
        float d2 = dot(toEmitter, toEmitter);
        float r2 = uFireGlowLightRadius[i] * uFireGlowLightRadius[i];
        if (d2 < r2) {
            float t = 1.0 - d2 / r2;
            fogGlow += uFireGlowLightColor[i] * (t * t) * uFireGlowLightIntensity[i];
        }
    }

    vColor = vec4(lighting * uTintColor + fogGlow, 1.0) * gl_Color;

    // Linear fog factor (1.0 = no fog, 0.0 = full fog)
    float fogDist = length((mWorldViewProj * gl_Vertex).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Fragment shader — solid (opaque) variant
static const char* FRAGMENT_SHADER_SOLID_SRC = R"(
#version 120

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, gl_TexCoord[0].xy);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Fragment shader — alpha-test variant (for vegetation/transparency)
static const char* FRAGMENT_SHADER_ALPHA_SRC = R"(
#version 120

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, gl_TexCoord[0].xy);
    if (texColor.a < 0.5) discard;
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// ============================================================================
// Atlas shaders — per-vertex lighting, CPU-precomputed atlas UV
// ============================================================================
// Uses the same per-vertex lighting approach as the non-atlas shaders.
// Key differences from the non-atlas path:
//   - Lighting is per-vertex (not per-pixel) to keep the fragment shader
//     trivially simple. The 8-point-light loop with sqrt/normalize/division
//     was taking 200+ms per frame on Mali 400's FP16 fragment cores.
//   - Atlas UVs are fully precomputed on the CPU during mesh building
//     (in buildAtlasedMesh) at full float precision, then passed through
//     gl_TexCoord[0] (built-in, high-precision interpolation on Mali 400).
//     The fragment shader just samples — no fract(), no multiply, no UV math.
//   - Per-triangle UV cell basing in the mesh builder ensures continuity
//     within each triangle, avoiding the fract() discontinuity that causes
//     warping at polygon edges where UVs cross integer boundaries.
//
// Vertex format: S3DVertex2TCoords
//   gl_MultiTexCoord0 = original UVs (kept for reference, not used by shader)
//   gl_MultiTexCoord1 = precomputed atlas UV (tileOffset + relUV * tileScale)

static const char* ATLAS_VERTEX_SHADER_SRC = R"(
#version 120

uniform mat4 mWorldViewProj;
uniform mat4 mWorld;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uTintColor;
uniform float uFogStart;
uniform float uFogEnd;

uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform vec3 uLightAtten[8];
uniform int uNumPointLights;

uniform vec3 uFireGlowLightPos[8];
uniform vec3 uFireGlowLightColor[8];
uniform float uFireGlowLightRadius[8];
uniform float uFireGlowLightIntensity[8];
uniform int uNumFireGlowLights;
uniform int uUnifiedLighting;

varying vec4 vColor;
varying float vFogFactor;

void main() {
    gl_Position = mWorldViewProj * gl_Vertex;

    gl_TexCoord[0] = gl_MultiTexCoord1;

    vec3 worldPos = (mWorld * gl_Vertex).xyz;
    vec3 worldN = normalize((mWorld * vec4(gl_Normal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    // Point lights (world space, guarded by light count)
    if (uNumPointLights > 0) {
        for (int i = 0; i < 8; i++) {
            vec3 lVec = uLightPos[i] - worldPos;
            float d = length(lVec) + 0.001;
            float atten = 1.0 / (uLightAtten[i].x
                                + uLightAtten[i].y * d
                                + uLightAtten[i].z * d * d + 0.0001);
            float nl = max(dot(worldN, normalize(lVec)), 0.0);
            lighting += uLightColor[i] * nl * atten;
        }
    }

    // Fire glow lighting (omnidirectional warm glow, quartic falloff)
    vec3 fogGlow = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        if (i >= uNumFireGlowLights) break;
        vec3 toEmitter = uFireGlowLightPos[i] - worldPos;
        float d2 = dot(toEmitter, toEmitter);
        float r2 = uFireGlowLightRadius[i] * uFireGlowLightRadius[i];
        if (d2 < r2) {
            float t = 1.0 - d2 / r2;
            fogGlow += uFireGlowLightColor[i] * (t * t) * uFireGlowLightIntensity[i];
        }
    }

    vColor = vec4(lighting * uTintColor + fogGlow, 1.0);

    // Linear fog factor (1.0 = no fog, 0.0 = full fog)
    float fogDist = length((mWorldViewProj * gl_Vertex).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Atlas fragment shader — solid (opaque)
static const char* ATLAS_FRAGMENT_SHADER_SOLID_SRC = R"(
#version 120

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, gl_TexCoord[0].xy);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Atlas fragment shader — alpha (dual ETC1)
static const char* ATLAS_FRAGMENT_SHADER_ALPHA_SRC = R"(
#version 120

uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying float vFogFactor;

void main() {
    vec2 atlasUV = gl_TexCoord[0].xy;
    float alpha = texture2D(uAlphaTexture, atlasUV).r;
    if (alpha < 0.5) discard;

    vec4 texColor = texture2D(uTexture, atlasUV);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// ============================================================================
// Wind vertex shader (GLSL 1.20) — extends standard VS with vertex displacement
// ============================================================================
static const char* WIND_VERTEX_SHADER_SRC = R"(
#version 120

uniform mat4 mWorldViewProj;
uniform mat4 mWorld;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uTintColor;
uniform float uFogStart;
uniform float uFogEnd;

uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform vec3 uLightAtten[8];
uniform int uNumPointLights;

uniform vec3 uFireGlowLightPos[8];
uniform vec3 uFireGlowLightColor[8];
uniform float uFireGlowLightRadius[8];
uniform float uFireGlowLightIntensity[8];
uniform int uNumFireGlowLights;
uniform int uUnifiedLighting;

uniform float uWindTime;
uniform vec4 uWindParams;   // baseStrength, baseFrequency, gustStrength, gustFrequency
uniform vec2 uMeshYBounds;  // minY, maxY in local mesh space (Irrlicht Y-up)

varying vec4 vColor;
varying float vFogFactor;

void main() {
    vec4 pos = gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;

    // Wind displacement based on vertex height within mesh
    float range = uMeshYBounds.y - uMeshYBounds.x;
    float normalizedH = 0.0;
    if (range > 0.001) {
        normalizedH = clamp((pos.y - uMeshYBounds.x) / range, 0.0, 1.0);
    }
    float influence = max(normalizedH - 0.3, 0.0) / 0.7;
    influence = influence * influence;

    vec3 meshCenter = (mWorld * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float seed = fract(sin(meshCenter.x * 12.9898 + meshCenter.z * 78.233) * 43758.5453) * 6.283;

    float baseStr = uWindParams.x;
    float baseFreq = uWindParams.y;
    float gustStr = uWindParams.z;
    float gustFreq = uWindParams.w;

    float sway = sin(uWindTime * baseFreq * 6.283 + seed);
    float gust = sin(uWindTime * gustFreq * 6.283 + seed * 0.7) * 0.5 + 0.5;
    float strength = baseStr + gustStr * gust;

    pos.x += influence * strength * sway;
    pos.z += influence * strength * sin(uWindTime * baseFreq * 6.283 + seed + 1.57);

    gl_Position = mWorldViewProj * pos;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(gl_Normal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    // Point lights (world space, guarded by light count)
    if (uNumPointLights > 0) {
        for (int i = 0; i < 8; i++) {
            vec3 lVec = uLightPos[i] - worldPos;
            float d = length(lVec) + 0.001;
            float atten = 1.0 / (uLightAtten[i].x
                                + uLightAtten[i].y * d
                                + uLightAtten[i].z * d * d + 0.0001);
            float nl = max(dot(worldN, normalize(lVec)), 0.0);
            lighting += uLightColor[i] * nl * atten;
        }
    }

    // Fire glow lighting (omnidirectional warm glow, quartic falloff)
    vec3 fogGlow = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        if (i >= uNumFireGlowLights) break;
        vec3 toEmitter = uFireGlowLightPos[i] - worldPos;
        float d2 = dot(toEmitter, toEmitter);
        float r2 = uFireGlowLightRadius[i] * uFireGlowLightRadius[i];
        if (d2 < r2) {
            float t = 1.0 - d2 / r2;
            fogGlow += uFireGlowLightColor[i] * (t * t) * uFireGlowLightIntensity[i];
        }
    }

    vColor = vec4(lighting * uTintColor + fogGlow, 1.0) * gl_Color;

    // Linear fog factor
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

#endif // !EQT_HAS_GLES2

} // namespace Graphics
} // namespace EQT
