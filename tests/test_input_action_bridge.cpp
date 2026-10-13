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
//   - (T228) The bridge-side mouse-look branch, processContinuousInput()
//     and the mouseSensitivity/invertMouseY/turnSpeed knobs were deleted:
//     the renderer owns movement and turning, the branch never fired at
//     runtime (T218 probe) and would have turned from an unprojected
//     heading of 0. The MouseLook_* and ConfigAccessors tests that only
//     exercised that code went with it.

#include "action_test_fakes.h"
#include "client/action/command_processor.h"
#include "client/action/input_action_bridge.h"
#include "client/hotbar/hotbar_model.h"  // T174: content-typed hotbar routing
#include "client/state/game_state.h"
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <vector>
#include <iterator>
#include <string>

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

TEST_F(InputActionBridgeTest, GraphicsToggleActions_AreNotBridgeTerritory) {
    // The graphics toggles are handled elsewhere -- GraphicalMode drains them
    // with consumeAction() (src/client/mode/graphical_mode.cpp) -- so the
    // bridge must leave them PENDING, not consume-and-drop.
    //
    // T368 (B100): this case was UIToggleActions_AreNotBridgeTerritory and
    // stood on ToggleVendor/ToggleSpellbook, the last two consumer-less UI
    // toggles in the enum. With all seven deleted the UI half has no subject
    // left, but the case itself does: the four below are REAL actions with a
    // real consumer that is not this bridge, which is a stronger subject than
    // the dead enumerators ever were. Restated rather than deleted.
    input.pendingActions = {
        InputAction::ToggleWireframe, InputAction::ToggleHUD,
        InputAction::ToggleNameTags, InputAction::ToggleZoneLights,
    };

    update();

    EXPECT_EQ(input.pendingActions.size(), 4u);
    EXPECT_TRUE(handler.empty());
    EXPECT_TRUE(reported.empty());
}

// T368 (B100): the four residual consumer-less UI toggles are GONE from
// eqt::input::InputAction -- ToggleVendor, TogglePetWindow, ToggleTrainer,
// ToggleSpellbook, the last of the seven T365 started on. None had a
// producer (no InputHandler set them) and none had a consumer
// (processDiscreteActions has no arm for any). The real toggles live in
// HotkeyAction + RendererAction, which are untouched.
//
// The enumerators cannot be named here any more, so this reads the header as
// text -- the same source-text idiom test_headless_command_refusal.cpp uses
// for a claim about a file rather than about a value. It fails loudly if any
// of the four is reintroduced.
TEST(InputActionEnumTest, T368_NoConsumerlessUITogglesRemain) {
    std::ifstream in(INPUT_ACTION_HEADER);
    ASSERT_TRUE(in.is_open()) << INPUT_ACTION_HEADER;

    // An enumerator is a line that is nothing but the name and a comma (the
    // enum is one-per-line throughout). The names still appear in the
    // tombstone comment above, which is the point of keeping them there --
    // matching whole LINES is what tells the two apart.
    std::vector<std::string> found;
    std::string line;
    while (std::getline(in, line)) {
        const auto b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        auto e = line.find_last_not_of(" \t");
        std::string trimmed = line.substr(b, e - b + 1);
        for (const char* dead : {"ToggleVendor", "TogglePetWindow",
                                 "ToggleTrainer", "ToggleSpellbook"}) {
            if (trimmed == std::string(dead) + ",") found.push_back(dead);
        }
    }

    EXPECT_TRUE(found.empty())
        << found.size() << " consumer-less UI toggle(s) are enumerators of "
           "eqt::input::InputAction again (first: " 
        << (found.empty() ? std::string("-") : found.front())
        << "); UI toggles belong in HotkeyAction/RendererAction (B100)";
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

// T316: the two keybind-reachable actions that used to report success (or a
// fixed failure) and change nothing. Driven here at the layer the key press
// actually lands on -- InputAction -> InputActionBridge -> ActionDispatcher --
// not at the dispatcher method, so the wiring is pinned along with the fix.
TEST_F(InputActionBridgeTest, InteractWorldObject_ClicksNearestObject) {
    handler.nearestWorldObjectId = 190;
    handler.nearestWorldObjectDistance = 4.0f;
    input.pendingActions = {InputAction::InteractWorldObject};

    update();

    EXPECT_TRUE(reportedOnce("InteractWorldObject", true))
        << "this action reported failure unconditionally before T316";
    EXPECT_TRUE(handler.calledOnceWith("clickWorldObject(190)"));
}

TEST_F(InputActionBridgeTest, InteractWorldObject_NothingNearby_ReportsFailure) {
    handler.nearestWorldObjectId = 0;
    input.pendingActions = {InputAction::InteractWorldObject};

    update();

    EXPECT_TRUE(reportedOnce("InteractWorldObject", false));
    EXPECT_TRUE(handler.empty());
}

TEST_F(InputActionBridgeTest, ReplyToTell_ReportsPrefillWhenATellHasArrived) {
    handler.lastTellSender = "Friend";
    input.pendingActions = {InputAction::ReplyToTell};

    ActionResult seen;
    bridge->setActionCallback([&](const std::string& name, const ActionResult& r) {
        if (name == "ReplyToTell") seen = r;
    });

    update();

    EXPECT_TRUE(seen.success);
    EXPECT_EQ(seen.message, "/tell Friend ")
        << "the pre-T316 sentinel was the fixed string \"open_reply\"";
}

TEST_F(InputActionBridgeTest, ReplyToTell_NoTellReceived_ReportsFailure) {
    handler.lastTellSender = "";
    input.pendingActions = {InputAction::ReplyToTell};

    update();

    EXPECT_TRUE(reportedOnce("ReplyToTell", false))
        << "reporting success with nobody to reply to was the defect";
}

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

TEST_F(InputActionBridgeTest, HotbarRequests_NoModelFallsBackToSpellGem) {
    // T174: with no HotbarModel wired the bridge cannot know a slot's content
    // type, so it degrades to the pre-T174 behavior: every hotbar button is a
    // spell gem, 0-based slot -> 1-based gem. (SetUp() wires no model.)
    input.hotbarRequests.push({0});
    input.hotbarRequests.push({7});

    update();

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "castSpell(1)");
    EXPECT_EQ(handler.calls[1], "castSpell(8)");
}

// ===================================================================
// Hotbar content-type routing (T174)
//
// Before T174 processHotbarRequests() forwarded EVERY keyboard hotbar press
// to castSpell(slot + 1), so a Skill/Item/Emote slot could never fire from
// the keyboard. These tests wire a real HotbarModel (the same one the mouse
// path reads) and assert each slot type reaches its matching dispatcher call.
// They FAIL against the old single-castSpell body (measured: see
// refactor/T174_keyboard_hotbar.md).
// ===================================================================

TEST_F(InputActionBridgeTest, HotbarSkillSlot_ReachesUseSkillNotCastSpell) {
    eqt::HotbarModel model;
    model.assignSlot({0, 0}, eqt::HotbarSlotType::Skill, /*id=*/73);  // Taunt
    bridge->setHotbarModel(&model);

    input.hotbarRequests.push({0});
    update();

    EXPECT_TRUE(handler.calledOnceWith("useSkill(73)"));
    EXPECT_EQ(handler.count(), 1u);  // and NOT castSpell
    EXPECT_TRUE(reportedOnce("UseSkill", true));
}

// T230: an Item slot carries an ITEM id, but useItem() takes an INVENTORY
// SLOT. The keyboard route now goes through ActionDispatcher::useItemById(),
// which resolves the hop through IActionHandler::findItemSlotByItemId (the
// same InventoryManager lookup the mouse path does) and only then calls
// useItem(resolvedSlot). Before T230 the arm was a logged skip (measured red:
// see refactor/T230_target_resolver_and_hotbar_items.md).
TEST_F(InputActionBridgeTest, HotbarItemSlot_ReachesUseItemWithResolvedInventorySlot) {
    eqt::HotbarModel model;
    model.assignSlot({0, 3}, eqt::HotbarSlotType::Item, /*id=*/1234);  // item id
    bridge->setHotbarModel(&model);
    handler.inventoryByItemId[1234] = 24;  // carried in general slot 24

    input.hotbarRequests.push({3});
    update();

    EXPECT_TRUE(handler.calledOnceWith("useItem(24)"));  // the SLOT, not the id
    EXPECT_EQ(handler.count(), 1u);                      // and no castSpell
    EXPECT_TRUE(reportedOnce("UseItem", true));
}

TEST_F(InputActionBridgeTest, HotbarItemSlot_ItemNotCarried_FailsWithoutUsing) {
    eqt::HotbarModel model;
    model.assignSlot({0, 3}, eqt::HotbarSlotType::Item, /*id=*/1234);
    bridge->setHotbarModel(&model);
    // handler.inventoryByItemId empty: item 1234 is not in inventory.

    input.hotbarRequests.push({3});
    update();

    EXPECT_TRUE(handler.empty());   // no useItem(1234) misfire, no castSpell
    EXPECT_TRUE(reportedOnce("UseItem", false));
}

TEST_F(InputActionBridgeTest, HotbarSpellSlot_StillReachesCastSpellByPosition) {
    eqt::HotbarModel model;
    model.assignSlot({0, 2}, eqt::HotbarSlotType::Spell, /*id=*/999);
    bridge->setHotbarModel(&model);

    input.hotbarRequests.push({2});
    update();

    // Preserved behavior: spell slots map hotbar position (0-based) to gem
    // (1-based), so slot 2 -> castSpell(3).
    EXPECT_TRUE(handler.calledOnceWith("castSpell(3)"));
    EXPECT_EQ(handler.count(), 1u);
}

TEST_F(InputActionBridgeTest, HotbarEmptySlot_IsSkippedNotCast) {
    eqt::HotbarModel model;
    // slot 5 left Empty (default).
    bridge->setHotbarModel(&model);

    input.hotbarRequests.push({5});  // Empty
    update();

    // No dispatcher call and no reportAction -- log-and-skip.
    EXPECT_TRUE(handler.empty());
    EXPECT_TRUE(reported.empty());
}

// T239: the three command-type slots reach the dispatcher's command route
// (was a logged skip for all three, T174/T230).
TEST_F(InputActionBridgeTest, HotbarEmoteSlot_ReachesCommandRouteWithItsText) {
    eqt::HotbarModel model;
    model.assignSlot({0, 1}, eqt::HotbarSlotType::Emote, /*id=*/0, "/wave");
    bridge->setHotbarModel(&model);

    input.hotbarRequests.push({1});
    update();

    EXPECT_TRUE(handler.calledOnceWith("activateHotbarCommandSlot(Emote, \"/wave\")"));
    EXPECT_TRUE(reportedOnce("HotbarCommand", true));
}

TEST_F(InputActionBridgeTest, HotbarMacroSlot_ReachesCommandRouteWithItsText) {
    eqt::HotbarModel model;
    model.assignSlot({0, 2}, eqt::HotbarSlotType::Macro, /*id=*/0, "/say hi");
    bridge->setHotbarModel(&model);

    input.hotbarRequests.push({2});
    update();

    EXPECT_TRUE(handler.calledOnceWith("activateHotbarCommandSlot(Macro, \"/say hi\")"));
    EXPECT_TRUE(reportedOnce("HotbarCommand", true));
}

TEST_F(InputActionBridgeTest, HotbarCombatSlot_ReachesCommandRoute) {
    eqt::HotbarModel model;
    model.assignSlot({0, 4}, eqt::HotbarSlotType::Combat, /*id=*/0);
    bridge->setHotbarModel(&model);

    input.hotbarRequests.push({4});
    update();

    EXPECT_TRUE(handler.calledOnceWith("activateHotbarCommandSlot(Combat, \"\")"));
    EXPECT_TRUE(reportedOnce("HotbarCommand", true));
}

TEST_F(InputActionBridgeTest, HotbarMacroSlot_EmptyText_FailsWithoutDispatch) {
    eqt::HotbarModel model;
    model.assignSlot({0, 2}, eqt::HotbarSlotType::Macro, /*id=*/0, "");
    bridge->setHotbarModel(&model);

    input.hotbarRequests.push({2});
    update();

    // The dispatcher refuses ("Hotbar slot has no command") before the handler.
    EXPECT_TRUE(handler.empty());
    EXPECT_TRUE(reportedOnce("HotbarCommand", false));
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

}  // namespace
