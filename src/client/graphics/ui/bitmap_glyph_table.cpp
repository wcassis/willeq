/*
 * See include/client/graphics/ui/bitmap_glyph_table.h. Moved verbatim from
 * TextBatch::buildGlyphTable() (T462) by B975.
 */

#include "client/graphics/ui/bitmap_glyph_table.h"

namespace EQT {
namespace Graphics {

bool BitmapGlyphTable::build(irr::gui::IGUIFont* font) {
    usable_ = false;
    for (auto& g : glyphs_) g = Glyph();
    if (!font || font->getType() != irr::gui::EGFT_BITMAP) return false;
    auto* bitmapFont = static_cast<irr::gui::IGUIFontBitmap*>(font);
    irr::gui::IGUISpriteBank* bank = bitmapFont->getSpriteBank();
    if (!bank) return false;
    auto& sprites = bank->getSprites();
    auto& positions = bank->getPositions();

    const wchar_t probe = L'A';
    const irr::s32 kerningBase = font->getKerningWidth(&probe, nullptr);
    for (size_t i = 0; i < kSize; ++i) {
        const wchar_t c = static_cast<wchar_t>(i);
        Glyph g;
        if (c == L'\0' || c == L'\r' || c == L'\n') {
            glyphs_[i] = g;  // not batchable: line breaks move the pen by MaxHeight
            continue;
        }
        const wchar_t one[2] = {c, L'\0'};
        g.underhang = font->getKerningWidth(&probe, &c) - kerningBase;
        g.advanceAfter = static_cast<irr::s32>(font->getDimension(one).Width) - g.underhang;
        g.drawn = (c != L' ');
        if (g.drawn) {
            const irr::u32 spriteNo = bitmapFont->getSpriteNoFromChar(&c);
            if (spriteNo >= sprites.size() || sprites[spriteNo].Frames.empty()) {
                g.drawn = false;  // CGUISpriteBank::draw2DSpriteBatch skips it
            } else if (sprites[spriteNo].frameTime != 0) {
                for (auto& reset : glyphs_) reset = Glyph();
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
    usable_ = true;
    return true;
}

} // namespace Graphics
} // namespace EQT
