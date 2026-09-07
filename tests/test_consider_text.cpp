// T429 Leg B (board B298 entity half, board B390) -- the /con line.
//
// WHAT WAS BROKEN. ZoneProcessConsider built the line with fmt::format out of
// nine hardcoded faction phrases. Eight were faithful copies of the official
// client's string table; faction 8 (FACTION_THREATENINGLY,
// /mnt/projects/akk-stack/code/common/faction.h:34) printed "threatens you",
// which appears in NO row of /mnt/projects/EverQuestP1999/eqstr_us.txt at all.
// The oracle's phrase is 12218 "glares at you threateningly". That is the
// silent transcription drift board B390 predicted, found by fixing it.
//
// The composer is free, so these cases need no server, no bridge, no device.

#include <gtest/gtest.h>

#include "eqstr_test_db.h"

#include "client/commands/consider_text.h"
#include "client/string_database.h"

#include <string>

namespace {

using eqt::commands::BuildConsiderText;
using eqt::commands::ConsiderEqStrIds;
using eqt::commands::ConsiderFactionStringId;

const EQT::StringDatabase& strings() { return eqt_test::ShippedStrings(); }

}  // namespace

// The whole line is eqstr 12239 "%1 %2 -- %3".
TEST(ConsiderTextT429, TheLineIsTheOraclesOwnFormat) {
    ASSERT_TRUE(strings().isLoaded()) << WILLEQ_DB_REAL_PATH;
    EXPECT_EQ(strings().getString(eqt::commands::kConStrLine), "%1 %2 -- %3");

    EXPECT_EQ(BuildConsiderText(strings(), "a gnoll pup", 5, "cons green"),
              "a gnoll pup regards you indifferently -- cons green");
}

// THE DEFECT. Faction 8 is 12218, not the invented "threatens you".
TEST(ConsiderTextT429, FactionEightIsTheOraclesThreateningPhrase) {
    ASSERT_TRUE(strings().isLoaded());
    EXPECT_EQ(ConsiderFactionStringId(8), 12218u);
    EXPECT_EQ(strings().getString(12218), "glares at you threateningly");

    const std::string line = BuildConsiderText(strings(), "Sirran", 8, "cons red");
    EXPECT_EQ(line, "Sirran glares at you threateningly -- cons red");
    EXPECT_EQ(line.find("threatens you"), std::string::npos)
        << "\"threatens you\" is in no eqstr row; it was fable's own invention";
}

// The table's order is NOT the server's faction order for 6..9. Pinning the
// whole map keeps a future edit from re-shuffling it.
TEST(ConsiderTextT429, EveryFactionValueMapsToItsOraclePhrase) {
    ASSERT_TRUE(strings().isLoaded());
    const std::pair<uint32_t, const char*> expected[] = {
        {1, "regards you as an ally"},
        {2, "looks upon you warmly"},
        {3, "kindly considers you"},
        {4, "judges you amiably"},
        {5, "regards you indifferently"},
        {6, "looks your way apprehensively"},
        {7, "glowers at you dubiously"},
        {8, "glares at you threateningly"},
        {9, "scowls at you, ready to attack"},
    };
    for (const auto& [faction, phrase] : expected) {
        const uint32_t id = ConsiderFactionStringId(faction);
        ASSERT_NE(id, 0u) << "faction " << faction;
        EXPECT_EQ(strings().getString(id), phrase) << "faction " << faction;
        EXPECT_EQ(BuildConsiderText(strings(), "Bob", faction, "cons white"),
                  std::string("Bob ") + phrase + " -- cons white");
    }
}

// An unknown faction value gets no phrase rather than an invented one.
TEST(ConsiderTextT429, AnUnknownFactionValueInventsNothing) {
    ASSERT_TRUE(strings().isLoaded());
    EXPECT_EQ(ConsiderFactionStringId(0), 0u);
    EXPECT_EQ(ConsiderFactionStringId(99), 0u);
    const std::string line = BuildConsiderText(strings(), "Bob", 99, "cons white");
    EXPECT_EQ(line.find("faction 99"), std::string::npos)
        << "the raw value belongs in the log, not in the chat window";
}

// The table is the source: reword 12239 and the line rewords.
TEST(ConsiderTextT429, AMissingLineFormatDrawsNothing) {
    EQT::StringDatabase empty;   // never loaded: every lookup misses
    EXPECT_TRUE(BuildConsiderText(empty, "Bob", 5, "cons white").empty());
}

TEST(ConsiderTextT429, EveryConsiderIdExistsInTheShippedDb) {
    ASSERT_TRUE(strings().isLoaded());
    for (uint32_t id : ConsiderEqStrIds()) {
        EXPECT_FALSE(strings().getString(id).empty())
            << "eqstr " << id << " is drawn by the /con line and is not in "
            << "data/willeq.db's eqstrings table";
    }
}
