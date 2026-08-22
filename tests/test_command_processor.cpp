// Tests for eqt::action::CommandProcessor (T12) -- core mechanics.
//
// Covers: processCommand vs processInput, unknown-command handling,
// registration round-trips, category/completion consistency, argument
// parsing (whitespace, trailing-argument commands like /tell), case
// sensitivity, /loot, output-renderer integration, cmdHail and cmdNpcState,
// per refactor/T12_action_layer.md section 3.
//
// T165 split this file; the per-command-family cases live in the sibling
// test_command_processor_*.cpp files, all built into the same binary. The
// shared fixture is in command_processor_test_fixture.h.

#include "command_processor_test_fixture.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace eqt::action;
using namespace eqt::action::test;
using eqt::state::GameState;
using eqt::tests::CommandProcessorTest;
using eqt::tests::RecordingOutputRenderer;

namespace {

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

// T178: the client-side refusal must NAME the offending id -- the server's own
// "Corpse not found (ent = 0)" hardcodes 0 and never says which id failed, so a
// decayed corpse and a bad request are otherwise indistinguishable.
TEST_F(CommandProcessorTest, Loot_MalformedId_RefusalEchoesOffendingToken) {
    auto result = processor->processCommand("/loot notanumber");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("notanumber"), std::string::npos)
        << "refusal should quote the bad id, got: " << result.message;
}

TEST_F(CommandProcessorTest, Loot_OutOfRangeId_RefusalEchoesOffendingToken) {
    auto result = processor->processCommand("/loot 70000");  // overflows uint16
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("70000"), std::string::npos)
        << "refusal should quote the out-of-range id, got: " << result.message;
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

}  // namespace
