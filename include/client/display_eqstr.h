/*
 * T429 Leg A (board B390) -- the one place a display composer turns an
 * eqstr id into a drawn line.
 *
 * WHAT WAS BROKEN. T421's `BuildItemDisplayText()` and T425's
 * `BuildSpellDisplayText()` each carried the official client's format
 * strings as C++ string literals, hand-transcribed out of
 * /mnt/projects/EverQuestP1999/eqstr_us.txt with the id in a comment. That
 * whole table already ships in `data/willeq.db` (`eqstrings`, 5,901 rows,
 * written by tools/willeq_db_builder.cpp's loadEqStrings()) and is already
 * reachable at runtime through `EQT::StringDatabase::getString()` /
 * `formatString()`, which `chat_component.cpp`, `merchant.cpp`,
 * `combat_actions.cpp` and `player_component.cpp` all use. So the composers
 * transcribed by hand what one lookup fetches: a localised eqstr_XX.txt
 * would never reach them, and a transcription typo was invisible.
 *
 * ONE FORMATTER. There is exactly one `%n` substitution engine in this
 * client -- `EQT::StringDatabase::formatTemplate()` in
 * src/client/string_database.cpp -- and this header does not add a second.
 * `EqStrLine()` is a fetch-and-call wrapper over
 * `StringDatabase::formatString()`, whose only job is to turn the table's
 * "" miss into "draw no line at all".
 *
 * THE ONE THING THE SHARED ENGINE CANNOT DO, and the surprise of this unit:
 * eqstr mixes two placeholder conventions. Nearly every row uses the
 * positional `%1..%n` the engine implements, but the two item cast-time
 * qualifiers are C PRINTF formats --
 *
 *     3030 (Casting Time: %2.1f)
 *     3033 (Must Equip.  Casting Time: %2.1f)
 *
 * -- where `%2.1f` is one float conversion (width 2, precision 1), not
 * argument two followed by the literal ".1f". Feeding that to the
 * positional engine yields "(Casting Time: 4.5.1f)". `EqStrPrintfFloat()`
 * handles exactly that shape, in one place, and is the reason it is here
 * rather than open-coded in the item composer.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
class StringDatabase;
}

namespace eqt {

/**
 * eqstr `id`'s shipped format, with `args` substituted through the client's
 * one `%n` engine (`EQT::StringDatabase::formatString()`).
 *
 * Returns "" when the string table has no row for `id` -- which every
 * caller treats as "the oracle has no such line, draw nothing", never as an
 * empty line. That is deliberate: a composer must not fall back to a
 * hardcoded copy of the format, because a fallback copy is the defect B390
 * names.
 */
std::string EqStrLine(const EQT::StringDatabase& strings, uint32_t id,
                      const std::vector<std::string>& args = {});

/**
 * eqstr `id`'s shipped format where the placeholder is a C printf FLOAT
 * conversion (`%2.1f`), with `value` rendered at the conversion's own
 * precision. Only eqstr 3030 and 3033 are in this shape in the ids this
 * client draws; see the header banner.
 *
 * Returns "" on a table miss, exactly as EqStrLine() does. If the fetched
 * row carries no float conversion it is returned unchanged, so a table that
 * ever switches those two rows to `%1` keeps working.
 */
std::string EqStrPrintfFloat(const EQT::StringDatabase& strings, uint32_t id,
                             double value);

}  // namespace eqt
