/*
 * TextBatch implementation.
 * U01: Batched text renderer using Irrlicht's built-in font atlas.
 */

#include "client/graphics/text_batch.h"
#include "common/logging.h"

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

void TextBatch::flush(irr::video::IVideoDriver* driver) {
    if (!font_ || entries_.empty()) return;
    (void)driver;  // We use font_->draw() which internally uses the driver

    // Draw all entries. The font's internal draw2DImageBatch handles
    // per-string batching. Our improvement is that callers no longer
    // make individual font->draw() calls scattered across 20+ files —
    // they all go through this batch, enabling future optimization
    // (e.g., direct GL VBO with per-vertex color when we replace the
    // font with a custom atlas).
    for (const auto& entry : entries_) {
        irr::core::rect<irr::s32> rect(
            entry.x, entry.y,
            entry.x + 2000,  // Wide enough — font clips naturally
            entry.y + lineHeight_);
        font_->draw(entry.text.c_str(), rect, entry.color);
    }

    entries_.clear();
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
