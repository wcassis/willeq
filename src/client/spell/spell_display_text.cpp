/*
 * T425 (board B298, spell half) -- see spell_display_text.h for the oracle,
 * for why there is no second window, and for the fields the oracle does NOT
 * show.
 *
 * T429 Leg A (board B390): the formats are FETCHED from the shipped string
 * table by id through eqt::EqStrLine(), the same one-engine route
 * BuildItemDisplayText() takes. The ids stay in the code and the comment
 * still names the line; the TEXT is data now, not a literal.
 */

#include "client/spell/spell_display_text.h"

#include "client/display_eqstr.h"
#include "client/spell/spell_data.h"
#include "client/skill/skill_constants.h"
#include "client/string_database.h"

#include <fmt/format.h>

namespace eqt {
namespace spell {

namespace {

/** Milliseconds -> the one-decimal seconds the oracle's cast-time lines use. */
std::string seconds(uint32_t ms) {
    return fmt::format("{:.1f}", static_cast<double>(ms) / 1000.0);
}

/** push_back unless the string table had no row for the id. */
void emit(std::vector<std::string>& out, std::string line) {
    if (!line.empty()) {
        out.push_back(std::move(line));
    }
}

}  // namespace

std::vector<std::string> BuildSpellDisplayText(const EQT::StringDatabase& strings,
                                               const EQ::SpellData& spell,
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
            emit(out, EqStrLine(strings, kSpellStrLevelCannotUse));
        } else if (needed != 0) {
            emit(out, EqStrLine(strings, kSpellStrLevelNeeded, {std::to_string(needed)}));
        }
    }

    // --- 3006 "Mana Cost: %1" ---
    if (spell.mana_cost != 0) {
        emit(out, EqStrLine(strings, kSpellStrManaCost, {std::to_string(spell.mana_cost)}));
    }

    // --- 3102 "Spell Duration: %1." / 3103 "Spell Duration: Permanent." ---
    // The unit substituted into %1 is fable's (see the header): the tick
    // count the client's own duration formula returns for THIS caster level.
    {
        const uint32_t ticks = spell.calculateDuration(playerLevel);
        if (ticks == 0xFFFFFFFFu) {
            emit(out, EqStrLine(strings, kSpellStrDurationPermanent));
        } else if (ticks != 0) {
            emit(out, EqStrLine(strings, kSpellStrDuration, {std::to_string(ticks)}));
        }
    }

    // --- 3105 "Skill: %1" ---
    // CastingSkill's enumerators ARE skill ids, so the name comes from the
    // same EQ::getSkillName() the item window's 3008 line uses.
    emit(out, EqStrLine(strings, kSpellStrSkill,
                        {EQ::getSkillName(static_cast<uint8_t>(spell.casting_skill))}));

    // --- 3107 "Cast Time: %1" ---
    if (spell.cast_time_ms != 0) {
        emit(out, EqStrLine(strings, kSpellStrCastTime, {seconds(spell.cast_time_ms)}));
    }

    // --- 6730 "Range = %1" ---
    if (spell.range > 0.0f) {
        emit(out, EqStrLine(strings, kSpellStrRange,
                            {std::to_string(static_cast<int>(spell.range))}));
    }

    // --- 9234 "Recast Delay: %1" ---
    if (spell.recast_time_ms != 0) {
        emit(out, EqStrLine(strings, kSpellStrRecastDelay, {seconds(spell.recast_time_ms)}));
    }

    return out;
}

} // namespace spell
} // namespace eqt
