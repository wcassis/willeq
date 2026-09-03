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
        // these, not just the connection. T286: zone-connected and
        // client-ready have ONE store (WorldState), so the harness seeds
        // one place instead of two.
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        eq.m_update_running = true;
        eq.Test_SetMySpawnIdForSeam(kMySpawnId);  // T288: one store

        dispatcher.setActionHandler(&handler);
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

    EXPECT_NE(h.eq.Entities().GetWorldObject(1), nullptr)
        << "OP_ClearObject deleted an unrelated ground spawn";
    // ...and the forge is still there and still open (a combine's ClearObject
    // does not close the window).
    EXPECT_NE(h.eq.Entities().GetWorldObject(kForgeDropId), nullptr);
    EXPECT_EQ(h.eq.GetActiveTradeskillObjectId(), kForgeDropId);
}

// =====================================================================
// T282 -- putting items INTO a world container (the T278 hand-back).
//
// T278 found that InventoryManager::isValidSlot() rejected 4000-4009, so
// `/equip <from> 4000` never sent and the WORLD form of /combine could not
// be driven from the client at all. Everything below is checked against
// eqemu source rather than against a member name (T273/T277):
//
//   * Titanium wire. common/patches/titanium.cpp, ServerToTitaniumSlot /
//     TitaniumToServerSlot: WORLD_BEGIN..WORLD_END (4000..4009,
//     titanium_limits.h invtype::WORLD_SIZE = 10) map straight through in
//     both directions. No translation, so 4000 on the wire IS slot 4000.
//
//   * The server accepts them. zone/inventory.cpp Client::IsValidSlot()
//     returns true for the whole 4000-4009 range unconditionally.
//
//   * ...but only Step 3 of Client::SwapItem() knows what to DO with them,
//     and Step 3 is gated on `m_tradeskill_object != nullptr`. With no
//     world container open the move falls through to Step 5's
//     m_inv.SwapItem(), whose _PutItem(4000, inst) finds
//     CalcSlotId(4000) == INVALID_INDEX (common/inventory_profile.cpp:
//     CalcSlotId has no WORLD branch), takes the "must be within a bag"
//     else-arm, gets a null parent bag, and calls MarkDirty(inst) -- the
//     item is DESTROYED, SwapItem still returns true, and nothing is sent
//     back. So the client must refuse the move itself.
//
//   * Taking an item OUT lands via Client::PutItemInInventory(dst, inst,
//     false), whose m_inv.PutItem() calls DeleteItem(slot_id) first. An
//     occupied destination is silently destroyed, so that is refused too.
//     The cursor is safe (PutItemInInventory forwards to PushItemOnCursor).
//
//   * NO DROP is allowed. The "No Drop Hack" block in SwapItem lists trade
//     and shared-bank destinations only; WORLD_BEGIN..WORLD_END is not in
//     it, which is what makes NO DROP quest combines possible.
//
//   * Neither direction is confirmed. Both world-container arms of Step 3
//     `return true` without queueing a packet (the put's only client
//     traffic is a possible SendCursorBuffer), so the client model has to
//     apply the move locally. The server's only world-container item
//     traffic is Object::HandleClick's open burst -- SendItemPacket(i,
//     inst, ItemPacketWorldContainer) with i the BAG INDEX 0-9, not 4000+i.
// =====================================================================

namespace {

// A Titanium serialized item (the T43/T255 builder shape) at `slot`.
std::string T282PlainItem(int16_t slot, uint32_t itemId, const std::string& name,
                          int32_t stackCount = 0) {
    // Instance fields, titanium.cpp SerializeItem: 0 = stack count (the
    // charges of a stackable, 0 otherwise), 2 = slot, 4 = count (1 for a
    // non-merchant item).
    std::string instance = std::to_string(stackCount) + "|0|" + std::to_string(slot) +
                           "|0|1|0|0|0|0|0|0";
    std::vector<std::string> fields(135, "");
    fields[0] = "0";
    fields[1] = name;
    fields[2] = name;
    fields[3] = "IT63";
    fields[4] = std::to_string(itemId);
    fields[5] = "10";
    fields[6] = "1";
    fields[7] = "1";
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    return instance + "\"" + staticData + "\"";
}

const eqt::events::MoveItemIntent* FindMoveIntent(
        const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& i : intents) {
        if (std::holds_alternative<eqt::events::MoveItemIntent>(i)) {
            return &std::get<eqt::events::MoveItemIntent>(i);
        }
    }
    return nullptr;
}

eqt::inventory::ItemInstance T282Ore(uint32_t itemId, const char* name) {
    eqt::inventory::ItemInstance ore;
    ore.itemId = itemId;
    ore.name = name;
    ore.bagSlots = 0;
    ore.quantity = 1;
    return ore;
}

} // namespace

// ---------------------------------------------------------------------
// 4a. The range is recognised at all
// ---------------------------------------------------------------------

TEST(WorldContainerSlots, MoveIntoAWorldSlotSendsTheWorldSlotOnTheWire) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);

    const int16_t kFrom = eqt::inventory::GENERAL_BEGIN;
    inv->setItem(kFrom, T282Ore(17000, "Small Piece of Ore"));
    h.clearSent();
    h.drainIntents();
    h.drainEvents();

    auto out = inv->moveItem(kFrom, eqt::inventory::WORLD_BEGIN);
    EXPECT_TRUE(out.success) << out.message;

    auto intents = h.drainIntents();
    const auto* mv = FindMoveIntent(intents);
    ASSERT_NE(mv, nullptr) << "no MoveItemIntent for a move into the forge";
    EXPECT_EQ(mv->fromSlot, kFrom);
    EXPECT_EQ(mv->toSlot, eqt::inventory::WORLD_BEGIN);

    // Neither arm of eqemu SwapItem Step 3 answers a successful put, so the
    // client model has to carry it.
    ASSERT_NE(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN)->itemId, 17000u);
    EXPECT_EQ(inv->getItem(kFrom), nullptr);

    // ...and it reaches the wire as 4000, untranslated.
    inv->setItem(kFrom, T282Ore(17000, "Small Piece of Ore"));
    inv->moveItem(kFrom, eqt::inventory::WORLD_BEGIN + 1);
    h.eq.ProcessBridgeIntents();
    auto pkts = h.packetsWithOpcode(HC_OP_MoveItem);
    ASSERT_FALSE(pkts.empty());
    ASSERT_GE(pkts.back().body.size(), 8u);
    EXPECT_EQ(ReadUInt32(pkts.back().body, 0), static_cast<uint32_t>(kFrom));
    EXPECT_EQ(ReadUInt32(pkts.back().body, 4),
              static_cast<uint32_t>(eqt::inventory::WORLD_BEGIN + 1));
}

TEST(WorldContainerSlots, MoveOutOfAWorldSlotToAnEmptySlotIsApplied) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();
    const int16_t kTo = eqt::inventory::GENERAL_BEGIN + 3;
    inv->setItem(eqt::inventory::WORLD_BEGIN, T282Ore(17001, "Water Flask"));
    h.drainIntents();

    auto out = inv->moveItem(eqt::inventory::WORLD_BEGIN, kTo);
    EXPECT_TRUE(out.success) << out.message;
    auto intents = h.drainIntents();
    const auto* mv = FindMoveIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, eqt::inventory::WORLD_BEGIN);
    EXPECT_EQ(mv->toSlot, kTo);
    ASSERT_NE(inv->getItem(kTo), nullptr);
    EXPECT_EQ(inv->getItem(kTo)->itemId, 17001u);
    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr);
}

TEST(WorldContainerSlots, TheCursorRoundTripsThroughAWorldSlot) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();
    const int16_t kFrom = eqt::inventory::GENERAL_BEGIN;
    inv->setItem(kFrom, T282Ore(17002, "Block of Ore"));

    ASSERT_TRUE(inv->moveItem(kFrom, eqt::inventory::CURSOR_SLOT).success);
    ASSERT_TRUE(inv->hasCursorItem());
    auto out = inv->moveItem(eqt::inventory::CURSOR_SLOT, eqt::inventory::WORLD_BEGIN);
    EXPECT_TRUE(out.success) << out.message;
    ASSERT_NE(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN)->itemId, 17002u);
    EXPECT_FALSE(inv->hasCursorItem());

    // ...and back onto the cursor.
    EXPECT_TRUE(inv->moveItem(eqt::inventory::WORLD_BEGIN, eqt::inventory::CURSOR_SLOT).success);
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 17002u);
    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr);
}

// ---------------------------------------------------------------------
// 4b. ...but only while the server has a world container open for us
// ---------------------------------------------------------------------

TEST(WorldContainerSlots, MoveIntoAWorldSlotIsRefusedWithNoContainerOpen) {
    CombineHarness h;
    ASSERT_EQ(h.eq.GetActiveTradeskillObjectId(), 0u);
    auto* inv = h.eq.GetInventoryManager();
    const int16_t kFrom = eqt::inventory::GENERAL_BEGIN;
    inv->setItem(kFrom, T282Ore(17000, "Small Piece of Ore"));
    h.drainIntents();

    auto out = inv->moveItem(kFrom, eqt::inventory::WORLD_BEGIN);
    EXPECT_FALSE(out.success);
    EXPECT_NE(out.message.find("tradeskill container"), std::string::npos) << out.message;
    // Nothing sent, nothing mutated: eqemu would MarkDirty() the item.
    EXPECT_EQ(FindMoveIntent(h.drainIntents()), nullptr);
    ASSERT_NE(inv->getItem(kFrom), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr);
}

TEST(WorldContainerSlots, ClosingTheContainerStopsAcceptingWorldSlotMoves) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();
    const int16_t kFrom = eqt::inventory::GENERAL_BEGIN;
    inv->setItem(kFrom, T282Ore(17000, "Small Piece of Ore"));
    ASSERT_TRUE(inv->moveItem(kFrom, eqt::inventory::WORLD_BEGIN).success);

    // The server's close notification (open = 0).
    h.serverClickObjectAction(kForgeDropId, /*open=*/0, /*type=*/17);
    ASSERT_EQ(h.eq.GetActiveTradeskillObjectId(), 0u);

    inv->setItem(kFrom, T282Ore(17000, "Small Piece of Ore"));
    h.drainIntents();
    auto out = inv->moveItem(kFrom, eqt::inventory::WORLD_BEGIN + 2);
    EXPECT_FALSE(out.success);
    EXPECT_EQ(FindMoveIntent(h.drainIntents()), nullptr);
}

TEST(WorldContainerSlots, LocalCloseCommandAlsoStopsAcceptingWorldSlotMoves) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();
    const int16_t kFrom = eqt::inventory::GENERAL_BEGIN;

    // /closecontainer clears m_active_tradeskill_object_id locally --
    // eqemu does NOT echo ClickObjectAction open=0 (T278), so the client
    // must not keep accepting world-slot moves while it waits.
    auto result = h.processor.processCommand("/closecontainer");
    EXPECT_TRUE(result.success) << result.message;
    h.eq.ProcessBridgeIntents();
    ASSERT_EQ(h.eq.GetActiveTradeskillObjectId(), 0u);

    inv->setItem(kFrom, T282Ore(17000, "Small Piece of Ore"));
    h.drainIntents();
    EXPECT_FALSE(inv->moveItem(kFrom, eqt::inventory::WORLD_BEGIN).success);
    EXPECT_EQ(FindMoveIntent(h.drainIntents()), nullptr);
}

// ---------------------------------------------------------------------
// 4c. The destination rules the server actually applies
// ---------------------------------------------------------------------

TEST(WorldContainerSlots, MoveOutOfAWorldSlotOntoAnOccupiedSlotIsRefused) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();
    const int16_t kTo = eqt::inventory::GENERAL_BEGIN + 3;
    inv->setItem(eqt::inventory::WORLD_BEGIN, T282Ore(17001, "Water Flask"));
    inv->setItem(kTo, T282Ore(17009, "Bandages"));
    h.drainIntents();

    // eqemu: PutItemInInventory -> m_inv.PutItem -> DeleteItem(slot) first.
    // The occupant would be destroyed with no packet back.
    auto out = inv->moveItem(eqt::inventory::WORLD_BEGIN, kTo);
    EXPECT_FALSE(out.success);
    EXPECT_NE(out.message.find("empty"), std::string::npos) << out.message;
    EXPECT_EQ(FindMoveIntent(h.drainIntents()), nullptr);
    ASSERT_NE(inv->getItem(kTo), nullptr);
    EXPECT_EQ(inv->getItem(kTo)->itemId, 17009u);
    ASSERT_NE(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr);
}

TEST(WorldContainerSlots, ANoDropItemMayGoIntoAWorldContainer) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();
    const int16_t kFrom = eqt::inventory::GENERAL_BEGIN;
    auto quest = T282Ore(17500, "Sealed Quest Note");
    quest.noDrop = true;
    inv->setItem(kFrom, quest);
    h.drainIntents();

    // eqemu's "No Drop Hack" block covers trade and shared-bank
    // destinations only -- NO DROP quest combines are legal.
    EXPECT_TRUE(inv->canPlaceInWorldContainer(inv->getItem(kFrom), eqt::inventory::WORLD_BEGIN));
    auto out = inv->moveItem(kFrom, eqt::inventory::WORLD_BEGIN);
    EXPECT_TRUE(out.success) << out.message;
    EXPECT_NE(FindMoveIntent(h.drainIntents()), nullptr);
}

TEST(WorldContainerSlots, ABagMayNotGoIntoAWorldContainer) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();
    const int16_t kFrom = eqt::inventory::GENERAL_BEGIN;
    auto bag = T282Ore(17909, "Forge");
    bag.bagSlots = 10;
    bag.bagType = 17;
    inv->setItem(kFrom, bag);
    h.drainIntents();

    EXPECT_FALSE(inv->canPlaceInWorldContainer(inv->getItem(kFrom), eqt::inventory::WORLD_BEGIN));
    auto out = inv->moveItem(kFrom, eqt::inventory::WORLD_BEGIN);
    EXPECT_FALSE(out.success);
    EXPECT_EQ(FindMoveIntent(h.drainIntents()), nullptr);
}

// ---------------------------------------------------------------------
// 4d. Object::HandleClick's open burst populates the slots
// ---------------------------------------------------------------------

TEST(WorldContainerSlots, WorldContainerItemPacketLandsInTheWorldSlotsNotEquipment) {
    CombineHarness h;
    h.openForge();
    auto* inv = h.eq.GetInventoryManager();

    // Something worn, so a stray write into equipment slot 0/1 is visible.
    inv->setItem(eqt::inventory::SLOT_CHARM, T282Ore(6000, "A Charm"));
    inv->setItem(eqt::inventory::SLOT_EAR1, T282Ore(6001, "An Earring"));

    // zone/object.cpp Object::HandleClick: SendItemPacket(i, inst,
    // ItemPacketWorldContainer) with i the bag index, 0-9.
    for (uint8_t idx : {uint8_t(0), uint8_t(1)}) {
        std::string item = T282PlainItem(static_cast<int16_t>(idx), 17000u + idx, "Ore");
        std::vector<uint8_t> body(4 + item.size() + 1, 0);
        body[0] = static_cast<uint8_t>(EQT::ITEM_PACKET_WORLD_CONTAINER);
        memcpy(body.data() + 4, item.data(), item.size());
        h.eq.ReplayZonePacket(HC_OP_ItemPacket, body.data(), body.size());
    }

    ASSERT_NE(inv->getItem(eqt::inventory::WORLD_BEGIN), nullptr)
        << "world-container item packet did not reach slot 4000";
    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN)->itemId, 17000u);
    ASSERT_NE(inv->getItem(eqt::inventory::WORLD_BEGIN + 1), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::WORLD_BEGIN + 1)->itemId, 17001u);

    // The equipment slots that share the raw indices 0 and 1 are untouched.
    ASSERT_NE(inv->getItem(eqt::inventory::SLOT_CHARM), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::SLOT_CHARM)->itemId, 6000u);
    ASSERT_NE(inv->getItem(eqt::inventory::SLOT_EAR1), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::SLOT_EAR1)->itemId, 6001u);
}

// A plain inventory ItemPacket still lands where it says it does -- the
// world-container remap must not touch the ordinary 103 case, and the
// dispatcher hands processItemPacket() an OPCODE-PREFIXED packet, so the
// serialized item starts at byte 6, not byte 4.
TEST(WorldContainerSlots, OrdinaryItemPacketStillLandsAtItsOwnSlot) {
    CombineHarness h;
    auto* inv = h.eq.GetInventoryManager();
    std::string item = T282PlainItem(eqt::inventory::GENERAL_BEGIN + 4, 7301, "Rusty Short Sword");
    std::vector<uint8_t> body(4 + item.size() + 1, 0);
    body[0] = static_cast<uint8_t>(EQT::ITEM_PACKET_INVENTORY);
    memcpy(body.data() + 4, item.data(), item.size());
    h.eq.ReplayZonePacket(HC_OP_ItemPacket, body.data(), body.size());

    ASSERT_NE(inv->getItem(eqt::inventory::GENERAL_BEGIN + 4), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::GENERAL_BEGIN + 4)->itemId, 7301u);
    EXPECT_EQ(inv->getItem(eqt::inventory::GENERAL_BEGIN + 4)->name, "Rusty Short Sword");
    // Field 0 (stack count) is only read correctly when the 2-byte opcode is
    // skipped; a misaligned read leaves it at the default and the quantity
    // silently falls back to field 4.
    EXPECT_EQ(inv->getItem(eqt::inventory::GENERAL_BEGIN + 4)->quantity, 1);
}

// The 2-byte misalignment above is only inert because instance field 0 is
// the FIRST field: a stray prefix corrupts it and nothing else. Field 0 is
// the stack count, so every stacked item arriving as an OP_ItemPacket --
// looted stacks, vendor buys, and the stacked contents of a world container
// -- silently collapsed to a quantity of 1.
TEST(WorldContainerSlots, StackedItemPacketKeepsItsStackCount) {
    CombineHarness h;
    auto* inv = h.eq.GetInventoryManager();
    std::string item = T282PlainItem(eqt::inventory::GENERAL_BEGIN + 5, 17000, "Small Piece of Ore",
                                     /*stackCount=*/20);
    std::vector<uint8_t> body(4 + item.size() + 1, 0);
    body[0] = static_cast<uint8_t>(EQT::ITEM_PACKET_INVENTORY);
    memcpy(body.data() + 4, item.data(), item.size());
    h.eq.ReplayZonePacket(HC_OP_ItemPacket, body.data(), body.size());

    ASSERT_NE(inv->getItem(eqt::inventory::GENERAL_BEGIN + 5), nullptr);
    EXPECT_EQ(inv->getItem(eqt::inventory::GENERAL_BEGIN + 5)->quantity, 20);
}
