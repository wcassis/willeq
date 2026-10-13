#pragma once

// B888-CC1 (#204). EQUI_FacePick.xml (stock Titanium) defines the 175x215
// FacePickWindow and its 24 child rects below. At 1280x800 the official
// client places it on the left, vertically centred (oracle/08_human_facepick.png);
// the XML's top-level (0,80) is not its runtime position. The rounded title
// frame puts the child origin 4 px right and 16 px down from the window edge.

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

struct FacePickLayout {
    using Rect = irr::core::rect<irr::s32>;

    static constexpr int kWindowW = 175, kWindowH = 215;
    static constexpr int kXmlX = 0, kXmlY = 80;
    static constexpr int kClientX = 4, kClientY = 16;
    static constexpr int kRows = 7;

    static Rect make(int x, int y, int w, int h) { return Rect(x, y, x + w, y + h); }
    static Rect windowRect(int, int sh) { return make(0, (sh - kWindowH) / 2, kWindowW, kWindowH); }
    static Rect child(int sw, int sh, int x, int y, int w, int h) {
        const Rect p = windowRect(sw, sh);
        return make(p.UpperLeftCorner.X + kClientX + x,
                    p.UpperLeftCorner.Y + kClientY + y, w, h);
    }

    // FPW_{Face,Hair,Hair_Color,Eyes,Beard,Beard_Color,Wode}_{Down,Text,Up}.
    static const char* rowLabel(int row) {
        static const char* const labels[kRows] = {
            "FACE", "HAIR", "HAIR COLOR", "EYES", "BEARD", "BEARD COLOR", "WOAD"
        };
        return labels[row];
    }
    static Rect downRect(int sw, int sh, int row) {
        return child(sw, sh, 15, 4 + 20 * row, 22, 12);
    }
    static Rect textRect(int sw, int sh, int row) {
        return child(sw, sh, row == 5 ? 40 : 45, 2 + 20 * row,
                     row == 5 ? 90 : 80, 16);
    }
    static Rect upRect(int sw, int sh, int row) {
        return child(sw, sh, 133, 4 + 20 * row, 22, 12);
    }
    static Rect randomRect(int sw, int sh) { return child(sw, sh, 50, 144, 64, 20); }
    static Rect acceptRect(int sw, int sh) { return child(sw, sh, 15, 170, 64, 20); }
    static Rect cancelRect(int sw, int sh) { return child(sw, sh, 89, 170, 64, 20); }
};

}  // namespace Graphics
}  // namespace EQT
