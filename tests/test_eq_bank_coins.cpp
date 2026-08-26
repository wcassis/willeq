// T269 -- bank coin deposit / withdraw / convert (T264 F3).
//
// Two layers pinned against the REAL EverQuest:
//
//  1. EverQuest::RequestBankCoinMove/RequestBankCoinConvert (the hop
//     /bank deposit|withdraw|convert reaches through EqActionHandler) push
//     the kept BankCurrencyMoveIntent / BankCurrencyConvertIntent, and
//     refuse (message, no intent) while the bank window is closed.
//
//  2. The eq_bridge.cpp intent arms build OP_MoveCoin exactly as eqemu's
//     Client::OPMoveCoin (zone/client_process.cpp) reads it:
//       from_slot/to_slot: 1 = inventory, 2 = bank (0 cursor, 3 trade,
//                          4 shared bank)
//       cointype1/2:       0 = cp, 1 = sp, 2 = gp, 3 = pp
//       deposit  = {1, 2, t, t, n}; withdraw = {2, 1, t, t, n};
//       convert  = {2, 2, from, to, n} (server ratio CoinTypeCoppers(from)
//                  / CoinTypeCoppers(to), truncating)
//     The bytes are asserted on the wire via a real DaybreakConnection over
//     FakeUdpTransport (T234 idiom). eqemu sends NOTHING back on a
//     successful MoveCoin (OPMoveCoin ends in SaveCurrency(), a DB write --
//     there is no OP_MoneyUpdate / OP_BankerChange echo), so the local
//     currency projection (EverQuest::m_platinum.. + GameState player())
//     is updated by the arm with the SAME arithmetic the server applies;
//     these tests pin that projection and that a refused move changes
//     nothing and sends nothing.
//
// No network, no graphics, no eqemu.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "common/net/daybreak_connection.h"
#include "common/packet_structs.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

class BankCoinHarness {
public:
    BankCoinHarness() : eq("127.0.0.1", 0, "test", "test", "test", "Testchar") {
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
    }

    ~BankCoinHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    // The same two writes ZoneProcessPlayerProfile ends in for currency
    // (eq.cpp: m_platinum.. then player.setCurrency/setBankCurrency).
    void setPools(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp,
                  uint32_t bpp, uint32_t bgp, uint32_t bsp, uint32_t bcp) {
        eq.m_platinum = pp; eq.m_gold = gp; eq.m_silver = sp; eq.m_copper = cp;
        eq.m_bank_platinum = bpp; eq.m_bank_gold = bgp; eq.m_bank_silver = bsp; eq.m_bank_copper = bcp;
        eq.m_game_state.player().setCurrency(pp, gp, sp, cp);
        eq.m_game_state.player().setBankCurrency(bpp, bgp, bsp, bcp);
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

    void clearSent() { conn->m_streams[0].sent_packets.clear(); }

    std::vector<EQT::MoveCoin_Struct> sentMoveCoins() {
        std::vector<EQT::MoveCoin_Struct> out;
        for (auto& p : sentPackets()) {
            if (p.opcode != HC_OP_MoveCoin) continue;
            EXPECT_EQ(p.body.size(), sizeof(EQT::MoveCoin_Struct))
                << "eqemu Handle_OP_MoveCoin drops any size != 20";
            if (p.body.size() < sizeof(EQT::MoveCoin_Struct)) continue;
            EQT::MoveCoin_Struct mc;
            std::memcpy(&mc, p.body.data(), sizeof(mc));
            out.push_back(mc);
        }
        return out;
    }

    std::vector<std::string> systemMessages() {
        std::vector<std::string> out;
        for (auto& e : bridge.drainEvents()) {
            if (e.type == eqt::state::GameEventType::SystemMessage) {
                out.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
            }
        }
        return out;
    }

    // Counts of the two projection events since the last drain.
    std::pair<int, int> currencyEvents() {
        int cur = 0, bank = 0;
        for (auto& e : bridge.drainEvents()) {
            if (e.type == eqt::state::GameEventType::CurrencyChanged) ++cur;
            if (e.type == eqt::state::GameEventType::BankCurrencyChanged) ++bank;
        }
        return {cur, bank};
    }

    EQ::Test::FakeChannel c2s;
    EQ::Test::FakeChannel s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    RecordingBridge bridge;
    EverQuest eq;
};

constexpr uint16_t kBankerId = 200;

// =====================================================================
// 1. The producer hop: RequestBankCoinMove / RequestBankCoinConvert
// =====================================================================

TEST(BankCoinRequest, Move_WindowClosed_RefusesAndPushesNoIntent) {
    BankCoinHarness h;
    h.setPools(10, 0, 0, 0, 0, 0, 0, 0);
    h.bridge.drainEvents();

    h.eq.RequestBankCoinMove(EQT::COINTYPE_PP, 5, /*fromBank=*/false);

    EXPECT_TRUE(h.bridge.drainIntents().empty());
    auto msgs = h.systemMessages();
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("bank"), std::string::npos) << msgs[0];
}

TEST(BankCoinRequest, Move_WindowOpen_PushesBankCurrencyMoveIntent) {
    BankCoinHarness h;
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();

    h.eq.RequestBankCoinMove(EQT::COINTYPE_GP, 7, /*fromBank=*/true);

    auto intents = h.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::BankCurrencyMoveIntent>(intents[0]));
    const auto& i = std::get<eqt::events::BankCurrencyMoveIntent>(intents[0]);
    EXPECT_EQ(i.coinType, EQT::COINTYPE_GP);
    EXPECT_EQ(i.amount, 7);
    EXPECT_TRUE(i.fromBank);
    EXPECT_TRUE(h.systemMessages().empty());
}

TEST(BankCoinRequest, Convert_WindowClosed_RefusesAndPushesNoIntent) {
    BankCoinHarness h;
    h.bridge.drainEvents();

    h.eq.RequestBankCoinConvert(EQT::COINTYPE_CP, EQT::COINTYPE_SP, 30);

    EXPECT_TRUE(h.bridge.drainIntents().empty());
    EXPECT_EQ(h.systemMessages().size(), 1u);
}

TEST(BankCoinRequest, Convert_WindowOpen_PushesBankCurrencyConvertIntent) {
    BankCoinHarness h;
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();

    h.eq.RequestBankCoinConvert(EQT::COINTYPE_CP, EQT::COINTYPE_SP, 30);

    auto intents = h.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::BankCurrencyConvertIntent>(intents[0]));
    const auto& i = std::get<eqt::events::BankCurrencyConvertIntent>(intents[0]);
    EXPECT_EQ(i.fromCoinType, EQT::COINTYPE_CP);
    EXPECT_EQ(i.toCoinType, EQT::COINTYPE_SP);
    EXPECT_EQ(i.amount, 30);
}

// =====================================================================
// 2. The consumer arm: intent -> OP_MoveCoin bytes + local projection
// =====================================================================

TEST(BankCoinIntent, Deposit_SendsInventoryToBankMoveCoin_AndProjectsBothPools) {
    BankCoinHarness h;
    h.setPools(10, 1, 2, 3, 4, 5, 6, 7);
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyMoveIntent{
        static_cast<uint8_t>(EQT::COINTYPE_PP), 5, /*fromBank=*/false});
    h.eq.ProcessBridgeIntents();

    auto mcs = h.sentMoveCoins();
    ASSERT_EQ(mcs.size(), 1u);
    EXPECT_EQ(mcs[0].from_slot, EQT::COINSLOT_INVENTORY);  // 1
    EXPECT_EQ(mcs[0].to_slot, EQT::COINSLOT_BANK);         // 2
    EXPECT_EQ(mcs[0].cointype1, static_cast<uint32_t>(EQT::COINTYPE_PP));  // 3
    EXPECT_EQ(mcs[0].cointype2, static_cast<uint32_t>(EQT::COINTYPE_PP));
    EXPECT_EQ(mcs[0].amount, 5);

    // Raw bytes, little-endian int32 x5 -- what eqemu memcpy's into MoveCoin_Struct.
    auto pkts = h.sentPackets();
    ASSERT_EQ(pkts.size(), 1u);
    const std::vector<uint8_t> expected = {
        1,0,0,0,  2,0,0,0,  3,0,0,0,  3,0,0,0,  5,0,0,0};
    EXPECT_EQ(pkts[0].body, expected);

    // Projection: EverQuest's own counters AND the GameState the /bank
    // pool check reads, both pools, other denominations untouched.
    EXPECT_EQ(h.eq.m_platinum, 5u);
    EXPECT_EQ(h.eq.m_bank_platinum, 9u);
    EXPECT_EQ(h.eq.m_gold, 1u);
    EXPECT_EQ(h.eq.m_bank_gold, 5u);
    const auto& player = h.eq.GetGameState().player();
    EXPECT_EQ(player.platinum(), 5u);
    EXPECT_EQ(player.bankPlatinum(), 9u);
    EXPECT_EQ(player.copper(), 3u);
    EXPECT_EQ(player.bankCopper(), 7u);
    auto [cur, bank] = h.currencyEvents();
    EXPECT_EQ(cur, 1);
    EXPECT_EQ(bank, 1);
}

TEST(BankCoinIntent, Withdraw_SendsBankToInventoryMoveCoin) {
    BankCoinHarness h;
    h.setPools(0, 0, 0, 0, 0, 0, 0, 25);
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyMoveIntent{
        static_cast<uint8_t>(EQT::COINTYPE_CP), 25, /*fromBank=*/true});
    h.eq.ProcessBridgeIntents();

    auto mcs = h.sentMoveCoins();
    ASSERT_EQ(mcs.size(), 1u);
    EXPECT_EQ(mcs[0].from_slot, EQT::COINSLOT_BANK);
    EXPECT_EQ(mcs[0].to_slot, EQT::COINSLOT_INVENTORY);
    EXPECT_EQ(mcs[0].cointype1, 0u);
    EXPECT_EQ(mcs[0].cointype2, 0u);
    EXPECT_EQ(mcs[0].amount, 25);
    EXPECT_EQ(h.eq.m_copper, 25u);
    EXPECT_EQ(h.eq.m_bank_copper, 0u);
    EXPECT_EQ(h.eq.GetGameState().player().copper(), 25u);
    EXPECT_EQ(h.eq.GetGameState().player().bankCopper(), 0u);
}

TEST(BankCoinIntent, Deposit_PoolLacksCoins_SendsNothing_ProjectionUnchanged) {
    BankCoinHarness h;
    h.setPools(3, 0, 0, 0, 0, 0, 0, 0);
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyMoveIntent{
        static_cast<uint8_t>(EQT::COINTYPE_PP), 5, /*fromBank=*/false});
    h.eq.ProcessBridgeIntents();

    EXPECT_TRUE(h.sentPackets().empty());
    EXPECT_EQ(h.eq.m_platinum, 3u);
    EXPECT_EQ(h.eq.m_bank_platinum, 0u);
    EXPECT_EQ(h.eq.GetGameState().player().platinum(), 3u);
    auto [cur, bank] = h.currencyEvents();
    EXPECT_EQ(cur, 0);
    EXPECT_EQ(bank, 0);
}

TEST(BankCoinIntent, Move_WindowClosed_SendsNothing) {
    // A gesture/intent arriving after the window closed: eqemu would log a
    // POSSIBLE_HACK and drop it silently if no banker is in range -- so the
    // arm refuses without sending, keeping the projection honest.
    BankCoinHarness h;
    h.setPools(10, 0, 0, 0, 0, 0, 0, 0);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyMoveIntent{
        static_cast<uint8_t>(EQT::COINTYPE_PP), 5, /*fromBank=*/false});
    h.eq.ProcessBridgeIntents();

    EXPECT_TRUE(h.sentPackets().empty());
    EXPECT_EQ(h.eq.m_platinum, 10u);
    EXPECT_EQ(h.eq.m_bank_platinum, 0u);
}

TEST(BankCoinIntent, ConvertUp_SendsBankToBankWithBothCoinTypes_AndProjectsRatio) {
    BankCoinHarness h;
    h.setPools(0, 0, 0, 0, 0, 0, 1, 30);
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyConvertIntent{
        static_cast<uint8_t>(EQT::COINTYPE_CP), static_cast<uint8_t>(EQT::COINTYPE_SP), 30});
    h.eq.ProcessBridgeIntents();

    auto mcs = h.sentMoveCoins();
    ASSERT_EQ(mcs.size(), 1u);
    EXPECT_EQ(mcs[0].from_slot, EQT::COINSLOT_BANK);
    EXPECT_EQ(mcs[0].to_slot, EQT::COINSLOT_BANK);
    EXPECT_EQ(mcs[0].cointype1, 0u);
    EXPECT_EQ(mcs[0].cointype2, 1u);
    EXPECT_EQ(mcs[0].amount, 30);
    // 30 cp * (1/10) = 3 sp -- eqemu: amount_to_add = take * coppers(from)/coppers(to)
    EXPECT_EQ(h.eq.m_bank_copper, 0u);
    EXPECT_EQ(h.eq.m_bank_silver, 4u);
    EXPECT_EQ(h.eq.GetGameState().player().bankSilver(), 4u);
    EXPECT_EQ(h.eq.GetGameState().player().bankCopper(), 0u);
    auto [cur, bank] = h.currencyEvents();
    EXPECT_EQ(cur, 0);
    EXPECT_EQ(bank, 1);
}

TEST(BankCoinIntent, ConvertDown_TwoStepsIsHundredToOne) {
    // pp -> sp: coppers(3)/coppers(1) = 1000/10 = 100 sp per pp.
    BankCoinHarness h;
    h.setPools(0, 0, 0, 0, 2, 0, 0, 0);
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyConvertIntent{
        static_cast<uint8_t>(EQT::COINTYPE_PP), static_cast<uint8_t>(EQT::COINTYPE_SP), 2});
    h.eq.ProcessBridgeIntents();

    auto mcs = h.sentMoveCoins();
    ASSERT_EQ(mcs.size(), 1u);
    EXPECT_EQ(mcs[0].cointype1, 3u);
    EXPECT_EQ(mcs[0].cointype2, 1u);
    EXPECT_EQ(mcs[0].amount, 2);
    EXPECT_EQ(h.eq.m_bank_platinum, 0u);
    EXPECT_EQ(h.eq.m_bank_silver, 200u);
}

TEST(BankCoinIntent, ConvertUp_NotAMultiple_SendsNothing) {
    // eqemu would take 10 of the 15 and leave 5 -- the amount on the wire
    // would not be what happened. Refused here; the command layer refuses
    // it earlier with a message.
    BankCoinHarness h;
    h.setPools(0, 0, 0, 0, 0, 0, 0, 15);
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyConvertIntent{
        static_cast<uint8_t>(EQT::COINTYPE_CP), static_cast<uint8_t>(EQT::COINTYPE_SP), 15});
    h.eq.ProcessBridgeIntents();

    EXPECT_TRUE(h.sentPackets().empty());
    EXPECT_EQ(h.eq.m_bank_copper, 15u);
    EXPECT_EQ(h.eq.m_bank_silver, 0u);
}

TEST(BankCoinIntent, Convert_PoolLacksCoins_SendsNothing) {
    BankCoinHarness h;
    h.setPools(0, 0, 0, 0, 0, 0, 0, 20);
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyConvertIntent{
        static_cast<uint8_t>(EQT::COINTYPE_CP), static_cast<uint8_t>(EQT::COINTYPE_SP), 30});
    h.eq.ProcessBridgeIntents();

    EXPECT_TRUE(h.sentPackets().empty());
    EXPECT_EQ(h.eq.m_bank_copper, 20u);
}

TEST(BankCoinIntent, Convert_SameCoinType_SendsNothing) {
    BankCoinHarness h;
    h.setPools(0, 0, 0, 0, 0, 0, 0, 20);
    h.eq.OpenBankWindow(kBankerId);
    h.bridge.drainEvents();
    h.clearSent();

    h.bridge.pushIntent(eqt::events::BankCurrencyConvertIntent{
        static_cast<uint8_t>(EQT::COINTYPE_CP), static_cast<uint8_t>(EQT::COINTYPE_CP), 10});
    h.eq.ProcessBridgeIntents();

    EXPECT_TRUE(h.sentPackets().empty());
    EXPECT_EQ(h.eq.m_bank_copper, 20u);
}

}  // namespace
