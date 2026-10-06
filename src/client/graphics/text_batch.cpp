/*
 * TextBatch implementation.
 * U01: Batched text renderer using Irrlicht's built-in font atlas.
 */

#include "client/graphics/text_batch.h"
#include "common/logging.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

bool TextBatch::init(irr::gui::IGUIEnvironment* guienv) {
    if (!guienv) return false;

    irr::gui::IGUIFont* font = guienv->getBuiltInFont();
    if (!font) {
        LOG_ERROR(MOD_GRAPHICS, "TextBatch: built-in font not available");
        return false;
    }

    lineHeight_ = static_cast<irr::s32>(font->getDimension(L"Aj").Height);

    // Extract per-glyph advance widths for measurement
    for (wchar_t c = 32; c < 127; ++c) {
        wchar_t str[2] = {c, 0};
        auto dim = font->getDimension(str);
        glyphAdvance_[c] = static_cast<irr::s32>(dim.Width);
    }

    font_ = font;

    LOG_INFO(MOD_GRAPHICS, "TextBatch initialized: lineHeight={}, {} glyphs",
        lineHeight_, glyphAdvance_.size());
    return true;
}

void TextBatch::begin() {
    entries_.clear();
}

void TextBatch::addText(const std::string& text, irr::s32 x, irr::s32 y,
                        irr::video::SColor color) {
    if (text.empty()) return;
    entries_.push_back({std::wstring(text.begin(), text.end()), x, y, color, false, false});
}

void TextBatch::addTextW(const wchar_t* text, irr::s32 x, irr::s32 y,
                         irr::video::SColor color) {
    if (!text || !text[0]) return;
    entries_.push_back({text, x, y, color, false, false});
}

void TextBatch::addTextCentered(const std::string& text,
                                const irr::core::rect<irr::s32>& rect,
                                irr::video::SColor color) {
    if (text.empty()) return;
    std::wstring wtext(text.begin(), text.end());
    irr::s32 tw = getTextWidthW(wtext.c_str());
    irr::s32 x = rect.UpperLeftCorner.X + (rect.getWidth() - tw) / 2;
    irr::s32 y = rect.UpperLeftCorner.Y + (rect.getHeight() - lineHeight_) / 2;
    entries_.push_back({std::move(wtext), x, y, color, false, false});
}

void TextBatch::addText3D(const std::string& text,
                          const irr::core::vector3df& worldPos,
                          irr::scene::ICameraSceneNode* camera,
                          irr::video::IVideoDriver* driver,
                          irr::video::SColor color) {
    if (text.empty() || !camera || !driver) return;

    // Project world position to screen coordinates
    const irr::core::dimension2du& screenSize = driver->getScreenSize();
    irr::core::matrix4 viewProj = camera->getProjectionMatrix();
    viewProj *= camera->getViewMatrix();

    irr::f32 transformedPos[4];
    transformedPos[0] = worldPos.X;
    transformedPos[1] = worldPos.Y;
    transformedPos[2] = worldPos.Z;
    transformedPos[3] = 1.0f;

    viewProj.multiplyWith1x4Matrix(transformedPos);

    // Behind camera — don't draw
    if (transformedPos[3] <= 0.0f) return;

    irr::f32 invW = 1.0f / transformedPos[3];
    irr::f32 ndcX = transformedPos[0] * invW;
    irr::f32 ndcY = transformedPos[1] * invW;

    // NDC to screen
    irr::s32 screenX = static_cast<irr::s32>((ndcX * 0.5f + 0.5f) * screenSize.Width);
    irr::s32 screenY = static_cast<irr::s32>((1.0f - (ndcY * 0.5f + 0.5f)) * screenSize.Height);

    // Center text horizontally on the projected point
    std::wstring wtext(text.begin(), text.end());
    irr::s32 tw = getTextWidthW(wtext.c_str());
    screenX -= tw / 2;

    entries_.push_back({std::move(wtext), screenX, screenY, color, false, false});
}

// T462 (board B506): the glyph table the batched flush lays text out from is
// BitmapGlyphTable (client/graphics/ui/bitmap_glyph_table.h; B975 moved it
// there so the entity name-tag batch shares it). A string the table cannot
// reproduce exactly -- or whose glyphs span two textures -- goes to
// font_->draw() instead.

void TextBatch::flush(irr::video::IVideoDriver* driver) {
    flushRange(driver, 0, entries_.size());
    entries_.clear();
}

bool TextBatch::entryIntersects(size_t i, const irr::core::rect<irr::s32>& r) const {
    if (!font_ || i >= entries_.size()) return false;
    const TextEntry& e = entries_[i];
    if (e.extentW < 0) {
        const auto dim = font_->getDimension(e.text.c_str());
        e.extentW = static_cast<irr::s32>(dim.Width);
        e.extentH = std::max(static_cast<irr::s32>(dim.Height), lineHeight_);
    }
    const irr::core::rect<irr::s32> extent(e.x, e.y, e.x + e.extentW, e.y + e.extentH);
    return extent.isRectCollided(r);
}

void TextBatch::flushRange(irr::video::IVideoDriver* driver, size_t begin, size_t end) {
    lastFlushDriverDraws_ = 0;
    end = std::min(end, entries_.size());
    if (!font_ || begin >= end) return;

    auto drawEntry = [&](const TextEntry& entry) {
        irr::core::rect<irr::s32> rect(
            entry.x, entry.y,
            entry.x + 2000,  // Wide enough — font clips naturally
            entry.y + lineHeight_);
        font_->draw(entry.text.c_str(), rect, entry.color);
        ++lastFlushDriverDraws_;
    };

    if (batchingEnabled_ && !glyphTableBuilt_) {
        glyphTableBuilt_ = true;
        glyphTableUsable_ = glyphs_.build(font_);
    }
    if (!batchingEnabled_ || !glyphTableUsable_ || !UiQuadBatch::driverSupportsBatching(driver)) {
        // The pre-T462 flush: one font draw per entry.
        for (size_t i = begin; i < end; ++i) drawEntry(entries_[i]);
        return;
    }

    // T462: the glyphs of consecutive plain entries go into one texture run,
    // in entry order and left to right within an entry -- the order the
    // per-entry draws rasterised them in.
    irr::video::ITexture* runTexture = nullptr;
    glyphRun_.clear();
    auto flushRun = [&]() {
        if (glyphRun_.empty()) return;
        lastFlushDriverDraws_ += quadBatch_.drawTexturedRun(driver, runTexture, glyphRun_, true);
        glyphRun_.clear();
    };

    for (size_t i = begin; i < end; ++i) {
        const TextEntry& entry = entries_[i];
        bool plain = true;
        irr::video::ITexture* entryTexture = nullptr;
        for (wchar_t c : entry.text) {
            const BitmapGlyphTable::Glyph& g = glyphs_.glyph(c);
            if (!g.batchable) { plain = false; break; }
            if (!g.drawn) continue;
            if (entryTexture && g.texture != entryTexture) { plain = false; break; }
            entryTexture = g.texture;
        }
        if (!plain) {
            flushRun();       // keep everything queued before it underneath it
            drawEntry(entry);
            continue;
        }

        irr::s32 penX = entry.x;
        for (wchar_t c : entry.text) {
            const BitmapGlyphTable::Glyph& g = glyphs_.glyph(c);
            penX += g.underhang;
            if (g.drawn) {
                if (runTexture && g.texture != runTexture) flushRun();
                runTexture = g.texture;
                glyphRun_.push_back({{penX, entry.y, penX + g.src.getWidth(), entry.y + g.src.getHeight()},
                                     g.src, entry.color});
            }
            penX += g.advanceAfter;
        }
    }
    flushRun();
}

irr::s32 TextBatch::getTextWidth(const std::string& text) const {
    std::wstring wtext(text.begin(), text.end());
    return getTextWidthW(wtext.c_str());
}

irr::s32 TextBatch::getTextWidthW(const wchar_t* text) const {
    if (!font_ || !text) return 0;
    auto dim = font_->getDimension(text);
    return static_cast<irr::s32>(dim.Width);
}

size_t TextBatch::getQuadCount() const {
    return entries_.size();
}

} // namespace Graphics
} // namespace EQT
