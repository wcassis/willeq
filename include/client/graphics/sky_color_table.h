// sky_color_table.h - the time-of-day sky colour table, as pure functions.
//
// T495 (board B789/B790). Extracted from SkyRenderer so the table and the
// arithmetic over it can be driven with no device, no assets and no scene
// manager: the defect these functions carry the fix for reached three
// consumers (the dome vertex tint, the clear colour and the fog colour) and
// none of them could be tested without a GLES2 context while the arithmetic
// lived in a private method.
//
// The defect. Until T495 every channel was lerped as
//     a.getRed() + (b.getRed() - a.getRed()) * t
// and `SColor::getRed()` returns `irr::u32`. On every ramp where a channel
// DECREASES -- and the table has one in almost every segment -- the
// subtraction wrapped to ~4.29e9, `* t` in float zeroed the low bits (a
// 24-bit mantissa cannot hold a 32-bit integer), and the `& 0xff` inside
// SColor's constructor left 0, 64, 128 or 192. MEASURED: at 17:25 the dome
// tint was (161,58,58) and the clear colour (80,29,29) -- the flat dark red
// filling T492's qeytoqrg sky AND tree bands; at 16:45 the fog was
// (171,180,0), blue exactly zero, which `setFog()` then mixed every fogged
// pixel toward; at 18:30 all three anchors collapsed to (0,0,0).
//
// The table's VALUES are not settled -- whether these anchors match the
// official client is board B781, a separate, live question. These functions
// only guarantee that a colour between two anchors is BETWEEN them.
#ifndef EQT_GRAPHICS_SKY_COLOR_TABLE_H
#define EQT_GRAPHICS_SKY_COLOR_TABLE_H

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

// Sky color set for a specific time of day
struct SkyColorSet {
    irr::video::SColor zenith;     // Top of sky dome color
    irr::video::SColor horizon;    // Horizon color (blends with zenith)
    irr::video::SColor fog;        // Recommended fog color
    float sunIntensity;            // Sun brightness (0.0 - 1.0)
    float cloudBrightness;         // Cloud layer brightness (0.0 - 1.0)
};

// The two anchors a time of day sits between, and where between them it sits.
// `a` and `b` reference the table's own statics and are never null; on a
// constant arm (night, full day) they are the same set and `t` is 0.
struct SkyColorSegment {
    const SkyColorSet* a = nullptr;
    const SkyColorSet* b = nullptr;
    float t = 0.0f;
};

// Which anchors govern `timeOfDay` (decimal hours, 0-24).
SkyColorSegment SkyColorSegmentAt(float timeOfDay);

// Per-channel linear interpolation in float, rounded and clamped to 0-255.
// t is clamped to 0-1.
SkyColorSet LerpSkyColors(const SkyColorSet& a, const SkyColorSet& b, float t);

// The table evaluated at `timeOfDay` -- SkyColorSegmentAt() fed to
// LerpSkyColors().
SkyColorSet SkyColorsAt(float timeOfDay);

// The dome's vertex tint: the zenith/horizon average scaled by the effective
// brightness (cloudBrightness * the weather brightness). This is what
// SkyRenderer::updateSkyLayerColors() writes into the dome, before the GLES2
// channel-order compensation (gles2_vertex_color.h) is applied to it.
irr::video::SColor SkyLayerTint(const SkyColorSet& colors, float effectiveBrightness);

// The scene clear colour SkyRenderer derives from that tint.
irr::video::SColor SkyClearColorFromTint(irr::video::SColor tint);

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_SKY_COLOR_TABLE_H
