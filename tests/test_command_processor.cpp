// Tests for eqt::action::CommandProcessor (T12).
//
// Covers: processCommand vs processInput, unknown-command handling,
// registration round-trips, category/completion consistency, argument
// parsing (whitespace, trailing-argument commands like /tell), and case
// sensitivity, per refactor/T12_action_layer.md section 3.

#include "action_test_fakes.h"
#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/output/output_renderer.h"
#include "client/state/game_state.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace eqt::action;
using namespace eqt::action::test;
using eqt::state::GameState;

namespace {

// Minimal recording IOutputRenderer fake. Only displaySystemMessage and the
// timestamp toggle are exercised by CommandProcessor; everything else is a
// harmless no-op stub required to satisfy the pure-virtual surface.
class RecordingOutputRenderer : public eqt::output::IOutputRenderer {
public:
    std::vector<std::string> systemMessages;
    bool showTimestamps = true;

    bool initialize(const eqt::output::RendererConfig&) override { return true; }
    void shutdown() override {}
    bool isRunning() const override { return true; }
    bool update(float) override { return true; }
    void connectToGameState(eqt::state::GameState&) override {}
    void disconnectFromGameState() override {}
    void loadZone(const std::string&) override {}
    void unloadZone() override {}
    const std::string& getCurrentZoneName() const override {
        static const std::string empty;
        return empty;
    }
    void setLoadingProgress(float, const std::wstring&) override {}
    void setZoneReady(bool) override {}
    bool isZoneReady() const override { return true; }
    void setExpectedEntityCount(size_t) override {}
    void notifyEntityLoaded() override {}
    void setNetworkReady(bool) override {}
    void setPlayerSpawnId(uint16_t) override {}
    void setPlayerPosition(float, float, float, float) override {}
    void setCharacterInfo(const std::wstring&, int, const std::wstring&) override {}
    void updateCharacterStats(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void setCurrentTarget(uint16_t, const std::string&, uint8_t, uint8_t) override {}
    void updateCurrentTargetHP(uint8_t) override {}
    void clearCurrentTarget() override {}
    void displayChatMessage(const std::string&, const std::string&, const std::string&) override {}
    void displaySystemMessage(const std::string& message) override {
        systemMessages.push_back(message);
    }
    void displayCombatMessage(const std::string&) override {}
    void requestQuit() override {}

    void setShowTimestamps(bool show) override { showTimestamps = show; }
    bool getShowTimestamps() const override { return showTimestamps; }
};

class CommandProcessorTest : public ::testing::Test {
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

// ===================================================================
// processCommand vs processInput
// ===================================================================

TEST_F(CommandProcessorTest, ProcessCommand_AcceptsWithOrWithoutLeadingSlash) {
    auto r1 = processor->processCommand("/sit");
    auto r2 = processor->processCommand("sit");
    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);
    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "sit()");
    EXPECT_EQ(handler.calls[1], "sit()");
}

TEST_F(CommandProcessorTest, ProcessInput_SlashPrefixDispatchesAsCommand) {
    auto result = processor->processInput("/stand");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("stand()"));
}

TEST_F(CommandProcessorTest, ProcessInput_NoSlashSendsAsDefaultChannelMessage) {
    processor->setDefaultChannel(ChatChannel::Say);
    auto result = processor->processInput("hello world");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Say, \"hello world\")"));
}

TEST_F(CommandProcessorTest, ProcessInput_RespectsConfiguredDefaultChannel) {
    processor->setDefaultChannel(ChatChannel::OOC);
    EXPECT_EQ(processor->getDefaultChannel(), ChatChannel::OOC);
    processor->processInput("just chatting");
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(OOC, \"just chatting\")"));
}

TEST_F(CommandProcessorTest, ProcessCommand_EmptyInput_SucceedsAsNoOp) {
    auto result = processor->processCommand("");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, ProcessInput_EmptyInput_SucceedsAsNoOp) {
    auto result = processor->processInput("");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(handler.empty());
}

// ===================================================================
// Unknown command
// ===================================================================

TEST_F(CommandProcessorTest, UnknownCommand_FailsWithSensibleMessage) {
    auto result = processor->processCommand("/notarealcommand");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("notarealcommand"), std::string::npos);
    EXPECT_TRUE(handler.empty());
}

// ===================================================================
// Registration round-trips
// ===================================================================

TEST_F(CommandProcessorTest, RegisterUnregister_RoundTrip) {
    EXPECT_FALSE(processor->hasCommand("custom"));

    CommandInfo info;
    info.name = "custom";
    info.aliases = {"cu"};
    info.usage = "/custom";
    info.description = "A custom test command";
    info.category = "TestCategory";

    bool invoked = false;
    processor->registerCommand(info, [&invoked](const std::string&) {
        invoked = true;
        return ActionResult::Success("custom ran");
    });

    EXPECT_TRUE(processor->hasCommand("custom"));
    EXPECT_TRUE(processor->hasCommand("cu"));   // alias resolves too
    EXPECT_TRUE(processor->hasCommand("CUSTOM"));  // case-insensitive lookup

    auto result = processor->processCommand("/custom");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(invoked);

    auto info2 = processor->getCommandInfo("cu");
    ASSERT_TRUE(info2.has_value());
    EXPECT_EQ(info2->name, "custom");
    EXPECT_EQ(info2->category, "TestCategory");

    processor->unregisterCommand("custom");
    EXPECT_FALSE(processor->hasCommand("custom"));
    EXPECT_FALSE(processor->hasCommand("cu"));  // alias removed with primary

    auto afterRemoval = processor->processCommand("/custom");
    EXPECT_FALSE(afterRemoval.success);
}

TEST_F(CommandProcessorTest, RegisterCommand_ReRegisteringExistingNameReplacesHandler) {
    CommandInfo info;
    info.name = "dup";
    info.usage = "/dup";
    info.description = "first";
    info.category = "Test";

    processor->registerCommand(info, [](const std::string&) {
        return ActionResult::Success("first-handler");
    });

    info.description = "second";
    processor->registerCommand(info, [](const std::string&) {
        return ActionResult::Success("second-handler");
    });

    auto result = processor->processCommand("/dup");
    EXPECT_EQ(result.message, "second-handler");

    auto queried = processor->getCommandInfo("dup");
    ASSERT_TRUE(queried.has_value());
    EXPECT_EQ(queried->description, "second");
}

TEST_F(CommandProcessorTest, UnregisterCommand_AbsentNameIsNoOp) {
    EXPECT_FALSE(processor->hasCommand("neverexisted"));
    processor->unregisterCommand("neverexisted");  // must not throw/crash
    EXPECT_FALSE(processor->hasCommand("neverexisted"));
}

TEST_F(CommandProcessorTest, GetCommandInfo_UnknownReturnsNullopt) {
    auto info = processor->getCommandInfo("doesnotexist");
    EXPECT_FALSE(info.has_value());
}

TEST_F(CommandProcessorTest, RequiresArgs_MissingArgumentsFails) {
    CommandInfo info;
    info.name = "needsargs";
    info.usage = "/needsargs <thing>";
    info.description = "requires an argument";
    info.category = "Test";
    info.requiresArgs = true;

    processor->registerCommand(info, [](const std::string&) {
        return ActionResult::Success();
    });

    auto result = processor->processCommand("/needsargs");
    EXPECT_FALSE(result.success);

    auto result2 = processor->processCommand("/needsargs something");
    EXPECT_TRUE(result2.success);
}

// ===================================================================
// getAllCommands / getCommandsByCategory / getCategories consistency
// ===================================================================

TEST_F(CommandProcessorTest, Categories_EveryCommandCategoryAppearsInGetCategories) {
    auto all = processor->getAllCommands();
    auto categories = processor->getCategories();

    ASSERT_FALSE(all.empty());
    ASSERT_FALSE(categories.empty());

    for (const auto& cmd : all) {
        EXPECT_NE(std::find(categories.begin(), categories.end(), cmd.category),
                  categories.end())
            << "command " << cmd.name << " has category " << cmd.category
            << " missing from getCategories()";
    }
}

TEST_F(CommandProcessorTest, GetCommandsByCategory_OnlyReturnsMatchingCategory) {
    auto combatCommands = processor->getCommandsByCategory("Combat");
    ASSERT_FALSE(combatCommands.empty());
    for (const auto& cmd : combatCommands) {
        EXPECT_EQ(cmd.category, "Combat");
    }

    // Representative sample rather than enumerating all built-ins.
    // T51: "attack"/"stopattack"/"aa" were removed from this registry (they
    // were shadowed dead code -- registry 1 always won the dispatch
    // collision); "target" is still registered here and stands in as the
    // representative sample.
    bool foundTarget = false;
    for (const auto& cmd : combatCommands) {
        if (cmd.name == "target") foundTarget = true;
    }
    EXPECT_TRUE(foundTarget);
}

TEST_F(CommandProcessorTest, GetCommandsByCategory_UnknownCategoryReturnsEmpty) {
    auto result = processor->getCommandsByCategory("NoSuchCategory");
    EXPECT_TRUE(result.empty());
}

TEST_F(CommandProcessorTest, BuiltinCommands_RepresentativeSampleRegisteredWithExpectedCategory) {
    struct Expected { const char* name; const char* category; };
    static const Expected samples[] = {
        {"say", "Chat"},
        {"sit", "Movement"},
        {"target", "Combat"},
        {"invite", "Group"},
        {"who", "Utility"},
        {"mem", "Spells"},
        {"pet", "Pet"},
    };
    for (const auto& s : samples) {
        auto info = processor->getCommandInfo(s.name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << s.name;
        EXPECT_EQ(info->category, s.category) << "wrong category for: " << s.name;
    }
}

// ===================================================================
// getCompletions / getAllCommandNames
// ===================================================================

TEST_F(CommandProcessorTest, GetCompletions_PrefixMatch) {
    auto completions = processor->getCompletions("sh");
    // "shout" (name) should match; "sh" is also registered as an alias of shout.
    EXPECT_NE(std::find(completions.begin(), completions.end(), "shout"),
              completions.end());
}

TEST_F(CommandProcessorTest, GetCompletions_EmptyPrefixMatchesEverything) {
    auto completions = processor->getCompletions("");
    auto allNames = processor->getAllCommandNames();
    // Empty prefix: every name.find("") == 0, so all names+aliases appear
    // (aliases are added again from m_aliases, so completions may include
    // both primary names and alias strings).
    EXPECT_FALSE(completions.empty());
    for (const auto& name : allNames) {
        // Every canonical name must be reachable via empty-prefix completion
        // (either directly, or its lowercase form).
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool found = std::find(completions.begin(), completions.end(), name) != completions.end() ||
                     std::find(completions.begin(), completions.end(), lower) != completions.end();
        EXPECT_TRUE(found) << "name not reachable via empty completion: " << name;
    }
}

TEST_F(CommandProcessorTest, GetCompletions_NoMatchReturnsEmpty) {
    auto completions = processor->getCompletions("zzzznomatch");
    EXPECT_TRUE(completions.empty());
}

TEST_F(CommandProcessorTest, GetCompletions_ExactMatchIncluded) {
    auto completions = processor->getCompletions("sit");
    EXPECT_NE(std::find(completions.begin(), completions.end(), "sit"),
              completions.end());
}

TEST_F(CommandProcessorTest, GetAllCommandNames_IncludesAliasesAndIsSorted) {
    auto names = processor->getAllCommandNames();
    ASSERT_FALSE(names.empty());
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
    // "s" is registered as an alias of "say".
    EXPECT_NE(std::find(names.begin(), names.end(), "s"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "say"), names.end());
}

// ===================================================================
// Argument handling
// ===================================================================

TEST_F(CommandProcessorTest, ArgumentParsing_NoArgs) {
    auto result = processor->processCommand("/sit");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sit()"));
}

TEST_F(CommandProcessorTest, ArgumentParsing_LeadingWhitespaceTrimmed) {
    auto result = processor->processCommand("/say   hello");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Say, \"hello\")"));
}

// parseCommandLine only trims the leading run of whitespace between the
// command name and its arguments; it does NOT trim trailing whitespace on
// the argument string. Pin the actual (asymmetric) behavior.
TEST_F(CommandProcessorTest, ArgumentParsing_TrailingWhitespaceNotTrimmed) {
    auto result = processor->processCommand("/say hello  ");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Say, \"hello  \")"));
}

TEST_F(CommandProcessorTest, ArgumentParsing_MultipleInternalSpacesPreservedInMessage) {
    auto result = processor->processCommand("/say hello   world");
    EXPECT_TRUE(result.success);
    // sendChatMessage receives the whole args string verbatim (only the
    // leading run of whitespace before the first token is stripped).
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Say, \"hello   world\")"));
}

TEST_F(CommandProcessorTest, TrailingArgumentCommand_TellSplitsTargetFromMessage) {
    auto result = processor->processCommand("/tell Bob hello there friend");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendTell(\"Bob\", \"hello there friend\")"));
}

TEST_F(CommandProcessorTest, TrailingArgumentCommand_TellMissingMessageFails) {
    auto result = processor->processCommand("/tell Bob");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.empty());
}

// splitArgs uses istream >> token, which does NOT honor quotes: a quoted
// phrase is still split on internal whitespace. Pin the current (unquoted)
// behavior rather than assuming shell-like quoting is supported.
TEST_F(CommandProcessorTest, TrailingArgumentCommand_QuotesAreNotSpecial) {
    auto result = processor->processCommand("/tell Bob \"hello there\"");
    EXPECT_TRUE(result.success);
    // splitArgs tokenizes on whitespace only: parts = {"Bob", "\"hello",
    // "there\""}; message reassembles parts[1..] with single spaces, so the
    // literal quote characters from the input end up embedded in the
    // message string (toStr then wraps that whole string in its own
    // quotes for the call-log representation).
    ASSERT_TRUE(handler.calledOnceWith("sendTell(\"Bob\", \"\"hello there\"\")"));
}

// ===================================================================
// Case sensitivity
// ===================================================================

TEST_F(CommandProcessorTest, CommandLookup_IsCaseInsensitive) {
    auto result = processor->processCommand("/SIT");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sit()"));
}

TEST_F(CommandProcessorTest, CommandLookup_MixedCaseAlias) {
    auto result = processor->processCommand("/Tar Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("targetEntityByName(\"Bob\")"));
}

TEST_F(CommandProcessorTest, HasCommand_IsCaseInsensitive) {
    EXPECT_TRUE(processor->hasCommand("SIT"));
    EXPECT_TRUE(processor->hasCommand("Sit"));
    EXPECT_TRUE(processor->hasCommand("sit"));
}

// Command *arguments* are case-preserved even though the command name
// lookup is not — e.g. entity names and messages must not be lowercased.
TEST_F(CommandProcessorTest, ArgumentCase_PreservedThroughDispatch) {
    auto result = processor->processCommand("/target BobTheBuilder");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("targetEntityByName(\"BobTheBuilder\")"));
}

// ===================================================================
// /loot (T34) -- must reach ActionDispatcher::lootCorpse() with the
// exact corpse id, the same entry point the renderer's shift+click
// overlay callback drives via InputActionBridge::processLootRequests.
// See refactor/T34_shiftclick_loot_verification.md.
// ===================================================================

TEST_F(CommandProcessorTest, Loot_ValidId_DispatchesLootCorpseWithExactId) {
    auto result = processor->processCommand("/loot 176");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("lootCorpse(176)"));
}

TEST_F(CommandProcessorTest, Loot_MissingId_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/loot");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, Loot_MalformedId_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/loot notanumber");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, Loot_TrailingGarbageAfterNumber_FailsCleanly) {
    // "176abc" must not silently truncate-parse to 176 -- either the whole
    // token is a valid id or the command fails, matching the id-only
    // contract in the plan (no partial/fuzzy corpse matching).
    auto result = processor->processCommand("/loot 176abc");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, Loot_NegativeId_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/loot -1");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, Loot_OutOfRangeId_FailsCleanlyWithoutDispatching) {
    // corpseId is uint16_t; 70000 overflows it.
    auto result = processor->processCommand("/loot 70000");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, Loot_ZeroIsAValidCorpseId) {
    // 0 is a legitimate spawn id in this codebase (see T33's corpse-id
    // matching); the parser must not treat it as "missing".
    auto result = processor->processCommand("/loot 0");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("lootCorpse(0)"));
}

// ===================================================================
// Output renderer integration (echo + timestamp toggle)
// ===================================================================

TEST_F(CommandProcessorTest, EchoEnabled_DisplaysTypedCommand) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);
    EXPECT_FALSE(processor->isEchoEnabled());
    processor->setEchoEnabled(true);
    EXPECT_TRUE(processor->isEchoEnabled());

    processor->processCommand("/sit");
    ASSERT_FALSE(renderer.systemMessages.empty());
    EXPECT_EQ(renderer.systemMessages[0], "/sit");
}

TEST_F(CommandProcessorTest, UnknownCommand_DisplaysErrorViaRenderer) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);

    processor->processCommand("/bogus");
    ASSERT_FALSE(renderer.systemMessages.empty());
    EXPECT_NE(renderer.systemMessages.back().find("bogus"), std::string::npos);
}

TEST_F(CommandProcessorTest, Timestamp_TogglesRendererSetting) {
    RecordingOutputRenderer renderer;
    renderer.showTimestamps = true;
    processor->setOutputRenderer(&renderer);

    processor->processCommand("/timestamp");
    EXPECT_FALSE(renderer.showTimestamps);
    processor->processCommand("/timestamp");
    EXPECT_TRUE(renderer.showTimestamps);
}

// ===================================================================
// cmdHail (T39) -- reads m_state.combat().hasTarget(). Until this unit,
// CombatState's target half was never populated in production (T28 left
// this reader deliberately untouched, see refactor/deferred.md "Dead
// parallel state copies in the state layer, WITH LIVE READERS"), so
// hasTarget() was permanently false and /hail always took the no-target
// branch. Application::syncCombatState() (application.cpp) now mirrors
// this same GameState instance's combat() from the authoritative
// CombatManager every frame; these tests exercise CommandProcessor's
// branch directly against `state` (bypassing Application) since
// CommandProcessor only ever sees state::GameState&, per T12's
// decoupling -- these would have FAILED before this unit (hailTarget()
// would never have been reachable via /hail).
// ===================================================================

TEST_F(CommandProcessorTest, Hail_NoTarget_CallsHailNotHailTarget) {
    ASSERT_FALSE(state.combat().hasTarget());

    auto result = processor->processCommand("/hail");

    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("hail()"));
}

TEST_F(CommandProcessorTest, Hail_WithTarget_CallsHailTargetNotHail) {
    state.combat().setTarget(1234, "a decrepit skeleton", 100, 10);
    ASSERT_TRUE(state.combat().hasTarget());

    auto result = processor->processCommand("/hail");

    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("hailTarget()"));
}

TEST_F(CommandProcessorTest, Hail_TargetClearedAfterward_RevertsToHail) {
    state.combat().setTarget(1234, "a decrepit skeleton", 100, 10);
    processor->processCommand("/hail");
    ASSERT_TRUE(handler.calledOnceWith("hailTarget()"));
    handler.clear();

    state.combat().clearTarget();
    ASSERT_FALSE(state.combat().hasTarget());

    processor->processCommand("/hail");
    ASSERT_TRUE(handler.calledOnceWith("hail()"));
}

// ===================================================================
// cmdNpcState (T49) -- reads m_state.player()'s vendor/banker/trainer
// fields, the state GameStateSyncBridge now feeds from bridge events
// instead of the deleted Application::syncNPCInteractionState() poll.
// No dispatcher call, no args -- pure state-layer read, same shape as
// cmdLoc's tests above. Model: T34/T48's argument-handling style plus
// the Hail tests' direct-state-mutation-then-read pattern.
// ===================================================================

TEST_F(CommandProcessorTest, NpcState_NoInteraction_ReportsNoneForAll) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);

    auto result = processor->processCommand("/npcstate");

    EXPECT_TRUE(result.success);
    ASSERT_EQ(renderer.systemMessages.size(), 3u);
    EXPECT_EQ(renderer.systemMessages[0], "Vendor: none");
    EXPECT_EQ(renderer.systemMessages[1], "Banker: none");
    EXPECT_EQ(renderer.systemMessages[2], "Trainer: none");
}

TEST_F(CommandProcessorTest, NpcState_VendorOpen_ReportsVendorWithRealSellRate) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);
    // The observable fidelity gain this unit delivers: the deleted poll
    // hardcoded sellRate=1.0f; the migrated path carries whatever
    // VendorWindowOpened actually published. Use a non-1.0 rate so a
    // regression back to the hardcoded value would be caught here.
    state.player().setVendor(500, 1.25f, "A Merchant");

    auto result = processor->processCommand("/npcstate");

    EXPECT_TRUE(result.success);
    ASSERT_FALSE(renderer.systemMessages.empty());
    EXPECT_NE(renderer.systemMessages[0].find("A Merchant"), std::string::npos);
    EXPECT_NE(renderer.systemMessages[0].find("500"), std::string::npos);
    EXPECT_NE(renderer.systemMessages[0].find("1.2500"), std::string::npos);
}

TEST_F(CommandProcessorTest, NpcState_BankerOpen_ReportsBanker) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);
    state.player().setBanker(600);

    auto result = processor->processCommand("/npcstate");

    EXPECT_TRUE(result.success);
    ASSERT_EQ(renderer.systemMessages.size(), 3u);
    EXPECT_EQ(renderer.systemMessages[0], "Vendor: none");
    EXPECT_NE(renderer.systemMessages[1].find("600"), std::string::npos);
    EXPECT_EQ(renderer.systemMessages[2], "Trainer: none");
}

TEST_F(CommandProcessorTest, NpcState_TrainerOpen_ReportsTrainer) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);
    state.player().setTrainer(700, "A Trainer");

    auto result = processor->processCommand("/npcstate");

    EXPECT_TRUE(result.success);
    ASSERT_EQ(renderer.systemMessages.size(), 3u);
    EXPECT_NE(renderer.systemMessages[2].find("A Trainer"), std::string::npos);
    EXPECT_NE(renderer.systemMessages[2].find("700"), std::string::npos);
}

TEST_F(CommandProcessorTest, NpcState_ClearedAfterward_RevertsToNone) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);
    state.player().setVendor(500, 1.25f, "A Merchant");
    processor->processCommand("/npcstate");
    ASSERT_NE(renderer.systemMessages[0].find("A Merchant"), std::string::npos);

    state.player().clearVendor();
    renderer.systemMessages.clear();

    processor->processCommand("/npcstate");
    EXPECT_EQ(renderer.systemMessages[0], "Vendor: none");
}

TEST_F(CommandProcessorTest, NpcState_IgnoresArguments) {
    // /npcstate takes no arguments; extra text should not error or change
    // behaviour (same convention as /loc, which also ignores args).
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);

    auto result = processor->processCommand("/npcstate some garbage args");

    EXPECT_TRUE(result.success);
    ASSERT_EQ(renderer.systemMessages.size(), 3u);
}

} // namespace
