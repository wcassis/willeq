/*
 * T429 Leg B -- see include/client/commands/consider_text.h for the oracle,
 * for the faction-order trap, and for what in this line is NOT the oracle's.
 */

#include "client/commands/consider_text.h"

#include "client/display_eqstr.h"
#include "client/string_database.h"

namespace eqt {
namespace commands {

uint32_t ConsiderFactionStringId(uint32_t factionValue) {
    // Server FACTION_VALUE -> eqstr id. The table's order is not the enum's
    // for 6..9; see the header. Source of the enum:
    // /mnt/projects/akk-stack/code/common/faction.h:26-35.
    switch (factionValue) {
        case 1: return 12212;  // FACTION_ALLY
        case 2: return 12213;  // FACTION_WARMLY
        case 3: return 12214;  // FACTION_KINDLY
        case 4: return 12215;  // FACTION_AMIABLY
        case 5: return 12216;  // FACTION_INDIFFERENTLY
        case 6: return 12220;  // FACTION_APPREHENSIVELY
        case 7: return 12219;  // FACTION_DUBIOUSLY
        case 8: return 12218;  // FACTION_THREATENINGLY
        case 9: return 12217;  // FACTION_SCOWLS
        default: return 0;
    }
}

std::string BuildConsiderText(const EQT::StringDatabase& strings,
                              const std::string& targetName,
                              uint32_t factionValue,
                              const std::string& difficulty) {
    const uint32_t factionId = ConsiderFactionStringId(factionValue);
    // An unknown faction value has no oracle phrase. Rather than invent one,
    // the phrase is left empty and 12239 renders "<name>  -- <tail>"; the
    // value itself goes to the log, not to the player's chat window.
    const std::string phrase = factionId != 0 ? EqStrLine(strings, factionId)
                                              : std::string();
    return EqStrLine(strings, kConStrLine, {targetName, phrase, difficulty});
}

}  // namespace commands
}  // namespace eqt
