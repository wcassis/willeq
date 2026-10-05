/*
 * The glyph table of Irrlicht's built-in bitmap font, laid out the way
 * CGUIFont::draw() lays it out -- so a caller can emit the same glyph quads
 * the font would, into its own batch.
 *
 * T462 (board B506) wrote this inside TextBatch for the static UI's text; B975
 * (GitLab #293) moved it here so the entity name-tag batch
 * (include/client/graphics/name_tag_batch.h) reads the SAME table instead of a
 * second copy of the layout rules.
 *
 * CGUIFont::draw() walks the string with a pen: for each character it adds the
 * character's underhang, queues its sprite at the pen unless the character is
 * in the font's invisible set, then adds width + overhang + the global kerning;
 * its sprite bank then issues one draw2DImageBatch per texture. Every one of
 * those numbers is reachable through the public font API, so the table is taken
 * from the font itself rather than re-derived:
 *   underhang(c)   = getKerningWidth(p, &c) - getKerningWidth(p, nullptr)
 *                    (getKerningWidth adds the PREVIOUS letter's underhang)
 *   total advance  = getDimension("c").Width (underhang + width + overhang +
 *                    kerning, exactly CGUIFont::getDimension's per-char sum)
 *   sprite         = getSpriteBank()->getSprites()[getSpriteNoFromChar(&c)]
 * The invisible set is CGUIFont's constructor default, L" " -- nothing in the
 * tree calls setInvisibleCharacters() on the built-in font. Anything the table
 * cannot reproduce exactly (a line break, a character outside 0..127, an
 * animated sprite, a sprite the bank would abort the string on) is marked not
 * batchable, and a caller sends a string containing it to font->draw().
 */

#pragma once

#include <irrlicht.h>
#include <cstddef>

namespace EQT {
namespace Graphics {

class BitmapGlyphTable {
public:
    struct Glyph {
        bool batchable = false;   // false: a string containing it goes to font->draw()
        bool drawn = false;       // false: advances the pen but draws nothing
        irr::s32 underhang = 0;   // pen offset before the glyph is placed
        irr::s32 advanceAfter = 0;// pen advance after it (width + overhang + kerning)
        irr::core::rect<irr::s32> src;
        irr::video::ITexture* texture = nullptr;
    };
    static constexpr size_t kSize = 128;

    /** Build from `font`. False (and every glyph unbatchable) when the font is
     *  not a bitmap font or is animated. */
    bool build(irr::gui::IGUIFont* font);

    bool usable() const { return usable_; }

    /** The glyph of `c`; a code point outside the table is an unbatchable glyph. */
    const Glyph& glyph(wchar_t c) const {
        const auto code = static_cast<unsigned long>(c);
        return code < kSize ? glyphs_[code] : unbatchable_;
    }

private:
    Glyph glyphs_[kSize];
    Glyph unbatchable_;
    bool usable_ = false;
};

} // namespace Graphics
} // namespace EQT
