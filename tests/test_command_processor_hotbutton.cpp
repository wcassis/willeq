// B1025-H3 (GitLab #345): /hotbutton [Name] <Color> [Text] -- the game
// thread's one hot-button producer. The oracle's session form is
// `/hotbutton H0 1 /say hi` (H-O gestures.tsv "social-command"): a name, a
// colour number and a macro. The macro is the button's TEXT (the Emote's
// emoteText, exactly what ActivateHotbarCommandSlot runs when the slot is
// pressed); the name is the label and the colour an accepted-and-ignored
// field (the model carries no colour -- command_processor_social.cpp's
// cmdHotbutton). The button lands on the cursor through the REAL producer
// chain: CommandProcessor -> ActionDispatcher::createHotbutton ->
// IActionHandler (recorded here), the same four layers every other command
// in this binary drives.
//
// A NEW source of the test_command_processor target (T165's split idiom), so
// it costs no extra binary or discovery pass.

#include "command_processor_test_fixture.h"

#include <gtest/gtest.h>

using namespace eqt::action;
using namespace eqt::action::test;
using eqt::tests::CommandProcessorTest;

namespace {

TEST_F(CommandProcessorTest, Hotbutton_RegisteredBesideUseskill) {
    auto info = processor->getCommandInfo("hotbutton");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->category, "Combat");
    EXPECT_TRUE(processor->hasCommand("hotbutton"));
}

// THE oracle's own invocation: the macro rejoining its own spaces and
// reaching the handler as the button's text.
TEST_F(CommandProcessorTest, Hotbutton_OracleFormCreatesTheSocialButton) {
    auto result = processor->processCommand("/hotbutton H0 1 /say hi");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_EQ(handler.calls.size(), 1u);
    EXPECT_EQ(handler.calls[0], "createHotbutton(\"/say hi\")");
    EXPECT_EQ(result.message, "Successfully created hotbutton");
}

// No arguments: eqstr 13278's usage line, nothing dispatched.
TEST_F(CommandProcessorTest, Hotbutton_NoArgsShowsUsage) {
    auto result = processor->processCommand("/hotbutton");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "/Hotbutton [Name] <Color> [Text]");
    EXPECT_TRUE(handler.calls.empty());
}

// A non-numeric colour is a mistyped macro: eqstr 13283, nothing dispatched.
TEST_F(CommandProcessorTest, Hotbutton_NonNumericColorRefuses) {
    auto result = processor->processCommand("/hotbutton H0 red /say hi");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message,
              "Error.  Could not create hotbutton.  Type /hotbutton for help.");
    EXPECT_TRUE(handler.calls.empty());
}

// A colour with no macro: the button would carry no text, which the
// dispatcher's own guard refuses (the command's parse reaches the empty
// text, not the macro).
TEST_F(CommandProcessorTest, Hotbutton_MissingTextRefuses) {
    auto result = processor->processCommand("/hotbutton H0 1");
    // Two tokens: name + colour, no text -> the dispatcher's empty-text
    // refusal (the zone is connected in the fixture, so this is the text
    // guard, not the connection guard).
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message,
              "Error.  Could not create hotbutton.  Type /hotbutton for help.");
    EXPECT_TRUE(handler.calls.empty());
}

// eqstr 13281: the macro is up to 60 characters; 61 refuses.
TEST_F(CommandProcessorTest, Hotbutton_OversizedTextRefuses) {
    std::string macro = std::string(61, 'x');
    auto result = processor->processCommand("/hotbutton H0 1 " + macro);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message,
              "Error.  Could not create hotbutton.  Type /hotbutton for help.");
    EXPECT_TRUE(handler.calls.empty());
}

// A 60-character macro is exactly at the limit and dispatches.
TEST_F(CommandProcessorTest, Hotbutton_ExactlySixtyCharactersDispatches) {
    std::string macro = std::string(60, 'x');
    auto result = processor->processCommand("/hotbutton H0 1 " + macro);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_EQ(handler.calls.size(), 1u);
    EXPECT_EQ(handler.calls[0], "createHotbutton(\"" + macro + "\")");
}

// The headless refusal: the handler cannot publish (no bridge), so the
// dispatcher reports eqstr 13283 even though the parse succeeded.
TEST_F(CommandProcessorTest, Hotbutton_PublishFailureRefuses) {
    handler.hotbuttonPublishFails = true;
    auto result = processor->processCommand("/hotbutton H0 1 /say hi");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message,
              "Error.  Could not create hotbutton.  Type /hotbutton for help.");
}

// Not zoned: the dispatcher's connection guard fires before anything else.
TEST_F(CommandProcessorTest, Hotbutton_NotZonedFailsCleanly) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/hotbutton H0 1 /say hi");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.calls.empty());
}

}  // namespace
