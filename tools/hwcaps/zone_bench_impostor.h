#pragma once

// B952 (GitLab #270): impostor billboards, part 1 -- make the impostor memory
// cost measurable and bounded in the benchmark, so the impostor row's budget
// and the impostor size can be chosen from numbers (refactor/B952/README.md).
//
// Before B952 every impostor was an addRenderTargetTexture(): an RGBA8 colour
// texture PLUS its own DEPTH16 renderbuffer and FBO
// (docker/irrlicht-drm/COGLES2Texture.cpp createRenderTarget), 384 KB at 256^2,
// with the billboard's size fixed at 6 x 8 units whatever the model, the capture
// taken from the model's +X side and the image rotated 180 degrees. Here:
//
//   - ONE scratch render target (FBO + DEPTH16) serves every impostor render;
//     an impostor is a colour texture only, swapped in as the scratch FBO's
//     colour attachment and wrapped for the material with the driver's
//     existing gles2WrapTexture() bridge. No driver change. The pre-B952 shape
//     stays reachable as ImpostorConfig::sharedFbo = false (--impostor-fbo per).
//   - Capture is orthographic along the camera->entity horizontal direction,
//     with the entity's real world transform (so the zone lights are the ones
//     the full model gets), and the billboard is sized to the capture frustum.
//   - Unique (one per entity) or shared (one per race/gender/texture-variant
//     key) textures; the tex.impostor owner row counts each texture ONCE and
//     names its claimant count (the shared-block rule).
//   - Creation and refresh run in a pre-pass before beginScene(), bracketed by
//     glFinish(), so the times are completed GPU work, not submission.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <irrlicht.h>

#include "client/core/memory_ledger.h"

namespace ZoneBench {

struct ZoneBenchData;
struct CameraState;

struct ImpostorConfig {
    int size = 256;             // texture edge (64/128/256)
    bool shared = false;        // key on race/gender/variant instead of entity
    bool sharedFbo = true;      // one scratch FBO+depth (B952) vs one RTT each (pre-B952)
    int refreshEvery = 0;       // re-render every live impostor every N frames (0 = never)
};

struct ImpostorEntry {
    irr::video::ITexture* tex = nullptr;     // what the billboard samples
    unsigned glTex = 0;                      // its GL name (sharedFbo) -- 0 for an own RTT
    bool ownRtt = false;                     // pre-B952 shape: tex is an RTT with its own depth
    float halfW = 1.0f, halfH = 1.0f;        // capture frustum half extents (world units)
    irr::core::vector3df centerOffset;       // bbox centre - entity position (Irrlicht coords)
    int claimants = 0;                       // entities drawing this texture
    int sourceEntity = -1;                   // the entity whose pose/lighting was captured
};

// A ledger snapshot pair's differences, accumulated over every creation batch.
struct ImpostorMemoryDelta {
    int64_t gpuDriver = 0;
    int64_t texturesDriver = 0;
    int64_t ledgerTotal = 0;
    int64_t rssAnon = 0;
    int64_t rssShmem = 0;
    int64_t rssFile = 0;
    int64_t sysShmemKb = 0;                  // /proc/meminfo Shmem (system-wide)
    int64_t gpuTextureBytes = 0;             // the driver's own texture tally
    std::map<std::string, int64_t> drmFdinfo;  // drm-* memory keys of this process's DRM fds
};

struct ImpostorStats {
    int created = 0;
    int createBatches = 0;
    double createMs = 0.0;                   // glFinish-bracketed
    int refreshPasses = 0;
    int refreshed = 0;
    double refreshMs = 0.0;
    double refreshMsMax = 0.0;
    int incompleteFbo = 0;                   // swapped attachment rejected by the driver
    // Per measured frame: pre-pass time (creation + refresh), and the tier split.
    std::vector<float> prepassMs;
    int prepassFrames = 0;                   // every pre-pass, warmup included
    long long tier1Sum = 0, tier2Sum = 0;
    int tierFrames = 0;
    ImpostorMemoryDelta mem;
};

class ImpostorCache {
public:
    ImpostorConfig cfg;
    ImpostorStats stats;

    // The entity's key: its own index (unique) or race/gender/variant (shared).
    uint64_t keyFor(const ZoneBenchData& data, int entityIdx) const;
    // Existing entry for the entity, or null.
    ImpostorEntry* find(const ZoneBenchData& data, int entityIdx);
    // Existing entry, or a new texture rendered from this entity now.
    ImpostorEntry* acquire(ZoneBenchData& data, int entityIdx, const CameraState& cam);
    // Re-render one entry from its source entity.
    void render(ZoneBenchData& data, ImpostorEntry& e, int entityIdx, const CameraState& cam);
    // Drop every texture (the pixel test re-creates at another size).
    void clear(ZoneBenchData& data);

    size_t textureCount() const { return entries_.size(); }
    size_t textureBytes() const;     // colour textures (+ their own depth for ownRtt)
    size_t scratchBytes() const;     // the one scratch RTT: colour + DEPTH16
    size_t claimantCount() const;
    const std::map<uint64_t, ImpostorEntry>& entries() const { return entries_; }
    std::map<uint64_t, ImpostorEntry>& mutableEntries() { return entries_; }

private:
    bool ensureScratch(ZoneBenchData& data);
    std::map<uint64_t, ImpostorEntry> entries_;
    std::map<int, uint64_t> entityKey_;
    irr::video::ITexture* scratch_ = nullptr;
    int scratchSamples_ = 0;      // >0: the scratch colour is EXT_multisampled_render_to_texture
    void* msAttach_ = nullptr;    // glFramebufferTexture2DMultisampleEXT
    int serial_ = 0;
};

// The cache hung off ZoneBenchData (created on first use with `cfg`).
ImpostorCache& impostorCache(ZoneBenchData& data);
void configureImpostors(ZoneBenchData& data, const ImpostorConfig& cfg);

// One impostor quad for an entity at `entityPos` (Irrlicht coords): the
// capture frustum, centred on the captured bbox centre, facing the camera along
// the horizontal camera->entity direction the capture looked along.
void drawImpostorBillboard(ZoneBenchData& data, const ImpostorEntry& imp,
                           const irr::core::vector3df& entityPos, const CameraState& cam);

// Frame pre-pass, BEFORE beginScene(): plan the adaptive tiers for this frame,
// create every impostor the plan needs, and refresh all live impostors every
// cfg.refreshEvery frames. `measuring` records the pre-pass time per frame.
void prepareImpostorFrame(ZoneBenchData& data, const CameraState& cam, float renderDist,
                          bool frustumCull, float lastFrameMs, int frameIndex, bool measuring);
void noteImpostorTiers(ZoneBenchData& data, int tier1, int tier2, bool measuring);

// `[memledger-owner] point=<p> category=textures_driver owner=tex.impostor ...`
// plus the `[impostor-timing]` and `[impostor-mem]` lines. `frameTotalsMs` are
// the measured frames' totalMs (for frame+pre-pass percentiles); may be empty.
void printImpostorReport(ZoneBenchData& data, const std::string& point,
                         const std::vector<float>& frameTotalsMs);
// JSON object for --memory-json, or "" when no impostor exists.
std::string impostorJson(ZoneBenchData& data);

// Re-place the entities in rows in front of the pose: `mixed` = first half
// inside tierDist, second half beyond it; otherwise all beyond it.
void placeImpostorScene(ZoneBenchData& data, float eqX, float eqY, float eqZ,
                        float dirX, float dirY, bool mixed, float tierDist);

// --impostor-pixel-test: full model vs impostor at 64/128/256 for every loaded
// variant at 40/80/160 units. Returns a process exit code.
int runImpostorPixelTest(ZoneBenchData& data, float eqX, float eqY, float eqZ,
                         float heading, const std::string& outDir);

}  // namespace ZoneBench
