// test_action_dispatcher_routing.cpp -- section 1 of the original, first
// half: movement, heading and facing, warp, move-to/follow, targeting,
// auto-attack, cast-from-gem, door and corpse interaction, chat, group,
// character state, inventory use and the hotbar command slot.
//
// Split out of tests/test_action_dispatcher.cpp by T453 (contiguous range
// 38..583 of the 1,217-line original, moved verbatim). 30 cases. The fixture
// is in tests/action_dispatcher_test_fixture.h.
#include "action_dispatcher_test_fixture.h"

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
