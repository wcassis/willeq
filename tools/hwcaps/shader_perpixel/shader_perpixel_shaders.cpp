// shader_perpixel_shaders.cpp — the thirteen GLSL ES sources
// gles2_shader_perpixel_benchmark compares (T447; sources moved verbatim from
// tools/hwcaps/gles2_shader_perpixel_benchmark.cpp).
//
// They are copies of zone_shader.cpp's programs plus the per-pixel/LUT
// variants under test; keeping them in their own TU is what took the tool
// under the file-size ceiling.

#include "hwcaps/shader_perpixel/shader_perpixel.h"


// ============================================================================
// Shader sources — copied from zone_shader.cpp
// ============================================================================

// Shared vertex shader (per-pixel variant): lights 1-7 per-vertex, passes worldPos/worldNormal
const char* VS_PERPIXEL = R"(
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

    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor, vec3(1.0));

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

    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting, 0.0);

    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Per-pixel fragment shader — BRANCHED (player light computed per-fragment, with if check)
const char* FS_PERPIXEL_BRANCHED = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLight = vec3(0.0);
    if (uPlayerLightColor.x + uPlayerLightColor.y + uPlayerLightColor.z > 0.0) {
        vec3 pLv = uPlayerLightPos - vWorldPos;
        float pLd = length(pLv) + 0.001;
        float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * pLd
                            + uPlayerLightAtten.z * pLd * pLd + 0.0001);
        float pLn = max(dot(normalize(vWorldNormal), pLv / pLd), 0.0);
        pLight = uPlayerLightColor * pLn * pLa;
    }

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Per-pixel fragment shader — BRANCHLESS (always computes player light)
// Current code: length() + normalize() + division + full attenuation
// Expensive ops: length (sqrt), normalize (inversesqrt+mul), pLv/pLd (div), 1.0/atten (div)
const char* FS_PERPIXEL_BRANCHLESS = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float pLd = length(pLv) + 0.001;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * pLd
                        + uPlayerLightAtten.z * pLd * pLd + 0.0001);
    float pLn = max(dot(normalize(vWorldNormal), pLv / pLd), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT B: Single inversesqrt — one inversesqrt gives both d and normalized lightDir.
// Skip FS normalize of vWorldNormal (VS already normalized, interpolation keeps ~unit length).
// Saves: 1 sqrt (from length), 1 inversesqrt (from normalize), 1 division (pLv/pLd).
const char* FS_OPT_INVERSESQRT = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv) + 0.0001;
    float invD = inversesqrt(d2);
    float d = d2 * invD;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * d
                        + uPlayerLightAtten.z * d2 + 0.0001);
    float pLn = max(dot(vWorldNormal, pLv * invD), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT C: Quadratic-only attenuation — drop linear term, so no need for d (only d²).
// Still uses inversesqrt for NdotL light direction.
// Saves vs B: d = d2*invD multiply, one less multiply in attenuation.
const char* FS_OPT_QUADRATIC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv) + 0.0001;
    float invD = inversesqrt(d2);
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.z * d2 + 0.0001);
    float pLn = max(dot(vWorldNormal, pLv * invD), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT D: Omnidirectional — distance-only falloff, no NdotL.
// Still uses full attenuation (linear + quadratic) requiring inversesqrt for d.
// Looks like a point glow rather than directional lighting.
const char* FS_OPT_OMNI = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv) + 0.0001;
    float invD = inversesqrt(d2);
    float d = d2 * invD;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * d
                        + uPlayerLightAtten.z * d2 + 0.0001);
    vec3 pLight = uPlayerLightColor * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT E: Omni + quadratic-only — minimal math, no sqrt/inversesqrt at all.
// Just dot product for d², reciprocal for attenuation. Cheapest possible per-pixel.
const char* FS_OPT_OMNI_QUAD = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv) + 0.0001;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.z * d2 + 0.0001);
    vec3 pLight = uPlayerLightColor * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT F: Per-vertex player light — compute player light in VS, pass contribution as varying.
// 4 varyings (vColor, vTexCoord, vFogFactor, vPlayerLight) instead of 5.
// FS is trivial: just add vPlayerLight * texColor. Tests varying interpolation vs FS compute.
const char* VS_PERVERTEX_PLIGHT = R"(
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

uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vPlayerLight;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;
    vTexCoord = aTexCoord0;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor, vec3(1.0));

    // VS point lights 1-7
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

    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting, 0.0);

    // Player light computed per-vertex, passed as varying
    vec3 pLv = uPlayerLightPos - worldPos;
    float pLd = length(pLv) + 0.001;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * pLd
                        + uPlayerLightAtten.z * pLd * pLd + 0.0001);
    float pLn = max(dot(worldN, pLv / pLd), 0.0);
    vPlayerLight = uPlayerLightColor * pLn * pLa;

    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// OPT F fragment shader — trivial FS, just adds interpolated player light
const char* FS_PERVERTEX_PLIGHT = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vPlayerLight;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = vec4(texColor.rgb * vColor.rgb + vPlayerLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Lightweight vertex shader: all 8 lights per-vertex, no worldPos/worldNormal output
const char* VS_LIGHTWEIGHT = R"(
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

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;
    vTexCoord = aTexCoord0;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor, vec3(1.0));

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

    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting, 0.0);

    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Lightweight fragment shader: trivial tex * vColor + fog
const char* FS_LIGHTWEIGHT = R"(
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

// ============================================================================
// Texture LUT variants — replace ALU (inversesqrt, reciprocal) with FP16
// texture lookups. LUT is a 256x1 GL_LUMINANCE_ALPHA + GL_HALF_FLOAT_OES
// texture where L = inversesqrt(d²), A = 1/(c + q*d²).
// ============================================================================

// LUT G: OPT C equivalent — directional NdotL + quadratic attenuation from LUT.
// Replaces inversesqrt(d²) and 1/(c+q*d²) with a single texture2D lookup.
// Same visual result as OPT C, but ALU → texture fetch.
const char* FS_LUT_QUADRATIC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAttenLUT;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
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
    float invD = lut.r;
    float pLa = lut.a;
    float pLn = max(dot(vWorldNormal, pLv * invD), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// LUT H: OPT E equivalent — omni + quadratic attenuation from LUT.
// Replaces 1/(c+q*d²) with a single texture2D lookup. No inversesqrt needed.
const char* FS_LUT_OMNI_QUAD = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAttenLUT;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
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
    float pLa = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5)).a;
    vec3 pLight = uPlayerLightColor * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

