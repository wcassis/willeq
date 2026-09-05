// T375 (board B023) -- /report (/bug) sends the body eqemu's OP_Report handler
// actually reads.
//
// THE ORACLE. eqemu's Client::Handle_OP_Report
// (/mnt/projects/akk-stack/code/zone/client_packet.cpp, ~13442) is unusual: it
// casts the payload to NO struct and performs NO size check. It walks the bytes
// with a three-state parser -- everything before the first '|' is `reported`,
// everything before the second '|' is `reporter`, and the remainder is the
// report text, with 0x0a translated to '\n' and 0x00 terminating -- and then
// calls database.AddReport(reporter, reported, current_string).
//
// EqemuParseReport() below is that loop transcribed statement for statement, so
// this file asserts against the server's own behaviour rather than against a
// re-derived format. Titanium has no encoder for this opcode (grep OP_Report
// over common/patches finds nothing), so the payload fable queues is byte for
// byte the payload the handler walks.
//
// The pre-T375 body was a 4-byte zero "category_id" followed by an unterminated
// string. Fed to the transcription that yields reported = "\0\0\0\0<text>",
// reporter = "", text = "" -- the row eqemu files loses the report entirely.
//
// Two observers, exactly as T360 (board B068) set up for the death sequence:
//   - the T268 capture seam ("the client decided to send this"), which sees the
//     opcode and the body bytes; and
//   - the R01 session recorder ("this went out through PacketSender"), which a
//     sender calling zone_connection->QueuePacket() directly is invisible to.
// SendReport was such a sender before this unit, so the recorder test is red
// pre-fix for a second, independent reason.
//
// No network, no graphics, no eqemu server.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "client/packet_sender.h"
#include "client/player/player_component.h"
#include "common/net/daybreak_connection.h"
#include "common/net/session_recorder.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace zonetest;

namespace {

namespace fs = std::filesystem;

// The character MakeEq() logs in as.
constexpr const char* kSelfName = "Testchar";

// ---------------------------------------------------------------------------
// eqemu's Handle_OP_Report, transcribed. Quoted in refactor/T375's Review.
// ---------------------------------------------------------------------------
struct ParsedReport {
    std::string reported;
    std::string reporter;
    std::string text;
    bool terminated = false;  // the 0x00 early-return arm was reached
};

ParsedReport EqemuParseReport(const std::vector<uint8_t>& buffer) {
    ParsedReport out;
    const uint32_t size = static_cast<uint32_t>(buffer.size());
    uint32_t current_point = 0;
    std::string current_string;
    int mode = 0;

    while (current_point < size) {
        if (mode < 2) {
            if (buffer[current_point] == '|') {
                mode++;
            } else {
                if (mode == 0) {
                    out.reported += static_cast<char>(buffer[current_point]);
                } else {
                    out.reporter += static_cast<char>(buffer[current_point]);
                }
            }
            current_point++;
        } else {
            if (buffer[current_point] == 0x0a) {
                current_string += '\n';
            } else if (buffer[current_point] == 0x00) {
                out.text = current_string;
                out.terminated = true;
                return out;  // AddReport(reporter, reported, current_string)
            } else {
                current_string += static_cast<char>(buffer[current_point]);
            }
            current_point++;
        }
    }

    out.text = current_string;  // the fall-through AddReport at the bottom
    return out;
}

// ---------------------------------------------------------------------------
// Harness: a real EverQuest with a live-looking zone connection, the T268 seam
// armed. Same construction as test_self_death_sequence.cpp's DeathHarness.
// ---------------------------------------------------------------------------
class SilentBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class ReportHarness {
public:
    ReportHarness() : eq("127.0.0.1", 0, "test", "test", "test", kSelfName) {
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        manager = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
        manager->OnNewConnection([this](std::shared_ptr<EQ::Net::DaybreakConnection> c) { conn = c; });
        manager->Connect("peer", 12345);
        EXPECT_NE(conn, nullptr);

        eq.setBridge(&bridge);
        eq.m_zone_connection = conn;  // private: T17 idiom
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        // SafeQueueZone refuses a connection whose stats are all zero
        // ("appears invalid"); a FakeUdpTransport connection that never
        // round-tripped looks exactly like that (T360's fixture note).
        conn->m_stats.sent_packets = 1;
        eq.Test_SetCaptureQueuedPacketsForSeam(true);
        eq.Test_ClearQueuedPackets();
    }

    ~ReportHarness() {
        if (eq.m_session_recorder) eq.m_session_recorder->close();
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    const std::vector<eqt::PacketSender::TestQueuedPacket>& sentPackets() {
        return eq.Test_GetQueuedPackets();
    }

    // The single OP_Report body the seam captured (opcode stripped).
    std::vector<uint8_t> reportBody() {
        std::vector<std::vector<uint8_t>> hits;
        for (auto& p : sentPackets()) {
            if (p.opcode == HC_OP_Report) hits.push_back(p.bytes);
        }
        EXPECT_EQ(hits.size(), 1u) << "expected exactly one OP_Report";
        return hits.empty() ? std::vector<uint8_t>{} : hits.front();
    }

    void startRecording(const fs::path& path) {
        eq.m_session_recorder = std::make_unique<EQT::Net::SessionRecorder>();
        ASSERT_TRUE(eq.m_session_recorder->open(path.string(), kSelfName, "qeynos"))
            << "could not open " << path.string();
    }

    void stopRecording() {
        if (eq.m_session_recorder) eq.m_session_recorder->close();
    }

    // C->S ZONE records from a .sleq, in file order.
    static std::vector<std::pair<uint16_t, std::vector<uint8_t>>> RecordedClientToServer(
        const fs::path& path) {
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> out;
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
            out.emplace_back(rec.opcode, std::move(body));
        }
        return out;
    }

    EverQuest eq;
    SilentBridge bridge;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    EQ::Test::FakeChannel c2s, s2c;
};

// ---------------------------------------------------------------------------
// 1. The body eqemu reads.
// ---------------------------------------------------------------------------

TEST(OpReportShape, QueuedBodyParsesWithEqemusHandler) {
    ReportHarness h;

    h.eq.Player().SendReport("the boat to Freeport never arrives");

    const std::vector<uint8_t> body = h.reportBody();
    ASSERT_FALSE(body.empty());

    const ParsedReport r = EqemuParseReport(body);
    // AddReport(who = reporter, against = reported, lines = text).
    EXPECT_EQ(r.reporter, kSelfName);
    EXPECT_EQ(r.text, "the boat to Freeport never arrives");
    // /bug reports a defect, not a player: nobody is accused.
    EXPECT_EQ(r.reported, "");
}

// The handler's mode-2 loop ends on 0x00 and on nothing else. Without the
// terminator it falls out of the while() instead, which is the arm eqemu takes
// for a MALFORMED payload -- so assert the early-return arm was the one taken.
TEST(OpReportShape, BodyIsNulTerminatedSoTheHandlerTakesTheTerminatingArm) {
    ReportHarness h;

    h.eq.Player().SendReport("terminator check");

    const std::vector<uint8_t> body = h.reportBody();
    ASSERT_FALSE(body.empty());
    EXPECT_EQ(body.back(), 0u) << "payload must end with the 0x00 the handler stops on";
    EXPECT_TRUE(EqemuParseReport(body).terminated);

    // Two separators, exactly: reported | reporter | text.
    int pipes = 0;
    for (uint8_t b : body) {
        if (b == '|') ++pipes;
    }
    EXPECT_EQ(pipes, 2) << "the handler needs two '|' to reach mode 2";
}

// There is no category field anywhere in Handle_OP_Report. The old body's four
// leading zero bytes made the first four characters of `reported` NUL, which is
// the exact tell this pins.
TEST(OpReportShape, NoCategoryPrefixPrecedesTheText) {
    ReportHarness h;

    h.eq.Player().SendReport("no category id exists");

    const std::vector<uint8_t> body = h.reportBody();
    ASSERT_FALSE(body.empty());
    EXPECT_NE(body[0], 0u) << "a leading NUL is the pre-T375 category_id";
    EXPECT_EQ(body[0], static_cast<uint8_t>('|'))
        << "an empty `reported` field is a leading '|'";
}

// A newline in the description survives: the handler maps 0x0a to '\n' itself.
TEST(OpReportShape, NewlinesInTheDescriptionSurviveTheHandler) {
    ReportHarness h;

    h.eq.Player().SendReport("line one\nline two");

    const ParsedReport r = EqemuParseReport(h.reportBody());
    EXPECT_EQ(r.text, "line one\nline two");
    EXPECT_EQ(r.reporter, kSelfName);
}

// The pre-T375 body, reconstructed byte for byte from the code this unit
// replaced (PutUInt16 opcode, PutUInt32(2, 0) "category_id", PutString(6, text)
// with no terminator), fed to the same transcription. This one passes BOTH
// before and after the fix -- it is not a regression net on SendReport but on
// the oracle transcription above, and it is the concrete statement of what the
// server used to file: an empty reporter, an EMPTY report text, and four NUL
// bytes glued to the front of the accused player's name.
TEST(OpReportShape, ThePreT375BodyLosesTheReportEntirely) {
    const std::string text = "the boat to Freeport never arrives";
    std::vector<uint8_t> old_body(4, 0);  // PutUInt32(2, 0) -- the "category_id"
    old_body.insert(old_body.end(), text.begin(), text.end());  // PutString, no NUL

    const ParsedReport r = EqemuParseReport(old_body);
    EXPECT_EQ(r.reporter, "") << "no '|' is ever found, so mode never leaves 0";
    EXPECT_EQ(r.text, "") << "mode 2 is never entered, so the text is discarded";
    EXPECT_FALSE(r.terminated);
    EXPECT_EQ(r.reported, std::string("\0\0\0\0", 4) + text);
}

// ---------------------------------------------------------------------------
// 2. The command path, through PacketSender, seen by the session recorder.
// ---------------------------------------------------------------------------

TEST(OpReportShape, RecordedSession_SlashBugEmitsExactlyOneOpReport) {
    ReportHarness h;
    eqt::EqActionHandler handler(h.eq);

    const fs::path log = fs::temp_directory_path() /
                         "t375_op_report_shape.sleq";
    fs::remove(log);
    h.startRecording(log);

    handler.bug("recorded through PacketSender");  // /bug, alias /report

    h.stopRecording();

    const auto records = ReportHarness::RecordedClientToServer(log);
    std::vector<std::vector<uint8_t>> reports;
    for (auto& r : records) {
        if (r.first == HC_OP_Report) reports.push_back(r.second);
    }
    ASSERT_EQ(reports.size(), 1u)
        << "the recorder only sees packets that go out through "
           "PacketSender::SafeQueueZone; a sender calling "
           "zone_connection->QueuePacket() directly is invisible to it";

    const ParsedReport r = EqemuParseReport(reports.front());
    EXPECT_EQ(r.reporter, kSelfName);
    EXPECT_EQ(r.text, "recorded through PacketSender");
    EXPECT_TRUE(r.terminated);

    fs::remove(log);
}

}  // namespace
