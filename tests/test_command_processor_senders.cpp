// CommandProcessor tests -- the zero-caller-sender wiring units:
// the Social/Find family (T150) and the four recipe senders (T149).
//
// Every command here reaches a sender that had ZERO production callers
// before its unit. T151's framing applies: such a sender is UNVERIFIED, not
// "correct but unreached", so these cases are the first thing that ever
// exercised the command layer above them.
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
// T150: Social / Find family -- four commands over four senders that had
// ZERO production callers before this unit. See
// refactor/T150_social_find_family.md.
// ===================================================================

TEST_F(CommandProcessorTest, T150_CommandsRegisteredWithExpectedCategories) {
    struct Row { const char* name; const char* category; };
    for (const Row& row : {Row{"groupassist", "Group"}, Row{"track", "Skills"},
                           Row{"find", "Social"}, Row{"friendswho", "Social"}}) {
        auto info = processor->getCommandInfo(row.name);
        ASSERT_TRUE(info.has_value()) << row.name;
        EXPECT_EQ(info->category, row.category) << row.name;
    }
}

// The collision guard, asserted rather than merely commented: /assist must
// still be the SINGLE-target assist (HC_OP_Assist), and must not have been
// repurposed for the group assist (HC_OP_AssistGroup). T146 lost a leg to
// exactly this kind of same-name-different-opcode confusion (/face).
TEST_F(CommandProcessorTest, T150_AssistIsUntouchedAndDistinctFromGroupAssist) {
    auto assistInfo = processor->getCommandInfo("assist");
    ASSERT_TRUE(assistInfo.has_value());
    EXPECT_EQ(assistInfo->name, "assist");
    EXPECT_EQ(assistInfo->category, "Combat");

    auto aliasInfo = processor->getCommandInfo("a");
    ASSERT_TRUE(aliasInfo.has_value());
    EXPECT_EQ(aliasInfo->name, "assist");

    auto groupInfo = processor->getCommandInfo("ga");
    ASSERT_TRUE(groupInfo.has_value());
    EXPECT_EQ(groupInfo->name, "groupassist");

    state.combat().setTarget(555, "a rat", 100, 10);
    auto r1 = processor->processCommand("/assist");
    auto r2 = processor->processCommand("/groupassist");
    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);
    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "assist(555)");
    EXPECT_EQ(handler.calls[1], "assistGroup(555)");
}

// ---------- groupassist / track / find: name-or-current-target ----------
// The interface point of the unit: none of these requires a numeric spawn id.

TEST_F(CommandProcessorTest, T150_GroupAssist_ExplicitName_ResolvesEntityAndDispatches) {
    eqt::state::Entity e;
    e.spawnId = 1234;
    e.name = "a decrepit skeleton";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/groupassist skeleton");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("assistGroup(1234)"));
}

TEST_F(CommandProcessorTest, T150_GroupAssist_Alias_NoNameFallsBackToCombatTarget) {
    state.combat().setTarget(555, "a rat", 100, 10);
    auto result = processor->processCommand("/ga");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("assistGroup(555)"));
}

TEST_F(CommandProcessorTest, T150_GroupAssist_NoNameNoTarget_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/groupassist");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ========== Tradeskill recipes (T149) ==========
// All four recipe senders in eq_merchant.cpp had ZERO production callers
// while both recipe reply parsers were already wired into the zone
// dispatcher. T144 made tradeskill containers openable; these commands are
// the other half of the feature.
//
// The assertion that matters most in this block is a NEGATIVE one: no
// recipe command takes an object_type. The container's type is resolved from
// the open container inside EqActionHandler, below IActionHandler, so it
// cannot appear in any recorded call here. A /recipesearch that made the
// player type "17" would be technically wired and practically unreachable.

TEST_F(CommandProcessorTest, T149_RecipeCommandsRegisteredUnderTradeskill) {
    for (const char* name : {"recipesearch", "recipedetails", "recipecombine",
                             "recipefavorites"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << name;
        EXPECT_EQ(info->category, "Tradeskill") << name;
    }
    EXPECT_TRUE(processor->hasCommand("recipes"));
    EXPECT_TRUE(processor->hasCommand("recipeinfo"));
    EXPECT_TRUE(processor->hasCommand("autocombine"));
    EXPECT_TRUE(processor->hasCommand("recipefav"));
}

TEST_F(CommandProcessorTest, T149_RecipeSearch_DefaultTrivialRangeIsNotZeroToZero) {
    // eqemu's search SQL filters `tr.trivial <= maxtrivial`, so a maxtrivial
    // of 0 matches nothing -- the naive default would make every search come
    // back empty. Pin the real default.
    auto result = processor->processCommand("/recipesearch");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("recipeSearch(\"\", 0, 1000)"));
}

TEST_F(CommandProcessorTest, T149_RecipeSearch_PassesQueryVerbatim) {
    auto result = processor->processCommand("/recipesearch iron ration");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("recipeSearch(\"iron ration\", 0, 1000)"));
}

TEST_F(CommandProcessorTest, T149_RecipeSearch_TrivialFlagsOverrideDefaults) {
    EXPECT_TRUE(processor->processCommand("/recipesearch --min 20 --max 120 blade").success);
    ASSERT_TRUE(handler.calledOnceWith("recipeSearch(\"blade\", 20, 120)"));

    handler.clear();
    EXPECT_TRUE(processor->processCommand("/recipesearch --max 50").success);
    ASSERT_TRUE(handler.calledOnceWith("recipeSearch(\"\", 0, 50)"));

    handler.clear();
    EXPECT_TRUE(processor->processCommand("/recipesearch --min 5").success);
    ASSERT_TRUE(handler.calledOnceWith("recipeSearch(\"\", 5, 1000)"));
}

TEST_F(CommandProcessorTest, T149_RecipeSearch_AliasWorks) {
    EXPECT_TRUE(processor->processCommand("/recipes bandage").success);
    ASSERT_TRUE(handler.calledOnceWith("recipeSearch(\"bandage\", 0, 1000)"));
}

TEST_F(CommandProcessorTest, T149_RecipeSearch_RejectsBadFlagsAndOverlongQuery) {
    const std::string tooLong(56, 'a');
    for (const std::string& bad : {std::string("/recipesearch --min"),
                                   std::string("/recipesearch --min abc"),
                                   std::string("/recipesearch --max -1"),
                                   std::string("/recipesearch --max 4294967296"),
                                   std::string("/recipesearch ") + tooLong}) {
        handler.clear();
        auto result = processor->processCommand(bad);
        EXPECT_FALSE(result.success) << "should have failed: " << bad;
        EXPECT_EQ(handler.count(), 0u) << "should not have dispatched: " << bad;
    }
}

TEST_F(CommandProcessorTest, T149_RecipeSearch_RejectsMaxBelowMin) {
    auto result = processor->processCommand("/recipesearch --min 100 --max 10");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T150_GroupAssist_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.combat().setTarget(555, "a rat", 100, 10);
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/groupassist");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// GM commands (T148)
//
// Net-new family, Guild's shape: all nine EverQuest::SendGM*() senders had
// ZERO production callers and no registration in EITHER registry before
// this unit (T146's finding). What is different here is the gate: every one
// of the nine is refused unless PlayerProfile.gm @13054 was set at login.
//
// RecordingActionHandler::gmMode defaults to FALSE, so the not-a-GM path is
// the default in this fixture and the GM path has to be asked for
// explicitly -- which is also why the "refused" tests below come first.
// ===================================================================

TEST_F(CommandProcessorTest, T148_GMCommandsRegisteredWithGMCategory) {
    for (const char* name : {"gmzone", "summon", "goto", "gmfind", "kick",
                              "kill", "hideme", "emotezone", "gmlastname"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << name;
        EXPECT_EQ(info->category, "GM") << "wrong category for: " << name;
    }
}

// ---------- the gate: refused when GM mode was off at login ----------

TEST_F(CommandProcessorTest, T148_GMCommand_NotGMMode_IsRefusedWithoutDispatching) {
    handler.gmMode = false;
    auto result = processor->processCommand("/kick Bob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u) << "a refused GM command must not reach the sender";
}

TEST_F(CommandProcessorTest, T148_GMCommand_NotGMMode_RefusalIsNotUnknownCommand) {
    // The whole point of registering these unconditionally: a player who
    // types /kick must learn the command EXISTS and is unavailable, not
    // that it does not exist. Contrast with a genuinely unknown command.
    handler.gmMode = false;
    auto refused = processor->processCommand("/kick Bob");
    auto unknown = processor->processCommand("/definitelynotacommand Bob");

    EXPECT_FALSE(refused.success);
    EXPECT_FALSE(unknown.success);
    EXPECT_NE(refused.message, unknown.message)
        << "the GM refusal must be distinguishable from 'no such command'";
    EXPECT_TRUE(processor->hasCommand("kick"));
    EXPECT_FALSE(processor->hasCommand("definitelynotacommand"));
}

TEST_F(CommandProcessorTest, T148_AllNineGMCommands_NotGMMode_AreRefused) {
    handler.gmMode = false;
    for (const char* line : {"/gmzone Bob 36", "/summon Bob", "/goto Bob",
                              "/gmfind Bob", "/kick Bob", "/kill Bob",
                              "/hideme on", "/emotezone hello",
                              "/gmlastname Bob Smith"}) {
        handler.clear();
        auto result = processor->processCommand(line);
        EXPECT_FALSE(result.success) << "should have been refused: " << line;
        EXPECT_EQ(handler.count(), 0u) << "should not have dispatched: " << line;
    }
}

TEST_F(CommandProcessorTest, T148_GateIsCheckedBeforeZoneConnection) {
    // An ordinary player who is not zoned in should get the honest reason
    // (not a GM), not the incidental one (not connected).
    handler.gmMode = false;
    state.world().setZoneConnected(false);
    auto notGm = processor->processCommand("/kick Bob");

    handler.gmMode = true;
    auto notConnected = processor->processCommand("/kick Bob");

    EXPECT_FALSE(notGm.success);
    EXPECT_FALSE(notConnected.success);
    EXPECT_NE(notGm.message, notConnected.message);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- the nine, with GM mode on ----------

TEST_F(CommandProcessorTest, T148_GMZone_DispatchesGMZoneRequest) {
    handler.gmMode = true;
    auto result = processor->processCommand("/gmzone Bob 36");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("gmZoneRequest(\"Bob\", 36)"));
}

TEST_F(CommandProcessorTest, T148_GMZone_NonNumericZoneId_FailsCleanlyWithoutDispatching) {
    handler.gmMode = true;
    auto result = processor->processCommand("/gmzone Bob arena");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T150_Track_ExplicitName_ResolvesEntityAndDispatches) {
    eqt::state::Entity e;
    e.spawnId = 4242;
    e.name = "a gnoll pup";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/track gnoll");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("trackTarget(4242)"));
}

TEST_F(CommandProcessorTest, T150_Track_NoNameFallsBackToCombatTarget) {
    state.combat().setTarget(77, "a bat", 100, 5);
    auto result = processor->processCommand("/track");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("trackTarget(77)"));
}

TEST_F(CommandProcessorTest, T150_Track_ExplicitNameNotFound_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/track nosuchmob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T148_GMZone_ZoneIdOutOfUint16Range_FailsCleanlyWithoutDispatching) {
    handler.gmMode = true;
    auto result = processor->processCommand("/gmzone Bob 70000");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T150_Find_ExplicitName_ResolvesEntityAndDispatches) {
    eqt::state::Entity e;
    e.spawnId = 909;
    e.name = "Guard Gehnus";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/find gehnus");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("findPerson(909)"));
}

TEST_F(CommandProcessorTest, T150_Find_FindpersonAlias_DispatchesSameAsPrimary) {
    state.combat().setTarget(31, "a merchant", 100, 20);
    auto result = processor->processCommand("/findperson");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("findPerson(31)"));
}

TEST_F(CommandProcessorTest, T150_Find_NoNameNoTarget_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/find");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T148_GMZone_MissingZoneId_FailsCleanlyWithoutDispatching) {
    handler.gmMode = true;
    auto result = processor->processCommand("/gmzone Bob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- friendswho: a NAME LIST, not an entity id ----------
// eqemu parses this payload as a comma-delimited character-name string and
// resolves it in WORLD, across zones -- so an entity-list lookup would be
// wrong here, not merely redundant. Args are passed through verbatim.

TEST_F(CommandProcessorTest, T150_FriendsWho_PassesNameListThroughVerbatim) {
    auto result = processor->processCommand("/friendswho Testmelee,Testbuddy");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("friendsWho(\"Testmelee,Testbuddy\")"));
}

TEST_F(CommandProcessorTest, T150_FriendsWho_FriendsAlias_DispatchesSameAsPrimary) {
    auto result = processor->processCommand("/friends Testbuddy");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("friendsWho(\"Testbuddy\")"));
}

TEST_F(CommandProcessorTest, T150_FriendsWho_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/friendswho");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T150_FriendsWho_DoesNotShadowWho) {
    auto whoInfo = processor->getCommandInfo("who");
    ASSERT_TRUE(whoInfo.has_value());
    EXPECT_EQ(whoInfo->name, "who");
    auto whoAllInfo = processor->getCommandInfo("whoall");
    ASSERT_TRUE(whoAllInfo.has_value());
    EXPECT_EQ(whoAllInfo->name, "who");
}

TEST_F(CommandProcessorTest, T149_RecipeSearch_QueryAtExactlyTheWireLimitIsAccepted) {
    // RecipesSearch_Struct::query is char[56] and eqemu forces query[55]=0.
    const std::string atLimit(55, 'a');
    auto result = processor->processCommand("/recipesearch " + atLimit);
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("recipeSearch(\"" + atLimit + "\", 0, 1000)"));
}

TEST_F(CommandProcessorTest, T149_RecipeDetails_DispatchesRecipeId) {
    EXPECT_TRUE(processor->processCommand("/recipedetails 4321").success);
    ASSERT_TRUE(handler.calledOnceWith("recipeDetails(4321)"));

    handler.clear();
    EXPECT_TRUE(processor->processCommand("/recipeinfo 1").success);
    ASSERT_TRUE(handler.calledOnceWith("recipeDetails(1)"));
}

TEST_F(CommandProcessorTest, T149_RecipeDetails_RejectsMissingOrJunkArgs) {
    for (const char* bad : {"/recipedetails", "/recipedetails abc", "/recipedetails 12abc",
                            "/recipedetails 4294967296", "/recipedetails 1 2",
                            "/recipedetails 0"}) {
        handler.clear();
        auto result = processor->processCommand(bad);
        EXPECT_FALSE(result.success) << "should have failed: " << bad;
        EXPECT_EQ(handler.count(), 0u) << "should not have dispatched: " << bad;
    }
}

TEST_F(CommandProcessorTest, T149_RecipeAutoCombine_DispatchesRecipeId) {
    EXPECT_TRUE(processor->processCommand("/recipecombine 4321").success);
    ASSERT_TRUE(handler.calledOnceWith("recipeAutoCombine(4321)"));

    handler.clear();
    EXPECT_TRUE(processor->processCommand("/autocombine 7").success);
    ASSERT_TRUE(handler.calledOnceWith("recipeAutoCombine(7)"));
}

TEST_F(CommandProcessorTest, T149_RecipeAutoCombine_RejectsMissingOrJunkArgs) {
    for (const char* bad : {"/recipecombine", "/recipecombine abc", "/recipecombine 0",
                            "/recipecombine 1 2"}) {
        handler.clear();
        auto result = processor->processCommand(bad);
        EXPECT_FALSE(result.success) << "should have failed: " << bad;
        EXPECT_EQ(handler.count(), 0u) << "should not have dispatched: " << bad;
    }
}

TEST_F(CommandProcessorTest, T149_RecipesFavorite_DispatchesIdList) {
    EXPECT_TRUE(processor->processCommand("/recipefavorites 10 20 30").success);
    ASSERT_TRUE(handler.calledOnceWith("recipesFavorite([10,20,30])"));

    handler.clear();
    EXPECT_TRUE(processor->processCommand("/recipefav 99").success);
    ASSERT_TRUE(handler.calledOnceWith("recipesFavorite([99])"));
}

TEST_F(CommandProcessorTest, T149_RecipesFavorite_RejectsEmptyZeroAndJunk) {
    for (const char* bad : {"/recipefavorites", "/recipefavorites abc",
                            "/recipefavorites 10 abc", "/recipefavorites 0",
                            "/recipefavorites 10 0"}) {
        handler.clear();
        auto result = processor->processCommand(bad);
        EXPECT_FALSE(result.success) << "should have failed: " << bad;
        EXPECT_EQ(handler.count(), 0u) << "should not have dispatched: " << bad;
    }
}

TEST_F(CommandProcessorTest, T149_RecipesFavorite_RejectsMoreThan500Ids) {
    std::string cmd = "/recipefavorites";
    for (int i = 1; i <= 501; ++i) {
        cmd += " " + std::to_string(i);
    }
    auto result = processor->processCommand(cmd);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T148_Summon_DispatchesGMSummon) {
    handler.gmMode = true;
    auto result = processor->processCommand("/summon Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("gmSummon(\"Bob\")"));
}

TEST_F(CommandProcessorTest, T148_Goto_DispatchesGMGoto) {
    handler.gmMode = true;
    auto result = processor->processCommand("/goto Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("gmGoto(\"Bob\")"));
}

TEST_F(CommandProcessorTest, T148_GMFind_DispatchesGMFind) {
    handler.gmMode = true;
    auto result = processor->processCommand("/gmfind Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("gmFind(\"Bob\")"));
}

TEST_F(CommandProcessorTest, T148_Kick_DispatchesGMKick) {
    handler.gmMode = true;
    auto result = processor->processCommand("/kick Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("gmKick(\"Bob\")"));
}

TEST_F(CommandProcessorTest, T148_Kill_DispatchesGMKill) {
    handler.gmMode = true;
    auto result = processor->processCommand("/kill Bob");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("gmKill(\"Bob\")"));
}

TEST_F(CommandProcessorTest, T148_HideMe_OnAndOff_DispatchExplicitBool) {
    handler.gmMode = true;
    EXPECT_TRUE(processor->processCommand("/hideme on").success);
    ASSERT_TRUE(handler.calledOnceWith("gmHideMe(true)"));

    handler.clear();
    EXPECT_TRUE(processor->processCommand("/hideme off").success);
    ASSERT_TRUE(handler.calledOnceWith("gmHideMe(false)"));
}

TEST_F(CommandProcessorTest, T148_HideMe_NoArgs_FailsRatherThanGuessing) {
    // Deliberately NOT a toggle -- the client keeps no hidden-state, so a
    // toggle would have to guess. See cmdGMHideMe.
    handler.gmMode = true;
    auto result = processor->processCommand("/hideme");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T149_RecipeCommands_NotConnectedToZone_FailCleanly) {
    state.world().setZoneConnected(false);
    for (const char* cmd : {"/recipesearch iron", "/recipedetails 1",
                            "/recipecombine 1", "/recipefavorites 1"}) {
        handler.clear();
        auto result = processor->processCommand(cmd);
        EXPECT_FALSE(result.success) << cmd;
        EXPECT_EQ(handler.count(), 0u) << cmd;
    }
}

TEST_F(CommandProcessorTest, T148_EmoteZone_DispatchesGMEmoteZone) {
    handler.gmMode = true;
    auto result = processor->processCommand("/emotezone the sky darkens");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("gmEmoteZone(\"the sky darkens\")"));
}

TEST_F(CommandProcessorTest, T148_GMLastName_DispatchesGMLastName) {
    handler.gmMode = true;
    auto result = processor->processCommand("/gmlastname Bob Smith");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("gmLastName(\"Bob\", \"Smith\")"));
}

TEST_F(CommandProcessorTest, T148_GMLastName_MissingLastName_FailsCleanlyWithoutDispatching) {
    handler.gmMode = true;
    auto result = processor->processCommand("/gmlastname Bob");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, T148_GMCommands_NotConnectedToZone_FailCleanlyWithoutDispatching) {
    handler.gmMode = true;
    state.world().setZoneConnected(false);
    for (const char* line : {"/gmzone Bob 36", "/summon Bob", "/goto Bob",
                              "/gmfind Bob", "/kick Bob", "/kill Bob",
                              "/hideme on", "/emotezone hello",
                              "/gmlastname Bob Smith"}) {
        handler.clear();
        auto result = processor->processCommand(line);
        EXPECT_FALSE(result.success) << "should have failed: " << line;
        EXPECT_EQ(handler.count(), 0u) << "should not have dispatched: " << line;
    }
}

// ===================================================================
// /inspectmessage (T171) -- the local inspect message. No sender of its own:
// it is the payload SendInspectAnswer (T170) carries, and the reason this
// client can answer an inspect without wiping the character's stored message.
// Lives in this file because it exists to feed the wire, like everything else
// here.
// ===================================================================

TEST_F(CommandProcessorTest, InspectMessage_NoArgsShowsRatherThanClears) {
    auto result = processor->processCommand("/inspectmessage");
    EXPECT_TRUE(result.success);
    // The destructive reading of a bare command would be "clear". It must be
    // the harmless one: the server never sends the message back, so a
    // wrongly-cleared message is unrecoverable.
    ASSERT_TRUE(handler.calledOnceWith("setInspectMessage(<show>)"));
}

TEST_F(CommandProcessorTest, InspectMessage_ClearRequiresTheExplicitFlag) {
    auto result = processor->processCommand("/inspectmessage -clear");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("setInspectMessage(\"\")"));
}

TEST_F(CommandProcessorTest, InspectMessage_TextIsTakenRawWithInternalSpaces) {
    auto result = processor->processCommand("/inspectmessage Bard for hire");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("setInspectMessage(\"Bard for hire\")"))
        << "the message is prose -- splitting and rejoining it would eat the "
           "user's own spacing";
}

TEST_F(CommandProcessorTest, InspectMessage_LeadingSeparatorSpaceIsDropped) {
    auto result = processor->processCommand("/inspectmessage    Hail");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("setInspectMessage(\"Hail\")"));
}

TEST_F(CommandProcessorTest, InspectMessage_WhitespaceOnlyArgumentShows) {
    auto result = processor->processCommand("/inspectmessage    ");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("setInspectMessage(<show>)"))
        << "a stray trailing space must not be read as a message";
}

TEST_F(CommandProcessorTest, InspectMessage_AliasWorks) {
    auto result = processor->processCommand("/inspectmsg -clear");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("setInspectMessage(\"\")"));
}

TEST_F(CommandProcessorTest, InspectMessage_IsRegisteredUnderSocial) {
    auto info = processor->getCommandInfo("inspectmessage");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->category, "Social");
}

}  // namespace
