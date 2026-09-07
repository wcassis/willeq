/*
 * T421 (board B298, item half) -- see item_display_text.h for the oracle and
 * for the fields the oracle does NOT show.
 *
 * T429 Leg A (board B390): every format string below is now FETCHED from the
 * shipped string table (`data/willeq.db`'s `eqstrings`, the client's own
 * eqstr_us.txt) by id, through eqt::EqStrLine() -> the one
 * EQT::StringDatabase substitution engine. The ids stay in the code and the
 * comment still names the line, so the intent reads the same; what changed
 * is that the TEXT is data, not a literal. A table miss draws no line rather
 * than falling back to a hardcoded copy -- a fallback copy is exactly the
 * defect B390 names.
 */

#include "client/inventory/item_display_text.h"

#include "client/display_eqstr.h"
#include "client/inventory/item_instance.h"
#include "client/inventory/inventory_constants.h"
#include "client/skill/skill_constants.h"
#include "client/string_database.h"

#include <fmt/format.h>

namespace eqt {
namespace inventory {

namespace {

// A row built from the trailing-space "STAT: %1    " formats (eqstr 12457..
// 12470). Those formats end in spaces precisely so the client can run
// several of them together on one line, which is why they are accumulated
// rather than emitted one per line.
struct Row {
    std::string text;
    void add(const std::string& s) { text += s; }
    bool empty() const { return text.empty(); }
};

void flush(std::vector<std::string>& out, Row& row) {
    if (!row.empty()) {
        // Trim the trailing spaces the format strings carry.
        size_t end = row.text.find_last_not_of(' ');
        out.push_back(row.text.substr(0, end + 1));
        row.text.clear();
    }
}

/** push_back unless the string table had no row for the id. */
void emit(std::vector<std::string>& out, std::string line) {
    if (!line.empty()) {
        out.push_back(std::move(line));
    }
}

/** The qualifier eqstr 3029..3034 puts after an "Effect: %1 %2" line. */
std::string effectQualifier(const EQT::StringDatabase& strings,
                            const ItemEffect& e, bool mustEquip, bool combat) {
    if (combat) {
        return EqStrLine(strings, kItemStrEffectCombat);          // 3029 "(Combat)"
    }
    if (e.castTime <= 0) {
        return EqStrLine(strings, mustEquip ? kItemStrEffectMustEquipInstant   // 3034
                                            : kItemStrEffectInstant);          // 3031
    }
    // 3030/3033 are printf formats -- "(Casting Time: %2.1f)" -- not the
    // positional %n every other row uses. See display_eqstr.h.
    const double secs = static_cast<double>(e.castTime) / 1000.0;
    return EqStrPrintfFloat(strings,
                            mustEquip ? kItemStrEffectMustEquipCastTime   // 3033
                                      : kItemStrEffectCastTime,           // 3030
                            secs);
}

std::string num(long long v) { return std::to_string(v); }

}  // namespace

std::vector<std::string> BuildItemDisplayText(const EQT::StringDatabase& strings,
                                              const ItemInstance& item) {
    std::vector<std::string> out;

    // --- The flag banner: eqstr 3094..3100 ---
    //   3094 MAGIC ITEM   3095 LORE ITEM   3097 ARTIFACT
    //   3098 SUMMONED     3099 NO TRADE    3100 TEMPORARY
    // `ItemInstance::getFlagsString()` already builds this list and had NO
    // production caller before T421; its wording ("Magic", "No Trade", ...)
    // is the same set in the same order, so it is reached rather than
    // duplicated here. It is a WORD table on the struct, not a format
    // string, which is why B390's fetch does not reach into it -- the same
    // reason buildSlotsString()/buildClassString()/buildRaceString() below
    // stay as they are.
    {
        const std::string flags = item.getFlagsString();
        if (!flags.empty()) {
            out.push_back(flags);
        }
    }

    // --- 3000 "Slot: %1" -- the EQUIPPABLE slots, not `currentSlot` ---
    if (item.slots != 0) {
        emit(out, EqStrLine(strings, kItemStrSlot, {buildSlotsString(item.slots)}));
    }

    // --- 12455 "DMG: %1" and 3016 "AC: %1" ---
    if (item.damage != 0) {
        emit(out, EqStrLine(strings, kItemStrDmg, {num(item.damage)}));
    }
    if (item.ac != 0) {
        emit(out, EqStrLine(strings, kItemStrAc, {num(item.ac)}));
    }

    // --- 3008 "Skill: %1   Atk Delay: %2" (B298: `delay`) ---
    if (item.delay != 0 || (item.skillType != 0 && item.damage != 0)) {
        emit(out, EqStrLine(strings, kItemStrSkillDelay,
                            {EQ::getSkillName(item.skillType), num(item.delay)}));
    }

    // --- The attribute block, eqstr 12457..12470, in the table's own order:
    //   12457 STR  12458 DEX  12459 STA  12460 CHA  12461 WIS  12462 INT
    //   12463 AGI  12464 HP   12465 MANA
    //   12466 SV FIRE  12467 SV DISEASE  12468 SV COLD  12469 SV MAGIC
    //   12470 SV POISON
    {
        Row row;
        auto stat = [&](uint32_t id, int v) {
            if (v != 0) row.add(EqStrLine(strings, id, {num(v)}));
        };
        stat(12457, item.str);
        stat(12458, item.dex);
        stat(12459, item.sta);
        stat(12460, item.cha);
        stat(12461, item.wis);
        stat(12462, item.int_);
        stat(12463, item.agi);
        flush(out, row);

        stat(12464, item.hp);
        stat(12465, item.mana);
        flush(out, row);

        stat(12466, item.fireResist);
        stat(12467, item.diseaseResist);
        stat(12468, item.coldResist);
        stat(12469, item.magicResist);
        stat(12470, item.poisonResist);
        flush(out, row);
    }

    // --- The extended stat block. Ascending eqstr id, which is the only
    // ordering evidence the shipped table carries. TWELVE of the fields
    // B298 named are on these lines.
    //   5809 "Endurance: %1"                 5836 "DoT Shielding: %1%"
    //   5844 "Attack: %1"                    5845 "Regeneration: %1"
    //   5846 "Mana Regeneration: %1"         5847 "Damage Shield: %1"
    //   5848 "Haste: %1%"                    6184 "Endurance Regeneration: %1"
    //   9040 "Shielding: %1%"                9041 "Spell Shield: %1%"
    //   9042 "Avoidance: %1"                 9043 "Accuracy: %1"
    //   9044 "Stun Resist: %1%"              9045 "Strikethrough: %1%"
    {
        const std::pair<uint32_t, int> extended[] = {
            {5809, item.endurance},      {5836, item.dotShield},
            {5844, item.attack},         {5845, item.hpRegen},
            {5846, item.manaRegen},      {5847, item.damageShield},
            {5848, item.haste},          {6184, item.enduranceRegen},
            {9040, item.shielding},      {9041, item.spellShield},
            {9042, item.avoidance},      {9043, item.accuracy},
            {9044, item.stunResist},     {9045, item.strikethrough},
        };
        for (const auto& [id, value] : extended) {
            if (value != 0) {
                emit(out, EqStrLine(strings, id, {num(value)}));
            }
        }
    }

    // --- Charges, eqstr 3001..3003 (B298: `maxCharges`, `expendable`) ---
    //   3001 "EXPENDABLE     Charges: %1"
    //   3002 "Charges: None"
    //   3003 "Charges: %1"
    if (item.expendable) {
        emit(out, EqStrLine(strings, kItemStrChargesExpendable, {num(item.charges)}));
    } else if (item.maxCharges > 0) {
        emit(out, EqStrLine(strings, kItemStrCharges, {num(item.charges)}));
    } else if (item.clickEffect.effectId != 0) {
        emit(out, EqStrLine(strings, kItemStrChargesNone));
    }

    // --- Effects, eqstr 3036 "Effect: %1 %2" + the 3029..3034 qualifiers,
    // and 5530 "Focus Effect: %1".
    if (item.clickEffect.effectId != 0) {
        emit(out, EqStrLine(strings, kItemStrEffect,
                            {item.clickEffect.name,
                             effectQualifier(strings, item.clickEffect, false, false)}));
    }
    if (item.procEffect.effectId != 0) {
        emit(out, EqStrLine(strings, kItemStrEffect,
                            {item.procEffect.name,
                             effectQualifier(strings, item.procEffect, true, true)}));
    }
    if (item.wornEffect.effectId != 0) {
        emit(out, EqStrLine(strings, kItemStrEffect,
                            {item.wornEffect.name,
                             EqStrLine(strings, kItemStrEffectWorn)}));  // 3032 "(Worn)"
    }
    if (item.focusEffect.effectId != 0) {
        emit(out, EqStrLine(strings, kItemStrFocusEffect, {item.focusEffect.name}));
    }

    // --- Level lines, eqstr 3004 and 3028 (B298: `recLevel`) ---
    //   3004 "Level Needed: %1"      3028 "Recommended level of %1."
    if (item.recLevel != 0) {
        emit(out, EqStrLine(strings, kItemStrRecommendedLevel, {num(item.recLevel)}));
    }
    if (item.reqLevel != 0) {
        emit(out, EqStrLine(strings, kItemStrLevelNeeded, {num(item.reqLevel)}));
    }

    // --- Weight / range / size, eqstr 3037/3038, and the container pair
    // 3042/3043. Item weight is carried in tenths on the wire, which is why
    // the official line prints a decimal.
    const std::string wt = fmt::format("{:.1f}", item.weight);
    if (item.isContainer()) {
        // 3042 "WT: %1    Weight Reduction: %2"
        emit(out, EqStrLine(strings, kItemStrContainerWeight, {wt, num(item.bagWR)}));
        // 3043 "Capacity: %1    Size Capacity: %2"
        emit(out, EqStrLine(strings, kItemStrContainerCapacity,
                            {num(item.bagSlots), getSizeName(item.bagSize)}));
    } else if (item.range != 0) {
        // 3037 "WT: %1    Range: %2    Size: %3"
        emit(out, EqStrLine(strings, kItemStrWeightRangeSize,
                            {wt, num(item.range), getSizeName(item.size)}));
    } else {
        // 3038 "WT: %1   Size: %2"
        emit(out, EqStrLine(strings, kItemStrWeightSize, {wt, getSizeName(item.size)}));
    }

    // --- Restrictions and lore. eqstr 3064 "Class: ", 3049 "Race: " and
    // 3048 "LORE: " carry NO placeholder in the shipped table -- they are
    // LABELS the official client concatenates its own list onto, which is
    // why these three are a fetch-then-append rather than a substitution.
    // buildClassString()/buildRaceString() are the abbreviation tables
    // eqstr 3050..3079 spells out.
    auto labelled = [&](uint32_t id, const std::string& body) {
        const std::string label = EqStrLine(strings, id);
        if (!label.empty()) {
            out.push_back(label + body);
        }
    };
    labelled(kItemStrClass, buildClassString(item.classes));
    labelled(kItemStrRace, buildRaceString(item.races));
    if (!item.lore.empty()) {
        labelled(kItemStrLore, item.lore);
    }

    return out;
}

} // namespace inventory
} // namespace eqt
