// sky_color_table.cpp - the time-of-day sky colour table and the arithmetic
// over it. See include/client/graphics/sky_color_table.h for what this is and
// what T495 fixed here.
//
// Moved out of SkyRenderer::calculateSkyColors() / interpolateSkyColors()
// (sky_renderer_colors.cpp) by T495; those two methods now delegate here and
// nothing else in the tree evaluates the table.
#include "client/graphics/sky_color_table.h"

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace {

// The anchors. THESE VALUES ARE UNCHANGED by T495 (board B781 owns whether
// they are the right values; this unit owns only the arithmetic between them).

// Night (0-4): Dark blue/black sky
const SkyColorSet NIGHT = {
    irr::video::SColor(255, 10, 10, 30),     // zenith: deep blue-black
    irr::video::SColor(255, 20, 20, 50),     // horizon: slightly lighter
    irr::video::SColor(255, 15, 15, 35),     // fog: dark blue
    0.0f,                                     // sunIntensity
    0.2f                                      // cloudBrightness: dim
};

// Pre-dawn (4-5): Very early light
const SkyColorSet PRE_DAWN = {
    irr::video::SColor(255, 25, 25, 60),     // zenith: dark blue
    irr::video::SColor(255, 60, 40, 50),     // horizon: hint of color
    irr::video::SColor(255, 35, 30, 45),     // fog
    0.05f,
    0.3f
};

// Dawn (5-6): Orange/pink gradient, sun rising
const SkyColorSet DAWN = {
    irr::video::SColor(255, 80, 100, 150),   // zenith: lightening blue
    irr::video::SColor(255, 220, 140, 90),   // horizon: orange-pink
    irr::video::SColor(255, 140, 110, 100),  // fog: warm
    0.3f,
    0.6f
};

// Sunrise (6-7): Golden hour
const SkyColorSet SUNRISE = {
    irr::video::SColor(255, 130, 160, 200),  // zenith: light blue
    irr::video::SColor(255, 255, 180, 100),  // horizon: golden
    irr::video::SColor(255, 180, 160, 140),  // fog: warm golden
    0.6f,
    0.8f
};

// Morning (7-10): Transitioning to day
const SkyColorSet MORNING = {
    irr::video::SColor(255, 140, 180, 220),  // zenith: bright blue
    irr::video::SColor(255, 200, 210, 220),  // horizon: pale blue
    irr::video::SColor(255, 180, 195, 210),  // fog: light blue
    0.85f,
    0.95f
};

// Day (10-16): Full daylight
const SkyColorSet DAY = {
    irr::video::SColor(255, 100, 150, 220),  // zenith: sky blue
    irr::video::SColor(255, 180, 200, 220),  // horizon: pale
    irr::video::SColor(255, 160, 180, 200),  // fog: bluish
    1.0f,
    1.0f
};

// Afternoon (16-17): Slightly warmer
const SkyColorSet AFTERNOON = {
    irr::video::SColor(255, 120, 160, 210),  // zenith: warm blue
    irr::video::SColor(255, 210, 200, 190),  // horizon: hint of warmth
    irr::video::SColor(255, 175, 180, 190),  // fog
    0.9f,
    1.0f
};

// Sunset (17-18): Golden hour
const SkyColorSet SUNSET = {
    irr::video::SColor(255, 130, 130, 180),  // zenith: purple-blue
    irr::video::SColor(255, 255, 160, 80),   // horizon: orange-gold
    irr::video::SColor(255, 180, 140, 120),  // fog: warm
    0.6f,
    0.8f
};

// Dusk (18-19): Orange/red gradient, sun setting
const SkyColorSet DUSK = {
    irr::video::SColor(255, 80, 60, 120),    // zenith: purple
    irr::video::SColor(255, 200, 100, 60),   // horizon: red-orange
    irr::video::SColor(255, 120, 80, 80),    // fog: reddish
    0.25f,
    0.5f
};

// Twilight (19-20): Transitioning to night
const SkyColorSet TWILIGHT = {
    irr::video::SColor(255, 40, 40, 80),     // zenith: dark blue-purple
    irr::video::SColor(255, 80, 50, 60),     // horizon: fading color
    irr::video::SColor(255, 50, 40, 55),     // fog: dark
    0.1f,
    0.35f
};

// PRE-T495 ARITHMETIC, MOVED VERBATIM. This is the defect; the next commit
// replaces it. Kept for exactly one commit so the red tests show the bug.
irr::u32 lerpChannel(irr::u32 a, irr::u32 b, float t) {
    return static_cast<irr::u32>(a + (b - a) * t);
}

irr::video::SColor lerpColor(const irr::video::SColor& a, const irr::video::SColor& b, float t) {
    return irr::video::SColor(
        255,
        lerpChannel(a.getRed(), b.getRed(), t),
        lerpChannel(a.getGreen(), b.getGreen(), t),
        lerpChannel(a.getBlue(), b.getBlue(), t));
}

}  // namespace

SkyColorSegment SkyColorSegmentAt(float timeOfDay) {
    if (timeOfDay < 4.0f) {
        return {&NIGHT, &NIGHT, 0.0f};                          // Night (0-4)
    } else if (timeOfDay < 5.0f) {
        return {&NIGHT, &PRE_DAWN, timeOfDay - 4.0f};           // Pre-dawn (4-5)
    } else if (timeOfDay < 6.0f) {
        return {&PRE_DAWN, &DAWN, timeOfDay - 5.0f};            // Dawn (5-6)
    } else if (timeOfDay < 7.0f) {
        return {&DAWN, &SUNRISE, timeOfDay - 6.0f};             // Sunrise (6-7)
    } else if (timeOfDay < 10.0f) {
        return {&SUNRISE, &MORNING, (timeOfDay - 7.0f) / 3.0f}; // Morning (7-10)
    } else if (timeOfDay < 16.0f) {
        if (timeOfDay < 11.0f) {
            return {&MORNING, &DAY, timeOfDay - 10.0f};         // (10-11)
        }
        return {&DAY, &DAY, 0.0f};                              // Day (11-16)
    } else if (timeOfDay < 17.0f) {
        return {&DAY, &AFTERNOON, timeOfDay - 16.0f};           // Afternoon (16-17)
    } else if (timeOfDay < 18.0f) {
        return {&AFTERNOON, &SUNSET, timeOfDay - 17.0f};        // Sunset (17-18)
    } else if (timeOfDay < 19.0f) {
        return {&SUNSET, &DUSK, timeOfDay - 18.0f};             // Dusk (18-19)
    } else if (timeOfDay < 20.0f) {
        return {&DUSK, &TWILIGHT, timeOfDay - 19.0f};           // Twilight (19-20)
    } else if (timeOfDay < 21.0f) {
        return {&TWILIGHT, &NIGHT, timeOfDay - 20.0f};          // To night (20-21)
    }
    return {&NIGHT, &NIGHT, 0.0f};                              // Night (21-24)
}

SkyColorSet LerpSkyColors(const SkyColorSet& a, const SkyColorSet& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);

    SkyColorSet result;
    result.zenith = lerpColor(a.zenith, b.zenith, t);
    result.horizon = lerpColor(a.horizon, b.horizon, t);
    result.fog = lerpColor(a.fog, b.fog, t);
    result.sunIntensity = a.sunIntensity + (b.sunIntensity - a.sunIntensity) * t;
    result.cloudBrightness = a.cloudBrightness + (b.cloudBrightness - a.cloudBrightness) * t;
    return result;
}

SkyColorSet SkyColorsAt(float timeOfDay) {
    const SkyColorSegment seg = SkyColorSegmentAt(timeOfDay);
    return LerpSkyColors(*seg.a, *seg.b, seg.t);
}

irr::video::SColor SkyLayerTint(const SkyColorSet& colors, float effectiveBrightness) {
    // The zenith/horizon average simulates the sky gradient. Both channels are
    // 0-255 and the sum cannot wrap, so this addition was never part of the
    // defect; it is here so the tint has ONE definition.
    const irr::u32 r = (colors.zenith.getRed() + colors.horizon.getRed()) / 2;
    const irr::u32 g = (colors.zenith.getGreen() + colors.horizon.getGreen()) / 2;
    const irr::u32 b = (colors.zenith.getBlue() + colors.horizon.getBlue()) / 2;

    const float scale = std::clamp(effectiveBrightness, 0.0f, 1.0f);
    return irr::video::SColor(
        255,
        static_cast<irr::u32>(static_cast<float>(r) * scale),
        static_cast<irr::u32>(static_cast<float>(g) * scale),
        static_cast<irr::u32>(static_cast<float>(b) * scale));
}

irr::video::SColor SkyClearColorFromTint(irr::video::SColor tint) {
    // A darker version of the tint, which shows through the semi-transparent
    // sky at night.
    return irr::video::SColor(255, tint.getRed() / 2, tint.getGreen() / 2,
                              tint.getBlue() / 2);
}

}  // namespace Graphics
}  // namespace EQT
