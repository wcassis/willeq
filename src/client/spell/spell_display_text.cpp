/*
 * T425 (board B298, spell half) -- see spell_display_text.h for the oracle,
 * for why there is no second window, and for the fields the oracle does NOT
 * show.
 *
 * Every format string below is quoted from
 * /mnt/projects/EverQuestP1999/eqstr_us.txt with its id. The ids are the
 * client's own; the comment is the shipped line verbatim.
 */

#include "client/spell/spell_display_text.h"

#include "client/spell/spell_data.h"
#include "client/skill/skill_constants.h"

#include <fmt/format.h>

namespace eqt {
namespace spell {

namespace {

/** Milliseconds -> the one-decimal seconds the oracle's cast-time lines use. */
std::string seconds(uint32_t ms) {
    return fmt::format("{:.1f}", static_cast<double>(ms) / 1000.0);
}

}  // namespace

std::vector<std::string> BuildSpellDisplayText(const EQ::SpellData& spell,
                                               uint32_t playerClassId,
                                               uint8_t playerLevel) {
    std::vector<std::string> out;

    // Ascending eqstr id, which is the only ordering evidence the shipped
    // table carries (BuildItemDisplayText()'s rule, applied here unchanged).

    // --- 3004 "Level Needed: %1" / 3005 "Level Needed: CANNOT USE" ---
    // SpellData::class_levels is indexed by class id - 1 and holds 255 for
    // "this class can never use this spell", which is exactly what 3005 says.
    if (playerClassId >= 1 && playerClassId <= EQ::NUM_CLASSES) {
        const uint8_t needed = spell.class_levels[playerClassId - 1];
        if (needed == 255) {
            out.push_back("Level Needed: CANNOT USE");
        } else if (needed != 0) {
            out.push_back(fmt::format("Level Needed: {}", needed));
        }
    }

    // --- 3006 "Mana Cost: %1" ---
    if (spell.mana_cost != 0) {
        out.push_back(fmt::format("Mana Cost: {}", spell.mana_cost));
    }

    // --- 3102 "Spell Duration: %1." / 3103 "Spell Duration: Permanent." ---
    // The unit substituted into %1 is fable's (see the header): the tick
    // count the client's own duration formula returns for THIS caster level.
    {
        const uint32_t ticks = spell.calculateDuration(playerLevel);
        if (ticks == 0xFFFFFFFFu) {
            out.push_back("Spell Duration: Permanent.");
        } else if (ticks != 0) {
            out.push_back(fmt::format("Spell Duration: {}.", ticks));
        }
    }

    // --- 3105 "Skill: %1" ---
    // CastingSkill's enumerators ARE skill ids, so the name comes from the
    // same EQ::getSkillName() the item window's 3008 line uses.
    out.push_back(fmt::format(
        "Skill: {}", EQ::getSkillName(static_cast<uint8_t>(spell.casting_skill))));

    // --- 3107 "Cast Time: %1" ---
    if (spell.cast_time_ms != 0) {
        out.push_back(fmt::format("Cast Time: {}", seconds(spell.cast_time_ms)));
    }

    // --- 6730 "Range = %1" ---
    if (spell.range > 0.0f) {
        out.push_back(fmt::format("Range = {}", static_cast<int>(spell.range)));
    }

    // --- 9234 "Recast Delay: %1" ---
    if (spell.recast_time_ms != 0) {
        out.push_back(fmt::format("Recast Delay: {}", seconds(spell.recast_time_ms)));
    }

    return out;
}

} // namespace spell
} // namespace eqt
