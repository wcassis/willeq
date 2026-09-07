/*
 * T421 (board B298, item half) -- the item display window's TEXT, and the
 * only place it is composed.
 *
 * WHAT WAS BROKEN. `ItemInstance` carries the whole Titanium item row --
 * `TitaniumItemParser` fills a hundred-odd fields off the wire -- and fable
 * drew NONE of them anywhere. There was no item display window at all; the
 * inventory popup shows three characters of an item's name in its slot and
 * nothing else. Every combat stat, every resist, every regen, the weapon's
 * delay, the recommended level, the charge count and the lore line were
 * parsed and dropped. Board B298's item half is that census.
 *
 * THE ORACLE. `/mnt/projects/EverQuestP1999/uifiles/default/EQUI_ItemDisplay.xml`
 * is a 400x190 `<Screen item="ItemDisplayWindow">` holding exactly two
 * children: `IDW_IconButton` (a 40x40 decal) and `IDW_ItemDescription`, an
 *
 *     <STMLbox item="IDW_ItemDescription">
 *         <DrawTemplate>WDT_Inner</DrawTemplate>
 *         <Style_VScroll>true</Style_VScroll>
 *         <AutoStretch>true</AutoStretch>
 *
 * -- a scrolling free-text box. So the XML gives the window's SHAPE and says
 * nothing at all about which stats go in it: the official client composes the
 * STML in its own code.
 *
 * THE LINE FORMATS ARE STILL QUOTABLE, and they are quoted here. They are
 * shipped in the client's own string table,
 * `/mnt/projects/EverQuestP1999/eqstr_us.txt`, and every format below carries
 * its string id in a comment. Nothing here is invented text.
 *
 * WHAT IS *NOT* THE ORACLE'S, flagged rather than dressed up as a quotation
 * (the T419 ORACLE_ROW_H rule): the ORDER of the lines and which of them
 * share a row. The string table is a flat id->format map with no ordering
 * information, and the composing code is in the executable. The order used
 * here is the string table's OWN id order within each group, which is the
 * only ordering evidence the shipped data carries.
 *
 * FIELDS THE ORACLE DOES NOT SHOW, and which therefore stay hidden:
 *   * `currentSlot` -- where the item is RIGHT NOW. `Slot:` (id 3000) is the
 *     item's EQUIPPABLE slot list, a different thing; the item display window
 *     has no element and no string for the item's present location.
 *   * the seven `heroic*` and `bardEffect` -- DELETED from `ItemInstance` by
 *     T425 Leg B (board B375). eqstr_us.txt has no heroic-stat format string
 *     at all (grep for "Heroic" returns nothing -- heroic stats postdate this
 *     client) and no instrument-modifier item-display string (5750 "All
 *     Instrument Types" is a spell-requirement line, not an item stat); and
 *     neither is on Titanium's wire, so there was nothing to parse them from
 *     either. They were dead struct members, not a missing display.
 *   * `loreGroup`, `flags`, `material`, `color`, `bookFilename`, the augment
 *     slot `type` bitmasks -- structural, never printed.
 *
 * T429 Leg A (board B390) CHANGED WHERE THE TEXT COMES FROM. The formats are
 * no longer C++ literals: the composer FETCHES each one out of the shipped
 * string table by id (`data/willeq.db`'s `eqstrings`, the client's own
 * eqstr_us.txt) through `eqt::EqStrLine()`, which runs the one
 * `EQT::StringDatabase` substitution engine. So the composer takes a
 * `StringDatabase&`; a table miss draws NO line rather than falling back to
 * a hardcoded copy of the format. The ids below are the contract, and
 * `tests/test_display_text_from_db.cpp` pins that every one exists in the
 * shipped db.
 *
 * The composer still takes only that database plus an `ItemInstance`, so a
 * test drives the real fields through the real composer with no bridge, no
 * renderer and no device -- the T290/T393 static-panel idiom, one layer
 * lower.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
class StringDatabase;
}

namespace eqt {
namespace inventory {

struct ItemInstance;

// ---------------------------------------------------------------------------
// The eqstr ids this window draws. Named where the code reads better for it;
// the fourteen attribute ids and the fourteen extended-stat ids are dense
// runs and stay numeric at the call site, with the run named in a comment.
// ---------------------------------------------------------------------------
constexpr uint32_t kItemStrSlot                     = 3000;   // "Slot: %1"
constexpr uint32_t kItemStrChargesExpendable        = 3001;   // "EXPENDABLE     Charges: %1"
constexpr uint32_t kItemStrChargesNone              = 3002;   // "Charges: None"
constexpr uint32_t kItemStrCharges                  = 3003;   // "Charges: %1"
constexpr uint32_t kItemStrLevelNeeded              = 3004;   // "Level Needed: %1"
constexpr uint32_t kItemStrSkillDelay               = 3008;   // "Skill: %1   Atk Delay: %2"
constexpr uint32_t kItemStrAc                       = 3016;   // "AC: %1"
constexpr uint32_t kItemStrRecommendedLevel         = 3028;   // "Recommended level of %1."
constexpr uint32_t kItemStrEffectCombat             = 3029;   // "(Combat)"
constexpr uint32_t kItemStrEffectCastTime           = 3030;   // "(Casting Time: %2.1f)"   <- printf
constexpr uint32_t kItemStrEffectInstant            = 3031;   // "(Casting Time: Instant)"
constexpr uint32_t kItemStrEffectWorn               = 3032;   // "(Worn)"
constexpr uint32_t kItemStrEffectMustEquipCastTime  = 3033;   // "(Must Equip.  Casting Time: %2.1f)" <- printf
constexpr uint32_t kItemStrEffectMustEquipInstant   = 3034;   // "(Must Equip.  Casting Time: Instant)"
constexpr uint32_t kItemStrEffect                   = 3036;   // "Effect: %1 %2"
constexpr uint32_t kItemStrWeightRangeSize          = 3037;   // "WT: %1    Range: %2    Size: %3"
constexpr uint32_t kItemStrWeightSize               = 3038;   // "WT: %1   Size: %2"
constexpr uint32_t kItemStrContainerWeight          = 3042;   // "WT: %1    Weight Reduction: %2"
constexpr uint32_t kItemStrContainerCapacity        = 3043;   // "Capacity: %1    Size Capacity: %2"
constexpr uint32_t kItemStrLore                     = 3048;   // "LORE: "   (a LABEL: no placeholder)
constexpr uint32_t kItemStrRace                     = 3049;   // "Race: "   (a LABEL: no placeholder)
constexpr uint32_t kItemStrClass                    = 3064;   // "Class: "  (a LABEL: no placeholder)
constexpr uint32_t kItemStrFocusEffect              = 5530;   // "Focus Effect: %1"
constexpr uint32_t kItemStrDmg                      = 12455;  // "DMG: %1"

/** Every eqstr id BuildItemDisplayText() can fetch, for the db census test. */
inline const std::vector<uint32_t>& ItemDisplayEqStrIds() {
    static const std::vector<uint32_t> ids = {
        3000, 3001, 3002, 3003, 3004, 3008, 3016, 3028,
        3029, 3030, 3031, 3032, 3033, 3034, 3036, 3037, 3038,
        3042, 3043, 3048, 3049, 3064, 5530,
        // the extended stat run
        5809, 5836, 5844, 5845, 5846, 5847, 5848, 6184,
        9040, 9041, 9042, 9043, 9044, 9045,
        12455,
        // the attribute run
        12457, 12458, 12459, 12460, 12461, 12462, 12463, 12464, 12465,
        12466, 12467, 12468, 12469, 12470,
    };
    return ids;
}

/**
 * The item display window's body text, one entry per drawn line. An item
 * with no stats yields the flag line (or nothing at all): a zero-valued
 * field is OMITTED, exactly as the official window omits it, rather than
 * printed as "0".
 *
 * `strings` is the shipped eqstr table -- `EverQuest`'s own, reached as
 * `ctx_.strings.Db()`. Every drawn format comes out of it (T429, B390).
 */
std::vector<std::string> BuildItemDisplayText(const EQT::StringDatabase& strings,
                                              const ItemInstance& item);

} // namespace inventory
} // namespace eqt
