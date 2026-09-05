// T376 Leg A (BOARD B140): the door / world-object click senders go out
// through eqt::PacketSender, not through a raw ctx_.zone_connection deref.
//
// EntitiesComponent::SendClickDoor, SendClickObject and SendCloseContainer
// each ended in `ctx_.zone_connection->QueuePacket(p, 0, true)`. That is the
// same bypass T360 fixed for SendSpawnAppearance/SendAnimation (B068), with
// the same two consequences:
//
//   1. a null zone connection is an unguarded shared_ptr dereference --
//      T374 measured it as exit 139 from a harness that set all three
//      IsFullyZonedIn() terms but installed no connection, which is the only
//      reason test_door_click_zone_reset.cpp carries a real
//      DaybreakConnection it otherwise would not need;
//   2. the R01 session recorder and the T268 ordered-capture seam are both
//      BLIND to every door and world-object click the client sends, so a
//      --record-session .sleq is a false negative for them and the Batch R
//      replay oracle can never assert on one.
//
// This file pins both halves. The no-connection cases below CRASH the whole
// binary pre-fix (a segfault takes every case in the process with it); the
// recorder case reads a real .sleq back off disk and counts the clicks.
//
// No network, no graphics, no eqemu. SendClickDoor is gated on
// IsFullyZonedIn(), whose third term (EverQuest::m_update_running) has no
// public setter, so this file uses the T17 `#define private public` idiom the
// sibling click tests use.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "captured_packets.h"
#include "client/bridge/game_state_bridge.h"
#include "client/entities/entities_component.h"
#include "client/world_object.h"
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

constexpr uint8_t kDoorId = 3;
// The drop id carried by the captured OP_GroundSpawn (a forge), reused from
// test_interact_world_object.cpp / test_eq_action_handler_tradeskill.cpp.
constexpr uint32_t kForgeDropId = 190;

struct RecordedPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> bytes;  // body only (no opcode prefix)
};

// Reads a .sleq back and returns its C->S ZONE records in file order.
// Same reader as DeathHarness::RecordedClientToServer (T360).
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

// A clickable world: one door and one ground-spawned forge, with all three
// IsFullyZonedIn() terms set. `withConnection` chooses whether a real
// DaybreakConnection over FakeUdpTransport is installed as m_zone_connection
// -- the point of the no-connection cases is that there is NOT one.
class ClickHarness {
public:
    explicit ClickHarness(bool withConnection) : eq(MakeEq()) {
        if (withConnection) {
            EQ::Net::DaybreakConnectionManagerOptions opts;
            opts.crc_length = 2;
            opts.encode_passes[0] = EQ::Net::EncodeNone;
            opts.encode_passes[1] = EQ::Net::EncodeNone;
            opts.connect_delay_ms = 0;
            opts.keepalive_delay_ms = 0;
            manager = std::make_unique<EQ::Net::DaybreakConnectionManager>(
                opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
            manager->OnNewConnection(
                [this](std::shared_ptr<EQ::Net::DaybreakConnection> c) { conn = c; });
            manager->Connect("peer", 12345);
            EXPECT_NE(conn, nullptr);
            eq.m_zone_connection = conn;  // private: T17 idiom
            // SafeQueueZone refuses a connection whose stats are all zero
            // ("appears invalid"); a FakeUdpTransport connection that has
            // never round-tripped looks exactly like that (T360's note).
            conn->m_stats.sent_packets = 1;
        }

        eq.setBridge(&bridge);
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        eq.m_update_running = true;
    }

    ~ClickHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    void SpawnDoor(uint8_t door_id) {
        EQT::DoorState d;
        d.doorId = door_id;
        d.name = "DOOR_T376";
        eq.GetGameState().doors().addDoor(d);
    }

    // The T144 captured OP_GroundSpawn (forge, drop id 190).
    void SpawnForge() {
        const uint8_t* bytes = eqt_test::kGroundSpawnForgeBytes;
        size_t size = sizeof(eqt_test::kGroundSpawnForgeBytes);
        uint16_t opcode = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
        eq.ReplayZonePacket(opcode, bytes + 2, size - 2);
        EXPECT_NE(eq.Entities().GetWorldObject(kForgeDropId), nullptr);
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
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    TestBridge bridge;
    EverQuest eq;
};

// ---------------------------------------------------------------------
// (a) A null zone connection must be a logged refusal, not a segfault.
// ---------------------------------------------------------------------

// RED pre-T376: `ctx_.zone_connection->QueuePacket(...)` on a null shared_ptr.
// The process dies here and takes every other case in this binary with it.
TEST(ClickSendersPacketSender, NoZoneConnection_ClickDoorReturnsWithoutCrashing) {
    ClickHarness h(/*withConnection=*/false);
    h.SpawnDoor(kDoorId);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);

    h.eq.Entities().SendClickDoor(kDoorId, 0);

    SUCCEED() << "SendClickDoor survived a null zone connection";
}

TEST(ClickSendersPacketSender, NoZoneConnection_ClickObjectReturnsWithoutCrashing) {
    ClickHarness h(/*withConnection=*/false);
    h.SpawnForge();
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);

    h.eq.Entities().SendClickObject(kForgeDropId);

    SUCCEED() << "SendClickObject survived a null zone connection";
}

TEST(ClickSendersPacketSender, NoZoneConnection_CloseContainerReturnsWithoutCrashing) {
    ClickHarness h(/*withConnection=*/false);
    ASSERT_EQ(h.eq.m_zone_connection, nullptr);

    h.eq.Entities().SendCloseContainer(kForgeDropId);

    SUCCEED() << "SendCloseContainer survived a null zone connection";
}

// ---------------------------------------------------------------------
// (b) The T268 seam and the R01 recorder both see the clicks.
// ---------------------------------------------------------------------

// The board's close condition: "the click visible in Test_GetQueuedPackets()".
// The seam sits inside SafeQueueZone/Queue, so a direct sender never reaches
// it -- pre-fix this reads zero.
TEST(ClickSendersPacketSender, OrderedCaptureSeam_SeesTheDoorAndObjectClicks) {
    ClickHarness h(/*withConnection=*/true);
    h.SpawnDoor(kDoorId);
    h.SpawnForge();
    h.eq.Test_SetCaptureQueuedPacketsForSeam(true);
    h.eq.Test_ClearQueuedPackets();

    h.eq.Entities().SendClickDoor(kDoorId, 0);
    h.eq.Entities().SendClickObject(kForgeDropId);

    size_t doors = 0, objects = 0;
    const std::vector<uint8_t>* doorBody = nullptr;
    for (const auto& p : h.eq.Test_GetQueuedPackets()) {
        if (p.opcode == HC_OP_ClickDoor) {
            ++doors;
            doorBody = &p.bytes;
        }
        if (p.opcode == HC_OP_ClickObject) ++objects;
    }
    EXPECT_EQ(doors, 1u)
        << "SendClickDoor must reach the T268 ordered-capture seam; a sender "
           "that calls zone_connection->QueuePacket() directly bypasses it "
           "(BOARD B140)";
    EXPECT_EQ(objects, 1u) << "SendClickObject, same bypass, same fix";

    // And the body survived the trip: the door id is the first payload byte.
    ASSERT_NE(doorBody, nullptr);
    ASSERT_GE(doorBody->size(), 1u);
    EXPECT_EQ((*doorBody)[0], kDoorId);
}

// A recorded session must contain the C->S clicks the client just sent.
// Pre-fix this is ZERO: a .sleq was a false negative for every door and
// world-object click, exactly as it was for appearances before T360.
TEST(ClickSendersPacketSender, RecordedSession_ContainsTheDoorAndObjectClicks) {
    const fs::path log = fs::temp_directory_path() / "t376_clicks_recorded.sleq";
    fs::remove(log);

    {
        ClickHarness h(/*withConnection=*/true);
        h.SpawnDoor(kDoorId);
        h.SpawnForge();
        h.StartRecording(log);

        h.eq.Entities().SendClickDoor(kDoorId, 0);
        h.eq.Entities().SendClickObject(kForgeDropId);
        h.eq.Entities().SendCloseContainer(kForgeDropId);

        h.StopRecording();
    }

    auto recorded = RecordedClientToServer(log);
    EXPECT_EQ(CountOpcode(recorded, HC_OP_ClickDoor), 1u)
        << "SendClickDoor must reach the session recorder; a sender that calls "
           "zone_connection->QueuePacket() directly is invisible to it "
           "(BOARD B140)";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_ClickObject), 1u)
        << "SendClickObject, same bypass, same fix";
    EXPECT_EQ(CountOpcode(recorded, HC_OP_ClickObjectAction), 1u)
        << "SendCloseContainer, the third direct sender in the same TU";

    for (const auto& p : recorded) {
        if (p.opcode != HC_OP_ClickDoor) continue;
        ASSERT_GE(p.bytes.size(), 1u);
        EXPECT_EQ(p.bytes[0], kDoorId);
    }
    fs::remove(log);
}

}  // namespace
