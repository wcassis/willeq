/*
 * HotbarSlotType — the ONE hotbar numbering.
 *
 * T156: three disagreeing numberings existed before this unit —
 *   eqt::ui::HotbarButtonType (eq.h)      Spell=0, Skill=1, Item=2, Command=3
 *   eqt::HotbarSlotType (hotbar_model.h)  Empty=0, Spell=1, Skill=2, Item=3, ...
 *   the switch in eq_bridge.cpp           0=Spell, 1=Item, 2=Emote, 3=Skill
 * The middle one is the only numbering any real data ever carried
 * (HotbarModel -> HotbarSlotAssignedData -> HotbarPanelState), so it is the
 * source of truth. It lives here, in a header with no dependencies, so the
 * renderer intent structs, the event bus and the hotbar model can all share
 * the type itself rather than a raw uint8_t that drifts.
 */

#pragma once

#include <cstdint>

namespace eqt {

enum class HotbarSlotType : uint8_t {
    Empty = 0,
    Spell = 1,
    Skill = 2,
    Item = 3,
    Emote = 4,
    Macro = 5,
    Combat = 6
};

/** Highest valid enumerator. Keep in step with the enum above. */
inline constexpr uint8_t HOTBAR_SLOT_TYPE_MAX = static_cast<uint8_t>(HotbarSlotType::Combat);

/**
 * Validated raw -> enum conversion. T95: never cast an unvalidated int to an
 * enum. Returns false (and leaves `out` untouched) for out-of-range values,
 * which is what a corrupt/older config file produces.
 */
inline bool hotbarSlotTypeFromRaw(uint32_t raw, HotbarSlotType& out) {
    if (raw > HOTBAR_SLOT_TYPE_MAX) return false;
    out = static_cast<HotbarSlotType>(static_cast<uint8_t>(raw));
    return true;
}

inline const char* hotbarSlotTypeName(HotbarSlotType type) {
    switch (type) {
        case HotbarSlotType::Empty:  return "Empty";
        case HotbarSlotType::Spell:  return "Spell";
        case HotbarSlotType::Skill:  return "Skill";
        case HotbarSlotType::Item:   return "Item";
        case HotbarSlotType::Emote:  return "Emote";
        case HotbarSlotType::Macro:  return "Macro";
        case HotbarSlotType::Combat: return "Combat";
    }
    return "Unknown";
}

} // namespace eqt
