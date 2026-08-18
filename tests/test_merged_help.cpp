// T143: registry-aware /help. Builds a real eqt::ui::CommandRegistry
// (registry 1's type) and a real eqt::action::CommandProcessor (registry
// 2's type, driven through a recording IActionHandler fake -- same idiom
// test_command_processor.cpp uses) with deliberately overlapping and
// non-overlapping command names, and asserts on eqt::help::BuildHelpResponse
// / ComputeHelpCoverage directly. No Application, no EverQuest, no
// Irrlicht -- see refactor/T143_registry_aware_help.md.

#include "action_test_fakes.h"
#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/graphics/ui/command_registry.h"
#include "client/help/merged_help.h"
#include "client/state/game_state.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <string>

using namespace eqt::action;
using namespace eqt::action::test;
using namespace eqt::ui;
using namespace eqt::help;
using eqt::state::GameState;

namespace {

bool ContainsSubstring(const std::vector<std::string>& lines, const std::string& needle) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(needle) != std::string::npos;
    });
}

} // namespace

class MergedHelpTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<ActionDispatcher>(state);
        dispatcher->setActionHandler(&handler);
        state.world().setZoneConnected(true);
        processor = std::make_unique<CommandProcessor>(state, *dispatcher);

        // Game registry (registry 1): one command with no action-side
        // counterpart ("gameonly"), plus one that DOES collide with an
        // action-registry command below ("shared", alias "tsalias").
        // Category "Utility" is one of the six CommandRegistry pre-registers
        // in its constructor (see command_registry.cpp) -- reused rather
        // than a made-up category name so getCategories() (which only
        // returns registerCategory()'d names, not every cmd.category value
        // in use) actually surfaces it, matching real production categories.
        Command gameOnly;
        gameOnly.name = "gameonly";
        gameOnly.usage = "/gameonly";
        gameOnly.description = "Game-only command";
        gameOnly.category = "Utility";
        gameOnly.handler = [](const std::string&) {};
        game.registerCommand(gameOnly);

        Command shared;
        shared.name = "shared";
        shared.aliases = {"tsalias"};
        shared.usage = "/shared";
        shared.description = "Game's version of shared";
        shared.category = "Utility";
        shared.handler = [](const std::string&) {};
        game.registerCommand(shared);

        // Action registry (registry 2): CommandProcessor's constructor
        // registers ~112 real built-in commands (registerBuiltinCommands())
        // -- this is a REAL production registry, not an empty fixture, so
        // coverage-count assertions below read the actual baseline rather
        // than assuming an empty starting point. One command with no
        // game-side counterpart ("actiononly"), plus a "shared" registration
        // that collides with the game one above (must be dedup'd / shadowed).
        processor->registerCommand(
            {"actiononly", {}, "/actiononly", "Action-only command", "Utility", false},
            [](const std::string&) { return ActionResult::Success(); });
        processor->registerCommand(
            {"shared", {"tsalias"}, "/shared", "Action's version of shared", "Utility", false},
            [](const std::string&) { return ActionResult::Success(); });
    }

    GameState state;
    RecordingActionHandler handler;
    std::unique_ptr<ActionDispatcher> dispatcher;
    std::unique_ptr<CommandProcessor> processor;
    CommandRegistry game;
};

// ===================================================================
// IsHelpCommandName
// ===================================================================

TEST(MergedHelpNameTest, RecognizesHelpAndAliases) {
    EXPECT_TRUE(IsHelpCommandName("help"));
    EXPECT_TRUE(IsHelpCommandName("Help"));
    EXPECT_TRUE(IsHelpCommandName("h"));
    EXPECT_TRUE(IsHelpCommandName("H"));
    EXPECT_TRUE(IsHelpCommandName("?"));
}

TEST(MergedHelpNameTest, RejectsOtherNames) {
    EXPECT_FALSE(IsHelpCommandName("hello"));
    EXPECT_FALSE(IsHelpCommandName("who"));
    EXPECT_FALSE(IsHelpCommandName(""));
}

// ===================================================================
// BuildHelpResponse: gating
// ===================================================================

TEST_F(MergedHelpTest, NonHelpCommandIsNotHandled) {
    HelpResponse resp = BuildHelpResponse("gameonly", &game, processor.get());
    EXPECT_FALSE(resp.handled);
    EXPECT_TRUE(resp.lines.empty());
}

TEST_F(MergedHelpTest, HelpAliasesAllHandled) {
    EXPECT_TRUE(BuildHelpResponse("help", &game, processor.get()).handled);
    EXPECT_TRUE(BuildHelpResponse("h", &game, processor.get()).handled);
    EXPECT_TRUE(BuildHelpResponse("?", &game, processor.get()).handled);
    EXPECT_TRUE(BuildHelpResponse("help gameonly", &game, processor.get()).handled);
}

// ===================================================================
// BuildHelpResponse: no-args overview surfaces BOTH registries
// ===================================================================

TEST_F(MergedHelpTest, OverviewListsCommandsFromBothRegistries) {
    HelpResponse resp = BuildHelpResponse("help", &game, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "gameonly"));
    EXPECT_TRUE(ContainsSubstring(resp.lines, "actiononly"));
}

// This is the case the plan calls out as "the one that will rot": a name
// present in both registries must appear exactly ONCE in the overview, not
// twice.
TEST_F(MergedHelpTest, NamePresentInBothRegistriesAppearsExactlyOnce) {
    HelpResponse resp = BuildHelpResponse("help", &game, processor.get());
    ASSERT_TRUE(resp.handled);

    int occurrences = 0;
    for (const auto& line : resp.lines) {
        size_t pos = 0;
        while ((pos = line.find("shared", pos)) != std::string::npos) {
            ++occurrences;
            pos += 1;
        }
    }
    EXPECT_EQ(occurrences, 1);
}

TEST_F(MergedHelpTest, GameOnlyResponseStillWorksWithNullAction) {
    HelpResponse resp = BuildHelpResponse("help", &game, nullptr);
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "gameonly"));
    EXPECT_FALSE(ContainsSubstring(resp.lines, "actiononly"));
}

TEST_F(MergedHelpTest, ActionOnlyResponseStillWorksWithNullGame) {
    HelpResponse resp = BuildHelpResponse("help", nullptr, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "actiononly"));
}

// ===================================================================
// BuildHelpResponse: specific-command lookup, game wins on collision
// ===================================================================

TEST_F(MergedHelpTest, GameOnlyCommandDetail) {
    HelpResponse resp = BuildHelpResponse("help gameonly", &game, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "gameonly"));
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Game-only command"));
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Registry: game"));
}

TEST_F(MergedHelpTest, ActionOnlyCommandDetail) {
    HelpResponse resp = BuildHelpResponse("help actiononly", &game, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "actiononly"));
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Action-only command"));
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Registry: action"));
}

// The collision case: game is tried first, so it must win -- this is the
// actual runtime dispatch order (Application::dispatchToActionOrRenderer
// tries registry 1 via EverQuest::DispatchSlashCommand before ever trying
// registry 2), so a help view that showed the action version would be
// describing a command the user cannot actually reach that way.
TEST_F(MergedHelpTest, SharedCommandDetail_GameWins) {
    HelpResponse resp = BuildHelpResponse("help shared", &game, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Game's version of shared"));
    EXPECT_FALSE(ContainsSubstring(resp.lines, "Action's version of shared"));
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Registry: game"));
}

// The alias "tsalias" is registered on BOTH the game "shared" command and
// the action "shared" command -- resolving by alias must also prefer game.
TEST_F(MergedHelpTest, SharedAliasDetail_GameWins) {
    HelpResponse resp = BuildHelpResponse("help tsalias", &game, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Game's version of shared"));
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Registry: game"));
}

TEST_F(MergedHelpTest, UnknownCommandDetail) {
    HelpResponse resp = BuildHelpResponse("help nosuchcommand", &game, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "Unknown command: nosuchcommand"));
}

// ===================================================================
// BuildHelpResponse: category lookup (new -- neither registry supported
// this before T143)
// ===================================================================

TEST_F(MergedHelpTest, CategoryDetailMergesBothRegistriesAndDedups) {
    HelpResponse resp = BuildHelpResponse("help Utility", &game, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "gameonly"));
    EXPECT_TRUE(ContainsSubstring(resp.lines, "actiononly"));

    int occurrences = 0;
    for (const auto& line : resp.lines) {
        if (line.find("/shared") != std::string::npos) ++occurrences;
    }
    EXPECT_EQ(occurrences, 1);
}

TEST_F(MergedHelpTest, CategoryLookupIsCaseInsensitive) {
    HelpResponse resp = BuildHelpResponse("help utility", &game, processor.get());
    ASSERT_TRUE(resp.handled);
    EXPECT_TRUE(ContainsSubstring(resp.lines, "=== Utility ==="));
}

// ===================================================================
// ComputeHelpCoverage
// ===================================================================

TEST_F(MergedHelpTest, CoverageCountsAndDedup) {
    HelpCoverage cov = ComputeHelpCoverage(&game, processor.get());
    // game: gameonly, shared = 2, a fresh CommandRegistry with nothing else
    // registered.
    EXPECT_EQ(cov.gameCount, 2u);
    // action: CommandProcessor's real ~112 built-ins (registerBuiltinCommands(),
    // run unconditionally by its constructor) plus our 2 test registrations
    // (actiononly, shared) -- read the real total rather than assuming an
    // empty starting registry.
    EXPECT_EQ(cov.actionCount, processor->getAllCommands().size());
    // Exactly one collision: action's "shared" (name AND its "tsalias"
    // alias) against game's "shared". "actiononly" and the ~112 built-ins
    // are all distinct from {gameonly, shared, tsalias}.
    EXPECT_EQ(cov.dedupedCount, 1u);
    EXPECT_EQ(cov.totalUnique, cov.gameCount + cov.actionCount - cov.dedupedCount);
}

TEST_F(MergedHelpTest, CoverageWithNullRegistriesIsZero) {
    HelpCoverage cov = ComputeHelpCoverage(nullptr, nullptr);
    EXPECT_EQ(cov.gameCount, 0u);
    EXPECT_EQ(cov.actionCount, 0u);
    EXPECT_EQ(cov.dedupedCount, 0u);
    EXPECT_EQ(cov.totalUnique, 0u);
}
