#include "client/graphics/vision_ambient.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

AmbientRGB visionAmbientFloor(VisionType vision) {
    switch (vision) {
        case VisionType::Ultravision:
            return AmbientRGB{kUltravisionAmbientGain * kUltravisionTint[0],
                              kUltravisionAmbientGain * kUltravisionTint[1],
                              kUltravisionAmbientGain * kUltravisionTint[2]};
        case VisionType::Infravision:
            return AmbientRGB{kInfravisionAmbientGain * kInfravisionTint[0],
                              kInfravisionAmbientGain * kInfravisionTint[1],
                              kInfravisionAmbientGain * kInfravisionTint[2]};
        case VisionType::Normal:
        default:
            return AmbientRGB{0.0f, 0.0f, 0.0f};
    }
}

VisionType racialBaseVision(uint16_t raceId) {
    switch (raceId) {
        case 6:   // Dark Elf -- the only Ultravision race the oracle gives
            return VisionType::Ultravision;
        case 4:   // Wood Elf
        case 5:   // High Elf
        case 7:   // Half Elf
        case 8:   // Dwarf
        case 9:   // Troll
        case 10:  // Ogre
        case 11:  // Halfling
        case 12:  // Gnome
        case 128: // Iksar
        case 130: // Vah Shir
            return VisionType::Infravision;
        default:  // Human (1), Barbarian (2), Erudite (3) -- all measured
                  // Normal; Froglok (330) measured a fourth level, see B060
            return VisionType::Normal;
    }
}

AmbientRGB applyVisionAmbientFloor(VisionType vision, AmbientRGB base) {
    const AmbientRGB floor = visionAmbientFloor(vision);
    return AmbientRGB{std::max(base.r, floor.r),
                      std::max(base.g, floor.g),
                      std::max(base.b, floor.b)};
}

AmbientRGB composeSceneAmbient(AmbientRGB dayNight, float weatherModifier,
                               VisionType vision) {
    // Weather is not clamped here: a lightning flash deliberately overshoots
    // 1.0. (T424 removed the user ambient multiplier and its per-channel
    // clamp -- see the header for why.)
    AmbientRGB out{dayNight.r, dayNight.g, dayNight.b};
    out.r *= weatherModifier;
    out.g *= weatherModifier;
    out.b *= weatherModifier;
    // Floor LAST -- see the header. Nothing may darken the world below what
    // the vision buff grants.
    return applyVisionAmbientFloor(vision, out);
}

}  // namespace Graphics
}  // namespace EQT
