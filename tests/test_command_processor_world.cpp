// CommandProcessor tests -- world interaction and the tail of the
// game-registry migration: /clickobject (T144), the split/save/bandage/
// rewind/bug/petition group, the vendor family, and the final migration
// rows (T136, T143).
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

// ========== /clickobject (T144) ==========
// The dispatcher/handler chain for clickWorldObject already existed and was
// fully plumbed; it had no production caller at all, because the only trigger
// was a renderer keypress. T144 registers the command so the tradeskill
// container path is reachable from any command channel (and so the unit's
// "a forge actually opens" acceptance leg could be driven at all).

TEST_F(CommandProcessorTest, T144_ClickObject_Registered) {
    auto info = processor->getCommandInfo("clickobject");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->category, "Utility");
    EXPECT_TRUE(processor->hasCommand("clickobj"));
}

TEST_F(CommandProcessorTest, T144_ClickObject_DispatchesWithDropId) {
    auto result = processor->processCommand("/clickobject 190");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("clickWorldObject(190)"));
}

TEST_F(CommandProcessorTest, T144_ClickObject_Alias_DispatchesWithDropId) {
    auto result = processor->processCommand("/clickobj 4294967295");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("clickWorldObject(4294967295)"));
}

TEST_F(CommandProcessorTest, T144_ClickObject_RejectsMissingOrJunkArgsWithoutDispatching) {
    for (const char* bad : {"/clickobject", "/clickobject abc", "/clickobject 12abc",
                            "/clickobject 4294967296"}) {
        handler.calls.clear();
        auto result = processor->processCommand(bad);
        EXPECT_FALSE(result.success) << "should have failed: " << bad;
        EXPECT_EQ(handler.count(), 0u) << "should not have dispatched: " << bad;
    }
}

TEST_F(CommandProcessorTest, T144_ClickObject_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/clickobject 190");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ========== Utility (T135) ==========
// split/save/bandage/rewind/bug/petition do not share a scenario (unlike
// Guild or Corpse-Rez) -- six independent groups of assertions, per the
// plan's explicit instruction not to force one live scenario onto them.

TEST_F(CommandProcessorTest, T135_UtilityCommandsRegisteredWithPortedCategories) {
    struct Expect { const char* name; const char* category; };
    for (const auto& e : {
        Expect{"split", "Group"}, Expect{"save", "Utility"},
        Expect{"bandage", "Skills"}, Expect{"rewind", "Movement"},
        Expect{"bug", "Utility"}, Expect{"petition", "Utility"},
    }) {
        auto info = processor->getCommandInfo(e.name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << e.name;
        EXPECT_EQ(info->category, e.category) << "wrong category for: " << e.name;
    }
    // Aliases ported verbatim from eq_commands.cpp.
    EXPECT_TRUE(processor->hasCommand("bindwound"));
    EXPECT_TRUE(processor->hasCommand("report"));
}

// ---------- split ----------

TEST_F(CommandProcessorTest, Split_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/split");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Split_PlatinumOnly_DefaultsRestToZero) {
    auto result = processor->processCommand("/split 10");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("split(10, 0, 0, 0)"));
}

TEST_F(CommandProcessorTest, Split_AllFourDenominations_DispatchesSplit) {
    auto result = processor->processCommand("/split 1 2 3 4");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("split(1, 2, 3, 4)"));
}

TEST_F(CommandProcessorTest, Split_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/split 10");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- save ----------

TEST_F(CommandProcessorTest, Save_DispatchesSave) {
    auto result = processor->processCommand("/save");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("save()"));
}

TEST_F(CommandProcessorTest, Save_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/save");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- bandage ----------
// No usage guard (game's bindwound_cmd took no args): both the primary name
// and the bindwound alias always dispatch bandage(0), letting
// EqActionHandler resolve current-target-or-self.

TEST_F(CommandProcessorTest, Bandage_DispatchesBandageWithZeroTargetId) {
    auto result = processor->processCommand("/bandage");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("bandage(0)"));
}

TEST_F(CommandProcessorTest, Bandage_Alias_DispatchesBandageWithZeroTargetId) {
    auto result = processor->processCommand("/bindwound");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("bandage(0)"));
}

TEST_F(CommandProcessorTest, Bandage_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/bandage");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- rewind ----------

TEST_F(CommandProcessorTest, Rewind_DispatchesRewind) {
    auto result = processor->processCommand("/rewind");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("rewind()"));
}

TEST_F(CommandProcessorTest, Rewind_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/rewind");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- bug ----------

TEST_F(CommandProcessorTest, Bug_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/bug");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Bug_WithDescription_DispatchesBug) {
    auto result = processor->processCommand("/bug the door is stuck");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("bug(\"the door is stuck\")"));
}

TEST_F(CommandProcessorTest, Bug_Alias_DispatchesBug) {
    auto result = processor->processCommand("/report the door is stuck");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("bug(\"the door is stuck\")"));
}

TEST_F(CommandProcessorTest, Bug_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/bug the door is stuck");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ---------- petition ----------

TEST_F(CommandProcessorTest, Petition_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/petition");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Petition_WithText_DispatchesPetition) {
    auto result = processor->processCommand("/petition stuck in geometry");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("petition(\"stuck in geometry\")"));
}

TEST_F(CommandProcessorTest, Petition_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/petition stuck in geometry");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ========== Merchant (T136) ==========
// vendor/vendorlist/buy/sell -- 4 of the 5-command family (equip stays in
// eq_commands.cpp, see refactor/T136_migrate_merchant_family.md's Review).
// Unlike every prior family, the "You are not at a vendor."/"no target"
// stateful guards live inside the real EverQuest (RequestVendorToggle/
// RequestVendorBuy/RequestVendorSell, EqActionHandler::vendorList()), not
// here -- RecordingActionHandler is a fake with no vendor state, so those
// guard paths are covered instead in test_eq_action_handler_vendor.cpp
// (real EverQuest, real wire-replayed vendor state). These tests cover only
// what belongs at this layer: registration/category, dispatch reaching the
// right ActionDispatcher method, and cmdBuy/cmdSell's own numeric-token
// parsing (a formatting concern ported from the old buy_cmd/sell_cmd
// istringstream parsing).

TEST_F(CommandProcessorTest, T136_MerchantCommandsRegisteredWithCommerceCategory) {
    for (const char* name : {"vendor", "vendorlist", "buy", "sell"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << name;
        EXPECT_EQ(info->category, "Commerce") << "wrong category for: " << name;
    }
}

TEST_F(CommandProcessorTest, Vendor_DispatchesVendorToggle) {
    auto result = processor->processCommand("/vendor");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("vendorToggle()"));
}

TEST_F(CommandProcessorTest, Vendor_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/vendor");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, VendorList_DispatchesVendorList) {
    auto result = processor->processCommand("/vendorlist");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("vendorList()"));
}

TEST_F(CommandProcessorTest, VendorList_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/vendorlist");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Buy_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/buy");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Usage:"), std::string::npos);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Buy_MalformedSlot_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/buy notanumber");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Invalid slot"), std::string::npos);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Buy_TrailingGarbageSlot_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/buy 5abc");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Invalid slot"), std::string::npos);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Buy_MalformedQuantity_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/buy 5 zero");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Invalid quantity"), std::string::npos);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Buy_ZeroQuantity_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/buy 5 0");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Invalid quantity"), std::string::npos);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Buy_ValidSlotNoQuantity_DispatchesWithDefaultQuantityOne) {
    auto result = processor->processCommand("/buy 5");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("buyFromVendor(5, 1)"));
}

TEST_F(CommandProcessorTest, Buy_ValidSlotAndQuantity_DispatchesExactValues) {
    auto result = processor->processCommand("/buy 5 3");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("buyFromVendor(5, 3)"));
}

TEST_F(CommandProcessorTest, Buy_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/buy 5");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Sell_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/sell");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Usage:"), std::string::npos);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Sell_MalformedSlot_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/sell notaslot");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Invalid slot"), std::string::npos) << result.message;
    EXPECT_EQ(handler.count(), 0u);
}

// T494 (board B796): -1 parses as a number but is no `inventory.slot_id`, so
// it gets the "this client is not sent that slot" refusal, not the usage one.
TEST_F(CommandProcessorTest, Sell_NumberThatIsNoSlot_FailsWithTheUnaddressableRefusal) {
    auto result = processor->processCommand("/sell -1");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("does not send"), std::string::npos) << result.message;
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Sell_ValidSlotNoQuantity_DispatchesWithDefaultQuantityOne) {
    auto result = processor->processCommand("/sell 23");
    EXPECT_TRUE(result.success);
    // T494: /sell takes `inventory.slot_id`; 23 is model slot 22, which is
    // what the dispatcher (and the wire) must see.
    ASSERT_TRUE(handler.calledOnceWith("sellToVendor(22, 1)"));
}

TEST_F(CommandProcessorTest, Sell_ValidSlotAndQuantity_DispatchesExactValues) {
    auto result = processor->processCommand("/sell 23 4");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("sellToVendor(22, 4)"));
}

TEST_F(CommandProcessorTest, Sell_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/sell 23");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ========== Equip (T141) ==========
// The last row of the game-registry migration programme -- see
// refactor/T141_equip_extraction.md. Raw arg string passed straight through
// (same idiom as warp() above): all three input-form parsing (slot-pair/
// item-name/list) and all legality/execution now live in InventoryManager,
// reached via EverQuest::RequestEquip() -- covered directly (real
// EverQuest, real InventoryManager) in test_eq_commands_equip_*.cpp. What
// belongs at THIS layer: registration/category and dispatch reaching the
// right ActionDispatcher method with the args forwarded verbatim.

TEST_F(CommandProcessorTest, T141_EquipRegisteredWithInventoryCategory) {
    auto info = processor->getCommandInfo("equip");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->category, "Inventory");
}

TEST_F(CommandProcessorTest, Equip_RawArgsPassedThrough) {
    auto result = processor->processCommand("/equip 23 13");
    EXPECT_TRUE(result.success);
    // The args string is passed through UNTOUCHED: cmdEquip does no parsing,
    // so the user's `inventory.slot_id` text arrives verbatim (T494).
    ASSERT_TRUE(handler.calledOnceWith("equip(\"23 13\")"));
}

TEST_F(CommandProcessorTest, Equip_ItemNameFormPassedThroughVerbatim) {
    auto result = processor->processCommand("/equip Rusty Mace");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("equip(\"Rusty Mace\")"));
}

TEST_F(CommandProcessorTest, Equip_NoArgs_StillDispatches_GuardLivesInEverQuest) {
    // No requiresArgs guard here -- /equip with no args is the "print
    // usage" form, which RequestEquip() itself handles (matching warp's
    // convention: the "what does an empty args string mean" decision is
    // command-specific and lives with the command, not the dispatcher).
    auto result = processor->processCommand("/equip");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("equip(\"\")"));
}

TEST_F(CommandProcessorTest, Equip_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/equip 23 13");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ========== Movement / Utility / Skills (T140) ==========
// Final 16 of the game-registry migration programme (equip excluded --
// see refactor/T140_migrate_final_seventeen.md). Stateful guards
// (warp/timeOfDay's parsing, bank/train's target validation) live inside
// real EverQuest (RequestWarp/RequestTimeOfDay/RequestBankToggle/
// RequestTraining) or EqActionHandler (showLocation/showOrSetZoning/
// campNow/debugLevel/perfReport) -- covered instead in
// test_eq_action_handler_movement_utility.cpp and
// test_eq_commands_bank_train.cpp (real EverQuest). What belongs at this
// layer: registration/category, dispatch reaching the right
// ActionDispatcher method with the right (possibly reformatted) message,
// and the requiresArgs usage-guard for move/moveto/follow.

TEST_F(CommandProcessorTest, T140_MovementCommandsRegisteredWithMovementCategory) {
    for (const char* name : {"loc", "sit", "stand", "move", "moveto", "warp", "zoning", "follow", "stopfollow"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << name;
        EXPECT_EQ(info->category, "Movement") << "wrong category for: " << name;
    }
}

TEST_F(CommandProcessorTest, T140_UtilityCommandsRegisteredWithUtilityCategory) {
    for (const char* name : {"quit", "camp", "debug", "perf", "time", "bank"}) {
        auto info = processor->getCommandInfo(name);
        ASSERT_TRUE(info.has_value()) << "missing built-in: " << name;
        EXPECT_EQ(info->category, "Utility") << "wrong category for: " << name;
    }
}

TEST_F(CommandProcessorTest, T140_TrainRegisteredWithSkillsCategory) {
    auto info = processor->getCommandInfo("train");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->category, "Skills");
}

TEST_F(CommandProcessorTest, Loc_DispatchesShowLocation) {
    auto result = processor->processCommand("/loc");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("showLocation()"));
}

TEST_F(CommandProcessorTest, Sit_DispatchesSit_WithOriginalAckText) {
    auto result = processor->processCommand("/sit");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "You sit down.");
    ASSERT_TRUE(handler.calledOnceWith("sit()"));
}

TEST_F(CommandProcessorTest, Stand_DispatchesStand_WithOriginalAckText) {
    auto result = processor->processCommand("/stand");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "You stand up.");
    ASSERT_TRUE(handler.calledOnceWith("stand()"));
}

// T88: dual-frame echo is load-bearing -- see cmdMove's comment.
TEST_F(CommandProcessorTest, Move_ValidArgs_DispatchesMoveToLocation_WithDualFrameEcho) {
    auto result = processor->processCommand("/move 1 2 3");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("moveToLocation(1, 2, 3)"));
    EXPECT_NE(result.message.find("client=(1.0, 2.0, 3.0)"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("server=(2.0, 1.0, 3.0)"), std::string::npos) << result.message;
}

// requiresArgs=true on the registration handles the empty-args case
// generically (processCommand() itself refuses before reaching cmdMove,
// with its own "Missing arguments" ActionResult -- the "Usage: ..." text,
// byte-identical to game's original, goes to displayError()/console, not
// into ActionResult::message).
TEST_F(CommandProcessorTest, Move_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/move");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Move_MalformedArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/move x y z");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, MoveTo_DispatchesMoveToEntity_WithOriginalAckWording) {
    eqt::state::Entity e;
    e.spawnId = 42;
    e.name = "a_goblin";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/moveto a_goblin");
    EXPECT_TRUE(result.success);
    // T356: the handler receives the RESOLVED spawn id, never the name.
    ASSERT_TRUE(handler.calledOnceWith("moveToEntity(42)"));
    EXPECT_EQ(result.message, "Moving to entity: a_goblin");
}

TEST_F(CommandProcessorTest, MoveTo_UnknownEntity_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/moveto no_such_entity");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, MoveTo_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/moveto");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Warp_RawArgsPassedThrough) {
    auto result = processor->processCommand("/warp 10 20 30");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("warp(\"10 20 30\")"));
}

TEST_F(CommandProcessorTest, Warp_NoArgs_StillDispatches_GuardLivesInEverQuest) {
    auto result = processor->processCommand("/warp");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("warp(\"\")"));
}

TEST_F(CommandProcessorTest, Warp_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/warp 10 20 30");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Zoning_RawArgsPassedThrough) {
    auto result = processor->processCommand("/zoning on");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("showOrSetZoning(\"on\")"));
}

TEST_F(CommandProcessorTest, Follow_DispatchesFollowEntity_WithOriginalAckWording) {
    eqt::state::Entity e;
    e.spawnId = 43;
    e.name = "a_ranger";
    state.entities().addEntity(e);

    auto result = processor->processCommand("/follow a_ranger");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("followEntity(43)"));
    EXPECT_EQ(result.message, "Following: a_ranger");
}

TEST_F(CommandProcessorTest, Follow_UnknownEntity_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/follow no_such_entity");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Follow_NoArgs_FailsCleanlyWithoutDispatching) {
    auto result = processor->processCommand("/follow");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, StopFollow_DispatchesStopFollow_MessageAlreadyMatchesOriginal) {
    auto result = processor->processCommand("/stopfollow");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "Stopped following");
    ASSERT_TRUE(handler.calledOnceWith("stopFollow()"));
}

TEST_F(CommandProcessorTest, StopFollow_StopAlias_DispatchesSameAsPrimary) {
    auto result = processor->processCommand("/stop");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("stopFollow()"));
}

// T30: quit must work whether connected or not.
TEST_F(CommandProcessorTest, Quit_DispatchesRequestQuit_EvenWhenNotConnectedToZone) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/quit");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("requestQuit()"));
}

TEST_F(CommandProcessorTest, Quit_ExitAndQAliases_DispatchSameAsPrimary) {
    auto r1 = processor->processCommand("/exit");
    auto r2 = processor->processCommand("/q");
    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);
    ASSERT_EQ(handler.count(), 2u);
    EXPECT_EQ(handler.calls[0], "requestQuit()");
    EXPECT_EQ(handler.calls[1], "requestQuit()");
}

TEST_F(CommandProcessorTest, Camp_DispatchesCampNow) {
    auto result = processor->processCommand("/camp");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("campNow()"));
}

TEST_F(CommandProcessorTest, Camp_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/camp");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(CommandProcessorTest, Debug_RawArgsPassedThrough) {
    auto result = processor->processCommand("/debug 3");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("debugLevel(\"3\")"));
}

TEST_F(CommandProcessorTest, Perf_DispatchesPerfReport) {
    auto result = processor->processCommand("/perf");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("perfReport()"));
}

TEST_F(CommandProcessorTest, Perf_AliasesDispatchSameAsPrimary) {
    auto r1 = processor->processCommand("/performance");
    auto r2 = processor->processCommand("/metrics");
    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);
    ASSERT_EQ(handler.count(), 2u);
}

TEST_F(CommandProcessorTest, Time_RawArgsPassedThrough) {
    auto result = processor->processCommand("/time 14");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("timeOfDay(\"14\")"));
}

TEST_F(CommandProcessorTest, Time_SettimeAlias_DispatchesSameAsPrimary) {
    auto result = processor->processCommand("/settime 14");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("timeOfDay(\"14\")"));
}

TEST_F(CommandProcessorTest, Bank_DispatchesBankToggle) {
    auto result = processor->processCommand("/bank");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("bankToggle()"));
}

TEST_F(CommandProcessorTest, Bank_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/bank");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

// ========== /bank deposit|withdraw|convert (T269) ==========
// What belongs at this layer: the sub-command parse, the denomination
// scale (EQT::COINTYPE_*: cp=0 sp=1 gp=2 pp=3), and the LOCAL pool check
// against GameState (player().platinum().. / bankPlatinum()..) -- a refused
// command dispatches nothing at all. The intent push and the OP_MoveCoin
// bytes are pinned on the real EverQuest in test_eq_bank_coins.cpp.

class BankCoinCommandTest : public CommandProcessorTest {
protected:
    void SetUp() override {
        CommandProcessorTest::SetUp();
        state.player().setBanker(200);
        state.player().setCurrency(10, 1, 2, 3);
        state.player().setBankCurrency(4, 5, 6, 30);
    }
};

TEST_F(BankCoinCommandTest, Deposit_DispatchesBankCoinMoveToBank) {
    auto result = processor->processCommand("/bank deposit 5 pp");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("bankCoinMove(3, 5, false)"));
}

TEST_F(BankCoinCommandTest, Deposit_LongDenominationNames_SameScale) {
    EXPECT_TRUE(processor->processCommand("/bank deposit 1 gold").success);
    EXPECT_TRUE(processor->processCommand("/bank deposit 2 SILVER").success);
    EXPECT_TRUE(processor->processCommand("/bank deposit 3 copper").success);
    EXPECT_TRUE(processor->processCommand("/bank deposit 10 platinum").success);
    ASSERT_EQ(handler.count(), 4u);
    EXPECT_EQ(handler.calls[0], "bankCoinMove(2, 1, false)");
    EXPECT_EQ(handler.calls[1], "bankCoinMove(1, 2, false)");
    EXPECT_EQ(handler.calls[2], "bankCoinMove(0, 3, false)");
    EXPECT_EQ(handler.calls[3], "bankCoinMove(3, 10, false)");
}

TEST_F(BankCoinCommandTest, Withdraw_DispatchesBankCoinMoveFromBank) {
    auto result = processor->processCommand("/bank withdraw 30 cp");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("bankCoinMove(0, 30, true)"));
}

TEST_F(BankCoinCommandTest, Deposit_PoolLacksCoins_RefusedNothingDispatched) {
    auto result = processor->processCommand("/bank deposit 11 pp");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("platinum"), std::string::npos) << result.message;
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, Withdraw_PoolLacksCoins_RefusedNothingDispatched) {
    auto result = processor->processCommand("/bank withdraw 7 sp");  // bank has 6
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("silver"), std::string::npos) << result.message;
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, Deposit_ExactPool_Allowed) {
    EXPECT_TRUE(processor->processCommand("/bank deposit 10 pp").success);
    ASSERT_TRUE(handler.calledOnceWith("bankCoinMove(3, 10, false)"));
}

TEST_F(BankCoinCommandTest, Deposit_BankWindowClosed_RefusedNothingDispatched) {
    state.player().clearBanker();
    auto result = processor->processCommand("/bank deposit 5 pp");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("bank"), std::string::npos) << result.message;
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, Deposit_BadAmountOrDenomination_UsageNothingDispatched) {
    for (const char* cmd : {"/bank deposit", "/bank deposit 5", "/bank deposit x pp",
                            "/bank deposit 0 pp", "/bank deposit -3 pp", "/bank deposit 5 zz",
                            "/bank deposit 99999999999 pp", "/bank withdraw 5"}) {
        auto result = processor->processCommand(cmd);
        EXPECT_FALSE(result.success) << cmd;
        EXPECT_NE(result.message.find("Usage"), std::string::npos) << cmd << " -> " << result.message;
    }
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, UnknownSubcommand_UsageNothingDispatched) {
    auto result = processor->processCommand("/bank frobnicate");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Usage"), std::string::npos) << result.message;
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, Convert_Up_DispatchesBankCoinConvert) {
    auto result = processor->processCommand("/bank convert 30 cp sp");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("bankCoinConvert(0, 1, 30)"));
}

TEST_F(BankCoinCommandTest, Convert_Down_DispatchesBankCoinConvert) {
    auto result = processor->processCommand("/bank convert 2 pp gp");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("bankCoinConvert(3, 2, 2)"));
}

TEST_F(BankCoinCommandTest, Convert_UpNotAMultiple_RefusedNothingDispatched) {
    auto result = processor->processCommand("/bank convert 15 cp sp");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("multiple"), std::string::npos) << result.message;
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, Convert_TwoStepsNeedsHundred) {
    EXPECT_FALSE(processor->processCommand("/bank convert 30 cp gp").success);
    EXPECT_EQ(handler.count(), 0u);
    state.player().setBankCurrency(0, 0, 0, 200);
    EXPECT_TRUE(processor->processCommand("/bank convert 200 cp gp").success);
    ASSERT_TRUE(handler.calledOnceWith("bankCoinConvert(0, 2, 200)"));
}

TEST_F(BankCoinCommandTest, Convert_PoolLacksCoins_RefusedNothingDispatched) {
    auto result = processor->processCommand("/bank convert 40 cp sp");  // bank has 30
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, Convert_SameDenomination_RefusedNothingDispatched) {
    auto result = processor->processCommand("/bank convert 10 cp cp");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, Convert_BankWindowClosed_RefusedNothingDispatched) {
    state.player().clearBanker();
    auto result = processor->processCommand("/bank convert 30 cp sp");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

TEST_F(BankCoinCommandTest, NoArgs_StillTogglesTheWindow) {
    auto result = processor->processCommand("/bank");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("bankToggle()"));
}

TEST_F(CommandProcessorTest, Train_DispatchesTrainWithTarget) {
    auto result = processor->processCommand("/train");
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(handler.calledOnceWith("trainWithTarget()"));
}

TEST_F(CommandProcessorTest, Train_NotConnectedToZone_FailsCleanlyWithoutDispatching) {
    state.world().setZoneConnected(false);
    auto result = processor->processCommand("/train");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(handler.count(), 0u);
}

}  // namespace
