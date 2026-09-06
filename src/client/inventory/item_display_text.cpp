/*
 * T421 (board B298, item half) -- see item_display_text.h for the oracle and
 * for the fields the oracle does NOT show.
 *
 * Every format string below is quoted from
 * /mnt/projects/EverQuestP1999/eqstr_us.txt with its id. The ids are the
 * client's own; the comment is the shipped line verbatim.
 */

#include "client/inventory/item_display_text.h"

#include "client/inventory/item_instance.h"
#include "client/inventory/inventory_constants.h"
#include "client/skill/skill_constants.h"

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

/** The qualifier eqstr 3029..3034 puts after an "Effect: %1 %2" line. */
std::string effectQualifier(const ItemEffect& e, bool mustEquip, bool combat) {
    if (combat) {
        return "(Combat)";                              // 3029
    }
    if (e.castTime <= 0) {
        return mustEquip ? "(Must Equip.  Casting Time: Instant)"  // 3034
                         : "(Casting Time: Instant)";              // 3031
    }
    const double secs = static_cast<double>(e.castTime) / 1000.0;
    return mustEquip
        ? fmt::format("(Must Equip.  Casting Time: {:.1f})", secs)  // 3033
        : fmt::format("(Casting Time: {:.1f})", secs);              // 3030
}

}  // namespace

std::vector<std::string> BuildItemDisplayText(const ItemInstance& item) {
    std::vector<std::string> out;

    // --- The flag banner: eqstr 3094..3100 ---
    //   3094 MAGIC ITEM   3095 LORE ITEM   3097 ARTIFACT
    //   3098 SUMMONED     3099 NO TRADE    3100 TEMPORARY
    // `ItemInstance::getFlagsString()` already builds this list and had NO
    // production caller before this unit; its wording ("Magic", "No Trade",
    // ...) is the same set in the same order, so it is reached rather than
    // duplicated here.
    {
        const std::string flags = item.getFlagsString();
        if (!flags.empty()) {
            out.push_back(flags);
        }
    }

    // --- 3000 "Slot: %1" -- the EQUIPPABLE slots, not `currentSlot` ---
    // buildSlotsString() names them in eqstr's own 558..576 order (Held/Ear/
    // Head/.../Ammo) and had zero production callers before this unit.
    if (item.slots != 0) {
        out.push_back(fmt::format("Slot: {}", buildSlotsString(item.slots)));
    }

    // --- 12455 "DMG: %1" and 3016 "AC: %1" ---
    if (item.damage != 0) {
        out.push_back(fmt::format("DMG: {}", item.damage));
    }
    if (item.ac != 0) {
        out.push_back(fmt::format("AC: {}", item.ac));
    }

    // --- 3008 "Skill: %1   Atk Delay: %2" (B298: `delay`) ---
    // A weapon has a skill type and a delay; anything with either gets the
    // line, with the skill name from skill_constants.h.
    if (item.delay != 0 || (item.skillType != 0 && item.damage != 0)) {
        out.push_back(fmt::format("Skill: {}   Atk Delay: {}",
                                  EQ::getSkillName(item.skillType),
                                  item.delay));
    }

    // --- The attribute block, eqstr 12457..12470, in the table's own order:
    //   12457 STR  12458 DEX  12459 STA  12460 CHA  12461 WIS  12462 INT
    //   12463 AGI  12464 HP   12465 MANA
    //   12466 SV FIRE  12467 SV DISEASE  12468 SV COLD  12469 SV MAGIC
    //   12470 SV POISON
    {
        Row row;
        if (item.str  != 0) row.add(fmt::format("STR: {}    ", item.str));
        if (item.dex  != 0) row.add(fmt::format("DEX: {}   ",  item.dex));
        if (item.sta  != 0) row.add(fmt::format("STA: {}    ", item.sta));
        if (item.cha  != 0) row.add(fmt::format("CHA: {}    ", item.cha));
        if (item.wis  != 0) row.add(fmt::format("WIS: {}    ", item.wis));
        if (item.int_ != 0) row.add(fmt::format("INT: {}    ", item.int_));
        if (item.agi  != 0) row.add(fmt::format("AGI: {}    ", item.agi));
        flush(out, row);

        if (item.hp   != 0) row.add(fmt::format("HP: {}    ",   item.hp));
        if (item.mana != 0) row.add(fmt::format("MANA: {}    ", item.mana));
        flush(out, row);

        if (item.fireResist    != 0) row.add(fmt::format("SV FIRE: {}    ",    item.fireResist));
        if (item.diseaseResist != 0) row.add(fmt::format("SV DISEASE: {}    ", item.diseaseResist));
        if (item.coldResist    != 0) row.add(fmt::format("SV COLD: {}    ",    item.coldResist));
        if (item.magicResist   != 0) row.add(fmt::format("SV MAGIC: {}    ",   item.magicResist));
        if (item.poisonResist  != 0) row.add(fmt::format("SV POISON: {}    ",  item.poisonResist));
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
    if (item.endurance      != 0) out.push_back(fmt::format("Endurance: {}", item.endurance));
    if (item.dotShield      != 0) out.push_back(fmt::format("DoT Shielding: {}%", item.dotShield));
    if (item.attack         != 0) out.push_back(fmt::format("Attack: {}", item.attack));
    if (item.hpRegen        != 0) out.push_back(fmt::format("Regeneration: {}", item.hpRegen));
    if (item.manaRegen      != 0) out.push_back(fmt::format("Mana Regeneration: {}", item.manaRegen));
    if (item.damageShield   != 0) out.push_back(fmt::format("Damage Shield: {}", item.damageShield));
    if (item.haste          != 0) out.push_back(fmt::format("Haste: {}%", item.haste));
    if (item.enduranceRegen != 0) out.push_back(fmt::format("Endurance Regeneration: {}", item.enduranceRegen));
    if (item.shielding      != 0) out.push_back(fmt::format("Shielding: {}%", item.shielding));
    if (item.spellShield    != 0) out.push_back(fmt::format("Spell Shield: {}%", item.spellShield));
    if (item.avoidance      != 0) out.push_back(fmt::format("Avoidance: {}", item.avoidance));
    if (item.accuracy       != 0) out.push_back(fmt::format("Accuracy: {}", item.accuracy));
    if (item.stunResist     != 0) out.push_back(fmt::format("Stun Resist: {}%", item.stunResist));
    if (item.strikethrough  != 0) out.push_back(fmt::format("Strikethrough: {}%", item.strikethrough));

    // --- Charges, eqstr 3001..3003 (B298: `maxCharges`, `expendable`) ---
    //   3001 "EXPENDABLE     Charges: %1"
    //   3002 "Charges: None"
    //   3003 "Charges: %1"
    if (item.expendable) {
        out.push_back(fmt::format("EXPENDABLE     Charges: {}", item.charges));
    } else if (item.maxCharges > 0) {
        out.push_back(fmt::format("Charges: {}", item.charges));
    } else if (item.clickEffect.effectId != 0) {
        out.push_back("Charges: None");
    }

    // --- Effects, eqstr 3036 "Effect: %1 %2" + the 3029..3034 qualifiers,
    // and 5530 "Focus Effect: %1".
    if (item.clickEffect.effectId != 0) {
        out.push_back(fmt::format("Effect: {} {}", item.clickEffect.name,
                                  effectQualifier(item.clickEffect, false, false)));
    }
    if (item.procEffect.effectId != 0) {
        out.push_back(fmt::format("Effect: {} {}", item.procEffect.name,
                                  effectQualifier(item.procEffect, true, true)));
    }
    if (item.wornEffect.effectId != 0) {
        out.push_back(fmt::format("Effect: {} (Worn)", item.wornEffect.name));  // 3032
    }
    if (item.focusEffect.effectId != 0) {
        out.push_back(fmt::format("Focus Effect: {}", item.focusEffect.name));  // 5530
    }

    // --- Level lines, eqstr 3004 and 3028 (B298: `recLevel`) ---
    //   3004 "Level Needed: %1"      3028 "Recommended level of %1."
    if (item.recLevel != 0) {
        out.push_back(fmt::format("Recommended level of {}.", item.recLevel));
    }
    if (item.reqLevel != 0) {
        out.push_back(fmt::format("Level Needed: {}", item.reqLevel));
    }

    // --- Weight / range / size, eqstr 3037/3038, and the container pair
    // 3042/3043. Item weight is carried in tenths on the wire, which is why
    // the official line prints a decimal.
    if (item.isContainer()) {
        // 3042 "WT: %1    Weight Reduction: %2"
        out.push_back(fmt::format("WT: {:.1f}    Weight Reduction: {}",
                                  item.weight, item.bagWR));
        // 3043 "Capacity: %1    Size Capacity: %2"
        out.push_back(fmt::format("Capacity: {}    Size Capacity: {}",
                                  item.bagSlots, getSizeName(item.bagSize)));
    } else if (item.range != 0) {
        // 3037 "WT: %1    Range: %2    Size: %3"
        out.push_back(fmt::format("WT: {:.1f}    Range: {}    Size: {}",
                                  item.weight, item.range, getSizeName(item.size)));
    } else {
        // 3038 "WT: %1   Size: %2"
        out.push_back(fmt::format("WT: {:.1f}   Size: {}",
                                  item.weight, getSizeName(item.size)));
    }

    // --- Restrictions, eqstr 3064 "Class: " and 3049 "Race: " ---
    // buildClassString()/buildRaceString() are the abbreviation tables
    // eqstr 3050..3079 spells out; both had zero production callers.
    out.push_back(fmt::format("Class: {}", buildClassString(item.classes)));
    out.push_back(fmt::format("Race: {}", buildRaceString(item.races)));

    // --- 3048 "LORE: " (B298: `lore`) ---
    if (!item.lore.empty()) {
        out.push_back(fmt::format("LORE: {}", item.lore));
    }

    return out;
}

} // namespace inventory
} // namespace eqt
