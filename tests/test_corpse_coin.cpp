// T382 Leg A (B172) -- the corpse-coin grant.
//
// OP_MoneyOnCorpse is the packet eqemu sends in the same breath as
// Client::AddMoneyToPP(), the instant a loot window opens. MEASURED at the
// official client, T377 (review/T377/, B036's corpse row): it plays
// BuyItem.WAV (138) and prints
//
//     You receive 1 platinum, 8 gold, 3 silver and 4 copper from the corpse.
//
// Before this unit fable parsed the four counts, logged them behind a debug
// level and dropped them: no coin, no sound, no line.
//
// The seams are the same three the rest of the tree is asserted through, so
// none of this needs an audio device, EQ client data or a server: a
// RecordingBridge carries the CurrencyChanged event and the SystemMessage
// line as EVENTS, the PlaySoundIntent as an INTENT, and the purse itself is
// read straight off GameState::player().
//
// The sound row proper is pinned next door in tests/test_ui_sound_events.cpp,
// with the rest of the measured UI-sound table.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/state/event_bus.h"
#include "client/state/game_state.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// The 20-byte moneyOnCorpseStruct at packet offset 2.
void ReplayMoneyOnCorpse(EverQuest& eq, uint8_t response,
                         uint32_t plat, uint32_t gold,
                         uint32_t silver, uint32_t copper) {
    EQT::MoneyOnCorpse_Struct s;
    std::memset(&s, 0, sizeof(s));
    s.response = response;
    s.platinum = plat;
    s.gold = gold;
    s.silver = silver;
    s.copper = copper;

    std::vector<uint8_t> payload(sizeof(s));
    std::memcpy(payload.data(), &s, sizeof(s));
    zonetest::ReplayZone(eq, HC_OP_MoneyOnCorpse, payload);
}

// Every SystemMessage line published, in order.
std::vector<std::string> DrainSystemLines(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (const auto& ev : bridge.drainEvents()) {
        if (ev.type != eqt::state::GameEventType::SystemMessage) continue;
        if (const auto* d = std::get_if<eqt::state::ChatMessageData>(&ev.data)) {
            out.push_back(d->message);
        }
    }
    return out;
}

// The last CurrencyChanged projection, if any.
bool LastCurrencyChanged(eqt::bridge::GameStateBridge& bridge,
                         eqt::state::CurrencyChangedData& out) {
    bool found = false;
    for (const auto& ev : bridge.drainEvents()) {
        if (ev.type != eqt::state::GameEventType::CurrencyChanged) continue;
        if (const auto* d = std::get_if<eqt::state::CurrencyChangedData>(&ev.data)) {
            out = *d;
            found = true;
        }
    }
    return found;
}

}  // namespace

// The measured grant: the four counts land in the purse, ON TOP of what the
// player already carried (the packet carries the DELTA the corpse gave, not
// the new totals OP_MoneyUpdate carries).
TEST(CorpseCoin, GrantIsBankedOntoTheExistingPurse) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    auto& player = eq.GetGameState().player();
    player.setCurrency(4, 6, 1, 8);
    bridge.drainEvents();

    ReplayMoneyOnCorpse(eq, /*response=*/1, 1, 8, 3, 4);

    EXPECT_EQ(player.platinum(), 5u);
    EXPECT_EQ(player.gold(), 14u);
    EXPECT_EQ(player.silver(), 4u);
    EXPECT_EQ(player.copper(), 12u);
    eq.setBridge(nullptr);
}

// An empty purse plus the measured corpse is exactly the measured corpse.
TEST(CorpseCoin, GrantOntoAnEmptyPurseIsTheGrant) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    eq.GetGameState().player().setCurrency(0, 0, 0, 0);
    bridge.drainEvents();

    ReplayMoneyOnCorpse(eq, /*response=*/1, 1, 8, 3, 4);

    const auto& player = eq.GetGameState().player();
    EXPECT_EQ(player.platinum(), 1u);
    EXPECT_EQ(player.gold(), 8u);
    EXPECT_EQ(player.silver(), 3u);
    EXPECT_EQ(player.copper(), 4u);
    eq.setBridge(nullptr);
}

// D18a: the counters on screen move only through the bridge, so the grant has
// to publish the same projection OP_MoneyUpdate publishes.
TEST(CorpseCoin, GrantPublishesCurrencyChangedWithTheNewTotals) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    eq.GetGameState().player().setCurrency(4, 6, 1, 8);
    bridge.drainEvents();

    ReplayMoneyOnCorpse(eq, /*response=*/1, 1, 8, 3, 4);

    eqt::state::CurrencyChangedData data{};
    ASSERT_TRUE(LastCurrencyChanged(bridge, data))
        << "no CurrencyChanged published -- the purse moved and nothing on "
           "screen was told";
    EXPECT_EQ(data.platinum, 5);
    EXPECT_EQ(data.gold, 14);
    EXPECT_EQ(data.silver, 4);
    EXPECT_EQ(data.copper, 12);
    eq.setBridge(nullptr);
}

// The MEASURED line, character for character. eqstr 12072 ("You receive %1
// from the corpse.") with the four counts substituted in. The oracle prints
// the list WITHOUT the Oxford comma that eqstr 13108 carries, so 13108 is
// deliberately not used -- see the handler's comment.
TEST(CorpseCoin, GrantPrintsTheMeasuredOracleLine) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainEvents();

    ReplayMoneyOnCorpse(eq, /*response=*/1, 1, 8, 3, 4);

    const auto lines = DrainSystemLines(bridge);
    const std::string want =
        "You receive 1 platinum, 8 gold, 3 silver and 4 copper from the corpse.";
    ASSERT_EQ(lines.size(), 1u) << "expected exactly one system line";
    EXPECT_EQ(lines[0], want);
    eq.setBridge(nullptr);
}

// A zero denomination is still printed. T377's single capture had all four
// non-zero, so the official client's zero form is UNMEASURED; printing all
// four is what the one measured line shows, and this test pins the choice so
// a later live measurement has something to correct.
TEST(CorpseCoin, AZeroDenominationIsStillPrinted) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainEvents();

    ReplayMoneyOnCorpse(eq, /*response=*/1, 0, 0, 3, 4);

    const auto lines = DrainSystemLines(bridge);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0],
              "You receive 0 platinum, 0 gold, 3 silver and 4 copper from the corpse.");
    eq.setBridge(nullptr);
}

// Every packet that is NOT a coin grant carries four zeros: eqemu's six
// SendLootReqErrorPacket responses, a corpse with no money, and the autosplit
// path (which zeroes the struct and pays the group through its own
// OP_MoneyUpdate). None of them may move the purse, print a line, or make a
// sound.
TEST(CorpseCoin, AnAllZeroPacketGrantsNothingAndSaysNothing) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    auto& player = eq.GetGameState().player();
    player.setCurrency(4, 6, 1, 8);
    bridge.drainEvents();

    ReplayMoneyOnCorpse(eq, /*response=*/1, 0, 0, 0, 0);

    EXPECT_EQ(player.platinum(), 4u);
    EXPECT_EQ(player.gold(), 6u);
    EXPECT_EQ(player.silver(), 1u);
    EXPECT_EQ(player.copper(), 8u);
    EXPECT_TRUE(DrainSystemLines(bridge).empty());
    eq.setBridge(nullptr);
}

TEST(CorpseCoin, ARefusedLootResponseGrantsNothing) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    auto& player = eq.GetGameState().player();
    player.setCurrency(4, 6, 1, 8);
    bridge.drainEvents();

    // LootResponse::SomeoneElse (0) -- SendLootReqErrorPacket zeroes the coin.
    ReplayMoneyOnCorpse(eq, /*response=*/0, 0, 0, 0, 0);

    EXPECT_EQ(player.platinum(), 4u);
    EXPECT_EQ(player.copper(), 8u);
    EXPECT_TRUE(DrainSystemLines(bridge).empty());
    eq.setBridge(nullptr);
}

// A short packet is dropped before anything is read, as it always was.
TEST(CorpseCoin, AShortPacketIsIgnored) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    eq.GetGameState().player().setCurrency(4, 6, 1, 8);
    bridge.drainEvents();

    std::vector<uint8_t> shortPayload(8, 0);
    zonetest::ReplayZone(eq, HC_OP_MoneyOnCorpse, shortPayload);

    EXPECT_EQ(eq.GetGameState().player().platinum(), 4u);
    EXPECT_TRUE(DrainSystemLines(bridge).empty());
    eq.setBridge(nullptr);
}
