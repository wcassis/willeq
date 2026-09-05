// Tests for eqt::action::ActionDispatcher (T12).
//
// Priorities per refactor/T12_action_layer.md:
//   1. Routing correctness across all action families (breadth, with
//      per-member routing strings inside families that have near-identical
//      members — that's where copy-paste mis-wiring hides).
//   2. Guard behavior: no-handler (regression guard, expected to pass —
//      not a bug hunt), handler-without-zone vs handler-with-zone (real
//      behavior: checkHandler()-only methods succeed without zone
//      connection, checkZoneConnection() methods do not).

#include "action_test_fakes.h"
#include "client/action/action_dispatcher.h"
#include "client/state/game_state.h"
#include "client/pet_constants.h"
#include <gtest/gtest.h>

using namespace eqt::action;
using namespace eqt::action::test;
using eqt::state::GameState;

namespace {

class ActionDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<ActionDispatcher>(state);
    }

    GameState state;
    RecordingActionHandler handler;
    std::unique_ptr<ActionDispatcher> dispatcher;

    void connectHandler() { dispatcher->setActionHandler(&handler); }
    void connectZone() { state.world().setZoneConnected(true); }
};

// ===================================================================
// 1. Routing correctness across families
// ===================================================================

TEST_F(ActionDispatcherTest, MovementFamily_RoutesToMatchingHandlerMethods) {
    connectHandler();
    connectZone();

    // T264: startMoving()/stopMoving() (the Direction family) were deleted --
    // no production caller; key movement is the renderer's own physics.
    EXPECT_TRUE(dispatcher->stopAllMovement().success);
    EXPECT_TRUE(dispatcher->jump().success);
    EXPECT_TRUE(dispatcher->sit().success);
    EXPECT_TRUE(dispatcher->stand().success);
    EXPECT_TRUE(dispatcher->toggleAutorun().success);

    ASSERT_EQ(handler.count(), 5u);
    EXPECT_EQ(handler.calls[0], "stopAllMovement()");
    EXPECT_EQ(handler.calls[1], "jump()");
    EXPECT_EQ(handler.calls[2], "sit()");
    EXPECT_EQ(handler.calls[3], "stand()");
    EXPECT_EQ(handler.calls[4], "toggleAutorun()");
}

TEST_F(ActionDispatcherTest, SetHeading_NormalizesAndPassesThrough) {
    connectHandler();
    connectZone();

    EXPECT_TRUE(dispatcher->setHeading(400.0f).success);
    ASSERT_TRUE(handler.calledOnceWith("setHeading(40)"));

    handler.clear();
    EXPECT_TRUE(dispatcher->setHeading(-10.0f).success);
    ASSERT_TRUE(handler.calledOnceWith("setHeading(350)"));
}

TEST_F(ActionDispatcherTest, FaceLocation_ComputesHeadingAndCallsSetHeading) {
    connectHandler();
    connectZone();

    // Player at origin facing dx=0, dy=1 (north) -> heading 0.
    ASSERT_TRUE(dispatcher->faceLocation(0.0f, 1.0f, 0.0f).success);
    ASSERT_EQ(handler.count(), 1u);
    EXPECT_EQ(handler.calls[0], "setHeading(0)");
}

TEST_F(ActionDispatcherTest, FaceEntity_NotFound_Fails) {
    connectHandler();
    connectZone();

    auto result = dispatcher->faceEntity("Nobody");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, FaceEntity_Found_CallsSetHeading) {
    connectHandler();
    connectZone();

    eqt::state::Entity e;
    e.spawnId = 5;
    e.name = "Bob";
    e.x = 0;
    e.y = 10;
    e.z = 0;
    state.entities().addEntity(e);

    ASSERT_TRUE(dispatcher->faceEntity("Bob").success);
    ASSERT_TRUE(handler.calledOnceWith("setHeading(0)"));
}

// T350 (B003). /face <name> must resolve the SAME entity /target <name>
// does -- the T244 nearest-first ranked overload -- not the unranked one,
// which ties on lowest spawn id. Measured live in review/T350: with two
// `a_fire_beetle`s in qeynos2, /target took the one 3.5 units away while
// /face turned to the one 550 units away, and eqemu then refused every
// auto-attack swing (Mob::IsFacingMob, +-80 of 511.5 heading units).
TEST_F(ActionDispatcherTest, FaceEntity_PicksTheNearestMatch_NotTheLowestSpawnId) {
    connectHandler();
    connectZone();

    // Player at the origin. The FAR beetle has the lower spawn id, so the
    // unranked resolver picks it; the near one is due north (heading 0).
    eqt::state::Entity far;
    far.spawnId = 68;
    far.name = "a_fire_beetle000";
    far.x = 100.0f;
    far.y = 0.0f;   // due east of the player -> heading 90
    far.z = 0.0f;
    state.entities().addEntity(far);

    eqt::state::Entity near;
    near.spawnId = 277;
    near.name = "a_fire_beetle005";
    near.x = 0.0f;
    near.y = 10.0f; // due north of the player -> heading 0
    near.z = 0.0f;
    state.entities().addEntity(near);

    ASSERT_TRUE(dispatcher->faceEntity("a_fire_beetle").success);
    ASSERT_TRUE(handler.calledOnceWith("setHeading(0)"));
}

// T356 (B003c). The same defect T350 fixed for /face, one layer down:
// /moveto and /follow used the UNRANKED findEntityByName(name) purely as an
// existence check and then passed the NAME to the handler, which re-resolved
// it game-side with a THIRD resolver (EntitiesComponent::FindEntityByName --
// unranked case-insensitive prefix match, lowest spawn id wins). With two
// same-named mobs the player walked to / followed a different one than
// /target and /face had picked. The dispatcher now resolves once with the
// T244 nearest-first overload and hands the handler a spawn id.
//
// The near beetle spawns SECOND and carries the HIGHER spawn id, so an
// unranked first-match picks the far one.
TEST_F(ActionDispatcherTest, MoveToAndFollow_PickTheNearestMatch_NotTheLowestSpawnId) {
    connectHandler();
    connectZone();

    eqt::state::Entity far;
    far.spawnId = 68;
    far.name = "a_fire_beetle000";
    far.x = 100.0f;
    far.y = 0.0f;
    far.z = 0.0f;
    state.entities().addEntity(far);

    eqt::state::Entity near;
    near.spawnId = 277;
    near.name = "a_fire_beetle005";
    near.x = 0.0f;
    near.y = 10.0f;
    near.z = 0.0f;
    state.entities().addEntity(near);

    ASSERT_TRUE(dispatcher->moveToEntity("a_fire_beetle").success);
    ASSERT_TRUE(dispatcher->followEntity("a_fire_beetle").success);

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "moveToEntity(277)");
    EXPECT_EQ(handler.calls[1], "followEntity(277)");
}

// T362 (B070). The same defect, one command later: /warp shipped the raw
// user-typed name to the game side, which resolved it with a THIRD resolver
// (EntitiesComponent::FindEntityByName -- unranked prefix match, lowest spawn
// id wins), so with two same-named mobs /warp teleported to a different one
// than /target had picked. The dispatcher now resolves the ENTITY form once,
// ranked nearest-first, and hands the handler a spawn id via warpToEntity();
// the empty and coordinate forms still go down as the raw arg string.
//
// Same fixture as MoveToAndFollow above: the near beetle spawns SECOND and
// carries the HIGHER spawn id, so an unranked first-match picks the far one.
TEST_F(ActionDispatcherTest, Warp_ByName_PicksTheNearestMatch_NotTheLowestSpawnId) {
    connectHandler();
    connectZone();

    eqt::state::Entity far;
    far.spawnId = 68;
    far.name = "a_fire_beetle000";
    far.x = 100.0f;
    far.y = 0.0f;
    far.z = 0.0f;
    state.entities().addEntity(far);

    eqt::state::Entity near;
    near.spawnId = 277;
    near.name = "a_fire_beetle005";
    near.x = 0.0f;
    near.y = 10.0f;
    near.z = 0.0f;
    state.entities().addEntity(near);

    ASSERT_TRUE(dispatcher->warp("a_fire_beetle").success);
    ASSERT_EQ(handler.count(), 1u);
    EXPECT_EQ(handler.calls[0], "warpToEntity(277)");
}

// The other two forms must NOT be name-resolved: they still reach the handler
// as the raw arg string, parsed by CommandsComponent::RequestWarp().
TEST_F(ActionDispatcherTest, Warp_CoordinatesAndEmptyStillGoDownAsTheRawArgString) {
    connectHandler();
    connectZone();

    EXPECT_TRUE(dispatcher->warp("100 200 300").success);
    EXPECT_TRUE(dispatcher->warp("").success);

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "warp(\"100 200 300\")");
    EXPECT_EQ(handler.calls[1], "warp(\"\")");
}

// An unresolvable name fails at the dispatcher and never touches the handler
// -- the same shape as moveToEntity/followEntity above.
TEST_F(ActionDispatcherTest, Warp_UnknownName_FailsWithoutTouchingTheHandler) {
    connectHandler();
    connectZone();

    EXPECT_FALSE(dispatcher->warp("no_such_entity").success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, MoveToFamily_RequiresEntityAndRoutesCorrectly) {
    connectHandler();
    connectZone();

    // Not found cases don't touch the handler.
    EXPECT_FALSE(dispatcher->moveToEntity("Ghost").success);
    EXPECT_FALSE(dispatcher->followEntity("Ghost").success);
    EXPECT_TRUE(handler.empty());

    eqt::state::Entity e;
    e.spawnId = 7;
    e.name = "Target";
    state.entities().addEntity(e);

    EXPECT_TRUE(dispatcher->moveToLocation(1.0f, 2.0f, 3.0f).success);
    EXPECT_TRUE(dispatcher->moveToEntity("Target").success);
    EXPECT_TRUE(dispatcher->followEntity("Target").success);
    EXPECT_TRUE(dispatcher->stopFollow().success);

    ASSERT_EQ(handler.count(), 4u);
    EXPECT_EQ(handler.calls[0], "moveToLocation(1, 2, 3)");
    EXPECT_EQ(handler.calls[1], "moveToEntity(7)");
    EXPECT_EQ(handler.calls[2], "followEntity(7)");
    EXPECT_EQ(handler.calls[3], "stopFollow()");
}

// The most likely real bug in this file per the plan: copy-pasted
// near-identical targeting members calling the wrong sibling method.
TEST_F(ActionDispatcherTest, TargetingFamily_EachMemberRoutesToItsOwnHandlerMethod) {
    connectHandler();
    connectZone();

    dispatcher->targetEntity(42);
    // T230: targetEntityByName was deleted (unreached second name resolver).
    // T264: targetNearest() was deleted (a "not yet implemented" stub with no
    // caller; targetNearestPC/NPC are the live members).
    dispatcher->targetNearestPC();
    dispatcher->targetNearestNPC();
    dispatcher->cycleTargets();
    dispatcher->cycleTargetsReverse();
    dispatcher->clearTarget();
    dispatcher->targetSelf();
    dispatcher->targetGroupMember(3);

    ASSERT_EQ(handler.count(), 8u);
    EXPECT_EQ(handler.calls[0], "targetEntity(42)");
    EXPECT_EQ(handler.calls[1], "targetNearestPC()");
    EXPECT_EQ(handler.calls[2], "targetNearestNPC()");
    EXPECT_EQ(handler.calls[3], "cycleTargets()");
    EXPECT_EQ(handler.calls[4], "cycleTargetsReverse()");
    EXPECT_EQ(handler.calls[5], "clearTarget()");
    EXPECT_EQ(handler.calls[6], "targetSelf()");
    EXPECT_EQ(handler.calls[7], "targetGroupMember(3)");
}

TEST_F(ActionDispatcherTest, AutoAttackFamily_EachMemberRoutesDistinctly) {
    connectHandler();
    connectZone();

    dispatcher->enableAutoAttack();
    dispatcher->disableAutoAttack();
    dispatcher->toggleAutoAttack();

    ASSERT_EQ(handler.count(), 3u);
    EXPECT_EQ(handler.calls[0], "enableAutoAttack()");
    EXPECT_EQ(handler.calls[1], "disableAutoAttack()");
    EXPECT_EQ(handler.calls[2], "toggleAutoAttack()");
}

TEST_F(ActionDispatcherTest, CastSpellFamily_GemSlotBoundsAndRouting) {
    connectHandler();
    connectZone();

    EXPECT_FALSE(dispatcher->castSpell(0).success);
    EXPECT_FALSE(dispatcher->castSpell(13).success);
    EXPECT_TRUE(handler.empty());

    EXPECT_TRUE(dispatcher->castSpell(1).success);
    EXPECT_TRUE(dispatcher->castSpell(12).success);

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "castSpell(1)");
    EXPECT_EQ(handler.calls[1], "castSpell(12)");
}

TEST_F(ActionDispatcherTest, InteractionFamily_DoorsAndCorpses) {
    connectHandler();
    connectZone();

    EQT::DoorState door;
    door.doorId = 3;
    door.x = 10;
    door.y = 10;
    door.z = 0;
    state.doors().addDoor(door);

    // T264: clickDoor(id) was deleted (no caller; DoorInteractIntent and
    // clickNearestDoor() are the live door routes).
    auto& player = state.player();
    player.setPosition(9, 9, 0);
    EXPECT_TRUE(dispatcher->clickNearestDoor().success);

    dispatcher->hail();
    dispatcher->hailTarget();
    dispatcher->lootCorpse(11);
    dispatcher->lootItem(11, 2);
    dispatcher->lootAll(11);
    dispatcher->consider();

    ASSERT_EQ(handler.count(), 7u);
    EXPECT_EQ(handler.calls[0], "clickNearestDoor()");
    EXPECT_EQ(handler.calls[1], "hail()");
    EXPECT_EQ(handler.calls[2], "hailTarget()");
    EXPECT_EQ(handler.calls[3], "lootCorpse(11)");
    EXPECT_EQ(handler.calls[4], "lootItem(11, 2)");
    EXPECT_EQ(handler.calls[5], "lootAll(11)");
    EXPECT_EQ(handler.calls[6], "consider()");
}

TEST_F(ActionDispatcherTest, InteractNearest_PrefersCloserOfDoorAndNPC) {
    connectHandler();
    connectZone();

    state.player().setPosition(0, 0, 0);

    EQT::DoorState door;
    door.doorId = 1;
    door.x = 100;
    door.y = 0;
    door.z = 0;
    state.doors().addDoor(door);

    eqt::state::Entity npc;
    npc.spawnId = 55;
    npc.name = "Guard";
    npc.npcType = 1;
    npc.x = 5;
    npc.y = 0;
    npc.z = 0;
    state.entities().addEntity(npc);

    auto result = dispatcher->interactNearest();
    EXPECT_TRUE(result.success);
    // NPC (dist 5) is closer than door (dist 100): should target + hail, not click door.
    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "targetEntity(55)");
    EXPECT_EQ(handler.calls[1], "hailTarget()");
}

TEST_F(ActionDispatcherTest, ChatFamily_EachChannelRoutesDistinctlyAndValidatesEmpty) {
    connectHandler();
    connectZone();

    EXPECT_FALSE(dispatcher->sendChatMessage(ChatChannel::Say, "").success);
    EXPECT_TRUE(handler.empty());

    EXPECT_TRUE(dispatcher->sendChatMessage(ChatChannel::Say, "hi").success);
    EXPECT_TRUE(dispatcher->sendChatMessage(ChatChannel::Shout, "yo").success);
    EXPECT_TRUE(dispatcher->sendChatMessage(ChatChannel::Group, "grp").success);
    EXPECT_FALSE(dispatcher->sendTell("", "msg").success);
    EXPECT_FALSE(dispatcher->sendTell("Bob", "").success);
    EXPECT_TRUE(dispatcher->sendTell("Bob", "hey").success);
    EXPECT_TRUE(dispatcher->replyToLastTell("reply").success);

    ASSERT_EQ(handler.count(), 5u);
    EXPECT_EQ(handler.calls[0], "sendChatMessage(Say, \"hi\")");
    EXPECT_EQ(handler.calls[1], "sendChatMessage(Shout, \"yo\")");
    EXPECT_EQ(handler.calls[2], "sendChatMessage(Group, \"grp\")");
    EXPECT_EQ(handler.calls[3], "sendTell(\"Bob\", \"hey\")");
    EXPECT_EQ(handler.calls[4], "replyToLastTell(\"reply\")");
}

// T222 (deferred.md T100 entry): with no tell to answer, replyToLastTell()
// must FAIL with a user-visible message and never reach the handler --
// previously it returned Success() and the handler silently no-oped.
TEST_F(ActionDispatcherTest, ReplyToLastTell_NoPendingTell_FailsWithoutDispatching) {
    connectHandler();
    connectZone();
    handler.tellToReplyTo = false;
    auto result = dispatcher->replyToLastTell("hello?");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "You have no one to reply to.");
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, ReplyToLastTell_WithPendingTell_Dispatches) {
    connectHandler();
    connectZone();
    handler.tellToReplyTo = true;
    auto result = dispatcher->replyToLastTell("hello!");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("replyToLastTell(\"hello!\")"));
}

TEST_F(ActionDispatcherTest, GroupFamily_InviteAcceptDeclineLeave) {
    connectHandler();
    connectZone();

    EXPECT_FALSE(dispatcher->inviteToGroup("").success);
    EXPECT_TRUE(handler.empty());

    EXPECT_FALSE(dispatcher->acceptGroupInvite().success);  // no pending invite
    EXPECT_FALSE(dispatcher->declineGroupInvite().success);
    EXPECT_FALSE(dispatcher->leaveGroup().success);  // not in group
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, GroupFamily_CallLogExactSequence) {
    connectHandler();
    connectZone();

    dispatcher->inviteToGroup("Carol");
    dispatcher->inviteTarget();
    state.group().setPendingInvite("Dave");
    dispatcher->acceptGroupInvite();
    state.group().setPendingInvite("Dave");
    dispatcher->declineGroupInvite();
    state.group().setInGroup(true);
    dispatcher->leaveGroup();

    ASSERT_EQ(handler.count(), 5u);
    EXPECT_EQ(handler.calls[0], "inviteToGroup(\"Carol\")");
    EXPECT_EQ(handler.calls[1], "inviteTarget()");
    EXPECT_EQ(handler.calls[2], "acceptGroupInvite()");
    EXPECT_EQ(handler.calls[3], "declineGroupInvite()");
    EXPECT_EQ(handler.calls[4], "leaveGroup()");
}

TEST_F(ActionDispatcherTest, CharacterStateFamily_AfkAnon) {
    connectHandler();
    connectZone();

    // ActionDispatcher does not mutate GameState directly; toggleAFK() reads
    // current player state (default false) and passes the flipped value
    // through to the handler.
    ASSERT_FALSE(state.player().isAFK());

    dispatcher->setAFK(true);
    dispatcher->toggleAFK();  // reads current (still false) -> passes true
    dispatcher->setAnonymous(true);
    // T264: toggleSneak()/startCamp()/cancelCamp() deleted -- /sneak is
    // setMovementMode(2), /camp is camp() -> campNow(), and camp cancel
    // happens on movement inside EverQuest; none had a caller here.

    ASSERT_EQ(handler.count(), 3u);
    EXPECT_EQ(handler.calls[0], "setAFK(true)");
    EXPECT_EQ(handler.calls[1], "setAFK(true)");
    EXPECT_EQ(handler.calls[2], "setAnonymous(true)");
}

TEST_F(ActionDispatcherTest, InventoryFamily_Use) {
    connectHandler();
    connectZone();

    // T264: moveItem()/deleteItem() deleted -- both were LOG-only stubs with
    // no caller; the live item routes are InventoryManager's own intents and
    // /equip (equip()).
    dispatcher->useItem(4);

    ASSERT_EQ(handler.count(), 1u);
    EXPECT_EQ(handler.calls[0], "useItem(4)");
}

// T230: useItemById() is the keyboard hotbar's route. It resolves the ITEM
// id to an INVENTORY slot through the handler and only then calls
// useItem(slot) -- never useItem(itemId).
TEST_F(ActionDispatcherTest, UseItemById_ResolvesInventorySlotThenUsesIt) {
    connectHandler();
    connectZone();
    handler.inventoryByItemId[1234] = 24;  // item 1234 sits in general slot 24

    auto result = dispatcher->useItemById(1234);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(handler.calledOnceWith("useItem(24)"));
    EXPECT_EQ(handler.count(), 1u);
}

TEST_F(ActionDispatcherTest, UseItemById_ItemNotCarried_FailsWithMousePathMessageAndNoUse) {
    connectHandler();
    connectZone();
    // inventoryByItemId empty: nothing carried.

    auto result = dispatcher->useItemById(1234);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Item not found in inventory");
    EXPECT_TRUE(handler.empty());  // and crucially not useItem(1234)
}

TEST_F(ActionDispatcherTest, UseItemById_NotZoneConnected_FailsBeforeLookup) {
    connectHandler();
    handler.inventoryByItemId[1234] = 24;

    auto result = dispatcher->useItemById(1234);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(handler.empty());
}

// T239: the keyboard hotbar command route (Emote / Macro / Combat).
TEST_F(ActionDispatcherTest, ActivateHotbarCommandSlot_CommandTypesReachHandler) {
    connectHandler();
    connectZone();

    EXPECT_TRUE(dispatcher->activateHotbarCommandSlot(eqt::HotbarSlotType::Emote, "/wave").success);
    EXPECT_TRUE(dispatcher->activateHotbarCommandSlot(eqt::HotbarSlotType::Macro, "/say hi").success);
    EXPECT_TRUE(dispatcher->activateHotbarCommandSlot(eqt::HotbarSlotType::Combat, "").success);

    EXPECT_TRUE(handler.calledOnceWith("activateHotbarCommandSlot(Emote, \"/wave\")"));
    EXPECT_TRUE(handler.calledOnceWith("activateHotbarCommandSlot(Macro, \"/say hi\")"));
    EXPECT_TRUE(handler.calledOnceWith("activateHotbarCommandSlot(Combat, \"\")"));
    EXPECT_EQ(handler.count(), 3u);
}

TEST_F(ActionDispatcherTest, ActivateHotbarCommandSlot_EmptyTextFailsBeforeHandler) {
    connectHandler();
    connectZone();

    auto result = dispatcher->activateHotbarCommandSlot(eqt::HotbarSlotType::Emote, "");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Hotbar slot has no command");
    EXPECT_FALSE(dispatcher->activateHotbarCommandSlot(eqt::HotbarSlotType::Macro, "").success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, ActivateHotbarCommandSlot_NonCommandTypesRefused) {
    connectHandler();
    connectZone();

    for (auto t : {eqt::HotbarSlotType::Empty, eqt::HotbarSlotType::Spell,
                   eqt::HotbarSlotType::Skill, eqt::HotbarSlotType::Item}) {
        auto result = dispatcher->activateHotbarCommandSlot(t, "/wave");
        EXPECT_FALSE(result.success);
        EXPECT_EQ(result.message, "Not a command hotbar slot");
    }
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, ActivateHotbarCommandSlot_NotZoneConnectedFails) {
    connectHandler();

    EXPECT_FALSE(dispatcher->activateHotbarCommandSlot(eqt::HotbarSlotType::Emote, "/wave").success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, SpellbookFamily_GemBoundsAndRouting) {
    connectHandler();
    connectZone();

    // T264: memorizeSpell(gem, id), openSpellbook(), closeSpellbook() deleted
    // -- no caller; /mem is memorizeSpellByName() and the spellbook window
    // is a renderer popup.
    EXPECT_FALSE(dispatcher->forgetSpell(13).success);
    EXPECT_TRUE(handler.empty());

    dispatcher->forgetSpell(2);

    ASSERT_EQ(handler.count(), 1u);
    EXPECT_EQ(handler.calls[0], "forgetSpell(2)");
}

TEST_F(ActionDispatcherTest, TradeFamily_Routing) {
    connectHandler();
    connectZone();

    dispatcher->requestTrade(77);
    dispatcher->acceptTrade();
    dispatcher->cancelTrade();

    ASSERT_EQ(handler.count(), 3u);
    EXPECT_EQ(handler.calls[0], "requestTrade(77)");
    EXPECT_EQ(handler.calls[1], "acceptTrade()");
    EXPECT_EQ(handler.calls[2], "cancelTrade()");
}

// Pet family: 9 near-identical convenience wrappers around sendPetCommand
// with different hardcoded command IDs. This is exactly the shape the plan
// warns is prone to copy-paste mis-wiring, so every member gets its own
// assertion of the exact command ID routed through.
//
// T96: this test previously pinned the DRIFTED literals (petAttack sending
// 2 == PET_GETLOST, etc. -- see refactor/T96_pet_command_literal_drift.md).
// It now pins the corrected `EQT::PetCommand` enumerator each wrapper is
// supposed to send, so a future renumbering of pet_constants.h breaks this
// test instead of silently mis-driving a player's pet again.
TEST_F(ActionDispatcherTest, PetFamily_EachConvenienceMethodUsesItsOwnCommandId) {
    connectHandler();
    connectZone();
    state.pet().setPet(200, "Fido", 10);

    dispatcher->sendPetCommand(9, 0);   // arbitrary raw command
    dispatcher->petAttack();            // EQT::PET_ATTACK = 7
    dispatcher->petBackOff();           // EQT::PET_BACKOFF = 1
    dispatcher->petFollow();            // EQT::PET_FOLLOWME = 8
    dispatcher->petGuard();             // EQT::PET_GUARDHERE = 5
    dispatcher->petSit();               // EQT::PET_SITDOWN = 9
    dispatcher->petTaunt();             // EQT::PET_TAUNT = 11
    dispatcher->petHold();              // EQT::PET_HOLD = 12
    dispatcher->petFocus();             // EQT::PET_FOCUS = 19
    dispatcher->petHealth();            // EQT::PET_HEALTHREPORT = 4
    dispatcher->dismissPet();

    ASSERT_EQ(handler.count(), 11u);
    EXPECT_EQ(handler.calls[0], "sendPetCommand(9, 0)");
    EXPECT_EQ(handler.calls[1],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_ATTACK)) + ", 0)");
    EXPECT_EQ(handler.calls[2],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_BACKOFF)) + ", 0)");
    EXPECT_EQ(handler.calls[3],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_FOLLOWME)) + ", 0)");
    EXPECT_EQ(handler.calls[4],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_GUARDHERE)) + ", 0)");
    EXPECT_EQ(handler.calls[5],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_SITDOWN)) + ", 0)");
    EXPECT_EQ(handler.calls[6],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_TAUNT)) + ", 0)");
    EXPECT_EQ(handler.calls[7],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_HOLD)) + ", 0)");
    EXPECT_EQ(handler.calls[8],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_FOCUS)) + ", 0)");
    EXPECT_EQ(handler.calls[9],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_HEALTHREPORT)) + ", 0)");
    EXPECT_EQ(handler.calls[10], "dismissPet()");
}

// Explicit numeric pin, independent of the symbolic assertions above: if
// pet_constants.h's numbering ever silently drifts back (the exact failure
// mode this unit exists to close), these literal expectations catch it even
// if EQT::PetCommand itself were the thing that changed underneath a purely
// symbolic comparison.
TEST_F(ActionDispatcherTest, PetFamily_EachConvenienceMethodSendsTheDocumentedWireValue) {
    connectHandler();
    connectZone();
    state.pet().setPet(200, "Fido", 10);

    dispatcher->petAttack();
    dispatcher->petBackOff();
    dispatcher->petFollow();
    dispatcher->petGuard();
    dispatcher->petSit();
    dispatcher->petTaunt();
    dispatcher->petHold();
    dispatcher->petFocus();
    dispatcher->petHealth();

    ASSERT_EQ(handler.count(), 9u);
    EXPECT_EQ(handler.calls[0], "sendPetCommand(7, 0)");   // PET_ATTACK
    EXPECT_EQ(handler.calls[1], "sendPetCommand(1, 0)");   // PET_BACKOFF
    EXPECT_EQ(handler.calls[2], "sendPetCommand(8, 0)");   // PET_FOLLOWME
    EXPECT_EQ(handler.calls[3], "sendPetCommand(5, 0)");   // PET_GUARDHERE
    EXPECT_EQ(handler.calls[4], "sendPetCommand(9, 0)");   // PET_SITDOWN
    EXPECT_EQ(handler.calls[5], "sendPetCommand(11, 0)");  // PET_TAUNT
    EXPECT_EQ(handler.calls[6], "sendPetCommand(12, 0)");  // PET_HOLD
    EXPECT_EQ(handler.calls[7], "sendPetCommand(19, 0)");  // PET_FOCUS
    EXPECT_EQ(handler.calls[8], "sendPetCommand(4, 0)");   // PET_HEALTHREPORT
}

TEST_F(ActionDispatcherTest, PetFamily_NoPetFailsWithoutDispatch) {
    connectHandler();
    connectZone();
    // Default GameState has no pet.

    EXPECT_FALSE(dispatcher->petAttack().success);
    EXPECT_FALSE(dispatcher->petFollow().success);
    EXPECT_FALSE(dispatcher->dismissPet().success);
    EXPECT_FALSE(dispatcher->sendPetCommand(2).success);
    EXPECT_TRUE(handler.empty());
}

// T101: the five pet wrappers added to close the vocabulary gap T97 found
// (game's /pet has leader/spellhold/notaunt/nohold/standup; the T96-fixed
// wrapper set didn't). Same shape as the pre-existing pet family tests
// above -- one dispatcher call, one exact command id.
TEST_F(ActionDispatcherTest, PetFamily_T101AddedWrappersUseCorrectCommandId) {
    connectHandler();
    connectZone();
    state.pet().setPet(200, "Fido", 10);

    dispatcher->petStand();      // EQT::PET_STANDUP = 10
    dispatcher->petTauntOff();   // EQT::PET_TAUNT_OFF = 14
    dispatcher->petHoldOff();    // EQT::PET_HOLD_OFF = 21
    dispatcher->petLeader();     // EQT::PET_LEADER = 16
    dispatcher->petSpellHold();  // EQT::PET_SPELLHOLD = 18

    ASSERT_EQ(handler.count(), 5u);
    EXPECT_EQ(handler.calls[0],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_STANDUP)) + ", 0)");
    EXPECT_EQ(handler.calls[1],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_TAUNT_OFF)) + ", 0)");
    EXPECT_EQ(handler.calls[2],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_HOLD_OFF)) + ", 0)");
    EXPECT_EQ(handler.calls[3],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_LEADER)) + ", 0)");
    EXPECT_EQ(handler.calls[4],
              "sendPetCommand(" + std::to_string(static_cast<int>(EQT::PET_SPELLHOLD)) + ", 0)");
}

TEST_F(ActionDispatcherTest, PetFamily_T101AddedWrappers_NoPetFailsWithoutDispatch) {
    connectHandler();
    connectZone();
    // Default GameState has no pet.

    EXPECT_FALSE(dispatcher->petStand().success);
    EXPECT_FALSE(dispatcher->petTauntOff().success);
    EXPECT_FALSE(dispatcher->petHoldOff().success);
    EXPECT_FALSE(dispatcher->petLeader().success);
    EXPECT_FALSE(dispatcher->petSpellHold().success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, PetAttack_ExplicitTargetId_PassedThrough) {
    // T101: petAttack() gained an explicit-target overload (default still
    // 0, "server uses current target") to carry CommandProcessor's /pet
    // attack <name> resolution.
    connectHandler();
    connectZone();
    state.pet().setPet(200, "Fido", 10);

    dispatcher->petAttack(4321);

    ASSERT_TRUE(handler.calledOnceWith("sendPetCommand(7, 4321)"));
}

// T101: the real (SpellManager-backed) cast/mem/gems path, distinct from
// castSpell()/castSpellOnTarget() (CombatManager, untouched -- see
// SpellbookFamily_GemBoundsAndRouting above and T93's real-EqActionHandler
// coverage) and from memorizeSpell()/forgetSpell() (also CombatManager
// except forgetSpell(), which T101 repointed to SpellManager underneath
// without changing its dispatcher signature/bounds shape below).
TEST_F(ActionDispatcherTest, RealSpellFamily_GemBoundsAndRouting) {
    connectHandler();
    connectZone();

    EXPECT_FALSE(dispatcher->beginCastFromGem(0, 0).success);
    EXPECT_FALSE(dispatcher->beginCastFromGem(9, 0).success);
    EXPECT_FALSE(dispatcher->memorizeSpellByName("", 1).success);
    EXPECT_FALSE(dispatcher->memorizeSpellByName("Root", 0).success);
    EXPECT_FALSE(dispatcher->memorizeSpellByName("Root", 9).success);
    EXPECT_TRUE(handler.empty());

    dispatcher->beginCastFromGem(3, 777);
    dispatcher->memorizeSpellByName("Minor Healing", 2);
    dispatcher->listMemorizedSpells();

    ASSERT_EQ(handler.count(), 3u);
    EXPECT_EQ(handler.calls[0], "beginCastFromGem(3, 777)");
    EXPECT_EQ(handler.calls[1], "memorizeSpellByName(\"Minor Healing\", 2)");
    EXPECT_EQ(handler.calls[2], "listMemorizedSpells()");
}

TEST_F(ActionDispatcherTest, RealSpellFamily_NoHandlerFailsWithoutDispatch) {
    // Handler intentionally not set.
    EXPECT_FALSE(dispatcher->beginCastFromGem(1, 0).success);
    EXPECT_FALSE(dispatcher->memorizeSpellByName("Root", 1).success);
    EXPECT_FALSE(dispatcher->listMemorizedSpells().success);
    EXPECT_TRUE(handler.empty());
}

TEST_F(ActionDispatcherTest, ListMemorizedSpells_HandlerOnlyNoZoneRequired) {
    // T101: matches game's /gems, which only required a spell manager, not
    // a zone connection -- checkHandler() only, not checkZoneConnection().
    connectHandler();
    ASSERT_FALSE(state.world().isZoneConnected());

    EXPECT_TRUE(dispatcher->listMemorizedSpells().success);
    ASSERT_TRUE(handler.calledOnceWith("listMemorizedSpells()"));
}

TEST_F(ActionDispatcherTest, ForgetSpell_NarrowedBoundIsOneToEight) {
    // T101: forgetSpell()'s bound narrowed from 1-12 to 1-8 (SpellManager
    // only has 8 gems) when its implementation was repointed off the stub.
    connectHandler();
    connectZone();

    EXPECT_TRUE(dispatcher->forgetSpell(1).success);
    EXPECT_TRUE(dispatcher->forgetSpell(8).success);
    EXPECT_FALSE(dispatcher->forgetSpell(9).success);
    EXPECT_FALSE(dispatcher->forgetSpell(0).success);

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "forgetSpell(1)");
    EXPECT_EQ(handler.calls[1], "forgetSpell(8)");
}

TEST_F(ActionDispatcherTest, TradeskillFamily_Routing) {
    connectHandler();
    connectZone();

    dispatcher->clickWorldObject(123);
    // T264: tradeskillCombine() deleted (a LOG-only stub with no caller;
    // manual combine is a listed blocked feature, see the T264 Review).

    ASSERT_EQ(handler.count(), 1u);
    EXPECT_EQ(handler.calls[0], "clickWorldObject(123)");
}

TEST_F(ActionDispatcherTest, ExtendedMovementFamily_ModeAndPositionState) {
    connectHandler();
    connectZone();

    dispatcher->setMovementMode(1);
    dispatcher->setPositionState(3);

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "setMovementMode(1)");
    EXPECT_EQ(handler.calls[1], "setPositionState(3)");
}

TEST_F(ActionDispatcherTest, ExtendedCharacterStateFamily_RoleplayAndPathfinding) {
    connectHandler();
    // Roleplay requires zone; pathfinding does not (checkHandler only).
    dispatcher->setRoleplay(true);
    EXPECT_TRUE(handler.empty());  // no zone yet -> fails
    connectZone();
    dispatcher->setRoleplay(true);
    dispatcher->setPathfinding(true);

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "setRoleplay(true)");
    EXPECT_EQ(handler.calls[1], "setPathfinding(true)");
}

TEST_F(ActionDispatcherTest, ExtendedCombatFamily_HuntingLootTargets) {
    connectHandler();
    connectZone();

    dispatcher->setAutoHunting(true);
    dispatcher->setAutoLoot(false);
    dispatcher->listHuntTargets();

    ASSERT_EQ(handler.count(), 3u);
    EXPECT_EQ(handler.calls[0], "setAutoHunting(true)");
    EXPECT_EQ(handler.calls[1], "setAutoLoot(false)");
    EXPECT_EQ(handler.calls[2], "listHuntTargets()");
}

TEST_F(ActionDispatcherTest, EntityQueryFamily_ListAndDump) {
    connectHandler();
    connectZone();

    dispatcher->listEntities("npc");
    dispatcher->dumpEntityAppearance("Bob");

    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "listEntities(\"npc\")");
    EXPECT_EQ(handler.calls[1], "dumpEntityAppearance(\"Bob\")");
}

TEST_F(ActionDispatcherTest, UtilityFamily_Animation) {
    connectHandler();
    connectZone();

    dispatcher->sendAnimation(29, 15);
    // T264: sendPositionUpdate() deleted (no caller).

    ASSERT_EQ(handler.count(), 1u);
    EXPECT_EQ(handler.calls[0], "sendAnimation(29, 15)");
}

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

} // namespace
