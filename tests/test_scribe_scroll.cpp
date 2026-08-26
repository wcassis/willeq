// T276 -- scribe a spell scroll into the spellbook (T264 F5).
//
// The route, end to end, against the REAL EverQuest -- no eqemu, no
// graphics:
//
//   /scribe <slot|name>
//     -> CommandProcessor::cmdScribe
//     -> ActionDispatcher::scribeItem      (zone gate + usage string)
//     -> EqActionHandler::scribeItem
//     -> EverQuest::RequestScribeItem      (resolve the scroll, refuse
//                                           locally, pick the book slot)
//     -> ScribeSpellIntent                 (T264 F5's BLOCKED intent)
//     -> ProcessBridgeIntents
//     -> EverQuest::ScribeSpellFromScroll  (OP_MoveItem then
//                                           OP_MemorizeSpell scribing=0)
//     -> server echo OP_MemorizeSpell scribing=0
//     -> SpellManager::scribeSpell + the scroll leaves the cursor
//
// The wire truth this pins was read out of eqemu, not assumed:
//   * Client::OPMemorizeSpell (zone/client_process.cpp) rejects any
//     OP_MemorizeSpell whose size != sizeof(MemorizeSpell_Struct) == 16.
//   * `scribing` for a scroll is memSpellScribing == 0 (zone/client.h:102).
//     It is NOT 1 -- 1 is memSpellMemorize, which is what T272 observed on
//     the /mem path.
//   * `slot` is a SPELLBOOK slot (0..399), not a gem.
//   * The scroll must be on the CURSOR: the handler reads
//     m_inv[EQ::invslot::slotCursor] and requires that item's
//     Scroll.Effect == spell_id. Hence the OP_MoveItem that must precede
//     the OP_MemorizeSpell on the wire.
//   * The scroll's spell id is Titanium item field 154
//     (ItemData::Scroll.Effect, titanium.cpp SerializeItem -- counted
//     against T273's verified Book/BookType/Filename = 100/101/102).

#include <gtest/gtest.h>

// eq.h only -- m_zone_connection / m_pending_scribe_spell_id are private
// (test_read_book.cpp's precedent). SpellManager is deliberately NOT opened
// up: `#define private public` can change member ORDER relative to the
// library's own translation units, and poking its spellbook array directly
// through the re-ordered layout produced garbage here before this was
// found. Every spellbook fixture below goes through the public
// setSpellbook()/scribeSpell() API instead.
#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/spell/spell_manager.h"

#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/spell/spell_constants.h"
#include "client/willeq_database.h"
#include "common/net/daybreak_connection.h"
#include "common/packet_structs.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <array>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

using namespace zonetest;

namespace {

EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) return env_path;
    return WILLEQ_DB_PATH;
}

bool hasDb() { return g_db_available; }

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

constexpr int16_t kScrollSlot = eqt::inventory::GENERAL_BEGIN;
constexpr int16_t kSwordSlot = eqt::inventory::GENERAL_BEGIN + 1;

// A wizard spell the shipped DB knows, usable at level 1, so the harness
// never has to invent spell data. Chosen at runtime from the real DB.
struct ChosenSpells {
    uint32_t usableAtLevel1 = 0;   // class can use it, level requirement 1
    uint32_t highLevel = 0;        // same class, level requirement > 1
    uint32_t wrongClass = 0;       // this class can NEVER use it
};

class ScribeHarness {
public:
    ScribeHarness()
        : eq(MakeEq()),
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
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetZoneConnectedForSeam(true);
        eq.m_zone_connection = conn;

        dispatcher.setActionHandler(&handler);
        eq.GetGameState().world().setZoneConnected(true);
    }

    ~ScribeHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    bool initSpells(EQ::PlayerClass cls, uint8_t level) {
        if (!eq.InitializeSpellSystem("")) return false;
        eq.GetGameState().player().setClass(static_cast<uint32_t>(cls));
        eq.GetGameState().player().setLevel(level);
        return true;
    }

    // The scroll exactly as the Titanium item packet lands it: field 154
    // (Scroll.Effect) carries the spell it teaches.
    void seedScroll(int16_t slot, const std::string& name, uint32_t spellId) {
        eqt::inventory::ItemInstance item;
        item.itemId = 2000 + static_cast<uint32_t>(slot);
        item.name = name;
        item.scrollEffect.effectId = static_cast<int32_t>(spellId);
        item.scrollEffect.type = 3;
        eq.GetInventoryManager()->setItem(slot, item);
    }

    void seedPlainItem(int16_t slot, const std::string& name) {
        eqt::inventory::ItemInstance item;
        item.itemId = 3000 + static_cast<uint32_t>(slot);
        item.name = name;
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

    std::vector<SentAppPacket> packetsWithOpcode(uint16_t op) {
        std::vector<SentAppPacket> out;
        for (auto& p : sentPackets()) {
            if (p.opcode == op) out.push_back(p);
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

    bool anyMessageContains(const std::string& needle) {
        for (const auto& m : systemMessages()) {
            if (m.find(needle) != std::string::npos) return true;
        }
        return false;
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

// NB: bind the vector to a named local before calling this -- the returned
// pointer aliases the argument, and FindScribeIntent(h.drainIntents())
// dangles the moment the full-expression ends.
const eqt::events::ScribeSpellIntent* FindScribeIntent(
        const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& i : intents) {
        if (std::holds_alternative<eqt::events::ScribeSpellIntent>(i)) {
            return &std::get<eqt::events::ScribeSpellIntent>(i);
        }
    }
    return nullptr;
}

// Pick real spells out of the shipped DB for the class under test.
ChosenSpells PickSpells(EQ::SpellDatabase& db, EQ::PlayerClass cls) {
    ChosenSpells out;
    for (uint32_t id = 1; id <= 4000; ++id) {
        const EQ::SpellData* s = db.getSpell(id);
        if (!s) continue;
        const uint8_t lvl = s->getClassLevel(cls);
        if (out.usableAtLevel1 == 0 && lvl == 1) out.usableAtLevel1 = id;
        if (out.highLevel == 0 && lvl > 1 && lvl < 255) out.highLevel = id;
        if (out.wrongClass == 0 && lvl == 255) out.wrongClass = id;
        if (out.usableAtLevel1 && out.highLevel && out.wrongClass) break;
    }
    return out;
}

// Occupy the first `count` spellbook slots through the public API.
void OccupyBookSlots(EQ::SpellManager& mgr, uint16_t count) {
    std::array<uint32_t, EQ::MAX_SPELLBOOK_SLOTS> book;
    book.fill(EQ::SPELL_UNKNOWN);
    for (uint16_t i = 0; i < count && i < EQ::MAX_SPELLBOOK_SLOTS; ++i) {
        book[i] = 900u + i;
    }
    mgr.setSpellbook(book.data(), book.size());
}

// The server's echo: MemorizeSpell_Struct with scribing = memSpellScribing.
std::vector<uint8_t> MemorizeEcho(uint32_t slot, uint32_t spellId, uint32_t scribing) {
    return BuildPayload(18, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, slot);
        p.PutUInt32(6, spellId);
        p.PutUInt32(10, scribing);
        p.PutUInt32(14, 0);
    });
}

// ===================================================================
// 1. findFreeSpellbookSlot -- the destination policy, no device, no DB
// ===================================================================

TEST(SpellbookFreeSlot, EmptyBookPicksSlotZero) {
    EQ::SpellManager mgr(nullptr);
    EXPECT_EQ(mgr.findFreeSpellbookSlot(), 0);
}

TEST(SpellbookFreeSlot, SkipsOccupiedSlots) {
    EQ::SpellManager mgr(nullptr);
    OccupyBookSlots(mgr, 2);
    EXPECT_EQ(mgr.findFreeSpellbookSlot(), 2);
}

TEST(SpellbookFreeSlot, ZeroCountsAsEmptyLikeTheSentinel) {
    // The PlayerProfile spellbook uses BOTH 0 and 0xFFFFFFFF for "empty" --
    // eq.cpp's own scribed-count loop skips both.
    EQ::SpellManager mgr(nullptr);
    std::array<uint32_t, EQ::MAX_SPELLBOOK_SLOTS> book;
    book.fill(EQ::SPELL_UNKNOWN);
    book[0] = 111;
    book[1] = 0;
    mgr.setSpellbook(book.data(), book.size());
    EXPECT_EQ(mgr.findFreeSpellbookSlot(), 1);
}

TEST(SpellbookFreeSlot, FullBookReturnsMinusOne) {
    EQ::SpellManager mgr(nullptr);
    OccupyBookSlots(mgr, EQ::MAX_SPELLBOOK_SLOTS);
    EXPECT_EQ(mgr.findFreeSpellbookSlot(), -1);
}

TEST(SpellbookFreeSlot, SlotsBeyond255AreReachable) {
    // T276 widened ScribeSpellIntent::bookSlot from uint8_t: with 256
    // spells known, the next free slot is 256, which a uint8_t would have
    // wrapped to 0 and silently overwritten a scribed spell.
    EQ::SpellManager mgr(nullptr);
    OccupyBookSlots(mgr, 256);
    EXPECT_EQ(mgr.findFreeSpellbookSlot(), 256);
    EXPECT_GT(static_cast<int>(std::numeric_limits<
                  decltype(eqt::events::ScribeSpellIntent::bookSlot)>::max()),
              255)
        << "ScribeSpellIntent::bookSlot must hold a 400-slot spellbook index";
}

// ===================================================================
// 2. The command produces the intent
// ===================================================================

TEST(ScribeScroll, NameFormPushesScribeSpellIntent) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto picks = PickSpells(h.eq.GetSpellManager()->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);

    auto r = h.processor.processCommand("/scribe Spell: Minor Shielding");
    EXPECT_TRUE(r.success) << r.message;

    const auto intents = h.drainIntents();
    const auto* intent = FindScribeIntent(intents);
    ASSERT_NE(intent, nullptr) << "T264 F5: /scribe must produce the kept ScribeSpellIntent";
    EXPECT_EQ(intent->spellId, picks.usableAtLevel1)
        << "the spell comes from item field 154 (Scroll.Effect), not the item name";
    EXPECT_EQ(intent->bookSlot, 0) << "first free spellbook slot";
    EXPECT_EQ(intent->sourceSlot, kScrollSlot);
}

TEST(ScribeScroll, SlotFormPushesScribeSpellIntent) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto picks = PickSpells(h.eq.GetSpellManager()->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);
    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);

    ASSERT_TRUE(h.processor.processCommand("/scribe " + std::to_string(kScrollSlot)).success);
    const auto intents = h.drainIntents();
    const auto* intent = FindScribeIntent(intents);
    ASSERT_NE(intent, nullptr);
    EXPECT_EQ(intent->spellId, picks.usableAtLevel1);
}

TEST(ScribeScroll, DestinationIsTheFirstFreeBookSlot) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto* mgr = h.eq.GetSpellManager();
    auto picks = PickSpells(mgr->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    OccupyBookSlots(*mgr, 2);
    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);

    ASSERT_TRUE(h.processor.processCommand("/scribe Minor Shielding").success);
    const auto intents = h.drainIntents();
    const auto* intent = FindScribeIntent(intents);
    ASSERT_NE(intent, nullptr);
    EXPECT_EQ(intent->bookSlot, 2);
}

// ===================================================================
// 3. Refusals push nothing and send nothing
// ===================================================================

TEST(ScribeScroll, NotAScrollIsRefusedAndSendsNothing) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    h.seedPlainItem(kSwordSlot, "Rusty Short Sword");
    h.clearSent();

    EXPECT_TRUE(h.processor.processCommand("/scribe Rusty").success)
        << "the refusal is a chat message, not a dispatcher failure";
    const auto intents = h.drainIntents();
    EXPECT_EQ(FindScribeIntent(intents), nullptr);
    EXPECT_TRUE(h.anyMessageContains("Rusty Short Sword")) << "T178: name what was asked for";
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MemorizeSpell).empty());
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MoveItem).empty());
}

TEST(ScribeScroll, AlreadyScribedIsRefusedAndSendsNothing) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto* mgr = h.eq.GetSpellManager();
    auto picks = PickSpells(mgr->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    ASSERT_TRUE(mgr->scribeSpell(picks.usableAtLevel1, 0));
    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);
    h.clearSent();

    ASSERT_TRUE(h.processor.processCommand("/scribe Minor Shielding").success);
    const auto intents = h.drainIntents();
    EXPECT_EQ(FindScribeIntent(intents), nullptr);
    EXPECT_TRUE(h.anyMessageContains("already have this spell scribed"));
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MemorizeSpell).empty());
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MoveItem).empty());
}

TEST(ScribeScroll, LevelTooLowIsRefusedWithTheOfficialString) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto picks = PickSpells(h.eq.GetSpellManager()->getDatabase(), EQ::PlayerClass::Wizard);
    if (picks.highLevel == 0) GTEST_SKIP() << "no above-level-1 wizard spell in scanned range";

    h.seedScroll(kScrollSlot, "Spell: Something Advanced", picks.highLevel);
    h.clearSent();

    ASSERT_TRUE(h.processor.processCommand("/scribe Something Advanced").success);
    const auto intents = h.drainIntents();
    EXPECT_EQ(FindScribeIntent(intents), nullptr);
    // eqstr_us.txt 12048 (eqemu SPELL_LEVEL_TO_LOW), the string the server
    // itself would have sent.
    EXPECT_TRUE(h.anyMessageContains("before you can scribe"));
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MemorizeSpell).empty());
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MoveItem).empty());
}

TEST(ScribeScroll, WrongClassIsRefusedWithTheOfficialString) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 60));
    auto picks = PickSpells(h.eq.GetSpellManager()->getDatabase(), EQ::PlayerClass::Wizard);
    if (picks.wrongClass == 0) GTEST_SKIP() << "no wizard-forbidden spell in scanned range";

    h.seedScroll(kScrollSlot, "Spell: Not For Wizards", picks.wrongClass);
    h.clearSent();

    ASSERT_TRUE(h.processor.processCommand("/scribe Not For Wizards").success);
    const auto intents = h.drainIntents();
    EXPECT_EQ(FindScribeIntent(intents), nullptr);
    // eqstr_us.txt 181 (eqemu CANNOT_USE_ITEM).
    EXPECT_TRUE(h.anyMessageContains("race, class, or deity"));
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MemorizeSpell).empty());
}

TEST(ScribeScroll, FullSpellbookIsRefusedAndSendsNothing) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto* mgr = h.eq.GetSpellManager();
    auto picks = PickSpells(mgr->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    OccupyBookSlots(*mgr, EQ::MAX_SPELLBOOK_SLOTS);
    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);
    h.clearSent();

    ASSERT_TRUE(h.processor.processCommand("/scribe Minor Shielding").success);
    const auto intents = h.drainIntents();
    EXPECT_EQ(FindScribeIntent(intents), nullptr);
    EXPECT_TRUE(h.anyMessageContains("spellbook is full"));
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MemorizeSpell).empty());
}

TEST(ScribeScroll, OccupiedCursorIsRefusedAndSendsNothing) {
    // eqemu reads the scroll off the CURSOR; something else already there
    // would be compared against the spell id and rejected server-side with
    // "Item Instance exists but item does not or spell ids do not match."
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto picks = PickSpells(h.eq.GetSpellManager()->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);
    h.seedPlainItem(kSwordSlot, "Rusty Short Sword");
    ASSERT_TRUE(h.eq.GetInventoryManager()->pickupItem(kSwordSlot));
    h.drainIntents();
    h.clearSent();

    ASSERT_TRUE(h.processor.processCommand("/scribe Minor Shielding").success);
    const auto intents = h.drainIntents();
    EXPECT_EQ(FindScribeIntent(intents), nullptr);
    EXPECT_TRUE(h.anyMessageContains("cursor is full"));
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MemorizeSpell).empty());
}

TEST(ScribeScroll, NoMatchIsRefusedAndSendsNothing) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    h.clearSent();
    ASSERT_TRUE(h.processor.processCommand("/scribe Nonexistent Scroll").success);
    const auto intents = h.drainIntents();
    EXPECT_EQ(FindScribeIntent(intents), nullptr);
    EXPECT_TRUE(h.anyMessageContains("Nonexistent Scroll"));
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MemorizeSpell).empty());
}

TEST(ScribeScroll, NoArgumentIsRefusedByTheDispatcher) {
    ScribeHarness h;
    auto r = h.processor.processCommand("/scribe");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.message.find("Usage:"), std::string::npos);
    const auto intents = h.drainIntents();
    EXPECT_EQ(FindScribeIntent(intents), nullptr);
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MemorizeSpell).empty());
}

// ===================================================================
// 4. The request bytes
// ===================================================================

TEST(ScribeScroll, RequestBytesMatchMemorizeSpellStructWithScribingZero) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto* mgr = h.eq.GetSpellManager();
    auto picks = PickSpells(mgr->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);
    OccupyBookSlots(*mgr, 1);   // force a non-zero destination slot

    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);
    ASSERT_TRUE(h.processor.processCommand("/scribe Minor Shielding").success);
    h.clearSent();
    h.eq.ProcessBridgeIntents();

    auto mem = h.packetsWithOpcode(HC_OP_MemorizeSpell);
    ASSERT_EQ(mem.size(), 1u) << "exactly one OP_MemorizeSpell per /scribe";
    const auto& body = mem[0].body;
    ASSERT_EQ(body.size(), 16u)
        << "Client::OPMemorizeSpell rejects any size != sizeof(MemorizeSpell_Struct)";

    auto u32 = [&](size_t off) {
        return static_cast<uint32_t>(body[off] | (body[off + 1] << 8) |
                                     (body[off + 2] << 16) | (body[off + 3] << 24));
    };
    EXPECT_EQ(u32(0), 1u) << "slot is a SPELLBOOK slot (first free = 1 here), not a gem";
    EXPECT_EQ(u32(4), picks.usableAtLevel1);
    EXPECT_EQ(u32(8), 0u) << "scribing = memSpellScribing = 0, NOT 1 (that is memSpellMemorize)";
    EXPECT_EQ(u32(12), 0u) << "reduction";
}

TEST(ScribeScroll, MoveItemToCursorPrecedesTheMemorizeSpell) {
    // eqemu reads the scroll off the cursor, so the MoveItem has to be
    // ahead of the OP_MemorizeSpell on the wire. Routing the pickup through
    // MoveItemIntent would have queued it AFTER, because both are drained
    // by the same ProcessBridgeIntents() pass.
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto picks = PickSpells(h.eq.GetSpellManager()->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);
    ASSERT_TRUE(h.processor.processCommand("/scribe Minor Shielding").success);
    h.clearSent();
    h.eq.ProcessBridgeIntents();

    int moveIdx = -1, memIdx = -1;
    const auto all = h.sentPackets();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].opcode == HC_OP_MoveItem && moveIdx < 0) moveIdx = static_cast<int>(i);
        if (all[i].opcode == HC_OP_MemorizeSpell && memIdx < 0) memIdx = static_cast<int>(i);
    }
    ASSERT_GE(moveIdx, 0) << "the scroll must be moved to the cursor";
    ASSERT_GE(memIdx, 0);
    EXPECT_LT(moveIdx, memIdx) << "MoveItem must reach the server BEFORE MemorizeSpell";
    // Exactly one. An earlier draft did the pickup in BOTH the producer and
    // the consumer arm and sent the move twice -- the second one addressed a
    // slot the item had already left.
    EXPECT_EQ(h.packetsWithOpcode(HC_OP_MoveItem).size(), 1u)
        << "one move to the cursor, not two";
    EXPECT_EQ(h.packetsWithOpcode(HC_OP_MemorizeSpell).size(), 1u);

    const auto& mv = all[moveIdx].body;
    ASSERT_GE(mv.size(), 12u);
    auto u32 = [&](size_t off) {
        return static_cast<uint32_t>(mv[off] | (mv[off + 1] << 8) |
                                     (mv[off + 2] << 16) | (mv[off + 3] << 24));
    };
    EXPECT_EQ(static_cast<int32_t>(u32(0)), kScrollSlot);
    EXPECT_EQ(static_cast<int32_t>(u32(4)), eqt::inventory::CURSOR_SLOT);

    // And the local model followed: the scroll is on the cursor.
    ASSERT_TRUE(h.eq.GetInventoryManager()->hasCursorItem());
    EXPECT_EQ(h.eq.GetInventoryManager()->getCursorItem()->getScrollSpellId(),
              picks.usableAtLevel1);
}

TEST(ScribeScroll, AScrollAlreadyOnTheCursorIsNotMovedAgain) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto picks = PickSpells(h.eq.GetSpellManager()->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);
    ASSERT_TRUE(h.eq.GetInventoryManager()->pickupItem(kScrollSlot));
    h.drainIntents();

    ASSERT_TRUE(h.processor.processCommand(
        "/scribe " + std::to_string(eqt::inventory::CURSOR_SLOT)).success);
    h.clearSent();
    h.eq.ProcessBridgeIntents();

    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_MoveItem).empty())
        << "already on the cursor -- nothing to move";
    EXPECT_EQ(h.packetsWithOpcode(HC_OP_MemorizeSpell).size(), 1u);
}

// ===================================================================
// 5. The server's echo adds the spell to the book and eats the scroll
// ===================================================================

TEST(ScribeScroll, EchoWithScribingZeroAddsTheSpellAndConsumesTheScroll) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto* mgr = h.eq.GetSpellManager();
    auto picks = PickSpells(mgr->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);
    ASSERT_TRUE(h.processor.processCommand("/scribe Minor Shielding").success);
    h.eq.ProcessBridgeIntents();
    ASSERT_TRUE(h.eq.GetInventoryManager()->hasCursorItem());
    ASSERT_FALSE(mgr->hasSpellScribed(picks.usableAtLevel1));

    ReplayZone(h.eq, HC_OP_MemorizeSpell, MemorizeEcho(0, picks.usableAtLevel1, 0));

    EXPECT_TRUE(mgr->hasSpellScribed(picks.usableAtLevel1));
    EXPECT_EQ(mgr->getSpellbookSpell(0), picks.usableAtLevel1);
    EXPECT_FALSE(h.eq.GetInventoryManager()->hasCursorItem())
        << "the server deletes the scroll from the cursor (DeleteItemInInventory)";
    EXPECT_FALSE(h.eq.GetInventoryManager()->isHoldingSpellForScribe());
    EXPECT_EQ(h.eq.m_pending_scribe_spell_id, 0u);
    EXPECT_TRUE(h.anyMessageContains("You have learned"));
}

TEST(ScribeScroll, AMemorizeEchoDoesNotScribeAnything) {
    // scribing = 1 is memSpellMemorize (T272's live observation on /mem).
    // It must not be mistaken for a scribe confirmation.
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ScribeHarness h;
    ASSERT_TRUE(h.initSpells(EQ::PlayerClass::Wizard, 1));
    auto* mgr = h.eq.GetSpellManager();
    auto picks = PickSpells(mgr->getDatabase(), EQ::PlayerClass::Wizard);
    ASSERT_NE(picks.usableAtLevel1, 0u);

    h.seedScroll(kScrollSlot, "Spell: Minor Shielding", picks.usableAtLevel1);
    ASSERT_TRUE(h.processor.processCommand("/scribe Minor Shielding").success);
    h.eq.ProcessBridgeIntents();

    ReplayZone(h.eq, HC_OP_MemorizeSpell, MemorizeEcho(2, picks.usableAtLevel1, 1));

    EXPECT_FALSE(mgr->hasSpellScribed(picks.usableAtLevel1));
    EXPECT_TRUE(h.eq.GetInventoryManager()->hasCursorItem());
    EXPECT_EQ(h.eq.m_pending_scribe_spell_id, picks.usableAtLevel1);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::string db_path = getWilleqDbPath();
    std::ifstream probe(db_path);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(db_path)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }

    return RUN_ALL_TESTS();
}
