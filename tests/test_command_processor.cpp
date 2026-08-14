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
#include <string>
#include <vector>

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

// T98: /sit and /stand were deleted (shadowed dead code -- registry 1 always
// won, see refactor/T97_command_registry_shadowing_audit.md /
// refactor/T98_delete_shadowed_commands.md). Re-pointed to /consider and
// /jump, both surviving no-arg commands, to keep this test covering the
// shared leading-slash-optional dispatch path rather than losing coverage.
TEST_F(CommandProcessorTest, ProcessCommand_AcceptsWithOrWithoutLeadingSlash) {
    auto r1 = processor->processCommand("/consider");
    auto r2 = processor->processCommand("consider");
    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);
    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "consider()");
    EXPECT_EQ(handler.calls[1], "consider()");
}

TEST_F(CommandProcessorTest, ProcessInput_SlashPrefixDispatchesAsCommand) {
    auto result = processor->processInput("/jump");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("jump()"));
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
    // collision). T98: "target"/"cast"/"interrupt" were removed for the same
    // reason; "consider" is still registered here and stands in as the
    // representative sample.
    bool foundConsider = false;
    for (const auto& cmd : combatCommands) {
        if (cmd.name == "consider") foundConsider = true;
    }
    EXPECT_TRUE(foundConsider);
}

TEST_F(CommandProcessorTest, GetCommandsByCategory_UnknownCategoryReturnsEmpty) {
    auto result = processor->getCommandsByCategory("NoSuchCategory");
    EXPECT_TRUE(result.empty());
}

// ===================================================================
// T51 registered attack/stopattack/aa as shadowed dead code and removed
// them from this registry (EverQuest::m_command_registry, registry 1, won
// T29's dispatch order for every one of these names -- see
// refactor/T51_attack_path_consolidation.md). T102 MIGRATES them back,
// this time as the sole (live) implementation -- the game-registry
// registrations that used to shadow them were deleted from
// eq_commands_movement.cpp in the same change. This is not a reversal of
// T51's finding, the same shape as T99/T100/T101's re-additions of their
// own previously-T98-deleted families. See
// refactor/T102_migrate_combat_social.md.
// ===================================================================

TEST_F(CommandProcessorTest, T102_AttackStopattackAa_NowRegistered) {
    EXPECT_TRUE(processor->getCommandInfo("attack").has_value());
    EXPECT_TRUE(processor->getCommandInfo("stopattack").has_value());
    EXPECT_TRUE(processor->getCommandInfo("aa").has_value());
    EXPECT_TRUE(processor->hasCommand("attack"));
    EXPECT_TRUE(processor->hasCommand("stopattack"));
    EXPECT_TRUE(processor->hasCommand("aa"));
}

// T98: the original samples (say/sit/target/invite/who/mem/pet) were ALL
// deleted -- every one of the seven was shadowed dead code (see
// refactor/T97_command_registry_shadowing_audit.md). Deleting invite/
// disband/decline empties "Group" entirely and deleting skills/gems/mem/
// forget empties "Spells" entirely and deleting pet empties "Pet" entirely
// in this registry, so those three categories have no representative left;
// re-pointed to one surviving command per category that still has members.
TEST_F(CommandProcessorTest, BuiltinCommands_RepresentativeSampleRegisteredWithExpectedCategory) {
    struct Expected { const char* name; const char* category; };
    static const Expected samples[] = {
        {"shout", "Chat"},
        {"face", "Movement"},
        {"consider", "Combat"},
        {"door", "Utility"},
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

// T98: "sit" was deleted (shadowed dead code); re-pointed to "face", a
// surviving command with no aliases.
TEST_F(CommandProcessorTest, GetCompletions_ExactMatchIncluded) {
    auto completions = processor->getCompletions("face");
    EXPECT_NE(std::find(completions.begin(), completions.end(), "face"),
              completions.end());
}

// T98: "say"/"s" were deleted (shadowed dead code); re-pointed to "shout"/
// "sh" -- shout is the one command in this registry whose primary name is
// shadowed too but whose alias escapes (game's own shout alias is "sho",
// not "sh"), so this also incidentally exercises the surviving alias.
TEST_F(CommandProcessorTest, GetAllCommandNames_IncludesAliasesAndIsSorted) {
    auto names = processor->getAllCommandNames();
    ASSERT_FALSE(names.empty());
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
    EXPECT_NE(std::find(names.begin(), names.end(), "sh"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "shout"), names.end());
}

// ===================================================================
// Argument handling
// ===================================================================

// T98: "sit" was deleted (shadowed dead code); re-pointed to "consider".
TEST_F(CommandProcessorTest, ArgumentParsing_NoArgs) {
    auto result = processor->processCommand("/consider");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("consider()"));
}

// T98: "say" was deleted (shadowed dead code); re-pointed to "shout", the
// only surviving command that still reaches sendChatMessage() directly by
// name (say/ooc/auction/gsay/gu/emote were all deleted too).
TEST_F(CommandProcessorTest, ArgumentParsing_LeadingWhitespaceTrimmed) {
    auto result = processor->processCommand("/shout   hello");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Shout, \"hello\")"));
}

// parseCommandLine only trims the leading run of whitespace between the
// command name and its arguments; it does NOT trim trailing whitespace on
// the argument string. Pin the actual (asymmetric) behavior.
TEST_F(CommandProcessorTest, ArgumentParsing_TrailingWhitespaceNotTrimmed) {
    auto result = processor->processCommand("/shout hello  ");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Shout, \"hello  \")"));
}

TEST_F(CommandProcessorTest, ArgumentParsing_MultipleInternalSpacesPreservedInMessage) {
    auto result = processor->processCommand("/shout hello   world");
    EXPECT_TRUE(result.success);
    // sendChatMessage receives the whole args string verbatim (only the
    // leading run of whitespace before the first token is stripped).
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Shout, \"hello   world\")"));
}

// T98: /tell was deleted (shadowed dead code, see
// refactor/T97_command_registry_shadowing_audit.md). cmdTell's own
// target+message-joining logic doesn't exist on any surviving command (no
// survivor re-joins split tokens with spaces the way cmdTell did), so the
// two tests that pinned that exact joining/quoting behavior
// (TellMissingMessageFails, QuotesAreNotSpecial) go with it -- they covered
// nothing but cmdTell's own validation/assembly, which no longer exists.
//
// The underlying splitArgs() PRIMITIVE, however, is still shared: cmdFace
// (a surviving command) calls it too. Re-pointed here at /face to keep
// splitArgs itself under CommandProcessor-level test coverage rather than
// losing it silently -- this is deliberately not a byte-for-byte rename of
// the old Tell test since no survivor's args-joining shape matches cmdTell's.
TEST_F(CommandProcessorTest, TrailingArgumentCommand_FaceUsesSplitArgsForThreeCoordinates) {
    auto result = processor->processCommand("/face 1 2 3");
    EXPECT_TRUE(result.success);
    // cmdFace only takes the >=3-numeric-token branch (faceLocation) if
    // splitArgs actually tokenized "1 2 3" into 3 parts; the exact heading
    // value depends on the player's position so only the dispatched method
    // is pinned, not its argument, to avoid a brittle floating-point match.
    ASSERT_EQ(handler.count(), 1u);
    EXPECT_EQ(handler.calls[0].rfind("setHeading(", 0), 0u)
        << "expected faceLocation's setHeading(...) call, got: " << handler.calls[0];
}

// ===================================================================
// Case sensitivity
// ===================================================================

// T98: "sit" was deleted (shadowed dead code); re-pointed to "consider".
TEST_F(CommandProcessorTest, CommandLookup_IsCaseInsensitive) {
    auto result = processor->processCommand("/CONSIDER");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("consider()"));
}

// T98: "target"/"tar" were deleted (shadowed dead code); re-pointed to
// shout's "sh" alias, which is the one alias in this registry that actually
// escapes shadowing live (see T97's Review) -- a bonus check that it still
// resolves case-insensitively.
TEST_F(CommandProcessorTest, CommandLookup_MixedCaseAlias) {
    auto result = processor->processCommand("/Sh hello");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Shout, \"hello\")"));
}

// T98: "sit" was deleted (shadowed dead code); re-pointed to "face".
TEST_F(CommandProcessorTest, HasCommand_IsCaseInsensitive) {
    EXPECT_TRUE(processor->hasCommand("FACE"));
    EXPECT_TRUE(processor->hasCommand("Face"));
    EXPECT_TRUE(processor->hasCommand("face"));
}

// Command *arguments* are case-preserved even though the command name
// lookup is not — e.g. entity names and messages must not be lowercased.
// T98: "target" was deleted (shadowed dead code); re-pointed to "dump",
// which also dispatches its single string argument case-preserved.
TEST_F(CommandProcessorTest, ArgumentCase_PreservedThroughDispatch) {
    auto result = processor->processCommand("/dump BobTheBuilder");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("dumpEntityAppearance(\"BobTheBuilder\")"));
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

// T98: "sit" was deleted (shadowed dead code); re-pointed to "consider".
TEST_F(CommandProcessorTest, EchoEnabled_DisplaysTypedCommand) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);
    EXPECT_FALSE(processor->isEchoEnabled());
    processor->setEchoEnabled(true);
    EXPECT_TRUE(processor->isEchoEnabled());

    processor->processCommand("/consider");
    ASSERT_FALSE(renderer.systemMessages.empty());
    EXPECT_EQ(renderer.systemMessages[0], "/consider");
}

TEST_F(CommandProcessorTest, UnknownCommand_DisplaysErrorViaRenderer) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);

    processor->processCommand("/bogus");
    ASSERT_FALSE(renderer.systemMessages.empty());
    EXPECT_NE(renderer.systemMessages.back().find("bogus"), std::string::npos);
}

// T98: Timestamp_TogglesRendererSetting removed along with cmdTimestamp
// ("timestamp" was shadowed dead code, see
// refactor/T97_command_registry_shadowing_audit.md). This test covered only
// cmdTimestamp's own IOutputRenderer::setShowTimestamps() toggle -- no
// surviving command touches renderer timestamp state, so there is no shared
// logic to re-point at.

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

// ===================================================================
// T98 verification: enumerate the 31 commands that survive the shadowed-
// command deletion (30 previously "Reachable" + "shout", which is
// "Reachable-via-alias" -- see refactor/T97_command_registry_shadowing_audit.md
// and refactor/T98_delete_shadowed_commands.md) and confirm each is still
// registered and dispatches, by primary name AND by every one of its
// aliases. This is the check the plan calls out as mattering most: a
// deletion that accidentally clips a survivor is the failure mode T98 must
// not produce. hasCommand()/getCommandInfo() prove registration; dispatching
// via processCommand() and asserting the failure (if any) is never the
// "Unknown command: ..." string proves the lookup itself resolves -- some
// of these legitimately fail for other reasons with no live GameState setup
// (e.g. requiresArgs commands given no args, or /loot with a synthetic id),
// and that is fine; only an "Unknown command" result would mean the survivor
// was not actually reachable.
// ===================================================================

TEST_F(CommandProcessorTest, T98_AllSurvivorsRegisteredAndDispatchByNameAndAlias) {
    struct Survivor { const char* name; std::vector<const char*> aliases; };
    static const std::vector<Survivor> survivors = {
        {"shout", {"sh"}},
        {"face", {}},
        {"consider", {"con"}},
        {"hail", {}},
        {"npcstate", {}},
        {"door", {"u"}},
        {"afk", {}},
        {"anon", {}},
        {"walk", {}},
        {"run", {}},
        {"sneak", {}},
        {"crouch", {"duck"}},
        {"feign", {"fd"}},
        {"jump", {}},
        {"turn", {}},
        {"list", {}},
        {"dump", {}},
        {"hunt", {}},
        {"autoloot", {}},
        {"listtargets", {}},
        {"loot", {}},
        {"pathfinding", {}},
        {"roleplay", {"rp"}},
        {"wave", {}},
        {"dance", {}},
        {"cheer", {}},
        {"laugh", {}},
        {"logonly", {}},
        {"logexclude", {}},
        {"logmodule", {}},
        {"logclear", {}},
    };
    ASSERT_EQ(survivors.size(), 31u) << "the survivor list itself drifted from 31";

    for (const auto& s : survivors) {
        EXPECT_TRUE(processor->hasCommand(s.name)) << "primary name not registered: " << s.name;
        auto info = processor->getCommandInfo(s.name);
        ASSERT_TRUE(info.has_value()) << "getCommandInfo missing: " << s.name;
        EXPECT_EQ(info->name, s.name);

        // shout needs an argument (requiresArgs=true); everything else can
        // be dispatched bare. Either way, "Unknown command" is the only
        // outcome that would mean the name failed to resolve.
        std::string cmdLine = "/" + std::string(s.name) + (std::string(s.name) == "shout" ? " hi" : "");
        auto result = processor->processCommand(cmdLine);
        EXPECT_EQ(result.message.find("Unknown command"), std::string::npos)
            << "primary name failed to dispatch: " << s.name << " -> " << result.message;

        for (const char* alias : s.aliases) {
            EXPECT_TRUE(processor->hasCommand(alias)) << "alias not registered: " << alias << " (of " << s.name << ")";
            auto aliasInfo = processor->getCommandInfo(alias);
            ASSERT_TRUE(aliasInfo.has_value()) << "getCommandInfo missing for alias: " << alias;
            EXPECT_EQ(aliasInfo->name, s.name) << "alias " << alias << " does not resolve to " << s.name;

            std::string aliasLine = "/" + std::string(alias) + (std::string(s.name) == "shout" ? " hi" : "");
            auto aliasResult = processor->processCommand(aliasLine);
            EXPECT_EQ(aliasResult.message.find("Unknown command"), std::string::npos)
                << "alias failed to dispatch: " << alias << " (of " << s.name << ") -> " << aliasResult.message;
        }
    }
}

// ===================================================================
// Group commands (T99 -- migrated from EverQuest::m_command_registry, see
// refactor/T99_action_layer_unification_pilot.md). These exercise the
// SAME RecordingActionHandler used throughout this file, so a passing test
// here is proof the call reaches ActionDispatcher -> IActionHandler, not
// just that CommandProcessor's own bookkeeping is happy.
// ===================================================================

TEST_F(CommandProcessorTest, T99_GroupCommandsRegisteredWithGroupCategory) {
    struct Expected { const char* name; const char* category; };
    static const Expected samples[] = {
        {"invite", "Group"},
        {"disband", "Group"},
        {"decline", "Group"},
        {"groupaccept", "Group"},
    };
    for (const auto& s : samples) {
        auto info = processor->getCommandInfo(s.name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << s.name;
        EXPECT_EQ(info->category, s.category) << "wrong category for: " << s.name;
    }
    // Aliases from the port: /inv (invite), /gaccept (groupaccept).
    EXPECT_TRUE(processor->hasCommand("inv"));
    EXPECT_TRUE(processor->hasCommand("gaccept"));
}

TEST_F(CommandProcessorTest, Invite_WithExplicitName_DispatchesInviteToGroup) {
    auto result = processor->processCommand("/invite Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("inviteToGroup(\"Bob\")"));
}

TEST_F(CommandProcessorTest, Invite_AliasWithExplicitName_DispatchesInviteToGroup) {
    auto result = processor->processCommand("/inv Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("inviteToGroup(\"Bob\")"));
}

TEST_F(CommandProcessorTest, Invite_NoArgsWithTarget_DispatchesInviteTarget) {
    state.combat().setTarget(1234, "a decrepit skeleton", 100, 10);
    auto result = processor->processCommand("/invite");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("inviteTarget()"));
}

TEST_F(CommandProcessorTest, Invite_NoArgsNoTarget_FailsCleanlyWithoutDispatching) {
    ASSERT_FALSE(state.combat().hasTarget());
    auto result = processor->processCommand("/invite");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Disband_NotInGroup_FailsCleanlyWithoutDispatching) {
    ASSERT_FALSE(state.group().inGroup());
    auto result = processor->processCommand("/disband");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Disband_AsLeader_DispatchesLeaveGroupAndReportsDisbanded) {
    state.group().setInGroup(true);
    state.group().setIsLeader(true);
    auto result = processor->processCommand("/disband");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("leaveGroup()"));
}

TEST_F(CommandProcessorTest, Disband_AsMember_DispatchesLeaveGroupAndReportsLeft) {
    state.group().setInGroup(true);
    state.group().setIsLeader(false);
    auto result = processor->processCommand("/disband");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("leaveGroup()"));
}

TEST_F(CommandProcessorTest, Decline_NoPendingInvite_FailsCleanlyWithoutDispatching) {
    ASSERT_FALSE(state.group().hasPendingInvite());
    auto result = processor->processCommand("/decline");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Decline_PendingInvite_DispatchesDeclineGroupInvite) {
    state.group().setPendingInvite("Alice");
    auto result = processor->processCommand("/decline");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("declineGroupInvite()"));
}

// The group-invite-accept path (T97/T98: dead by every route -- game's own
// Social follow_cmd shadowed by Movement's follow, action's old copy
// shadowed then deleted, eq_bridge.cpp's GroupAcceptIntent has no live
// producer). This test is the headless proof that /groupaccept reaches
// ActionDispatcher::acceptGroupInvite(); the live leg proves it against a
// real server (review/T99/).
TEST_F(CommandProcessorTest, GroupAccept_NoPendingInvite_FailsCleanlyWithoutDispatching) {
    ASSERT_FALSE(state.group().hasPendingInvite());
    auto result = processor->processCommand("/groupaccept");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, GroupAccept_PendingInvite_DispatchesAcceptGroupInvite) {
    state.group().setPendingInvite("Alice");
    auto result = processor->processCommand("/groupaccept");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("acceptGroupInvite()"));
}

TEST_F(CommandProcessorTest, GroupAccept_Alias_DispatchesAcceptGroupInvite) {
    state.group().setPendingInvite("Alice");
    auto result = processor->processCommand("/gaccept");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("acceptGroupInvite()"));
}

// ===================================================================
// Spells / Pet commands (T101 -- migrated from EverQuest::m_command_
// registry's Spells (6) and Pet (1) categories, see
// refactor/T101_migrate_spells_pet_family.md). Same shape as the T99 Group
// tests above: RecordingActionHandler proves the call reaches
// ActionDispatcher -> IActionHandler, not just that CommandProcessor's own
// bookkeeping is happy.
// ===================================================================

TEST_F(CommandProcessorTest, T101_SpellsAndPetCommandsRegisteredWithCorrectCategory) {
    struct Expected { const char* name; const char* category; };
    static const Expected samples[] = {
        {"cast", "Spells"},
        {"mem", "Spells"},
        {"forget", "Spells"},
        {"gems", "Spells"},
        {"interrupt", "Spells"},
        {"spellbook", "Spells"},
        {"pet", "Pet"},
    };
    for (const auto& s : samples) {
        auto info = processor->getCommandInfo(s.name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << s.name;
        EXPECT_EQ(info->category, s.category) << "wrong category for: " << s.name;
    }
    // Aliases carried over from game's registrations verbatim.
    EXPECT_TRUE(processor->hasCommand("memspell"));
    EXPECT_TRUE(processor->hasCommand("memorize"));
    EXPECT_TRUE(processor->hasCommand("ducking"));
    EXPECT_TRUE(processor->hasCommand("book"));
    // /skills is deliberately NOT migrated here: registered category
    // "Utility" in game, not "Spells" -- outside this unit's confirmed
    // membership (see refactor/T101_migrate_spells_pet_family.md's Review).
    EXPECT_FALSE(processor->hasCommand("skills"));
}

TEST_F(CommandProcessorTest, Cast_ValidGem_DispatchesBeginCastFromGem) {
    auto result = processor->processCommand("/cast 3");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("beginCastFromGem(3, 0)"));
}

TEST_F(CommandProcessorTest, Cast_WithTarget_PassesTargetId) {
    state.combat().setTarget(555, "a rat", 100, 10);
    auto result = processor->processCommand("/cast 1");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("beginCastFromGem(1, 555)"));
}

TEST_F(CommandProcessorTest, Cast_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/cast");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Cast_OutOfRangeGem_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/cast 9");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Cast_MalformedGem_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/cast abc");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Mem_ValidArgs_DispatchesMemorizeSpellByName) {
    auto result = processor->processCommand("/mem 4 Minor Healing");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("memorizeSpellByName(\"Minor Healing\", 4)"));
}

TEST_F(CommandProcessorTest, Mem_Alias_DispatchesMemorizeSpellByName) {
    auto result = processor->processCommand("/memorize 2 Root");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("memorizeSpellByName(\"Root\", 2)"));
}

TEST_F(CommandProcessorTest, Mem_MissingSpellName_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/mem 4");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Mem_OutOfRangeGem_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/mem 9 Root");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Forget_ValidGem_DispatchesForgetSpell) {
    auto result = processor->processCommand("/forget 5");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("forgetSpell(5)"));
}

TEST_F(CommandProcessorTest, Forget_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/forget");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Forget_OutOfRangeGem_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/forget 13");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Gems_DispatchesListMemorizedSpells) {
    auto result = processor->processCommand("/gems");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("listMemorizedSpells()"));
}

TEST_F(CommandProcessorTest, Interrupt_DispatchesInterruptCast) {
    auto result = processor->processCommand("/interrupt");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("interruptCast()"));
}

TEST_F(CommandProcessorTest, Interrupt_Alias_DispatchesInterruptCast) {
    auto result = processor->processCommand("/ducking");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("interruptCast()"));
}

TEST_F(CommandProcessorTest, Spellbook_DisplaysMessageWithoutDispatching) {
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);
    auto result = processor->processCommand("/spellbook");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(handler.empty());
    ASSERT_EQ(renderer.systemMessages.size(), 1u);
    EXPECT_EQ(renderer.systemMessages[0], "Spellbook window not yet implemented in UI");
}

// ---- Pet ----

TEST_F(CommandProcessorTest, Pet_NoPet_FailsCleanlyWithoutDispatching) {
    ASSERT_FALSE(state.pet().hasPet());
    auto result = processor->processCommand("/pet attack");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Pet_Attack_ExplicitName_ResolvesEntityAndDispatches) {
    state.pet().setPet(999, "a wolf", 10);
    eqt::state::Entity e;
    e.spawnId = 1234;
    e.name = "a decrepit skeleton";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/pet attack skeleton");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendPetCommand(7, 1234)"));  // EQT::PET_ATTACK == 7
}

TEST_F(CommandProcessorTest, Pet_Attack_ExplicitNameNotFound_FailsCleanlyWithoutDispatching) {
    state.pet().setPet(999, "a wolf", 10);
    auto result = processor->processCommand("/pet attack nosuchmob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Pet_Attack_NoNameFallsBackToCombatTarget) {
    state.pet().setPet(999, "a wolf", 10);
    state.combat().setTarget(555, "a rat", 100, 10);
    auto result = processor->processCommand("/pet attack");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendPetCommand(7, 555)"));
}

TEST_F(CommandProcessorTest, Pet_Attack_NoNameNoTarget_FailsCleanlyWithoutDispatching) {
    state.pet().setPet(999, "a wolf", 10);
    ASSERT_FALSE(state.combat().hasTarget());
    auto result = processor->processCommand("/pet attack");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Pet_Vocabulary_DispatchesExpectedCommandId) {
    // T97: game's /pet vocabulary is a strict superset of the pre-T101
    // ActionDispatcher wrapper set (leader/spellhold/nospellhold/notaunt/
    // nohold/standup were all missing). This table covers every subcommand
    // -- the new ones (T101-added wrappers) alongside the pre-existing
    // (T96-fixed) ones, all through the SAME cmdPet() code path.
    struct Case { const char* args; int expectedCommandId; };
    static const Case cases[] = {
        {"back", 1},          // PET_BACKOFF
        {"stop", 1},
        {"follow", 8},        // PET_FOLLOWME
        {"guard", 5},         // PET_GUARDHERE
        {"sit", 9},           // PET_SITDOWN
        {"sitdown", 9},
        {"stand", 10},        // PET_STANDUP (T101-added wrapper)
        {"standup", 10},
        {"taunt", 11},        // PET_TAUNT
        {"notaunt", 14},      // PET_TAUNT_OFF (T101-added wrapper)
        {"hold", 12},         // PET_HOLD
        {"nohold", 21},       // PET_HOLD_OFF (T101-added wrapper)
        {"unhold", 21},
        {"focus", 19},        // PET_FOCUS
        {"nofocus", 19},      // toggle-only, same id
        {"health", 4},        // PET_HEALTHREPORT
        {"report", 4},
        {"leader", 16},       // PET_LEADER (T101-added wrapper)
        {"spellhold", 18},    // PET_SPELLHOLD (T101-added wrapper)
        {"ghold", 18},
        {"nospellhold", 18},  // toggle-only, same id
        {"noghold", 18},
    };
    for (const auto& c : cases) {
        state.pet().setPet(999, "a wolf", 10);
        handler.clear();
        auto result = processor->processCommand(std::string("/pet ") + c.args);
        EXPECT_TRUE(result.success) << "subcommand: " << c.args;
        std::string expected = "sendPetCommand(" + std::to_string(c.expectedCommandId) + ", 0)";
        EXPECT_TRUE(handler.calledOnceWith(expected))
            << "subcommand: " << c.args << " expected: " << expected;
    }
}

TEST_F(CommandProcessorTest, Pet_Dismiss_DispatchesDismissPet) {
    state.pet().setPet(999, "a wolf", 10);
    for (const char* args : {"dismiss", "getlost", "leave"}) {
        handler.clear();
        auto result = processor->processCommand(std::string("/pet ") + args);
        EXPECT_TRUE(result.success);
        ASSERT_TRUE(handler.calledOnceWith("dismissPet()")) << "subcommand: " << args;
    }
}

TEST_F(CommandProcessorTest, Pet_EmptyArgs_ListsCommandsWithoutDispatching) {
    state.pet().setPet(999, "a wolf", 10);
    RecordingOutputRenderer renderer;
    processor->setOutputRenderer(&renderer);
    auto result = processor->processCommand("/pet");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(handler.empty());
    ASSERT_FALSE(renderer.systemMessages.empty());
}

TEST_F(CommandProcessorTest, Pet_UnknownSubcommand_FailsCleanlyWithoutDispatching) {
    state.pet().setPet(999, "a wolf", 10);
    auto result = processor->processCommand("/pet somethingmadeup");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}


// ===================================================================
// Chat commands (T100 -- migrated from EverQuest::m_command_registry, see
// refactor/T100_migrate_chat_family.md). Same shape as the T99 Group tests
// above: exercising RecordingActionHandler proves the call reaches
// ActionDispatcher -> IActionHandler, not just that CommandProcessor's own
// bookkeeping is happy.
// ===================================================================

TEST_F(CommandProcessorTest, T100_ChatCommandsRegisteredWithChatCategory) {
    struct Expected { const char* name; const char* category; };
    static const Expected samples[] = {
        {"say", "Chat"},     {"ooc", "Chat"},   {"auction", "Chat"},
        {"gsay", "Chat"},    {"gu", "Chat"},    {"tell", "Chat"},
        {"emote", "Chat"},   {"reply", "Chat"},
    };
    for (const auto& s : samples) {
        auto info = processor->getCommandInfo(s.name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << s.name;
        EXPECT_EQ(info->category, s.category) << "wrong category for: " << s.name;
    }
    // Aliases ported verbatim from game's registrations.
    EXPECT_TRUE(processor->hasCommand("s"));          // say
    EXPECT_TRUE(processor->hasCommand("o"));           // ooc
    EXPECT_TRUE(processor->hasCommand("auc"));         // auction
    EXPECT_TRUE(processor->hasCommand("g"));           // gsay
    EXPECT_TRUE(processor->hasCommand("guildsay"));    // gu
    EXPECT_TRUE(processor->hasCommand("t"));           // tell
    EXPECT_TRUE(processor->hasCommand("msg"));         // tell
    EXPECT_TRUE(processor->hasCommand("em"));          // emote
    EXPECT_TRUE(processor->hasCommand("me"));          // emote
    EXPECT_TRUE(processor->hasCommand("r"));           // reply
}

TEST_F(CommandProcessorTest, Say_DispatchesSendChatMessageSayChannel) {
    auto result = processor->processCommand("/say hello there");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Say, \"hello there\")"));
}

TEST_F(CommandProcessorTest, OOC_DispatchesSendChatMessageOOCChannel) {
    auto result = processor->processCommand("/ooc anyone selling a lantern");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(OOC, \"anyone selling a lantern\")"));
}

TEST_F(CommandProcessorTest, Auction_DispatchesSendChatMessageAuctionChannel) {
    auto result = processor->processCommand("/auction WTS fine steel rapier");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Auction, \"WTS fine steel rapier\")"));
}

TEST_F(CommandProcessorTest, Gsay_DispatchesSendChatMessageGroupChannel) {
    auto result = processor->processCommand("/gsay incoming add");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Group, \"incoming add\")"));
}

TEST_F(CommandProcessorTest, Gu_DispatchesSendChatMessageGuildChannel) {
    auto result = processor->processCommand("/gu raid at 8pm server time");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Guild, \"raid at 8pm server time\")"));
}

TEST_F(CommandProcessorTest, Emote_DispatchesSendChatMessageEmoteChannel_AsLiteralText) {
    // T97's divergence finding: game's /emote always sends the literal text
    // as a text emote (real EQ behaviour) -- the OLD (T98-deleted) action
    // copy silently reinterpreted 9 known words as animation triggers
    // instead. This test pins the text-emote behaviour, not the animation
    // reinterpretation, matching what was ported.
    auto result = processor->processCommand("/emote dances a jig");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendChatMessage(Emote, \"dances a jig\")"));
}

// /tell argument parsing: preserved EXACTLY per the plan's caution --
// split on the FIRST space only, so a multi-word message is passed through
// unsplit as a single string argument to sendTell().
TEST_F(CommandProcessorTest, Tell_SplitsTargetFromMultiWordMessage) {
    auto result = processor->processCommand("/tell Bob multi word message");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendTell(\"Bob\", \"multi word message\")"));
}

TEST_F(CommandProcessorTest, Tell_Alias_SplitsTargetFromMessage) {
    auto result = processor->processCommand("/t Bob hey there");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendTell(\"Bob\", \"hey there\")"));
}

// game's tell.handler did NOT treat quote characters specially -- they are
// just ordinary characters inside the message string, split only on the
// first space (T98 removed a test named exactly for this,
// TrailingArgumentCommand_QuotesAreNotSpecial; this is its T100 revival now
// that /tell is live again).
TEST_F(CommandProcessorTest, Tell_QuotesAreNotSpecial) {
    auto result = processor->processCommand("/tell Bob \"quoted phrase\" rest");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sendTell(\"Bob\", \"\"quoted phrase\" rest\")"));
}

TEST_F(CommandProcessorTest, Tell_MissingMessage_SingleWordFailsCleanlyWithoutDispatching) {
    // A single word with no following space (e.g. "/tell Bob") is NOT
    // rejected by requiresArgs (Bob alone is non-empty args) -- it must
    // reach cmdTell's own "no space found" branch and fail there without
    // calling the dispatcher, exactly matching game's else-if(!args.empty())
    // usage-message branch.
    auto result = processor->processCommand("/tell Bob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Tell_NoArgsAtAll_FailsViaRequiresArgsWithoutDispatching) {
    // Fully empty args is rejected by CommandProcessor::processCommand's own
    // requiresArgs gate before cmdTell is ever invoked (tell.requiresArgs is
    // true, ported verbatim from game's registration).
    auto result = processor->processCommand("/tell");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Reply_WithMessage_DispatchesReplyToLastTell) {
    auto result = processor->processCommand("/reply thanks for the tell");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("replyToLastTell(\"thanks for the tell\")"));
}

TEST_F(CommandProcessorTest, Reply_Alias_DispatchesReplyToLastTell) {
    auto result = processor->processCommand("/r ok");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("replyToLastTell(\"ok\")"));
}

TEST_F(CommandProcessorTest, Reply_NoMessage_FailsCleanlyWithoutDispatching) {
    // reply.requiresArgs is false (ported verbatim from game), so this must
    // reach cmdReply's own empty-args check rather than being rejected by
    // CommandProcessor's generic gate.
    auto result = processor->processCommand("/reply");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// shout reconciliation (T100): game's own `shout`/`sho` registration was
// deleted from eq_commands.cpp; CommandProcessor's pre-existing shout
// (previously reachable only via the escaping alias "sh") gained "sho" so
// the exact same command surface game used to offer is preserved, now
// entirely inside the action layer. All three spellings must resolve to
// the identical handler.
TEST_F(CommandProcessorTest, Shout_PrimaryNameAndBothAliasesAllDispatch) {
    auto r1 = processor->processCommand("/shout hello1");
    auto r2 = processor->processCommand("/sh hello2");
    auto r3 = processor->processCommand("/sho hello3");
    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);
    EXPECT_TRUE(r3.success);
    ASSERT_EQ(handler.count(), 3u);
    EXPECT_EQ(handler.calls[0], "sendChatMessage(Shout, \"hello1\")");
    EXPECT_EQ(handler.calls[1], "sendChatMessage(Shout, \"hello2\")");
    EXPECT_EQ(handler.calls[2], "sendChatMessage(Shout, \"hello3\")");
}

// ===================================================================
// T102: Combat (12) + Social (10 distinct names / 11 registrations) --
// migrated from EverQuest::m_command_registry's Combat and Social
// categories. See refactor/T102_migrate_combat_social.md.
// ===================================================================

TEST_F(CommandProcessorTest, T102_CombatCommandsRegisteredWithCombatCategory) {
    for (const char* name : {"target", "attack", "stopattack", "aa", "combat",
                              "taunt", "autofire", "assist", "shield", "duel",
                              "duelaccept", "dueldecline"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << name;
        EXPECT_EQ(info->category, "Combat") << name;
    }
}

TEST_F(CommandProcessorTest, T102_SocialCommandsRegisteredWithSocialCategory) {
    for (const char* name : {"trade", "accept", "tradedecline", "canceltrade",
                              "who", "inspect", "lfg", "surname", "random", "yell"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << name;
        EXPECT_EQ(info->category, "Social") << name;
    }
}

// ---------- target ----------

TEST_F(CommandProcessorTest, Target_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/target");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Target_ExplicitName_ResolvesEntityAndDispatches) {
    eqt::state::Entity e;
    e.spawnId = 1234;
    e.name = "a decrepit skeleton";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/target skeleton");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("targetEntity(1234)"));
}

TEST_F(CommandProcessorTest, Target_Alias_ResolvesEntityAndDispatches) {
    eqt::state::Entity e;
    e.spawnId = 1234;
    e.name = "a decrepit skeleton";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/tar skeleton");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("targetEntity(1234)"));
}

TEST_F(CommandProcessorTest, Target_NameNotFound_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/target nosuchmob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- attack / stopattack / aa ----------

TEST_F(CommandProcessorTest, Attack_DispatchesEnableAutoAttack) {
    auto result = processor->processCommand("/attack");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("enableAutoAttack()"));
}

TEST_F(CommandProcessorTest, Attack_Alias_DispatchesEnableAutoAttack) {
    auto result = processor->processCommand("/att");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("enableAutoAttack()"));
}

TEST_F(CommandProcessorTest, StopAttack_DispatchesDisableAutoAttack) {
    auto result = processor->processCommand("/stopattack");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("disableAutoAttack()"));
}

TEST_F(CommandProcessorTest, Aa_DispatchesToggleAutoAttack) {
    auto result = processor->processCommand("/aa");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("toggleAutoAttack()"));
}

TEST_F(CommandProcessorTest, Aa_TildeAlias_DispatchesToggleAutoAttack) {
    auto result = processor->processCommand("/~");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("toggleAutoAttack()"));
}

// ---------- combat on|off|toggle ----------
// T26 could not reach this command at all; T29's unified command channel
// made it reachable in `game`, and this migration keeps it reachable as
// the sole implementation. Confirming that plainly here.

TEST_F(CommandProcessorTest, Combat_On_DispatchesEnableCombatManager) {
    auto result = processor->processCommand("/combat on");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("enableCombatManager()"));
}

TEST_F(CommandProcessorTest, Combat_Off_DispatchesDisableCombatManager) {
    auto result = processor->processCommand("/combat off");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("disableCombatManager()"));
}

TEST_F(CommandProcessorTest, Combat_NoArgs_DispatchesToggleCombatManager) {
    auto result = processor->processCommand("/combat");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("toggleCombatManager()"));
}

TEST_F(CommandProcessorTest, Combat_UnknownArg_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/combat sideways");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- taunt / autofire ----------

TEST_F(CommandProcessorTest, Taunt_NoTarget_FailsCleanlyWithoutDispatching) {
    ASSERT_FALSE(state.combat().hasTarget());
    auto result = processor->processCommand("/taunt");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Taunt_WithTarget_DispatchesTaunt) {
    state.combat().setTarget(555, "a rat", 100, 10);
    auto result = processor->processCommand("/taunt");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("taunt()"));
}

TEST_F(CommandProcessorTest, Autofire_DispatchesToggleAutoFire) {
    auto result = processor->processCommand("/autofire");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("toggleAutoFire()"));
}

TEST_F(CommandProcessorTest, Autofire_Alias_DispatchesToggleAutoFire) {
    auto result = processor->processCommand("/af");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("toggleAutoFire()"));
}

// ---------- assist ----------

TEST_F(CommandProcessorTest, Assist_ExplicitName_ResolvesEntityAndDispatches) {
    eqt::state::Entity e;
    e.spawnId = 1234;
    e.name = "a decrepit skeleton";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/assist skeleton");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("assist(1234)"));
}

TEST_F(CommandProcessorTest, Assist_Alias_NoNameFallsBackToCombatTarget) {
    state.combat().setTarget(555, "a rat", 100, 10);
    auto result = processor->processCommand("/a");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("assist(555)"));
}

TEST_F(CommandProcessorTest, Assist_NoNameNoTarget_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/assist");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Assist_ExplicitNameNotFound_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/assist nosuchmob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- shield / duel (current target only, no name argument) ----------

TEST_F(CommandProcessorTest, Shield_NoTarget_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/shield");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Shield_WithTarget_DispatchesShieldTarget) {
    state.combat().setTarget(555, "a rat", 100, 10);
    auto result = processor->processCommand("/shield");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("shieldTarget(555)"));
}

TEST_F(CommandProcessorTest, Duel_NoTarget_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/duel");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Duel_WithTarget_DispatchesDuelRequest) {
    state.combat().setTarget(555, "a rat", 100, 10);
    auto result = processor->processCommand("/duel");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("duelRequest(555)"));
}

TEST_F(CommandProcessorTest, DuelAccept_DispatchesDuelAccept) {
    // No GameState duel domain -- the pending-duel pre-check lives in
    // EqActionHandler; this layer just dispatches unconditionally.
    auto result = processor->processCommand("/duelaccept");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("duelAccept()"));
}

TEST_F(CommandProcessorTest, DuelDecline_DispatchesDuelDecline) {
    auto result = processor->processCommand("/dueldecline");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("duelDecline()"));
}

// ---------- trade / accept / tradedecline / canceltrade ----------

TEST_F(CommandProcessorTest, Trade_ExplicitName_ResolvesEntityAndDispatches) {
    eqt::state::Entity e;
    e.spawnId = 1234;
    e.name = "Testbuddy";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/trade testbuddy");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestTrade(1234)"));
}

TEST_F(CommandProcessorTest, Trade_NoArgs_DispatchesRequestTradeWithZeroSentinel) {
    // No name given -- the real "no target" check and the live-combat-target
    // fallback both live in EqActionHandler (TradeManager/CombatManager are
    // not projected into GameState; see cmdTrade's own comment). This layer
    // always dispatches with 0 when no name resolves.
    auto result = processor->processCommand("/trade");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestTrade(0)"));
}

TEST_F(CommandProcessorTest, Trade_ExplicitNameNotFound_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/trade nosuchplayer");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Accept_DispatchesAcceptTrade) {
    auto result = processor->processCommand("/accept");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("acceptTrade()"));
}

TEST_F(CommandProcessorTest, TradeDecline_DispatchesDeclineTrade) {
    auto result = processor->processCommand("/tradedecline");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("declineTrade()"));
}

TEST_F(CommandProcessorTest, TradeDecline_Alias_DispatchesDeclineTrade) {
    auto result = processor->processCommand("/rejecttrade");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("declineTrade()"));
}

TEST_F(CommandProcessorTest, CancelTrade_DispatchesCancelTrade) {
    auto result = processor->processCommand("/canceltrade");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("cancelTrade()"));
}

TEST_F(CommandProcessorTest, CancelTrade_Alias_DispatchesCancelTrade) {
    auto result = processor->processCommand("/stoptrade");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("cancelTrade()"));
}

// ---------- who ----------
// T97/T82: `who` is G02's intra-registry duplicate. This ports Social's
// who_cmd (the real server /who request) -- see cmdWho's own comment for
// why over Movement's client-side scan.

TEST_F(CommandProcessorTest, Who_NoArgs_DispatchesRequestWhoWithDefaults) {
    auto result = processor->processCommand("/who");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestWho(\"\", -1, -1)"));
}

TEST_F(CommandProcessorTest, Who_Alias_DispatchesRequestWho) {
    auto result = processor->processCommand("/whoall");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestWho(\"\", -1, -1)"));
}

TEST_F(CommandProcessorTest, Who_Name_DispatchesRequestWhoWithName) {
    auto result = processor->processCommand("/who Testbuddy");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestWho(\"Testbuddy\", -1, -1)"));
}

// Ported verbatim from game's who_cmd: a single numeric token sets lvllow
// only -- lvlhigh only gets lvllow's value as a fallback inside the
// two-token branch (a malformed second token, e.g. "/who 20 x"), not for a
// single bare number. Confirmed by reading who_cmd.handler before writing
// this test, not assumed.
TEST_F(CommandProcessorTest, Who_SingleLevel_DispatchesRequestWhoWithOnlyLowSet) {
    auto result = processor->processCommand("/who 30");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestWho(\"\", 30, -1)"));
}

TEST_F(CommandProcessorTest, Who_LevelRange_DispatchesRequestWhoWithBothLevels) {
    auto result = processor->processCommand("/who 20 30");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestWho(\"\", 20, 30)"));
}

TEST_F(CommandProcessorTest, Who_NameAndLevelRange_DispatchesRequestWhoWithAllThree) {
    auto result = processor->processCommand("/who Testbuddy 20 30");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestWho(\"Testbuddy\", 20, 30)"));
}

// ---------- inspect ----------

TEST_F(CommandProcessorTest, Inspect_ExplicitName_ResolvesEntityAndDispatches) {
    eqt::state::Entity e;
    e.spawnId = 1234;
    e.name = "Testbuddy";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/inspect testbuddy");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("inspectTarget(1234)"));
}

TEST_F(CommandProcessorTest, Inspect_NoNameFallsBackToCombatTarget) {
    state.combat().setTarget(555, "a rat", 100, 10);
    auto result = processor->processCommand("/inspect");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("inspectTarget(555)"));
}

TEST_F(CommandProcessorTest, Inspect_NoNameNoTarget_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/inspect");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- lfg / surname / random / yell ----------

TEST_F(CommandProcessorTest, Lfg_DispatchesToggleLFG) {
    auto result = processor->processCommand("/lfg");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("toggleLFG()"));
}

TEST_F(CommandProcessorTest, Surname_WithName_DispatchesSetSurname) {
    auto result = processor->processCommand("/surname Thunderfist");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("setSurname(\"Thunderfist\")"));
}

TEST_F(CommandProcessorTest, Surname_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/surname");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Random_NoArgs_DispatchesRollRandomWithDefaults) {
    auto result = processor->processCommand("/random");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("rollRandom(0, 100)"));
}

TEST_F(CommandProcessorTest, Random_Alias_DispatchesRollRandom) {
    auto result = processor->processCommand("/roll");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("rollRandom(0, 100)"));
}

TEST_F(CommandProcessorTest, Random_WithBothArgs_DispatchesRollRandomWithGivenRange) {
    auto result = processor->processCommand("/random 5 50");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("rollRandom(5, 50)"));
}

TEST_F(CommandProcessorTest, Yell_DispatchesYellForHelp) {
    auto result = processor->processCommand("/yell");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("yellForHelp()"));
}

} // namespace
