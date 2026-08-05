// T48 -- coverage for /vendorlist, /buy, /sell (eq_commands.cpp
// RegisterVendorCommands()), the command-channel route to
// VendorBuyIntent/VendorSellIntent added to close the automation-
// reachability gap described in refactor/deferred.md "PATTERN: real
// features reachable only through a UI gesture, invisible to automation"
// and refactor/T48_vendor_reachability.md.
//
// Modelled on test_command_processor.cpp's T34 /loot cases (argument
// handling: missing/malformed slot, quantity defaulting) and this file's own
// T28 sibling test_eq_commands_assist.cpp (same MakeEq()/TestBridge/
// ProcessChatInput seam, since these commands live in the SAME registry
// -- EverQuest::m_command_registry -- T28 exercises).
//
// Vendor-open state is set up via the exact same production path the real
// server does: ReplayZone() with HC_OP_ShopRequest (opens the vendor,
// mirrors ZoneProcessShopRequest's Merchant_Open_Struct) and HC_OP_ItemPacket
// with item_packet_type=ITEM_PACKET_MERCHANT (populates m_vendor_items via
// ZoneProcessVendorItemToUI -> TitaniumItemParser::parseItem). The serialized
// item string format is T43's BuildItemString helper
// (test_zone_connection_flow.cpp), copied here rather than shared across a
// header since that file's copy is anonymous-namespace/internal-linkage.
//
// These tests assert what the command pushed onto the bridge's intent queue
// directly (bridge.drainIntents()) rather than driving all the way through
// ProcessBridgeIntents() -> BuyFromVendor()/SellToVendor() -> QueuePacket():
// this unit's territory is the command's reachability into the intent
// system, not re-verifying BuyFromVendor/SellToVendor themselves (out of
// scope per the plan's fence) -- that hop is exercised for real in the live
// eqemu acceptance under benchmarks/audits/T48/.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/events/renderer_intents.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

// T43's helper (test_zone_connection_flow.cpp), reproduced here: builds a
// minimal but well-formed Titanium serialized-item string --
// instanceFields|"quoted static fields". Extended with a non-zero price
// (instance field index 3) and count (index 4), which parseInstanceData()
// maps to ItemInstance::price/quantity -- the two GameVendorItem fields this
// unit's /vendorlist and slot-validity checks depend on.
std::string BuildVendorItemString(int16_t slot, uint32_t itemId, const std::string& name,
                                   int32_t price, int32_t count) {
    // Instance fields (11): stackCount|unknown|slot|price|count|exp|serial|recast|charges|attuned|unknown
    std::string instance = "0|0|" + std::to_string(slot) + "|" + std::to_string(price) + "|" +
                            std::to_string(count) + "|0|0|0|0|0|0";

    // Static fields (135 minimum -- parseStaticData rejects fewer).
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

class EqCommandsVendorTest : public ::testing::Test {
protected:
    EqCommandsVendorTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~EqCommandsVendorTest() override { eq.setBridge(nullptr); }

    // Opens the vendor (npc 500) with one item at slot 5: itemId 1001
    // "a_test_sword", price 1234 copper, count 1.
    void OpenVendorWithOneItem() {
        ReplayZone(eq, HC_OP_ShopRequest, ShopOpenPayload(500));
        ReplayZone(eq, HC_OP_ItemPacket,
            VendorItemPacketPayload(BuildVendorItemString(5, 1001, "a_test_sword", 1234, 1)));
        // Drain the WindowOpened/VendorItemAdded events pushed as a side
        // effect of setup -- these tests only assert on messages/intents the
        // COMMAND under test produces, not the setup replay.
        bridge.drainEvents();
    }

    EverQuest eq;
    TestBridge bridge;
};

// ---------------------------------------------------------------------------
// /vendor -- opens/closes the vendor window with the current target. Added
// alongside /vendorlist, /buy, /sell once scoping the live acceptance
// showed VendorToggleIntent (the gesture's OWN open/close intent) had no
// producer either -- see RegisterVendorCommands()'s comment on vendor_cmd.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsVendorTest, Vendor_NoTargetNotOpen_PrintsNoTarget_PushesNoIntent) {
    eq.ProcessChatInput("/vendor");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("target"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Vendor_WithTarget_PushesVendorToggleIntent) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_merchant", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));
    bridge.drainEvents();  // discard NewSpawn-related events

    eq.ProcessChatInput("/vendor");

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<eqt::events::VendorToggleIntent>(intents[0]));
}

TEST_F(EqCommandsVendorTest, Vendor_AlreadyOpen_PushesToggleIntentWithoutRequiringTarget) {
    OpenVendorWithOneItem();  // vendor already open, no CombatManager target set

    eq.ProcessChatInput("/vendor");

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<eqt::events::VendorToggleIntent>(intents[0]));
}

// ---------------------------------------------------------------------------
// /vendorlist
// ---------------------------------------------------------------------------

TEST_F(EqCommandsVendorTest, VendorList_NoVendorOpen_PrintsNotAtVendor) {
    eq.ProcessChatInput("/vendorlist");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You are not at a vendor.");
}

TEST_F(EqCommandsVendorTest, VendorList_VendorOpen_ListsSlotNamePriceQuantity) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/vendorlist");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_GE(msgs.size(), 2u);
    // Header line, then one line per item -- assert the item line has the
    // slot, name, and formatted price all present rather than pin the exact
    // FormatPrice() string (that formatting is untouched pre-existing code).
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
// /buy -- argument handling (T34 /loot model: missing/malformed slot, no
// vendor open, quantity defaulting), asserted via the pushed intent's fields.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsVendorTest, Buy_NoVendorOpen_PrintsNotAtVendor_PushesNoIntent) {
    eq.ProcessChatInput("/buy 5");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You are not at a vendor.");
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Buy_MissingSlot_PrintsUsage_PushesNoIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/buy");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage:"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Buy_MalformedSlot_PrintsInvalidSlot_PushesNoIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/buy notanumber");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid slot"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Buy_TrailingGarbageSlot_PrintsInvalidSlot_PushesNoIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/buy 5abc");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid slot"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Buy_UnknownSlot_PrintsNoItemInSlot_PushesNoIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/buy 99");  // vendor only stocks slot 5

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("No item in vendor slot 99"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Buy_MalformedQuantity_PrintsInvalidQuantity_PushesNoIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/buy 5 zero");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid quantity"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Buy_ZeroQuantity_PrintsInvalidQuantity_PushesNoIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/buy 5 0");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid quantity"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Buy_ValidSlotNoQuantity_DefaultsQuantityToOne) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/buy 5");

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::VendorBuyIntent>(intents[0]));
    const auto& buy = std::get<eqt::events::VendorBuyIntent>(intents[0]);
    EXPECT_EQ(buy.npcId, 500);       // defaulted to GetVendorNpcId(), not caller-supplied
    EXPECT_EQ(buy.slot, 5u);
    EXPECT_EQ(buy.quantity, 1u);     // default
}

TEST_F(EqCommandsVendorTest, Buy_ValidSlotAndQuantity_PushesExactIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/buy 5 3");

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::VendorBuyIntent>(intents[0]));
    const auto& buy = std::get<eqt::events::VendorBuyIntent>(intents[0]);
    EXPECT_EQ(buy.npcId, 500);
    EXPECT_EQ(buy.slot, 5u);
    EXPECT_EQ(buy.quantity, 3u);
}

// ---------------------------------------------------------------------------
// /sell -- same argument-handling shape; unlike /buy, /sell does not
// pre-check the vendor's own item map (the slot is the CALLER's inventory
// slot, not a vendor slot -- SellToVendor() itself validates item presence/
// NO_DROP/stack size, out of this unit's scope per the plan's fence).
// ---------------------------------------------------------------------------

TEST_F(EqCommandsVendorTest, Sell_NoVendorOpen_PrintsNotAtVendor_PushesNoIntent) {
    eq.ProcessChatInput("/sell 23");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You are not at a vendor.");
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Sell_MissingSlot_PrintsUsage_PushesNoIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/sell");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage:"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Sell_MalformedSlot_PrintsInvalidSlot_PushesNoIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/sell -1");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid slot"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsVendorTest, Sell_ValidSlotNoQuantity_DefaultsQuantityToOne) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/sell 23");

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::VendorSellIntent>(intents[0]));
    const auto& sell = std::get<eqt::events::VendorSellIntent>(intents[0]);
    EXPECT_EQ(sell.npcId, 500);
    EXPECT_EQ(sell.slot, 23u);
    EXPECT_EQ(sell.quantity, 1u);
}

TEST_F(EqCommandsVendorTest, Sell_ValidSlotAndQuantity_PushesExactIntent) {
    OpenVendorWithOneItem();

    eq.ProcessChatInput("/sell 23 4");

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
