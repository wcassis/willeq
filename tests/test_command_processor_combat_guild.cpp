// CommandProcessor tests -- Combat and Social (T102) and Guild (T132).
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

// ===================================================================
// Guild commands (T132)
//
// Net-new family -- action::CommandProcessor had zero guild registrations
// before this unit (T98 emptied the category and there was nothing
// underneath to revive, unlike T99-T102's reconcile-and-delete families).
// See refactor/T132_migrate_guild_family.md.
// ===================================================================

TEST_F(CommandProcessorTest, T132_GuildCommandsRegisteredWithGuildCategory) {
    for (const char* name : {"guildinvite", "guildaccept", "guilddecline",
                              "guildremove", "guilddemote", "guildleader",
                              "guildmotd"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << name;
        EXPECT_EQ(info->category, "Guild") << "wrong category for: " << name;
    }
    // Aliases ported verbatim from eq_commands.cpp's Guild block.
    EXPECT_TRUE(processor->hasCommand("ginvite"));
    EXPECT_TRUE(processor->hasCommand("acceptguild"));
    EXPECT_TRUE(processor->hasCommand("declineguild"));
    EXPECT_TRUE(processor->hasCommand("gremove"));
    EXPECT_TRUE(processor->hasCommand("gkick"));
    EXPECT_TRUE(processor->hasCommand("gdemote"));
    EXPECT_TRUE(processor->hasCommand("gleader"));
    EXPECT_TRUE(processor->hasCommand("gmotd"));
}

// ---------- guildinvite ----------

TEST_F(CommandProcessorTest, GuildInvite_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/guildinvite");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, GuildInvite_WithName_DispatchesGuildInvite) {
    auto result = processor->processCommand("/guildinvite Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildInvite(\"Bob\")"));
}

TEST_F(CommandProcessorTest, GuildInvite_Alias_DispatchesGuildInvite) {
    auto result = processor->processCommand("/ginvite Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildInvite(\"Bob\")"));
}

TEST_F(CommandProcessorTest, GuildInvite_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/guildinvite Bob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- guildaccept / guilddecline ----------

TEST_F(CommandProcessorTest, GuildAccept_DispatchesGuildAcceptInvite) {
    auto result = processor->processCommand("/guildaccept");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildAcceptInvite()"));
}

TEST_F(CommandProcessorTest, GuildAccept_Alias_DispatchesGuildAcceptInvite) {
    auto result = processor->processCommand("/acceptguild");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildAcceptInvite()"));
}

TEST_F(CommandProcessorTest, GuildDecline_DispatchesGuildDeclineInvite) {
    auto result = processor->processCommand("/guilddecline");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildDeclineInvite()"));
}

TEST_F(CommandProcessorTest, GuildDecline_Alias_DispatchesGuildDeclineInvite) {
    auto result = processor->processCommand("/declineguild");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildDeclineInvite()"));
}

// ---------- guildremove ----------

TEST_F(CommandProcessorTest, GuildRemove_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/guildremove");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, GuildRemove_WithName_DispatchesGuildRemove) {
    auto result = processor->processCommand("/guildremove Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildRemove(\"Bob\")"));
}

TEST_F(CommandProcessorTest, GuildRemove_AliasGremove_DispatchesGuildRemove) {
    auto result = processor->processCommand("/gremove Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildRemove(\"Bob\")"));
}

TEST_F(CommandProcessorTest, GuildRemove_AliasGkick_DispatchesGuildRemove) {
    auto result = processor->processCommand("/gkick Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildRemove(\"Bob\")"));
}

// ---------- guilddemote ----------

TEST_F(CommandProcessorTest, GuildDemote_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/guilddemote");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, GuildDemote_WithName_DispatchesGuildDemote) {
    auto result = processor->processCommand("/guilddemote Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildDemote(\"Bob\")"));
}

TEST_F(CommandProcessorTest, GuildDemote_Alias_DispatchesGuildDemote) {
    auto result = processor->processCommand("/gdemote Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildDemote(\"Bob\")"));
}

// ---------- guildleader ----------

TEST_F(CommandProcessorTest, GuildLeader_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/guildleader");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, GuildLeader_WithName_DispatchesGuildMakeLeader) {
    auto result = processor->processCommand("/guildleader Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildMakeLeader(\"Bob\")"));
}

TEST_F(CommandProcessorTest, GuildLeader_Alias_DispatchesGuildMakeLeader) {
    auto result = processor->processCommand("/gleader Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("guildMakeLeader(\"Bob\")"));
}

// ---------- guildmotd ----------

TEST_F(CommandProcessorTest, GuildMotd_NoArgs_DispatchesRequestGuildMOTD) {
    auto result = processor->processCommand("/guildmotd");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestGuildMOTD()"));
}

TEST_F(CommandProcessorTest, GuildMotd_WithMessage_DispatchesSetGuildMOTD) {
    auto result = processor->processCommand("/guildmotd Welcome to the guild!");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("setGuildMOTD(\"Welcome to the guild!\")"));
}

TEST_F(CommandProcessorTest, GuildMotd_Alias_DispatchesRequestGuildMOTD) {
    auto result = processor->processCommand("/gmotd");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestGuildMOTD()"));
}

// ========== Corpse / Resurrection (T134) ==========

TEST_F(CommandProcessorTest, T134_CorpseCommandsRegisteredWithCorpseCategory) {
    for (const char* name : {"corpsedrag", "corpsedrop", "consent", "deny"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << name;
        EXPECT_EQ(info->category, "Corpse") << "wrong category for: " << name;
    }
    // Aliases ported verbatim from eq_commands.cpp's Corpse block.
    EXPECT_TRUE(processor->hasCommand("drag"));
    EXPECT_TRUE(processor->hasCommand("drop"));
}

TEST_F(CommandProcessorTest, T134_RezCommandsRegisteredWithUtilityCategory) {
    for (const char* name : {"rezaccept", "rezdecline"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << name;
        EXPECT_EQ(info->category, "Utility") << "wrong category for: " << name;
    }
    // Aliases ported verbatim from eq_commands.cpp's Resurrection block.
    EXPECT_TRUE(processor->hasCommand("acceptrez"));
    EXPECT_TRUE(processor->hasCommand("declinerez"));
}

// ---------- corpsedrag ----------

TEST_F(CommandProcessorTest, CorpseDrag_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/corpsedrag");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, CorpseDrag_WithName_DispatchesCorpseDrag) {
    auto result = processor->processCommand("/corpsedrag Bob's Corpse");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("corpseDrag(\"Bob's Corpse\")"));
}

TEST_F(CommandProcessorTest, CorpseDrag_Alias_DispatchesCorpseDrag) {
    auto result = processor->processCommand("/drag Bob's Corpse");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("corpseDrag(\"Bob's Corpse\")"));
}

TEST_F(CommandProcessorTest, CorpseDrag_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/corpsedrag Bob's Corpse");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- corpsedrop ----------

TEST_F(CommandProcessorTest, CorpseDrop_DispatchesCorpseDrop) {
    auto result = processor->processCommand("/corpsedrop");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("corpseDrop()"));
}

TEST_F(CommandProcessorTest, CorpseDrop_Alias_DispatchesCorpseDrop) {
    auto result = processor->processCommand("/drop");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("corpseDrop()"));
}

// ---------- consent / deny ----------

TEST_F(CommandProcessorTest, Consent_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/consent");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Consent_WithName_DispatchesConsent) {
    auto result = processor->processCommand("/consent Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("consent(\"Bob\")"));
}

TEST_F(CommandProcessorTest, Deny_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/deny");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Deny_WithName_DispatchesConsentDeny) {
    auto result = processor->processCommand("/deny Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("consentDeny(\"Bob\")"));
}

TEST_F(CommandProcessorTest, Consent_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/consent Bob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- rezaccept / rezdecline ----------
// Both route to EverQuest::SendRezzAnswer(bool) on the sender side, but at
// the dispatch layer they are two distinct IActionHandler/ActionDispatcher
// methods (rezAccept()/rezDecline()) -- these assertions are the flag
// distinction the plan calls out explicitly.

TEST_F(CommandProcessorTest, RezAccept_DispatchesRezAccept) {
    auto result = processor->processCommand("/rezaccept");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("rezAccept()"));
}

TEST_F(CommandProcessorTest, RezAccept_Alias_DispatchesRezAccept) {
    auto result = processor->processCommand("/acceptrez");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("rezAccept()"));
}

TEST_F(CommandProcessorTest, RezDecline_DispatchesRezDecline) {
    auto result = processor->processCommand("/rezdecline");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("rezDecline()"));
}

TEST_F(CommandProcessorTest, RezDecline_Alias_DispatchesRezDecline) {
    auto result = processor->processCommand("/declinerez");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("rezDecline()"));
}

TEST_F(CommandProcessorTest, RezAccept_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/rezaccept");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

}  // namespace
