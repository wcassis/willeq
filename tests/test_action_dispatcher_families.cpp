// test_action_dispatcher_families.cpp -- section 1 of the original, second
// half: spellbook, trade, the pet command family (including the documented
// wire values), the real spell family, tradeskill, the extended movement,
// character-state and combat families, entity queries and animation.
//
// Split out of tests/test_action_dispatcher.cpp by T453 (contiguous range
// 585..897 of the 1,217-line original, moved verbatim). 18 cases. The fixture
// is in tests/action_dispatcher_test_fixture.h.
#include "action_dispatcher_test_fixture.h"

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
