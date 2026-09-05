#ifndef EQT_GRAPHICS_VISION_TYPE_H
#define EQT_GRAPHICS_VISION_TYPE_H

// The vision type and the zone-light modifiers it applies.
//
// T360 (BOARD B066): the enum lived in renderer_types.h, and the mapping from
// it to (intensity, redShift) was written out TWICE by hand in
// src/client/graphics/sim_animation.cpp as
//     case 1: // Ultravision   intensity = 1.0
//     case 2: // Infravision   intensity = 0.75, redShift = 0.3
// -- SWAPPED against this enum, against EQ::BuffManager::computeVisionType
// (src/client/spell/buff_manager.cpp, which literally `return 2;` for an
// Ultravision effect) and against the comment on SimulationInput::visionType.
// An Ultravision landing therefore got Infravision's red-shifted 0.75, which
// T358 measured live (R +5.5%, G +5.2%, B +0.4% across a `#castspell 46`
// landing -- a red shift, which only the Infravision arm produces).
//
// The enum cannot live in renderer_types.h AND be reachable from
// simulation_worker.h: renderer_types.h includes simulation_worker.h, so the
// dependency only runs one way. Hence this leaf header, which both include.
// Every existing spelling (EQT::Graphics::VisionType, and IrrlichtRenderer's
// re-export) resolves unchanged.

#include <cstdint>

namespace EQT {
namespace Graphics {

// Vision types affecting zone light visibility.
// The race sets the base (PlayerCameraComponent::setPlayerRace -> baseVision_)
// and buffs raise it above that base. T334 (B046): a buff value is APPLIED,
// not accumulated -- the publisher recomputes the whole buff set on every fade
// -- so vision does come back down, never below the racial base.
enum class VisionType {
    Normal,      // Base vision (25% intensity) - Human, Barbarian, Erudite, Vah Shir, Froglok
    Infravision, // Heat vision (75% intensity, red-shifted) - Dwarf, Gnome, Half Elf, Ogre, Halfling
    Ultravision  // Full dark vision (100% intensity) - Dark Elf, High Elf, Wood Elf, Troll, Iksar
};

// What a vision type does to a zone light's colour. The consumers apply it as
//     r = base.r * intensity, then * (1 + redShift) when redShift > 0
//     g = base.g * intensity * (1 - redShift * 0.5)
//     b = base.b * intensity * (1 - redShift)
// T360 did NOT retune these numbers -- only which arm each belongs to. Whether
// point lights are the right place for vision AT ALL is BOARD B067, which
// needs the oracle.
struct VisionLightModifiers {
    float intensity = 0.25f;
    float redShift = 0.0f;
};

// The one place the mapping is written. Switching on the enum (not on an int)
// is what makes a re-swap a compile-visible edit rather than a comment typo.
inline VisionLightModifiers visionLightModifiers(VisionType vision) {
    switch (vision) {
    case VisionType::Ultravision:
        return VisionLightModifiers{1.0f, 0.0f};
    case VisionType::Infravision:
        return VisionLightModifiers{0.75f, 0.3f};
    case VisionType::Normal:
        break;
    }
    return VisionLightModifiers{};
}

// SimulationInput carries the vision type as a raw uint8_t over the worker
// boundary (and uses 255 as its "not yet applied" sentinel). This is the ONE
// cast from that wire value to the enum; an out-of-range value falls through
// the switch above to the Normal default.
inline VisionLightModifiers visionLightModifiers(uint8_t vision_type) {
    return visionLightModifiers(static_cast<VisionType>(vision_type));
}

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_VISION_TYPE_H
