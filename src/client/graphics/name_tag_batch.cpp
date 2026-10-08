// B975 (GitLab #293): every entity name tag in one batch -- see name_tag_batch.h.

#include "client/graphics/name_tag_batch.h"

#include "client/core/memory_ledger.h"
#include "client/core/memory_owner_rows.h"
#include "client/core/gem_owner_census.h"
#include "common/logging.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
extern void gles2Prepare2DImageRun(void* driver, void* texture);
extern void gles2AdjustRawBufferBytes(void* driver, ptrdiff_t delta);
extern void gles2SyncBufferBindings(void* driver);
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>

namespace EQT {
namespace Graphics {

namespace {
constexpr const char* kOwnerRow = "ent.nametag_batch";
constexpr int64_t kWindowMs = 5000;
// B975's shrink: the tag pass's own vertex layout, 20 B/vertex
// {f32 x, y; u8 r, g, b, a (normalized); f32 u, v}, attributes 0/2/3 as
// ui_quad_batch.cpp's. A quad is 80 B of vertices + 12 B of indices, against
// the UiQuad intermediate's 36 B + 144 B vertices + 12 B indices.
constexpr size_t kMaxCompactQuadsPerDraw = 16384;  // 16-bit indices: 65536 vertices

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

bool nameTagBatchEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("WILLEQ_NAMETAG_BATCH");
        return !(v && v[0] == '0');
    }();
    return enabled;
}

// ---------------------------------------------------------------- NameTagNode

NameTagNode::NameTagNode(irr::scene::ISceneNode* parent, irr::scene::ISceneManager* mgr,
                         NameTagBatchNode* batch, const wchar_t* text, irr::video::SColor color,
                         const irr::core::vector3df& position)
    : irr::scene::ITextSceneNode(parent, mgr, -1, position),
      text_(text ? text : L""), color_(color), batch_(batch) {
    if (batch_) batch_->grab();
    // CTextSceneNode's: never culled by the scene manager.
    setAutomaticCulling(irr::scene::EAC_OFF);
}

NameTagNode::~NameTagNode() {
    if (batch_) batch_->drop();
}

void NameTagNode::OnRegisterSceneNode() {
    // CTextSceneNode registered itself in ESNRP_TRANSPARENT under exactly this
    // condition; the batch draws it instead.
    if (IsVisible && batch_) batch_->enqueue(this);
    ISceneNode::OnRegisterSceneNode();
}

void NameTagNode::setText(const wchar_t* text) {
    text_ = text ? text : L"";
    layoutDirty_ = true;
}

// The collision manager's projection with the proj x view product handed in:
// CSceneCollisionManager::getScreenCoordinatesFrom3DPosition builds
// `trans = cam->getProjectionMatrix(); trans *= cam->getViewMatrix();` on every
// call (1.9 us x N on the Pi -- B975's per-tag cost); the batch builds it once
// per render() and feeds it through the same multiplyWith1x4Matrix + round32
// steps, so a tag's screen position is the per-node path's position of the
// frame (same product, same steps; the per-pose pixel tool is the net).
// halfW/halfH are the driver's viewPort width/height over 2. NOT the per-node
// path's `dim`: CTextSceneNode passes useViewPort=false, i.e. the current
// render-target size. They agree while the viewport covers the render target
// (the client never calls setViewPort).
void projectNameTag(const irr::core::matrix4& projView, const irr::core::vector3df& world,
                    irr::s32 halfW, irr::s32 halfH, irr::core::position2d<irr::s32>& out) {
    irr::f32 p[4] = {world.X, world.Y, world.Z, 1.0f};
    projView.multiplyWith1x4Matrix(p);
    if (p[3] < 0.0f) {
        out = irr::core::position2d<irr::s32>(-10000, -10000);
        return;
    }
    const irr::f32 zDiv = p[3] == 0.0f ? 1.0f : irr::core::reciprocal(p[3]);
    out = irr::core::position2d<irr::s32>(
        halfW + irr::core::round32(static_cast<irr::f32>(halfW) * (p[0] * zDiv)),
        halfH - irr::core::round32(static_cast<irr::f32>(halfH) * (p[1] * zDiv)));
}

// ----------------------------------------------------------- NameTagBatchNode

NameTagBatchNode::NameTagBatchNode(irr::scene::ISceneNode* parent, irr::scene::ISceneManager* mgr,
                                   irr::gui::IGUIFont* font)
    : irr::scene::ISceneNode(parent, mgr, -1), font_(font) {
    if (font_) font_->grab();
    box_ = irr::core::aabbox3d<irr::f32>(-1e7f, -1e7f, -1e7f, 1e7f, 1e7f, 1e7f);
    setAutomaticCulling(irr::scene::EAC_OFF);
    if (table_.build(font_)) {
        // Every batchable glyph must come from one texture for a tag's quads
        // to be one run in left-to-right order (CGUISpriteBank groups by
        // texture). A tag whose glyphs span two goes to font->draw().
        for (size_t i = 0; i < BitmapGlyphTable::kSize; ++i) {
            const auto& g = table_.glyph(static_cast<wchar_t>(i));
            if (g.drawn && g.texture) { texture_ = g.texture; break; }
        }
        if (texture_) {
            const auto& ts = texture_->getOriginalSize();
            const irr::f32 invW = 1.0f / static_cast<irr::f32>(ts.Width);
            const irr::f32 invH = 1.0f / static_cast<irr::f32>(ts.Height);
            for (size_t i = 0; i < BitmapGlyphTable::kSize; ++i) {
                const auto& src = table_.glyph(static_cast<wchar_t>(i)).src;
                glyphUvs_[i] = {static_cast<irr::f32>(src.UpperLeftCorner.X) * invW,
                               static_cast<irr::f32>(src.UpperLeftCorner.Y) * invH,
                               static_cast<irr::f32>(src.LowerRightCorner.X) * invW,
                               static_cast<irr::f32>(src.LowerRightCorner.Y) * invH};
            }
        }
    }
}

NameTagBatchNode::~NameTagBatchNode() {
    releaseGpuBuffers();
    for (auto* t : pending_) t->drop();
    pending_.clear();
    eqt::memory::clearOwnerRow(kOwnerRow);
    if (font_) font_->drop();
}

void NameTagBatchNode::enqueue(NameTagNode* tag) {
    // render() empties the list every drawAll(); a list this long means no
    // render ran (the node is out of the tree) -- never let it grow.
    if (pending_.size() >= 65536) {
        for (auto* t : pending_) t->drop();
        pending_.clear();
    }
    tag->grab();
    pending_.push_back(tag);
}

void NameTagBatchNode::OnRegisterSceneNode() {
    if (IsVisible) {
        // Sort last among the transparent nodes: TransparentNodeEntry measures
        // the absolute translation against the active camera, so put it ON the
        // camera (distance 0) -- every other transparent node is drawn first,
        // every tag after it, as the 2D overlay they are.
        if (irr::scene::ICameraSceneNode* cam = SceneManager->getActiveCamera()) {
            AbsoluteTransformation.setTranslation(cam->getAbsolutePosition());
        }
        SceneManager->registerNodeForRendering(this, irr::scene::ESNRP_TRANSPARENT);
    }
    ISceneNode::OnRegisterSceneNode();
}

void NameTagBatchNode::layout(NameTagNode& tag) {
    tag.layoutDirty_ = false;
    tag.glyphs_.clear();
    tag.batchable_ = false;
    if (!font_) return;
    const irr::core::dimension2d<irr::u32> dim = font_->getDimension(tag.text_.c_str());
    tag.width_ = static_cast<irr::s32>(dim.Width);
    tag.height_ = static_cast<irr::s32>(dim.Height);
    if (!table_.usable() || !texture_) return;
    irr::s32 pen = 0;
    for (wchar_t c : tag.text_) {
        const BitmapGlyphTable::Glyph& g = table_.glyph(c);
        if (!g.batchable || (g.drawn && g.texture != texture_)) {
            tag.glyphs_.clear();
            return;
        }
        pen += g.underhang;
        if (g.drawn) tag.glyphs_.push_back({pen, c});
        pen += g.advanceAfter;
    }
    tag.glyphs_.shrink_to_fit();
    tag.batchable_ = true;
}

size_t NameTagBatchNode::bufferBytes() const {
    return compactVertices_.capacity() * sizeof(CompactVertex) +
           compactIndices_.capacity() * sizeof(uint16_t) + quads_.capacity() * sizeof(UiQuad) +
           quadBatch_.bytesReserved() + pending_.capacity() * sizeof(NameTagNode*) +
           sorted_.allocated_size() * sizeof(Entry);
}

void NameTagBatchNode::publishRow() {
    const size_t bytes = bufferBytes();
    if (bytes + gpuBufferBytes() == publishedBytes_) return;
    publishedBytes_ = bytes + gpuBufferBytes();
    eqt::memory::setOwnerRow(kOwnerRow, "entity_instances", bytes,
                             "compact_quads=" + std::to_string(compactIndices_.capacity() / 6) +
                             " gpu_buffer_bytes=" + std::to_string(gpuBufferBytes()));
}

void NameTagBatchNode::logWindow(int64_t micros) {
    const int64_t now = nowMs();
    if (windowStartMs_ == 0) windowStartMs_ = now;
    ++windowFrames_;
    windowTags_ += last_.tags;
    windowMaxTags_ = std::max(windowMaxTags_, last_.tags);
    windowMicros_.push_back(micros);
    if (now - windowStartMs_ < kWindowMs) return;

    std::vector<int64_t> s = windowMicros_;
    std::sort(s.begin(), s.end());
    const int64_t p50 = s[s.size() / 2];
    const int64_t p95 = s[std::min(s.size() - 1, (s.size() * 95) / 100)];
    const double avgTags = static_cast<double>(windowTags_) / static_cast<double>(windowFrames_);
    LOG_INFO(MOD_GRAPHICS,
             "[nametags] frames={} tags_avg={:.1f} tags_max={} cpu_us_p50={} cpu_us_p95={} "
             "us_per_tag_p50={:.2f} last_build_us={} last_submit_us={} draws={} quads={} fallback={} "
             "buffer_bytes={} gpu_buffer_bytes={} setup_us={} index_us={} upload_us={} draw_us={}",
             windowFrames_, avgTags, windowMaxTags_, p50, p95,
             avgTags > 0.0 ? static_cast<double>(p50) / avgTags : 0.0,
             last_.cpuMicros - last_.submitMicros, last_.submitMicros, last_.driverDraws,
             last_.glyphQuads, last_.fallbackTags, bufferBytes(), gpuBufferBytes(),
             last_.setupMicros, last_.indexMicros, last_.uploadMicros, last_.drawMicros);

    // Give back what a crowd that has left made the buffers hold: keep at most
    // twice this window's peak.
    const size_t keepQuads = 2 * windowMaxTags_ * 16 + 64;
    if (gpuBufferBytes() > 4 * keepQuads * 4 * sizeof(CompactVertex) ||
        (windowMaxTags_ == 0 && gpuBufferBytes())) releaseGpuBuffers();
    if (compactVertices_.capacity() / 4 > 4 * keepQuads ||
        compactIndices_.capacity() / 6 > 4 * keepQuads) {
        eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);
        std::vector<CompactVertex>().swap(compactVertices_);
        compactVertices_.reserve(keepQuads * 4);
        std::vector<uint16_t>().swap(compactIndices_);
        compactIndices_.reserve(std::min(keepQuads, kMaxCompactQuadsPerDraw) * 6);
    }
    if (quads_.capacity() > 4 * keepQuads || quadBatch_.bytesReserved() > keepQuads * (4 * 9 * 4 + 6 * 2)) {
        eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);
        std::vector<UiQuad>().swap(quads_);
        quads_.reserve(keepQuads);
        quadBatch_.trim(keepQuads);
    }
    publishRow();  // trimming changed capacity; publish the reduced owner row now
    windowStartMs_ = now;
    windowFrames_ = 0;
    windowTags_ = 0;
    windowMaxTags_ = 0;
    windowMicros_.clear();
}

// B975's shrink: the tag pass's own vertex fill (no UiQuad intermediate).
// One compact quad, COpenGLES2Driver::draw2DImage()'s own vertex arithmetic in
// the compact layout; the colour bytes are the tag's SColor verbatim (the
// shader's aColor decodes them normalized, the same floats draw2DImage()
// computed with `red / 255.0f`).
void writeCompactQuad(CompactVertex* vertices,
                       irr::s32 dstLeft, irr::s32 dstTop, irr::s32 dstRight, irr::s32 dstBottom,
                       irr::f32 u0, irr::f32 v0, irr::f32 u1, irr::f32 v1,
                       const irr::video::SColor& color) {
    // Storage was sized once for the tag, outside this glyph loop.
    const irr::u8 r = color.getRed(), g = color.getGreen(), b = color.getBlue(), a = color.getAlpha();
    vertices[0] = {static_cast<irr::f32>(dstLeft), static_cast<irr::f32>(dstTop), r, g, b, a, u0, v0};
    vertices[1] = {static_cast<irr::f32>(dstRight), static_cast<irr::f32>(dstTop), r, g, b, a, u1, v0};
    vertices[2] = {static_cast<irr::f32>(dstRight), static_cast<irr::f32>(dstBottom), r, g, b, a, u1, v1};
    vertices[3] = {static_cast<irr::f32>(dstLeft), static_cast<irr::f32>(dstBottom), r, g, b, a, u0, v1};
}

// The caller establishes the driver's own 2D state without drawing. All
// glyphs use bounded, caller-owned GPU storage, chunked at the 16-bit index
// cap. No orphaning or unsynchronized mapping: GL owns any necessary wait.
size_t NameTagBatchNode::drawCompactRun(irr::video::IVideoDriver* driver) {
    const auto& vertices = compactVertices_;
    auto& indices = compactIndices_;
    if (!driver || compactVertexCount_ == 0) return 0;
    if (!UiQuadBatch::driverSupportsBatching(driver)) return 0;
#ifdef EQT_HAS_GLES2
    using Clock = std::chrono::steady_clock;
    auto elapsed = [](Clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
    };
    auto start = Clock::now();
    // One immutable prefix, grown only when a run needs more quads. Every
    // chunk addresses its own vertex base with the same 16-bit pattern.
    const size_t quads = compactVertexCount_ / 4;
    const size_t patternQuads = std::min(kMaxCompactQuadsPerDraw, quads);
    indices.reserve(patternQuads * 6);
    for (size_t q = indices.size() / 6; q < patternQuads; ++q) {
        const uint16_t base = static_cast<uint16_t>(q * 4);
        const uint16_t i1 = base + 1, i2 = base + 2, i3 = base + 3;
        indices.insert(indices.end(), {base, i1, i2, base, i2, i3});
    }
    last_.indexMicros += elapsed(start);
    start = Clock::now();
    eqt::memory::GemTag gemTag("vbo:nametags");
    gpuDriver_ = driver;
    if (!gpuIndex_) glGenBuffers(1, &gpuIndex_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpuIndex_);
    // B972-FT (#326): the index EBO is written only when the pattern grows
    // (static), so it never enters the per-chunk write-wait below. The
    // vertex chunks, though, each write-then-draw the SAME buffer within a
    // run -- the very pattern issue #326 names. They now stream through a
    // RING of 12 vertex VBOs (3 frame rows x 4 chunk slots, the particle
    // renderer's shape): run r's chunk k writes slot (r % 3) * 4 + k --
    // distinct from the run's own previous chunks (same row, distinct
    // slots) and from the previous TWO runs' slots (the rows are 3-deep,
    // and the transparent pass is the frame's LAST). A run of more than 4
    // chunks would wrap into the next row, but that is 65536+ name-tag
    // quads -- far past any measured zone (keepQuads is 2*maxTags*16+64) --
    // and the wrap stays within-row-adjacent, never a previous-run slot.
    const size_t chunks = (quads + kMaxCompactQuadsPerDraw - 1) / kMaxCompactQuadsPerDraw;
    ++runChunk_;
    const size_t row = runChunk_ % kNametagRingFrames;
    const size_t vertexBytes = patternQuads * 4 * sizeof(CompactVertex) + sizeof(CompactVertex);
    const size_t indexBytes = patternQuads * 6 * sizeof(uint16_t);
    if (indexBytes > gpuIndexBytes_) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexBytes, indices.data(), GL_STATIC_DRAW);
        gles2AdjustRawBufferBytes(driver, static_cast<ptrdiff_t>(indexBytes) - gpuIndexBytes_);
        gpuIndexBytes_ = indexBytes;
    }
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CompactVertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CompactVertex),
                          reinterpret_cast<const GLvoid*>(offsetof(CompactVertex, r)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(CompactVertex),
                          reinterpret_cast<const GLvoid*>(offsetof(CompactVertex, u)));
    glEnableVertexAttribArray(3);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(4);
    last_.uploadMicros += elapsed(start);
    size_t draws = 0;
    for (size_t chunkIdx = 0; chunkIdx < chunks; ++chunkIdx) {
        const size_t chunkQuads = std::min(kMaxCompactQuadsPerDraw, quads - chunkIdx * kMaxCompactQuadsPerDraw);
        const CompactVertex* v = vertices.data() + chunkIdx * kMaxCompactQuadsPerDraw * 4;
        const size_t slot = (row * kNametagRingChunks + chunkIdx) % kNametagRingSlots;
        // One extra vertex keeps every subdata write strictly smaller than
        // the allocation (B975): partial writes reuse the bounded storage.
        if (!gpuVertexRing_[slot]) glGenBuffers(1, &gpuVertexRing_[slot]);
        glBindBuffer(GL_ARRAY_BUFFER, gpuVertexRing_[slot]);
        if (vertexBytes > gpuVertexRingBytes_[slot]) {
            glBufferData(GL_ARRAY_BUFFER, vertexBytes, nullptr, GL_DYNAMIC_DRAW);
            gles2AdjustRawBufferBytes(driver,
                                      static_cast<ptrdiff_t>(vertexBytes) - gpuVertexRingBytes_[slot]);
            gpuVertexRingBytes_[slot] = vertexBytes;
        }
        start = Clock::now();
        glBufferSubData(GL_ARRAY_BUFFER, 0, chunkQuads * 4 * sizeof(CompactVertex), v);
        last_.uploadMicros += elapsed(start);
        start = Clock::now();
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(chunkQuads * 6), GL_UNSIGNED_SHORT, nullptr);
        last_.drawMicros += elapsed(start);
        ++draws;
    }
    // setOrthoProjection left the driver's cached binds at zero. Restore
    // that state before a font fallback or any other renderer runs.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    // B972: the driver's state_ cache still names the tag VBO/EBO as bound;
    // a client-array fallback that skips its bind on a 0-cache would then
    // draw through the stale buffer. Re-read the real bindings from GL.
    gles2SyncBufferBindings(driver);
    return draws;
#else
    return 0;
#endif
}

void NameTagBatchNode::releaseGpuBuffers() {
#ifdef EQT_HAS_GLES2
    for (size_t i = 0; i < kNametagRingSlots; ++i) {
        if (gpuVertexRing_[i]) glDeleteBuffers(1, &gpuVertexRing_[i]);
        gpuVertexRing_[i] = 0;
        gpuVertexRingBytes_[i] = 0;
    }
    if (gpuIndex_) glDeleteBuffers(1, &gpuIndex_);
    if (gpuDriver_) gles2AdjustRawBufferBytes(gpuDriver_, -static_cast<ptrdiff_t>(gpuBufferBytes()));
#endif
    gpuIndex_ = 0;
    gpuIndexBytes_ = 0;
    gpuDriver_ = nullptr;
}

void NameTagBatchNode::render() {
    const auto t0 = std::chrono::steady_clock::now();
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);
    last_ = FrameStats();
    if (pending_.empty()) { logWindow(0); return; }

    irr::video::IVideoDriver* driver = SceneManager->getVideoDriver();
    irr::scene::ICameraSceneNode* cam = SceneManager->getActiveCamera();
    if (driver && cam && font_) {
        // CSceneManager's transparent order: farthest first, the same heapsort.
        const irr::core::vector3df camPos = cam->getAbsolutePosition();
        sorted_.set_used(0);
        for (NameTagNode* t : pending_) sorted_.push_back(Entry(t, camPos));
        sorted_.sort();

        const irr::core::rect<irr::s32>& vp = driver->getViewPort();
        // The ONE proj x view product of the frame (the collision manager
        // rebuilt it per tag; the arithmetic fed through it is its own).
        irr::core::matrix4 projView = cam->getProjectionMatrix();
        projView *= cam->getViewMatrix();
        const irr::s32 halfW = static_cast<irr::s32>(vp.getWidth() / 2);
        const irr::s32 halfH = static_cast<irr::s32>(vp.getHeight() / 2);

        // The compact 80 B/quad path is written for the GLES2 driver; any other
        // driver keeps the old UiQuad + UiQuadBatch per-quad path.
        const bool compact = UiQuadBatch::driverSupportsBatching(driver);
        if (compact) {
            compactVertexCount_ = 0;
        } else {
            quads_.clear();
        }
        irr::video::ITexture* runTexture = nullptr;
        bool runHasFirst = false;  // non-GLES2 fallback retains its first quad
        irr::core::rect<irr::s32> firstDest, firstSrc;
        irr::video::SColor firstColor;
        auto flushRun = [&]() {
            const bool haveCompact = compact && compactVertexCount_ != 0;
            if (!runHasFirst && !haveCompact && quads_.empty()) return;
            const auto s0 = std::chrono::steady_clock::now();
            if (runHasFirst) {
                if (compact) {
                    // Exactly the driver's draw2DImage state, without a
                    // client-array draw (and its transient upload allocation).
#ifdef EQT_HAS_GLES2
                    gles2Prepare2DImageRun(driver, runTexture);
#endif
                    last_.setupMicros += std::chrono::duration_cast<std::chrono::microseconds>(
                                            std::chrono::steady_clock::now() - s0).count();
                    last_.driverDraws += drawCompactRun(driver);
                    last_.glyphQuads += compactVertexCount_ / 4;
                } else {
                    // The old path: the first quad and every other quad of the
                    // run go through UiQuadBatch (per-quad on a non-GLES2 driver).
                    quads_.insert(quads_.begin(),
                                  UiQuad{firstDest, firstSrc, firstColor});
                }
            }
            if (!compact && !quads_.empty()) {
                last_.driverDraws += quadBatch_.drawTexturedRun(driver, runTexture, quads_, true);
                last_.glyphQuads += quads_.size();
            }
            last_.submitMicros += std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - s0).count();
            if (compact) {
                compactVertexCount_ = 0;
            }
            quads_.clear();
            runHasFirst = false;
            runTexture = nullptr;
        };

        for (irr::u32 i = 0; i < sorted_.size(); ++i) {
            NameTagNode& tag = *sorted_[i].node;
            if (tag.layoutDirty_) layout(tag);
            if (tag.text_.empty()) continue;
            // CTextSceneNode::render(): the same projection, then
            // CGUIFont::draw(text, rect(pos, 1x1), color, hcenter, vcenter).
            irr::core::position2d<irr::s32> pos;
            projectNameTag(projView, tag.getAbsolutePosition(), halfW, halfH, pos);
            const irr::s32 ox = pos.X + ((1 - tag.width_) >> 1);
            const irr::s32 oy = pos.Y + ((1 - tag.height_) >> 1);
            // Entirely off the viewport (behind the camera reads -10000): the
            // per-node draw rasterised nothing either.
            if (ox - 64 > vp.LowerRightCorner.X || oy - 64 > vp.LowerRightCorner.Y ||
                ox + tag.width_ + 64 < vp.UpperLeftCorner.X || oy + tag.height_ + 64 < vp.UpperLeftCorner.Y) {
                continue;
            }
            ++last_.tags;
            if (!tag.batchable_) {
                flushRun();  // keep everything queued before it underneath it
                const irr::core::rect<irr::s32> r(pos, irr::core::dimension2d<irr::s32>(1, 1));
                const auto s0 = std::chrono::steady_clock::now();
                font_->draw(tag.text_.c_str(), r, tag.color_, true, true);
                last_.submitMicros += std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - s0).count();
                ++last_.fallbackTags;
                ++last_.driverDraws;
                continue;
            }
            // Reuse the sized storage, not merely its capacity. Growing once
            // per tag removes per-corner/per-glyph vector bookkeeping; only
            // the used prefix is submitted, including after fallback splits.
            if (compact) {
                const size_t need = compactVertexCount_ + tag.glyphs_.size() * 4;
                if (compactVertices_.size() < need) compactVertices_.resize(need);
            }
            for (const auto& gp : tag.glyphs_) {
                const BitmapGlyphTable::Glyph& g = table_.glyph(gp.c);
                if (runTexture && g.texture != runTexture) flushRun();
                runTexture = g.texture;
                const irr::s32 x = ox + gp.dx;
                if (!runHasFirst) {
                    // Capture the first glyph for the non-GLES2 fallback.
                    // The compact path includes it in the same GPU run.
                    firstDest = irr::core::rect<irr::s32>(x, oy,
                                                          x + g.src.getWidth(),
                                                          oy + g.src.getHeight());
                    firstSrc = g.src;
                    firstColor = tag.color_;
                    runHasFirst = true;
                    if (!compact) continue;
                }
                if (compact) {
                    const auto& uv = glyphUvs_[static_cast<size_t>(gp.c)];
                    writeCompactQuad(compactVertices_.data() + compactVertexCount_, x, oy,
                                      x + g.src.getWidth(), oy + g.src.getHeight(),
                                      uv.u0, uv.v0, uv.u1, uv.v1, tag.color_);
                    compactVertexCount_ += 4;
                } else {
                    quads_.push_back({{x, oy, x + g.src.getWidth(), oy + g.src.getHeight()}, g.src, tag.color_});
                }
            }
        }
        flushRun();
    }

    for (NameTagNode* t : pending_) t->drop();
    pending_.clear();
    publishRow();
    last_.cpuMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - t0).count();
    logWindow(last_.cpuMicros);
}

} // namespace Graphics
} // namespace EQT
