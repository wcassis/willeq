// EQE1 flat-pack archive plumbing + the fire-glow icosphere renderer --
// EQT::Graphics::Eqe1ArchiveComponent.
//
// L02 (Batch L, refactor/L_renderer_component_extraction.md): moved here from
// src/client/graphics/renderer_effects.cpp and rebased from IrrlichtRenderer
// onto Eqe1ArchiveComponent. Verbatim move -- the only edits inside a body are
// the batch's sanctioned substitutions:
//   * RendererContext core members (5): camera_, config_,
//     constrainedTextureCache_, currentZoneName_, zoneShader_ -> ctx_.<name>
//   * IrrlichtRenderer members shared with another family, which stay on the
//     renderer under Mechanism 3 and are reached through the transition
//     handle (17): captured3DProj_, captured3DView_, eqe1CandidateRaces_,
//     eqe1CandidatesMutex_, eqe1Index_, eqe1Loaders_,
//     eqe1LoadTimeCandidateCount_, eqe1Mutex_, fireGlowLights_, fireGlowTime_,
//     icosphereIBO_, icosphereIndices_, icosphereProgram_, icosphereVBO_,
//     icosphereVertices_, maxFireGlowLights_, pendingGlobalEQE1Archives_
//     -> owner_.<name>
//   * IrrlichtRenderer:: -> Eqe1ArchiveComponent:: (the 14 definition headers
//     plus the one qualified call to isPersistentEQE1Archive, which moved)
// The 9 icoLoc* uniform locations, the EQE1ArchiveEntry struct and the
// eqe1ArchiveEntries_ map are single-family and moved into the component, so
// they keep their bare names. No body was otherwise touched. See the L02
// Review.
//
// irrlicht_renderer.h is still included: 17 owner_ members are private
// IrrlichtRenderer state, so the complete type is required (and the component
// is a friend).

#include "client/graphics/eqe1/eqe1_archive_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/race_codes.h"
#include "common/logging.h"
#include "client/core/gem_owner_census.h"  // B962
#include "client/data_paths.h"
#include <cctype>
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <mutex>

#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif

namespace EQT {
namespace Graphics {

// ============================================================================
// Fire Glow: icosphere mesh + shader + rendering
// ============================================================================

#ifdef EQT_HAS_DRM

// GLES2 spec constants — fallback defines for older headers
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB 0x80C8
#endif
#ifndef GL_DEPTH_WRITEMASK
#define GL_DEPTH_WRITEMASK 0x0B72
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif

void Eqe1ArchiveComponent::buildIcosphereMesh() {
    // Build subdivision 2 icosphere (312 verts, 320 tris) on unit sphere
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    float icoV[][3] = {
        {-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},
        {0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},
        {t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1},
    };
    for (int i = 0; i < 12; i++) {
        float l = sqrtf(icoV[i][0]*icoV[i][0]+icoV[i][1]*icoV[i][1]+icoV[i][2]*icoV[i][2]);
        icoV[i][0]/=l; icoV[i][1]/=l; icoV[i][2]/=l;
    }
    int icoI[] = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };

    struct Tri { int i0, i1, i2; };
    std::vector<float> pts;
    for (int i = 0; i < 12; i++) { pts.push_back(icoV[i][0]); pts.push_back(icoV[i][1]); pts.push_back(icoV[i][2]); }
    std::vector<Tri> tris;
    for (int i = 0; i < 20; i++) tris.push_back({icoI[i*3], icoI[i*3+1], icoI[i*3+2]});

    // Subdivide 2 times
    for (int s = 0; s < 2; s++) {
        std::vector<Tri> newTris;
        for (const auto& tri : tris) {
            auto mid = [&](int a, int b) -> int {
                int idx = static_cast<int>(pts.size()) / 3;
                float mx=(pts[a*3]+pts[b*3])*0.5f, my=(pts[a*3+1]+pts[b*3+1])*0.5f, mz=(pts[a*3+2]+pts[b*3+2])*0.5f;
                float l=sqrtf(mx*mx+my*my+mz*mz);
                pts.push_back(mx/l); pts.push_back(my/l); pts.push_back(mz/l);
                return idx;
            };
            int m01=mid(tri.i0,tri.i1), m12=mid(tri.i1,tri.i2), m20=mid(tri.i2,tri.i0);
            newTris.push_back({tri.i0,m01,m20}); newTris.push_back({tri.i1,m12,m01});
            newTris.push_back({tri.i2,m20,m12}); newTris.push_back({m01,m12,m20});
        }
        tris = newTris;
    }

    // Store vertices (position only — normal == position for unit sphere)
    icosphereVertices_.clear();
    icosphereVertices_ = std::move(pts);

    icosphereIndices_.clear();
    for (const auto& tri : tris) {
        icosphereIndices_.push_back(static_cast<uint16_t>(tri.i0));
        icosphereIndices_.push_back(static_cast<uint16_t>(tri.i1));
        icosphereIndices_.push_back(static_cast<uint16_t>(tri.i2));
    }

    // Upload to GPU
    eqt::memory::GemTag gemTag("vbo:fire-icosphere");
    glGenBuffers(1, &icosphereVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, icosphereVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 icosphereVertices_.size() * sizeof(float),
                 icosphereVertices_.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &icosphereIBO_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, icosphereIBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 icosphereIndices_.size() * sizeof(uint16_t),
                 icosphereIndices_.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    LOG_INFO(MOD_GRAPHICS, "Built icosphere mesh: {} verts, {} tris, VBO={}, IBO={}",
             static_cast<int>(icosphereVertices_.size()) / 3,
             static_cast<int>(icosphereIndices_.size()) / 3,
             icosphereVBO_, icosphereIBO_);
}

void Eqe1ArchiveComponent::compileIcosphereShader() {
    // Vertex shader: scale pulse + vertex jitter, fog distance
    static const char* VS_SRC = R"(
precision highp float;
attribute vec3 aPosition;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;
uniform float uTime;
uniform float uJitterAmount;
uniform vec4 uColor;
uniform float uFogStart;
uniform float uFogEnd;

varying vec4 vColor;
varying float vFogFactor;

void main() {
    // Per-vertex jitter using position components as pseudo-random phase
    float phase = aPosition.x * 7.13 + aPosition.y * 11.37 + aPosition.z * 5.79;
    float jitter = sin(uTime * 4.0 + phase) * uJitterAmount;

    vec3 worldPos = uCenter + aPosition * uRadius * (1.0 + jitter);
    vec4 clipPos = uViewProj * vec4(worldPos, 1.0);
    gl_Position = clipPos;
    vColor = uColor;

    float fogDist = length(clipPos.xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

    // Fragment shader: trivial output with fog
    static const char* FS_SRC = R"(
precision mediump float;
uniform vec4 uFogColor;
varying vec4 vColor;
varying float vFogFactor;
void main() {
    gl_FragColor = vColor * vFogFactor;
}
)";

    // Compile VS
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &VS_SRC, nullptr);
    glCompileShader(vs);
    GLint ok = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
        LOG_ERROR(MOD_GRAPHICS, "Icosphere VS compile error: {}", log);
        glDeleteShader(vs);
        return;
    }

    // Compile FS
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &FS_SRC, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
        LOG_ERROR(MOD_GRAPHICS, "Icosphere FS compile error: {}", log);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }

    // Link
    ctx_.icosphereProgram = glCreateProgram();
    glAttachShader(ctx_.icosphereProgram, vs);
    glAttachShader(ctx_.icosphereProgram, fs);
    glBindAttribLocation(ctx_.icosphereProgram, 0, "aPosition");
    glLinkProgram(ctx_.icosphereProgram);
    glGetProgramiv(ctx_.icosphereProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(ctx_.icosphereProgram, sizeof(log), nullptr, log);
        LOG_ERROR(MOD_GRAPHICS, "Icosphere program link error: {}", log);
        glDeleteProgram(ctx_.icosphereProgram);
        ctx_.icosphereProgram = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }
    glDetachShader(ctx_.icosphereProgram, vs);
    glDetachShader(ctx_.icosphereProgram, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // Cache uniform locations
    icoLocViewProj_ = glGetUniformLocation(ctx_.icosphereProgram, "uViewProj");
    icoLocCenter_ = glGetUniformLocation(ctx_.icosphereProgram, "uCenter");
    icoLocRadius_ = glGetUniformLocation(ctx_.icosphereProgram, "uRadius");
    icoLocTime_ = glGetUniformLocation(ctx_.icosphereProgram, "uTime");
    icoLocJitterAmt_ = glGetUniformLocation(ctx_.icosphereProgram, "uJitterAmount");
    icoLocColor_ = glGetUniformLocation(ctx_.icosphereProgram, "uColor");
    icoLocFogStart_ = glGetUniformLocation(ctx_.icosphereProgram, "uFogStart");
    icoLocFogEnd_ = glGetUniformLocation(ctx_.icosphereProgram, "uFogEnd");
    icoLocFogColor_ = glGetUniformLocation(ctx_.icosphereProgram, "uFogColor");

    LOG_INFO(MOD_GRAPHICS, "Compiled icosphere shader program={}", ctx_.icosphereProgram);
}

void Eqe1ArchiveComponent::updateFireGlowLighting() {
    if (!ctx_.zoneShader || ctx_.fireGlowLights.empty()) return;

    // Get camera position in Irrlicht coords (Y-up)
    irr::core::vector3df camPos = ctx_.camera ? ctx_.camera->getAbsolutePosition()
                                          : irr::core::vector3df(0, 0, 0);

    // Sort emitters by distance to camera, select nearest N
    int maxN = std::min(ctx_.maxFireGlowLights,
                        static_cast<int>(ctx_.fireGlowLights.size()));
    maxN = std::min(maxN, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);

    // Build index + distance array for partial sort
    struct EmitterDist { int index; float distSq; };
    std::vector<EmitterDist> dists(ctx_.fireGlowLights.size());
    for (size_t i = 0; i < ctx_.fireGlowLights.size(); ++i) {
        float dx = ctx_.fireGlowLights[i].position.X - camPos.X;
        float dy = ctx_.fireGlowLights[i].position.Y - camPos.Y;
        float dz = ctx_.fireGlowLights[i].position.Z - camPos.Z;
        dists[i] = { static_cast<int>(i), dx*dx + dy*dy + dz*dz };
    }
    std::partial_sort(dists.begin(), dists.begin() + maxN, dists.end(),
                      [](const EmitterDist& a, const EmitterDist& b) { return a.distSq < b.distSq; });

    ctx_.zoneShader->clearFireGlowLights();
    ctx_.zoneShader->setNumFireGlowLights(maxN);

    for (int i = 0; i < maxN; ++i) {
        const auto& e = ctx_.fireGlowLights[dists[i].index];
        // Fire glow light uniforms are in Irrlicht coords (Y-up) — matches zone VS worldPos
        ctx_.zoneShader->setFireGlowLight(i,
            e.position.X, e.position.Y, e.position.Z,
            e.r, e.g, e.b,
            e.radius, 1.0f);
    }
}

void Eqe1ArchiveComponent::renderFireGlowIcospheres() {
    if (icosphereVBO_ == 0 || ctx_.icosphereProgram == 0 || ctx_.fireGlowLights.empty()) return;

    // Get camera position for nearest-N selection (reuse fire glow light selection)
    irr::core::vector3df camPos = ctx_.camera ? ctx_.camera->getAbsolutePosition()
                                          : irr::core::vector3df(0, 0, 0);

    int maxN = std::min(ctx_.maxFireGlowLights,
                        static_cast<int>(ctx_.fireGlowLights.size()));

    // Build distance-sorted indices
    struct EmitterDist { int index; float distSq; };
    std::vector<EmitterDist> dists(ctx_.fireGlowLights.size());
    for (size_t i = 0; i < ctx_.fireGlowLights.size(); ++i) {
        float dx = ctx_.fireGlowLights[i].position.X - camPos.X;
        float dy = ctx_.fireGlowLights[i].position.Y - camPos.Y;
        float dz = ctx_.fireGlowLights[i].position.Z - camPos.Z;
        dists[i] = { static_cast<int>(i), dx*dx + dy*dy + dz*dz };
    }
    std::partial_sort(dists.begin(), dists.begin() + maxN, dists.end(),
                      [](const EmitterDist& a, const EmitterDist& b) { return a.distSq < b.distSq; });

    // Build ViewProj matrix from captured 3D transforms
    irr::core::matrix4 viewProj = ctx_.captured3DProj * ctx_.captured3DView;
    float vpMat[16];
    for (int i = 0; i < 16; i++) vpMat[i] = viewProj[i];

    // Save GL state
    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLint prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
    GLboolean prevDepthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthWrite);
    GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    // Set render state: additive blend, depth test ON, depth write OFF, no cull
    glUseProgram(ctx_.icosphereProgram);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    // Set shared uniforms
    glUniformMatrix4fv(icoLocViewProj_, 1, GL_FALSE, vpMat);
    glUniform1f(icoLocTime_, ctx_.Frame().fireGlowTime());
    glUniform1f(icoLocJitterAmt_, 0.08f);

    float fogStart = ctx_.zoneShader ? ctx_.zoneShader->fogStart() : 999999.0f;
    float fogEnd = ctx_.zoneShader ? ctx_.zoneShader->fogEnd() : 999999.0f;
    const float* fogCol = ctx_.zoneShader ? ctx_.zoneShader->fogColor() : nullptr;
    glUniform1f(icoLocFogStart_, fogStart);
    glUniform1f(icoLocFogEnd_, fogEnd);
    if (fogCol) glUniform4f(icoLocFogColor_, fogCol[0], fogCol[1], fogCol[2], fogCol[3]);
    else glUniform4f(icoLocFogColor_, 0.0f, 0.0f, 0.0f, 0.0f);

    // Bind icosphere VBO/IBO
    glBindBuffer(GL_ARRAY_BUFFER, icosphereVBO_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, icosphereIBO_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    // Disable other attribs that zone shaders may have left enabled
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);

    int indexCount = static_cast<int>(icosphereIndices_.size());

    // xorshift RNG for per-emitter color flicker (deterministic per frame from time)
    uint32_t flickerRng = static_cast<uint32_t>(ctx_.Frame().fireGlowTime() * 1000.0f) ^ 0xDEADBEEF;

    for (int i = 0; i < maxN; ++i) {
        const auto& e = ctx_.fireGlowLights[dists[i].index];

        // Color flicker: random intensity variation per light per frame
        flickerRng ^= flickerRng << 13; flickerRng ^= flickerRng >> 17; flickerRng ^= flickerRng << 5;
        float flickerIntensity = 0.6f + 0.4f * static_cast<float>(flickerRng & 0xFFFF) / 65535.0f;

        float alpha = 0.25f * flickerIntensity;  // subtle additive glow

        glUniform3f(icoLocCenter_, e.position.X, e.position.Y, e.position.Z);
        glUniform1f(icoLocRadius_, e.radius);
        glUniform4f(icoLocColor_, e.r * alpha, e.g * alpha, e.b * alpha, alpha);

        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
    }

    // Restore GL state
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDepthMask(prevDepthWrite);
    if (prevCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(prevBlendSrc, prevBlendDst);
    glUseProgram(prevProgram);
}

#endif // EQT_HAS_DRM

// ---------------------------------------------------------------------------
// EQE1 flat pack lazy-open (thread-safe)
// ---------------------------------------------------------------------------

const EQE1Loader* Eqe1ArchiveComponent::getOrOpenEQE1(const std::string& archiveBaseName) {
    if (ctx_.config.constrainedConfig.eqe1Path.empty()) return nullptr;
    return getOrOpenEQE1FromDir(archiveBaseName, ctx_.config.constrainedConfig.eqe1Path);
}

const EQE1Loader* Eqe1ArchiveComponent::getOrOpenEQE1FromDir(const std::string& archiveBaseName,
                                                         const std::string& dir) {
    std::lock_guard<std::mutex> lock(owner_.eqe1Mutex_);

    // Already opened?
    auto it = ctx_.eqe1Loaders.find(archiveBaseName);
    if (it != ctx_.eqe1Loaders.end()) {
        return it->second.get();  // may be nullptr if previously failed
    }

    // Try to open
    std::string path = dir + "/" + archiveBaseName + ".eqe1";
    auto loader = std::make_unique<EQE1Loader>();
    if (loader->open(path)) {
        const EQE1Loader* ptr = loader.get();
        ctx_.eqe1Loaders[archiveBaseName] = std::move(loader);

        // Register with cache (cache mutex is separate from eqe1Mutex_ — no deadlock)
        if (ctx_.constrainedTextureCache) {
            ctx_.constrainedTextureCache->addEQE1Loader(ptr);
        }

        return ptr;
    }

    // Store nullptr to prevent repeated open attempts
    ctx_.eqe1Loaders[archiveBaseName] = nullptr;
    LOG_DEBUG(MOD_GRAPHICS, "EQE1: {} not found, S3D fallback", path);
    return nullptr;
}

const EQE1Loader* Eqe1ArchiveComponent::openCompositeSheetEQE1(const std::string& name) {
    // T284: the only composite case the name index cannot answer -- a
    // --composite-entities directory that is NOT the indexed --eqe1-path.
    //
    // data/eqe1_texture_index.json is built over the eqe1 tree, so packs that
    // live only in a separate composite directory are absent from it and
    // resolve() answers "no archive has this" -- the branch decision #2 turns
    // into ConstrainedTextureCache's exit(1). Before T284 the step-0 directory
    // scan hid that by opening the whole composite directory; with the scan
    // gone this is what keeps a split layout working.
    //
    // No scan and no index is needed for it: a composite sheet pack is
    // SELF-NAMED -- the pack stem IS its single entry name
    // (<model>_hs_def_<gender>_<variant>, <model>_actordef_0_<variant>; T281
    // verified all 498 shipped sheet packs have this shape) -- so the
    // requested name names its pack directly. At most one open, and none when
    // the file is not there.
    if (!ctx_.config.constrainedConfig.enableCompositeEntities) return nullptr;
    const std::string& dir = ctx_.config.constrainedConfig.compositeEntityPath;
    if (dir.empty() || dir == ctx_.config.constrainedConfig.eqe1Path) return nullptr;
    // Existence-check first so a name that is simply absent from the tree (the
    // decision-#2 asset gap) does not leave a negative entry in eqe1Loaders_
    // under a stem some later archive lookup might legitimately want.
    std::error_code ec;
    if (!std::filesystem::exists(dir + "/" + name + ".eqe1", ec)) return nullptr;
    return getOrOpenEQE1FromDir(name, dir);
}

const EQE1Loader* Eqe1ArchiveComponent::openArchiveContaining(const std::string& name) {
    // T207 / T266. Three rules, in order (EQE1TextureIndex::resolve):
    //   1. A name absent from the whole tree opens NOTHING. That is the case
    //      the old probe handled worst -- races 415/457 resolve to no archive,
    //      and each one used to drain the entire configured list. It stays
    //      the all-or-nothing FATAL (decision #2): a real asset gap.
    //   2. Among the archives that hold the name, a candidate wins, in
    //      configured order. This is the fast path and serves every name the
    //      load-time spawn list (T199) and the post-load race extension
    //      (queueEntityPrepRequests) already accounted for.
    //   3. A name in the tree but in no candidate opens the first owner in
    //      index order (rule R2, T214: a lookup needed it and nothing open
    //      could serve it). Before T266 this case returned nullptr and the
    //      all-or-nothing rule exited the client -- T263 hit it with a GM
    //      spawn of race 195 in qeynos2 (bwdhe0005 in bwd_chr, 9 owners,
    //      none queued), and the same happens for any roamer/pet/summon of a
    //      race absent at zone load. The archive is appended to the candidate
    //      set so its sibling names take path 2 from then on.
    if (!eqe1Index_.isLoaded()) return openCompositeSheetEQE1(name);

    std::vector<std::string> candidates;
    size_t loadTimeCount = 0;
    {
        std::lock_guard<std::mutex> lock(owner_.eqe1CandidatesMutex_);
        candidates = ctx_.pendingGlobalEQE1Archives;
        loadTimeCount = ctx_.eqe1LoadTimeCandidateCount;
    }

    EQE1TextureIndex::Resolution r;
    if (!eqe1Index_.resolve(name, candidates, r)) return openCompositeSheetEQE1(name);

    bool late = r.lateOpen;
    if (!late) {
        // A candidate appended after load (a race that spawned later) is a
        // late-open too: the load-time set did not name it.
        const auto pos = std::find(candidates.begin(), candidates.end(), r.archive);
        late = pos != candidates.end() &&
               static_cast<size_t>(pos - candidates.begin()) >= loadTimeCount;
    }

    const bool alreadyOpen = [&] {
        std::lock_guard<std::mutex> lock(owner_.eqe1Mutex_);
        auto it = ctx_.eqe1Loaders.find(r.archive);
        return it != ctx_.eqe1Loaders.end() && it->second != nullptr;
    }();

    const EQE1Loader* loader = getOrOpenEQE1(r.archive);
    if (!loader) return nullptr;

    if (r.lateOpen) {
        std::lock_guard<std::mutex> lock(owner_.eqe1CandidatesMutex_);
        if (std::find(ctx_.pendingGlobalEQE1Archives.begin(), ctx_.pendingGlobalEQE1Archives.end(),
                      r.archive) == ctx_.pendingGlobalEQE1Archives.end()) {
            ctx_.pendingGlobalEQE1Archives.push_back(r.archive);
        }
    }
    if (late && !alreadyOpen) {
        LOG_INFO(MOD_GRAPHICS, "EQE1: late-open '{}' for '{}' ({} owner(s); {}) "
                 "in zone '{}' (rule R2)",
                 r.archive, name, r.ownerCount,
                 r.lateOpen ? "none in the candidate set" : "candidate added after zone load",
                 ctx_.currentZoneName);
        if (ctx_.constrainedTextureCache) ctx_.constrainedTextureCache->noteEQE1LateOpen(r.archive);
    }
    return loader;
}

bool Eqe1ArchiveComponent::addSpawnedRaceEQE1Candidate(uint16_t raceId) {
    std::lock_guard<std::mutex> lock(owner_.eqe1CandidatesMutex_);
    if (!ctx_.eqe1CandidateRaces.insert(raceId).second) return false;
    std::string s3d = EQT::Graphics::getRaceS3DFile(raceId);
    if (s3d.empty()) return false;
    // race_models stores "akanon_chr.s3d"; archives are keyed by stem.
    const size_t dot = s3d.rfind(".s3d");
    if (dot != std::string::npos) s3d.resize(dot);
    std::transform(s3d.begin(), s3d.end(), s3d.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (s3d.empty()) return false;
    // Already opened as a zone archive, or already queued.
    if (std::find(ctx_.pendingGlobalEQE1Archives.begin(), ctx_.pendingGlobalEQE1Archives.end(), s3d)
        != ctx_.pendingGlobalEQE1Archives.end()) {
        return false;
    }
    ctx_.pendingGlobalEQE1Archives.push_back(s3d);
    return true;
}

void Eqe1ArchiveComponent::logEQE1ArchiveUsage() const {
    std::lock_guard<std::mutex> lock(owner_.eqe1Mutex_);

    std::vector<std::pair<std::string, const EQE1Loader*>> open;
    for (const auto& [name, loader] : ctx_.eqe1Loaders) {
        if (loader && loader->isOpen()) open.emplace_back(name, loader.get());
    }
    if (open.empty()) return;

    // Unused first, then by ascending hits -- the waste is what we came to see.
    std::sort(open.begin(), open.end(), [](const auto& a, const auto& b) {
        if (a.second->getLookupHits() != b.second->getLookupHits())
            return a.second->getLookupHits() < b.second->getLookupHits();
        return a.first < b.first;
    });

    // T214: an archive is legitimately open for exactly TWO reasons --
    //   R1. it is the CURRENT ZONE'S OWN archive ({zone}, {zone}_obj, {zone}_chr)
    //   R2. a lookup needed it and nothing already open could serve it
    // Only an R2 archive can be WASTE, and only by serving nothing. A zone-own
    // archive with zero hits is NORMAL -- it just means this zone's NPCs used
    // global_chr models rather than zone-specific skins -- and lumping it into
    // a "wasted opens" percentage manufactures a defect that does not exist.
    // This reporter did exactly that and it cost a session: befallen was
    // reported as "16% wasted opens, 4065KB mapped for nothing" when the true
    // answer was zero waste. Classify before counting.
    auto isZoneOwn = [this](const std::string& n) {
        return n == ctx_.currentZoneName || n == ctx_.currentZoneName + "_obj" ||
               n == ctx_.currentZoneName + "_chr";
    };

    size_t wasted = 0, wastedBytes = 0, totalBytes = 0, totalHits = 0;
    size_t zoneOwnIdle = 0;
    for (const auto& [name, loader] : open) {
        const size_t bytes = loader->getMappedBytes();
        const uint32_t hits = loader->getLookupHits();
        totalBytes += bytes;
        totalHits  += hits;

        const char* note = "";
        if (hits == 0) {
            if (isZoneOwn(name)) {
                ++zoneOwnIdle;
                note = "   (zone-own, no lookups -- not waste)";
            } else if (Eqe1ArchiveComponent::isPersistentEQE1Archive(name)) {
                note = "   (client pack, no lookups -- not waste)";
            } else {
                ++wasted;
                wastedBytes += bytes;
                note = "   <-- WASTED OPEN";
            }
        }
        LOG_INFO(MOD_GRAPHICS, "EQE1-USAGE:   {:<20} hits={:<6} entries={:<5} mapped={}KB{}",
                 name, hits, loader->getEntryCount(), bytes / 1024, note);
    }

    LOG_INFO(MOD_GRAPHICS,
             "EQE1-USAGE: zone '{}': {} archive(s) open, {} wasted open(s) "
             "({}% , {}KB of {}KB mapped for nothing), {} zone-own archive(s) "
             "idle (expected), {} lookups served total",
             ctx_.currentZoneName, open.size(), wasted,
             open.empty() ? 0 : (wasted * 100 / open.size()),
             wastedBytes / 1024, totalBytes / 1024, zoneOwnIdle, totalHits);
}

size_t Eqe1ArchiveComponent::getEQE1MappedBytesTotal() const {
    std::lock_guard<std::mutex> lock(owner_.eqe1Mutex_);
    size_t total = 0;
    for (const auto& [name, loader] : ctx_.eqe1Loaders) {
        if (loader) total += loader->getMappedBytes();
    }
    return total;
}

// ---------------------------------------------------------------------------
// EQE1 archive config (JSON-driven archive list)
// ---------------------------------------------------------------------------

bool Eqe1ArchiveComponent::loadEQE1ArchiveConfig(const std::string& path) {
    // T399 (board B235): resolved; the miss is an ERROR because
    // config/eqe1_archives.json is shipped and the built-in list below is a
    // DIFFERENT archive set, not a degraded one.
    const std::string resolved = eqt::ResolveDataPath(path);
    std::ifstream file(resolved);
    if (!file.is_open()) {
        LOG_ERROR(MOD_GRAPHICS,
                  "cannot open '{}' (resolved to '{}') -- falling back to the BUILT-IN "
                  "EQE1 archive list, which is not the shipped one", path, resolved);

        // Built-in fallback (matches original hard-coded list)
        EQE1ArchiveEntry defaultEntry;
        defaultEntry.zoneArchives = {"{zone}", "{zone}_obj", "{zone}_chr"};
        defaultEntry.globalArchives = {
            "global_chr", "global2_chr", "global3_chr", "global4_chr",
            "global5_chr", "global6_chr", "global7_chr",
            "gequip", "gequip2", "gequip3", "gequip4",
            "gequip5", "gequip6", "gequip8",  // T223: no gequip7.s3d exists
            "gatesequip", "ldonequip", "lgequip", "lgequip2",
            "lgequip_amr", "lgequip_amr2",
            "loyequip", "vequip", "frogequip", "obequip_lexit",
            "global17_amr", "global18_amr", "global19_amr", "global20_amr",
            "global21_amr", "global22_amr", "global23_amr",
            "sky"
        };
        eqe1ArchiveEntries_["default"] = std::move(defaultEntry);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to parse {}: {}", path, errors);
        return false;
    }

    for (const auto& key : root.getMemberNames()) {
        const Json::Value& entry = root[key];
        EQE1ArchiveEntry archiveEntry;

        if (entry.isMember("zoneArchives")) {
            for (const auto& v : entry["zoneArchives"]) {
                archiveEntry.zoneArchives.push_back(v.asString());
            }
        }
        if (entry.isMember("globalArchives")) {
            for (const auto& v : entry["globalArchives"]) {
                archiveEntry.globalArchives.push_back(v.asString());
            }
        }

        eqe1ArchiveEntries_[key] = std::move(archiveEntry);
    }

    LOG_INFO(MOD_GRAPHICS, "Loaded EQE1 archive config: {} entries ({} zone-specific)",
             eqe1ArchiveEntries_.size(),
             eqe1ArchiveEntries_.size() - (eqe1ArchiveEntries_.count("default") ? 1 : 0));
    return true;
}

Eqe1ArchiveComponent::EQE1ArchiveEntry Eqe1ArchiveComponent::resolveEQE1Archives(const std::string& zoneName) const {
    EQE1ArchiveEntry result;

    // Start with default
    auto defaultIt = eqe1ArchiveEntries_.find("default");
    if (defaultIt != eqe1ArchiveEntries_.end()) {
        result = defaultIt->second;
    }

    // Merge zone-specific entry if present
    auto zoneIt = eqe1ArchiveEntries_.find(zoneName);
    if (zoneIt != eqe1ArchiveEntries_.end()) {
        // Append zone-specific archives, dedup against existing
        auto appendUnique = [](std::vector<std::string>& dest, const std::vector<std::string>& src) {
            for (const auto& item : src) {
                if (std::find(dest.begin(), dest.end(), item) == dest.end()) {
                    dest.push_back(item);
                }
            }
        };
        appendUnique(result.zoneArchives, zoneIt->second.zoneArchives);
        appendUnique(result.globalArchives, zoneIt->second.globalArchives);
    }

    // Replace {zone} placeholder in zone archives
    for (auto& name : result.zoneArchives) {
        size_t pos = name.find("{zone}");
        if (pos != std::string::npos) {
            name.replace(pos, 6, zoneName);
        }
    }

    return result;
}

} // namespace Graphics
} // namespace EQT
