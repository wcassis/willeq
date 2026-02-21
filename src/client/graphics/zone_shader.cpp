#include "client/graphics/zone_shader.h"
#include "common/logging.h"
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#elif defined(EQT_HAS_DRM)
#include <GL/gl.h>
#endif
#include <cstring>

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
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    // Point lights (world space, always iterate all 8)
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        lighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4(lighting * uTintColor, 1.0) * aColor;

    // Linear fog factor (1.0 = no fog, 0.0 = full fog)
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// GLSL ES 1.00 fragment shader — solid (opaque)
static const char* FRAGMENT_SHADER_SOLID_SRC = R"(
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

// GLSL ES 1.00 fragment shader — alpha-test (base)
static const char* FRAGMENT_SHADER_ALPHA_SRC_BASE = R"(
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

// GLSL ES 1.00 fragment shader — alpha-test (derivatives variant)
// Clamp threshold to [0.1, 0.5] so binary alpha masks still discard transparent pixels.
static const char* FRAGMENT_SHADER_ALPHA_SRC_DERIVATIVES = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float threshold = clamp(0.5 - fwidth(texColor.a), 0.1, 0.5);
    if (texColor.a < threshold) discard;
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// GLSL ES 1.00 atlas vertex shader — per-vertex lighting, precomputed atlas UV
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
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    // Point lights (world space, always iterate all 8)
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        lighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4(lighting * uTintColor, 1.0);

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

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// GLSL ES 1.00 atlas fragment shader — alpha (dual ETC1, base)
static const char* ATLAS_FRAGMENT_SHADER_ALPHA_SRC_BASE = R"(
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

// GLSL ES 1.00 atlas fragment shader — alpha (dual ETC1, derivatives variant)
// Clamp threshold to [0.1, 0.5] so ETC1-compressed binary alpha still discards.
static const char* ATLAS_FRAGMENT_SHADER_ALPHA_SRC_DERIVATIVES = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
    float threshold = clamp(0.5 - fwidth(alpha), 0.1, 0.5);
    if (alpha < threshold) discard;

    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

#else // !EQT_HAS_GLES2 — Desktop GL / GLSL 1.20

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

varying vec4 vColor;
varying float vFogFactor;

void main() {
    gl_Position = mWorldViewProj * gl_Vertex;
    // Use gl_TexCoord (built-in varying) instead of a custom varying vec2.
    // On Mali 400, built-in texture coordinate varyings may be routed through
    // dedicated interpolation hardware with higher precision than the generic
    // FP16 varying interpolators, avoiding blocky texture artifacts.
    gl_TexCoord[0] = gl_MultiTexCoord0;

    vec3 worldPos = (mWorld * gl_Vertex).xyz;
    vec3 worldN = normalize((mWorld * vec4(gl_Normal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    // Point lights (world space, always iterate all 8)
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        lighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4(lighting * uTintColor, 1.0) * gl_Color;

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

varying vec4 vColor;
varying float vFogFactor;

void main() {
    gl_Position = mWorldViewProj * gl_Vertex;

    // Pass precomputed atlas UV through built-in varying for
    // high-precision interpolation on Mali 400.
    gl_TexCoord[0] = gl_MultiTexCoord1;

    vec3 worldPos = (mWorld * gl_Vertex).xyz;
    vec3 worldN = normalize((mWorld * vec4(gl_Normal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    // Point lights (world space, always iterate all 8)
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        lighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4(lighting * uTintColor, 1.0);

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

#endif // EQT_HAS_GLES2

// ============================================================================
// Shader callbacks
// ============================================================================

// Existing per-vertex lighting callback
class ShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    ShaderCallback(ZoneShaderManager* owner) : owner_(owner) {}

    void OnSetMaterial(const irr::video::SMaterial& material) override {}

    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 userData) override {
        irr::video::IVideoDriver* driver = services->getVideoDriver();

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
        // Lima (Mali 400 Mesa driver) requires GL_TEXTURE_2D to be enabled for
        // GLSL texture2D() sampling to work. The atlas callback enables it for
        // its manually-bound textures, but it can also get disabled by Irrlicht
        // when switching materials. Ensure it's on for entity/tree rendering too.
        // Not needed under native GLES2 — GL_TEXTURE_2D enable/disable doesn't exist.
        glEnable(GL_TEXTURE_2D);
        // Override texture filtering on Lima.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#endif

        // Matrices — Irrlicht provides these automatically for built-in names
        // mWorldViewProj and mWorld, but we need to set them explicitly for GLSL
        irr::core::matrix4 worldViewProj = driver->getTransform(irr::video::ETS_PROJECTION);
        worldViewProj *= driver->getTransform(irr::video::ETS_VIEW);
        worldViewProj *= driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorldViewProj", worldViewProj.pointer(), 16);

        irr::core::matrix4 world = driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorld", world.pointer(), 16);

        // Custom uniforms
        services->setVertexShaderConstant("uSunDir", owner_->sunDir(), 3);
        services->setVertexShaderConstant("uSunColor", owner_->sunColor(), 3);
        services->setVertexShaderConstant("uAmbientColor", owner_->ambientColor(), 3);
        services->setVertexShaderConstant("uTintColor", owner_->tintColor(), 3);

        float fogStart = owner_->fogStart();
        float fogEnd = owner_->fogEnd();
        services->setVertexShaderConstant("uFogStart", &fogStart, 1);
        services->setVertexShaderConstant("uFogEnd", &fogEnd, 1);

        // Point lights — data fed directly from updateObjectLights()
        // Lima/Mesa returns "uLightPos[0]" from glGetActiveUniform (confirmed via logging).
        services->setVertexShaderConstant("uLightPos[0]", owner_->lightPos(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightColor[0]", owner_->lightColor(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightAtten[0]", owner_->lightAtten(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);

        // Fragment shader uniforms
        float fogColor[4];
        std::memcpy(fogColor, owner_->fogColor(), sizeof(fogColor));
        services->setPixelShaderConstant("uFogColor", fogColor, 4);

        irr::s32 texUnit = 0;
        services->setPixelShaderConstant("uTexture", &texUnit, 1);
    }

private:
    ZoneShaderManager* owner_;
};

// Atlas shader callback — per-vertex lighting, atlas texture binding
class AtlasShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    AtlasShaderCallback(ZoneShaderManager* owner, bool hasAlphaTexture)
        : owner_(owner), hasAlphaTexture_(hasAlphaTexture) {}

    void OnSetMaterial(const irr::video::SMaterial& material) override {
        // Extract atlas page index from material params (set by buildAtlasedMesh)
        currentPageIndex_ = static_cast<int>(material.MaterialTypeParam);
        if (hasAlphaTexture_) {
            currentAlphaPageIndex_ = static_cast<int>(material.MaterialTypeParam2);
        }
    }

    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 userData) override {
        irr::video::IVideoDriver* driver = services->getVideoDriver();

#ifdef EQT_HAS_DRM
        // Bind atlas page texture to unit 0.
        // Atlas page textures are raw GL handles (not ITexture), so we bind manually.
        uint32_t rgbTex = owner_->getAtlasPageTexture(static_cast<uint16_t>(currentPageIndex_));
        if (bindLogCount_ < 5) {
            LOG_INFO(MOD_GRAPHICS, "AtlasShader: binding page {} -> GL tex {} (alpha={})",
                     currentPageIndex_, rgbTex, hasAlphaTexture_);
            ++bindLogCount_;
        }
        if (rgbTex > 0) {
            glActiveTexture(GL_TEXTURE0);
#ifndef EQT_HAS_GLES2
            // Lima (Mali 400 Mesa driver) requires GL_TEXTURE_2D to be enabled for
            // GLSL texture2D() sampling to work via desktop GL. Not needed on GLES2.
            glEnable(GL_TEXTURE_2D);
#endif
            glBindTexture(GL_TEXTURE_2D, rgbTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        // Bind alpha page texture to unit 1 if needed
        if (hasAlphaTexture_) {
            uint32_t alphaTex = owner_->getAtlasPageTexture(static_cast<uint16_t>(currentAlphaPageIndex_));
            if (alphaTex > 0) {
                glActiveTexture(GL_TEXTURE1);
#ifndef EQT_HAS_GLES2
                glEnable(GL_TEXTURE_2D);
#endif
                glBindTexture(GL_TEXTURE_2D, alphaTex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glActiveTexture(GL_TEXTURE0);  // Restore
            }
        }
#endif

        // Matrices
        irr::core::matrix4 worldViewProj = driver->getTransform(irr::video::ETS_PROJECTION);
        worldViewProj *= driver->getTransform(irr::video::ETS_VIEW);
        worldViewProj *= driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorldViewProj", worldViewProj.pointer(), 16);

        irr::core::matrix4 world = driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorld", world.pointer(), 16);

        // Vertex shader uniforms — lighting moved to VS for Mali 400 perf
        services->setVertexShaderConstant("uSunDir", owner_->sunDir(), 3);
        services->setVertexShaderConstant("uSunColor", owner_->sunColor(), 3);
        services->setVertexShaderConstant("uAmbientColor", owner_->ambientColor(), 3);
        services->setVertexShaderConstant("uTintColor", owner_->tintColor(), 3);

        float fogStart = owner_->fogStart();
        float fogEnd = owner_->fogEnd();
        services->setVertexShaderConstant("uFogStart", &fogStart, 1);
        services->setVertexShaderConstant("uFogEnd", &fogEnd, 1);

        // Point lights
        services->setVertexShaderConstant("uLightPos[0]", owner_->lightPos(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightColor[0]", owner_->lightColor(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightAtten[0]", owner_->lightAtten(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);

        // Fragment shader uniforms — fog color and texture samplers
        float fogColor[4];
        std::memcpy(fogColor, owner_->fogColor(), sizeof(fogColor));
        services->setPixelShaderConstant("uFogColor", fogColor, 4);

        irr::s32 texUnit0 = 0;
        services->setPixelShaderConstant("uTexture", &texUnit0, 1);
        if (hasAlphaTexture_) {
            irr::s32 texUnit1 = 1;
            services->setPixelShaderConstant("uAlphaTexture", &texUnit1, 1);
        }
    }

private:
    ZoneShaderManager* owner_;
    bool hasAlphaTexture_;
    int currentPageIndex_ = 0;
    int currentAlphaPageIndex_ = 0;
    int bindLogCount_ = 0;
};

// ============================================================================
// ZoneShaderManager constructor
// ============================================================================

ZoneShaderManager::ZoneShaderManager(irr::video::IVideoDriver* driver,
                                     irr::video::IGPUProgrammingServices* gpu) {
    if (!driver || !gpu) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: No GPU programming services available");
        return;
    }

    // Check for shader support
    if (!driver->queryFeature(irr::video::EVDF_VERTEX_SHADER_1_1) ||
        !driver->queryFeature(irr::video::EVDF_PIXEL_SHADER_1_1)) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Vertex/pixel shaders not supported");
        return;
    }

#ifdef EQT_HAS_GLES2
    // Lima driver's fwidth() returns degenerate values — use base alpha shaders.
    const char* fragmentAlphaSrc = FRAGMENT_SHADER_ALPHA_SRC_BASE;
    const char* atlasFragmentAlphaSrc = ATLAS_FRAGMENT_SHADER_ALPHA_SRC_BASE;
#else
    const char* fragmentAlphaSrc = FRAGMENT_SHADER_ALPHA_SRC;
    const char* atlasFragmentAlphaSrc = ATLAS_FRAGMENT_SHADER_ALPHA_SRC;
#endif

    // Create callback (shared between both material types)
    // Irrlicht takes ownership via reference counting
    ShaderCallback* callback = new ShaderCallback(this);

    // Create solid material type
    materialSolid_ = gpu->addHighLevelShaderMaterial(
        VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        FRAGMENT_SHADER_SOLID_SRC, "main", irr::video::EPST_PS_1_1,
        callback,
        irr::video::EMT_SOLID);

    if (materialSolid_ < 0) {
        LOG_ERROR(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile solid shader");
        callback->drop();
        return;
    }

    // Create alpha-test material type
    materialAlphaTest_ = gpu->addHighLevelShaderMaterial(
        VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        fragmentAlphaSrc, "main", irr::video::EPST_PS_1_1,
        callback,
        irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);

    if (materialAlphaTest_ < 0) {
        LOG_ERROR(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile alpha-test shader");
        materialSolid_ = -1;
        callback->drop();
        return;
    }

    callback->drop();
    available_ = true;
    LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: GLSL shaders compiled (solid={}, alphaTest={})",
             materialSolid_, materialAlphaTest_);

    // Compile atlas shaders (separate callbacks for solid and alpha)
    AtlasShaderCallback* atlasSolidCallback = new AtlasShaderCallback(this, false);
    materialAtlasSolid_ = gpu->addHighLevelShaderMaterial(
        ATLAS_VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        ATLAS_FRAGMENT_SHADER_SOLID_SRC, "main", irr::video::EPST_PS_1_1,
        atlasSolidCallback,
        irr::video::EMT_SOLID);
    atlasSolidCallback->drop();

    if (materialAtlasSolid_ < 0) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile atlas solid shader");
    } else {
        AtlasShaderCallback* atlasAlphaCallback = new AtlasShaderCallback(this, true);
        materialAtlasAlpha_ = gpu->addHighLevelShaderMaterial(
            ATLAS_VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
            atlasFragmentAlphaSrc, "main", irr::video::EPST_PS_1_1,
            atlasAlphaCallback,
            irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
        atlasAlphaCallback->drop();

        if (materialAtlasAlpha_ < 0) {
            LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile atlas alpha shader");
            materialAtlasSolid_ = -1;  // Disable atlas shaders entirely
        } else {
            LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: Atlas GLSL shaders compiled (solid={}, alpha={})",
                     materialAtlasSolid_, materialAtlasAlpha_);
        }
    }
}

// ============================================================================
// Uniform setters
// ============================================================================

void ZoneShaderManager::setFog(float start, float end, float r, float g, float b, float a) {
    fogStart_ = start;
    fogEnd_ = end;
    fogColor_[0] = r;
    fogColor_[1] = g;
    fogColor_[2] = b;
    fogColor_[3] = a;
}

void ZoneShaderManager::setSunDirection(float x, float y, float z) {
    sunDir_[0] = x;
    sunDir_[1] = y;
    sunDir_[2] = z;
}

void ZoneShaderManager::setSunColor(float r, float g, float b) {
    sunColor_[0] = r;
    sunColor_[1] = g;
    sunColor_[2] = b;
}

void ZoneShaderManager::setAmbientColor(float r, float g, float b) {
    ambientColor_[0] = r;
    ambientColor_[1] = g;
    ambientColor_[2] = b;
}

void ZoneShaderManager::setTintColor(float r, float g, float b) {
    tintColor_[0] = r;
    tintColor_[1] = g;
    tintColor_[2] = b;
}

void ZoneShaderManager::setCameraPos(float x, float y, float z) {
    cameraPos_[0] = x;
    cameraPos_[1] = y;
    cameraPos_[2] = z;
}

} // namespace Graphics
} // namespace EQT
