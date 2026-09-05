// tools/hwcaps/fog_volume/fog_volume_shaders.cpp -- the GLSL ES sources the
// analytic fog-volume benchmark compiles.
//
// T385 (2026-09-05, board B167): gles2_fog_volume_benchmark.cpp was 1,711
// lines, over the ceiling that check_file_sizes.sh did not apply to tools/
// until this unit, and roughly a third of it was shader text. The strings
// are moved verbatim -- only `static` is dropped, so they can be named from
// the benchmark TU through fog_volume_shaders.h.

#include "fog_volume/fog_volume_shaders.h"

// ============================================================================
// Zone lighting shaders (same as perpixel benchmark — lightweight baseline)
// ============================================================================

const char* VS_ZONE = R"(
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

const char* FS_ZONE = R"(
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
// Fog volume shaders
//
// Fog volumes are rendered as back-face billboards (camera-facing quads).
// The VS passes the ray origin and direction to the FS, which computes
// ray-sphere intersection and evaluates the analytic density integral.
//
// The fog volume VS is shared across all density variants. Each density
// function has its own FS to avoid branches (Mali 400 FS branches are
// extremely expensive).
// ============================================================================

// Fog volume vertex shader
// Renders a screen-aligned quad for each fog sphere. The quad is sized
// to cover the sphere's projected extent. The FS computes ray-sphere
// intersection per fragment.
const char* VS_FOG = R"(
precision highp float;

attribute vec3 aPosition;  // quad corners: (-1,-1), (1,-1), (-1,1), (1,1)

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform highp vec3 uSphereCenter;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;   // camera position in sphere-local space
varying highp vec3 vRayDir;      // ray direction (not normalized — FS normalizes)

void main() {
    // Billboard quad corners in world space, sized to sphere radius * sqrt(2)
    // to ensure full sphere coverage from any view angle
    float extent = uSphereRadius * 1.5;
    vec3 worldPos = uSphereCenter
                  + uCameraRight * aPosition.x * extent
                  + uCameraUp * aPosition.y * extent;

    gl_Position = uViewProj * vec4(worldPos, 1.0);

    // Transform to sphere-local space (centered at origin, unit radius)
    // so density functions work in normalized [0,1] radius
    vRayOrigin = (uCameraPos - uSphereCenter) / uSphereRadius;
    vRayDir = (worldPos - uCameraPos) / uSphereRadius;
}
)";

// Ray-sphere intersection helper (shared preamble for all fog FS variants).
// Returns false if the ray misses the unit sphere. Sets tNear/tFar to the
// entry/exit distances along the ray.
//
// Inlined as a string prefix for each FS variant to avoid GLSL function
// call overhead (Mali 400 may not inline functions efficiently).
const char* FS_FOG_PREAMBLE = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;    // overall density multiplier
uniform float uSphereRadius;  // for depth comparison

varying vec3 vRayOrigin;
varying vec3 vRayDir;
)";

// --- QUADRATIC density: f(r) = (1 - r^2), cheapest ---
// Cross-section: g(t) = 1 - (a*t^2 + b*t + c) where a=dot(d,d), b=2*dot(o,d), c=dot(o,o)
// Antiderivative: F(t) = t - (a*t^3/3 + b*t^2/2 + c*t) = t*(1 - c - b*t/2 - a*t^2/3)
// Wait — expanding: g(t) = 1 - a*t^2 - b*t - c
// F(t) = (1-c)*t - b*t^2/2 - a*t^3/3
const char* FS_FOG_QUADRATIC = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

void main() {
    vec3 rd = normalize(vRayDir);

    // Ray-sphere intersection (unit sphere)
    float a_i = dot(rd, rd);  // = 1.0 for normalized rd
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    // Miss — discard (no fog contribution)
    if (disc < 0.0) discard;

    float sqrtDisc = sqrt(disc);
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    // Shift ray origin to tNear for numerical precision
    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    // Analytic integral of (1 - r^2) along the ray segment [0, L]
    // where r^2 = dot(o + rd*t, o + rd*t) = a*t^2 + b*t + c
    float a = dot(rd, rd);     // 1.0
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    // Antiderivative F(t) = (1-c)*t - b*t^2/2 - a*t^3/3
    // Integral = F(L) - F(0) = F(L)
    float integral = L * ((1.0 - c) + L * (-b * 0.5 + L * (-a / 3.0)));

    // Beer-Lambert transmittance
    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = exp(-density);
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// --- QUARTIC density: f(r) = (1 - r^2)^2, smooth edges ---
// Cross-section: g(t) = (1 - (a*t^2 + b*t + c))^2
// Let h(t) = 1 - a*t^2 - b*t - c, then g(t) = h(t)^2
// h(t)^2 = (1-c)^2 - 2(1-c)*b*t + (b^2 - 2(1-c)*a)*t^2 + 2*a*b*t^3 + a^2*t^4
// Antiderivative is a degree-5 polynomial
const char* FS_FOG_QUARTIC = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

void main() {
    vec3 rd = normalize(vRayDir);

    float a_i = dot(rd, rd);
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    if (disc < 0.0) discard;

    float sqrtDisc = sqrt(disc);
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    float a = dot(rd, rd);
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    // h(t) = (1-c) - b*t - a*t^2
    // g(t) = h(t)^2 -- expand and integrate term by term
    float h0 = 1.0 - c;  // h(0)
    // Coefficients of h(t)^2 = h0^2 + (-2*h0*b)*t + (b^2 - 2*h0*a)*t^2 + (2*a*b)*t^3 + a^2*t^4
    float c0 = h0 * h0;
    float c1 = -2.0 * h0 * b;
    float c2 = b * b - 2.0 * h0 * a;
    float c3 = 2.0 * a * b;
    float c4 = a * a;

    // F(L) = c0*L + c1*L^2/2 + c2*L^3/3 + c3*L^4/4 + c4*L^5/5
    float integral = L * (c0 + L * (c1 * 0.5 + L * (c2 / 3.0 + L * (c3 * 0.25 + L * c4 * 0.2))));

    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = exp(-density);
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// --- LINEAR density: f(r) = 1 - r, requires sqrt ---
// r = sqrt(a*t^2 + b*t + c), so g(t) = 1 - sqrt(a*t^2 + b*t + c)
// Antiderivative of sqrt(quadratic) involves sqrt + log (see article).
// This is the expensive variant — tests whether sqrt+log cost is acceptable.
const char* FS_FOG_LINEAR = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

void main() {
    vec3 rd = normalize(vRayDir);

    float a_i = dot(rd, rd);
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    if (disc < 0.0) discard;

    float sqrtDisc = sqrt(disc);
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    float a = dot(rd, rd);
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    // Integral of (1 - sqrt(a*t^2 + b*t + c)) dt from 0 to L
    // = L - integral of sqrt(a*t^2 + b*t + c) dt from 0 to L
    //
    // Antiderivative of sqrt(a*t^2 + b*t + c):
    //   Let u = t + b/(2a), v = (b^2 - 4ac) / (4a^2)  (note: negative inside sphere)
    //   But we use v = (c - b^2/(4a)) / a = c/a - b^2/(4a^2) ... actually:
    //   sqrt(a*(t^2 + b/a*t + c/a)) = sqrt(a) * sqrt((t+b/2a)^2 + (c/a - b^2/4a^2))
    //
    // For numerical stability, evaluate at both endpoints:
    float sqrtA = sqrt(a);  // = 1.0 for normalized rd
    float bOverA = b / a;
    float v = c / a - bOverA * bOverA * 0.25;  // (4ac - b^2) / (4a^2), negative

    // F(t) = (u * sqrt(u^2 + v) + v * log(u + sqrt(u^2 + v))) / 2
    // where u = t + b/(2a)
    float u0 = bOverA * 0.5;
    float u1 = L + bOverA * 0.5;

    float r0sq = u0 * u0 + v;
    float r1sq = u1 * u1 + v;
    float r0 = sqrt(max(r0sq, 0.0));
    float r1 = sqrt(max(r1sq, 0.0));

    float F0 = u0 * r0 + v * log(max(u0 + r0, 0.001));
    float F1 = u1 * r1 + v * log(max(u1 + r1, 0.001));

    float sqrtIntegral = sqrtA * (F1 - F0) * 0.5;
    float integral = L - sqrtIntegral;

    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = exp(-density);
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// --- GAUSSIAN density: f(r) = exp(-r^2), requires exp + erf ---
// Cross-section: g(t) = exp(-(a*t^2 + b*t + c))
// Antiderivative involves the error function (erf).
const char* FS_FOG_GAUSSIAN = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

// Polynomial erf approximation (Abramowitz & Stegun, max error ~5e-4)
float erf_approx(float z) {
    float az = abs(z);
    float t = 1.0 / (1.0 + az * (0.278393 + az * (0.230389 + az * (0.000972 + az * 0.078108))));
    float r = 1.0 - t * t * t * t;  // 1 - (1/(1+p))^4
    return sign(z) * r;
}

void main() {
    vec3 rd = normalize(vRayDir);

    float a_i = dot(rd, rd);
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    // For Gaussian we still clip to the unit sphere to avoid computing
    // the infinite tail (which is negligible at r=1 where exp(-1)=0.37)
    if (disc < 0.0) discard;

    float sqrtDisc = sqrt(disc);
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    float a = dot(rd, rd);
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    // Integral of exp(-(a*t^2 + b*t + c)) from 0 to L
    // Complete the square: a*t^2 + b*t + c = a*(t + b/2a)^2 + (c - b^2/4a)
    // = a*(t + b/2a)^2 + k  where k = c - b^2/(4a)
    float sqrtA = sqrt(a);
    float k = c - b * b / (4.0 * a);

    // Integral = sqrt(pi) * exp(-k) / (2*sqrt(a)) * (erf(sqrt(a)*(L+b/2a)) - erf(sqrt(a)*b/2a))
    float bOver2a = b / (2.0 * a);
    float erf0 = erf_approx(sqrtA * bOver2a);
    float erf1 = erf_approx(sqrtA * (L + bOver2a));

    float integral = 1.7724539 * exp(-k) * (erf1 - erf0) / (2.0 * sqrtA);  // sqrt(pi) = 1.7724539

    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = exp(-density);
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// ============================================================================
// LUT fog shader variants — replace sqrt + exp ALU with texture lookups
// ============================================================================

// LUT QUADRATIC: sqrt(disc) and exp(-density) replaced by FP16 LUT reads
const char* FS_FOG_LUT_QUADRATIC = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;
uniform sampler2D uSqrtLUT;
uniform float uSqrtLUTScale;
uniform sampler2D uExpLUT;
uniform float uExpLUTScale;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

void main() {
    vec3 rd = normalize(vRayDir);

    float a_i = dot(rd, rd);
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    if (disc < 0.0) discard;

    float sqrtDisc = texture2D(uSqrtLUT, vec2(disc * uSqrtLUTScale, 0.5)).r;
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    float a = dot(rd, rd);
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    float integral = L * ((1.0 - c) + L * (-b * 0.5 + L * (-a / 3.0)));

    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = texture2D(uExpLUT, vec2(density * uExpLUTScale, 0.5)).r;
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// LUT QUARTIC: sqrt(disc) and exp(-density) replaced by FP16 LUT reads
const char* FS_FOG_LUT_QUARTIC = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;
uniform sampler2D uSqrtLUT;
uniform float uSqrtLUTScale;
uniform sampler2D uExpLUT;
uniform float uExpLUTScale;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

void main() {
    vec3 rd = normalize(vRayDir);

    float a_i = dot(rd, rd);
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    if (disc < 0.0) discard;

    float sqrtDisc = texture2D(uSqrtLUT, vec2(disc * uSqrtLUTScale, 0.5)).r;
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    float a = dot(rd, rd);
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    float h0 = 1.0 - c;
    float c0 = h0 * h0;
    float c1 = -2.0 * h0 * b;
    float c2 = b * b - 2.0 * h0 * a;
    float c3 = 2.0 * a * b;
    float c4 = a * a;

    float integral = L * (c0 + L * (c1 * 0.5 + L * (c2 / 3.0 + L * (c3 * 0.25 + L * c4 * 0.2))));

    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = texture2D(uExpLUT, vec2(density * uExpLUTScale, 0.5)).r;
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";
