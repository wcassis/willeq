// zone_bench_impostor.cpp -- B952 (GitLab #270): the benchmark's impostor
// textures, their memory row and their timing. See zone_bench_impostor.h.

#include "zone_bench_impostor.h"

#include "zone_bench_loader.h"
#include "client/core/gpu_buffer_census.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "client/graphics/eq/animated_mesh_scene_node.h"

#ifdef EQT_HAS_GLES2
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
// The driver's own bridges (docker/irrlicht-drm/COpenGLES2Driver.cpp): wrap a
// GL texture name as an ITexture the material system can bind, and the
// driver's texture-byte tally (the same one zone_bench_memory.cpp reports).
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                              unsigned int width, unsigned int height, unsigned int gpuBytes);
extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
#endif

#ifdef __linux__
#include <dirent.h>
#include <unistd.h>
#endif

namespace ZoneBench {

namespace {

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void finishGpu() {
#ifdef EQT_HAS_GLES2
    glFinish();
#endif
}

// System-wide shared memory (kB): where a GEM/shmem-backed GPU buffer that is
// not mapped into this process is visible at all.
int64_t readSysShmemKb() {
#ifdef __linux__
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    int64_t kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Shmem:", 6) == 0) {
            kb = strtoll(line + 6, nullptr, 10);
            break;
        }
    }
    fclose(f);
    return kb;
#else
    return 0;
#endif
}

// The drm-* memory keys (drm-total-*, drm-resident-*, drm-memory-*, ...) of
// every /dev/dri fd this process holds, in bytes. Empty where the kernel
// driver does not publish them (the DRM fdinfo memory stats are per-driver).
std::map<std::string, int64_t> readDrmFdinfo() {
    std::map<std::string, int64_t> out;
#ifdef __linux__
    DIR* dir = opendir("/proc/self/fd");
    if (!dir) return out;
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        char linkPath[320], target[256];
        snprintf(linkPath, sizeof(linkPath), "/proc/self/fd/%s", de->d_name);
        ssize_t n = readlink(linkPath, target, sizeof(target) - 1);
        if (n <= 0) continue;
        target[n] = '\0';
        if (strncmp(target, "/dev/dri/", 9) != 0) continue;
        if (!eqt::memory::drmFdinfoIsSafe(target)) continue;  // B1056: pvrsrvkm panics on this read
        char infoPath[320];
        snprintf(infoPath, sizeof(infoPath), "/proc/self/fdinfo/%s", de->d_name);
        FILE* f = fopen(infoPath, "r");
        if (!f) continue;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "drm-", 4) != 0) continue;
            char* colon = strchr(line, ':');
            if (!colon) continue;
            std::string key(line, colon - line);
            // Only the memory keys; drm-engine-* are nanoseconds.
            if (key.find("total") == std::string::npos &&
                key.find("resident") == std::string::npos &&
                key.find("memory") == std::string::npos &&
                key.find("shared") == std::string::npos &&
                key.find("active") == std::string::npos &&
                key.find("purgeable") == std::string::npos)
                continue;
            char* end = nullptr;
            long long v = strtoll(colon + 1, &end, 10);
            while (end && *end == ' ') ++end;
            if (end && strncmp(end, "KiB", 3) == 0) v *= 1024;
            else if (end && strncmp(end, "MiB", 3) == 0) v *= 1024 * 1024;
            out[key] += v;
        }
        fclose(f);
    }
    closedir(dir);
#endif
    return out;
}

struct MemProbe {
    eqt::memory::Snapshot ledger;
    int64_t sysShmemKb = 0;
    int64_t gpuTextureBytes = 0;
    std::map<std::string, int64_t> drm;
};

MemProbe probeMemory(ZoneBenchData& data) {
    MemProbe p;
    p.ledger = eqt::memory::snapshot("impostor_probe");
    p.sysShmemKb = readSysShmemKb();
#ifdef EQT_HAS_GLES2
    p.gpuTextureBytes = static_cast<int64_t>(gles2GetGpuTextureMemoryUsage(data.driver));
#else
    (void)data;
#endif
    p.drm = readDrmFdinfo();
    return p;
}

void accumulate(ImpostorMemoryDelta& d, const MemProbe& a, const MemProbe& b) {
    using eqt::memory::Category;
    auto live = [](const eqt::memory::Snapshot& s, Category c) {
        return static_cast<int64_t>(s.categories[static_cast<size_t>(c)].liveBytes);
    };
    d.gpuDriver += live(b.ledger, Category::GpuDriver) - live(a.ledger, Category::GpuDriver);
    d.texturesDriver += live(b.ledger, Category::TexturesDriver) - live(a.ledger, Category::TexturesDriver);
    d.ledgerTotal += static_cast<int64_t>(b.ledger.ledgerTotalBytes) -
                     static_cast<int64_t>(a.ledger.ledgerTotalBytes);
    d.rssAnon += static_cast<int64_t>(b.ledger.rssAnonBytes) - static_cast<int64_t>(a.ledger.rssAnonBytes);
    d.rssShmem += static_cast<int64_t>(b.ledger.rssShmemBytes) - static_cast<int64_t>(a.ledger.rssShmemBytes);
    d.rssFile += static_cast<int64_t>(b.ledger.rssFileBytes) - static_cast<int64_t>(a.ledger.rssFileBytes);
    d.sysShmemKb += b.sysShmemKb - a.sysShmemKb;
    d.gpuTextureBytes += b.gpuTextureBytes - a.gpuTextureBytes;
    for (const auto& [k, v] : b.drm) {
        auto it = a.drm.find(k);
        d.drmFdinfo[k] += v - (it == a.drm.end() ? 0 : it->second);
    }
}

float percentile(std::vector<float> v, double p) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(std::ceil(p * v.size())) ;
    if (idx > 0) --idx;
    if (idx >= v.size()) idx = v.size() - 1;
    return v[idx];
}

const char* modeName(const ImpostorConfig& c) { return c.shared ? "shared" : "unique"; }
const char* fboName(const ImpostorConfig& c) { return c.sharedFbo ? "shared" : "per"; }

}  // namespace

// ---------------------------------------------------------------------------
// ImpostorCache
// ---------------------------------------------------------------------------

uint64_t ImpostorCache::keyFor(const ZoneBenchData& data, int entityIdx) const {
    if (!cfg.shared || entityIdx < 0 || entityIdx >= static_cast<int>(data.entities.size()))
        return static_cast<uint64_t>(entityIdx);
    const auto& e = data.entities[entityIdx];
    return (1ull << 40) | (static_cast<uint64_t>(e.raceId) << 16) |
           (static_cast<uint64_t>(e.gender) << 8) | e.textureVariant;
}

ImpostorEntry* ImpostorCache::find(const ZoneBenchData& data, int entityIdx) {
    auto it = entries_.find(keyFor(data, entityIdx));
    return it == entries_.end() ? nullptr : &it->second;
}

bool ImpostorCache::ensureScratch(ZoneBenchData& data) {
    if (scratch_) return true;
    char name[64];
    snprintf(name, sizeof(name), "impostor_scratch_%d", cfg.size);
    irr::core::dimension2d<irr::u32> dim(cfg.size, cfg.size);
    eqt::memory::MemoryScope scope(eqt::memory::Category::TexturesDriver);
    scratch_ = data.driver->addRenderTargetTexture(dim, name);
    if (!scratch_) return false;
#ifdef EQT_HAS_GLES2
    // COGLES2Texture::createRenderTarget() attaches the colour through
    // GL_EXT_multisampled_render_to_texture when the driver has it (Lima
    // does, llvmpipe does not), with a multisampled DEPTH16. A plain
    // glFramebufferTexture2D swap is then FBO-incomplete (sample mismatch --
    // measured on the Pi, every impostor render), so the swap must use the
    // same entry point and sample count. Read both once, from the scratch.
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (ext && strstr(ext, "GL_EXT_multisampled_render_to_texture")) {
        msAttach_ = reinterpret_cast<void*>(eglGetProcAddress("glFramebufferTexture2DMultisampleEXT"));
        data.driver->setRenderTarget(scratch_, false, false);
        GLint samples = 0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                              GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_SAMPLES_EXT, &samples);
        while (glGetError() != GL_NO_ERROR) {}
        data.driver->setRenderTarget(nullptr, false, false);
        scratchSamples_ = samples;
    }
    printf("[impostor] scratch %dx%d: colour samples=%d (%s)\n", cfg.size, cfg.size, scratchSamples_,
           scratchSamples_ > 0 ? "EXT_multisampled_render_to_texture" : "single-sample");
#endif
    return true;
}

size_t ImpostorCache::textureBytes() const {
    size_t perTex = static_cast<size_t>(cfg.size) * cfg.size * 4;
    size_t perDepth = static_cast<size_t>(cfg.size) * cfg.size * 2;
    size_t total = 0;
    for (const auto& [k, e] : entries_) total += perTex + (e.ownRtt ? perDepth : 0);
    return total;
}

size_t ImpostorCache::scratchBytes() const {
    if (!scratch_) return 0;
    return static_cast<size_t>(cfg.size) * cfg.size * (4 + 2);  // RGBA8 + DEPTH16
}

size_t ImpostorCache::claimantCount() const {
    size_t n = 0;
    for (const auto& [k, e] : entries_) n += static_cast<size_t>(e.claimants);
    return n;
}

ImpostorEntry* ImpostorCache::acquire(ZoneBenchData& data, int entityIdx, const CameraState& cam) {
    const uint64_t key = keyFor(data, entityIdx);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        auto ek = entityKey_.find(entityIdx);
        if (ek == entityKey_.end()) {
            entityKey_[entityIdx] = key;
            it->second.claimants++;
        }
        return &it->second;
    }

    eqt::memory::MemoryScope scope(eqt::memory::Category::TexturesDriver);
    ImpostorEntry e;
    char name[64];
    snprintf(name, sizeof(name), "impostor_%d_%d", cfg.size, serial_++);
    const irr::core::dimension2d<irr::u32> dim(cfg.size, cfg.size);
#ifdef EQT_HAS_GLES2
    if (cfg.sharedFbo && ensureScratch(data)) {
        // A colour texture only; the depth lives once, in the scratch RTT.
        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cfg.size, cfg.size, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        // The driver caches its bindings: put back what it believes is bound.
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
        e.glTex = t;
        e.tex = static_cast<irr::video::ITexture*>(gles2WrapTexture(
            data.driver, name, t, cfg.size, cfg.size, cfg.size * cfg.size * 4));
    }
#endif
    if (!e.tex) {
        e.tex = data.driver->addRenderTargetTexture(dim, name);
        e.ownRtt = true;
        e.glTex = 0;
    }
    if (!e.tex) return nullptr;

    e.sourceEntity = entityIdx;
    e.claimants = 1;
    auto ins = entries_.emplace(key, e);
    entityKey_[entityIdx] = key;
    render(data, ins.first->second, entityIdx, cam);
    stats.created++;
    return &ins.first->second;
}

void ImpostorCache::render(ZoneBenchData& data, ImpostorEntry& e, int entityIdx,
                           const CameraState& cam) {
    if (entityIdx < 0 || entityIdx >= static_cast<int>(data.entities.size())) return;
    const auto& ent = data.entities[entityIdx];
    eqt::memory::MemoryScope scope(eqt::memory::Category::TexturesDriver);

    // Idle pose (first frame of o01), as before B952; the static mesh when the
    // entity has no animated node.
    irr::scene::IMesh* mesh = ent.mesh;
    EQT::Graphics::EQAnimatedMeshSceneNode* node =
        (entityIdx < static_cast<int>(data.animatedNodes.size())) ? data.animatedNodes[entityIdx] : nullptr;
    std::string prevAnim;
    if (node) {
        prevAnim = node->getCurrentAnimation();
        node->playAnimation("o01", true);
        node->forceAnimationUpdate();
        if (node->getInstanceMesh()) mesh = node->getInstanceMesh();
    }
    if (!mesh) return;

    // Model-space bounding box of the posed vertices.
    irr::core::aabbox3df bbox;
    bool init = false;
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        auto* buf = mesh->getMeshBuffer(b);
        if (!buf || buf->getVertexCount() == 0) continue;
        const auto* v = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
        for (irr::u32 i = 0; i < buf->getVertexCount(); ++i) {
            if (!init) { bbox.reset(v[i].Pos); init = true; }
            else bbox.addInternalPoint(v[i].Pos);
        }
    }
    if (!init) return;
    const irr::core::vector3df centerModel = bbox.getCenter();
    const irr::core::vector3df centerWorld = ent.position + centerModel;

    // Look along the horizontal camera->entity direction (Irrlicht: EQ x, z, y).
    irr::core::vector3df d(centerWorld.X - cam.eqX, 0.0f, centerWorld.Z - cam.eqY);
    if (d.getLengthSQ() < 1e-6f) d.set(0.0f, 0.0f, 1.0f);
    d.normalize();
    const irr::core::vector3df right(d.Z, 0.0f, -d.X);  // up x forward

    // Frustum half extents from the posed vertices projected on that basis.
    float maxR = 0.0f, maxY = 0.0f;
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        auto* buf = mesh->getMeshBuffer(b);
        if (!buf || buf->getVertexCount() == 0) continue;
        const auto* v = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
        for (irr::u32 i = 0; i < buf->getVertexCount(); ++i) {
            const irr::core::vector3df p = v[i].Pos - centerModel;
            maxR = std::max(maxR, std::fabs(p.dotProduct(right)));
            maxY = std::max(maxY, std::fabs(p.Y));
        }
    }
    e.halfW = std::max(0.5f, maxR * 1.04f);
    e.halfH = std::max(0.5f, maxY * 1.04f);
    e.centerOffset = centerModel;

    const irr::core::matrix4 savedView = data.driver->getTransform(irr::video::ETS_VIEW);
    const irr::core::matrix4 savedProj = data.driver->getTransform(irr::video::ETS_PROJECTION);

    const irr::video::SColor clear(0, 0, 0, 0);
    if (e.ownRtt) {
        data.driver->setRenderTarget(e.tex, true, true, clear);
    } else {
#ifdef EQT_HAS_GLES2
        // Bind the scratch FBO, swap this impostor's texture in as its colour
        // attachment, then bind again with the clears (the clears must hit the
        // swapped attachment, not the previous one).
        data.driver->setRenderTarget(scratch_, false, false);
        if (scratchSamples_ > 0 && msAttach_) {
            reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC>(msAttach_)(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.glTex, 0, scratchSamples_);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.glTex, 0);
        }
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            if (stats.incompleteFbo++ == 0)
                printf("[impostor] WARNING: scratch FBO incomplete with a swapped colour "
                       "attachment (size %d) -- this impostor stays blank\n", cfg.size);
        }
        data.driver->setRenderTarget(scratch_, true, true, clear);
#endif
    }

    irr::core::matrix4 proj;
    proj.buildProjectionMatrixOrthoLH(e.halfW * 2.0f, e.halfH * 2.0f, 0.1f, 500.0f);
    data.driver->setTransform(irr::video::ETS_PROJECTION, proj);
    irr::core::matrix4 view;
    view.buildCameraLookAtMatrixLH(centerWorld - d * 100.0f, centerWorld,
                                   irr::core::vector3df(0.0f, 1.0f, 0.0f));
    data.driver->setTransform(irr::video::ETS_VIEW, view);
    irr::core::matrix4 world;
    world.setTranslation(ent.position);  // the entity's own world position: same lights
    data.driver->setTransform(irr::video::ETS_WORLD, world);

    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        auto* buf = mesh->getMeshBuffer(b);
        if (!buf || buf->getVertexCount() == 0) continue;
        irr::video::SMaterial mat = buf->getMaterial();
        mat.FogEnable = false;
        mat.BackfaceCulling = false;
        data.driver->setMaterial(mat);
        data.driver->drawMeshBuffer(buf);
    }

    data.driver->setRenderTarget(nullptr, false, false);
    data.driver->setTransform(irr::video::ETS_VIEW, savedView);
    data.driver->setTransform(irr::video::ETS_PROJECTION, savedProj);
    data.driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    if (node && !prevAnim.empty() && prevAnim != "o01") {
        node->playAnimation(prevAnim, true);
        node->forceAnimationUpdate();
    }
}

void ImpostorCache::clear(ZoneBenchData& data) {
    for (auto& [k, e] : entries_) {
        if (e.tex) data.driver->removeTexture(e.tex);
    }
    entries_.clear();
    entityKey_.clear();
    if (scratch_) {
        data.driver->removeTexture(scratch_);
        scratch_ = nullptr;
    }
}

ImpostorCache& impostorCache(ZoneBenchData& data) {
    auto& p = data.adaptiveLodState.impostors;
    if (!p) p = std::make_shared<ImpostorCache>();
    return *p;
}

void configureImpostors(ZoneBenchData& data, const ImpostorConfig& cfg) {
    impostorCache(data).cfg = cfg;
}

void drawImpostorBillboard(ZoneBenchData& data, const ImpostorEntry& imp,
                           const irr::core::vector3df& entityPos, const CameraState& cam) {
    const irr::core::vector3df c = entityPos + imp.centerOffset;
    irr::core::vector3df d(c.X - cam.eqX, 0.0f, c.Z - cam.eqY);
    if (d.getLengthSQ() < 1e-6f) d.set(0.0f, 0.0f, 1.0f);
    d.normalize();
    const irr::core::vector3df r(d.Z * imp.halfW, 0.0f, -d.X * imp.halfW);  // up x forward
    const float y0 = c.Y - imp.halfH, y1 = c.Y + imp.halfH;
    const irr::video::SColor white(255, 255, 255, 255);
    // An FBO's row 0 is the BOTTOM of the capture (the driver does not flip a
    // 3D render target), so V = 0 on the quad's bottom edge.
    irr::video::S3DVertex q[4] = {
        irr::video::S3DVertex(c.X - r.X, y0, c.Z - r.Z, 0, 0, 1, white, 0.0f, 0.0f),
        irr::video::S3DVertex(c.X + r.X, y0, c.Z + r.Z, 0, 0, 1, white, 1.0f, 0.0f),
        irr::video::S3DVertex(c.X + r.X, y1, c.Z + r.Z, 0, 0, 1, white, 1.0f, 1.0f),
        irr::video::S3DVertex(c.X - r.X, y1, c.Z - r.Z, 0, 0, 1, white, 0.0f, 1.0f),
    };
    const irr::u16 idx[6] = {0, 1, 2, 0, 2, 3};
    irr::video::SMaterial m;
    m.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
    m.MaterialTypeParam = 0.1f;  // alpha ref: the capture clears to alpha 0
    m.Lighting = false;
    m.BackfaceCulling = false;
    // Diagnostic (B952 pixel decomposition): fog the quad at its own depth.
    static const bool bbFog = std::getenv("B952_BB_FOG") && *std::getenv("B952_BB_FOG") == '1';
    m.FogEnable = bbFog;
    m.setTexture(0, imp.tex);
    data.driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
    data.driver->setMaterial(m);
    data.driver->drawVertexPrimitiveList(q, 4, idx, 2, irr::video::EVT_STANDARD,
                                         irr::scene::EPT_TRIANGLES, irr::video::EIT_16BIT);
}

// ---------------------------------------------------------------------------
// The frame pre-pass
// ---------------------------------------------------------------------------

void prepareImpostorFrame(ZoneBenchData& data, const CameraState& cam, float renderDist,
                          bool frustumCull, float lastFrameMs, int frameIndex, bool measuring) {
    ImpostorCache& cache = impostorCache(data);
    planAdaptiveLod(data, cam, renderDist, frustumCull, lastFrameMs);

    double prepass = 0.0;

    // Creation: every billboard in the plan that has no texture yet.
    bool missing = false;
    for (const auto& pe : data.adaptiveLodState.plan) {
        if (pe.billboard && !cache.find(data, pe.entityIdx)) { missing = true; break; }
    }
    if (missing) {
        finishGpu();
        MemProbe before = probeMemory(data);
        const int createdBefore = cache.stats.created;
        auto t0 = std::chrono::steady_clock::now();
        for (const auto& pe : data.adaptiveLodState.plan) {
            if (pe.billboard) cache.acquire(data, pe.entityIdx, cam);
        }
        finishGpu();
        const double ms = msSince(t0);
        MemProbe after = probeMemory(data);
        accumulate(cache.stats.mem, before, after);
        cache.stats.createMs += ms;
        cache.stats.createBatches++;
        prepass += ms;
        printf("[impostor-create] frame=%d created=%d ms=%.3f per_impostor_ms=%.3f\n",
               frameIndex, cache.stats.created - createdBefore, ms,
               (cache.stats.created - createdBefore) > 0 ? ms / (cache.stats.created - createdBefore) : 0.0);
    }
    // Claimants: an entity may draw an existing shared texture for the first time.
    for (const auto& pe : data.adaptiveLodState.plan) {
        if (pe.billboard) cache.acquire(data, pe.entityIdx, cam);
    }

    // Refresh: every live impostor every N frames (never on a creation frame's
    // first render -- frameIndex 0 is the creation pass).
    if (cache.cfg.refreshEvery > 0 && frameIndex > 0 && frameIndex % cache.cfg.refreshEvery == 0 &&
        cache.textureCount() > 0) {
        finishGpu();
        auto t0 = std::chrono::steady_clock::now();
        int n = 0;
        for (auto& [k, e] : cache.mutableEntries()) {
            cache.render(data, e, e.sourceEntity, cam);
            ++n;
        }
        finishGpu();
        const double ms = msSince(t0);
        cache.stats.refreshPasses++;
        cache.stats.refreshed += n;
        cache.stats.refreshMs += ms;
        cache.stats.refreshMsMax = std::max(cache.stats.refreshMsMax, ms);
        prepass += ms;
    }
    cache.stats.prepassFrames++;
    if (measuring) cache.stats.prepassMs.push_back(static_cast<float>(prepass));
}

void noteImpostorTiers(ZoneBenchData& data, int tier1, int tier2, bool measuring) {
    if (!measuring) return;
    ImpostorCache& cache = impostorCache(data);
    cache.stats.tier1Sum += tier1;
    cache.stats.tier2Sum += tier2;
    cache.stats.tierFrames++;
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

void printImpostorReport(ZoneBenchData& data, const std::string& point,
                         const std::vector<float>& frameTotalsMs) {
    auto& p = data.adaptiveLodState.impostors;
    if (!p) return;
    const ImpostorCache& c = *p;
    const auto& s = c.stats;
    const size_t tex = c.textureBytes(), scr = c.scratchBytes();
    printf("[memledger-owner] point=%s category=textures_driver owner=tex.impostor live=%zu "
           "textures=%zu texture_bytes=%zu scratch_bytes=%zu claimants=%zu size=%d mode=%s fbo=%s\n",
           point.c_str(), tex + scr, c.textureCount(), tex, scr, c.claimantCount(), c.cfg.size,
           modeName(c.cfg), fboName(c.cfg));
    const auto& m = s.mem;
    printf("[impostor-mem] point=%s gpu_driver_delta=%" PRId64 " textures_driver_delta=%" PRId64
           " ledger_delta=%" PRId64 " rss_anon_delta=%" PRId64 " rss_shmem_delta=%" PRId64
           " rss_file_delta=%" PRId64 " sys_shmem_delta_kb=%" PRId64 " gpu_texture_bytes_delta=%" PRId64,
           point.c_str(), m.gpuDriver, m.texturesDriver, m.ledgerTotal, m.rssAnon, m.rssShmem,
           m.rssFile, m.sysShmemKb, m.gpuTextureBytes);
    for (const auto& [k, v] : m.drmFdinfo) printf(" %s_delta=%" PRId64, k.c_str(), v);
    printf("\n");

    std::vector<float> withPre;
    const size_t n = std::min(frameTotalsMs.size(), s.prepassMs.size());
    for (size_t i = 0; i < n; ++i) withPre.push_back(frameTotalsMs[i] + s.prepassMs[i]);
    printf("[impostor-timing] point=%s created=%d create_batches=%d create_ms=%.3f "
           "create_ms_per_impostor=%.4f refresh_every=%d refresh_passes=%d refreshed=%d "
           "refresh_ms_per_pass=%.3f refresh_ms_max=%.3f refresh_ms_per_frame=%.4f "
           "tier1_avg=%.1f tier2_avg=%.1f frame_p50_ms=%.2f frame_p95_ms=%.2f "
           "frame_plus_prepass_p50_ms=%.2f frame_plus_prepass_p95_ms=%.2f incomplete_fbo=%d\n",
           point.c_str(), s.created, s.createBatches, s.createMs,
           s.created ? s.createMs / s.created : 0.0, c.cfg.refreshEvery, s.refreshPasses,
           s.refreshed, s.refreshPasses ? s.refreshMs / s.refreshPasses : 0.0, s.refreshMsMax,
           s.prepassFrames ? s.refreshMs / s.prepassFrames : 0.0,
           s.tierFrames ? static_cast<double>(s.tier1Sum) / s.tierFrames : 0.0,
           s.tierFrames ? static_cast<double>(s.tier2Sum) / s.tierFrames : 0.0,
           percentile(frameTotalsMs, 0.50), percentile(frameTotalsMs, 0.95),
           percentile(withPre, 0.50), percentile(withPre, 0.95), s.incompleteFbo);
    fflush(stdout);
}

std::string impostorJson(ZoneBenchData& data) {
    auto& p = data.adaptiveLodState.impostors;
    if (!p || p->textureCount() == 0) return std::string();
    const ImpostorCache& c = *p;
    const auto& s = c.stats;
    const auto& m = s.mem;
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{\"owner\": \"tex.impostor\", \"category\": \"textures_driver\", \"size\": %d, "
             "\"mode\": \"%s\", \"fbo\": \"%s\", \"textures\": %zu, \"claimants\": %zu, "
             "\"textureBytes\": %zu, \"scratchBytes\": %zu, \"rowBytes\": %zu, "
             "\"created\": %d, \"createMs\": %.3f, \"refreshEvery\": %d, \"refreshPasses\": %d, "
             "\"refreshMs\": %.3f, \"gpuDriverDelta\": %" PRId64 ", \"texturesDriverDelta\": %" PRId64
             ", \"rssAnonDelta\": %" PRId64 ", \"rssShmemDelta\": %" PRId64
             ", \"sysShmemDeltaKb\": %" PRId64 ", \"gpuTextureBytesDelta\": %" PRId64 ", \"drmFdinfoDelta\": {",
             c.cfg.size, modeName(c.cfg), fboName(c.cfg), c.textureCount(), c.claimantCount(),
             c.textureBytes(), c.scratchBytes(), c.textureBytes() + c.scratchBytes(), s.created,
             s.createMs, c.cfg.refreshEvery, s.refreshPasses, s.refreshMs, m.gpuDriver,
             m.texturesDriver, m.rssAnon, m.rssShmem, m.sysShmemKb, m.gpuTextureBytes);
    std::string out = buf;
    bool first = true;
    for (const auto& [k, v] : m.drmFdinfo) {
        out += (first ? "" : ", ");
        out += "\"" + k + "\": " + std::to_string(v);
        first = false;
    }
    out += "}}";
    return out;
}

}  // namespace ZoneBench
