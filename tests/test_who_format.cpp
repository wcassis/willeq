#include <gtest/gtest.h>
#include "client/who_format.h"

using EQT::substituteWhoCount;

TEST(WhoFormat, SubstitutesSingularPlaceholder) {
    bool sub = false;
    EXPECT_EQ(substituteWhoCount("There is %1 player in EverQuest.", 1, sub),
              "There is 1 player in EverQuest.");
    EXPECT_TRUE(sub);
}

TEST(WhoFormat, SubstitutesPluralCount) {
    bool sub = false;
    EXPECT_EQ(substituteWhoCount("There are %1 players in EverQuest.", 42, sub),
              "There are 42 players in EverQuest.");
    EXPECT_TRUE(sub);
}

TEST(WhoFormat, NoPlaceholderReportsNotSubstituted) {
    bool sub = true;
    EXPECT_EQ(substituteWhoCount("Players in EverQuest", 3, sub),
              "Players in EverQuest");
    EXPECT_FALSE(sub);  // caller then appends the count itself
}

TEST(WhoFormat, ReplacesEveryOccurrenceAndDoesNotRescanDigits) {
    // A "%1" whose replacement contains '1' must not re-trigger on the inserted
    // digits (would loop forever / double-substitute).
    bool sub = false;
    EXPECT_EQ(substituteWhoCount("%1 of %1", 1, sub), "1 of 1");
    EXPECT_TRUE(sub);
}

TEST(WhoFormat, LeavesOtherPlaceholdersUntouched) {
    bool sub = false;
    EXPECT_EQ(substituteWhoCount("%1 in %2", 5, sub), "5 in %2");
    EXPECT_TRUE(sub);
}
