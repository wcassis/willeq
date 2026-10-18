// spellbook_backdrop.h -- the spellbook's book-ART source side: the four
// backdrop tiles and the Done "X" that tools/spellbook_backdrop_builder
// composes from the official client's own TGA art into ONE shipped BMP
// (data/textures/spellbook_backdrop.bmp). B1076 (GitLab #396).
//
// This header is the SOURCE/COMPOSITION map -- which TGA rectangle lands at
// which place on the composed 512x363 canvas. It is NOT the window geometry:
// where each of those four tiles is DRAWN inside the open window lives in
// spellbook_layout.h (backdropTopPageRect() .. backdropRightBottomRect()),
// and the two agree because both quote the same EQUI_SpellBookWnd.xml.
//
// WHY ONE COMPOSED TILE, NOT FOUR. The oracle's four StaticAnimations
// (SBW_SpellBook1..4, below) are four separate textures; fable ships the book
// art through the EQE1 pack one BMP per entry. A 256x256 page half would not
// fit the 256x256 ui_atlas, and four entries are four times the pack/mip
// cost for art that only ever appears as one book. So the builder composes
// them, with the official client's own per-tile source crops, into one
// 512x363 image; the renderer blits that one texture's four sub-rects (the
// spellbook_layout.h tile rects) into the window. Nothing is resampled --
// every pixel is copied from its TGA at its native size, so the output is
// bit-identical at -O0 and -O2 (board B268's rule).
//
// THE FOUR TILES (EQUI_SpellBookWnd.xml, window-relative, the oracle):
//   SBW_SpellBook1  (0,0)   256x256  -> Default_SpellBook1  default_spellbook01.tga (0,0) 256x256
//   SBW_SpellBook2  (256,0) 256x256  -> Default_SpellBook2  default_spellbook02.tga (0,0) 256x256
//   SBW_SpellBook3  (0,256) 256x107  -> Default_SpellBook3  default_spellbook03.tga (0,1) 256x107
//   SBW_SpellBook4  (256,256)256x107  -> Default_SpellBook4  default_spellbook04.tga (1,1) 255x107
// The window is 510x360, so the composed 512x363 canvas is clipped 2 px on
// the right and 3 px on the bottom by the window itself (spellbook_layout.h's
// kWindowW/kWindowH) -- the renderer never draws past the window.
//
// THE SOURCE CROPS are the XML's own per-animation <Location>/<Size> for the
// bottom strips -- (0,1) 256x107 and (1,1) 255x107 -- which the official
// client uses to shave one px of the shared 128 px TGA row on the left edge
// and one px off the right strip's width. They are quoted here, not the
// full 256x128, because that is the art the window actually shows.
//
// THE DONE "X" (Default_SpellBookClose) is a 13x13 piece of
// default_spellbook05.tga at source (54,35), drawn at the window's
// SBW_DoneButton position (449,5). It is composited onto the SAME canvas so
// the renderer needs one texture for the whole book chrome.
//
// ALL-OR-NOTHING: a source TGA that will not read, is not the size this
// header says, or decodes with no alpha in a tile it must carry is a hard
// exit 1 -- never a silently degraded backdrop (Will's standing EQE1 rule).

#pragma once

#include <cstddef>

namespace EQT {
namespace Graphics {
namespace SpellBookBackdrop {

// The composed canvas: 512x363. The window clips it to 510x360.
constexpr int kCanvasW = 512;
constexpr int kCanvasH = 363;

// The bottom strips are 107 px tall (the XML's <CY>); the two page halves
// above them are 256 px, so the canvas is 256 + 107 = 363.
constexpr int kHalfW = 256;
constexpr int kStripH = 107;

// One tile of the composed book art: the source TGA + its source crop +
// where that crop lands on the 512x363 canvas. Tile index 0..3 is the draw
// order the renderer uses (top-left, top-right, bottom-left, bottom-right),
// matching spellbook_layout.h's backdropTopPageRect() ..
// backdropRightBottomRect().
struct Tile {
    const char* source;  // TGA file name under uifiles/default/
    int srcX, srcY;      // source crop origin (the XML's <Location>)
    int srcW, srcH;      // source crop size   (the XML's <Size>)
    int dstX, dstY;      // placement on the composed canvas
};

constexpr Tile kTiles[4] = {
    // top-left page half
    {"default_spellbook01.tga", 0, 0, 256, 256, 0, 0},
    // top-right page half
    {"default_spellbook02.tga", 0, 0, 256, 256, 256, 0},
    // bottom-left control strip (1 px left crop, the XML's own)
    {"default_spellbook03.tga", 0, 1, 256, 107, 0, 256},
    // bottom-right control strip (1,1 origin, 255 px wide, the XML's own)
    {"default_spellbook04.tga", 1, 1, 255, 107, 256, 256},
};

// The expected source TGA size per tile index, checked before the crop --
// the official art is 256x256 for the two halves and 256x128 for the two
// strips, and a TGA that is not that is a wrong input, not a silent shrink.
inline int sourceWidth(int index) {
    return index < 2 ? 256 : 256;
}

inline int sourceHeight(int index) {
    return index < 2 ? 256 : 128;
}

// The Done "X" on the composed canvas: a 13x13 crop of
// default_spellbook05.tga at source (54,35), at the window's SBW_DoneButton
// position (449,5).
constexpr const char* kDoneSource = "default_spellbook05.tga";
constexpr int kDoneSrcX = 54;
constexpr int kDoneSrcY = 35;
constexpr int kDoneSize = 13;
constexpr int kDoneDstX = 449;
constexpr int kDoneDstY = 5;

// The shipped BMP's pack name (the EQE1 entry the renderer loads).
constexpr const char* kPackName = "spellbook_backdrop";

// The shipped BMP's file name under data/textures/.
constexpr const char* kShippedFile = "spellbook_backdrop.bmp";

}  // namespace SpellBookBackdrop
}  // namespace Graphics
}  // namespace EQT
