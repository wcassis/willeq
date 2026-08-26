// T277 -- clicking a buff off the buff bar.
//
// Before this unit the whole route was dead in BOTH directions:
//
//   * PRODUCER: the buff bar (renderBuffBar, static_panels.cpp) drew icons
//     and had NO hit-test of any kind. Nothing anywhere pushed
//     BuffCancelIntent -- it sat on test_reachability_sweep's
//     kBlockedIntents list. There was no /removebuff either.
//   * CONSUMER: eq_bridge.cpp's arm called
//     BuffManager::removeBuffBySlot(0, i.slot) and printed "Buff in slot N
//     cancelled". target_id 0 IS the player's own list, so it really did
//     erase the buff -- locally, optimistically, and WITHOUT SENDING A
//     SINGLE BYTE. The server kept the buff ticking; the next OP_Buff or
//     zone-in put it back; and it "succeeded" on detrimental buffs the
//     server refuses to drop.
//
// Four legs, in the order a click travels:
//   1. hitTestBuffBar()             -- geometry, pinned against the renderer
//   2. StaticUIInput::processInput()-- the real producer, real intent queue
//   3. BuildBuffCancelPacket()      -- the wire bytes, offset by offset
//   4. EverQuest::ProcessBridgeIntents() / RequestCancelBuff() -- the real
//      consumer, real BuffManager, asserting the buff does NOT leave the
//      model until the server's OP_Buff bufffade==1 arrives.
//
// WIRE TRUTH (verified against eqemu at /mnt/projects/akk-stack/code, not
// from the plan's wording): Titanium has NO OP_BuffRemoveRequest. That
// opcode and BuffRemoveRequest_Struct are Underfoot+ (the struct's own
// comment says so; patch_Titanium.conf has no such line). Titanium clicks a
// buff off with OP_Buff (0x6a53) carrying the 32-byte Titanium
// SpellBuffPacket_Struct, and Client::Handle_OP_Buff reads exactly one
// field of it -- buff.spellid.

#include <gtest/gtest.h>

#include "client/spell/buff_cancel_request.h"
#include "client/spell/buff_manager.h"
#include "client/spell/spell_manager.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

using namespace EQT::Graphics;

namespace {

// Little-endian readers over the built packet.
uint16_t U16(const uint8_t* p, size_t off) { uint16_t v; std::memcpy(&v, p + off, 2); return v; }
uint32_t U32(const uint8_t* p, size_t off) { uint32_t v; std::memcpy(&v, p + off, 4); return v; }

// ===================================================================
// Leg 1 -- hitTestBuffBar() geometry
// ===================================================================
//
// 800x600: buffBar = {8, chatY - 8 - 36, 308, chatY - 8}. Rather than
// hard-code chatY, every case works from layout.buffBar's own corners --
// the same source renderBuffBar() and the hit-test both read.

class BuffBarHitTest : public ::testing::Test {
protected:
    UILayout layout;
    BuffBarState buffs;

    void SetUp() override { layout.computeLayout(800, 600); }

    // Centre of the Nth ICON DRAWN (not the Nth slot) -- icons are 20px
    // with 2px of padding, packed from the bar's top-left + 2.
    int iconX(int drawIndex) const {
        return layout.buffBar.UpperLeftCorner.X + 2 + drawIndex * 22 + 10;
    }
    int iconY() const { return layout.buffBar.UpperLeftCorner.Y + 2 + 10; }
};

TEST_F(BuffBarHitTest, EmptyBarIsNotClickableAnywhere) {
    // renderBuffBar() draws nothing at all when no slot is occupied -- not
    // even the background -- so nothing in the rect may be hittable.
    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(0), iconY()), -1);
}

TEST_F(BuffBarHitTest, FirstIconIsTheFirstOccupiedSlot) {
    buffs.buffs[0].spellId = 111;
    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(0), iconY()), 0);
}

// THE test that fails against a naive "index = (x - left) / pitch"
// hit-test: the bar is PACKED, so the on-screen column is not the slot.
TEST_F(BuffBarHitTest, PackedBarMapsDrawPositionBackToTheSPARSESlot) {
    buffs.buffs[3].spellId = 111;
    buffs.buffs[9].spellId = 222;
    buffs.buffs[20].spellId = 333;

    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(0), iconY()), 3);
    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(1), iconY()), 9);
    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(2), iconY()), 20);
    // Nothing is drawn in the fourth column.
    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(3), iconY()), -1);
}

TEST_F(BuffBarHitTest, GapBetweenIconsHitsNothing) {
    buffs.buffs[0].spellId = 111;
    buffs.buffs[1].spellId = 222;
    // x = left + 2 + 20 .. +22 is the 2px pad between icon 0 and icon 1.
    int gapX = layout.buffBar.UpperLeftCorner.X + 2 + 21;
    EXPECT_EQ(hitTestBuffBar(layout, buffs, gapX, iconY()), -1);
}

TEST_F(BuffBarHitTest, BelowTheIconRowHitsNothing) {
    // The duration text sits under the icon; it is not part of the target.
    buffs.buffs[0].spellId = 111;
    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(0),
                             layout.buffBar.UpperLeftCorner.Y + 2 + 25), -1);
}

TEST_F(BuffBarHitTest, SlotsPastTheBarWidthAreNotHittable) {
    // The bar is 300px wide with 2px padding: floor((300 - 4) / 22) = 13
    // icons fit, so all 25 occupied slots cannot be reached.
    for (int i = 0; i < BuffBarState::MAX_BUFFS; ++i) buffs.buffs[i].spellId = 100 + i;
    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(12), iconY()), 12);
    EXPECT_EQ(hitTestBuffBar(layout, buffs, iconX(13), iconY()), -1)
        << "renderBuffBar() stops drawing here, so the hit-test must too";
}

// ===================================================================
// Leg 2 -- the click producer
// ===================================================================

class TestUIBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class BuffBarClickTest : public ::testing::Test {
protected:
    UILayout layout;
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    SkillsPopupState skills;
    BuffBarState buffs;
    StaticUIInput input;
    TestUIBridge bridge;

    void SetUp() override {
        layout.computeLayout(800, 600);
        input.init(&bridge);
    }

    bool click(int x, int y) {
        return input.processInput(x, y, /*leftClicked=*/true, layout, hotbar, gems,
                                  inventory, spellbook, skills, buffs);
    }
    int iconX(int drawIndex) const {
        return layout.buffBar.UpperLeftCorner.X + 2 + drawIndex * 22 + 10;
    }
    int iconY() const { return layout.buffBar.UpperLeftCorner.Y + 2 + 10; }
};

TEST_F(BuffBarClickTest, ClickOnABuffPushesCancelIntentForThatSlot) {
    buffs.buffs[4].spellId = 555;
    EXPECT_TRUE(click(iconX(0), iconY()));

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::BuffCancelIntent>(intents[0]));
    EXPECT_EQ(std::get<eqt::events::BuffCancelIntent>(intents[0]).slot, 4);
}

TEST_F(BuffBarClickTest, ClickOnAnEmptyBarPushesNothingAndIsNotConsumed) {
    EXPECT_FALSE(click(iconX(0), iconY()));
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(BuffBarClickTest, ClickBetweenIconsPushesNothing) {
    buffs.buffs[0].spellId = 111;
    buffs.buffs[1].spellId = 222;
    click(layout.buffBar.UpperLeftCorner.X + 2 + 21, iconY());
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(BuffBarClickTest, HoverWithoutAClickPushesNothing) {
    buffs.buffs[0].spellId = 111;
    input.processInput(iconX(0), iconY(), /*leftClicked=*/false, layout, hotbar,
                       gems, inventory, spellbook, skills, buffs);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ===================================================================
// Leg 3 -- the wire bytes
// ===================================================================

TEST(BuffCancelPacket, TitaniumLayoutFieldByField) {
    EQ::ActiveBuff buff;
    buff.spell_id = 202;              // Minor Shielding
    buff.caster_id = 0x1234;
    buff.caster_level = 7;
    buff.remaining_seconds = 162 * 6; // 162 ticks
    buff.counters = 3;
    buff.effect_type = EQ::BuffEffectType::Buff;
    buff.slot = 5;

    auto pkt = EQ::BuildBuffCancelPacket(0x6a53, /*entityId=*/0xABCD, /*slot=*/5, buff);
    const uint8_t* p = pkt.data();

    // 2-byte opcode + 32-byte Titanium SpellBuffPacket_Struct.
    ASSERT_EQ(pkt.size(), 34u);
    EXPECT_EQ(U16(p, 0), 0x6a53u);
    EXPECT_EQ(U32(p, EQ::kBuffPacketEntityId), 0xABCDu);
    EXPECT_EQ(p[EQ::kBuffPacketEffectType], 2u);
    EXPECT_EQ(p[EQ::kBuffPacketLevel], 7u);
    EXPECT_EQ(U32(p, EQ::kBuffPacketSpellId), 202u);
    EXPECT_EQ(U32(p, EQ::kBuffPacketDuration), 162u) << "wire field is TICKS, not seconds";
    EXPECT_EQ(U32(p, EQ::kBuffPacketCounters), 3u);
    EXPECT_EQ(U32(p, EQ::kBuffPacketPlayerId), 0x1234u);
    EXPECT_EQ(U32(p, EQ::kBuffPacketSlotId), 5u);
}

// The single most important byte in the packet, and the least obvious.
TEST(BuffCancelPacket, BuffFadeIsZeroOnTheRequest) {
    // Handle_OP_Buff REFUSES by echoing our own bytes straight back at us
    // (QueuePacket(app)). ZoneProcessBuff() removes the buff on
    // bufffade == 1 -- so a request built with bufffade = 1 would make
    // every refusal look identical to a success and silently desync the
    // buff bar from the server. 0 makes the echo a harmless re-apply.
    EQ::ActiveBuff buff;
    buff.spell_id = 202;
    buff.effect_type = EQ::BuffEffectType::Buff;
    auto pkt = EQ::BuildBuffCancelPacket(0x6a53, 1, 0, buff);
    EXPECT_EQ(U32(pkt.data(), EQ::kBuffPacketBuffFade), 0u);
}

TEST(BuffCancelPacket, PermanentBuffKeepsItsSentinelDuration) {
    EQ::ActiveBuff buff;
    buff.spell_id = 202;
    buff.remaining_seconds = 0xFFFFFFFFu;
    buff.effect_type = EQ::BuffEffectType::Buff;
    auto pkt = EQ::BuildBuffCancelPacket(0x6a53, 1, 0, buff);
    EXPECT_EQ(U32(pkt.data(), EQ::kBuffPacketDuration), 0xFFFFFFFFu);
}

// The offsets the builder writes are the offsets ZoneProcessBuff() reads --
// asserted rather than commented, because they are the same struct seen
// from opposite ends of the wire.
TEST(BuffCancelPacket, OffsetsMatchTheInboundReaderInEqPlayerInfo) {
    EXPECT_EQ(EQ::kBuffPacketEntityId, 2u);
    EXPECT_EQ(EQ::kBuffPacketEffectType, 6u);
    EXPECT_EQ(EQ::kBuffPacketSpellId, 10u);
    EXPECT_EQ(EQ::kBuffPacketDuration, 14u);
    EXPECT_EQ(EQ::kBuffPacketCounters, 18u);
    EXPECT_EQ(EQ::kBuffPacketPlayerId, 22u);
    EXPECT_EQ(EQ::kBuffPacketSlotId, 26u);
    EXPECT_EQ(EQ::kBuffPacketBuffFade, 30u);
}

// ===================================================================
// Leg 4 -- the consumer arm and the command, against a real EverQuest
// ===================================================================

EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) return env_path;
    return WILLEQ_DB_PATH;
}
bool hasDb() { return g_db_available; }

// Put one buff in `slot` on the player, the way OP_Buff would.
void GiveBuff(EverQuest& eq, uint8_t slot, uint32_t spellId, int32_t ticks) {
    EQT::SpellBuff_Struct b{};
    b.effect_type = 2;
    b.level = 10;
    b.spellid = spellId;
    b.duration = ticks;
    b.player_id = 1;
    eq.GetBuffManager()->setPlayerBuff(slot, b);
}

class BuffCancelConsumer : public ::testing::Test {
protected:
    void SetUp() override {
        if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    }
};

TEST_F(BuffCancelConsumer, IntentDoesNotRemoveTheBuffOptimistically) {
    // RED before T277: the arm called removeBuffBySlot(0, slot), which is
    // the player's own list -- the buff vanished from the client while the
    // server still had it and nothing had been sent.
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    GiveBuff(eq, 2, 202, 100);
    ASSERT_NE(eq.GetBuffManager()->getPlayerBuffBySlot(2), nullptr);

    bridge.pushIntent(eqt::events::BuffCancelIntent{2});
    eq.ProcessBridgeIntents();

    EXPECT_NE(eq.GetBuffManager()->getPlayerBuffBySlot(2), nullptr)
        << "the buff must survive until the server confirms the drop";
}

TEST_F(BuffCancelConsumer, ServerConfirmationIsWhatRemovesTheBuff) {
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    eq.Test_SetMySpawnIdForSeam(77);
    GiveBuff(eq, 2, 202, 100);

    bridge.pushIntent(eqt::events::BuffCancelIntent{2});
    eq.ProcessBridgeIntents();
    ASSERT_NE(eq.GetBuffManager()->getPlayerBuffBySlot(2), nullptr);

    // The server's answer: OP_Buff, bufffade == 1, same slot.
    auto payload = zonetest::BuildPayload(34, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(0, 0x6a53);
        p.PutUInt32(EQ::kBuffPacketEntityId, 77);
        p.PutUInt8(EQ::kBuffPacketEffectType, 2);
        p.PutUInt32(EQ::kBuffPacketSpellId, 202);
        p.PutUInt32(EQ::kBuffPacketSlotId, 2);
        p.PutUInt32(EQ::kBuffPacketBuffFade, 1);
    });
    zonetest::ReplayZone(eq, 0x6a53, payload);

    EXPECT_EQ(eq.GetBuffManager()->getPlayerBuffBySlot(2), nullptr);
}

TEST_F(BuffCancelConsumer, ARefusalEchoLeavesTheBuffInPlace) {
    // What the server actually sends back when it says no: our own request
    // bytes, verbatim. Feed the builder's output into the reader and pin
    // that the buff is still there afterwards.
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    eq.Test_SetMySpawnIdForSeam(77);
    GiveBuff(eq, 2, 202, 100);

    const EQ::ActiveBuff* buff = eq.GetBuffManager()->getPlayerBuffBySlot(2);
    ASSERT_NE(buff, nullptr);
    auto request = EQ::BuildBuffCancelPacket(0x6a53, 77, 2, *buff);
    std::vector<uint8_t> echo(request.begin() + 2, request.end());
    zonetest::ReplayZone(eq, 0x6a53, echo);

    const EQ::ActiveBuff* after = eq.GetBuffManager()->getPlayerBuffBySlot(2);
    ASSERT_NE(after, nullptr) << "a refusal must not drop the buff";
    EXPECT_EQ(after->spell_id, 202u);
}

TEST_F(BuffCancelConsumer, RemoveBuffByNameResolvesThroughTheLiveBuffModel) {
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);

    // Find any two distinct spells with names, put them in two slots, and
    // ask for the second one by name.
    auto& db = eq.GetSpellManager()->getDatabase();
    uint32_t idA = 0, idB = 0;
    std::string nameB;
    for (uint32_t id = 1; id <= 4000 && idB == 0; ++id) {
        const EQ::SpellData* s = db.getSpell(id);
        if (!s || s->name.empty()) continue;
        if (idA == 0) { idA = id; continue; }
        if (s->name == db.getSpell(idA)->name) continue;
        idB = id;
        nameB = s->name;
    }
    ASSERT_NE(idB, 0u);

    GiveBuff(eq, 0, idA, 100);
    GiveBuff(eq, 4, idB, 100);

    uint64_t before = eq.GetReplaySuppressedSendCount();
    eq.RequestCancelBuff(nameB);
    // No zone connection in a unit test: the send is suppressed and counted,
    // which is exactly the proof that resolution got as far as the wire hop
    // for the RIGHT slot -- an unresolvable name never reaches it.
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), before + 1);

    // Both buffs still present: the request is not optimistic either way.
    EXPECT_NE(eq.GetBuffManager()->getPlayerBuffBySlot(0), nullptr);
    EXPECT_NE(eq.GetBuffManager()->getPlayerBuffBySlot(4), nullptr);
}

TEST_F(BuffCancelConsumer, RemoveBuffRefusesLocallyOnlyWhatTheClientCanKnow) {
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    GiveBuff(eq, 0, 202, 100);

    uint64_t before = eq.GetReplaySuppressedSendCount();

    eq.RequestCancelBuff("26");            // past MAX_PLAYER_BUFFS
    eq.RequestCancelBuff("0");             // 1-based, so 0 is out of range
    eq.RequestCancelBuff("3");             // in range, but empty
    eq.RequestCancelBuff("Nosuchbuffname");
    eq.RequestCancelBuff("");
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), before)
        << "none of these may reach the wire";

    eq.RequestCancelBuff("1");             // the occupied slot, 1-based
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), before + 1);
}


// ===================================================================
// Leg 5 -- the buff bar can actually FILL
// ===================================================================
//
// Found live, not from the plan: the bar had no population path at all.
// eqemu only sends OP_Buff for a DURATION CHANGE or a FADE
// (Client::SendBuffDurationPacket / MakeBuffFadePacket) -- a buff that is
// merely still running never generates one -- and the PlayerProfile's 25
// buff slots went into BuffManager and stopped there, publishing nothing.
// So renderBuffBar() drew an empty bar for the whole session and the click
// gesture had nothing to hit. PublishPlayerBuffUpdated() is the shared body
// both paths now use.

TEST_F(BuffCancelConsumer, AnAppliedBuffReachesTheRendererAsBuffUpdated) {
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    eq.Test_SetMySpawnIdForSeam(77);

    auto payload = zonetest::BuildPayload(34, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(0, 0x6a53);
        p.PutUInt32(EQ::kBuffPacketEntityId, 77);
        p.PutUInt8(EQ::kBuffPacketEffectType, 2);
        p.PutUInt32(EQ::kBuffPacketSpellId, 202);
        p.PutUInt32(EQ::kBuffPacketDuration, 90);
        p.PutUInt32(EQ::kBuffPacketSlotId, 6);
        p.PutUInt32(EQ::kBuffPacketBuffFade, 0);
    });
    zonetest::ReplayZone(eq, 0x6a53, payload);

    bool found = false;
    for (const auto& ev : bridge.drainEvents()) {
        if (ev.type != eqt::state::GameEventType::BuffUpdated) continue;
        const auto& d = std::get<eqt::state::BuffUpdatedData>(ev.data);
        EXPECT_EQ(d.slot, 6);
        EXPECT_EQ(d.spellId, 202u);
        EXPECT_EQ(d.ticksLeft, 90u);
        EXPECT_FALSE(d.spellName.empty()) << "the bar draws the name, not the id";
        found = true;
    }
    EXPECT_TRUE(found) << "no BuffUpdated reached the bridge";
}

TEST_F(BuffCancelConsumer, ProfileBuffsPublishTheSameEventTheBarConsumes) {
    // The zone-in path: the profile's slots are handed to the SAME body.
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);

    EQT::SpellBuff_Struct b{};
    b.effect_type = 2;
    b.level = 20;
    b.spellid = 202;
    b.duration = 120;
    eq.PublishPlayerBuffUpdated(11, b);

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].type, eqt::state::GameEventType::BuffUpdated);
    const auto& d = std::get<eqt::state::BuffUpdatedData>(events[0].data);
    EXPECT_EQ(d.slot, 11);
    EXPECT_EQ(d.spellId, 202u);
    EXPECT_EQ(d.ticksLeft, 120u);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    std::string dbPath = getWilleqDbPath();
    std::ifstream probe(dbPath);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(dbPath)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }
    return RUN_ALL_TESTS();
}
