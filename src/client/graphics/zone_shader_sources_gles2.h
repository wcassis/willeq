#pragma once
// GLES2 (GLSL ES 1.00) zone shader sources, split out of zone_shader.cpp by
// Batch J unit J09. PRIVATE internal header: these are file-local `static`
// definitions and this file must be included by zone_shader.cpp and NOTHING
// ELSE. See refactor/J09_zone_shader_split.md.

namespace EQT {
namespace Graphics {

// ============================================================================
// GLES2 shader variants (GLSL ES 1.00)
// ============================================================================
// When targeting native GLES2 (COpenGLES2Driver), we use attribute/varying
// syntax instead of gl_Vertex/gl_TexCoord built-ins, and explicit precision
// qualifiers. The built-in GLES2 driver programs (COGLES2Shaders) handle the
// basic rendering; these custom shaders add per-vertex lighting and fog.

#ifdef EQT_HAS_GLES2

// GLSL ES 1.00 vertex shader — per-vertex lighting + fog
// Light[0] (player light) is computed per-pixel in FS for smooth illumination
// on large triangles. Lights[1..7] (zone torches etc.) remain per-vertex.
static const char* VERTEX_SHADER_SRC = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;

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
// T408 (B110): the zone's baked per-vertex lighting.
//   uBakedLight = (gain, skyMaskWeight, mode); mode 0 = off (pre-T408),
//   1 = additive, 2 = multiplicative, 3 = max.
//   uBakedFlag  = 1.0 only on materials whose vertices actually carry the
//   WLD's baked colours (set from SMaterial::MaterialTypeParam). Entities and
//   every other mesh share these programs with aColor = opaque white, so the
//   equation below is EXACTLY the pre-T408 one whenever uBakedFlag is 0.
uniform vec3 uBakedLight;
uniform float uBakedFlag;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;
    vTexCoord = aTexCoord0;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vWorldPos = worldPos;
    vWorldNormal = worldN;

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    float bakedOn = uBakedFlag * step(0.5, uBakedLight.z);
    vec3 bakedRGB = aColor.rgb;
    // On a mesh whose aColor IS the baked light, the old "aColor is a tint"
    // reading has to be replaced by white, not merely left alone -- otherwise
    // turning the baked path OFF silently becomes the multiplicative
    // composition instead of the pre-T408 equation, and mode 0 stops being a
    // baseline. Same for the alpha byte, which on those meshes is the sky
    // exposure channel and not an opacity.
    vec3 tintSrc = mix(aColor.rgb, vec3(1.0), uBakedFlag);
    float alphaSrc = mix(aColor.a, 1.0, uBakedFlag);
    float skyMask = mix(1.0, aColor.a, uBakedLight.y * bakedOn);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor * skyMask, vec3(1.0));

    // Point lights 1-7 per-vertex (light[0] = player light, computed per-pixel in FS)
    vec3 pointLighting = vec3(0.0);
    if (uNumPointLights > 1) {
        for (int i = 1; i < 8; i++) {
            vec3 lVec = uLightPos[i] - worldPos;
            float d = length(lVec) + 0.001;
            float atten = 1.0 / (uLightAtten[i].x
                                + uLightAtten[i].y * d
                                + uLightAtten[i].z * d * d + 0.0001);
            float nl = max(dot(worldN, normalize(lVec)), 0.0);
            pointLighting += uLightColor[i] * nl * atten;
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

    vec3 tintOnly = baseLighting * uTintColor;
    vec3 litAdd = tintOnly + uBakedLight.x * bakedRGB;
    vec3 litMul = tintOnly * bakedRGB;
    vec3 litMax = max(tintOnly, uBakedLight.x * bakedRGB);
    vec3 lit = litAdd;
    if (uBakedLight.z > 1.5 && uBakedLight.z < 2.5) lit = litMul;
    else if (uBakedLight.z > 2.5) lit = litMax;

    if (uUnifiedLighting > 0) {
        vColor = vec4((tintOnly + pointLighting + fogGlow) * tintSrc, alphaSrc);
    } else {
        vec3 litRGB = mix(tintOnly * tintSrc, lit, bakedOn) + pointLighting + fogGlow;
        vColor = vec4(litRGB, alphaSrc);
    }

    // Linear fog factor (1.0 = no fog, 0.0 = full fog)
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// GLSL ES 1.00 fragment shader — solid (opaque)
// Per-pixel player light (light[0]) for smooth illumination on large triangles.
static const char* FRAGMENT_SHADER_SOLID_SRC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform sampler2D uAttenLUT;
uniform float uLUTScale;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));
    float pLn = max(dot(vWorldNormal, pLv * lut.r), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * lut.a;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// GLSL ES 1.00 fragment shader — alpha-test (base)
static const char* FRAGMENT_SHADER_ALPHA_SRC_BASE = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform sampler2D uAttenLUT;
uniform float uLUTScale;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
    if (alpha < 0.5) discard;

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));
    float pLn = max(dot(vWorldNormal, pLv * lut.r), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * lut.a;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, alpha * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// GLSL ES 1.00 fragment shader — alpha-test, RGBA main-texture-alpha variant
// (H02d, Option A). Reads alpha from uTexture's own alpha channel instead of
// a companion texture on unit 1 — used for masked textures on the direct
// RGBA/DDS decode path (ConstrainedTextureCache::uploadTexture()), which has
// real per-pixel alpha in the main texture and never builds a companion.
// Identical to FRAGMENT_SHADER_ALPHA_SRC_BASE except for the alpha source.
static const char* FRAGMENT_SHADER_ALPHA_RGBA_SRC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform sampler2D uAttenLUT;
uniform float uLUTScale;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    if (texColor.a < 0.5) discard;

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));
    float pLn = max(dot(vWorldNormal, pLv * lut.r), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * lut.a;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// B960 (GitLab #278): the fwidth() "derivatives" alpha-test variant that sat
// here had no caller (zone_shader.cpp compiles the constant-threshold sources
// on every GLES2 build) and is the shader shape Lima runs INVERTED (B958,
// refactor/B958/README.md) -- deleted rather than left as a trap.

// GLSL ES 1.00 atlas vertex shader — per-vertex lighting, precomputed atlas UV
// Light[0] (player light) computed per-pixel in FS; lights[1..7] per-vertex.
static const char* ATLAS_VERTEX_SHADER_SRC = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;
attribute vec2 aTexCoord1;

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
// T408 (B110): the zone's baked per-vertex lighting.
//   uBakedLight = (gain, skyMaskWeight, mode); mode 0 = off (pre-T408),
//   1 = additive, 2 = multiplicative, 3 = max.
//   uBakedFlag  = 1.0 only on materials whose vertices actually carry the
//   WLD's baked colours (set from SMaterial::MaterialTypeParam). Entities and
//   every other mesh share these programs with aColor = opaque white, so the
//   equation below is EXACTLY the pre-T408 one whenever uBakedFlag is 0.
uniform vec3 uBakedLight;
uniform float uBakedFlag;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;

    // Use precomputed atlas UV from texcoord1
    vTexCoord = aTexCoord1;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vWorldPos = worldPos;
    vWorldNormal = worldN;

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    float bakedOn = uBakedFlag * step(0.5, uBakedLight.z);
    vec3 bakedRGB = aColor.rgb;
    // On a mesh whose aColor IS the baked light, the old "aColor is a tint"
    // reading has to be replaced by white, not merely left alone -- otherwise
    // turning the baked path OFF silently becomes the multiplicative
    // composition instead of the pre-T408 equation, and mode 0 stops being a
    // baseline. Same for the alpha byte, which on those meshes is the sky
    // exposure channel and not an opacity.
    vec3 tintSrc = mix(aColor.rgb, vec3(1.0), uBakedFlag);
    float alphaSrc = mix(aColor.a, 1.0, uBakedFlag);
    float skyMask = mix(1.0, aColor.a, uBakedLight.y * bakedOn);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor * skyMask, vec3(1.0));

    // Point lights 1-7 per-vertex (light[0] = player light, computed per-pixel in FS)
    vec3 pointLighting = vec3(0.0);
    if (uNumPointLights > 1) {
        for (int i = 1; i < 8; i++) {
            vec3 lVec = uLightPos[i] - worldPos;
            float d = length(lVec) + 0.001;
            float atten = 1.0 / (uLightAtten[i].x
                                + uLightAtten[i].y * d
                                + uLightAtten[i].z * d * d + 0.0001);
            float nl = max(dot(worldN, normalize(lVec)), 0.0);
            pointLighting += uLightColor[i] * nl * atten;
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

    vec3 tintOnly = baseLighting * uTintColor;
    vec3 litAdd = tintOnly + uBakedLight.x * bakedRGB;
    vec3 litMul = tintOnly * bakedRGB;
    vec3 litMax = max(tintOnly, uBakedLight.x * bakedRGB);
    vec3 lit = litAdd;
    if (uBakedLight.z > 1.5 && uBakedLight.z < 2.5) lit = litMul;
    else if (uBakedLight.z > 2.5) lit = litMax;

    if (uUnifiedLighting > 0) {
        vColor = vec4((tintOnly + pointLighting + fogGlow) * tintSrc, alphaSrc);
    } else {
        vec3 litRGB = mix(tintOnly * tintSrc, lit, bakedOn) + pointLighting + fogGlow;
        vColor = vec4(litRGB, alphaSrc);
    }

    // Linear fog factor (1.0 = no fog, 0.0 = full fog)
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// GLSL ES 1.00 atlas fragment shader — solid (opaque)
static const char* ATLAS_FRAGMENT_SHADER_SOLID_SRC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform sampler2D uAttenLUT;
uniform float uLUTScale;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));
    float pLn = max(dot(vWorldNormal, pLv * lut.r), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * lut.a;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// GLSL ES 1.00 atlas fragment shader — alpha (dual ETC1, base)
static const char* ATLAS_FRAGMENT_SHADER_ALPHA_SRC_BASE = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform sampler2D uAttenLUT;
uniform float uLUTScale;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
    if (alpha < 0.5) discard;
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));
    float pLn = max(dot(vWorldNormal, pLv * lut.r), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * lut.a;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// B960: the atlas fwidth() alpha variant was deleted for the same reason.

// ============================================================================
// Debug fragment shader (GLSL ES 1.00) — visualize shader intermediates
// ============================================================================
// Compiled as a separate program. Only used when /shaderdebug N (N>0) is active.
// Cost: ~36ms on Mali 400 due to FS branches. Never active in production rendering.
// Modes: 1=texColor 2=vColor 3=LUT 4=pLight 5=worldNormal 6=lightDir
//        7=alpha 8=tex*vColor 9=UV 10=depth 11=rawTexAlpha
//        12=companionAlpha (H02d — uAlphaTexture.r, unit 1; cannot visualize
//        the main-texture-alpha RGBA variant since that reads uTexture.a,
//        already covered by modes 7/11)
static const char* FRAGMENT_SHADER_DEBUG_SRC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform sampler2D uAttenLUT;
uniform float uLUTScale;
uniform int uShaderDebug;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    if (texColor.a < 0.5) discard;

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));
    float pLn = max(dot(vWorldNormal, pLv * lut.r), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * lut.a;

    if (uShaderDebug == 1) { gl_FragColor = texColor; return; }
    if (uShaderDebug == 2) { gl_FragColor = vColor; return; }
    if (uShaderDebug == 3) { gl_FragColor = vec4(lut.r, lut.a, 0.0, 1.0); return; }
    if (uShaderDebug == 4) { gl_FragColor = vec4(pLight, 1.0); return; }
    if (uShaderDebug == 5) { gl_FragColor = vec4(vWorldNormal * 0.5 + 0.5, 1.0); return; }
    if (uShaderDebug == 6) { gl_FragColor = vec4(normalize(pLv) * 0.5 + 0.5, 1.0); return; }
    if (uShaderDebug == 7) { gl_FragColor = vec4(texColor.a, texColor.a, texColor.a, 1.0); return; }
    if (uShaderDebug == 8) { gl_FragColor = vec4(texColor.rgb * vColor.rgb, 1.0); return; }
    if (uShaderDebug == 9) { gl_FragColor = vec4(vTexCoord.x, vTexCoord.y, 0.0, 1.0); return; }
    if (uShaderDebug == 10) { gl_FragColor = vec4(vec3(gl_FragCoord.z), 1.0); return; }
    if (uShaderDebug == 11) { gl_FragColor = vec4(texColor.rgb, texColor.a); return; }
    if (uShaderDebug == 12) {
        float compAlpha = texture2D(uAlphaTexture, vTexCoord).r;
        gl_FragColor = vec4(compAlpha, compAlpha, compAlpha, 1.0);
        return;
    }

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// ============================================================================
// Wind vertex shader (GLSL ES 1.00) — extends standard VS with vertex displacement
// ============================================================================
// Used for tree objects rendered through the normal object pipeline on GPU path.
// Wind displacement applied per-vertex based on normalized height within mesh.
// uMeshYBounds (minY, maxY) packed via material.MaterialTypeParam/Param2.
static const char* WIND_VERTEX_SHADER_SRC = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;

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
// T408 (B110): the zone's baked per-vertex lighting.
//   uBakedLight = (gain, skyMaskWeight, mode); mode 0 = off (pre-T408),
//   1 = additive, 2 = multiplicative, 3 = max.
//   uBakedFlag  = 1.0 only on materials whose vertices actually carry the
//   WLD's baked colours (set from SMaterial::MaterialTypeParam). Entities and
//   every other mesh share these programs with aColor = opaque white, so the
//   equation below is EXACTLY the pre-T408 one whenever uBakedFlag is 0.
uniform vec3 uBakedLight;
uniform float uBakedFlag;

uniform float uWindTime;
uniform vec4 uWindParams;   // baseStrength, baseFrequency, gustStrength, gustFrequency
uniform vec2 uMeshYBounds;  // minY, maxY in local mesh space (Irrlicht Y-up)

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    vTexCoord = aTexCoord0;

    // Wind displacement based on vertex height within mesh
    float range = uMeshYBounds.y - uMeshYBounds.x;
    float normalizedH = 0.0;
    if (range > 0.001) {
        normalizedH = clamp((aPosition.y - uMeshYBounds.x) / range, 0.0, 1.0);
    }
    // Influence curve: no sway at base, quadratic ramp above 30% height
    float influence = max(normalizedH - 0.3, 0.0) / 0.7;
    influence = influence * influence;

    // Per-tree seed from world position of mesh center
    vec3 meshCenter = (mWorld * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float seed = fract(sin(meshCenter.x * 12.9898 + meshCenter.z * 78.233) * 43758.5453) * 6.283;

    // Wind displacement
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
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vWorldPos = worldPos;
    vWorldNormal = worldN;

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    float bakedOn = uBakedFlag * step(0.5, uBakedLight.z);
    vec3 bakedRGB = aColor.rgb;
    // On a mesh whose aColor IS the baked light, the old "aColor is a tint"
    // reading has to be replaced by white, not merely left alone -- otherwise
    // turning the baked path OFF silently becomes the multiplicative
    // composition instead of the pre-T408 equation, and mode 0 stops being a
    // baseline. Same for the alpha byte, which on those meshes is the sky
    // exposure channel and not an opacity.
    vec3 tintSrc = mix(aColor.rgb, vec3(1.0), uBakedFlag);
    float alphaSrc = mix(aColor.a, 1.0, uBakedFlag);
    float skyMask = mix(1.0, aColor.a, uBakedLight.y * bakedOn);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor * skyMask, vec3(1.0));

    // Point lights 1-7 per-vertex (light[0] = player light, computed per-pixel in FS)
    vec3 pointLighting = vec3(0.0);
    if (uNumPointLights > 1) {
        for (int i = 1; i < 8; i++) {
            vec3 lVec = uLightPos[i] - worldPos;
            float d = length(lVec) + 0.001;
            float atten = 1.0 / (uLightAtten[i].x
                                + uLightAtten[i].y * d
                                + uLightAtten[i].z * d * d + 0.0001);
            float nl = max(dot(worldN, normalize(lVec)), 0.0);
            pointLighting += uLightColor[i] * nl * atten;
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

    vec3 tintOnly = baseLighting * uTintColor;
    vec3 litAdd = tintOnly + uBakedLight.x * bakedRGB;
    vec3 litMul = tintOnly * bakedRGB;
    vec3 litMax = max(tintOnly, uBakedLight.x * bakedRGB);
    vec3 lit = litAdd;
    if (uBakedLight.z > 1.5 && uBakedLight.z < 2.5) lit = litMul;
    else if (uBakedLight.z > 2.5) lit = litMax;

    if (uUnifiedLighting > 0) {
        vColor = vec4((tintOnly + pointLighting + fogGlow) * tintSrc, alphaSrc);
    } else {
        vec3 litRGB = mix(tintOnly * tintSrc, lit, bakedOn) + pointLighting + fogGlow;
        vColor = vec4(litRGB, alphaSrc);
    }

    // Linear fog factor
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// ============================================================================
// Lightweight shader variants (GLSL ES 1.00) — no per-pixel player light
// ============================================================================
// These variants eliminate vWorldPos and vWorldNormal varyings (6 fewer floats
// interpolated per fragment), yielding a trivial FS identical to the built-in
// GLES2 programs. All 8 point lights are computed per-vertex with standard
// Lambertian (same loop structure as the desktop GL shaders that fit within
// Mali 400's 512-instruction VS limit).

// Lightweight VS (standard) — all 8 lights per-vertex, 3 varyings output
static const char* VERTEX_SHADER_LIGHTWEIGHT_SRC = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;

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

uniform vec3 uFireGlowLightPos[8];
uniform vec3 uFireGlowLightColor[8];
uniform float uFireGlowLightRadius[8];
uniform float uFireGlowLightIntensity[8];
uniform int uNumFireGlowLights;
uniform int uUnifiedLighting;
// T408 (B110): the zone's baked per-vertex lighting.
//   uBakedLight = (gain, skyMaskWeight, mode); mode 0 = off (pre-T408),
//   1 = additive, 2 = multiplicative, 3 = max.
//   uBakedFlag  = 1.0 only on materials whose vertices actually carry the
//   WLD's baked colours (set from SMaterial::MaterialTypeParam). Entities and
//   every other mesh share these programs with aColor = opaque white, so the
//   equation below is EXACTLY the pre-T408 one whenever uBakedFlag is 0.
uniform vec3 uBakedLight;
uniform float uBakedFlag;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;
    vTexCoord = aTexCoord0;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    float bakedOn = uBakedFlag * step(0.5, uBakedLight.z);
    vec3 bakedRGB = aColor.rgb;
    // On a mesh whose aColor IS the baked light, the old "aColor is a tint"
    // reading has to be replaced by white, not merely left alone -- otherwise
    // turning the baked path OFF silently becomes the multiplicative
    // composition instead of the pre-T408 equation, and mode 0 stops being a
    // baseline. Same for the alpha byte, which on those meshes is the sky
    // exposure channel and not an opacity.
    vec3 tintSrc = mix(aColor.rgb, vec3(1.0), uBakedFlag);
    float alphaSrc = mix(aColor.a, 1.0, uBakedFlag);
    float skyMask = mix(1.0, aColor.a, uBakedLight.y * bakedOn);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor * skyMask, vec3(1.0));

    // All 8 point lights per-vertex (standard Lambertian, single loop)
    vec3 pointLighting = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        pointLighting += uLightColor[i] * nl * atten;
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

    vec3 tintOnly = baseLighting * uTintColor;
    vec3 litAdd = tintOnly + uBakedLight.x * bakedRGB;
    vec3 litMul = tintOnly * bakedRGB;
    vec3 litMax = max(tintOnly, uBakedLight.x * bakedRGB);
    vec3 lit = litAdd;
    if (uBakedLight.z > 1.5 && uBakedLight.z < 2.5) lit = litMul;
    else if (uBakedLight.z > 2.5) lit = litMax;

    if (uUnifiedLighting > 0) {
        vColor = vec4((tintOnly + pointLighting + fogGlow) * tintSrc, alphaSrc);
    } else {
        vec3 litRGB = mix(tintOnly * tintSrc, lit, bakedOn) + pointLighting + fogGlow;
        vColor = vec4(litRGB, alphaSrc);
    }

    // Linear fog factor (1.0 = no fog, 0.0 = full fog)
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Lightweight VS (atlas) — precomputed atlas UV from texcoord1
static const char* ATLAS_VERTEX_SHADER_LIGHTWEIGHT_SRC = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;
attribute vec2 aTexCoord1;

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

uniform vec3 uFireGlowLightPos[8];
uniform vec3 uFireGlowLightColor[8];
uniform float uFireGlowLightRadius[8];
uniform float uFireGlowLightIntensity[8];
uniform int uNumFireGlowLights;
uniform int uUnifiedLighting;
// T408 (B110): the zone's baked per-vertex lighting.
//   uBakedLight = (gain, skyMaskWeight, mode); mode 0 = off (pre-T408),
//   1 = additive, 2 = multiplicative, 3 = max.
//   uBakedFlag  = 1.0 only on materials whose vertices actually carry the
//   WLD's baked colours (set from SMaterial::MaterialTypeParam). Entities and
//   every other mesh share these programs with aColor = opaque white, so the
//   equation below is EXACTLY the pre-T408 one whenever uBakedFlag is 0.
uniform vec3 uBakedLight;
uniform float uBakedFlag;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;

    // Use precomputed atlas UV from texcoord1
    vTexCoord = aTexCoord1;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    float bakedOn = uBakedFlag * step(0.5, uBakedLight.z);
    vec3 bakedRGB = aColor.rgb;
    // On a mesh whose aColor IS the baked light, the old "aColor is a tint"
    // reading has to be replaced by white, not merely left alone -- otherwise
    // turning the baked path OFF silently becomes the multiplicative
    // composition instead of the pre-T408 equation, and mode 0 stops being a
    // baseline. Same for the alpha byte, which on those meshes is the sky
    // exposure channel and not an opacity.
    vec3 tintSrc = mix(aColor.rgb, vec3(1.0), uBakedFlag);
    float alphaSrc = mix(aColor.a, 1.0, uBakedFlag);
    float skyMask = mix(1.0, aColor.a, uBakedLight.y * bakedOn);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor * skyMask, vec3(1.0));

    // All 8 point lights per-vertex (standard Lambertian, single loop)
    vec3 pointLighting = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        pointLighting += uLightColor[i] * nl * atten;
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

    vec3 tintOnly = baseLighting * uTintColor;
    vec3 litAdd = tintOnly + uBakedLight.x * bakedRGB;
    vec3 litMul = tintOnly * bakedRGB;
    vec3 litMax = max(tintOnly, uBakedLight.x * bakedRGB);
    vec3 lit = litAdd;
    if (uBakedLight.z > 1.5 && uBakedLight.z < 2.5) lit = litMul;
    else if (uBakedLight.z > 2.5) lit = litMax;

    if (uUnifiedLighting > 0) {
        vColor = vec4((tintOnly + pointLighting + fogGlow) * tintSrc, alphaSrc);
    } else {
        vec3 litRGB = mix(tintOnly * tintSrc, lit, bakedOn) + pointLighting + fogGlow;
        vColor = vec4(litRGB, alphaSrc);
    }

    // Linear fog factor (1.0 = no fog, 0.0 = full fog)
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Lightweight FS (solid) — trivial: tex * vColor + fog
static const char* FRAGMENT_SHADER_LIGHTWEIGHT_SOLID_SRC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Lightweight FS (alpha-test) — discard + tex * vColor + fog
static const char* FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_SRC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
    if (alpha < 0.5) discard;
    vec4 lit = vec4(texColor.rgb * vColor.rgb, alpha * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Lightweight FS (RGBA main-texture-alpha variant, H02d Option A) — alpha
// from uTexture's own alpha channel, no companion texture. See
// FRAGMENT_SHADER_ALPHA_RGBA_SRC for the heavy/per-pixel-light equivalent.
static const char* FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_RGBA_SRC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    if (texColor.a < 0.5) discard;
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Lightweight FS (atlas alpha, dual ETC1) — alpha from unit 1
static const char* ATLAS_FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_SRC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
    if (alpha < 0.5) discard;
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Lightweight wind VS — vertex displacement + all 8 lights per-vertex
static const char* WIND_VERTEX_SHADER_LIGHTWEIGHT_SRC = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;

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

uniform vec3 uFireGlowLightPos[8];
uniform vec3 uFireGlowLightColor[8];
uniform float uFireGlowLightRadius[8];
uniform float uFireGlowLightIntensity[8];
uniform int uNumFireGlowLights;
uniform int uUnifiedLighting;
// T408 (B110): the zone's baked per-vertex lighting.
//   uBakedLight = (gain, skyMaskWeight, mode); mode 0 = off (pre-T408),
//   1 = additive, 2 = multiplicative, 3 = max.
//   uBakedFlag  = 1.0 only on materials whose vertices actually carry the
//   WLD's baked colours (set from SMaterial::MaterialTypeParam). Entities and
//   every other mesh share these programs with aColor = opaque white, so the
//   equation below is EXACTLY the pre-T408 one whenever uBakedFlag is 0.
uniform vec3 uBakedLight;
uniform float uBakedFlag;

uniform float uWindTime;
uniform vec4 uWindParams;   // baseStrength, baseFrequency, gustStrength, gustFrequency
uniform vec2 uMeshYBounds;  // minY, maxY in local mesh space (Irrlicht Y-up)

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    vTexCoord = aTexCoord0;

    // Wind displacement based on vertex height within mesh
    float range = uMeshYBounds.y - uMeshYBounds.x;
    float normalizedH = 0.0;
    if (range > 0.001) {
        normalizedH = clamp((aPosition.y - uMeshYBounds.x) / range, 0.0, 1.0);
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
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    float bakedOn = uBakedFlag * step(0.5, uBakedLight.z);
    vec3 bakedRGB = aColor.rgb;
    // On a mesh whose aColor IS the baked light, the old "aColor is a tint"
    // reading has to be replaced by white, not merely left alone -- otherwise
    // turning the baked path OFF silently becomes the multiplicative
    // composition instead of the pre-T408 equation, and mode 0 stops being a
    // baseline. Same for the alpha byte, which on those meshes is the sky
    // exposure channel and not an opacity.
    vec3 tintSrc = mix(aColor.rgb, vec3(1.0), uBakedFlag);
    float alphaSrc = mix(aColor.a, 1.0, uBakedFlag);
    float skyMask = mix(1.0, aColor.a, uBakedLight.y * bakedOn);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor * skyMask, vec3(1.0));

    // All 8 point lights per-vertex (standard Lambertian, single loop)
    vec3 pointLighting = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        pointLighting += uLightColor[i] * nl * atten;
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

    vec3 tintOnly = baseLighting * uTintColor;
    vec3 litAdd = tintOnly + uBakedLight.x * bakedRGB;
    vec3 litMul = tintOnly * bakedRGB;
    vec3 litMax = max(tintOnly, uBakedLight.x * bakedRGB);
    vec3 lit = litAdd;
    if (uBakedLight.z > 1.5 && uBakedLight.z < 2.5) lit = litMul;
    else if (uBakedLight.z > 2.5) lit = litMax;

    if (uUnifiedLighting > 0) {
        vColor = vec4((tintOnly + pointLighting + fogGlow) * tintSrc, alphaSrc);
    } else {
        vec3 litRGB = mix(tintOnly * tintSrc, lit, bakedOn) + pointLighting + fogGlow;
        vColor = vec4(litRGB, alphaSrc);
    }

    // Linear fog factor
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

#endif // EQT_HAS_GLES2

} // namespace Graphics
} // namespace EQT
