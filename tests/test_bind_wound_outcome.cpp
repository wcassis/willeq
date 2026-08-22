#include <gtest/gtest.h>
#include "client/bind_wound_outcome.h"

// T177: the OP_Bind_Wound reply `type` -> player message mapping. Pure and
// total; the eqemu authority for each value is documented in the header.
using EQT::bindWoundOutcomeMessage;

TEST(BindWoundOutcome, DoneShowsBound) {
    EXPECT_EQ(bindWoundOutcomeMessage(1), "You have bound your wounds.");
}

TEST(BindWoundOutcome, DeadOrNoTarget) {
    EXPECT_EQ(bindWoundOutcomeMessage(4), "You cannot bind wounds on that target.");
}

TEST(BindWoundOutcome, TargetLeftZone) {
    EXPECT_EQ(bindWoundOutcomeMessage(5),
              "Your bind wound target is no longer in the zone.");
}

// The headline: the missing-bandage case that the player saw nothing for.
TEST(BindWoundOutcome, NoBandage) {
    EXPECT_EQ(bindWoundOutcomeMessage(7), "You have no bandages.");
}

// Control/state ticks carry no outcome -- empty, so the parser shows nothing.
TEST(BindWoundOutcome, ControlTypesAreSilent) {
    EXPECT_EQ(bindWoundOutcomeMessage(0), "");  // unlock/reset
    EXPECT_EQ(bindWoundOutcomeMessage(3), "");  // start control
}

TEST(BindWoundOutcome, UnknownTypeIsSilentNotGarbage) {
    EXPECT_EQ(bindWoundOutcomeMessage(2), "");
    EXPECT_EQ(bindWoundOutcomeMessage(99), "");
}
