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
#include "client/help/renderer_command_catalog.h"
#include "client/state/game_state.h"

#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <regex>
#include <set>
#include <sstream>
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
    // T223: registry 3's static catalog is always counted -- it does not
    // depend on either live registry object.
    EXPECT_EQ(cov.rendererCount, AllRendererCommands().size());
    EXPECT_GT(cov.rendererCount, 0u);
    EXPECT_EQ(cov.totalUnique,
              cov.gameCount + cov.actionCount - cov.dedupedCount + cov.rendererCount);
}

TEST_F(MergedHelpTest, CoverageWithNullRegistriesIsZero) {
    HelpCoverage cov = ComputeHelpCoverage(nullptr, nullptr);
    EXPECT_EQ(cov.gameCount, 0u);
    EXPECT_EQ(cov.actionCount, 0u);
    EXPECT_EQ(cov.dedupedCount, 0u);
    // T223: only registry 3 (static) contributes with both live registries null.
    EXPECT_EQ(cov.rendererCount, AllRendererCommands().size());
    EXPECT_EQ(cov.totalUnique, cov.rendererCount);
}

// ===================================================================
// T223: registry 3 (renderer if/else chain) via its static catalog
// ===================================================================

// The T143 defect: the skills-window toggle -- then spelled /newskills, and
// the only working one -- was in NO registry, so "/help newskills" said
// "Unknown command". T363 (B058) renamed it to /skills with NO alias, so the
// old spelling must now genuinely be unknown.
TEST_F(MergedHelpTest, T363_SkillsIsListedByHelpUnderGraphics) {
    HelpResponse r = BuildHelpResponse("help graphics", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_FALSE(ContainsSubstring(r.lines, "Unknown command"));
    EXPECT_TRUE(ContainsSubstring(r.lines, "/skills"))
        << "the renamed command is not in the Graphics category listing";
    EXPECT_FALSE(ContainsSubstring(r.lines, "/newskills"));
}

// T363, MEASURED, not assumed: the new name COLLIDES with registry 2's
// "Skills" CATEGORY (bandage/train/trainclose/track), and BuildHelpResponse
// resolves a bare argument as a category BEFORE a command name. So
// `/help skills` prints the Skills category, and the command's own detail
// block ("=== skills ===", "Registry: renderer (graphics)") is unreachable by
// that name -- which `/help newskills` used to reach. The command itself is
// unaffected; only its help topic is shadowed. Boarded as B090 rather than
// changed here: flipping the resolution order would shadow every category
// that shares a name with a command, a strictly wider behaviour change than
// this unit is scoped for.
// T365 (B092), renamed from T363_HelpSkillsResolvesToTheSkillsCategoryNotThe-
// Command. `skills` is BOTH a registry-3 command (T363's rename) and a
// registry-2 category, and the category used to win outright, leaving the
// command's own detail block unreachable by name. Resolution is now
// command-first, with the category appended after a note -- so both are in
// the one response.
TEST_F(MergedHelpTest, T365_HelpSkillsPrintsTheCommandFirstThenTheCategory) {
    HelpResponse r = BuildHelpResponse("help skills", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_FALSE(ContainsSubstring(r.lines, "Unknown command"));
    // The command detail, which B092 made unreachable.
    EXPECT_TRUE(ContainsSubstring(r.lines, "=== skills ==="));
    EXPECT_TRUE(ContainsSubstring(r.lines, "Registry: renderer (graphics)"));
    // The category, still printed, after the note.
    EXPECT_TRUE(ContainsSubstring(r.lines, "\"Skills\" is also a category:"));
    EXPECT_TRUE(ContainsSubstring(r.lines, "=== Skills ==="));
    EXPECT_TRUE(ContainsSubstring(r.lines, "/bandage"));
    // Order: the command block precedes the note.
    size_t cmdAt = r.lines.size(), noteAt = r.lines.size();
    for (size_t i = 0; i < r.lines.size(); ++i) {
        if (cmdAt == r.lines.size() && r.lines[i].find("Registry: renderer") != std::string::npos) cmdAt = i;
        if (noteAt == r.lines.size() && r.lines[i].find("is also a category") != std::string::npos) noteAt = i;
    }
    EXPECT_LT(cmdAt, noteAt);
}

// T365: the same collision one command over -- `/inventory` (renamed from
// `/newinv` by this unit) against registry 2's "Inventory" category.
TEST_F(MergedHelpTest, T365_HelpInventoryPrintsTheCommandFirstThenTheCategory) {
    HelpResponse r = BuildHelpResponse("help inventory", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_TRUE(ContainsSubstring(r.lines, "=== inventory ==="));
    EXPECT_TRUE(ContainsSubstring(r.lines, "Registry: renderer (graphics)"));
    EXPECT_TRUE(ContainsSubstring(r.lines, "\"Inventory\" is also a category:"));
}

// T365: a category that is NOT also a command name must be unchanged -- no
// note, no "Unknown command", just the category block.
TEST_F(MergedHelpTest, T365_ACategoryThatIsNotACommandStillResolvesToTheCategory) {
    HelpResponse r = BuildHelpResponse("help Movement", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_FALSE(ContainsSubstring(r.lines, "Unknown command"));
    EXPECT_FALSE(ContainsSubstring(r.lines, "is also a category"));
    EXPECT_TRUE(ContainsSubstring(r.lines, "=== Movement ==="));
}

// T365 (B090): the registry-2 half of the `inv` collision. It is /invite's
// alias, and registry 2 is tried before registry 3 -- which is why
// ConsoleInputHandler rewrites the bare `inv` keyword to `inventory` instead
// of letting it fall through, and why `/inventory` has no `inv` alias.
TEST_F(MergedHelpTest, T365_InvIsInvitesAliasNotInventorys) {
    HelpResponse r = BuildHelpResponse("help inv", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_TRUE(ContainsSubstring(r.lines, "=== invite ==="));
    EXPECT_FALSE(ContainsSubstring(r.lines, "=== inventory ==="));
}

// T365: the old `/newinv` spelling is gone from every registry.
TEST_F(MergedHelpTest, T365_NewInvIsUnknownToHelp) {
    HelpResponse r = BuildHelpResponse("help newinv", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_TRUE(ContainsSubstring(r.lines, "Unknown command"));
}

TEST_F(MergedHelpTest, T363_NewSkillsIsUnknownToHelp) {
    HelpResponse r = BuildHelpResponse("help newskills", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_TRUE(ContainsSubstring(r.lines, "Unknown command"))
        << "the old spelling is still in a registry -- the alias was not removed";
}

TEST_F(MergedHelpTest, T223_RendererAliasIsFoundByHelp) {
    HelpResponse r = BuildHelpResponse("help ds", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_TRUE(ContainsSubstring(r.lines, "=== dumpscene ==="));
    EXPECT_TRUE(ContainsSubstring(r.lines, "/ds"));
}

TEST_F(MergedHelpTest, T223_OverviewListsGraphicsCategoryWithTag) {
    HelpResponse r = BuildHelpResponse("help", &game, processor.get());
    ASSERT_TRUE(r.handled);
    EXPECT_TRUE(ContainsSubstring(r.lines, "Graphics (graphics):"));
    EXPECT_TRUE(ContainsSubstring(r.lines, "skills"));
    EXPECT_FALSE(ContainsSubstring(r.lines, "newskills"))
        << "the overview still lists the old spelling";
}

TEST_F(MergedHelpTest, T223_GraphicsCategoryDetailTagsEveryLine) {
    HelpResponse r = BuildHelpResponse("help graphics", &game, processor.get());
    ASSERT_TRUE(r.handled);
    ASSERT_GE(r.lines.size(), 2u);
    EXPECT_EQ(r.lines[0], "=== Graphics ===");
    for (size_t i = 1; i < r.lines.size(); ++i) {
        EXPECT_NE(r.lines[i].find("(graphics)"), std::string::npos) << r.lines[i];
    }
    EXPECT_EQ(r.lines.size() - 1, AllRendererCommands().size());
}

TEST(RendererCommandCatalogTest, LookupIsCaseInsensitiveAndSlashTolerant) {
    ASSERT_NE(FindRendererCommand("skills"), nullptr);
    ASSERT_NE(FindRendererCommand("/Skills"), nullptr);
    EXPECT_EQ(FindRendererCommand("/Skills")->name, "skills");
    // T363: no alias was kept for the old spelling.
    EXPECT_EQ(FindRendererCommand("newskills"), nullptr);
    EXPECT_EQ(FindRendererCommand("WQ")->name, "weatherquality");
    EXPECT_EQ(FindRendererCommand("nosuchcommand"), nullptr);
    EXPECT_EQ(FindRendererCommand(""), nullptr);
    EXPECT_EQ(FindRendererCommand("/"), nullptr);
}

TEST(RendererCommandCatalogTest, NoDuplicateIdentifiers) {
    std::set<std::string> seen;
    for (const auto& info : AllRendererCommands()) {
        EXPECT_TRUE(seen.insert(info.name).second) << "duplicate: " << info.name;
        for (const auto& a : info.aliases) {
            EXPECT_TRUE(seen.insert(a).second) << "duplicate alias: " << a;
        }
    }
}

// DRIFT GUARD (the plan's "derive from the if-chain, don't hand-duplicate"):
// the catalog is a hand-written table, so this test derives the truth from
// the dispatcher's own source text -- every `cmd == "/name"` literal in
// IrrlichtRenderer::processSlashCommand() -- and requires set equality in
// both directions. A branch added without a catalog entry, or an entry
// whose branch was removed, fails here.
TEST(RendererCommandCatalogTest, MatchesRendererIfChainExactly) {
#ifndef RENDERER_COMMAND_SOURCE
    GTEST_SKIP() << "RENDERER_COMMAND_SOURCE not defined at configure time";
#else
    std::ifstream in(RENDERER_COMMAND_SOURCE);
    ASSERT_TRUE(in.is_open()) << RENDERER_COMMAND_SOURCE;
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string src = buf.str();
    // L06 (Batch L, L05 correction 21): the body's OWNER changed when the
    // slash family became SlashCommandsComponent, so this matcher accepts
    // either spelling and requires EXACTLY ONE definition -- two would be the
    // duplication this guard exists to stop, and a matcher that silently found
    // none would pass vacuously once the ASSERT below is the only thing left.
    static const char* const kOwners[] = {
        "IrrlichtRenderer::processSlashCommand(",
        "SlashCommandsComponent::processSlashCommand(",
    };
    size_t fnStart = std::string::npos;
    int definitionsFound = 0;
    for (const char* owner : kOwners) {
        const size_t at = src.find(owner);
        if (at != std::string::npos) { ++definitionsFound; fnStart = at; }
    }
    ASSERT_EQ(definitionsFound, 1)
        << "expected exactly one processSlashCommand() definition in "
        << RENDERER_COMMAND_SOURCE << ", found " << definitionsFound;
    ASSERT_NE(fnStart, std::string::npos);
    // The chain ends at the next member definition after the function, or at
    // end of file: since J11 processSlashCommand() is the last definition in
    // renderer_slash_commands.cpp, so there is no following member.
    size_t fnEnd = std::string::npos;
    for (const char* next : {"\nvoid IrrlichtRenderer::", "\nvoid SlashCommandsComponent::"}) {
        const size_t at = src.find(next, fnStart + 1);
        if (at != std::string::npos && (fnEnd == std::string::npos || at < fnEnd)) fnEnd = at;
    }
    if (fnEnd == std::string::npos) fnEnd = src.size();
    const std::string body = src.substr(fnStart, fnEnd - fnStart);

    std::set<std::string> fromChain;
    const std::regex literal(R"re(cmd == "/([A-Za-z0-9_]+)")re");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), literal);
         it != std::sregex_iterator(); ++it) {
        fromChain.insert((*it)[1].str());
    }
    ASSERT_GT(fromChain.size(), 30u) << "parser found too few literals -- pattern drift?";

    std::set<std::string> fromCatalog;
    for (const auto& info : AllRendererCommands()) {
        fromCatalog.insert(info.name);
        for (const auto& a : info.aliases) fromCatalog.insert(a);
    }

    for (const auto& n : fromChain) {
        EXPECT_TRUE(fromCatalog.count(n)) << "in if-chain but NOT in catalog: /" << n;
    }
    for (const auto& n : fromCatalog) {
        EXPECT_TRUE(fromChain.count(n)) << "in catalog but NOT in if-chain: /" << n;
    }
    EXPECT_EQ(fromChain.size(), fromCatalog.size());
#endif
}
