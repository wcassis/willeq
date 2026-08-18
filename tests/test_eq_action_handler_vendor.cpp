// T136 -- real-EqActionHandler/EverQuest coverage for vendor/vendorlist/buy/
// sell after their migration to action::CommandProcessor. Adapted from T48's
// test_eq_commands_vendor.cpp (deleted in this unit -- its target,
// eq_commands.cpp's RegisterVendorCommands(), no longer exists), retargeted
// from `eq.ProcessChatInput("/vendor")` (the old game-registry entry point)
// to the new entry points these commands actually reach through:
//   - vendor/buy/sell: EverQuest::RequestVendorToggle()/RequestVendorBuy()/
//     RequestVendorSell() directly -- the new "senders" T136 added, since
//     EqActionHandler has no access to the private m_bridge and can only
//     call these EverQuest methods (see eq_commands.cpp's T136 comment).
//   - vendorlist: EqActionHandler::vendorList() -- the pure state read +
//     print this unit put at the action layer (see this plan's Review,
//     "what migrate means for vendorlist").
//
// Argument PARSING (missing/malformed slot, malformed/zero quantity) is
// deliberately NOT retested here -- that logic moved to
// action::CommandProcessor::cmdBuy/cmdSell and is covered in
// test_command_processor.cpp's T136 section (RecordingActionHandler, no
// wire replay needed). What remains here is what still lives in real
// EverQuest/EqActionHandler: the STATEFUL guards (not-at-a-vendor, unknown
// vendor slot) and the intent-push fidelity (same eqt::events::Vendor*Intent
// the renderer's own gesture pushes).
//
// Vendor-open state is set up via the exact same production path the real
// server does: ReplayZone() with HC_OP_ShopRequest (opens the vendor,
// mirrors ZoneProcessShopRequest's Merchant_Open_Struct) and HC_OP_ItemPacket
// with item_packet_type=ITEM_PACKET_MERCHANT (populates m_vendor_items via
// ZoneProcessVendorItemToUI -> TitaniumItemParser::parseItem) -- unchanged
// from the T48 original.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

// T43's helper (test_zone_connection_flow.cpp), reproduced here (same copy
// test_eq_commands_vendor.cpp carried, T48): builds a minimal but
// well-formed Titanium serialized-item string --
// instanceFields|"quoted static fields". Extended with a non-zero price
// (instance field index 3) and count (index 4), which parseInstanceData()
// maps to ItemInstance::price/quantity -- the two GameVendorItem fields
// this unit's /vendorlist and slot-validity checks depend on.
std::string BuildVendorItemString(int16_t slot, uint32_t itemId, const std::string& name,
                                   int32_t price, int32_t count) {
    std::string instance = "0|0|" + std::to_string(slot) + "|" + std::to_string(price) + "|" +
                            std::to_string(count) + "|0|0|0|0|0|0";

    std::vector<std::string> fields(135, "");
    fields[0] = "0";                    // itemType (0 = Common)
    fields[1] = name;                   // name
    fields[2] = name;                   // lore
    fields[3] = "IT63";                 // idfile
    fields[4] = std::to_string(itemId); // itemId
    fields[5] = "10";                   // weight
    fields[6] = "1";                    // noRent
    fields[7] = "1";                    // noDrop
    fields[8] = "0";                    // size
    fields[9] = "0";                    // equip slots bitmask
    fields[10] = "0";                   // vendor price (unused by parser; instance field 3 is)
    fields[11] = "0";                   // icon

    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    return instance + "\"" + staticData + "\"";
}

// Merchant_Open_Struct: npc_id(4)@2, unknown04(4)@6, action(4)@10, sell_rate(4)@14.
std::vector<uint8_t> ShopOpenPayload(uint32_t npcId, uint32_t action = 1, float sellRate = 1.0f) {
    return BuildPayload(18, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, npcId);
        p.PutUInt32(6, 0);
        p.PutUInt32(10, action);
        uint32_t bits;
        std::memcpy(&bits, &sellRate, sizeof(bits));
        p.PutUInt32(14, bits);
    });
}

// ItemPacket wire format: item_packet_type(4)@2, then the serialized item
// string (no length prefix -- length is p.Length()-2-4, per
// ZoneProcessVendorItemToUI).
std::vector<uint8_t> VendorItemPacketPayload(const std::string& itemStr) {
    constexpr uint32_t kItemPacketMerchant = 100;  // EQT::ITEM_PACKET_MERCHANT
    return BuildPayload(2 + 4 + itemStr.size(), [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, kItemPacketMerchant);
        p.PutData(6, const_cast<char*>(itemStr.data()), itemStr.size());
    });
}

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

class EqActionHandlerVendorTest : public ::testing::Test {
protected:
    EqActionHandlerVendorTest() : eq(MakeEq()), handler(eq) { eq.setBridge(&bridge); }
    ~EqActionHandlerVendorTest() override { eq.setBridge(nullptr); }

    // Opens the vendor (npc 500) with one item at slot 5: itemId 1001
    // "a_test_sword", price 1234 copper, count 1.
    void OpenVendorWithOneItem() {
        ReplayZone(eq, HC_OP_ShopRequest, ShopOpenPayload(500));
        ReplayZone(eq, HC_OP_ItemPacket,
            VendorItemPacketPayload(BuildVendorItemString(5, 1001, "a_test_sword", 1234, 1)));
        // Drain the WindowOpened/VendorItemAdded events pushed as a side
        // effect of setup -- these tests only assert on messages/intents the
        // METHOD under test produces, not the setup replay.
        bridge.drainEvents();
    }

    EverQuest eq;
    eqt::EqActionHandler handler;
    TestBridge bridge;
};

// ---------------------------------------------------------------------------
// vendorToggle() -- opens/closes the vendor window with the current target.
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerVendorTest, VendorToggle_NoTargetNotOpen_PrintsNoTarget_PushesNoIntent) {
    handler.vendorToggle();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("target"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqActionHandlerVendorTest, VendorToggle_WithTarget_PushesVendorToggleIntent) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_merchant", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));
    bridge.drainEvents();  // discard NewSpawn-related events

    handler.vendorToggle();

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<eqt::events::VendorToggleIntent>(intents[0]));
}

TEST_F(EqActionHandlerVendorTest, VendorToggle_AlreadyOpen_PushesToggleIntentWithoutRequiringTarget) {
    OpenVendorWithOneItem();  // vendor already open, no CombatManager target set

    handler.vendorToggle();

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<eqt::events::VendorToggleIntent>(intents[0]));
}

// ---------------------------------------------------------------------------
// vendorList() -- pure state read + print.
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerVendorTest, VendorList_NoVendorOpen_PrintsNotAtVendor) {
    handler.vendorList();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You are not at a vendor.");
}

TEST_F(EqActionHandlerVendorTest, VendorList_VendorOpen_ListsSlotNamePriceQuantity) {
    OpenVendorWithOneItem();

    handler.vendorList();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_GE(msgs.size(), 2u);
    bool foundItemLine = false;
    for (const auto& m : msgs) {
        if (m.find("slot 5") != std::string::npos &&
            m.find("a_test_sword") != std::string::npos) {
            foundItemLine = true;
        }
    }
    EXPECT_TRUE(foundItemLine) << "no line named slot 5 / a_test_sword among: "
        << [&]{ std::string s; for (auto& m : msgs) s += m + " | "; return s; }();
}

// ---------------------------------------------------------------------------
// buyFromVendor() -- stateful guards only; argument parsing tested in
// test_command_processor.cpp.
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerVendorTest, Buy_NoVendorOpen_PrintsNotAtVendor_PushesNoIntent) {
    handler.buyFromVendor(5, 1);

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You are not at a vendor.");
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqActionHandlerVendorTest, Buy_UnknownSlot_PrintsNoItemInSlot_PushesNoIntent) {
    OpenVendorWithOneItem();

    handler.buyFromVendor(99, 1);  // vendor only stocks slot 5

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("No item in vendor slot 99"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqActionHandlerVendorTest, Buy_ValidSlotAndQuantity_PushesExactIntent) {
    OpenVendorWithOneItem();

    handler.buyFromVendor(5, 3);

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::VendorBuyIntent>(intents[0]));
    const auto& buy = std::get<eqt::events::VendorBuyIntent>(intents[0]);
    EXPECT_EQ(buy.npcId, 500);       // defaulted to GetVendorNpcId(), not caller-supplied
    EXPECT_EQ(buy.slot, 5u);
    EXPECT_EQ(buy.quantity, 3u);
}

// ---------------------------------------------------------------------------
// sellToVendor() -- unlike buy, sell does not pre-check the vendor's own
// item map (the slot is the CALLER's inventory slot, not a vendor slot --
// SellToVendor() itself validates item presence/NO_DROP/stack size, out of
// this unit's scope per the plan's fence, unchanged from T48).
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerVendorTest, Sell_NoVendorOpen_PrintsNotAtVendor_PushesNoIntent) {
    handler.sellToVendor(23, 1);

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You are not at a vendor.");
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqActionHandlerVendorTest, Sell_ValidSlotAndQuantity_PushesExactIntent) {
    OpenVendorWithOneItem();

    handler.sellToVendor(23, 4);

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::VendorSellIntent>(intents[0]));
    const auto& sell = std::get<eqt::events::VendorSellIntent>(intents[0]);
    EXPECT_EQ(sell.npcId, 500);
    EXPECT_EQ(sell.slot, 23u);
    EXPECT_EQ(sell.quantity, 4u);
}

}  // namespace
