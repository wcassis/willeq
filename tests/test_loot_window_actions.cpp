// T268 -- loot window actions (take ONE item, loot all, close) through the
// real chain: CommandProcessor -> ActionDispatcher -> EqActionHandler ->
// EverQuest / CombatManager, asserted on the WIRE (the T268 QueuePacket
// capture seam) and on the game-side state the renderer would see.
//
// Every test here drives a public entry point (a slash command, a bridge
// intent, or UpdateLootState() -- the T26 game-tick) and asserts the exact
// packet sequence: OP_LootRequest to open, one OP_LootItem per taken item,
// OP_EndLootRequest to close. Timers are backdated with the T17
// #define-private-public technique combat_manager_test_fixture.h established, so
// nothing sleeps.
//
// Loot-window population uses the same production path the server does:
// ReplayZone() with HC_OP_ItemPacket while a corpse is being looted routes
// to ZoneProcessLootItemToUI (window population) or, once a slot is pending,
// ZoneProcessLootedItemToInventory (the looted item arriving).

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/combat.h"
#undef private
#undef protected

#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "zone_stream_test_utils.h"

#include <chrono>
#include <cstring>

using namespace zonetest;

namespace {

constexpr uint16_t kCorpseId = 777;
constexpr uint16_t kMySpawnId = 556;

// Serialized loot item as ZoneProcessLootItemToUI parses it:
// "1|0|slot|0|1|0|icon|0|0|0|0|" + '"' + static fields (name at static[1] ->
// absolute field[12], itemId at static[4] -> field[15], icon at static[11]
// -> field[22]).
std::string BuildLootItemString(int16_t slot, uint32_t itemId, const std::string& name) {
    std::string instance = "1|0|" + std::to_string(slot) + "|0|1|0|0|0|0|0|0";
    std::vector<std::string> fields(135, "");
    fields[0] = "0";
    fields[1] = name;
    fields[2] = name;
    fields[3] = "IT63";
    fields[4] = std::to_string(itemId);
    fields[5] = "10";
    fields[6] = "1";
    fields[7] = "1";
    fields[8] = "0";
    fields[9] = "0";
    fields[10] = "0";
    fields[11] = "500";
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    return instance + "\"" + staticData + "\"";
}

// ItemPacket wire format: item_packet_type(4)@2, then the serialized item.
std::vector<uint8_t> ItemPacketPayload(uint32_t packetType, const std::string& itemStr) {
    return BuildPayload(2 + 4 + itemStr.size(), [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, packetType);
        p.PutData(6, const_cast<char*>(itemStr.data()), itemStr.size());
    });
}

constexpr uint32_t kItemPacketLoot = 102;       // eqemu ItemPacketLoot
constexpr uint32_t kItemPacketInventory = 103;  // EQT::ITEM_PACKET_INVENTORY

struct LootItemWire {
    uint32_t lootee;
    uint32_t looter;
    uint16_t slot_id;
    uint16_t unknown;
    uint32_t auto_loot;
};
static_assert(sizeof(LootItemWire) == 16, "LootingItem_Struct is 16 bytes");

LootItemWire DecodeLootItem(const EverQuest::TestQueuedPacket& p) {
    LootItemWire w{};
    EXPECT_EQ(p.opcode, HC_OP_LootItem);
    EXPECT_EQ(p.bytes.size(), sizeof(LootItemWire));
    if (p.bytes.size() == sizeof(LootItemWire)) {
        std::memcpy(&w, p.bytes.data(), sizeof(w));
    }
    return w;
}

size_t CountEvents(const std::vector<eqt::state::GameEvent>& evs, eqt::state::GameEventType t) {
    size_t n = 0;
    for (const auto& e : evs) if (e.type == t) ++n;
    return n;
}

class LootWindowActionsTest : public ::testing::Test {
protected:
    LootWindowActionsTest()
        : eq(MakeEq()), handler(eq), dispatcher(eq.GetGameState()),
          processor(eq.GetGameState(), dispatcher) {
        eq.setBridge(&bridge);
        dispatcher.setActionHandler(&handler);
        eq.GetGameState().world().setZoneConnected(true);
        eq.Test_SetMySpawnIdForSeam(kMySpawnId);
        eq.Test_SetCaptureQueuedPacketsForSeam(true);
        // A corpse 10 units away, named the way eqemu names NPC corpses.
        ReplayZone(eq, HC_OP_NewSpawn,
            NewSpawnPayloadAt(kCorpseId, "a_fire_beetle's_corpse777", 0, 10, 0));
        // Manual looting: the auto-loot policy must not take the items itself.
        cm = eq.GetCombatManager();
        cm->SetAutoLootEnabled(false);
        bridge.drainEvents();
        eq.Test_ClearQueuedPackets();
    }
    ~LootWindowActionsTest() override { eq.setBridge(nullptr); }

    // Opens the corpse via /loot <id> and populates two loot slots.
    void OpenWithTwoItems() {
        auto r = processor.processCommand("/loot 777");
        ASSERT_TRUE(r.success) << r.message;
        ReplayZone(eq, HC_OP_ItemPacket,
            ItemPacketPayload(kItemPacketLoot, BuildLootItemString(0, 2001, "Fire Beetle Eye")));
        ReplayZone(eq, HC_OP_ItemPacket,
            ItemPacketPayload(kItemPacketLoot, BuildLootItemString(1, 2002, "Fire Beetle Leg")));
        bridge.drainEvents();
        eq.Test_ClearQueuedPackets();
    }

    // Drives the T26 machine to completion without sleeping: every tick
    // backdates the pacing/closing clocks so each step fires immediately.
    void TickLootMachineToIdle(int maxTicks = 20) {
        for (int i = 0; i < maxTicks && cm->GetLootSubState() != LootSubState::Idle; ++i) {
            cm->m_last_item_send_time = std::chrono::steady_clock::now() -
                std::chrono::milliseconds(CombatManager::kInterItemDelayMs + 10);
            cm->m_loot_state_entered = std::chrono::steady_clock::now() -
                std::chrono::milliseconds(CombatManager::kClosingDelayMs + 10);
            cm->UpdateLootState();
        }
    }

    bool IsLooting() const { return eq.GetGameState().player().isLooting(); }

    EverQuest eq;
    TestBridge bridge;
    eqt::EqActionHandler handler;
    eqt::action::ActionDispatcher dispatcher;
    eqt::action::CommandProcessor processor;
    CombatManager* cm = nullptr;
};

// ---------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------

TEST_F(LootWindowActionsTest, LootByCorpseId_SendsLootRequestAndOpensWindow) {
    auto r = processor.processCommand("/loot 777");
    ASSERT_TRUE(r.success) << r.message;

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].opcode, HC_OP_LootRequest);
    ASSERT_EQ(q[0].bytes.size(), 4u);
    uint32_t id = 0;
    std::memcpy(&id, q[0].bytes.data(), 4);
    EXPECT_EQ(id, kCorpseId);

    EXPECT_TRUE(IsLooting());
    EXPECT_EQ(eq.GetGameState().player().lootingCorpseId(), kCorpseId);
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_LOOTING);
    EXPECT_EQ(CountEvents(bridge.drainEvents(), eqt::state::GameEventType::LootWindowOpened), 1u);
}

TEST_F(LootWindowActionsTest, LootByCorpseName_ResolvesTheCorpseAndOpensIt) {
    auto r = processor.processCommand("/loot beetle");
    ASSERT_TRUE(r.success) << r.message;

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].opcode, HC_OP_LootRequest);
    uint32_t id = 0;
    std::memcpy(&id, q[0].bytes.data(), 4);
    EXPECT_EQ(id, kCorpseId);
    EXPECT_EQ(eq.GetGameState().player().lootingCorpseId(), kCorpseId);
}

TEST_F(LootWindowActionsTest, LootByName_NoSuchCorpse_FailsAndSendsNothing) {
    auto r = processor.processCommand("/loot dragon");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.message.find("dragon"), std::string::npos) << r.message;
    EXPECT_TRUE(eq.Test_GetQueuedPackets().empty());
    EXPECT_FALSE(IsLooting());
}

TEST_F(LootWindowActionsTest, ShiftClickIntent_OpensThroughTheSamePathAsTheCommand) {
    bridge.pushIntent(eqt::events::LootCorpseIntent{kCorpseId});
    eq.Bridge().ProcessBridgeIntents();

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].opcode, HC_OP_LootRequest);
    EXPECT_TRUE(IsLooting());
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_LOOTING)
        << "the gesture and /loot must enter the same loot state machine";
}

// ---------------------------------------------------------------------------
// Window population and manual hold (auto-loot off)
// ---------------------------------------------------------------------------

TEST_F(LootWindowActionsTest, ItemsArriveInTheWindow_AutoLootOffTakesNothing) {
    OpenWithTwoItems();
    // Quiescence and even the hard deadline must NOT take or close anything
    // with auto-loot off: the window is waiting for the player.
    cm->m_last_loot_item_time = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kItemQuiescenceMs + 10);
    cm->UpdateLootState();
    cm->m_loot_state_entered = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kAwaitingItemsHardDeadlineMs + 10);
    cm->UpdateLootState();
    cm->UpdateLootState();

    EXPECT_TRUE(eq.Test_GetQueuedPackets().empty())
        << "no OP_LootItem / OP_EndLootRequest may be sent on the player's behalf";
    EXPECT_TRUE(IsLooting()) << "window must stay open past the auto-loot deadline";
    EXPECT_EQ(cm->m_loot_items.size(), 2u);
}

// ---------------------------------------------------------------------------
// Take ONE item
// ---------------------------------------------------------------------------

TEST_F(LootWindowActionsTest, TakeOneByName_SendsExactlyOneLootItem) {
    OpenWithTwoItems();

    auto r = processor.processCommand("/loot beetle leg");
    ASSERT_TRUE(r.success) << r.message;

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    auto w = DecodeLootItem(q[0]);
    EXPECT_EQ(w.lootee, kCorpseId);
    EXPECT_EQ(w.looter, kMySpawnId);
    EXPECT_EQ(w.slot_id, 1u);
    EXPECT_EQ(w.auto_loot, 1u) << "command-driven take goes to inventory, not the cursor";
    EXPECT_TRUE(IsLooting()) << "taking one item does not close the window";
}

TEST_F(LootWindowActionsTest, TakeOneBySlot_WhileOpen_IsAnItemNotACorpseId) {
    OpenWithTwoItems();

    auto r = processor.processCommand("/loot 0");
    ASSERT_TRUE(r.success) << r.message;

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    auto w = DecodeLootItem(q[0]);
    EXPECT_EQ(w.lootee, kCorpseId);
    EXPECT_EQ(w.slot_id, 0u);
}

TEST_F(LootWindowActionsTest, TakeOne_UnknownItem_FailsAndSendsNothing) {
    OpenWithTwoItems();

    auto r = processor.processCommand("/loot rusty sword");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.message.find("rusty sword"), std::string::npos) << r.message;
    EXPECT_TRUE(eq.Test_GetQueuedPackets().empty());

    auto r2 = processor.processCommand("/loot 7");
    EXPECT_FALSE(r2.success);
    EXPECT_TRUE(eq.Test_GetQueuedPackets().empty());
}

TEST_F(LootWindowActionsTest, TakeOne_LootedItemArrival_RemovesItFromBothViews) {
    OpenWithTwoItems();
    ASSERT_TRUE(processor.processCommand("/loot beetle leg").success);

    // The server ACKs, then the item lands in inventory (ItemPacket while
    // slot 1 is pending -> ZoneProcessLootedItemToInventory).
    ReplayZone(eq, HC_OP_ItemPacket,
        ItemPacketPayload(kItemPacketInventory, BuildLootItemString(23, 2002, "Fire Beetle Leg")));

    auto evs = bridge.drainEvents();
    EXPECT_EQ(CountEvents(evs, eqt::state::GameEventType::LootItemRemoved), 1u);
    EXPECT_EQ(CountEvents(evs, eqt::state::GameEventType::InventorySlotChanged), 1u);
    // The auto-loot machine's own slot list must drop it too, or a later
    // /lootall would re-request an already-taken slot.
    EXPECT_EQ(cm->m_loot_items.size(), 1u);
    EXPECT_EQ(cm->m_loot_items.front(), 0u);
    EXPECT_TRUE(IsLooting());

    // A second take by the same name now finds nothing.
    auto r = processor.processCommand("/loot beetle leg");
    EXPECT_FALSE(r.success);
}

TEST_F(LootWindowActionsTest, LootItemIntent_SendsLootItemToCursor) {
    OpenWithTwoItems();

    bridge.pushIntent(eqt::events::LootItemIntent{kCorpseId, 1});
    eq.Bridge().ProcessBridgeIntents();

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    auto w = DecodeLootItem(q[0]);
    EXPECT_EQ(w.lootee, kCorpseId);
    EXPECT_EQ(w.slot_id, 1u);
    EXPECT_EQ(w.auto_loot, 0u) << "the UI gesture takes to the cursor, as the real client does";
}

// ---------------------------------------------------------------------------
// Loot all
// ---------------------------------------------------------------------------

TEST_F(LootWindowActionsTest, LootAll_SendsOneLootItemPerSlotThenEndLootRequest) {
    OpenWithTwoItems();

    auto r = processor.processCommand("/lootall");
    ASSERT_TRUE(r.success) << r.message;
    TickLootMachineToIdle();

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 3u) << "2 x OP_LootItem + OP_EndLootRequest";
    auto w0 = DecodeLootItem(q[0]);
    auto w1 = DecodeLootItem(q[1]);
    EXPECT_EQ(w0.lootee, kCorpseId);
    EXPECT_EQ(w0.slot_id, 0u);
    EXPECT_EQ(w0.auto_loot, 1u);
    EXPECT_EQ(w1.slot_id, 1u);
    EXPECT_EQ(w1.auto_loot, 1u);
    EXPECT_EQ(q[2].opcode, HC_OP_EndLootRequest);
    ASSERT_EQ(q[2].bytes.size(), 4u);
    uint16_t closed = 0;
    std::memcpy(&closed, q[2].bytes.data(), 2);
    EXPECT_EQ(closed, kCorpseId);

    EXPECT_FALSE(IsLooting());
    EXPECT_NE(cm->GetCombatState(), COMBAT_STATE_LOOTING);
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle);
    EXPECT_EQ(CountEvents(bridge.drainEvents(), eqt::state::GameEventType::LootWindowClosed), 1u);
}

TEST_F(LootWindowActionsTest, LootAll_AfterTakingOne_SkipsTheTakenSlot) {
    OpenWithTwoItems();
    ASSERT_TRUE(processor.processCommand("/loot beetle eye").success);
    ReplayZone(eq, HC_OP_ItemPacket,
        ItemPacketPayload(kItemPacketInventory, BuildLootItemString(23, 2001, "Fire Beetle Eye")));
    eq.Test_ClearQueuedPackets();

    ASSERT_TRUE(processor.processCommand("/lootall").success);
    TickLootMachineToIdle();

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 2u) << "only slot 1 remains, then the close";
    EXPECT_EQ(DecodeLootItem(q[0]).slot_id, 1u);
    EXPECT_EQ(q[1].opcode, HC_OP_EndLootRequest);
}

TEST_F(LootWindowActionsTest, LootAllIntent_DrivesTheSameMachine) {
    OpenWithTwoItems();

    bridge.pushIntent(eqt::events::LootAllIntent{kCorpseId});
    eq.Bridge().ProcessBridgeIntents();
    TickLootMachineToIdle();

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 3u);
    EXPECT_EQ(q[0].opcode, HC_OP_LootItem);
    EXPECT_EQ(q[1].opcode, HC_OP_LootItem);
    EXPECT_EQ(q[2].opcode, HC_OP_EndLootRequest);
    EXPECT_FALSE(IsLooting());
}

TEST_F(LootWindowActionsTest, LootAll_WithoutWindow_FailsAndSendsNothing) {
    auto r = processor.processCommand("/lootall");
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(eq.Test_GetQueuedPackets().empty());
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

TEST_F(LootWindowActionsTest, Close_SendsEndLootRequestOnly) {
    OpenWithTwoItems();

    auto r = processor.processCommand("/lootclose");
    ASSERT_TRUE(r.success) << r.message;

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].opcode, HC_OP_EndLootRequest);
    uint16_t closed = 0;
    std::memcpy(&closed, q[0].bytes.data(), 2);
    EXPECT_EQ(closed, kCorpseId);

    EXPECT_FALSE(IsLooting());
    EXPECT_NE(cm->GetCombatState(), COMBAT_STATE_LOOTING);
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle);
    EXPECT_EQ(cm->m_current_corpse_id, 0);
    EXPECT_EQ(CountEvents(bridge.drainEvents(), eqt::state::GameEventType::LootWindowClosed), 1u);

    // Nothing pending: a later game tick must not send a second close.
    TickLootMachineToIdle();
    EXPECT_EQ(eq.Test_GetQueuedPackets().size(), 1u);
}

TEST_F(LootWindowActionsTest, CloseIntent_SendsEndLootRequestOnly) {
    OpenWithTwoItems();

    bridge.pushIntent(eqt::events::CloseLootIntent{kCorpseId});
    eq.Bridge().ProcessBridgeIntents();

    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].opcode, HC_OP_EndLootRequest);
    EXPECT_FALSE(IsLooting());
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle);
}

TEST_F(LootWindowActionsTest, Close_WithoutWindow_FailsAndSendsNothing) {
    auto r = processor.processCommand("/lootclose");
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(eq.Test_GetQueuedPackets().empty());
}

TEST_F(LootWindowActionsTest, AfterClose_LootByNumberOpensACorpseAgain) {
    OpenWithTwoItems();
    ASSERT_TRUE(processor.processCommand("/lootclose").success);
    eq.Test_ClearQueuedPackets();

    auto r = processor.processCommand("/loot 777");
    ASSERT_TRUE(r.success) << r.message;
    const auto& q = eq.Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].opcode, HC_OP_LootRequest) << "with no window open, a number is a corpse id again";
}

}  // namespace
