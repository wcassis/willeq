// B964: one draw per impostor atlas page -- see impostor_batch_node.h.

#include "client/graphics/impostor/impostor_batch_node.h"

#include "common/logging.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

namespace EQT {
namespace Graphics {

namespace {

// The entity vertex shader's fog, evaluated the same way (zone_shader_sources_gles2.h,
// VERTEX_SHADER_SRC): linear in the length of the clip-space position.
const char* kImpostorVS = R"(
precision highp float;
attribute vec3 aPosition;
attribute vec2 aTexCoord0;
uniform mat4 mWorldViewProj;
uniform float uFogStart;
uniform float uFogEnd;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;
    vTexCoord = aTexCoord0;
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Constant threshold (B958). The capture's clear is (0,0,0,0), so a bilinear
// sample is premultiplied by its coverage; dividing by alpha (>= 0.5 here)
// returns the opaque neighbours' own colour instead of a darkened one (#274).
const char* kImpostorFS = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 t = texture2D(uTexture, vTexCoord);
    if (t.a < 0.5) discard;
    gl_FragColor = mix(uFogColor, vec4(t.rgb / t.a, 1.0), vFogFactor);
}
)";

class ImpostorShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    explicit ImpostorShaderCallback(const int* diagMode) : diagMode_(diagMode) {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override {
        irr::video::IVideoDriver* driver = services->getVideoDriver();
        irr::core::matrix4 wvp = driver->getTransform(irr::video::ETS_PROJECTION);
        wvp *= driver->getTransform(irr::video::ETS_VIEW);
        wvp *= driver->getTransform(irr::video::ETS_WORLD);
        irr::video::SColor fogColor;
        irr::video::E_FOG_TYPE fogType;
        irr::f32 fogStart = 0.0f, fogEnd = 1.0f, fogDensity = 0.0f;
        bool pixelFog = false, rangeFog = false;
        driver->getFog(fogColor, fogType, fogStart, fogEnd, fogDensity, pixelFog, rangeFog);
        if (fogEnd <= fogStart) fogEnd = fogStart + 1.0f;
        if (diagMode_ && *diagMode_ == 2) {  // diagnostic: no fog
            fogStart = 1e9f;
            fogEnd = 2e9f;
        }
        const float fog[4] = {fogColor.getRed() / 255.0f, fogColor.getGreen() / 255.0f,
                              fogColor.getBlue() / 255.0f, fogColor.getAlpha() / 255.0f};
#ifdef EQT_HAS_GLES2
        GLint prog = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        if (prog != prog_) {
            prog_ = prog;
            locWvp_ = glGetUniformLocation(prog, "mWorldViewProj");
            locFogStart_ = glGetUniformLocation(prog, "uFogStart");
            locFogEnd_ = glGetUniformLocation(prog, "uFogEnd");
            locFogColor_ = glGetUniformLocation(prog, "uFogColor");
            locTex_ = glGetUniformLocation(prog, "uTexture");
        }
        if (locWvp_ >= 0) glUniformMatrix4fv(locWvp_, 1, GL_FALSE, wvp.pointer());
        if (locFogStart_ >= 0) glUniform1f(locFogStart_, fogStart);
        if (locFogEnd_ >= 0) glUniform1f(locFogEnd_, fogEnd);
        if (locFogColor_ >= 0) glUniform4fv(locFogColor_, 1, fog);
        if (locTex_ >= 0) glUniform1i(locTex_, 0);
#else
        services->setVertexShaderConstant("mWorldViewProj", wvp.pointer(), 16);
        services->setVertexShaderConstant("uFogStart", &fogStart, 1);
        services->setVertexShaderConstant("uFogEnd", &fogEnd, 1);
        services->setPixelShaderConstant("uFogColor", fog, 4);
        irr::s32 unit = 0;
        services->setPixelShaderConstant("uTexture", &unit, 1);
#endif
    }

private:
    const int* diagMode_ = nullptr;
#ifdef EQT_HAS_GLES2
    GLint prog_ = -1;
    GLint locWvp_ = -1, locFogStart_ = -1, locFogEnd_ = -1, locFogColor_ = -1, locTex_ = -1;
#endif
};

}  // namespace

ImpostorBatchNode::ImpostorBatchNode(irr::scene::ISceneNode* parent, irr::scene::ISceneManager* mgr)
    : irr::scene::ISceneNode(parent, mgr, -1) {
    box_ = irr::core::aabbox3d<irr::f32>(-1e7f, -1e7f, -1e7f, 1e7f, 1e7f, 1e7f);
    setAutomaticCulling(irr::scene::EAC_OFF);
    compileProgram();
    material_.Lighting = false;
    material_.BackfaceCulling = false;
    material_.FogEnable = false;  // the program applies the entity shader's fog itself
    material_.ZWriteEnable = true;
    material_.TextureLayer[0].BilinearFilter = true;
    material_.TextureLayer[0].TextureWrapU = irr::video::ETC_CLAMP_TO_EDGE;
    material_.TextureLayer[0].TextureWrapV = irr::video::ETC_CLAMP_TO_EDGE;
    if (materialType_ >= 0) {
        material_.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(materialType_);
    } else {
        // The built-in alpha test (constant 0.5 since B958) without the
        // un-premultiply: the fringe returns, the draw still works.
        material_.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
        material_.MaterialTypeParam = 0.5f;
    }
}

ImpostorBatchNode::~ImpostorBatchNode() { clearQuads(); }

void ImpostorBatchNode::setDiagnosticMode(int mode) {
    diagMode_ = mode;
    if (mode == 1 || materialType_ < 0) {
        material_.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
        material_.MaterialTypeParam = 0.5f;
    } else {
        material_.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(materialType_);
        material_.MaterialTypeParam = 0.0f;
    }
}

void ImpostorBatchNode::compileProgram() {
    irr::video::IVideoDriver* driver = SceneManager ? SceneManager->getVideoDriver() : nullptr;
    irr::video::IGPUProgrammingServices* gpu = driver ? driver->getGPUProgrammingServices() : nullptr;
    if (!gpu) return;
    auto* cb = new ImpostorShaderCallback(&diagMode_);
    materialType_ = gpu->addHighLevelShaderMaterial(kImpostorVS, "main", irr::video::EVST_VS_1_1,
                                                    kImpostorFS, "main", irr::video::EPST_PS_1_1, cb,
                                                    irr::video::EMT_SOLID);
    cb->drop();
    if (materialType_ < 0) {
        LOG_WARN(MOD_GRAPHICS, "Impostor: billboard program did not compile; using the built-in alpha test");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Impostor: billboard program compiled (material {})", materialType_);
    }
}

void ImpostorBatchNode::clearQuads() {
    for (auto& p : pages_) p.quads.clear();
    for (auto* n : names_) n->drop();
    names_.clear();
    quadCount_ = 0;
}

void ImpostorBatchNode::addQuad(int page, irr::video::ITexture* pageTex, const irr::core::vector3df& center,
                                float halfW, float halfH, float u0, float v0, float u1, float v1) {
    if (page < 0) return;
    if (static_cast<size_t>(page) >= pages_.size()) pages_.resize(page + 1);
    pages_[page].tex = pageTex;
    pages_[page].quads.push_back(Quad{center, halfW, halfH, u0, v0, u1, v1});
    ++quadCount_;
}

void ImpostorBatchNode::addNameNode(irr::scene::ISceneNode* node) {
    if (!node) return;
    node->grab();
    names_.push_back(node);
}

void ImpostorBatchNode::OnRegisterSceneNode() {
    if (quadCount_ == 0) drawCallsLastRender_ = 0;  // render() will not run this frame
    if (IsVisible) {
        if (quadCount_ > 0) SceneManager->registerNodeForRendering(this, irr::scene::ESNRP_SOLID);
        // The name tags of withheld entities: register them as their parent
        // would have (the text node checks its own visibility).
        for (auto* n : names_) n->OnRegisterSceneNode();
    }
    ISceneNode::OnRegisterSceneNode();
}

void ImpostorBatchNode::render() {
    drawCallsLastRender_ = 0;
    irr::video::IVideoDriver* driver = SceneManager->getVideoDriver();
    irr::scene::ICameraSceneNode* cam = SceneManager->getActiveCamera();
    if (!driver || !cam || quadCount_ == 0) return;
    const irr::core::vector3df camPos = cam->getAbsolutePosition();
    driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
    const irr::video::SColor white(255, 255, 255, 255);

    for (const auto& page : pages_) {
        if (page.quads.empty() || !page.tex) continue;
        verts_.clear();
        indices_.clear();
        for (const Quad& q : page.quads) {
            // Face the camera along the horizontal camera->billboard direction
            // (the direction the capture looked along); right = up x forward.
            irr::core::vector3df d(q.center.X - camPos.X, 0.0f, q.center.Z - camPos.Z);
            if (d.getLengthSQ() < 1e-6f) d.set(0.0f, 0.0f, 1.0f);
            d.normalize();
            const irr::core::vector3df r(d.Z * q.halfW, 0.0f, -d.X * q.halfW);
            const float y0 = q.center.Y - q.halfH, y1 = q.center.Y + q.halfH;
            const irr::u16 base = static_cast<irr::u16>(verts_.size());
            // An FBO's row 0 is the bottom of the capture: v0 on the bottom edge.
            verts_.emplace_back(q.center.X - r.X, y0, q.center.Z - r.Z, 0, 0, 1, white, q.u0, q.v0);
            verts_.emplace_back(q.center.X + r.X, y0, q.center.Z + r.Z, 0, 0, 1, white, q.u1, q.v0);
            verts_.emplace_back(q.center.X + r.X, y1, q.center.Z + r.Z, 0, 0, 1, white, q.u1, q.v1);
            verts_.emplace_back(q.center.X - r.X, y1, q.center.Z - r.Z, 0, 0, 1, white, q.u0, q.v1);
            const irr::u16 quadIdx[6] = {0, 1, 2, 0, 2, 3};
            for (irr::u16 i : quadIdx) indices_.push_back(static_cast<irr::u16>(base + i));
            // 16-bit indices: flush at the limit (16,384 billboards a page is
            // far past the 64 cells one holds, so this never triggers).
        }
        material_.setTexture(0, page.tex);
        driver->setMaterial(material_);
        driver->drawVertexPrimitiveList(verts_.data(), static_cast<irr::u32>(verts_.size()), indices_.data(),
                                        static_cast<irr::u32>(indices_.size() / 3), irr::video::EVT_STANDARD,
                                        irr::scene::EPT_TRIANGLES, irr::video::EIT_16BIT);
        ++drawCallsLastRender_;
    }
}

}  // namespace Graphics
}  // namespace EQT
