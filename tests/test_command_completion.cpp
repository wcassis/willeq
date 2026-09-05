// T362 (B069) -- the shared slash-command completion helper.
//
// `action::CommandProcessor::getCompletions()` and
// `ui::CommandRegistry::getCompletions()` were duplicated-then-drifted: the
// same prefix idiom over a lowercase-keyed command map then a lowercase-keyed
// alias map, each with its own private toLower, both sorting -- and exactly
// one line apart. CommandProcessor de-duplicated (std::unique after the sort);
// CommandRegistry did not, so a command registered with its OWN NAME as an
// alias was offered twice. T357 hid that behind
// Application::commandCompletions(), which de-duplicates the union of both,
// but every other caller still saw it.
//
// Both now call eqt::completion::PrefixCompletions
// (include/client/command_completion.h). The CommandProcessor side keeps its
// existing coverage in test_command_processor.cpp; what is new here is the
// registry side (which had none) and the helper itself.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "client/command_completion.h"
#include "client/graphics/ui/command_registry.h"
#include "client/help/renderer_command_catalog.h"

namespace {

eqt::ui::Command MakeCommand(const std::string& name,
                             const std::vector<std::string>& aliases) {
    eqt::ui::Command cmd;
    cmd.name = name;
    cmd.aliases = aliases;
    cmd.usage = "/" + name;
    cmd.description = name;
    cmd.category = "Utility";
    cmd.handler = [](const std::string&) {};
    return cmd;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

size_t CountOf(const std::vector<std::string>& v, const std::string& s) {
    return static_cast<size_t>(std::count(v.begin(), v.end(), s));
}

}  // namespace

// ---------------------------------------------------------------------------
// The defect B069 names, at the registry that had it.
// ---------------------------------------------------------------------------

TEST(CommandRegistryCompletionTest, ACommandRegisteredWithItsOwnNameAsAnAliasCompletesOnce) {
    eqt::ui::CommandRegistry registry;
    // The exact registration B069 describes: the primary name repeated in the
    // alias list. Before T362 this landed in the result twice -- once from the
    // command map, once from the alias map -- and nothing removed it.
    registry.registerCommand(MakeCommand("consider", {"consider", "con"}));

    auto completions = registry.getCompletions("con");

    EXPECT_EQ(CountOf(completions, "consider"), 1u)
        << "the primary name is also its own alias -- it must be offered ONCE";
    EXPECT_EQ(CountOf(completions, "con"), 1u);
    EXPECT_EQ(completions.size(), 2u);
}

TEST(CommandRegistryCompletionTest, CompletionsAreSortedAndCoverNamesAndAliases) {
    eqt::ui::CommandRegistry registry;
    registry.registerCommand(MakeCommand("shout", {"sh"}));
    registry.registerCommand(MakeCommand("show", {}));
    registry.registerCommand(MakeCommand("say", {"s"}));

    auto completions = registry.getCompletions("sh");

    EXPECT_TRUE(std::is_sorted(completions.begin(), completions.end()));
    EXPECT_TRUE(Contains(completions, "shout"));
    EXPECT_TRUE(Contains(completions, "show"));
    EXPECT_TRUE(Contains(completions, "sh"));      // the alias itself
    EXPECT_FALSE(Contains(completions, "say"));
    EXPECT_FALSE(Contains(completions, "s"));
}

TEST(CommandRegistryCompletionTest, CaseInsensitivePartialAndOriginalCaseNames) {
    eqt::ui::CommandRegistry registry;
    registry.registerCommand(MakeCommand("Shout", {}));

    auto completions = registry.getCompletions("SH");

    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0], "Shout") << "the original-case name is offered";
}

TEST(CommandRegistryCompletionTest, EmptyPartialOffersEverythingAndNoMatchOffersNothing) {
    eqt::ui::CommandRegistry registry;
    registry.registerCommand(MakeCommand("say", {"s"}));
    registry.registerCommand(MakeCommand("shout", {}));

    EXPECT_EQ(registry.getCompletions("").size(), 3u);  // say, s, shout
    EXPECT_TRUE(registry.getCompletions("zzzznomatch").empty());
}

// ---------------------------------------------------------------------------
// The helper itself, independent of either registry.
// ---------------------------------------------------------------------------

TEST(CommandCompletionHelperTest, SortsAndDeDuplicatesAcrossBothMaps) {
    std::map<std::string, std::string> commands{{"consider", "Consider"},
                                                {"camp", "Camp"}};
    std::map<std::string, std::string> aliases{{"con", "consider"},
                                               {"c", "camp"}};

    auto out = eqt::completion::PrefixCompletions(
        "c", commands, [](const std::string& display) { return display; }, aliases);

    EXPECT_EQ(out, (std::vector<std::string>{"Camp", "Consider", "c", "con"}));
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end()));
}

TEST(CommandCompletionHelperTest, ADisplayNameEqualToAnAliasAppearsOnce) {
    std::map<std::string, std::string> commands{{"con", "con"}};
    std::map<std::string, std::string> aliases{{"con", "con"}};

    auto out = eqt::completion::PrefixCompletions(
        "", commands, [](const std::string& display) { return display; }, aliases);

    EXPECT_EQ(out, (std::vector<std::string>{"con"}));
}

TEST(CommandCompletionHelperTest, ToLowerIsTheOneLowercaser) {
    EXPECT_EQ(eqt::completion::ToLower("ShOuT"), "shout");
    EXPECT_EQ(eqt::completion::ToLower(""), "");
    EXPECT_EQ(eqt::completion::ToLower("/say Hi"), "/say hi");
}

// ---------------------------------------------------------------------------
// T365 (B091) -- registry 3's completion source.
//
// The static renderer catalog had no getCompletions() at all, so
// Application::commandCompletions() unioned registries 1 and 2 only while
// /help listed all three: none of the ~70 renderer commands was tab-
// completable. RendererCommandCompletions() is the missing half, and it is
// the SAME body -- eqt::completion::PrefixCompletions -- the other two use.
// ---------------------------------------------------------------------------

TEST(RendererCommandCompletionTest, ARendererCatalogNameCompletes) {
    auto out = eqt::help::RendererCommandCompletions("ski");
    EXPECT_TRUE(Contains(out, "skills"))
        << "registry 3 offers no completions -- /ski<Tab> cannot reach /skills";
}

TEST(RendererCommandCompletionTest, TheRenamedInventoryCommandCompletes) {
    auto out = eqt::help::RendererCommandCompletions("inventor");
    EXPECT_EQ(out, (std::vector<std::string>{"inventory"}));
}

TEST(RendererCommandCompletionTest, AliasesCompleteToo) {
    // `sd` is the alias of `shaderdebug`; both must be offered for "s".
    auto out = eqt::help::RendererCommandCompletions("sd");
    EXPECT_TRUE(Contains(out, "sd"));
}

TEST(RendererCommandCompletionTest, SortedDeDuplicatedAndCaseInsensitive) {
    auto lower = eqt::help::RendererCommandCompletions("scr");
    auto upper = eqt::help::RendererCommandCompletions("SCR");
    EXPECT_EQ(lower, upper);
    EXPECT_TRUE(std::is_sorted(lower.begin(), lower.end()));
    EXPECT_EQ(std::adjacent_find(lower.begin(), lower.end()), lower.end());
    EXPECT_TRUE(Contains(lower, "screenshot"));
}

TEST(RendererCommandCompletionTest, AnEmptyPartialOffersTheWholeCatalog) {
    auto out = eqt::help::RendererCommandCompletions("");
    EXPECT_GE(out.size(), eqt::help::AllRendererCommands().size());
}

TEST(RendererCommandCompletionTest, AnUnknownPrefixOffersNothing) {
    EXPECT_TRUE(eqt::help::RendererCommandCompletions("zzzznotacommand").empty());
}
