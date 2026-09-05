// T378 (BOARD B145): the zone-stream direct senders go out through
// eqt::PacketSender::SafeQueueZone, not through a raw
// ctx_.zone_connection->QueuePacket() deref.
//
// T376's census found 70 such sites in twelve files (69 at this tip -- T375
// routed one of player_misc.cpp's on its way past). Each one has the two
// consequences T360 (B068) and T376 (B140) already fixed for their own
// handful of senders:
//
//   1. TEN of them are an UNGUARDED shared_ptr dereference -- there is no
//      `ctx_.zone_connection` test anywhere in the enclosing function -- so a
//      null zone connection is a segfault, not a logged refusal;
//   2. all of them are invisible to the R01 session recorder and to the T268
//      ordered-capture seam, both of which live INSIDE PacketSender. A
//      --record-session .sleq is therefore a false negative for every chat
//      line, group invite, jump, merchant move, GM command, zone-handshake
//      step and petition the client sends.
//
// This file pins both halves, one case pair per family:
//   (a) NoZoneConnection_<Family> calls every routed sender in that family
//       with m_zone_connection null and must not crash;
//   (b) RecordedSession_<Family> opens a real SessionRecorder onto a temp
//       .sleq, sends the family's most user-visible packet, closes, and reads
//       the file back off disk. Pre-fix that count is ZERO.
//
// No network, no graphics, no eqemu. Several senders gate on
// IsFullyZonedIn(), whose third term (EverQuest::m_update_running) has no
// public setter, and several of the zone-handshake senders are private, so
// this file uses the T17 `#define private public` idiom its sibling
// sender tests use.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/chat/chat_component.h"
#include "client/gm/gm_component.h"
#include "client/group/group_component.h"
#include "client/merchant/merchant_component.h"
#include "client/movement/movement_component.h"
#include "client/player/player_component.h"
#include "client/zone/zone_component.h"
#include "common/net/daybreak_connection.h"
#include "common/net/session_log.h"
#include "common/net/session_recorder.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace zonetest;

namespace fs = std::filesystem;

namespace {

struct RecordedPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> bytes;  // body only (no opcode prefix)
};

// Reads a .sleq back and returns its C->S ZONE records in file order.
// Same reader as T376's ClickSendersPacketSender / T360's DeathHarness.
std::vector<RecordedPacket> RecordedClientToServer(const fs::path& path) {
    std::vector<RecordedPacket> out;
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << "no session log at " << path.string();
    EQT::Net::SessionLogFileHeader fh{};
    in.read(reinterpret_cast<char*>(&fh), sizeof(fh));
    EXPECT_EQ(fh.magic, EQT::Net::SESSION_LOG_MAGIC);
    while (in) {
        EQT::Net::SessionLogRecordHeader rec{};
        in.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!in) break;
        std::vector<uint8_t> body(rec.size);
        if (rec.size) in.read(reinterpret_cast<char*>(body.data()), rec.size);
        if (!in && rec.size) break;
        if (rec.stream != static_cast<uint8_t>(EQT::Net::SessionStream::Zone)) continue;
        if (rec.direction != static_cast<uint8_t>(EQT::Net::SessionDirection::ClientToServer))
            continue;
        out.push_back(RecordedPacket{rec.opcode, std::move(body)});
    }
    return out;
}

size_t CountOpcode(const std::vector<RecordedPacket>& v, uint16_t opcode) {
    size_t n = 0;
    for (const auto& p : v) {
        if (p.opcode == opcode) ++n;
    }
    return n;
}

// A fully-zoned-in client. `withConnection` chooses whether a real
// DaybreakConnection over FakeUdpTransport is installed as m_zone_connection
// -- the point of the (a) cases is that there is NOT one.
class SenderHarness {
public:
    explicit SenderHarness(bool withConnection) : eq(MakeEq()) {
        // The connection MANAGER is installed either way. Several senders
        // (the whole group family) gate on `ctx_.zone_connection_manager`
        // rather than on `ctx_.zone_connection`, so a harness without one
        // never reaches the send and the (a) cases below would be green
        // pre-fix for the wrong reason. A live manager with a dropped
        // connection is also the real shape of the bug.
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        auto mgr = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
        mgr->OnNewConnection(
            [this](std::shared_ptr<EQ::Net::DaybreakConnection> c) { conn = c; });
        mgr->Connect("peer", 12345);
        EXPECT_NE(conn, nullptr);
        // SafeQueueZone refuses a connection whose stats are all zero
        // ("appears invalid"); a FakeUdpTransport connection that has never
        // round-tripped looks exactly like that (T360's note, re-found by
        // T376).
        conn->m_stats.sent_packets = 1;
        eq.m_zone_connection_manager = std::move(mgr);  // private: T17 idiom
        if (withConnection) {
            eq.m_zone_connection = conn;
        }

        eq.setBridge(&bridge);
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        eq.m_update_running = true;
    }

    ~SenderHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    // SendGroupDisband refuses when not grouped and SendPetCommand refuses
    // without a pet; both must reach their send for the (a) case to mean
    // anything.
    void InGroup() {
        eq.GetGameState().group().setInGroup(true);
    }
    void WithPet() {
        eq.GetGameState().pet().setPet(42, "Gob", 1);
    }

    void StartRecording(const fs::path& path) {
        eq.m_session_recorder = std::make_unique<EQT::Net::SessionRecorder>();
        ASSERT_TRUE(eq.m_session_recorder->open(path.string(), "Testchar", "qeynos"))
            << "could not open " << path.string();
    }

    void StopRecording() {
        if (eq.m_session_recorder) eq.m_session_recorder->close();
    }

    EQ::Test::FakeChannel c2s, s2c;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    TestBridge bridge;
    EverQuest eq;
};

// Runs `body` against a recorder writing to `name`, then hands the recorded
// C->S zone stream to `check`. Keeps every family's (b) case to two lines.
void WithRecordedSession(const char* name,
                         const std::function<void(SenderHarness&)>& body,
                         const std::function<void(const std::vector<RecordedPacket>&)>& check) {
    const fs::path log = fs::temp_directory_path() / name;
    fs::remove(log);
    {
        SenderHarness h(/*withConnection=*/true);
        h.StartRecording(log);
        body(h);
        h.StopRecording();
    }
    check(RecordedClientToServer(log));
    fs::remove(log);
}

// =====================================================================
// chat -- ChatComponent::ZoneSendChannelMessage / ZoneSendEmote
// Both are the B140 crash shape: no ctx_.zone_connection test anywhere in
// either function (the null test lives in the higher-level SendChatMessage,
// which the action layer is not the only caller of).
// =====================================================================

TEST(DirectSendersPacketSender, NoZoneConnection_Chat) {
    SenderHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);

    h.eq.Chat().ZoneSendChannelMessage("hello", CHAT_CHANNEL_SAY, "");
    h.eq.Chat().ZoneSendEmote("waves");

    SUCCEED() << "the chat senders survived a null zone connection";
}

TEST(DirectSendersPacketSender, RecordedSession_Chat) {
    WithRecordedSession(
        "t378_chat_recorded.sleq",
        [](SenderHarness& h) {
            h.eq.Chat().ZoneSendChannelMessage("hello", CHAT_CHANNEL_SAY, "");
            h.eq.Chat().ZoneSendEmote("waves");
        },
        [](const std::vector<RecordedPacket>& recorded) {
            EXPECT_EQ(CountOpcode(recorded, HC_OP_ChannelMessage), 1u)
                << "ZoneSendChannelMessage must reach the session recorder; a "
                   "sender that calls zone_connection->QueuePacket() directly "
                   "is invisible to it (BOARD B145)";
            EXPECT_EQ(CountOpcode(recorded, HC_OP_Emote), 1u)
                << "ZoneSendEmote, same bypass, same fix";
        });
}

// =====================================================================
// group -- GroupComponent::SendGroupInvite / SendGroupFollow /
// SendGroupDecline / SendGroupDisband / SendPetCommand / SendRaidCommand
// Invite, Disband and PetCommand are the B140 crash shape: their guards test
// ctx_.zone_connection_manager and IsFullyZonedIn() and never the connection
// itself. Follow and Decline each wrapped the send in a bare
// `if (ctx_.zone_connection)` that guarded nothing else, so the wrapper goes
// with the routing; SendRaidCommand keeps its early return, which also tests
// isZoneConnected() and logs.
// =====================================================================

TEST(DirectSendersPacketSender, NoZoneConnection_Group) {
    SenderHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);
    ASSERT_NE(h.eq.m_zone_connection_manager, nullptr);
    h.InGroup();
    h.WithPet();

    h.eq.Group().SendGroupInvite("Someone");
    h.eq.Group().SendGroupFollow("Someone");
    h.eq.Group().SendGroupDecline("Someone");
    h.eq.Group().SendGroupDisband();
    h.eq.Group().SendPetCommand(EQT::PET_ATTACK, 0);
    h.eq.Group().SendRaidCommand(0, "Someone", "Someone", 0);

    SUCCEED() << "the group senders survived a null zone connection";
}

TEST(DirectSendersPacketSender, RecordedSession_Group) {
    WithRecordedSession(
        "t378_group_recorded.sleq",
        [](SenderHarness& h) { h.eq.Group().SendGroupInvite("Someone"); },
        [](const std::vector<RecordedPacket>& recorded) {
            EXPECT_EQ(CountOpcode(recorded, HC_OP_GroupInvite), 1u)
                << "SendGroupInvite must reach the session recorder; a sender "
                   "that calls zone_connection->QueuePacket() directly is "
                   "invisible to it (BOARD B145)";
        });
}

// =====================================================================
// movement -- MovementComponent::SendJump
// The B140 crash shape: the only guard is IsFullyZonedIn(), which says
// nothing about the connection pointer.
// =====================================================================

TEST(DirectSendersPacketSender, NoZoneConnection_Movement) {
    SenderHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);
    ASSERT_TRUE(h.eq.IsFullyZonedIn());

    h.eq.Movement().SendJump();

    SUCCEED() << "SendJump survived a null zone connection";
}

TEST(DirectSendersPacketSender, RecordedSession_Movement) {
    WithRecordedSession(
        "t378_movement_recorded.sleq",
        [](SenderHarness& h) { h.eq.Movement().SendJump(); },
        [](const std::vector<RecordedPacket>& recorded) {
            EXPECT_EQ(CountOpcode(recorded, HC_OP_Jump), 1u)
                << "SendJump must reach the session recorder; a sender that "
                   "calls zone_connection->QueuePacket() directly is invisible "
                   "to it (BOARD B145)";
        });
}

// =====================================================================
// player -- PlayerComponent, 25 senders across four TUs:
//   player_component.cpp  SendCorpseDrag SendCorpseDrop SendConsiderCorpse
//                         SendConsent SendConsentDeny
//   player_state.cpp      SendTradeSkillCombine
//   player_buffs.cpp      SendBuffCancelRequest SendApplyPoison
//   player_misc.cpp       SendAssist SendAssistGroup SendSplit
//                         SendLFGCommand SendShielding SendDuelRequest
//                         SendDuelAccept SendDuelDecline SendBindWound
//                         SendTrackTarget SendSurname SendFaceChange
//                         SendRandom SendFindPerson SendRewind
//                         SendYellForHelp SendFriendsWho
// SendApplyPoison and SendTradeSkillCombine are the B140 crash shape (their
// only guard is IsFullyZonedIn()); the other 23 sit inside an
// `if (ctx_.zone_connection && isZoneConnected())` that also gates logging,
// so those wrappers stay and only the send moves.
// =====================================================================

TEST(DirectSendersPacketSender, NoZoneConnection_Player) {
    SenderHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);
    ASSERT_TRUE(h.eq.IsFullyZonedIn());

    auto& p = h.eq.Player();
    p.SendCorpseDrag("Testchar's corpse");
    p.SendCorpseDrop();
    p.SendConsiderCorpse(7);
    p.SendConsent("Someone");
    p.SendConsentDeny("Someone");
    p.SendTradeSkillCombine(262);
    p.SendBuffCancelRequest(0);
    p.SendApplyPoison(22);
    p.SendAssist(9);
    p.SendAssistGroup(9);
    p.SendSplit(1, 2, 3, 4);
    p.SendLFGCommand(true);
    p.SendShielding(9);
    p.SendDuelRequest(9);
    p.SendDuelAccept(9);
    p.SendDuelDecline(9);
    p.SendBindWound(9);
    p.SendTrackTarget(9);
    p.SendSurname("Thepoor");
    p.SendFaceChange(1, 2, 3, 4, 5, 6, 7);
    p.SendRandom(1, 100);
    p.SendFindPerson(9);
    p.SendRewind();
    p.SendYellForHelp();
    p.SendFriendsWho("Someone");

    SUCCEED() << "the 25 player senders survived a null zone connection";
}

TEST(DirectSendersPacketSender, RecordedSession_Player) {
    WithRecordedSession(
        "t378_player_recorded.sleq",
        [](SenderHarness& h) {
            h.eq.Player().SendRandom(1, 100);
            h.eq.Player().SendApplyPoison(22);
        },
        [](const std::vector<RecordedPacket>& recorded) {
            EXPECT_EQ(CountOpcode(recorded, HC_OP_RandomReq), 1u)
                << "SendRandom (/random) must reach the session recorder; a "
                   "sender that calls zone_connection->QueuePacket() directly "
                   "is invisible to it (BOARD B145)";
            EXPECT_EQ(CountOpcode(recorded, HC_OP_ApplyPoison), 1u)
                << "SendApplyPoison, one of the ten crash-shape sites, same "
                   "bypass, same fix";
        });
}

// =====================================================================
// zone handshake -- ZoneComponent, 12 senders:
//   ZoneSendZoneEntry ZoneSendReqClientSpawn ZoneSendClientReady
//   ZoneSendSetServerFilter ZoneSendStreamIdentify ZoneSendAckPacket
//   ZoneSendReqNewZone ZoneSendSendAATable ZoneSendUpdateAA
//   ZoneSendSendTributes ZoneSendRequestGuildTributes ZoneSendSendExpZonein
// ZoneSendStreamIdentify is the B140 crash shape -- the only one of the
// twelve with no connection test at all. The other eleven keep their
// if/else, because the else arm bumps the replay-suppressed-send counter,
// which SafeQueueZone does NOT do: that wrapper guards something else.
// =====================================================================

TEST(DirectSendersPacketSender, NoZoneConnection_ZoneHandshake) {
    SenderHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);

    auto& z = h.eq.Zone();
    z.ZoneSendStreamIdentify();
    z.ZoneSendAckPacket();
    z.ZoneSendZoneEntry();
    z.ZoneSendReqClientSpawn();
    z.ZoneSendClientReady();
    z.ZoneSendSetServerFilter();
    z.ZoneSendReqNewZone();
    z.ZoneSendSendAATable();
    z.ZoneSendUpdateAA();
    z.ZoneSendSendTributes();
    z.ZoneSendRequestGuildTributes();
    z.ZoneSendSendExpZonein();

    SUCCEED() << "the 12 zone-handshake senders survived a null zone connection";
}

// The eleven guarded senders must still bump the suppressed-send counter when
// there is no connection -- routing must not silently retire that count.
TEST(DirectSendersPacketSender, NoZoneConnection_ZoneHandshakeStillCountsSuppressedSends) {
    SenderHarness h(/*withConnection=*/false);
    const uint64_t before = h.eq.Net().ReplaySuppressedSendCount();

    h.eq.Zone().ZoneSendClientReady();
    h.eq.Zone().ZoneSendReqNewZone();

    EXPECT_EQ(h.eq.Net().ReplaySuppressedSendCount(), before + 2)
        << "the else arm of the zone-handshake send guard is the only writer "
           "of the replay-suppressed-send counter; SafeQueueZone's own null "
           "guard does not bump it, so that wrapper must survive the routing";
}

TEST(DirectSendersPacketSender, RecordedSession_ZoneHandshake) {
    WithRecordedSession(
        "t378_zone_recorded.sleq",
        [](SenderHarness& h) {
            h.eq.Zone().ZoneSendStreamIdentify();
            h.eq.Zone().ZoneSendClientReady();
            h.eq.Zone().ZoneSendReqNewZone();
        },
        [](const std::vector<RecordedPacket>& recorded) {
            EXPECT_EQ(CountOpcode(recorded, HC_OP_ClientReady), 1u)
                << "ZoneSendClientReady must reach the session recorder; a "
                   "sender that calls zone_connection->QueuePacket() directly "
                   "is invisible to it (BOARD B145)";
            EXPECT_EQ(CountOpcode(recorded, HC_OP_ReqNewZone), 1u)
                << "ZoneSendReqNewZone, same bypass, same fix";
            // ZoneSendStreamIdentify sends the ZoneEntry opcode with an empty
            // body -- it is the crash-shape one of the twelve.
            EXPECT_EQ(CountOpcode(recorded, HC_OP_ZoneEntry), 1u)
                << "ZoneSendStreamIdentify, the unguarded one, same fix";
        });
}

// =====================================================================
// merchant -- MerchantComponent, 13 senders across two TUs:
//   merchant.cpp              SendMoveItem SendDeleteItem
//                             ScribeSpellFromScroll SendTradeRequest
//                             SendTradeRequestAck SendMoveCoin
//                             SendTradeAcceptClick SendCancelTrade
//                             SendReadBookRequest
//   merchant_vendor_bank.cpp  SendRecipesFavorite SendRecipesSearch
//                             SendRecipeDetails SendRecipeAutoCombine
// None of these is a crash-shape site -- every one already returns early or
// skips the send on a null connection -- so the (a) case below is green in
// both directions and is here as the regression net. The (b) case is the
// finding: all thirteen were invisible to the recorder.
// =====================================================================

TEST(DirectSendersPacketSender, NoZoneConnection_Merchant) {
    SenderHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);

    auto& m = h.eq.Merchant();
    EQT::TradeRequest_Struct req{};
    EQT::TradeRequestAck_Struct ack{};
    EQT::MoveCoin_Struct coin{};
    EQT::TradeAcceptClick_Struct accept{};
    EQT::CancelTrade_Struct cancel{};
    m.SendMoveItem(30, 31, 0);
    m.SendDeleteItem(30);
    m.ScribeSpellFromScroll(1, 0, 30);
    m.SendTradeRequest(req);
    m.SendTradeRequestAck(ack);
    m.SendMoveCoin(coin);
    m.SendTradeAcceptClick(accept);
    m.SendCancelTrade(cancel);
    m.SendReadBookRequest(0, 0, "book");
    m.SendRecipesFavorite(15, std::vector<uint32_t>{1, 2});
    m.SendRecipesSearch(15, "iron", 0, 300);
    m.SendRecipeDetails(1);
    m.SendRecipeAutoCombine(15, 1);

    SUCCEED() << "the 13 merchant senders survived a null zone connection";
}

TEST(DirectSendersPacketSender, RecordedSession_Merchant) {
    WithRecordedSession(
        "t378_merchant_recorded.sleq",
        [](SenderHarness& h) {
            h.eq.Merchant().SendMoveItem(30, 31, 0);
            h.eq.Merchant().SendRecipeDetails(1);
        },
        [](const std::vector<RecordedPacket>& recorded) {
            EXPECT_EQ(CountOpcode(recorded, HC_OP_MoveItem), 1u)
                << "SendMoveItem -- every inventory drag in the client -- must "
                   "reach the session recorder; a sender that calls "
                   "zone_connection->QueuePacket() directly is invisible to it "
                   "(BOARD B145)";
            EXPECT_EQ(CountOpcode(recorded, HC_OP_RecipeDetails), 1u)
                << "SendRecipeDetails, the second merchant TU, same fix";
        });
}

// =====================================================================
// gm -- GmComponent, 9 senders:
//   SendGMZoneRequest SendGMSummon SendGMGoto SendGMFind SendGMKick
//   SendGMKill SendGMHideMe SendGMEmoteZone SendGMLastName
// All nine sit inside an if (ctx_.zone_connection && isZoneConnected()) that
// also gates their log line, so none is a crash-shape site and all nine
// wrappers stay; only the send moves. The recorder was blind to every GM
// command the client sent.
// =====================================================================

TEST(DirectSendersPacketSender, NoZoneConnection_Gm) {
    SenderHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);

    auto& g = h.eq.Gm();
    g.SendGMZoneRequest("Someone", 1);
    g.SendGMSummon("Someone");
    g.SendGMGoto("Someone");
    g.SendGMFind("Someone");
    g.SendGMKick("Someone");
    g.SendGMKill("Someone");
    g.SendGMHideMe(true);
    g.SendGMEmoteZone("hello");
    g.SendGMLastName("Someone", "Thegm");

    SUCCEED() << "the 9 GM senders survived a null zone connection";
}

TEST(DirectSendersPacketSender, RecordedSession_Gm) {
    WithRecordedSession(
        "t378_gm_recorded.sleq",
        [](SenderHarness& h) {
            h.eq.Gm().SendGMSummon("Someone");
            h.eq.Gm().SendGMHideMe(true);
        },
        [](const std::vector<RecordedPacket>& recorded) {
            EXPECT_EQ(CountOpcode(recorded, HC_OP_GMSummon), 1u)
                << "SendGMSummon must reach the session recorder; a sender "
                   "that calls zone_connection->QueuePacket() directly is "
                   "invisible to it (BOARD B145)";
            EXPECT_EQ(CountOpcode(recorded, HC_OP_GMHideMe), 1u)
                << "SendGMHideMe, same bypass, same fix";
        });
}

// =====================================================================
// core -- EverQuest::SendPetition, the one direct sender left on EverQuest
// itself (src/client/core/everquest.cpp). It reaches PacketSender as
// m_packet_sender, not ctx_.net, because it is not inside a component.
// Guarded by if (m_zone_connection && isZoneConnected()), which also gates
// the log line and the "Your petition has been submitted." chat reply, so
// the wrapper stays.
// =====================================================================

TEST(DirectSendersPacketSender, NoZoneConnection_Core) {
    SenderHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);

    h.eq.SendPetition("stuck in a wall");

    SUCCEED() << "SendPetition survived a null zone connection";
}

TEST(DirectSendersPacketSender, RecordedSession_Core) {
    WithRecordedSession(
        "t378_core_recorded.sleq",
        [](SenderHarness& h) { h.eq.SendPetition("stuck in a wall"); },
        [](const std::vector<RecordedPacket>& recorded) {
            EXPECT_EQ(CountOpcode(recorded, HC_OP_Petition), 1u)
                << "SendPetition must reach the session recorder; a sender "
                   "that calls m_zone_connection->QueuePacket() directly is "
                   "invisible to it (BOARD B145)";
        });
}

}  // namespace
