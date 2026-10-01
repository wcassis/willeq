// B975 (GitLab #293): every entity name tag in one batch -- see name_tag_batch.h.

#include "client/graphics/name_tag_batch.h"

#include "client/core/memory_ledger.h"
#include "client/core/memory_owner_rows.h"
#include "common/logging.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>

namespace EQT {
namespace Graphics {

namespace {
constexpr const char* kOwnerRow = "ent.nametag_batch";
constexpr int64_t kWindowMs = 5000;

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
    }
}

NameTagBatchNode::~NameTagBatchNode() {
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
    return quads_.capacity() * sizeof(UiQuad) + pending_.capacity() * sizeof(NameTagNode*) +
           sorted_.allocated_size() * sizeof(Entry) + quadBatch_.bytesReserved();
}

void NameTagBatchNode::publishRow() {
    const size_t bytes = bufferBytes();
    if (bytes == publishedBytes_) return;
    publishedBytes_ = bytes;
    eqt::memory::setOwnerRow(kOwnerRow, "entity_instances", bytes,
                             "quad_capacity=" + std::to_string(quads_.capacity()));
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
             "buffer_bytes={}",
             windowFrames_, avgTags, windowMaxTags_, p50, p95,
             avgTags > 0.0 ? static_cast<double>(p50) / avgTags : 0.0,
             last_.cpuMicros - last_.submitMicros, last_.submitMicros, last_.driverDraws,
             last_.glyphQuads, last_.fallbackTags, bufferBytes());

    // Give back what a crowd that has left made the buffers hold: keep at most
    // twice this window's peak.
    const size_t keepQuads = 2 * windowMaxTags_ * 16 + 64;
    if (quads_.capacity() > 4 * keepQuads) {
        eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);
        std::vector<UiQuad>().swap(quads_);
        quads_.reserve(keepQuads);
        quadBatch_.trim(keepQuads);
        publishRow();
    }
    windowStartMs_ = now;
    windowFrames_ = 0;
    windowTags_ = 0;
    windowMaxTags_ = 0;
    windowMicros_.clear();
}

void NameTagBatchNode::render() {
    const auto t0 = std::chrono::steady_clock::now();
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);
    last_ = FrameStats();
    if (pending_.empty()) return;

    irr::video::IVideoDriver* driver = SceneManager->getVideoDriver();
    irr::scene::ICameraSceneNode* cam = SceneManager->getActiveCamera();
    irr::scene::ISceneCollisionManager* coll = SceneManager->getSceneCollisionManager();
    if (driver && cam && coll && font_) {
        // CSceneManager's transparent order: farthest first, the same heapsort.
        const irr::core::vector3df camPos = cam->getAbsolutePosition();
        sorted_.set_used(0);
        for (NameTagNode* t : pending_) sorted_.push_back(Entry(t, camPos));
        sorted_.sort();

        const irr::core::rect<irr::s32>& vp = driver->getViewPort();
        quads_.clear();
        irr::video::ITexture* runTexture = nullptr;
        auto flushRun = [&]() {
            if (quads_.empty()) return;
            const auto s0 = std::chrono::steady_clock::now();
            last_.driverDraws += quadBatch_.drawTexturedRun(driver, runTexture, quads_, true);
            last_.submitMicros += std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - s0).count();
            last_.glyphQuads += quads_.size();
            quads_.clear();
        };

        for (irr::u32 i = 0; i < sorted_.size(); ++i) {
            NameTagNode& tag = *sorted_[i].node;
            if (tag.layoutDirty_) layout(tag);
            if (tag.text_.empty()) continue;
            // CTextSceneNode::render(): the same projection call, then
            // CGUIFont::draw(text, rect(pos, 1x1), color, hcenter, vcenter).
            const irr::core::position2d<irr::s32> pos =
                coll->getScreenCoordinatesFrom3DPosition(tag.getAbsolutePosition(), cam);
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
            for (const auto& gp : tag.glyphs_) {
                const BitmapGlyphTable::Glyph& g = table_.glyph(gp.c);
                if (runTexture && g.texture != runTexture) flushRun();
                runTexture = g.texture;
                const irr::s32 x = ox + gp.dx;
                quads_.push_back({{x, oy, x + g.src.getWidth(), oy + g.src.getHeight()}, g.src, tag.color_});
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
