#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/eqe1_loader.h"
#include "common/logging.h"
#include <json/json.h>
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

void IrrlichtRenderer::buildIcosphereMesh() {
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

void IrrlichtRenderer::compileIcosphereShader() {
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
    icosphereProgram_ = glCreateProgram();
    glAttachShader(icosphereProgram_, vs);
    glAttachShader(icosphereProgram_, fs);
    glBindAttribLocation(icosphereProgram_, 0, "aPosition");
    glLinkProgram(icosphereProgram_);
    glGetProgramiv(icosphereProgram_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(icosphereProgram_, sizeof(log), nullptr, log);
        LOG_ERROR(MOD_GRAPHICS, "Icosphere program link error: {}", log);
        glDeleteProgram(icosphereProgram_);
        icosphereProgram_ = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }
    glDetachShader(icosphereProgram_, vs);
    glDetachShader(icosphereProgram_, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // Cache uniform locations
    icoLocViewProj_ = glGetUniformLocation(icosphereProgram_, "uViewProj");
    icoLocCenter_ = glGetUniformLocation(icosphereProgram_, "uCenter");
    icoLocRadius_ = glGetUniformLocation(icosphereProgram_, "uRadius");
    icoLocTime_ = glGetUniformLocation(icosphereProgram_, "uTime");
    icoLocJitterAmt_ = glGetUniformLocation(icosphereProgram_, "uJitterAmount");
    icoLocColor_ = glGetUniformLocation(icosphereProgram_, "uColor");
    icoLocFogStart_ = glGetUniformLocation(icosphereProgram_, "uFogStart");
    icoLocFogEnd_ = glGetUniformLocation(icosphereProgram_, "uFogEnd");
    icoLocFogColor_ = glGetUniformLocation(icosphereProgram_, "uFogColor");

    LOG_INFO(MOD_GRAPHICS, "Compiled icosphere shader program={}", icosphereProgram_);
}

void IrrlichtRenderer::updateFireGlowLighting() {
    if (!zoneShader_ || fireGlowLights_.empty()) return;

    // Get camera position in Irrlicht coords (Y-up)
    irr::core::vector3df camPos = camera_ ? camera_->getAbsolutePosition()
                                          : irr::core::vector3df(0, 0, 0);

    // Sort emitters by distance to camera, select nearest N
    int maxN = std::min(maxFireGlowLights_,
                        static_cast<int>(fireGlowLights_.size()));
    maxN = std::min(maxN, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);

    // Build index + distance array for partial sort
    struct EmitterDist { int index; float distSq; };
    std::vector<EmitterDist> dists(fireGlowLights_.size());
    for (size_t i = 0; i < fireGlowLights_.size(); ++i) {
        float dx = fireGlowLights_[i].position.X - camPos.X;
        float dy = fireGlowLights_[i].position.Y - camPos.Y;
        float dz = fireGlowLights_[i].position.Z - camPos.Z;
        dists[i] = { static_cast<int>(i), dx*dx + dy*dy + dz*dz };
    }
    std::partial_sort(dists.begin(), dists.begin() + maxN, dists.end(),
                      [](const EmitterDist& a, const EmitterDist& b) { return a.distSq < b.distSq; });

    zoneShader_->clearFireGlowLights();
    zoneShader_->setNumFireGlowLights(maxN);

    for (int i = 0; i < maxN; ++i) {
        const auto& e = fireGlowLights_[dists[i].index];
        // Fire glow light uniforms are in Irrlicht coords (Y-up) — matches zone VS worldPos
        zoneShader_->setFireGlowLight(i,
            e.position.X, e.position.Y, e.position.Z,
            e.r, e.g, e.b,
            e.radius, 1.0f);
    }
}

void IrrlichtRenderer::renderFireGlowIcospheres() {
    if (icosphereVBO_ == 0 || icosphereProgram_ == 0 || fireGlowLights_.empty()) return;

    // Get camera position for nearest-N selection (reuse fire glow light selection)
    irr::core::vector3df camPos = camera_ ? camera_->getAbsolutePosition()
                                          : irr::core::vector3df(0, 0, 0);

    int maxN = std::min(maxFireGlowLights_,
                        static_cast<int>(fireGlowLights_.size()));

    // Build distance-sorted indices
    struct EmitterDist { int index; float distSq; };
    std::vector<EmitterDist> dists(fireGlowLights_.size());
    for (size_t i = 0; i < fireGlowLights_.size(); ++i) {
        float dx = fireGlowLights_[i].position.X - camPos.X;
        float dy = fireGlowLights_[i].position.Y - camPos.Y;
        float dz = fireGlowLights_[i].position.Z - camPos.Z;
        dists[i] = { static_cast<int>(i), dx*dx + dy*dy + dz*dz };
    }
    std::partial_sort(dists.begin(), dists.begin() + maxN, dists.end(),
                      [](const EmitterDist& a, const EmitterDist& b) { return a.distSq < b.distSq; });

    // Build ViewProj matrix from captured 3D transforms
    irr::core::matrix4 viewProj = captured3DProj_ * captured3DView_;
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
    glUseProgram(icosphereProgram_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    // Set shared uniforms
    glUniformMatrix4fv(icoLocViewProj_, 1, GL_FALSE, vpMat);
    glUniform1f(icoLocTime_, fireGlowTime_);
    glUniform1f(icoLocJitterAmt_, 0.08f);

    float fogStart = zoneShader_ ? zoneShader_->fogStart() : 999999.0f;
    float fogEnd = zoneShader_ ? zoneShader_->fogEnd() : 999999.0f;
    const float* fogCol = zoneShader_ ? zoneShader_->fogColor() : nullptr;
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
    uint32_t flickerRng = static_cast<uint32_t>(fireGlowTime_ * 1000.0f) ^ 0xDEADBEEF;

    for (int i = 0; i < maxN; ++i) {
        const auto& e = fireGlowLights_[dists[i].index];

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

const EQE1Loader* IrrlichtRenderer::getOrOpenEQE1(const std::string& archiveBaseName) {
    if (config_.constrainedConfig.eqe1Path.empty()) return nullptr;

    std::lock_guard<std::mutex> lock(eqe1Mutex_);

    // Already opened?
    auto it = eqe1Loaders_.find(archiveBaseName);
    if (it != eqe1Loaders_.end()) {
        return it->second.get();  // may be nullptr if previously failed
    }

    // Try to open
    std::string path = config_.constrainedConfig.eqe1Path + "/" + archiveBaseName + ".eqe1";
    auto loader = std::make_unique<EQE1Loader>();
    if (loader->open(path)) {
        const EQE1Loader* ptr = loader.get();
        eqe1Loaders_[archiveBaseName] = std::move(loader);

        // Register with cache (cache mutex is separate from eqe1Mutex_ — no deadlock)
        if (constrainedTextureCache_) {
            constrainedTextureCache_->addEQE1Loader(ptr);
        }

        return ptr;
    }

    // Store nullptr to prevent repeated open attempts
    eqe1Loaders_[archiveBaseName] = nullptr;
    LOG_DEBUG(MOD_GRAPHICS, "EQE1: {} not found, S3D fallback", path);
    return nullptr;
}

const EQE1Loader* IrrlichtRenderer::tryOpenNextGlobalEQE1() {
    while (nextGlobalEQE1Idx_ < pendingGlobalEQE1Archives_.size()) {
        const std::string name = pendingGlobalEQE1Archives_[nextGlobalEQE1Idx_++];
        const EQE1Loader* loader = getOrOpenEQE1(name);
        if (loader) {
            return loader;
        }
        // getOrOpenEQE1 returned nullptr (file missing/bad) — try the next
        // global archive rather than giving up the whole lazy-open chain.
    }
    return nullptr;
}

size_t IrrlichtRenderer::getEQE1MappedBytesTotal() const {
    std::lock_guard<std::mutex> lock(eqe1Mutex_);
    size_t total = 0;
    for (const auto& [name, loader] : eqe1Loaders_) {
        if (loader) total += loader->getMappedBytes();
    }
    return total;
}

// ---------------------------------------------------------------------------
// EQE1 archive config (JSON-driven archive list)
// ---------------------------------------------------------------------------

bool IrrlichtRenderer::loadEQE1ArchiveConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_INFO(MOD_GRAPHICS, "No {} found, using built-in EQE1 archive defaults", path);

        // Built-in fallback (matches original hard-coded list)
        EQE1ArchiveEntry defaultEntry;
        defaultEntry.zoneArchives = {"{zone}", "{zone}_obj", "{zone}_chr"};
        defaultEntry.globalArchives = {
            "global_chr", "global2_chr", "global3_chr", "global4_chr",
            "global5_chr", "global6_chr", "global7_chr",
            "gequip", "gequip2", "gequip3", "gequip4",
            "gequip5", "gequip6", "gequip7", "gequip8",
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

IrrlichtRenderer::EQE1ArchiveEntry IrrlichtRenderer::resolveEQE1Archives(const std::string& zoneName) const {
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
