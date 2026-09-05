#include "client/graphics/vision_ambient.h"

#include <algorithm>
#include <cmath>

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

AmbientRGB visionFrameRatios(VisionType vision, AmbientRGB base) {
    const AmbientRGB lit = applyVisionAmbientFloor(vision, base);
    const auto ratio = [](float top, float bottom) {
        if (bottom <= 0.0f) return 1.0f;
        return std::pow(top / bottom, kVisionFrameExponent);
    };
    return AmbientRGB{ratio(lit.r, base.r), ratio(lit.g, base.g),
                      ratio(lit.b, base.b)};
}

AmbientRGB applyVisionAmbientFloor(VisionType vision, AmbientRGB base) {
    const AmbientRGB floor = visionAmbientFloor(vision);
    return AmbientRGB{std::max(base.r, floor.r),
                      std::max(base.g, floor.g),
                      std::max(base.b, floor.b)};
}

}  // namespace Graphics
}  // namespace EQT
