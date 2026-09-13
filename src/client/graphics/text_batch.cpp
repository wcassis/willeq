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

// T462 (board B506): the glyph table the batched flush lays text out from.
//
// CGUIFont::draw() (the built-in font is a CGUIFont, an IGUIFontBitmap) walks
// the string with a pen: for each character it adds the character's
// underhang, queues its sprite at the pen unless the character is in the
// font's invisible set, then adds width + overhang + the global kerning; its
// sprite bank then issues one draw2DImageBatch per texture. Every one of those
// numbers is reachable through the public font API, so the table is taken
// from the font itself rather than re-derived:
//   underhang(c)   = getKerningWidth(p, &c) - getKerningWidth(p, nullptr)
//                    (getKerningWidth adds the PREVIOUS letter's underhang)
//   total advance  = getDimension("c").Width (underhang + width + overhang +
//                    kerning, exactly CGUIFont::getDimension's per-char sum)
//   sprite         = getSpriteBank()->getSprites()[getSpriteNoFromChar(&c)]
// The invisible set is CGUIFont's constructor default, L" " -- nothing in the
// tree calls setInvisibleCharacters() on the built-in font. Anything the
// table cannot reproduce exactly (a line break, a character outside 0..127, an
// animated sprite, a sprite the bank would abort the string on, a string whose
// glyphs span two textures) marks the entry for font_->draw() instead.
bool TextBatch::buildGlyphTable() {
    glyphTableBuilt_ = true;
    if (!font_ || font_->getType() != irr::gui::EGFT_BITMAP) return false;
    auto* bitmapFont = static_cast<irr::gui::IGUIFontBitmap*>(font_);
    irr::gui::IGUISpriteBank* bank = bitmapFont->getSpriteBank();
    if (!bank) return false;
    auto& sprites = bank->getSprites();
    auto& positions = bank->getPositions();

    const wchar_t probe = L'A';
    const irr::s32 kerningBase = font_->getKerningWidth(&probe, nullptr);
    for (size_t i = 0; i < kGlyphTableSize; ++i) {
        const wchar_t c = static_cast<wchar_t>(i);
        Glyph g;
        if (c == L'\0' || c == L'\r' || c == L'\n') {
            glyphs_[i] = g;  // not batchable: line breaks move the pen by MaxHeight
            continue;
        }
        const wchar_t one[2] = {c, L'\0'};
        g.underhang = font_->getKerningWidth(&probe, &c) - kerningBase;
        g.advanceAfter = static_cast<irr::s32>(font_->getDimension(one).Width) - g.underhang;
        g.drawn = (c != L' ');
        if (g.drawn) {
            const irr::u32 spriteNo = bitmapFont->getSpriteNoFromChar(&c);
            if (spriteNo >= sprites.size() || sprites[spriteNo].Frames.empty()) {
                g.drawn = false;  // CGUISpriteBank::draw2DSpriteBatch skips it
            } else if (sprites[spriteNo].frameTime != 0) {
                return false;     // an animated font: frames depend on the clock
            } else {
                const irr::gui::SGUISpriteFrame& frame = sprites[spriteNo].Frames[0];
                if (frame.rectNumber >= positions.size()) {
                    glyphs_[i] = g;  // the sprite bank aborts the string here
                    continue;
                }
                g.src = positions[frame.rectNumber];
                g.texture = bank->getTexture(frame.textureNumber);
                if (!g.texture) g.drawn = false;  // draw2DImageBatch(nullptr) draws nothing
            }
        }
        g.batchable = true;
        glyphs_[i] = g;
    }
    return true;
}

void TextBatch::flush(irr::video::IVideoDriver* driver) {
    lastFlushDriverDraws_ = 0;
    if (!font_ || entries_.empty()) return;

    auto drawEntry = [&](const TextEntry& entry) {
        irr::core::rect<irr::s32> rect(
            entry.x, entry.y,
            entry.x + 2000,  // Wide enough — font clips naturally
            entry.y + lineHeight_);
        font_->draw(entry.text.c_str(), rect, entry.color);
        ++lastFlushDriverDraws_;
    };

    if (batchingEnabled_ && !glyphTableBuilt_) glyphTableUsable_ = buildGlyphTable();
    if (!batchingEnabled_ || !glyphTableUsable_ || !UiQuadBatch::driverSupportsBatching(driver)) {
        // The pre-T462 flush: one font draw per entry.
        for (const auto& entry : entries_) drawEntry(entry);
        entries_.clear();
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

    for (const auto& entry : entries_) {
        bool plain = true;
        irr::video::ITexture* entryTexture = nullptr;
        for (wchar_t c : entry.text) {
            const auto code = static_cast<uint32_t>(c);
            if (code >= kGlyphTableSize || !glyphs_[code].batchable) { plain = false; break; }
            const Glyph& g = glyphs_[code];
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
            const Glyph& g = glyphs_[static_cast<uint32_t>(c)];
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
