// T278 -- manual tradeskill combine + container close (T264 F7).
//
// What T264 actually found, and what this pins:
//
//   /recipecombine is NOT the manual combine. It is T149's AUTO-combine and
//   sends OP_RecipeAutoCombine (SendRecipeAutoCombine) with a recipe id, and
//   the server picks the components. The MANUAL combine -- items placed in
//   the forge by hand, then "Combine" -- is OP_TradeSkillCombine
//   (SendTradeSkillCombine) and had NO producer; the only caller was the
//   TradeskillCombineIntent arm in eq_bridge.cpp, an intent nothing pushed.
//   Same for SendCloseContainer / TradeskillCloseIntent.
//
// Three layers, real EverQuest, no eqemu and no graphics (the test_read_book
// idiom -- real DaybreakConnection over FakeUdpTransport so the WIRE BYTES
// are asserted, not just the call):
//
//  1. COMMAND. CommandProcessor("/combine") -> ActionDispatcher ->
//     EqActionHandler -> EverQuest::RequestTradeskillCombine(), which gates
//     on the container the SERVER said is open and pushes the kept
//     TradeskillCombineIntent. Ditto /closecontainer.
//
//  2. REQUEST BYTES, checked against eqemu rather than member names (T273):
//       OP_TradeSkillCombine (0x0b40) : titanium structs::NewCombine_Struct,
//         DECODE_LENGTH_EXACT 4 bytes -- int16 container_slot, int16
//         guildtribute_slot. container_slot must be 1000
//         (SLOT_TRADESKILL_EXPERIMENT_COMBINE; TitaniumToServerSlot maps
//         1000 -> 1000) or Object::HandleCombine looks in the player's
//         INVENTORY instead of the world container.
//       OP_ClickObjectAction (0x2eae) : ClickObjectAction_Struct, exact 92
//         bytes, no Titanium translation. open must be 0 -- eqemu logs
//         "Unsupported action" for anything else -- and drop_id must be the
//         server's object id or the entity lookup fails.
//
//  3. THE SERVER'S ANSWER. eqemu does NOT echo ClickObjectAction open=0.
//     Object::Close() queues OP_ClearObject (+ two OP_FinishWindow), and so
//     do the world-container branches of HandleCombine/HandleAugmentation
//     after a successful combine. ClearObject_Struct is
//     { uint8 Clear; uint8 Unknown001[7] } with Clear=1 -- it carries NO
//     object id. The client used to read its first four bytes as a drop_id
//     and erase m_world_objects[1]; it now empties the world container
//     slots (4000-4009), which is what the packet means.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "captured_packets.h"
#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/world_object.h"
#include "common/net/daybreak_connection.h"
#include "common/packet_structs.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using namespace zonetest;

namespace {

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

// T144's real captured forge: drop_id 190, object_type 17 (blacksmithing).
constexpr uint32_t kForgeDropId = 190;
constexpr uint16_t kMySpawnId = 4242;

class CombineHarness {
public:
    CombineHarness()
        : eq("127.0.0.1", 0, "test", "test", "test", "Testchar"),
          handler(eq),
          dispatcher(eq.GetGameState()),
          processor(eq.GetGameState(), dispatcher) {
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        manager = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
        manager->OnNewConnection([this](std::shared_ptr<EQ::Net::DaybreakConnection> c) {
            conn = c;
        });
        manager->Connect("peer", 12345);
        EXPECT_NE(conn, nullptr);

        eq.setBridge(&bridge);
        eq.m_zone_connection = conn;
        // SendTradeSkillCombine gates on IsFullyZonedIn() -- all three of
        // these, not just the connection.
        eq.m_zone_connected = true;
        eq.m_client_ready_sent = true;
        eq.m_update_running = true;
        eq.m_my_spawn_id = kMySpawnId;

        dispatcher.setActionHandler(&handler);
        eq.GetGameState().world().setZoneConnected(true);
    }

    ~CombineHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    // The captured OP_GroundSpawn for the forge (opcode included).
    void spawnForge() {
        const uint8_t* bytes = eqt_test::kGroundSpawnForgeBytes;
        size_t size = sizeof(eqt_test::kGroundSpawnForgeBytes);
        uint16_t opcode = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
        eq.ReplayZonePacket(opcode, bytes + 2, size - 2);
    }

    // The SERVER's open/close notification -- the only writer of
    // EverQuest::m_active_tradeskill_object_id.
    void serverClickObjectAction(uint32_t dropId, uint32_t open, uint32_t type) {
        EQT::ClickObjectAction_Struct action;
        memset(&action, 0, sizeof(action));
        action.player_id = kMySpawnId;
        action.drop_id = dropId;
        action.open = open;
        action.type = type;
        action.unknown16 = 0xA;
        action.icon = 1115;
        snprintf(action.object_name, sizeof(action.object_name), "%s", "Half Elf Forge");
        std::vector<uint8_t> body(sizeof(action));
        memcpy(body.data(), &action, sizeof(action));
        eq.ReplayZonePacket(HC_OP_ClickObjectAction, body.data(), body.size());
    }

    void openForge() {
        spawnForge();
        serverClickObjectAction(kForgeDropId, /*open=*/1, /*type=*/17);
    }

    // eqemu's ClearObject_Struct: uint8 Clear = 1, then 7 unknown bytes.
    void serverClearObject() {
        std::vector<uint8_t> body(8, 0);
        body[0] = 1;
        eq.ReplayZonePacket(HC_OP_ClearObject, body.data(), body.size());
    }

    std::vector<SentAppPacket> sentPackets() {
        std::vector<SentAppPacket> out;
        for (auto& [seq, sent] : conn->m_streams[0].sent_packets) {
            const auto* d = static_cast<const uint8_t*>(sent.packet.Data());
            size_t n = sent.packet.Length();
            size_t off = EQ::Net::DaybreakReliableHeader::size();
            if (off < n && d[off] == 0) ++off;
            if (off + 2 > n) continue;
            SentAppPacket pkt;
            pkt.opcode = static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
            pkt.body.assign(d + off + 2, d + n);
            out.push_back(std::move(pkt));
        }
        return out;
    }

    std::vector<SentAppPacket> packetsWithOpcode(uint16_t opcode) {
        std::vector<SentAppPacket> out;
        for (auto& p : sentPackets()) {
            if (p.opcode == opcode) out.push_back(p);
        }
        return out;
    }

    void clearSent() { conn->m_streams[0].sent_packets.clear(); }

    std::vector<eqt::events::RendererIntent> drainIntents() { return bridge.drainIntents(); }

    std::vector<std::string> systemMessages() {
        std::vector<std::string> out;
        for (auto& e : bridge.drainEvents()) {
            if (e.type == eqt::state::GameEventType::SystemMessage) {
                out.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
            }
        }
        return out;
    }

    std::vector<eqt::state::GameEvent> drainEvents() { return bridge.drainEvents(); }

    EverQuest eq;
    RecordingBridge bridge;
    eqt::EqActionHandler handler;
    eqt::action::ActionDispatcher dispatcher;
    eqt::action::CommandProcessor processor;
    EQ::Test::FakeChannel c2s;
    EQ::Test::FakeChannel s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
};

const eqt::events::TradeskillCombineIntent* FindCombineIntent(
        const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& i : intents) {
        if (std::holds_alternative<eqt::events::TradeskillCombineIntent>(i)) {
            return &std::get<eqt::events::TradeskillCombineIntent>(i);
        }
    }
    return nullptr;
}

const eqt::events::TradeskillCloseIntent* FindCloseIntent(
        const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& i : intents) {
        if (std::holds_alternative<eqt::events::TradeskillCloseIntent>(i)) {
            return &std::get<eqt::events::TradeskillCloseIntent>(i);
        }
    }
    return nullptr;
}

int16_t ReadInt16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<int16_t>(b[off] | (b[off + 1] << 8));
}

uint32_t ReadUInt32(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

} // namespace

// ---------------------------------------------------------------------
// 1. The command chain produces the intents T264 listed as producer-less
// ---------------------------------------------------------------------

TEST(TradeskillCombine, CombineCommandWithOpenContainerPushesTheCombineIntent) {
    CombineHarness h;
    h.openForge();
    ASSERT_EQ(h.eq.GetActiveTradeskillObjectId(), kForgeDropId);
    h.drainIntents();

    auto result = h.processor.processCommand("/combine");
    EXPECT_TRUE(result.success) << result.message;

    auto intents = h.drainIntents();
    const auto* intent = FindCombineIntent(intents);
    ASSERT_NE(intent, nullptr) << "/combine pushed no TradeskillCombineIntent";
    // The world-container slot, NOT an inventory bag slot.
    EXPECT_EQ(intent->containerSlot, eqt::inventory::SLOT_TRADESKILL_EXPERIMENT_COMBINE);
    EXPECT_EQ(intent->containerSlot, 1000);
}

TEST(TradeskillCombine, CloseCommandWithOpenContainerPushesTheCloseIntentWithTheServersDropId) {
    CombineHarness h;
    h.openForge();
    h.drainIntents();

    auto result = h.processor.processCommand("/closecontainer");
    EXPECT_TRUE(result.success) << result.message;

    auto intents = h.drainIntents();
    const auto* intent = FindCloseIntent(intents);
    ASSERT_NE(intent, nullptr) << "/closecontainer pushed no TradeskillCloseIntent";
    EXPECT_EQ(intent->dropId, kForgeDropId);
}

// The gate: no container open -> no intent, and the player is told why.
// This is the RED half -- before this unit /combine did not exist at all, so
// there was nothing to refuse.
TEST(TradeskillCombine, NoContainerOpen_BothCommandsRefuseAndPushNothing) {
    CombineHarness h;
    ASSERT_EQ(h.eq.GetActiveTradeskillObjectId(), 0u);
    h.drainIntents();
    h.drainEvents();

    h.processor.processCommand("/combine");
    EXPECT_EQ(FindCombineIntent(h.drainIntents()), nullptr);
    auto msgs = h.systemMessages();
    ASSERT_FALSE(msgs.empty());
    EXPECT_EQ(msgs.back(), "You must have a tradeskill container open.");

    h.processor.processCommand("/closecontainer");
    EXPECT_EQ(FindCloseIntent(h.drainIntents()), nullptr);
    msgs = h.systemMessages();
    ASSERT_FALSE(msgs.empty());
    EXPECT_EQ(msgs.back(), "You do not have a tradeskill container open.");
}

// A stray argument is a typo for /recipecombine, not a combine request --
// silently combining the forge's real contents would consume the wrong items.
TEST(TradeskillCombine, StrayArgumentIsRefusedAndPointsAtRecipeCombine) {
    CombineHarness h;
    h.openForge();
    h.drainIntents();

    // 4321 is not a general slot, so it cannot be the carried-container form.
    auto result = h.processor.processCommand("/combine 4321");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("/recipecombine"), std::string::npos) << result.message;
    EXPECT_EQ(FindCombineIntent(h.drainIntents()), nullptr);

    auto closeResult = h.processor.processCommand("/closecontainer 190");
    EXPECT_FALSE(closeResult.success);
    EXPECT_EQ(FindCloseIntent(h.drainIntents()), nullptr);
}

// ---------------------------------------------------------------------
// 2. The wire bytes
// ---------------------------------------------------------------------

TEST(TradeskillCombine, IntentBecomesAFourByteNewCombineStructWithTheWorldContainerSlot) {
    CombineHarness h;
    h.openForge();
    h.clearSent();

    h.processor.processCommand("/combine");
    h.eq.ProcessBridgeIntents();

    auto pkts = h.packetsWithOpcode(HC_OP_TradeSkillCombine);
    ASSERT_EQ(pkts.size(), 1u) << "expected exactly one OP_TradeSkillCombine";
    // eqemu's DECODE is DECODE_LENGTH_EXACT(structs::NewCombine_Struct):
    // anything but 4 bytes is dropped with a log line.
    ASSERT_EQ(pkts[0].body.size(), sizeof(EQT::NewCombine_Struct));
    ASSERT_EQ(pkts[0].body.size(), 4u);
    EXPECT_EQ(ReadInt16(pkts[0].body, 0), 1000);  // container_slot
    EXPECT_EQ(ReadInt16(pkts[0].body, 2), 0);     // guildtribute_slot
}

TEST(TradeskillCombine, CloseIntentBecomesA92ByteClickObjectActionWithOpenZero) {
    CombineHarness h;
    h.openForge();
    h.clearSent();

    h.processor.processCommand("/closecontainer");
    h.eq.ProcessBridgeIntents();

    auto pkts = h.packetsWithOpcode(HC_OP_ClickObjectAction);
    ASSERT_EQ(pkts.size(), 1u) << "expected exactly one OP_ClickObjectAction";
    ASSERT_EQ(pkts[0].body.size(), sizeof(EQT::ClickObjectAction_Struct));
    ASSERT_EQ(pkts[0].body.size(), 92u);
    EXPECT_EQ(ReadUInt32(pkts[0].body, 0), kMySpawnId);    // player_id
    EXPECT_EQ(ReadUInt32(pkts[0].body, 4), kForgeDropId);  // drop_id
    EXPECT_EQ(ReadUInt32(pkts[0].body, 8), 0u);            // open == 0 -> Object::Close()

    // The client stops considering the container open the moment it asks to
    // close it -- eqemu never echoes ClickObjectAction back.
    EXPECT_EQ(h.eq.GetActiveTradeskillObjectId(), 0u);
}

TEST(TradeskillCombine, CombineDoesNotCloseTheContainer) {
    CombineHarness h;
    h.openForge();

    h.processor.processCommand("/combine");
    h.eq.ProcessBridgeIntents();

    // A combine leaves the forge open -- the player usually makes several.
    EXPECT_EQ(h.eq.GetActiveTradeskillObjectId(), kForgeDropId);
}

// ---------------------------------------------------------------------
// 2b. The CARRIED container form -- /combine <general slot>
//
// The same sender drives both, because container_slot is the only thing
// that tells them apart on the wire. eqemu's HandleCombine branches on
// container_slot == 1000: anything else is looked up with
// user_inv.GetItem(container_slot) and its BagType is read server-side.
// (T149's "some_id" caveat does not apply -- some_id is a HandleCombine
// local, not a wire field of OP_TradeSkillCombine.)
// ---------------------------------------------------------------------

TEST(TradeskillCombine, CarriedContainerFormSendsItsOwnInventorySlot) {
    CombineHarness h;
    // No world container open: the carried form must not need one.
    ASSERT_EQ(h.eq.GetActiveTradeskillObjectId(), 0u);

    const int16_t kKitSlot = eqt::inventory::GENERAL_BEGIN;
    eqt::inventory::ItemInstance kit;
    kit.itemId = 17909;  // "Forge", bagtype 17, 10 slots
    kit.name = "Forge";
    kit.bagSlots = 10;
    kit.bagType = 17;
    h.eq.GetInventoryManager()->setItem(kKitSlot, kit);
    h.clearSent();
    h.drainIntents();

    auto result = h.processor.processCommand("/combine " + std::to_string(kKitSlot));
    EXPECT_TRUE(result.success) << result.message;

    auto intents = h.drainIntents();
    const auto* intent = FindCombineIntent(intents);
    ASSERT_NE(intent, nullptr);
    EXPECT_EQ(intent->containerSlot, kKitSlot);

    // Issue it again and let the bridge drain it this time -- drainIntents()
    // above already consumed the first one.
    h.processor.processCommand("/combine " + std::to_string(kKitSlot));
    h.eq.ProcessBridgeIntents();
    auto pkts = h.packetsWithOpcode(HC_OP_TradeSkillCombine);
    ASSERT_EQ(pkts.size(), 1u);
    ASSERT_EQ(pkts[0].body.size(), 4u);
    EXPECT_EQ(ReadInt16(pkts[0].body, 0), kKitSlot);
    EXPECT_NE(ReadInt16(pkts[0].body, 0), 1000);  // NOT the world form
}

TEST(TradeskillCombine, CarriedContainerFormRefusesAnEmptySlotAndANonContainer) {
    CombineHarness h;
    const int16_t kEmpty = eqt::inventory::GENERAL_BEGIN;
    const int16_t kSword = eqt::inventory::GENERAL_BEGIN + 1;

    eqt::inventory::ItemInstance sword;
    sword.itemId = 5023;
    sword.name = "Rusty Long Sword";
    sword.bagSlots = 0;
    h.eq.GetInventoryManager()->setItem(kSword, sword);
    h.drainEvents();
    h.drainIntents();

    h.processor.processCommand("/combine " + std::to_string(kEmpty));
    EXPECT_EQ(FindCombineIntent(h.drainIntents()), nullptr);
    auto msgs = h.systemMessages();
    ASSERT_FALSE(msgs.empty());
    EXPECT_NE(msgs.back().find("Nothing in"), std::string::npos) << msgs.back();

    h.processor.processCommand("/combine " + std::to_string(kSword));
    EXPECT_EQ(FindCombineIntent(h.drainIntents()), nullptr);
    msgs = h.systemMessages();
    ASSERT_FALSE(msgs.empty());
    EXPECT_NE(msgs.back().find("is not a container"), std::string::npos) << msgs.back();
}

// ---------------------------------------------------------------------
// 3. OP_ClearObject -- the server's actual answer
// ---------------------------------------------------------------------

TEST(TradeskillCombine, ClearObjectEmptiesTheWorldContainerSlots) {
    CombineHarness h;
    h.openForge();

    auto* inv = h.eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);
    eqt::inventory::ItemInstance ore;
    ore.itemId = 17000;
    ore.name = "Small Piece of Ore";
    inv->setItem(eqt::inventory::WORLD_BEGIN, ore);
    inv->setItem(eqt::inventory::WORLD_BEGIN + 1, ore);
    ASSERT_NE(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr);
    h.drainEvents();

    h.serverClearObject();

    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN + 1), nullptr);

    // Every world slot is announced so a UI holding its own copy updates.
    int cleared = 0;
    for (auto& e : h.drainEvents()) {
        if (e.type != eqt::state::GameEventType::InventorySlotChanged) continue;
        const auto& d = std::get<eqt::state::InventorySlotChangedData>(e.data);
        if (d.slotId >= eqt::inventory::WORLD_BEGIN && d.slotId <= eqt::inventory::WORLD_END) {
            EXPECT_FALSE(d.hasItem);
            ++cleared;
        }
    }
    EXPECT_EQ(cleared, eqt::inventory::WORLD_COUNT);
}

// The bug this unit fixed: ClearObject_Struct's first byte is a flag set to
// 1, not an object id. Reading four bytes off it yielded the drop_id 1 and
// deleted whatever ground spawn happened to have it.
TEST(TradeskillCombine, ClearObjectDoesNotDeleteTheWorldObjectWithDropIdOne) {
    CombineHarness h;
    h.openForge();

    // Fabricate a ground spawn at drop_id 1 -- the id the old misread
    // produced from every ClearObject.
    eqt::WorldObject decoy;
    decoy.drop_id = 1;
    decoy.name = "a decoy";
    decoy.object_type = 0;
    h.eq.m_world_objects[1] = decoy;

    h.serverClearObject();

    EXPECT_NE(h.eq.GetWorldObject(1), nullptr)
        << "OP_ClearObject deleted an unrelated ground spawn";
    // ...and the forge is still there and still open (a combine's ClearObject
    // does not close the window).
    EXPECT_NE(h.eq.GetWorldObject(kForgeDropId), nullptr);
    EXPECT_EQ(h.eq.GetActiveTradeskillObjectId(), kForgeDropId);
}
