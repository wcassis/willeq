// CommandProcessor tests -- the game-registry migration families:
// the T98 surviving-command enumeration, Group (T99), Spells/Pet (T101)
// and Chat (T100).
//
// Split out of test_command_processor.cpp by T165 (see
// command_processor_test_fixture.h for why). Same binary, same fixture.

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

// T483 (B770 option (a)): Titanium stores a ninth gem, but the official
// client's skin shows eight and offers no ninth, so /cast 9 is refused
// client-side with the same message as every other out-of-range gem.
TEST_F(CommandProcessorTest, Cast_NinthGem_RefusedClientSideWithoutDispatching) {
    auto result = processor->processCommand("/cast 9");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Gem slot must be 1-8");
    EXPECT_EQ(handler.count(), 0u);
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
    // T272: still no dispatcher call -- CommandProcessor has no channel to
    // the renderer's popup state -- but the text no longer claims the
    // spellbook window is missing. It exists (Ctrl+B / /newbook) and its
    // rows are clickable since T272.
    ASSERT_EQ(renderer.systemMessages.size(), 3u);
    EXPECT_EQ(renderer.systemMessages[0], "Spellbook: press Ctrl+B to open it (or /newbook).");
    EXPECT_EQ(renderer.systemMessages[1], "Click a spell to memorize it into the first empty gem;");
    EXPECT_EQ(renderer.systemMessages[2], "click a gem first to choose which gem to overwrite.");
    for (const auto& m : renderer.systemMessages) {
        EXPECT_EQ(m.find("not yet implemented"), std::string::npos)
            << "the stale stub text is back: " << m;
    }
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

// T222: the "No one to reply to" pre-check game's reply_cmd had (and T100
// could not port -- m_last_tell_sender was unreachable from the action
// layer) is now reproduced via IActionHandler::hasTellToReplyTo().
TEST_F(CommandProcessorTest, Reply_NoPendingTell_FailsWithMessageWithoutDispatching) {
    handler.tellToReplyTo = false;
    auto result = processor->processCommand("/reply anyone there?");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "You have no one to reply to.");
    EXPECT_EQ(handler.count(), 0u);
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

}  // namespace
