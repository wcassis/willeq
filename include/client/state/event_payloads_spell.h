#pragma once

// Event payloads: spells, buffs, skills, weather and zone lifecycle.
//
// T451: split out of the 1,269-line include/client/state/event_bus.h.
// Every declaration is a contiguous line range of that file, moved verbatim;
// "client/state/event_bus.h" still includes all of them, so no include site
// changed.

#include <functional>
#include <vector>
#include <variant>
#include <string>
#include <cstdint>
#include <mutex>
#include <memory>
#include <glm/glm.hpp>

#include "client/hotbar/hotbar_slot_type.h"  // T156: HotbarSlotAssignedData carries the enum, not a raw uint8_t
#include "client/spell/spell_visual_vocabulary.h"  // T381: SpellCastVisual*Data carry the render vocabulary by value, not a SpellDatabase*

class HCMap;
class PathfinderNavmesh;

#include "client/state/event_types.h"

namespace eqt {
namespace state {

// --- Spell events ---

struct SpellGemChangedData {
    uint8_t gemSlot;
    uint32_t spellId;
    uint8_t gemState;  // SpellGemState value
    uint32_t cooldownRemainingMs;
    uint32_t cooldownTotalMs;       // Total cooldown duration (for progress computation)
    // T413 (board B296): `memorizeTotalMs` lived here, was copied onto the
    // cached gem state and read by nothing. The oracle draws no gauge on a
    // gem: EQUI_CastSpellWnd.xml's <SpellGem item="CSPW_Spell0".."Spell8">
    // carry only a SpellGemDrawTemplate (Holder/Background/Highlight) and no
    // Gauge piece at all. Memorisation progress is shown by
    // EQUI_SpellBookWnd.xml's <Gauge item="SBW_Memorize_Gauge"> (EQType 9),
    // which fable now draws from SpellMemorizingData -- the event that
    // actually carries the progress. So this duplicate is dropped, not drawn.
    std::string spellName;          // Spell name (for tooltip/display)
    uint32_t iconId;                // Spell gem icon ID
};

struct CastingStateChangedData {
    bool isCasting;
    uint32_t spellId;
    uint16_t targetId;
    uint32_t castTimeRemainingMs;
    uint32_t castTimeTotalMs;
};

struct SpellMemorizingData {
    bool isMemorizing;
    uint8_t gemSlot;
    uint32_t spellId;
    uint32_t progressMs;
    uint32_t totalMs;
};

struct BuffUpdatedData {
    uint8_t slot;
    uint32_t spellId;
    uint32_t ticksLeft;
    std::string casterName;
    std::string spellName;  // U06c: for new UI display
    uint32_t iconId = 0;    // U06c: spell icon
};

struct BuffRemovedData {
    uint8_t slot;
};

struct VisionChangedData {
    uint8_t visionType;  // 0=normal, 1=infravision, 2=ultravision
};

// --- Skill events ---

struct SkillValueChangedData {
    uint16_t skillId;
    uint16_t value;
};

struct SkillsRefreshedData {
    // Full skill refresh — consumer should re-query all skills
};

// U06h: Full skills snapshot (published once at zone-in from U00)
struct SkillDisplayEntry {
    uint8_t skillId;
    std::string name;
    uint32_t value;
    uint32_t maxValue;
};

struct SkillsSnapshotData {
    std::vector<SkillDisplayEntry> skills;
};

// --- World/environment events ---

struct WeatherChangedData {
    uint8_t type;       // 0=none, 1=rain, 2=snow
    uint8_t intensity;  // 0-255
};

struct SwimmingStateChangedData {
    bool isSwimming;
    float swimSpeed;
    bool isLevitating;
};

// --- Zone lifecycle events ---

struct CollisionMapChangedData {
    // P35: shared ownership (was S2's non-owning raw HCMap*, game thread
    // retained ownership) -- the game thread can reset/replace its
    // m_zone_map on the next zone change at any time, independent of when
    // the renderer/SimulationWorker side finishes with the old map; a
    // shared_ptr copy lets the object simply outlive every holder instead
    // of dangling under the renderer.
    std::shared_ptr<const HCMap> map;
};

struct ZoneLineBoundingBoxesData {
    // Consumer should re-query zone lines from game state
};

} // namespace state
} // namespace eqt
