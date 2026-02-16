#include "client/graphics/zone_shader.h"
#include "common/logging.h"
#include <cstring>

namespace EQT {
namespace Graphics {

// GLSL 1.20 vertex shader — fog, directional lighting, day/night tint
static const char* VERTEX_SHADER_SRC = R"(
#version 120

// Irrlicht built-in matrices (set automatically)
uniform mat4 mWorldViewProj;
uniform mat4 mWorld;

// Custom uniforms
uniform vec3 uSunDir;       // world-space sun direction (unnormalized OK)
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uTintColor;
uniform float uFogStart;
uniform float uFogEnd;

varying vec2 vTexCoord;
varying vec4 vColor;
varying float vFogFactor;

void main() {
    gl_Position = mWorldViewProj * gl_Vertex;

    vTexCoord = gl_MultiTexCoord0.xy;

    // Compute world-space normal for lighting
    vec3 N = normalize((mWorld * vec4(gl_Normal, 0.0)).xyz);
    vec3 L = normalize(-uSunDir);
    float NdotL = max(dot(N, L), 0.0);

    // Directional diffuse + ambient, modulated by tint and vertex color
    vec3 lighting = uAmbientColor + NdotL * uSunColor;
    vColor = vec4(lighting * uTintColor, 1.0) * gl_Color;

    // Linear fog factor (1.0 = no fog, 0.0 = full fog)
    float dist = length((mWorldViewProj * gl_Vertex).xyz);
    vFogFactor = clamp((uFogEnd - dist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Fragment shader — solid (opaque) variant
static const char* FRAGMENT_SHADER_SOLID_SRC = R"(
#version 120

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec2 vTexCoord;
varying vec4 vColor;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Fragment shader — alpha-test variant (for vegetation/transparency)
static const char* FRAGMENT_SHADER_ALPHA_SRC = R"(
#version 120

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec2 vTexCoord;
varying vec4 vColor;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    if (texColor.a < 0.5) discard;
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Shader constant callback — uploads uniforms each draw call
class ShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    ShaderCallback(ZoneShaderManager* owner) : owner_(owner) {}

    void OnSetMaterial(const irr::video::SMaterial& material) override {}

    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 userData) override {
        irr::video::IVideoDriver* driver = services->getVideoDriver();

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
        FRAGMENT_SHADER_ALPHA_SRC, "main", irr::video::EPST_PS_1_1,
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
}

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

} // namespace Graphics
} // namespace EQT
