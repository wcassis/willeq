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
// The race sets the base (PlayerCameraComponent::setPlayerRace -> baseVision_;
// at zone-in from registerEntity(), and B448 (#29) on a race change of a
// player already in the world -- illusion / `#race N` -- from
// updatePlayerAppearance()) and buffs raise it above that base. T334 (B046): a buff value is APPLIED,
// not accumulated -- the publisher recomputes the whole buff set on every fade
// -- so vision does come back down, never below the racial base.
//
// T428 (board B060) added the FOURTH value on Will's ruling. T361 measured
// Froglok (race 330) at a night floor 1.51 luma below Ultravision and 3.31
// above Infravision -- outside the noise on both sides -- and left it on
// Normal rather than rounding it to a neighbour by argument. T428 measured it
// per channel against the same official client on T408's geometry-only crop
// (review/T428/, oracle session o3, three sessions agreeing to <1% on the
// other two levels):
//
//     level         R        G        B     grey
//     Ultravision   x2.7995  x1.9517  x2.4946  2.33
//     Froglok       x2.6803  x1.9478  x2.1753  2.20
//     Infravision   x2.2483  x1.4121  x1.3561  1.59
//
// It is its own level and not a rounding of either neighbour: it sits 5.5%
// under Ultravision on B and 60% over Infravision on the same channel.
// FroglokVision is named for the only race that holds it, the way the table
// gives Ultravision to the Dark Elf alone -- EQ has no fourth term for it.
enum class VisionType {
    Normal,      // Base vision (25% intensity) - Human, Barbarian, Erudite
    Infravision, // Heat vision (75% intensity, red-shifted) - Dwarf, Gnome, Half Elf, Ogre, Halfling
    Ultravision, // Full dark vision (100% intensity) - Dark Elf ALONE (T361)
    FroglokVision // The measured fourth level - Froglok (330) alone (T428)
};

// THE ENUMERATOR VALUES ARE WIRE VALUES -- do not reorder them. `Ultravision`
// is 2 because `BuffManager::computeVisionType()` literally `return 2;` for an
// UltraVision effect and `SimulationInput::visionType` carries the enum as a
// raw uint8_t across the worker boundary (255 is its "not applied" sentinel).
// T428 therefore APPENDED FroglokVision as 3 rather than slotting it into
// brightness order, and split brightness out into visionRank() below.
//
// visionRank() is the MEASURED brightness order, which is NOT the enumerator
// order: Froglok sits between Infravision and Ultravision (vision_ambient.h).
// The one place that matters is `PlayerCameraComponent::setVisionType()`,
// whose rule is "a buff raises vision, never lowers it" -- written as a
// comparison on the enum it would have made an Ultravision buff a NO-OP on a
// Froglok, because 3 > 2 by enumerator and 2.80 > 2.68 by measurement.
inline int visionRank(VisionType vision) {
    switch (vision) {
    case VisionType::Ultravision:   return 3;
    case VisionType::FroglokVision: return 2;
    case VisionType::Infravision:   return 1;
    case VisionType::Normal:        break;
    }
    return 0;
}

// One spelling of each level's name. Three log sites open-coded a two-armed
// ternary that printed "Normal" for anything it did not recognise, which a
// fourth level makes actively wrong.
inline const char* visionName(VisionType vision) {
    switch (vision) {
    case VisionType::Ultravision:   return "Ultravision";
    case VisionType::FroglokVision: return "FroglokVision";
    case VisionType::Infravision:   return "Infravision";
    case VisionType::Normal:        break;
    }
    return "Normal";
}

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
    case VisionType::FroglokVision:
        // T428: the zone-LIGHT half of the fourth level is NOT measured. What
        // T428 measured is the global ambient floor (vision_ambient.h), which
        // is the term that carries the buff since T361; whether vision belongs
        // on the zone's data-only point lights AT ALL is board B067, still
        // open and worth ~7% of the residual. Rather than invent a fourth pair
        // of numbers here, the fourth level takes Ultravision's -- the nearest
        // measured neighbour, 5% away on the ambient floor -- and this comment
        // is the record that it is inherited, not measured.
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
