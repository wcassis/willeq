// test_action_dispatcher_guards.cpp -- section 2 of the original: guard
// behaviour. isReady, the no-handler broad sweep, the
// checkHandler()-only-vs-checkZoneConnection() split, reply-to-tell,
// nearest-world-object, and T148 GM gate.
//
// Split out of tests/test_action_dispatcher.cpp by T453 (contiguous range
// 899..1216 of the 1,217-line original, moved verbatim). 15 cases. The
// fixture is in tests/action_dispatcher_test_fixture.h.
#include "action_dispatcher_test_fixture.h"

// ===================================================================
// 2. Guard behavior
// ===================================================================

TEST_F(ActionDispatcherTest, IsReady_FalseUntilHandlerSet) {
    EXPECT_FALSE(dispatcher->isReady());
    connectHandler();
    EXPECT_TRUE(dispatcher->isReady());
}

// Regression guard (not a bug hunt, per the plan): every one of these
// currently-guarded methods must fail cleanly with no handler set, and
// must not touch the (null) handler. A method added later without a guard
// would show up here as a crash or an unexpected dispatch.
TEST_F(ActionDispatcherTest, NoHandler_BroadSweepFailsCleanlyWithoutDispatch) {
    // Handler intentionally not set; zone intentionally not connected.
    EXPECT_FALSE(dispatcher->isReady());

    std::vector<ActionResult> results;
    results.push_back(dispatcher->stopAllMovement());
    results.push_back(dispatcher->setHeading(10));
    results.push_back(dispatcher->jump());
    results.push_back(dispatcher->sit());
    results.push_back(dispatcher->stand());
    results.push_back(dispatcher->toggleAutorun());
    results.push_back(dispatcher->moveToLocation(1, 2, 3));
    results.push_back(dispatcher->stopFollow());
    results.push_back(dispatcher->targetEntity(1));
    results.push_back(dispatcher->targetNearestPC());
    results.push_back(dispatcher->targetNearestNPC());
    results.push_back(dispatcher->cycleTargets());
    results.push_back(dispatcher->clearTarget());
    results.push_back(dispatcher->targetSelf());
    results.push_back(dispatcher->enableAutoAttack());
    results.push_back(dispatcher->disableAutoAttack());
    results.push_back(dispatcher->toggleAutoAttack());
    results.push_back(dispatcher->castSpell(1));
    results.push_back(dispatcher->interruptCast());
    results.push_back(dispatcher->useSkill(1));
    results.push_back(dispatcher->consider());
    results.push_back(dispatcher->hail());
    results.push_back(dispatcher->hailTarget());
    results.push_back(dispatcher->clickNearestDoor());
    results.push_back(dispatcher->lootCorpse(1));
    results.push_back(dispatcher->sendChatMessage(ChatChannel::Say, "hi"));
    results.push_back(dispatcher->sendTell("Bob", "hi"));
    results.push_back(dispatcher->replyToLastTell("hi"));
    results.push_back(dispatcher->openReplyToTell());
    results.push_back(dispatcher->inviteToGroup("Bob"));
    results.push_back(dispatcher->inviteTarget());
    results.push_back(dispatcher->acceptGroupInvite());
    results.push_back(dispatcher->declineGroupInvite());
    results.push_back(dispatcher->leaveGroup());
    results.push_back(dispatcher->setAFK(true));
    results.push_back(dispatcher->toggleAFK());
    results.push_back(dispatcher->setAnonymous(true));
    results.push_back(dispatcher->useItem(1));
    results.push_back(dispatcher->forgetSpell(1));
    results.push_back(dispatcher->requestTrade(1));
    results.push_back(dispatcher->acceptTrade());
    results.push_back(dispatcher->cancelTrade());
    results.push_back(dispatcher->sendPetCommand(1));
    results.push_back(dispatcher->dismissPet());
    results.push_back(dispatcher->petAttack());
    results.push_back(dispatcher->clickWorldObject(1));
    results.push_back(dispatcher->setMovementMode(1));
    results.push_back(dispatcher->setPositionState(1));
    results.push_back(dispatcher->setRoleplay(true));
    results.push_back(dispatcher->setPathfinding(true));
    results.push_back(dispatcher->setAutoHunting(true));
    results.push_back(dispatcher->setAutoLoot(true));
    results.push_back(dispatcher->listHuntTargets());
    results.push_back(dispatcher->listEntities(""));
    results.push_back(dispatcher->dumpEntityAppearance("Bob"));
    results.push_back(dispatcher->sendAnimation(1));

    for (const auto& r : results) {
        EXPECT_FALSE(r.success);
        EXPECT_FALSE(r.message.empty());
    }
    EXPECT_TRUE(handler.empty());
}

// ===================================================================
// Guard split: checkHandler()-only vs checkZoneConnection().
// checkZoneConnection() calls checkHandler() first, so with a handler set
// but zone NOT connected: checkZoneConnection() methods must fail, while
// checkHandler()-only methods must still succeed (and dispatch).
// ===================================================================

TEST_F(ActionDispatcherTest, HandlerWithoutZone_ZoneGatedMethodsFail) {
    connectHandler();
    // Zone intentionally not connected.
    ASSERT_FALSE(state.world().isZoneConnected());

    EXPECT_FALSE(dispatcher->setHeading(10).success);
    EXPECT_FALSE(dispatcher->jump().success);
    EXPECT_FALSE(dispatcher->sit().success);
    EXPECT_FALSE(dispatcher->stand().success);
    EXPECT_FALSE(dispatcher->toggleAutorun().success);
    EXPECT_FALSE(dispatcher->targetEntity(1).success);
    EXPECT_FALSE(dispatcher->enableAutoAttack().success);
    EXPECT_FALSE(dispatcher->toggleAutoAttack().success);
    EXPECT_FALSE(dispatcher->castSpell(1).success);
    EXPECT_FALSE(dispatcher->consider().success);
    EXPECT_FALSE(dispatcher->hail().success);
    EXPECT_FALSE(dispatcher->setRoleplay(true).success);
    EXPECT_FALSE(dispatcher->setAutoHunting(true).success);
    EXPECT_FALSE(dispatcher->listEntities("").success);
    EXPECT_FALSE(dispatcher->sendAnimation(1).success);

    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, HandlerWithoutZone_CheckHandlerOnlyMethodsSucceed) {
    connectHandler();
    // Zone intentionally not connected: these methods use checkHandler()
    // only (no checkZoneConnection()), so they must still succeed.
    ASSERT_FALSE(state.world().isZoneConnected());

    EXPECT_TRUE(dispatcher->stopAllMovement().success);
    EXPECT_TRUE(dispatcher->stopFollow().success);
    EXPECT_TRUE(dispatcher->clearTarget().success);
    EXPECT_TRUE(dispatcher->disableAutoAttack().success);
    EXPECT_TRUE(dispatcher->interruptCast().success);
    EXPECT_TRUE(dispatcher->openReplyToTell().success);
    state.group().setPendingInvite("Dave");
    EXPECT_TRUE(dispatcher->declineGroupInvite().success);
    EXPECT_TRUE(dispatcher->cancelTrade().success);
    EXPECT_TRUE(dispatcher->setPathfinding(true).success);

    // Every one of these (except openReplyToTell, which only QUERIES the
    // handler -- T316 -- and never dispatches) must have actually dispatched.
    EXPECT_TRUE(handler.calledOnceWith("stopAllMovement()"));
    EXPECT_TRUE(handler.calledOnceWith("stopFollow()"));
    EXPECT_TRUE(handler.calledOnceWith("clearTarget()"));
    EXPECT_TRUE(handler.calledOnceWith("disableAutoAttack()"));
    EXPECT_TRUE(handler.calledOnceWith("interruptCast()"));
    EXPECT_TRUE(handler.calledOnceWith("declineGroupInvite()"));
    EXPECT_TRUE(handler.calledOnceWith("cancelTrade()"));
    EXPECT_TRUE(handler.calledOnceWith("setPathfinding(true)"));
}

TEST_F(ActionDispatcherTest, ZoneConnected_GatedMethodsSucceed) {
    connectHandler();
    connectZone();

    EXPECT_TRUE(dispatcher->jump().success);
    EXPECT_TRUE(dispatcher->targetNearestNPC().success);
    EXPECT_TRUE(dispatcher->setRoleplay(true).success);
    EXPECT_EQ(handler.count(), 3u);
}

// T316: openReplyToTell() used to return Success("open_reply") -- a sentinel,
// no sender, and success even when no tell had ever arrived. It now asks the
// handler for the last tell sender (a pure query, so the call log stays empty)
// and hands back the text the chat input opens prefilled with.
TEST_F(ActionDispatcherTest, OpenReplyToTell_ReturnsPrefillForLastTellSender) {
    connectHandler();
    handler.lastTellSender = "Friend";

    auto result = dispatcher->openReplyToTell();

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "/tell Friend ")
        << "the sentinel \"open_reply\" carried no sender and opened nothing";
    EXPECT_TRUE(handler.empty()) << "a pure query, not a dispatch";
}

TEST_F(ActionDispatcherTest, OpenReplyToTell_NoTellReceived_FailsInsteadOfSucceeding) {
    connectHandler();
    handler.lastTellSender = "";

    auto result = dispatcher->openReplyToTell();

    EXPECT_FALSE(result.success);
    // Same wording /reply refuses with (T222).
    EXPECT_EQ(result.message, "You have no one to reply to.");
    EXPECT_TRUE(handler.empty());
}

// T316: interactNearestWorldObject() used to return
// Failure("Use clickWorldObject with drop ID") unconditionally. It now runs
// the handler's nearest-object query and clicks what it finds -- the same
// shape as clickNearestDoor() directly above it in the dispatcher.
TEST_F(ActionDispatcherTest, InteractNearestWorldObject_ClicksTheNearestObject) {
    connectHandler();
    connectZone();
    handler.nearestWorldObjectId = 190;
    handler.nearestWorldObjectDistance = 4.0f;

    auto result = dispatcher->interactNearestWorldObject();

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(handler.calledOnceWith("clickWorldObject(190)"));
}

TEST_F(ActionDispatcherTest, InteractNearestWorldObject_NothingNearby_FailsWithoutDispatching) {
    connectHandler();
    connectZone();
    handler.nearestWorldObjectId = 0;

    auto result = dispatcher->interactNearestWorldObject();

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "No object nearby");
    EXPECT_TRUE(handler.empty());
}

// T316: interactNearest()'s own comment has always said "door, object, or
// NPC", but the object branch was missing -- with no door and no NPC it
// refused even while standing at a forge.
TEST_F(ActionDispatcherTest, InteractNearest_PicksWorldObjectWhenItIsTheOnlyThingAround) {
    connectHandler();
    connectZone();
    handler.nearestWorldObjectId = 190;
    handler.nearestWorldObjectDistance = 4.0f;

    auto result = dispatcher->interactNearest();

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "Interacting with object");
    EXPECT_TRUE(handler.calledOnceWith("clickWorldObject(190)"));
}

// ===================================================================
// T148: the GM gate at the dispatcher layer.
//
// checkGMMode() is the single point that consults the login-time
// PlayerProfile.gm @13054 flag. These tests drive it directly, without a
// CommandProcessor, so a regression that moved or weakened the gate cannot
// hide behind command-name plumbing.
// ===================================================================

TEST_F(ActionDispatcherTest, T148_NoHandler_GMCommandFailsWithoutCrashing) {
    // checkGMMode() calls checkHandler() first, so a null handler must not
    // be dereferenced by isGMMode().
    auto result = dispatcher->gmKick("Bob");
    EXPECT_FALSE(result.success);
}

TEST_F(ActionDispatcherTest, T148_NotGMMode_AllNineRefusedWithoutTouchingHandler) {
    connectHandler();
    connectZone();
    handler.gmMode = false;

    EXPECT_FALSE(dispatcher->gmZoneRequest("Bob", 36).success);
    EXPECT_FALSE(dispatcher->gmSummon("Bob").success);
    EXPECT_FALSE(dispatcher->gmGoto("Bob").success);
    EXPECT_FALSE(dispatcher->gmFind("Bob").success);
    EXPECT_FALSE(dispatcher->gmKick("Bob").success);
    EXPECT_FALSE(dispatcher->gmKill("Bob").success);
    EXPECT_FALSE(dispatcher->gmHideMe(true).success);
    EXPECT_FALSE(dispatcher->gmEmoteZone("hi").success);
    EXPECT_FALSE(dispatcher->gmLastName("Bob", "Smith").success);

    EXPECT_TRUE(handler.empty()) << "no GM sender may be reached when GM mode was off at login";
}

TEST_F(ActionDispatcherTest, T148_GMMode_AllNineRouteToMatchingHandlerMethods) {
    connectHandler();
    connectZone();
    handler.gmMode = true;

    EXPECT_TRUE(dispatcher->gmZoneRequest("Bob", 36).success);
    EXPECT_TRUE(dispatcher->gmSummon("Bob").success);
    EXPECT_TRUE(dispatcher->gmGoto("Bob").success);
    EXPECT_TRUE(dispatcher->gmFind("Bob").success);
    EXPECT_TRUE(dispatcher->gmKick("Bob").success);
    EXPECT_TRUE(dispatcher->gmKill("Bob").success);
    EXPECT_TRUE(dispatcher->gmHideMe(true).success);
    EXPECT_TRUE(dispatcher->gmEmoteZone("hi").success);
    EXPECT_TRUE(dispatcher->gmLastName("Bob", "Smith").success);

    EXPECT_TRUE(handler.calledOnceWith("gmZoneRequest(\"Bob\", 36)"));
    EXPECT_TRUE(handler.calledOnceWith("gmSummon(\"Bob\")"));
    EXPECT_TRUE(handler.calledOnceWith("gmGoto(\"Bob\")"));
    EXPECT_TRUE(handler.calledOnceWith("gmFind(\"Bob\")"));
    EXPECT_TRUE(handler.calledOnceWith("gmKick(\"Bob\")"));
    EXPECT_TRUE(handler.calledOnceWith("gmKill(\"Bob\")"));
    EXPECT_TRUE(handler.calledOnceWith("gmHideMe(true)"));
    EXPECT_TRUE(handler.calledOnceWith("gmEmoteZone(\"hi\")"));
    EXPECT_TRUE(handler.calledOnceWith("gmLastName(\"Bob\", \"Smith\")"));
    EXPECT_EQ(handler.count(), 9u);
}

TEST_F(ActionDispatcherTest, T148_RefusalMessageNamesGMModeAndTheRelogFix) {
    connectHandler();
    connectZone();
    handler.gmMode = false;

    auto result = dispatcher->gmKick("Bob");
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.message.find("GM"), std::string::npos)
        << "refusal must say this is a GM command: " << result.message;
    EXPECT_NE(result.message.find("log out"), std::string::npos)
        << "refusal must tell the GM how to refresh (relog): " << result.message;
    EXPECT_NE(result.message, "Not connected to zone")
        << "the GM refusal must not be confused with the zone-connection failure";
}

TEST_F(ActionDispatcherTest, T148_GMMode_EmptyArgumentsRejectedBeforeDispatch) {
    connectHandler();
    connectZone();
    handler.gmMode = true;

    EXPECT_FALSE(dispatcher->gmZoneRequest("", 36).success);
    EXPECT_FALSE(dispatcher->gmSummon("").success);
    EXPECT_FALSE(dispatcher->gmGoto("").success);
    EXPECT_FALSE(dispatcher->gmFind("").success);
    EXPECT_FALSE(dispatcher->gmKick("").success);
    EXPECT_FALSE(dispatcher->gmKill("").success);
    EXPECT_FALSE(dispatcher->gmEmoteZone("").success);
    EXPECT_FALSE(dispatcher->gmLastName("Bob", "").success);
    EXPECT_FALSE(dispatcher->gmLastName("", "Smith").success);
    EXPECT_TRUE(handler.empty());
}

