// T273 -- read a book / note item (T264 F2).
//
// Three layers pinned against the REAL EverQuest, no eqemu, no graphics:
//
//  1. The COMMAND chain. CommandProcessor("/read ...") ->
//     ActionDispatcher::readItem -> EqActionHandler::readItem ->
//     EverQuest::RequestReadItem resolves the carried item (slot form or
//     item-name form, the same rules /equip uses) and pushes the kept
//     ReadItemIntent -- the intent T264 listed as BLOCKED for having a
//     consumer arm and no producer. Refusals (no argument, no match, not a
//     book) push NOTHING and name what was asked for (T178).
//
//  2. The REQUEST bytes. ProcessBridgeIntents() drains the intent into
//     RequestReadBook() -> SendReadBookRequest(), asserted on the wire via a
//     real DaybreakConnection over FakeUdpTransport (T234 idiom). The wire
//     form is eqemu's titanium structs::BookRequest_Struct:
//       uint8 window (0xFF = new window) | uint8 type | char txtfile[] (NUL)
//     titanium.cpp's DECODE(OP_ReadBook) is DECODE_LENGTH_ATLEAST(3) and
//     strn0cpy's txtfile into the 20-byte server-side field, so the payload
//     must be >= 3 bytes and NUL-terminated. `txtfile` is the item's
//     Filename field (the `books`.`name` key), NOT the item's name.
//
//  3. The REPLY. ZoneProcessReadBook parses structs::BookText_Struct
//     (window | type | booktext[], NUL-terminated) and both publishes
//     NoteWindowOpened AND surfaces the text into the chat window as
//     SystemMessage lines -- IrrlichtRenderer::showNoteWindow() only
//     LOG_INFOs (there is no book panel), so without the chat surfacing the
//     text never reaches a player.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
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

constexpr int16_t kBookSlot = eqt::inventory::GENERAL_BEGIN;
constexpr int16_t kSwordSlot = eqt::inventory::GENERAL_BEGIN + 1;

class ReadBookHarness {
public:
    ReadBookHarness()
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
        eq.Net().Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetZoneConnectedForSeam(true);
        eq.m_zone_connection = conn;
        // T378: this harness observes the outbound stream one layer BELOW
        // SafeQueueZone (the DaybreakConnection resend map), and
        // SafeQueueZone refuses a connection whose stats are all zero
        // ("appears invalid") -- which is exactly what a FakeUdpTransport
        // connection that has never round-tripped looks like. Same line,
        // same reason, as DeathHarness (T346), PoseHarness (T360) and
        // CombineHarness (T376) carry. No assertion is weakened.
        conn->m_stats.sent_packets = 1;

        dispatcher.setActionHandler(&handler);
        eq.GetGameState().world().setZoneConnected(true);
    }

    ~ReadBookHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    // A carried item exactly as the Titanium item packet would land it:
    // `filename` is item field 102 (eqemu ItemData::Filename), `bookType`
    // is field 101 (ItemData::BookType). An empty filename is a non-book.
    void seedItem(int16_t slot, const std::string& name,
                  const std::string& filename, uint32_t bookType) {
        eqt::inventory::ItemInstance item;
        item.itemId = 1000 + static_cast<uint32_t>(slot);
        item.name = name;
        item.bookFlag = filename.empty() ? 0 : 1;
        item.bookType = bookType;
        item.bookFilename = filename;
        eq.GetInventoryManager()->setItem(slot, item);
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

    std::vector<SentAppPacket> readBookPackets() {
        std::vector<SentAppPacket> out;
        for (auto& p : sentPackets()) {
            if (p.opcode == HC_OP_ReadBook) out.push_back(p);
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

    std::vector<eqt::state::NoteWindowOpenedData> noteEvents(
            const std::vector<eqt::state::GameEvent>& evs) {
        std::vector<eqt::state::NoteWindowOpenedData> out;
        for (auto& e : evs) {
            if (e.type == eqt::state::GameEventType::NoteWindowOpened) {
                out.push_back(std::get<eqt::state::NoteWindowOpenedData>(e.data));
            }
        }
        return out;
    }

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

const eqt::events::ReadItemIntent* FindReadIntent(
        const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& i : intents) {
        if (std::holds_alternative<eqt::events::ReadItemIntent>(i)) {
            return &std::get<eqt::events::ReadItemIntent>(i);
        }
    }
    return nullptr;
}

// The server's OP_ReadBook reply: structs::BookText_Struct.
std::vector<uint8_t> BookTextPayload(uint8_t window, uint8_t type, const std::string& text) {
    return BuildPayload(2 + 2 + text.size() + 1, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2, window);
        p.PutUInt8(3, type);
        if (!text.empty()) {
            p.PutData(4, const_cast<char*>(text.data()), text.size());
        }
        p.PutUInt8(4 + text.size(), 0);
    });
}

// ===================================================================
// 1. The command produces the intent
// ===================================================================

TEST(ReadBook, NameFormPushesReadItemIntent) {
    ReadBookHarness h;
    h.seedItem(kBookSlot, "Tattered Note", "tatterednote", 0);

    auto r = h.processor.processCommand("/read Tattered Note");
    EXPECT_TRUE(r.success) << r.message;

    auto intents = h.drainIntents();
    const auto* intent = FindReadIntent(intents);
    ASSERT_NE(intent, nullptr) << "T264 F2: /read must produce the kept ReadItemIntent";
    EXPECT_EQ(intent->filename, "tatterednote")
        << "the wire asks for the item's Filename field, not its name";
    EXPECT_EQ(intent->type, 0);
}

TEST(ReadBook, SlotFormPushesReadItemIntent) {
    ReadBookHarness h;
    h.seedItem(kBookSlot, "Journal of Ranger Wu", "wujournal", 1);

    // T494 (board B796): /read's numeric form takes `inventory.slot_id`.
    auto r = h.processor.processCommand(
        "/read " + std::to_string(eqt::inventory::clientSlotToServer(kBookSlot)));
    EXPECT_TRUE(r.success) << r.message;

    auto intents = h.drainIntents();
    const auto* intent = FindReadIntent(intents);
    ASSERT_NE(intent, nullptr);
    EXPECT_EQ(intent->filename, "wujournal");
    EXPECT_EQ(intent->type, 1) << "type is item field 101 (BookType), not field 100 (Book)";
}

TEST(ReadBook, NotABookIsRefusedAndSendsNothing) {
    ReadBookHarness h;
    h.seedItem(kSwordSlot, "Rusty Short Sword", "", 0);

    auto r = h.processor.processCommand("/read Rusty");
    EXPECT_TRUE(r.success) << "the refusal is a chat message, not a dispatcher failure";

    auto intents = h.drainIntents();
    EXPECT_EQ(FindReadIntent(intents), nullptr);
    auto msgs = h.systemMessages();
    bool named = false;
    for (const auto& m : msgs) {
        if (m.find("Rusty Short Sword") != std::string::npos &&
            m.find("not a book") != std::string::npos) {
            named = true;
        }
    }
    EXPECT_TRUE(named) << "T178: the refusal must name the item";
    EXPECT_TRUE(h.readBookPackets().empty());
}

TEST(ReadBook, NoMatchIsRefusedAndSendsNothing) {
    ReadBookHarness h;
    h.seedItem(kBookSlot, "Tattered Note", "tatterednote", 0);

    auto r = h.processor.processCommand("/read Nonexistent Tome");
    EXPECT_TRUE(r.success);
    auto intents = h.drainIntents();
    EXPECT_EQ(FindReadIntent(intents), nullptr);

    bool named = false;
    for (const auto& m : h.systemMessages()) {
        if (m.find("Nonexistent Tome") != std::string::npos) named = true;
    }
    EXPECT_TRUE(named);
    EXPECT_TRUE(h.readBookPackets().empty());
}

TEST(ReadBook, NoArgumentIsRefusedByTheDispatcher) {
    ReadBookHarness h;
    auto r = h.processor.processCommand("/read");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.message.find("Usage:"), std::string::npos);
    auto intents = h.drainIntents();
    EXPECT_EQ(FindReadIntent(intents), nullptr);
    EXPECT_TRUE(h.readBookPackets().empty());
}

// ===================================================================
// 2. The request bytes
// ===================================================================

TEST(ReadBook, RequestBytesMatchTitaniumBookRequestStruct) {
    ReadBookHarness h;
    h.seedItem(kBookSlot, "Tattered Note", "tatterednote", 2);
    h.clearSent();

    ASSERT_TRUE(h.processor.processCommand("/read Tattered Note").success);
    h.eq.Bridge().ProcessBridgeIntents();

    auto pkts = h.readBookPackets();
    ASSERT_EQ(pkts.size(), 1u) << "exactly one OP_ReadBook per /read";
    const auto& body = pkts[0].body;

    // window | type | txtfile (NUL-terminated)
    ASSERT_EQ(body.size(), 2u + std::string("tatterednote").size() + 1u);
    EXPECT_EQ(body[0], 0xFF) << "0xFF = new window (eqemu BookRequest_Struct::window)";
    EXPECT_EQ(body[1], 2) << "the item's BookType, echoed back by the server on Titanium";
    EXPECT_STREQ(reinterpret_cast<const char*>(body.data() + 2), "tatterednote");
    EXPECT_EQ(body.back(), 0) << "titanium.cpp strn0cpy's txtfile -- it must be NUL-terminated";

    // titanium.cpp DECODE(OP_ReadBook) is DECODE_LENGTH_ATLEAST(
    // structs::BookRequest_Struct) == 3 bytes.
    EXPECT_GE(body.size(), sizeof(EQT::BookRequest_Struct));
    // Client::ReadBook copies txtfile into a char[20] server-side field.
    EXPECT_LT(std::string("tatterednote").size(), 20u);
}

// ===================================================================
// 3. The reply is parsed and surfaced
// ===================================================================

TEST(ReadBook, ReplyPublishesNoteWindowAndChatLines) {
    ReadBookHarness h;
    // eqemu's own struct comment: '`' is the newline character.
    ReplayZone(h.eq, HC_OP_ReadBook,
               BookTextPayload(0xFF, 1, "Line one`Line two`Line three"));

    auto evs = h.bridge.drainEvents();
    auto notes = h.noteEvents(evs);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].text, "Line one`Line two`Line three");
    EXPECT_EQ(notes[0].type, 1);

    std::vector<std::string> chat;
    for (auto& e : evs) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            chat.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
        }
    }
    ASSERT_EQ(chat.size(), 3u) << "the text must reach the chat window, one line per '`'";
    EXPECT_EQ(chat[0], "Line one");
    EXPECT_EQ(chat[1], "Line two");
    EXPECT_EQ(chat[2], "Line three");
}

TEST(ReadBook, ReplySplitsOnCaretToo) {
    ReadBookHarness h;
    ReplayZone(h.eq, HC_OP_ReadBook, BookTextPayload(0xFF, 0, "Alpha^Beta"));

    std::vector<std::string> chat;
    for (auto& e : h.bridge.drainEvents()) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            chat.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
        }
    }
    ASSERT_EQ(chat.size(), 2u);
    EXPECT_EQ(chat[0], "Alpha");
    EXPECT_EQ(chat[1], "Beta");
}

TEST(ReadBook, EmptyReplySaysSoRatherThanNothing) {
    ReadBookHarness h;
    ReplayZone(h.eq, HC_OP_ReadBook, BookTextPayload(0xFF, 0, ""));

    std::vector<std::string> chat;
    for (auto& e : h.bridge.drainEvents()) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            chat.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
        }
    }
    ASSERT_EQ(chat.size(), 1u);
    EXPECT_EQ(chat[0], "The page is blank.");
}

TEST(ReadBook, ShortReplyIsIgnored) {
    ReadBookHarness h;
    ReplayZone(h.eq, HC_OP_ReadBook, BuildPayload(3, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2, 0xFF);
    }));
    auto evs = h.bridge.drainEvents();
    EXPECT_TRUE(h.noteEvents(evs).empty());
}

// ===================================================================
// 3b. The item fields the request is built from
// ===================================================================
//
// eqemu titanium.cpp SerializeItem emits, in order, ItemData::Book (100),
// ItemData::BookType (101) and ItemData::Filename (102). Before T273 the
// parser read field 100 as the book TYPE, dropped field 101 entirely, and
// stored field 102 under a name ("bookText") that claimed it was the text.
TEST(ReadBook, ItemParserReadsBookFlagTypeAndFilename) {
    std::vector<std::string> fields(135, "0");
    fields[0] = "0";
    fields[1] = "Tattered Note";
    fields[2] = "Tattered Note";
    fields[3] = "IT63";
    fields[4] = "12345";
    fields[100] = "1";             // Book: it IS a book
    fields[101] = "2";             // BookType: 2 = item info
    fields[102] = "tatterednote";  // Filename: the `books`.`name` key
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    std::string serialized = "1|0|22|0|1|0|0|0|0|0|0\"" + staticData + "\"";

    int16_t slotId = -1;
    auto item = eqt::inventory::TitaniumItemParser::parseItem(serialized, slotId);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->name, "Tattered Note");
    EXPECT_EQ(item->bookFlag, 1);
    EXPECT_EQ(item->bookType, 2u) << "field 101 is BookType, not field 100";
    EXPECT_EQ(item->bookFilename, "tatterednote");
}

// ===================================================================
// 4. Round trip: command -> wire -> reply -> chat
// ===================================================================

TEST(ReadBook, RoundTripFromCommandToChat) {
    ReadBookHarness h;
    h.seedItem(kBookSlot, "Tattered Note", "tatterednote", 0);
    h.clearSent();

    ASSERT_TRUE(h.processor.processCommand("/read tattered").success);
    h.eq.Bridge().ProcessBridgeIntents();
    ASSERT_EQ(h.readBookPackets().size(), 1u);

    // The server answers with the text for that txtfile.
    ReplayZone(h.eq, HC_OP_ReadBook,
               BookTextPayload(0xFF, 0, "Meet me at the docks.`-- K"));

    bool sawText = false;
    for (auto& e : h.bridge.drainEvents()) {
        if (e.type == eqt::state::GameEventType::SystemMessage &&
            std::get<eqt::state::ChatMessageData>(e.data).message == "Meet me at the docks.") {
            sawText = true;
        }
    }
    EXPECT_TRUE(sawText);
}

}  // namespace
