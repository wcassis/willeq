// B964: the impostor cache's GL half -- see include/client/graphics/impostor/impostor_cache.h.

#include "client/graphics/impostor/impostor_cache.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "client/core/memory_ledger.h"
#include "client/core/memory_owner_rows.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/impostor/impostor_batch_node.h"
#include "common/logging.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
// The driver's bridge (docker/irrlicht-drm/COpenGLES2Driver.cpp): wrap a GL
// texture name as an ITexture the material system can bind; its bytes join
// the driver's gpuTextureBytes tally.
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                              unsigned int width, unsigned int height, unsigned int gpuBytes);
#endif

namespace EQT {
namespace Graphics {

using namespace impostor;

ImpostorCache::ImpostorCache(irr::video::IVideoDriver* driver, irr::scene::ISceneManager* smgr,
                             size_t budgetBytes)
    : driver_(driver), smgr_(smgr), budgetBytes_(budgetBytes), cells_(budgetBytes) {
#ifdef EQT_HAS_GLES2
    usable_ = driver_ && smgr_ && driver_->getDriverType() == irr::video::EDT_OGLES2 && cells_.maxPages() > 0;
#endif
    if (!usable_) return;
    eqt::memory::MemoryScope scope(eqt::memory::Category::TexturesDriver);
    batch_ = new ImpostorBatchNode(smgr_->getRootSceneNode(), smgr_);
    LOG_INFO(MOD_GRAPHICS,
             "Impostor: cache on -- budget {} B, cell {}^2, page {}^2 ({} cells), max {} pages = {} cells",
             budgetBytes_, kCellPx, kPagePx, kCellsPerPage, cells_.maxPages(), cells_.maxCells());
}

ImpostorCache::~ImpostorCache() {
    clear();
    if (scratch_ && driver_) driver_->removeTexture(scratch_);
    scratch_ = nullptr;
    if (batch_) {
        batch_->clearQuads();
        batch_->remove();
        batch_->drop();
        batch_ = nullptr;
    }
    eqt::memory::clearOwnerRow("tex.impostor");
}

bool ImpostorCache::ensureScratch() {
    if (scratch_) return true;
    eqt::memory::MemoryScope scope(eqt::memory::Category::TexturesDriver);
    char name[64];
    snprintf(name, sizeof(name), "impostor_scratch_%d", kCellPx);
    scratch_ = driver_->addRenderTargetTexture(irr::core::dimension2d<irr::u32>(kCellPx, kCellPx), name);
    return scratch_ != nullptr;
}

bool ImpostorCache::ensurePage(int page) {
    if (page < static_cast<int>(pages_.size()) && pages_[page].tex) return true;
#ifdef EQT_HAS_GLES2
    eqt::memory::MemoryScope scope(eqt::memory::Category::TexturesDriver);
    if (static_cast<size_t>(page) >= pages_.size()) pages_.resize(page + 1);
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kPagePx, kPagePx, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // The driver caches its bindings: put back what it believes is bound.
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    char name[64];
    snprintf(name, sizeof(name), "impostor_page_%d", serial_++);
    pages_[page].glTex = t;
    pages_[page].tex = static_cast<irr::video::ITexture*>(
        gles2WrapTexture(driver_, name, t, kPagePx, kPagePx, static_cast<unsigned>(kPageBytes)));
    stats_.pageAllocs++;
    LOG_INFO(MOD_GRAPHICS, "Impostor: page {} allocated ({} B); row now {} B of {} B", page, kPageBytes,
             rowBytes(), budgetBytes_);
    return pages_[page].tex != nullptr;
#else
    (void)page;
    return false;
#endif
}

bool ImpostorCache::acquireCell(uint64_t key, uint32_t frame, bool& isNew) {
    isNew = false;
    CellRef ref;
    switch (cells_.acquire(key, frame, ref)) {
        case CellTable::Alloc::Existing:
            return true;
        case CellTable::Alloc::NewCell:
            isNew = true;
            captures_[key] = ImpostorCapture{};
            return true;
        case CellTable::Alloc::NewPage:
            if (!ensurePage(ref.page)) {
                cells_.release(key);
                return false;
            }
            isNew = true;
            captures_[key] = ImpostorCapture{};
            return true;
        case CellTable::Alloc::Full:
        default:
            stats_.capFull++;
            return false;
    }
}

void ImpostorCache::releaseCell(uint64_t key) {
    cells_.release(key);
    captures_.erase(key);
}

const ImpostorCapture* ImpostorCache::captureFor(uint64_t key) const {
    auto it = captures_.find(key);
    return it == captures_.end() ? nullptr : &it->second;
}

std::vector<uint64_t> ImpostorCache::sharedKeys() const {
    std::vector<uint64_t> out;
    for (const auto& [k, c] : captures_)
        if (isSharedKey(k)) out.push_back(k);
    return out;
}

void ImpostorCache::cellUv(const CellRef& ref, float& u0, float& v0, float& u1, float& v1) const {
    const int cx = ref.cell % kCellsPerRow, cy = ref.cell / kCellsPerRow;
    const float inv = 1.0f / static_cast<float>(kPagePx);
    // Half a texel in from the cell edge, so bilinear never reads a neighbour.
    u0 = (cx * kCellPx + 0.5f) * inv;
    u1 = (cx * kCellPx + kCellPx - 0.5f) * inv;
    v0 = (cy * kCellPx + 0.5f) * inv;
    v1 = (cy * kCellPx + kCellPx - 0.5f) * inv;
}

bool ImpostorCache::capture(uint64_t key, EQAnimatedMeshSceneNode* node,
                            const std::vector<irr::scene::IMeshSceneNode*>& heldItems,
                            const irr::core::vector3df& camPos, float viewAngleDeg, uint16_t sourceSpawn) {
#ifdef EQT_HAS_GLES2
    CellRef ref;
    if (!node || !cells_.find(key, ref) || ref.page >= static_cast<int>(pages_.size())) return false;
    if (!ensureScratch()) return false;
    irr::scene::SMesh* mesh = node->getInstanceMesh();
    if (!mesh) return false;
    eqt::memory::MemoryScope scope(eqt::memory::Category::TexturesDriver);

    using Clock = std::chrono::steady_clock;
    const auto tStart = Clock::now();
    // The pose the entity is in now (its skinning may be suspended).
    node->forceAnimationUpdate();
    node->updateAbsolutePosition();
    const irr::core::matrix4 world = node->getAbsoluteTransformation();
    for (auto* item : heldItems)
        if (item) item->updateAbsolutePosition();

    // World-space bounds of the posed vertices and the held items.
    irr::core::aabbox3df box;
    bool init = false;
    auto addPoint = [&](const irr::core::vector3df& p) {
        if (!init) { box.reset(p); init = true; }
        else box.addInternalPoint(p);
    };
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        auto* buf = mesh->getMeshBuffer(b);
        if (!buf || buf->getVertexCount() == 0 || buf->getVertexType() != irr::video::EVT_STANDARD) continue;
        const auto* v = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
        for (irr::u32 i = 0; i < buf->getVertexCount(); ++i) {
            irr::core::vector3df p = v[i].Pos;
            world.transformVect(p);
            addPoint(p);
        }
    }
    if (!init) return false;
    for (auto* item : heldItems) {
        if (!item || !item->getMesh()) continue;
        irr::core::aabbox3df ib = item->getMesh()->getBoundingBox();
        item->getAbsoluteTransformation().transformBoxEx(ib);
        addPoint(ib.MinEdge);
        addPoint(ib.MaxEdge);
    }
    const irr::core::vector3df center = box.getCenter();

    // Look along the horizontal camera->entity direction; right = up x forward.
    irr::core::vector3df d(center.X - camPos.X, 0.0f, center.Z - camPos.Z);
    if (d.getLengthSQ() < 1e-6f) d.set(0.0f, 0.0f, 1.0f);
    d.normalize();
    const irr::core::vector3df right(d.Z, 0.0f, -d.X);
    float maxR = 0.0f, maxY = 0.0f;
    irr::core::vector3df corners[8];
    box.getEdges(corners);
    for (const auto& c : corners) {
        const irr::core::vector3df p = c - center;
        maxR = std::max(maxR, std::fabs(p.dotProduct(right)));
        maxY = std::max(maxY, std::fabs(p.Y));
    }
    ImpostorCapture& cap = captures_[key];
    cap.halfW = std::max(0.5f, maxR * 1.04f);
    cap.halfH = std::max(0.5f, maxY * 1.04f);
    cap.offset = center - node->getAbsolutePosition();
    cap.viewAngleDeg = viewAngleDeg;
    cap.sourceSpawn = sourceSpawn;
    cap.pose = node->getCurrentAnimation();

    const irr::core::matrix4 savedView = driver_->getTransform(irr::video::ETS_VIEW);
    const irr::core::matrix4 savedProj = driver_->getTransform(irr::video::ETS_PROJECTION);
    const irr::core::matrix4 savedWorld = driver_->getTransform(irr::video::ETS_WORLD);

    const auto tBind = Clock::now();
    driver_->setRenderTarget(scratch_, true, true, irr::video::SColor(0, 0, 0, 0));
    const auto tDraw = Clock::now();
    irr::core::matrix4 proj;
    proj.buildProjectionMatrixOrthoLH(cap.halfW * 2.0f, cap.halfH * 2.0f, 0.1f, 500.0f);
    driver_->setTransform(irr::video::ETS_PROJECTION, proj);
    irr::core::matrix4 view;
    view.buildCameraLookAtMatrixLH(center - d * 100.0f, center, irr::core::vector3df(0.0f, 1.0f, 0.0f));
    driver_->setTransform(irr::video::ETS_VIEW, view);

    // The model with the materials it is drawn with (EQAnimatedMeshSceneNode::render()).
    driver_->setTransform(irr::video::ETS_WORLD, world);
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb) continue;
        irr::video::SMaterial mat = i < node->getMaterialCount() ? node->getMaterial(i) : mb->getMaterial();
        mat.BackfaceCulling = false;
        driver_->setMaterial(mat);
        driver_->drawMeshBuffer(mb);
    }
    for (auto* item : heldItems) {
        if (!item || !item->getMesh() || !item->isVisible()) continue;
        driver_->setTransform(irr::video::ETS_WORLD, item->getAbsoluteTransformation());
        irr::scene::IMesh* im = item->getMesh();
        for (irr::u32 i = 0; i < im->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* mb = im->getMeshBuffer(i);
            if (!mb) continue;
            irr::video::SMaterial mat = i < item->getMaterialCount() ? item->getMaterial(i) : mb->getMaterial();
            mat.BackfaceCulling = false;
            driver_->setMaterial(mat);
            driver_->drawMeshBuffer(mb);
        }
    }

    const auto tCopy = Clock::now();
    // Copy the capture into its cell while the scratch FBO is still bound.
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        if (stats_.incompleteFbo++ == 0)
            LOG_WARN(MOD_GRAPHICS, "Impostor: scratch FBO incomplete -- this impostor stays blank");
    }
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glBindTexture(GL_TEXTURE_2D, pages_[ref.page].glTex);
    const int cx = ref.cell % kCellsPerRow, cy = ref.cell / kCellsPerRow;
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, cx * kCellPx, cy * kCellPx, 0, 0, kCellPx, kCellPx);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));

    const auto tUnbind = Clock::now();
    driver_->setRenderTarget(nullptr, false, false);
    const auto tEnd = Clock::now();
    // Where a capture's time goes, for the first few (the unit timing in the
    // [impostor] line is the whole of it).
    if (stats_.units < 16) {
        auto ms = [](Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        LOG_INFO(MOD_GRAPHICS,
                 "[impostor-capture] key={} pose_bounds_ms={:.2f} bind_clear_ms={:.2f} draw_ms={:.2f} "
                 "copy_ms={:.2f} unbind_ms={:.2f} buffers={}",
                 key, ms(tStart, tBind), ms(tBind, tDraw), ms(tDraw, tCopy), ms(tCopy, tUnbind),
                 ms(tUnbind, tEnd), mesh->getMeshBufferCount());
    }
    driver_->setTransform(irr::video::ETS_VIEW, savedView);
    driver_->setTransform(irr::video::ETS_PROJECTION, savedProj);
    driver_->setTransform(irr::video::ETS_WORLD, savedWorld);
    cap.captured = true;
    return true;
#else
    (void)key; (void)node; (void)heldItems; (void)camPos; (void)viewAngleDeg; (void)sourceSpawn;
    return false;
#endif
}

void ImpostorCache::finishGpu() const {
#ifdef EQT_HAS_GLES2
    glFinish();
#endif
}

void ImpostorCache::beginBatch() {
    if (batch_) batch_->clearQuads();
}

void ImpostorCache::addBillboard(uint64_t key, const irr::core::vector3df& nodePos, bool useHorizontalOffset) {
    if (!batch_) return;
    CellRef ref;
    if (!cells_.find(key, ref) || ref.page >= static_cast<int>(pages_.size())) return;
    const ImpostorCapture* cap = captureFor(key);
    if (!cap || !cap->captured) return;
    irr::core::vector3df c = nodePos;
    c.Y += cap->offset.Y;
    if (useHorizontalOffset) {
        c.X += cap->offset.X;
        c.Z += cap->offset.Z;
    }
    float u0, v0, u1, v1;
    cellUv(ref, u0, v0, u1, v1);
    batch_->addQuad(ref.page, pages_[ref.page].tex, c, cap->halfW, cap->halfH, u0, v0, u1, v1);
}

void ImpostorCache::clear() {
    if (batch_) batch_->clearQuads();
    for (auto& p : pages_) {
        if (p.tex && driver_) driver_->removeTexture(p.tex);
    }
    pages_.clear();
    cells_.clear();
    captures_.clear();
}

void ImpostorCache::publishRow(int claimants, int tierFull, int tierImpostor) const {
    char detail[384];
    snprintf(detail, sizeof(detail),
             "pages=%d cells=%d unique=%d shared=%d claimants=%d page_bytes=%zu scratch_bytes=%zu "
             "budget=%zu cell_px=%d tier_full=%d tier_impostor=%d created=%llu refreshed=%llu demotions=%llu",
             cells_.pages(), cells_.cellsUsed(), cells_.uniqueCount(), cells_.sharedCount(), claimants,
             static_cast<size_t>(cells_.pages()) * kPageBytes, scratch_ ? kScratchBytes : size_t(0),
             budgetBytes_, kCellPx, tierFull, tierImpostor, static_cast<unsigned long long>(stats_.created),
             static_cast<unsigned long long>(stats_.refreshAppearance + stats_.refreshAngle + stats_.refreshFlip),
             static_cast<unsigned long long>(stats_.demotions));
    eqt::memory::setOwnerRow("tex.impostor", "textures_driver", rowBytes(), detail);
}

}  // namespace Graphics
}  // namespace EQT
