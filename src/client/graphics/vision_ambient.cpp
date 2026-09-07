#include "client/graphics/vision_ambient.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

AmbientRGB dayNightAmbient(uint8_t hour, uint8_t minute, AmbientRGB night,
                           float& sunIntensity) {
    // Byte-for-byte the four-armed if-chain that lived in
    // IrrlichtRenderer::updateTimeOfDay() before T428, with the night triple
    // taken from `night` in all three arms that name it (the night arm and
    // both ramp endpoints) instead of being written out three times.
    if (hour >= 7 && hour <= 17) {
        sunIntensity = kDaySunIntensity;
        return AmbientRGB{kDayAmbient[0], kDayAmbient[1], kDayAmbient[2]};
    }
    if (hour >= 20 || hour <= 4) {
        sunIntensity = kNightSunIntensity;
        return night;
    }
    if (hour >= 5 && hour <= 6) {
        // Dawn -- night to day.
        float t = (static_cast<float>(hour) - 5.0f) + minute / 60.0f;
        t /= 2.0f;
        sunIntensity = kNightSunIntensity + t * (kDaySunIntensity - kNightSunIntensity);
        return AmbientRGB{night.r + t * (kDawnDuskDayAnchor[0] - night.r),
                          night.g + t * (kDawnDuskDayAnchor[1] - night.g),
                          night.b + t * (kDawnDuskDayAnchor[2] - night.b)};
    }
    // Dusk (18-19) -- day to night.
    float t = (static_cast<float>(hour) - 18.0f) + minute / 60.0f;
    t /= 2.0f;
    sunIntensity = kDaySunIntensity - t * (kDaySunIntensity - kNightSunIntensity);
    return AmbientRGB{kDawnDuskDayAnchor[0] - t * (kDawnDuskDayAnchor[0] - night.r),
                      kDawnDuskDayAnchor[1] - t * (kDawnDuskDayAnchor[1] - night.g),
                      kDawnDuskDayAnchor[2] - t * (kDawnDuskDayAnchor[2] - night.b)};
}

AmbientRGB visionAmbientFloor(VisionType vision) {
    switch (vision) {
        case VisionType::Ultravision:
            return AmbientRGB{kUltravisionAmbientGain * kUltravisionTint[0],
                              kUltravisionAmbientGain * kUltravisionTint[1],
                              kUltravisionAmbientGain * kUltravisionTint[2]};
        case VisionType::FroglokVision:
            return AmbientRGB{kFroglokVisionAmbientGain * kFroglokVisionTint[0],
                              kFroglokVisionAmbientGain * kFroglokVisionTint[1],
                              kFroglokVisionAmbientGain * kFroglokVisionTint[2]};
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
        case 330: // Froglok -- the measured FOURTH level (T361 found it, T428
                  // measured it per channel and gave the enum a value for it;
                  // Will's ruling, board B060)
            return VisionType::FroglokVision;
        default:  // Human (1), Barbarian (2), Erudite (3) -- all measured Normal
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
