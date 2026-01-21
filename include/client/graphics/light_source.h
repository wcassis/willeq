#ifndef CLIENT_GRAPHICS_LIGHT_SOURCE_H
#define CLIENT_GRAPHICS_LIGHT_SOURCE_H

#include <cstdint>

namespace EQT {
namespace Graphics {
namespace lightsource {

// Light types sent by the server (0-15)
// These classify the type of light source
enum LightType : uint8_t {
    LightTypeNone = 0,
    LightTypeCandle,              // 1
    LightTypeTorch,               // 2
    LightTypeTinyGlowingSkull,    // 3
    LightTypeSmallLantern,        // 4
    LightTypeSteinOfMoggok,       // 5
    LightTypeLargeLantern,        // 6
    LightTypeFlamelessLantern,    // 7
    LightTypeGlobeOfStars,        // 8
    LightTypeLightGlobe,          // 9
    LightTypeLightstone,          // 10
    LightTypeGreaterLightstone,   // 11
    LightTypeFireBeetleEye,       // 12
    LightTypeColdlight,           // 13
    LightTypeUnknown1,            // 14
    LightTypeUnknown2,            // 15
    LightTypeCount
};

// Light levels (intensities) - used for rendering calculations
enum LightLevel : uint8_t {
    LightLevelUnlit = 0,
    LightLevelCandle,       // 1
    LightLevelTorch,        // 2
    LightLevelSmallMagic,   // 3
    LightLevelRedLight,     // 4
    LightLevelBlueLight,    // 5
    LightLevelSmallLantern, // 6
    LightLevelMagicLantern, // 7
    LightLevelLargeLantern, // 8
    LightLevelLargeMagic,   // 9
    LightLevelBrilliant,    // 10
    LightLevelCount
};

// Convert a light type to its corresponding light level (intensity)
inline uint8_t TypeToLevel(uint8_t light_type) {
    switch (light_type) {
    case LightTypeGlobeOfStars:
        return LightLevelBrilliant;      // 10
    case LightTypeFlamelessLantern:
    case LightTypeGreaterLightstone:
        return LightLevelLargeMagic;     // 9
    case LightTypeLargeLantern:
        return LightLevelLargeLantern;   // 8
    case LightTypeSteinOfMoggok:
    case LightTypeLightstone:
        return LightLevelMagicLantern;   // 7
    case LightTypeSmallLantern:
        return LightLevelSmallLantern;   // 6
    case LightTypeColdlight:
    case LightTypeUnknown2:
        return LightLevelBlueLight;      // 5
    case LightTypeFireBeetleEye:
    case LightTypeUnknown1:
        return LightLevelRedLight;       // 4
    case LightTypeTinyGlowingSkull:
    case LightTypeLightGlobe:
        return LightLevelSmallMagic;     // 3
    case LightTypeTorch:
        return LightLevelTorch;          // 2
    case LightTypeCandle:
        return LightLevelCandle;         // 1
    default:
        return LightLevelUnlit;          // 0
    }
}

} // namespace lightsource
} // namespace Graphics
} // namespace EQT

#endif // CLIENT_GRAPHICS_LIGHT_SOURCE_H
