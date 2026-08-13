// Tests for eqt::action::InputActionBridge (T15, the T12 follow-on).
//
// The bridge is driven through a scriptable FakeInputHandler (see
// action_test_fakes.h) and observed at two layers:
//   - the ActionCallback (reportAction), which sees every bridge-level
//     dispatch attempt with its name and ActionResult -- this is the
//     bridge's own contract, independent of whether the dispatcher
//     succeeded;
//   - the RecordingActionHandler behind a real ActionDispatcher, which
//     sees the calls that survived dispatcher validation.
//
// Findings pinned here (T15; the spell-cast off-by-one was then fixed in
// T16 -- see SpellCastRequest_* tests for the corrected 0-based-request to
// 1-based-dispatcher conversion):
//   - "raid" has no mapping in processChatMessages(), so a raid message
//     silently becomes ChatChannel::Say (as does any unknown channel).
//   - A tell with an empty target is dropped without any dispatch or
//     callback.
//   - setInvertMouseY() has no observable effect: the inverted deltaY is
//     computed and never used (vertical look was left to the camera
//     controller).
//   - processContinuousInput()/updateMovementState() are uncalled private
//     methods (movement went renderer-side in Batch D), which also makes
//     the setTurnSpeed() knob inert. Compile-coverage only; nothing to
//     assert through the public surface.

#include "action_test_fakes.h"
#include "client/action/command_processor.h"
#include "client/action/input_action_bridge.h"
#include "client/state/game_state.h"
#include <gtest/gtest.h>

using namespace eqt::action;
using namespace eqt::action::test;
using namespace eqt::input;
using eqt::state::GameState;

namespace {

class InputActionBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<ActionDispatcher>(state);
        dispatcher->setActionHandler(&handler);
        state.world().setZoneConnected(true);

        bridge = std::make_unique<InputActionBridge>(state, *dispatcher);
        bridge->setInputHandler(&input);
        bridge->setActionCallback([this](const std::string& name, const ActionResult& result) {
            reported.push_back({name, result.success});
        });
    }

    void update() { bridge->update(0.016f); }

    bool reportedOnce(const std::string& name, bool success) const {
        int count = 0;
        for (const auto& r : reported) {
            if (r.first == name && r.second == success) ++count;
        }
        return count == 1;
    }

    GameState state;
    RecordingActionHandler handler;
    FakeInputHandler input;
    std::unique_ptr<ActionDispatcher> dispatcher;
    std::unique_ptr<InputActionBridge> bridge;
    std::vector<std::pair<std::string, bool>> reported;
};

// ===================================================================
// update() gating
// ===================================================================

TEST_F(InputActionBridgeTest, Update_DisabledProcessesNothing) {
    bridge->setEnabled(false);
    input.pendingActions.insert(InputAction::Jump);

    update();

    EXPECT_EQ(input.updateCalls, 0);
    EXPECT_TRUE(input.pendingActions.count(InputAction::Jump));
    EXPECT_TRUE(handler.empty());
    EXPECT_TRUE(reported.empty());
}

TEST_F(InputActionBridgeTest, Update_NullInputHandlerIsSafe) {
    bridge->setInputHandler(nullptr);

    EXPECT_NO_THROW(update());
    EXPECT_TRUE(handler.empty());
}

TEST_F(InputActionBridgeTest, Update_CallsInputUpdateOncePerFrame) {
    update();
    update();
    EXPECT_EQ(input.updateCalls, 2);
}

TEST_F(InputActionBridgeTest, EnabledFlag_DefaultsTrueAndRoundTrips) {
    EXPECT_TRUE(bridge->isEnabled());
    bridge->setEnabled(false);
    EXPECT_FALSE(bridge->isEnabled());
}

// ===================================================================
// Discrete actions
// ===================================================================

TEST_F(InputActionBridgeTest, QuitAndScreenshot_ConsumedWithoutDispatch) {
    input.pendingActions.insert(InputAction::Quit);
    input.pendingActions.insert(InputAction::Screenshot);

    update();

    // Consumed (so they don't fire forever) but routed nowhere.
    EXPECT_TRUE(input.pendingActions.empty());
    EXPECT_TRUE(handler.empty());
    EXPECT_TRUE(reported.empty());
}

TEST_F(InputActionBridgeTest, MovementAndCombatActions_RouteToMatchingDispatcherCalls) {
    input.pendingActions = {
        InputAction::ToggleAutorun, InputAction::Jump,
        InputAction::ToggleAutoAttack, InputAction::Attack,
        InputAction::ClearTarget, InputAction::Consider,
    };

    update();

    EXPECT_TRUE(handler.calledOnceWith("toggleAutorun()"));
    EXPECT_TRUE(handler.calledOnceWith("jump()"));
    EXPECT_TRUE(handler.calledOnceWith("toggleAutoAttack()"));
    EXPECT_TRUE(handler.calledOnceWith("enableAutoAttack()"));  // Attack != toggle
    EXPECT_TRUE(handler.calledOnceWith("clearTarget()"));
    EXPECT_TRUE(handler.calledOnceWith("consider()"));
    EXPECT_EQ(handler.count(), 6u);
}

TEST_F(InputActionBridgeTest, TargetingActions_RouteWithCorrectGroupIndices) {
    input.pendingActions = {
        InputAction::TargetSelf,
        InputAction::TargetGroupMember1, InputAction::TargetGroupMember2,
        InputAction::TargetGroupMember3, InputAction::TargetGroupMember4,
        InputAction::TargetGroupMember5,
    };

    update();

    EXPECT_TRUE(handler.calledOnceWith("targetSelf()"));
    // The five hotkeys map to 0-based member indices.
    EXPECT_TRUE(handler.calledOnceWith("targetGroupMember(0)"));
    EXPECT_TRUE(handler.calledOnceWith("targetGroupMember(1)"));
    EXPECT_TRUE(handler.calledOnceWith("targetGroupMember(2)"));
    EXPECT_TRUE(handler.calledOnceWith("targetGroupMember(3)"));
    EXPECT_TRUE(handler.calledOnceWith("targetGroupMember(4)"));
}

TEST_F(InputActionBridgeTest, NearestAndCycleTargeting_RouteThrough) {
    input.pendingActions = {
        InputAction::TargetNearestPC, InputAction::TargetNearestNPC,
        InputAction::CycleTargets, InputAction::CycleTargetsReverse,
    };

    update();

    // With no entities in state the dispatcher may fail these, but the
    // bridge must still have attempted each one exactly once.
    EXPECT_TRUE(reportedOnce("TargetNearestPC", false) || reportedOnce("TargetNearestPC", true));
    EXPECT_TRUE(reportedOnce("TargetNearestNPC", false) || reportedOnce("TargetNearestNPC", true));
    EXPECT_TRUE(reportedOnce("CycleTargets", false) || reportedOnce("CycleTargets", true));
    EXPECT_TRUE(reportedOnce("CycleTargetsReverse", false) || reportedOnce("CycleTargetsReverse", true));
}

TEST_F(InputActionBridgeTest, InteractionActions_RouteThrough) {
    input.pendingActions = {
        InputAction::Hail, InputAction::InteractDoor,
        InputAction::InteractWorldObject, InputAction::Interact,
        InputAction::ReplyToTell,
    };

    update();

    EXPECT_TRUE(reportedOnce("Hail", true));
    EXPECT_TRUE(handler.calledOnceWith("hailTarget()"));
    // Empty state: no doors/objects/NPCs, so these report failed attempts.
    EXPECT_TRUE(reportedOnce("ClickNearestDoor", false));
    EXPECT_TRUE(reportedOnce("InteractWorldObject", false));
    EXPECT_TRUE(reportedOnce("Interact", false));
}

TEST_F(InputActionBridgeTest, DiscreteAction_FiresOnceThenStaysQuiet) {
    input.pendingActions.insert(InputAction::Jump);

    update();
    update();

    EXPECT_TRUE(handler.calledOnceWith("jump()"));
    EXPECT_EQ(handler.count(), 1u);
}

TEST_F(InputActionBridgeTest, UIToggleActions_AreNotBridgeTerritory) {
    // The UI/graphics toggles are handled elsewhere (GraphicsInputHandler /
    // renderer); the bridge must leave them pending, not consume-and-drop.
    input.pendingActions = {
        InputAction::ToggleInventory, InputAction::ToggleSpellbook,
        InputAction::ToggleWireframe, InputAction::OpenChat,
    };

    update();

    EXPECT_EQ(input.pendingActions.size(), 4u);
    EXPECT_TRUE(handler.empty());
    EXPECT_TRUE(reported.empty());
}

// ===================================================================
// Mouse look
// ===================================================================

TEST_F(InputActionBridgeTest, MouseLook_LeftDragTurnsHeadingByScaledDeltaX) {
    state.player().setHeading(100.0f);
    input.state.leftButtonDown = true;
    input.state.mouseDeltaX = 50;

    update();

    // deltaX * sensitivity(1.0) * 0.1 = 5 degrees.
    EXPECT_TRUE(handler.calledOnceWith("setHeading(105)"));
}

TEST_F(InputActionBridgeTest, MouseLook_SensitivityScalesTurn) {
    bridge->setMouseSensitivity(2.0f);
    state.player().setHeading(100.0f);
    input.state.leftButtonDown = true;
    input.state.mouseDeltaX = 50;

    update();

    EXPECT_TRUE(handler.calledOnceWith("setHeading(110)"));
}

TEST_F(InputActionBridgeTest, MouseLook_HeadingWrapsIntoRange) {
    state.player().setHeading(359.0f);
    input.state.leftButtonDown = true;
    input.state.mouseDeltaX = 50;  // +5 degrees -> 364 -> 4

    update();

    EXPECT_TRUE(handler.calledOnceWith("setHeading(4)"));
}

TEST_F(InputActionBridgeTest, MouseLook_NoButtonOrNoDeltaMeansNoTurn) {
    state.player().setHeading(100.0f);

    input.state.leftButtonDown = false;
    input.state.mouseDeltaX = 50;
    update();

    input.state.leftButtonDown = true;
    input.state.mouseDeltaX = 0;
    update();

    EXPECT_TRUE(handler.empty());
}

TEST_F(InputActionBridgeTest, MouseLook_InvertMouseYHasNoObservableEffect) {
    // Pinned finding: deltaY is inverted and then never used -- vertical
    // look was left to the camera controller -- so the invert knob changes
    // nothing the bridge dispatches.
    state.player().setHeading(100.0f);
    input.state.leftButtonDown = true;
    input.state.mouseDeltaX = 50;
    input.state.mouseDeltaY = 80;

    bridge->setInvertMouseY(true);
    update();
    bridge->setInvertMouseY(false);
    update();

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "setHeading(105)");
    EXPECT_EQ(handler.calls[1], "setHeading(105)");
}

// ===================================================================
// Console commands
// ===================================================================

// T98: /sit and /stand were deleted from action::CommandProcessor (shadowed
// dead code -- registry 1 always won the dispatch collision, see
// refactor/T97_command_registry_shadowing_audit.md). Re-pointed to /consider
// and /afk, two surviving no-arg commands with distinct dispatcher calls
// that don't depend on any world-state fixture (unlike e.g. /door, which
// needs a nearby door in GameState to reach the handler at all), to keep
// proving ordering is preserved across the queue drain.
TEST_F(InputActionBridgeTest, ConsoleCommands_ForwardToCommandProcessorInOrder) {
    CommandProcessor processor(state, *dispatcher);
    bridge->setCommandProcessor(&processor);

    input.rawCommands.push("/consider");
    input.rawCommands.push("/afk");

    update();

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "consider()");
    EXPECT_EQ(handler.calls[1], "setAFK(true)");
    EXPECT_TRUE(input.rawCommands.empty());
}

TEST_F(InputActionBridgeTest, ConsoleCommands_WithoutProcessorLeftQueued) {
    input.rawCommands.push("/sit");

    EXPECT_NO_THROW(update());

    // No processor: the bridge returns early without draining the queue.
    EXPECT_EQ(input.rawCommands.size(), 1u);
    EXPECT_TRUE(handler.empty());
}

// ===================================================================
// Chat messages
// ===================================================================

TEST_F(InputActionBridgeTest, ChatMessages_ChannelStringsMapToChannels) {
    input.chatMessages.push({"hello", "say", ""});
    input.chatMessages.push({"loud", "shout", ""});
    input.chatMessages.push({"chatter", "ooc", ""});
    input.chatMessages.push({"selling", "auction", ""});
    input.chatMessages.push({"team", "group", ""});
    input.chatMessages.push({"team2", "gsay", ""});
    input.chatMessages.push({"guildies", "guild", ""});
    input.chatMessages.push({"guildies2", "gu", ""});
    input.chatMessages.push({"waves", "emote", ""});

    update();

    ASSERT_EQ(handler.count(), 9u);
    EXPECT_EQ(handler.calls[0], "sendChatMessage(Say, \"hello\")");
    EXPECT_EQ(handler.calls[1], "sendChatMessage(Shout, \"loud\")");
    EXPECT_EQ(handler.calls[2], "sendChatMessage(OOC, \"chatter\")");
    EXPECT_EQ(handler.calls[3], "sendChatMessage(Auction, \"selling\")");
    EXPECT_EQ(handler.calls[4], "sendChatMessage(Group, \"team\")");
    EXPECT_EQ(handler.calls[5], "sendChatMessage(Group, \"team2\")");
    EXPECT_EQ(handler.calls[6], "sendChatMessage(Guild, \"guildies\")");
    EXPECT_EQ(handler.calls[7], "sendChatMessage(Guild, \"guildies2\")");
    EXPECT_EQ(handler.calls[8], "sendChatMessage(Emote, \"waves\")");
}

TEST_F(InputActionBridgeTest, ChatMessages_TellWithTargetSendsTell) {
    input.chatMessages.push({"psst", "tell", "Friend"});

    update();

    EXPECT_TRUE(handler.calledOnceWith("sendTell(\"Friend\", \"psst\")"));
    EXPECT_TRUE(reportedOnce("SendTell", true));
}

TEST_F(InputActionBridgeTest, ChatMessages_TellWithoutTargetSilentlyDropped) {
    // Pinned behavior: no dispatch, no callback, no error surface at all.
    input.chatMessages.push({"psst", "tell", ""});

    update();

    EXPECT_TRUE(handler.empty());
    EXPECT_TRUE(reported.empty());
}

TEST_F(InputActionBridgeTest, ChatMessages_UnknownChannelFallsBackToSay) {
    // Pinned finding: "raid" is a real ChatChannel with no string mapping
    // here, so it falls into the default Say path like any unknown channel.
    input.chatMessages.push({"raid msg", "raid", ""});
    input.chatMessages.push({"???", "bogus", ""});

    update();

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "sendChatMessage(Say, \"raid msg\")");
    EXPECT_EQ(handler.calls[1], "sendChatMessage(Say, \"???\")");
}

// ===================================================================
// Move commands
// ===================================================================

TEST_F(InputActionBridgeTest, MoveCommands_AllFourTypesRoute) {
    MoveCommand coords{MoveCommand::Type::Coordinates, 10.0f, 20.0f, 30.0f, ""};
    MoveCommand entity{MoveCommand::Type::Entity, 0, 0, 0, "a_rat"};
    MoveCommand stop{MoveCommand::Type::Stop, 0, 0, 0, ""};
    input.moveCommands.push(coords);
    input.moveCommands.push(entity);
    input.moveCommands.push(stop);

    update();

    EXPECT_TRUE(reportedOnce("MoveToLocation", true));
    EXPECT_TRUE(handler.calledOnceWith("moveToLocation(10, 20, 30)"));
    // moveToEntity fails against an empty entity list, but must be attempted.
    EXPECT_TRUE(reportedOnce("MoveToEntity", false));
    EXPECT_TRUE(reportedOnce("StopMovement", true));
    EXPECT_TRUE(handler.calledOnceWith("stopAllMovement()"));
}

TEST_F(InputActionBridgeTest, MoveCommands_FaceWithNameFacesEntity_EmptyNameFacesLocation) {
    MoveCommand faceEntity{MoveCommand::Type::Face, 0, 0, 0, "a_rat"};
    MoveCommand faceLocation{MoveCommand::Type::Face, 5.0f, 5.0f, 0.0f, ""};
    input.moveCommands.push(faceEntity);
    input.moveCommands.push(faceLocation);

    update();

    // Entity not in state -> attempted and failed; location -> setHeading.
    EXPECT_TRUE(reportedOnce("FaceEntity", false));
    EXPECT_TRUE(reportedOnce("FaceLocation", true));
    ASSERT_EQ(handler.count(), 1u);
    EXPECT_EQ(handler.calls[0].rfind("setHeading(", 0), 0u);
}

// ===================================================================
// Spell casts -- 0-based request to 1-based dispatcher (T16 fix)
// ===================================================================

TEST_F(InputActionBridgeTest, SpellCastRequest_GemZeroCastsFirstGem) {
    // Both real producers are 0-based: ConsoleInputHandler pushes
    // (gem - 1) for "/cast 1..8" and GraphicsInputHandler accepts gem
    // clicks 0..7. The dispatcher is 1-based (validates 1-12, handler does
    // gemSlot - 1), so the bridge must convert. Before T16 it forwarded the
    // value unchanged, which made gem 1 fail validation outright.
    input.spellCasts.push({0});

    update();

    EXPECT_TRUE(reportedOnce("CastSpell", true));
    EXPECT_TRUE(handler.calledOnceWith("castSpell(1)"));
}

TEST_F(InputActionBridgeTest, SpellCastRequest_MidAndTopGemsMapExactly) {
    // "/cast 5" -> producer enqueues gemSlot=4 -> dispatcher gem 5 -> handler
    // SpellSlot 4. Before T16 every cast landed one gem low. Gem 8
    // (gemSlot=7, the producers' upper bound) pins the top of the range.
    input.spellCasts.push({4});
    input.spellCasts.push({7});

    update();

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "castSpell(5)");
    EXPECT_EQ(handler.calls[1], "castSpell(8)");
}

// ===================================================================
// Hotbar, targeting, loot requests
// ===================================================================

TEST_F(InputActionBridgeTest, HotbarRequests_SlotMapsToGemSlotPlusOne) {
    input.hotbarRequests.push({0});
    input.hotbarRequests.push({7});

    update();

    // Documented "for now" behavior: every hotbar button is a spell gem,
    // 0-based slot -> 1-based gem. Consistent with the dispatcher's 1-based
    // contract (unlike the SpellCastRequest path above).
    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "castSpell(1)");
    EXPECT_EQ(handler.calls[1], "castSpell(8)");
}

TEST_F(InputActionBridgeTest, TargetRequests_RouteSpawnIdAndDropZero) {
    input.targetRequests.push({0});     // dropped without dispatch
    input.targetRequests.push({1234});

    update();

    EXPECT_TRUE(handler.calledOnceWith("targetEntity(1234)"));
    EXPECT_EQ(handler.count(), 1u);
}

TEST_F(InputActionBridgeTest, LootRequests_RouteCorpseId) {
    input.lootRequests.push({777});

    update();

    EXPECT_TRUE(handler.calledOnceWith("lootCorpse(777)"));
}

TEST_F(InputActionBridgeTest, Requests_QueuesFullyDrainedInOneUpdate) {
    input.targetRequests.push({1});
    input.targetRequests.push({2});
    input.targetRequests.push({3});

    update();

    ASSERT_EQ(handler.count(), 3u);
    EXPECT_EQ(handler.calls[0], "targetEntity(1)");
    EXPECT_EQ(handler.calls[1], "targetEntity(2)");
    EXPECT_EQ(handler.calls[2], "targetEntity(3)");
}

// ===================================================================
// Callback plumbing and config accessors
// ===================================================================

TEST_F(InputActionBridgeTest, NoCallbackSet_DispatchStillWorks) {
    bridge->setActionCallback(nullptr);
    input.pendingActions.insert(InputAction::Jump);

    EXPECT_NO_THROW(update());
    EXPECT_TRUE(handler.calledOnceWith("jump()"));
}

TEST_F(InputActionBridgeTest, Callback_ReceivesNameAndResult) {
    input.pendingActions.insert(InputAction::Jump);

    update();

    ASSERT_EQ(reported.size(), 1u);
    EXPECT_EQ(reported[0].first, "Jump");
    EXPECT_TRUE(reported[0].second);
}

TEST_F(InputActionBridgeTest, ConfigAccessors_RoundTrip) {
    bridge->setMouseSensitivity(0.5f);
    EXPECT_FLOAT_EQ(bridge->getMouseSensitivity(), 0.5f);

    bridge->setInvertMouseY(true);
    EXPECT_TRUE(bridge->getInvertMouseY());

    // Note: turn speed's only consumer (processContinuousInput) is dead
    // code -- the knob round-trips but currently steers nothing.
    bridge->setTurnSpeed(90.0f);
    EXPECT_FLOAT_EQ(bridge->getTurnSpeed(), 90.0f);
}

}  // namespace
