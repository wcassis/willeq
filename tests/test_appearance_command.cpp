// T153 -- /appearance parse and validation.
//
// EverQuest::SendFaceChange() had zero production callers before T153, so
// nothing had ever exercised it (T151's framing: a zero-caller sender is
// UNVERIFIED, not "correct but unreached"). This file pins the command layer
// that now reaches it.
//
// Scope: the parse/validation path only. The merge with the character's
// current appearance happens below IActionHandler, in EqActionHandler, and so
// cannot appear here -- by design, since only the handler can see that state.
//
// A new file rather than more of test_command_processor.cpp: that file is
// already 3168 lines, over check_file_sizes.sh's 3000-line FAIL ceiling, and
// growing it further would deepen a pre-existing failure.

#include "action_test_fakes.h"
#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/state/game_state.h"

#include <gtest/gtest.h>
#include <memory>

using namespace eqt::action;
using namespace eqt::action::test;
using eqt::state::GameState;

namespace {

class AppearanceCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<ActionDispatcher>(state);
        dispatcher->setActionHandler(&handler);
        state.world().setZoneConnected(true);
        processor = std::make_unique<CommandProcessor>(state, *dispatcher);
    }

    GameState state;
    RecordingActionHandler handler;
    std::unique_ptr<ActionDispatcher> dispatcher;
    std::unique_ptr<CommandProcessor> processor;
};

// The most important assertion here is the NAME. The command is /appearance,
// not /face: /face is the *movement* face and has been since T98, and T146
// already lost a leg to that exact collision.
TEST_F(AppearanceCommandTest, RegisteredUnderCharacter_AndFaceIsStillMovement) {
    auto info = processor->getCommandInfo("appearance");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->category, "Character");

    auto faceInfo = processor->getCommandInfo("face");
    ASSERT_TRUE(faceInfo.has_value());
    EXPECT_EQ(faceInfo->category, "Movement");
}

TEST_F(AppearanceCommandTest, SetsOnlyTheNamedFields) {
    EXPECT_TRUE(processor->processCommand("/appearance face 3").success);
    ASSERT_TRUE(handler.calledOnceWith("setAppearance(face=3)"));

    handler.clear();
    EXPECT_TRUE(processor->processCommand("/appearance hairstyle 2 haircolor 7").success);
    ASSERT_TRUE(handler.calledOnceWith("setAppearance(haircolor=7 hairstyle=2)"));
}

TEST_F(AppearanceCommandTest, AcceptsAllSevenFieldNames) {
    EXPECT_TRUE(processor->processCommand(
        "/appearance haircolor 1 beardcolor 2 eyecolor1 3 eyecolor2 4 "
        "hairstyle 5 beard 6 face 7").success);
    ASSERT_TRUE(handler.calledOnceWith(
        "setAppearance(haircolor=1 beardcolor=2 eyecolor1=3 eyecolor2=4 "
        "hairstyle=5 beard=6 face=7)"));
}

TEST_F(AppearanceCommandTest, FieldNamesAreCaseInsensitive) {
    EXPECT_TRUE(processor->processCommand("/appearance FACE 3").success);
    ASSERT_TRUE(handler.calledOnceWith("setAppearance(face=3)"));
}

TEST_F(AppearanceCommandTest, RejectsBadInputWithoutDispatching) {
    // Every field is a uint8 in Titanium::structs::FaceChange_Struct. 256 and
    // -1 are exactly the unvalidated casts T95 made a tracked defect class:
    // they must be rejected, not silently truncated to 0 and 255.
    for (const char* bad : {"/appearance",
                            "/appearance face",
                            "/appearance face 3 hairstyle",
                            "/appearance nose 3",
                            "/appearance face 256",
                            "/appearance face -1",
                            "/appearance face 3x",
                            "/appearance face abc"}) {
        handler.clear();
        auto result = processor->processCommand(bad);
        EXPECT_FALSE(result.success) << "should have failed: " << bad;
        EXPECT_EQ(handler.count(), 0u) << "should not have dispatched: " << bad;
    }
}

TEST_F(AppearanceCommandTest, BoundaryValuesAreAccepted) {
    EXPECT_TRUE(processor->processCommand("/appearance face 0 beard 255").success);
    ASSERT_TRUE(handler.calledOnceWith("setAppearance(beard=255 face=0)"));
}

TEST_F(AppearanceCommandTest, NotConnectedToZone_FailsWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/appearance face 3");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

}  // namespace
