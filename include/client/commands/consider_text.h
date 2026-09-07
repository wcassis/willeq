/*
 * T429 Leg B (board B298, entity half; board B390) -- the /con line, and the
 * only place it is composed.
 *
 * WHAT WAS BROKEN. `ZoneProcessConsider` built the line with `fmt::format`
 * out of nine hardcoded faction phrases and an invented "-- cons <colour>"
 * tail. Eight of the nine phrases were faithful transcriptions of the
 * official client's own string table; ONE WAS NOT. Faction 8
 * (`FACTION_THREATENINGLY`, /mnt/projects/akk-stack/code/common/faction.h:34)
 * printed **"threatens you"**, a phrase that appears NOWHERE in
 * /mnt/projects/EverQuestP1999/eqstr_us.txt -- the oracle's line is 12218
 * "glares at you threateningly". That is exactly the class of silent
 * transcription drift board B390 says a fetch would have made impossible,
 * and it is why this composer fetches.
 *
 * THE ORACLE. The whole line is one shipped format:
 *
 *     12239  %1 %2 -- %3
 *
 * with `%1` the target's name, `%2` the faction phrase and `%3` the
 * difficulty tail. The nine faction phrases are 12212..12220, and the
 * table's own order is NOT the server's faction order -- 6/7/8/9 map to
 * 12220/12219/12218/12217, not to 12217..12220:
 *
 *     12212 regards you as an ally            <- FACTION_ALLY           1
 *     12213 looks upon you warmly             <- FACTION_WARMLY         2
 *     12214 kindly considers you              <- FACTION_KINDLY         3
 *     12215 judges you amiably                <- FACTION_AMIABLY        4
 *     12216 regards you indifferently         <- FACTION_INDIFFERENTLY  5
 *     12217 scowls at you, ready to attack    <- FACTION_SCOWLS         9
 *     12218 glares at you threateningly       <- FACTION_THREATENINGLY  8
 *     12219 glowers at you dubiously          <- FACTION_DUBIOUSLY      7
 *     12220 looks your way apprehensively     <- FACTION_APPREHENSIVELY 6
 *
 * WHAT IS *NOT* THE ORACLE'S, flagged rather than dressed up (the T419
 * ORACLE_ROW_H rule): the DIFFICULTY TAIL that fills `%3`. The table has
 * sixteen candidate difficulty lines (12223..12238, from "%1 would not make
 * a worthy opponent." to "looks like %1 would wipe the floor with you!"),
 * several of them near-duplicates of each other, and it carries NO mapping
 * from a con COLOUR to one of them -- the official client's own executable
 * holds that table. Choosing six of sixteen would be inference, so fable
 * keeps its own "cons <colour>" tail, which is fable's wording and says so.
 * Boarded (B417) as measurable against the official client.
 *
 * This function is FREE and takes only the string table plus the three
 * facts, so a test drives it with no server, no bridge and no device -- the
 * T421/T425 composer idiom.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
class StringDatabase;
}

namespace eqt {
namespace commands {

constexpr uint32_t kConStrLine = 12239;  // "%1 %2 -- %3"

/** eqstr id for a server FACTION_VALUE (1..9); 0 when the value is unknown. */
uint32_t ConsiderFactionStringId(uint32_t factionValue);

/** Every eqstr id the /con line can fetch, for the db census test. */
inline const std::vector<uint32_t>& ConsiderEqStrIds() {
    static const std::vector<uint32_t> ids = {
        kConStrLine,
        12212, 12213, 12214, 12215, 12216, 12217, 12218, 12219, 12220,
    };
    return ids;
}

/**
 * The /con chat line. `difficulty` fills the oracle's `%3` and is fable's
 * own con-colour wording (see the banner). Returns "" if the string table
 * has no 12239 -- the caller then prints nothing rather than a fabricated
 * line.
 */
std::string BuildConsiderText(const EQT::StringDatabase& strings,
                              const std::string& targetName,
                              uint32_t factionValue,
                              const std::string& difficulty);

}  // namespace commands
}  // namespace eqt
