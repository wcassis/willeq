// T379 (BOARD B146): the login-stream and world-stream senders go out through
// eqt::PacketSender, not through a raw m_login_connection / m_world_connection
// dereference.
//
// The twelve senders in src/client/login_world/login_world_component.cpp each
// ended in
//
//     if (m_world_connection) { m_world_connection->QueuePacket(p); }
//     else { ctx_.net.NoteSuppressedSend(); }
//
// which is null-SAFE (unlike the B140 zone senders T376 fixed) but completely
// invisible to the two observers the client has:
//
//   1. the R01 session recorder -- EQT::Net::SessionStream has had Login and
//      World members since Batch R, and NOTHING ever wrote a ClientToServer
//      record on either, so a --record-session .sleq of a fresh login held the
//      SERVER's half of the handshake and none of the client's;
//   2. the T268 ordered-capture seam, which lives inside PacketSender and so
//      never saw a login or world packet at all.
//
// Both halves are pinned below. Pre-fix (the twelve sites unrouted) every
// assertion on either observer reads zero.
//
// No network, no graphics, no eqemu: the connections are real
// DaybreakConnections over FakeUdpTransport, exactly as the sibling
// test_click_senders_packet_sender.cpp (T376) builds them. The senders are
// private members of eqt::login_world::LoginWorldComponent, so this file uses
// the same T17 `#define private public` idiom that file does -- eq.h includes
// login_world_component.h, so the one wrap opens both.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/eq_opcodes.h"
#include "client/login_world/login_world_component.h"
#include "common/net/daybreak_connection.h"
#include "common/net/session_log.h"
#include "common/net/session_recorder.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace zonetest;

namespace fs = std::filesystem;

namespace {

struct RecordedPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> bytes;  // body only (no opcode prefix)
};

// Reads a .sleq back and returns its ClientToServer records on ONE stream, in
// file order. Same reader as T376's RecordedClientToServer, parameterised by
// stream because this unit is about the two streams nothing ever wrote.
std::vector<RecordedPacket> RecordedClientToServer(const fs::path& path,
                                                   EQT::Net::SessionStream stream) {
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
        if (rec.stream != static_cast<uint8_t>(stream)) continue;
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

// One client with, optionally, a live login connection and a live world
// connection installed on LoginWorldComponent. `withConnections=false` is the
// point of the null cases: the senders must refuse, not dereference.
class HandshakeHarness {
public:
    explicit HandshakeHarness(bool withConnections) : eq(MakeEq()) {
        if (withConnections) {
            login_manager = MakeManager(&login_c2s, &login_s2c, login_conn);
            world_manager = MakeManager(&world_c2s, &world_s2c, world_conn);
            EXPECT_NE(login_conn, nullptr);
            EXPECT_NE(world_conn, nullptr);
            // private: T17 idiom. These are the exact members
            // PacketSender::BindLoginWorldStreams took the addresses of in
            // EverQuest's constructor, so writing them here is what a real
            // LoginOnNewConnection / WorldOnNewConnection does.
            eq.LoginWorld().m_login_connection = login_conn;
            eq.LoginWorld().m_world_connection = world_conn;
        }
        eq.setBridge(&bridge);
        // WorldSendClientAuth serialises these two into OP_SendLoginInfo.
        eq.LoginWorld().m_dbid = 4242;
        eq.LoginWorld().m_key = "T379KEY";
    }

    ~HandshakeHarness() {
        eq.LoginWorld().m_login_connection.reset();
        eq.LoginWorld().m_world_connection.reset();
        eq.setBridge(nullptr);
    }

    void StartRecording(const fs::path& path) {
        eq.m_session_recorder = std::make_unique<EQT::Net::SessionRecorder>();
        ASSERT_TRUE(eq.m_session_recorder->open(path.string(), "Testchar", "qeynos"))
            << "could not open " << path.string();
    }

    void StopRecording() {
        if (eq.m_session_recorder) eq.m_session_recorder->close();
    }

    // The four login-stream senders, in handshake order.
    void SendLoginHandshake() {
        eq.LoginWorld().LoginSendSessionReady();
        eq.LoginWorld().LoginSendLogin();
        eq.LoginWorld().LoginSendServerRequest();
        eq.LoginWorld().LoginSendPlayRequest(1);
    }

    // Five of the eight world-stream senders (the sixth, WorldSendWorldClientCRC,
    // sends two; the eighth is the inline OP_AckPacket inside
    // WorldProcessExpansionInfo).
    void SendWorldHandshake() {
        eq.LoginWorld().WorldSendClientAuth();
        eq.LoginWorld().WorldSendApproveWorld();
        eq.LoginWorld().WorldSendWorldClientCRC();
        eq.LoginWorld().WorldSendWorldClientReady();
        eq.LoginWorld().WorldSendEnterWorld("Testchar");
        eq.LoginWorld().WorldSendWorldComplete();
    }

    EQ::Test::FakeChannel login_c2s, login_s2c, world_c2s, world_s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> login_manager, world_manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> login_conn, world_conn;
    TestBridge bridge;
    EverQuest eq;

private:
    static std::unique_ptr<EQ::Net::DaybreakConnectionManager> MakeManager(
        EQ::Test::FakeChannel* c2s, EQ::Test::FakeChannel* s2c,
        std::shared_ptr<EQ::Net::DaybreakConnection>& out) {
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        auto m = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(c2s, s2c));
        m->OnNewConnection([&out](std::shared_ptr<EQ::Net::DaybreakConnection> c) { out = c; });
        m->Connect("peer", 12345);
        return m;
    }
};

// ---------------------------------------------------------------------
// (a) A missing connection is a logged refusal on both streams.
// ---------------------------------------------------------------------

TEST(LoginWorldSendersPacketSender, NoLoginConnection_SendersReturnWithoutCrashing) {
    HandshakeHarness h(/*withConnections=*/false);
    ASSERT_EQ(h.eq.LoginWorld().m_login_connection, nullptr);
    const uint64_t before = h.eq.Net().ReplaySuppressedSendCount();

    h.SendLoginHandshake();

    // Every refusal bumps the replay-suppression counter, which is what the
    // twelve call sites used to do in their own else branch -- the contract
    // moved into PacketSender, it did not disappear.
    EXPECT_EQ(h.eq.Net().ReplaySuppressedSendCount() - before, 4u)
        << "each of the four login senders must refuse and count, not crash";
}

TEST(LoginWorldSendersPacketSender, NoWorldConnection_SendersReturnWithoutCrashing) {
    HandshakeHarness h(/*withConnections=*/false);
    ASSERT_EQ(h.eq.LoginWorld().m_world_connection, nullptr);
    const uint64_t before = h.eq.Net().ReplaySuppressedSendCount();

    h.SendWorldHandshake();

    // Six calls, seven packets: WorldSendWorldClientCRC sends CRC1 and CRC2.
    EXPECT_EQ(h.eq.Net().ReplaySuppressedSendCount() - before, 7u)
        << "each world send must refuse and count, not crash";
}

// PacketSender's own contract, straight at the entry points: unbound (no
// BindLoginWorldStreams call has happened on a default-constructed sender) is
// as safe as bound-but-down.
TEST(LoginWorldSendersPacketSender, UnboundStreams_QueueLoginAndQueueWorldRefuse) {
    HandshakeHarness h(/*withConnections=*/false);
    // EverQuest's constructor binds the two slots, so unbind them by hand to
    // reach the other null axis.
    h.eq.Net().BindLoginWorldStreams(nullptr, nullptr);

    EQ::Net::DynamicPacket p;
    p.Resize(2);
    p.PutUInt16(0, HC_OP_SessionReady);

    EXPECT_FALSE(h.eq.Net().QueueLogin(p));
    EXPECT_FALSE(h.eq.Net().QueueWorld(p));
}

// ---------------------------------------------------------------------
// (b) The T268 seam sees the whole handshake, in order.
// ---------------------------------------------------------------------

TEST(LoginWorldSendersPacketSender, OrderedCaptureSeam_SeesTheLoginAndWorldHandshake) {
    HandshakeHarness h(/*withConnections=*/true);
    h.eq.Test_SetCaptureQueuedPacketsForSeam(true);
    h.eq.Test_ClearQueuedPackets();

    h.SendLoginHandshake();
    h.SendWorldHandshake();

    std::vector<uint16_t> opcodes;
    for (const auto& p : h.eq.Test_GetQueuedPackets()) {
        opcodes.push_back(p.opcode);
    }

    const std::vector<uint16_t> expected = {
        HC_OP_SessionReady, HC_OP_Login, HC_OP_ServerListRequest, HC_OP_PlayEverquestRequest,
        HC_OP_SendLoginInfo, HC_OP_ApproveWorld, HC_OP_World_Client_CRC1,
        HC_OP_World_Client_CRC2, HC_OP_WorldClientReady, HC_OP_EnterWorld,
        HC_OP_WorldComplete,
    };
    EXPECT_EQ(opcodes, expected)
        << "the login/world senders must reach the T268 ordered-capture seam; a "
           "sender that calls connection->QueuePacket() directly bypasses it "
           "(BOARD B146)";
}

// ---------------------------------------------------------------------
// (c) A recorded session contains the CLIENT's half of the handshake.
// ---------------------------------------------------------------------

TEST(LoginWorldSendersPacketSender, RecordedSession_ContainsTheClientLoginHandshake) {
    const fs::path log = fs::temp_directory_path() / "t379_login_recorded.sleq";
    fs::remove(log);

    {
        HandshakeHarness h(/*withConnections=*/true);
        h.StartRecording(log);
        h.SendLoginHandshake();
        h.StopRecording();
    }

    auto recorded = RecordedClientToServer(log, EQT::Net::SessionStream::Login);
    EXPECT_EQ(CountOpcode(recorded, HC_OP_SessionReady), 1u)
        << "OP_SessionReady must reach the session recorder on SessionStream::Login; "
           "before T379 nothing ever wrote a ClientToServer record on that stream "
           "(BOARD B146)";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_Login), 1u) << "OP_Login, same bypass";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_ServerListRequest), 1u)
        << "OP_ServerListRequest, same bypass";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_PlayEverquestRequest), 1u)
        << "OP_PlayEverquestRequest, same bypass";

    // And the bodies survived: the login sequence counter is the first four
    // payload bytes of every login-stream packet. It starts at 2 (the value
    // LoginWorldComponent::m_login_sequence is initialised to, matching the
    // official client's first SessionReady) and increments once per send, so
    // the four records must carry 2, 3, 4, 5 IN THAT ORDER -- which also pins
    // that the recorder wrote them in send order.
    ASSERT_EQ(recorded.size(), 4u);
    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_GE(recorded[i].bytes.size(), 4u);
        uint32_t seq = recorded[i].bytes[0] | (recorded[i].bytes[1] << 8) |
                       (recorded[i].bytes[2] << 16) | (recorded[i].bytes[3] << 24);
        EXPECT_EQ(seq, 2u + i) << "login sequence at record " << i;
    }
    fs::remove(log);
}

TEST(LoginWorldSendersPacketSender, RecordedSession_ContainsTheClientWorldHandshake) {
    const fs::path log = fs::temp_directory_path() / "t379_world_recorded.sleq";
    fs::remove(log);

    {
        HandshakeHarness h(/*withConnections=*/true);
        h.StartRecording(log);
        h.SendWorldHandshake();
        h.StopRecording();
    }

    auto recorded = RecordedClientToServer(log, EQT::Net::SessionStream::World);
    EXPECT_EQ(CountOpcode(recorded, HC_OP_SendLoginInfo), 1u)
        << "OP_SendLoginInfo must reach the session recorder on SessionStream::World "
           "(BOARD B146)";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_EnterWorld), 1u) << "OP_EnterWorld, same bypass";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_ApproveWorld), 1u) << "OP_ApproveWorld, same bypass";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_World_Client_CRC1), 1u) << "OP_World_Client_CRC1";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_World_Client_CRC2), 1u) << "OP_World_Client_CRC2";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_WorldClientReady), 1u) << "OP_WorldClientReady";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_WorldComplete), 1u) << "OP_WorldComplete";

    // The body survived: OP_EnterWorld carries the character name at payload
    // offset 0, and OP_SendLoginInfo the account id as decimal text.
    for (const auto& p : recorded) {
        if (p.opcode == HC_OP_EnterWorld) {
            ASSERT_GE(p.bytes.size(), 8u);
            EXPECT_STREQ(reinterpret_cast<const char*>(p.bytes.data()), "Testchar");
        }
        if (p.opcode == HC_OP_SendLoginInfo) {
            ASSERT_GE(p.bytes.size(), 16u);
            EXPECT_STREQ(reinterpret_cast<const char*>(p.bytes.data()), "4242");
        }
    }
    fs::remove(log);
}

// ---------------------------------------------------------------------
// (d) Census pin for this TU: no direct sender may come back.
// ---------------------------------------------------------------------

// T379's close condition, as a grep the suite enforces. login_world_component.cpp
// is the ONLY TU that ever sent on the login or world stream; every send in it
// must be a PacketSender call.
TEST(LoginWorldSendersPacketSender, CensusPin_NoDirectQueuePacketInTheLoginWorldTU) {
    const fs::path src = fs::path(WILLEQ_SOURCE_DIR) /
        "src/client/login_world/login_world_component.cpp";
    std::ifstream in(src);
    ASSERT_TRUE(in.is_open()) << "cannot open " << src.string();

    std::vector<int> offenders;
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 2, "//") == 0) continue;
        if (line.find("->QueuePacket(") != std::string::npos) offenders.push_back(n);
    }

    EXPECT_TRUE(offenders.empty())
        << "login_world_component.cpp must send only through eqt::PacketSender "
           "(ctx_.net.QueueLogin / ctx_.net.QueueWorld); a direct "
           "connection->QueuePacket() is invisible to the R01 recorder and the "
           "T268 seam (BOARD B146). First offending line: "
        << (offenders.empty() ? 0 : offenders.front());
}

}  // namespace
