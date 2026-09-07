// K12 (Batch K, refactor/K12_commands_component.md) --
// eqt::commands::CommandsComponent (commands + combat_actions).
//
// K12 dropped forwarders entirely, following K01's ruling: EverQuest no
// longer declares a single Request*() sender, ZoneProcess* combat handler,
// or the commands/*.cpp registration methods -- every caller goes through
// EverQuest::Commands(). This file covers the component itself --
// construction against a real EverQuest, the ClientContext binding, that
// RegisterCommands() (called from EverQuest's ctor) actually populated the
// registry, the moved m_vendor_items state, and IsStunned()'s idle state.
// The Request*()/ZoneProcess*() bodies themselves keep their existing
// per-feature coverage (test_eq_commands_equip_*.cpp,
// test_eq_commands_bank_train.cpp, test_train_skill.cpp,
// test_tradeskill_combine.cpp, test_scribe_scroll.cpp, test_read_book.cpp,
// test_eq_action_handler_cast.cpp's stun cases, ...); only their call sites
// were retargeted to eq.Commands().

#include <gtest/gtest.h>

#include "client/commands/commands_component.h"
#include "client/graphics/ui/command_registry.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class CommandsComponentTest : public ::testing::Test {
protected:
    CommandsComponentTest() : eq(MakeEq()) {}
    EverQuest eq;
};

// ===================================================================
// 1. It constructs against a real EverQuest in headless mode
// ===================================================================

TEST_F(CommandsComponentTest, ComponentExistsAndIsStable) {
    // Commands() must hand back the SAME component every time, not a copy.
    EXPECT_EQ(&eq.Commands(), &eq.Commands());
}

TEST_F(CommandsComponentTest, ContextPointsAtTheOwningEverQuestsOwnMembers) {
    EXPECT_EQ(&eq.Context().game_state, &eq.GetGameState());
    EXPECT_EQ(eq.Context().character, eq.GetMyName());
    EXPECT_EQ(eq.Context().character, "Testchar");
}

// ===================================================================
// 2. RegisterCommands() ran from EverQuest's ctor (eq.cpp calls
//    m_commands->RegisterCommands() right after allocating the registry) --
//    prove the registry it populated is reachable and non-empty through
//    Commands().
// ===================================================================

TEST_F(CommandsComponentTest, RegisterCommandsRanAtConstructionAndPopulatedTheRegistry) {
    const eqt::ui::CommandRegistry* reg = eq.Commands().GetCommandRegistry();
    ASSERT_NE(reg, nullptr);
    // /pmem is registered unconditionally by RegisterSpellCommands()
    // (commands_spells.cpp) regardless of EQT_HAS_GRAPHICS -- only its
    // handler body is guarded.
    EXPECT_NE(reg->findCommand("pmem"), nullptr);
}

TEST_F(CommandsComponentTest, GetCommandRegistryReturnsTheSameEveryTime) {
    EXPECT_EQ(eq.Commands().GetCommandRegistry(), eq.Commands().GetCommandRegistry());
}

// ===================================================================
// 3. IsStunned() -- T247. Idle state only here; the OP_Stun wire behaviour
//    (ZoneProcessStun via ReplayZonePacket, the expiry clock, the
//    already-stunned/no-message-repeat rule) is covered in
//    test_eq_action_handler_cast.cpp, which reaches this same component
//    through eq.Commands().IsStunned().
// ===================================================================

TEST_F(CommandsComponentTest, FreshClientIsNotStunned) {
    EXPECT_FALSE(eq.Commands().IsStunned());
}

// ===================================================================
// 4. m_vendor_items -- moved here from EverQuest (K05 left it, K12 was the
//    last family reading it). VendorItems() is the same non-const
//    reference-accessor shape as K09's ZoneComponent::MovementHistory(),
//    used by both this component (RequestVendorBuy) and MerchantComponent
//    (merchant_vendor_bank.cpp/merchant_trade.cpp) across the family
//    boundary.
// ===================================================================

TEST_F(CommandsComponentTest, VendorItemsStartsEmpty) {
    EXPECT_TRUE(eq.Commands().VendorItems().empty());
}

TEST_F(CommandsComponentTest, VendorItemsRoundTripsThroughTheAccessor) {
    eqt::commands::CommandsComponent::GameVendorItem item;
    item.itemId = 1001;
    item.name = "Rusty Sword";
    item.price = 250;
    item.quantity = 3;
    eq.Commands().VendorItems()[7] = item;

    ASSERT_EQ(eq.Commands().VendorItems().size(), 1u);
    const auto& got = eq.Commands().VendorItems().at(7);
    EXPECT_EQ(got.itemId, 1001u);
    EXPECT_EQ(got.name, "Rusty Sword");
    EXPECT_EQ(got.price, 250);
    EXPECT_EQ(got.quantity, 3);
}

}  // namespace
