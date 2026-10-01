#pragma once

// B964 (GitLab #282): the impostor cache -- the GL half of the production port
// of B952's benchmark impostors (tools/hwcaps/zone_bench_impostor.{h,cpp}).
// The decision logic is impostor_policy.h; EntityRenderer drives both from
// src/client/graphics/entity_impostors.cpp.
//
//   - ONE scratch render target (kCellPx^2 RGBA8 + its DEPTH16, from the
//     driver's addRenderTargetTexture(), so on Lima it carries the driver's
//     EXT_multisampled_render_to_texture sample count) takes every capture.
//   - The capture is copied (glCopyTexSubImage2D) into a cell of an ATLAS
//     PAGE: kPagePx^2 RGBA8, a raw GL texture wrapped for the material system
//     with gles2WrapTexture(). The pages ARE the storage, so `tex.impostor`
//     counts pages + scratch, and the driver's gpuTextureBytes tally (B955's
//     logical column) carries them too.
//   - Capture: orthographic along the horizontal camera->entity direction at
//     the entity's own world transform (the lights and shaders the full model
//     gets), its current pose and its held items; the billboard is the
//     capture frustum. B952's capture, minus its idle-pose switch.
//   - Every billboard of a page is drawn by ImpostorBatchNode in ONE draw
//     (B952: one draw per billboard cost ~0.12 ms each on the Pi).

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/impostor/impostor_policy.h"

namespace EQT {
namespace Graphics {

class EQAnimatedMeshSceneNode;
class ImpostorBatchNode;

// What a capture measured: the frustum half extents and the frustum centre
// relative to the node's position (Irrlicht world axes).
struct ImpostorCapture {
    float halfW = 0.5f;
    float halfH = 0.5f;
    irr::core::vector3df offset;
    float viewAngleDeg = 0.0f;   // relative view angle the capture was taken at
    std::string pose;            // the animation playing when it was taken
    uint16_t sourceSpawn = 0;
    bool captured = false;
};

struct ImpostorCacheStats {
    uint64_t created = 0;          // first capture of a cell
    uint64_t refreshAppearance = 0;
    uint64_t refreshAngle = 0;
    uint64_t refreshFlip = 0;
    uint64_t flipReused = 0;       // a flip whose capture still matched (angle + pose)
    uint64_t demotions = 0;        // a unique cell taken for a shared key
    uint64_t sharedClaims = 0;     // a spawn drawn with an existing shared cell
    uint64_t pageAllocs = 0;
    uint64_t capFull = 0;          // a request the cap refused outright
    uint64_t incompleteFbo = 0;
    uint64_t units = 0;
    double unitMsSum = 0.0;
    double unitMsMax = 0.0;
    double frameUnitMsMax = 0.0;   // most capture time spent in one frame
    int frameUnitsMax = 0;
};

class ImpostorCache {
public:
    ImpostorCache(irr::video::IVideoDriver* driver, irr::scene::ISceneManager* smgr,
                  size_t budgetBytes);
    ~ImpostorCache();
    ImpostorCache(const ImpostorCache&) = delete;
    ImpostorCache& operator=(const ImpostorCache&) = delete;

    bool usable() const { return usable_; }
    size_t budgetBytes() const { return budgetBytes_; }

    // The cell for `key`, allocating (and creating its page) if needed.
    // Returns false when the cap refuses it.
    bool acquireCell(uint64_t key, uint32_t frame, bool& isNew);
    void touch(uint64_t key, uint32_t frame) { cells_.touch(key, frame); }
    void releaseCell(uint64_t key);
    // Least-recently-drawn unique key, for demotion.
    bool lruUnique(uint64_t& key) const { return cells_.lruUnique(key); }
    const impostor::CellTable& cells() const { return cells_; }

    // Render `node` (and its held-item children) into the key's cell, looking
    // from `camPos` (Irrlicht world). Returns false if nothing was drawn.
    bool capture(uint64_t key, EQAnimatedMeshSceneNode* node,
                 const std::vector<irr::scene::IMeshSceneNode*>& heldItems,
                 const irr::core::vector3df& camPos, float viewAngleDeg, uint16_t sourceSpawn);
    const ImpostorCapture* captureFor(uint64_t key) const;
    // Every shared key that holds a cell (for releasing the unclaimed ones).
    std::vector<uint64_t> sharedKeys() const;

    // The batch: cleared and refilled every frame by the entity pass.
    ImpostorBatchNode* batchNode() const { return batch_; }
    void addBillboard(uint64_t key, const irr::core::vector3df& nodePos, bool useHorizontalOffset);
    void beginBatch();

    // glFinish() -- the WILLEQ_IMPOSTOR_FINISH measurement switch only.
    void finishGpu() const;

    // Zone change: drop every cell and page (the scratch stays).
    void clear();

    size_t rowBytes() const { return impostor::rowBytes(cells_.pages(), scratch_ != nullptr); }
    // Publish the `tex.impostor` owner row (memory_owner_rows.h).
    void publishRow(int claimants, int tierFull, int tierImpostor) const;

    ImpostorCacheStats& stats() { return stats_; }
    const ImpostorCacheStats& stats() const { return stats_; }

private:
    bool ensureScratch();
    bool ensurePage(int page);
    void cellUv(const impostor::CellRef& ref, float& u0, float& v0, float& u1, float& v1) const;

    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    size_t budgetBytes_ = 0;
    bool usable_ = false;

    impostor::CellTable cells_;
    std::unordered_map<uint64_t, ImpostorCapture> captures_;
    struct Page {
        irr::video::ITexture* tex = nullptr;
        unsigned glTex = 0;
    };
    std::vector<Page> pages_;
    irr::video::ITexture* scratch_ = nullptr;
    int serial_ = 0;

    ImpostorBatchNode* batch_ = nullptr;
    ImpostorCacheStats stats_;
};

}  // namespace Graphics
}  // namespace EQT
