// B964: one draw per impostor atlas page -- see impostor_batch_node.h.

#include "client/graphics/impostor/impostor_batch_node.h"

#include "client/core/gem_owner_census.h"
#include "common/logging.h"

#include <chrono>
#include <cstdlib>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
extern void gles2AdjustRawBufferBytes(void* driver, ptrdiff_t delta);
extern void gles2SyncBufferBindings(void* driver);
#endif

namespace EQT {
namespace Graphics {

namespace {

// docker/irrlicht-drm/COGLES2Shaders.h: the fixed attribute slots (EOGLES2VA_*)
// the driver binds with glBindAttribLocation before every program link -- the
// custom program's own aPosition/aTexCoord0 ride slots 0 and 3 of that.
constexpr unsigned kAttrPosition = 0;
constexpr unsigned kAttrNormal = 1;
constexpr unsigned kAttrColor = 2;
constexpr unsigned kAttrTexCoord0 = 3;
constexpr unsigned kAttrTexCoord1 = 4;

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

ImpostorBatchNode::~ImpostorBatchNode() {
    releaseGpuBuffers();
    clearQuads();
}

// B972-FT: the configured ring depth: B972FT_RING_DEPTH (1..3), default 3.
// 1 is the pre-B972-FT single shared pair -- the A/C control on the Pi.
size_t ImpostorBatchNode::ringDepth() const {
    const char* env = getenv("B972FT_RING_DEPTH");
    if (env && env[0]) {
        const long d = strtol(env, nullptr, 10);
        if (d >= 1 && d <= (long)kVboRingDepthMax) return static_cast<size_t>(d);
    }
    return 3;
}

size_t ImpostorBatchNode::vboBytes() const {
    size_t sum = 0;
    for (const VboPair& p : vboRing_) sum += p.vertBytes + p.idxBytes;
    return sum;
}

void ImpostorBatchNode::releaseGpuBuffers() {
#ifdef EQT_HAS_GLES2
    size_t total = 0;
    for (const VboPair& p : vboRing_) total += p.vertBytes + p.idxBytes;
    for (VboPair& p : vboRing_) {
        if (p.vert) glDeleteBuffers(1, &p.vert);
        if (p.idx) glDeleteBuffers(1, &p.idx);
        p.vert = p.idx = 0;
        p.vertBytes = p.idxBytes = 0;
    }
    vboRing_.clear();
    if (gpuDriver_)
        gles2AdjustRawBufferBytes(gpuDriver_, -static_cast<ptrdiff_t>(total));
#endif
    gpuDriver_ = nullptr;
}

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
    if (!driver || !cam || quadCount_ == 0) {
        if (const char* d = getenv("B972_DIAG")) fprintf(stderr, "[B972] render SKIP %s\n", d);
        return;
    }
    const irr::core::vector3df camPos = cam->getAbsolutePosition();
    driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
    if (getenv("B972_DIAG")) {
        const float* pj = driver->getTransform(irr::video::ETS_PROJECTION).pointer();
        const float* vw = driver->getTransform(irr::video::ETS_VIEW).pointer();
        fprintf(stderr, "[B972] render quads=%d vbo=%d matType=%d prog=%d\n", (int)quadCount_,
                (int)useVbo_, (int)material_.MaterialType, materialType_);
        fprintf(stderr, "[B972] PROJ[0][0]=%g PROJ[1][1]=%g VIEW[0][3]=%g VIEW[1][3]=%g VIEW[2][3]=%g\n",
                pj[0], pj[5], vw[12], vw[13], vw[14]);
    }
    const irr::video::SColor white(255, 255, 255, 255);

    // B972-FT: this frame's ring row. Frame N page p writes pair
    // row * pages_ + p (row = render()-count % depth), so within-frame pages
    // use distinct pairs and the in-flight frames N-1/N-2 use other rows --
    // the written BO is referenced by no live draw. Depth 1 collapses
    // everything onto pair 0: the pre-B972-FT single shared pair, the A/C
    // control.
    const size_t depth = ringDepth();
    const size_t row = depth > 1 ? (frameCount_ % depth) : 0;
    const size_t slotBase = depth > 1 ? row * pages_.size() : 0;
    ++frameCount_;

    const size_t pageCount = pages_.size();
    for (size_t pageIdx = 0; pageIdx < pageCount; ++pageIdx) {
        const auto& page = pages_[pageIdx];
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
#ifdef EQT_HAS_GLES2
        // B972 (#290): the bounded-VBO path replaces the client-array draw
        // (whose 1 MiB Mesa stream slot and per-draw varying BO are what the
        // crowd-tour census carries as unnamed GEM). Any other driver, or the
        // pixel-identity baseline (setVboPathEnabled(false), test_b972),
        // keeps the old path.
        if (useVbo_ && driver->getDriverType() == irr::video::EDT_OGLES2) {
            drawPageVbo(driver, slotBase + pageIdx, pageIdx);
        } else
#endif
        {
            driver->drawVertexPrimitiveList(verts_.data(), static_cast<irr::u32>(verts_.size()),
                                            indices_.data(), static_cast<irr::u32>(indices_.size() / 3),
                                            irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                                            irr::video::EIT_16BIT);
        }
        ++drawCallsLastRender_;
    }
}

// B972 (#290) / B972-FT (#326): one page's quads through the bounded VBO/EBO
// of the ring's `slot`th pair. The material state (blend off / depth write
// on / the page texture on unit 0, the custom program, its uniforms) is
// exactly what the preceding driver->setMaterial() applied, so this only
// supplies the vertex sources and issues the draw.
void ImpostorBatchNode::drawPageVbo(irr::video::IVideoDriver* driver, size_t slot, size_t page) {
#ifdef EQT_HAS_GLES2
    if (verts_.empty() || indices_.empty()) return;
    // The tag spans the whole run: the glBufferData growth AND the per-page
    // glBufferSubData uploads both allocate GEM storage (Lima's varying
    // reuses the bounded BO on the partial write, but the attribution must
    // still name them vbo:impostor, not leave them for the 1 MiB slot).
    eqt::memory::GemTag gemTag("vbo:impostor");
    gpuDriver_ = driver;
    if (slot >= vboRing_.size()) vboRing_.resize(slot + 1);
    VboPair& pair = vboRing_[slot];
    if (!pair.vert) glGenBuffers(1, &pair.vert);
    if (!pair.idx) glGenBuffers(1, &pair.idx);
    glBindBuffer(GL_ARRAY_BUFFER, pair.vert);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pair.idx);
    // One slack vertex/index keeps every subdata write strictly smaller than
    // the allocation (B975): a whole-resource write is what renames a fresh
    // BO; a partial write reuses the bounded storage.
    const size_t neededVerts =
        verts_.size() * sizeof(irr::video::S3DVertex) + sizeof(irr::video::S3DVertex);
    const size_t neededIdx =
        indices_.size() * sizeof(irr::u16) + sizeof(irr::u16);
    if (neededVerts > pair.vertBytes) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(neededVerts), nullptr,
                     GL_DYNAMIC_DRAW);
        gles2AdjustRawBufferBytes(gpuDriver_,
                                  static_cast<ptrdiff_t>(neededVerts) - pair.vertBytes);
        pair.vertBytes = neededVerts;
    }
    if (neededIdx > pair.idxBytes) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(neededIdx), nullptr,
                     GL_DYNAMIC_DRAW);
        gles2AdjustRawBufferBytes(gpuDriver_,
                                  static_cast<ptrdiff_t>(neededIdx) - pair.idxBytes);
        pair.idxBytes = neededIdx;
    }
    // B972-FT: the confirming measurement (issue #326, acceptance gate 4).
    // WILLEQ_B972FT_TIMING=1 wraps every glBufferSubData in a wall clock and
    // reports per page + per frame. On Lima the subdata's wall clock carries
    // lima_transfer_map's lima_flush_job_accessing_bo + lima_bo_wait -- with
    // a shared pair (depth 1) every page >= 2 blocks; with the ring it must
    // collapse toward the memcpy alone. The log goes to stderr (the sweep
    // runner prefixes every line with its epoch and keeps it in client.log).
    if (getenv("WILLEQ_B972FT_TIMING")) {
        static size_t frameSubdatas = 0;
        static int64_t frameVertUs = 0;
        static int64_t frameIdxUs = 0;
        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(verts_.size() * sizeof(irr::video::S3DVertex)),
                        verts_.data());
        const auto t1 = Clock::now();
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(indices_.size() * sizeof(irr::u16)), indices_.data());
        const auto t2 = Clock::now();
        const int64_t vertUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        const int64_t idxUs = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        ++frameSubdatas;
        frameVertUs += vertUs;
        frameIdxUs += idxUs;
        fprintf(stderr, "[B972FT] subdata frame=%u page=%zu slot=%zu vert=%lld us idx=%lld us verts=%zu idx=%zu\n",
                (unsigned)(frameCount_ - 1), page, slot,
                (long long)vertUs, (long long)idxUs, verts_.size(), indices_.size());
        // One frame line: this frame's subdata count and totals, printed when
        // the LAST page of the frame uploads (a skipped page defers the line
        // to the next frame's last page -- per-call lines are the evidence).
        if (page + 1 == pages_.size()) {
            fprintf(stderr, "[B972FT] frame=%u subdatas=%zu vert_total=%lld us idx_total=%lld us\n",
                    (unsigned)(frameCount_ - 1), frameSubdatas,
                    (long long)frameVertUs, (long long)frameIdxUs);
            frameSubdatas = 0;
            frameVertUs = 0;
            frameIdxUs = 0;
        }
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(verts_.size() * sizeof(irr::video::S3DVertex)),
                        verts_.data());
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(indices_.size() * sizeof(irr::u16)), indices_.data());
    }

    const GLsizei stride = sizeof(irr::video::S3DVertex);
    glVertexAttribPointer(kAttrPosition, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const GLvoid*>(0));
    glEnableVertexAttribArray(kAttrPosition);
    glVertexAttribPointer(kAttrNormal, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const GLvoid*>(12));
    glEnableVertexAttribArray(kAttrNormal);
    glVertexAttribPointer(kAttrColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<const GLvoid*>(24));
    glEnableVertexAttribArray(kAttrColor);
    glVertexAttribPointer(kAttrTexCoord0, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const GLvoid*>(28));
    glEnableVertexAttribArray(kAttrTexCoord0);
    glDisableVertexAttribArray(kAttrTexCoord1);

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_SHORT,
                   reinterpret_cast<const GLvoid*>(0));

    // The client-array fallbacks below this draw skip a bind their state_
    // cache says is a no-op, so re-read the real bindings: the GL binds are
    // left on the VBO/EBO by design, and a later 2D client-array draw must
    // not sample through them.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gles2SyncBufferBindings(driver);
#else
    (void)driver;
#endif
}

}  // namespace Graphics
}  // namespace EQT
