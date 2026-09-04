// T346 (BOARD B001 remainder): the self-death sequence matches the official
// client.
//
// T311 Leg A captured a real Titanium client dying four times against a real
// eqemu (review/T311/legA/decode/T311A_table2_opcode_timeline.tsv). Every one
// of the four windows has the SAME client-to-server shape, and it is the
// specification this file pins:
//
//     S->C OP_Death (our own spawn id)
//     S->C OP_ZonePlayerToBind
//     C->S OP_Save              (192 bytes -- see the golden's note; NOT
//                                compared, fable's SendSave is opcode-only)
//     C->S OP_SpawnAppearance   type 0x0e AT_ANIMATION, param 0x73 = 115 Lying
//     C->S OP_ZoneChange        the bind re-entry, even into the same zone
//     S->C OP_ZoneChange        (approval)
//     C->S OP_DeleteSpawn       (zone teardown -- NOT compared, it belongs to
//                                CleanupZone, which needs the full handshake)
//
// and, decisively, ZERO C->S OP_ClientUpdate anywhere between the death and
// the zone teardown. The client goes silent the instant it dies.
//
// OP_FloatListThing is excluded from the comparison as a HEARTBEAT, not a
// death step: it is sent 1291 times across the capture against
// OP_ClientUpdate's 1522, appears on both sides of every death, and appears
// nowhere near a death in three of the four windows in the same position.
//
// Harness idiom is T344's (test_forced_pose_no_echo.cpp): a real
// DaybreakConnection over FakeUdpTransport installed as m_zone_connection,
// with all three IsFullyZonedIn() terms set, so every send runs its real
// production path.
//
// The ORDERED seam, though, is the T268 capture seam
// (Test_SetCaptureQueuedPacketsForSeam / Test_GetQueuedPackets), the same one
// test_zone_out_handshake.cpp uses -- widened by this unit to
// PacketSender::SafeQueueZone and EntitiesComponent::SendSpawnAppearance.
// T344's DaybreakConnection seam cannot do this job: OP_ClientUpdate is sent
// UNRELIABLE (SendPositionUpdate -> SafeQueueZone(p, 0, false)), and an
// unreliable packet never enters the connection's sent_packets resend map --
// it goes straight into the send buffer and onto the wire. A test that read
// only sent_packets would score "no OP_ClientUpdate while dead" as a pass
// before any gate existed. No network, no graphics, no eqemu.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/movement/movement_component.h"
#include "client/zone/zone_component.h"
#include "client/packet_sender.h"
#include "common/net/daybreak_connection.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace eqt;
using namespace zonetest;

namespace {

namespace fs = std::filesystem;

// The self spawn id from the T311 Leg A death capture (0x0111).
constexpr uint16_t kSelfId = 0x0111;

const fs::path kGolden =
    fs::path(WILLEQ_REPO_ROOT) / "tests" / "goldens" / "self_death_outbound_g07.txt";

// ------------------------------------------------------------------
// The T311 Leg A capture bytes, verbatim.
// ------------------------------------------------------------------

// S->C OP_Death, the 09-01-2026 20:12:31 record: spawn_id 0x111, killer
// 0x111, corpse 0x111, attack_skill 1, spell 0xffffffff, bindzoneid 4,
// damage 100000 (a GM #kill).
const uint8_t kT311SelfDeathBody[32] = {
    0x11, 0x01, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x04, 0x00, 0x00, 0x00,
    0xa0, 0x86, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// S->C OP_ZonePlayerToBind, the same record (33 bytes):
// bind_zone_id 1, bind_instance_id 0, x -510.0, y 57.0, z 31.75, heading 0,
// zone_name "South Qeynos".
const uint8_t kT311ZonePlayerToBindBody[33] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xc3, 0x00, 0x00, 0x64, 0x42,
    0x00, 0x00, 0xfe, 0x41, 0x00, 0x00, 0x00, 0x00, 'S',  'o',  'u',  't',
    'h',  ' ',  'Q',  'e',  'y',  'n',  'o',  's',  0x00,
};

class SilentBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class DeathHarness {
public:
    DeathHarness() : eq("127.0.0.1", 0, "test", "test", "test", "Oraclealpha") {
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
        eq.m_update_running = true;
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(kSelfId);
        // SafeQueueZone refuses to send over a connection whose stats are all
        // zero ("appears invalid"). A FakeUdpTransport connection that has
        // never round-tripped looks exactly like that, so declare it live --
        // otherwise every OP_ClientUpdate and OP_ZoneChange in this file dies
        // in that guard instead of in the code under test.
        conn->m_stats.sent_packets = 1;
        eq.Test_SetCaptureQueuedPacketsForSeam(true);
        eq.Test_ClearQueuedPackets();
    }

    ~DeathHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    void spawnSelf() {
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(kSelfId, "Oraclealpha", 0, 10, 0, 5, /*npc_type=*/0));
        ASSERT_NE(eq.GetEntities().find(kSelfId), eq.GetEntities().end());
        eq.Test_SetMySpawnIdForSeam(kSelfId);
    }

    // Outbound application packets in send order, from the T268 seam.
    const std::vector<eqt::PacketSender::TestQueuedPacket>& sentPackets() {
        return eq.Test_GetQueuedPackets();
    }

    // The death-sequence projection: the three opcodes T311 Leg A's windows
    // and this unit are about, in order, rendered as golden lines. Every
    // other opcode fable happens to emit is deliberately not compared -- see
    // the golden's header for each exclusion and why.
    std::vector<std::string> deathSequence() {
        std::vector<std::string> out;
        for (auto& p : sentPackets()) {
            switch (p.opcode) {
            case HC_OP_SpawnAppearance: {
                if (p.bytes.size() < 8) break;
                uint16_t type = static_cast<uint16_t>(p.bytes[2] | (p.bytes[3] << 8));
                uint32_t param = static_cast<uint32_t>(p.bytes[4]) |
                                 (static_cast<uint32_t>(p.bytes[5]) << 8) |
                                 (static_cast<uint32_t>(p.bytes[6]) << 16) |
                                 (static_cast<uint32_t>(p.bytes[7]) << 24);
                std::ostringstream os;
                os << "C->S OP_SpawnAppearance type=" << type << " param=" << param;
                out.push_back(os.str());
                break;
            }
            case HC_OP_ClientUpdate:
                out.push_back("C->S OP_ClientUpdate");
                break;
            case HC_OP_ZoneChange:
                // No zone_id: the capture's TSV keeps only the first 32
                // payload bytes and the field lives at offset 64. The bind
                // zone is pinned separately, from OP_ZonePlayerToBind, whose
                // whole 33-byte body IS in the capture.
                out.push_back("C->S OP_ZoneChange");
                break;
            default:
                break;
            }
        }
        return out;
    }

    std::vector<std::pair<uint16_t, uint32_t>> sentAppearances() {
        std::vector<std::pair<uint16_t, uint32_t>> out;
        for (auto& p : sentPackets()) {
            if (p.opcode != HC_OP_SpawnAppearance || p.bytes.size() < 8) continue;
            uint16_t type = static_cast<uint16_t>(p.bytes[2] | (p.bytes[3] << 8));
            uint32_t param = static_cast<uint32_t>(p.bytes[4]) |
                             (static_cast<uint32_t>(p.bytes[5]) << 8) |
                             (static_cast<uint32_t>(p.bytes[6]) << 16) |
                             (static_cast<uint32_t>(p.bytes[7]) << 24);
            out.emplace_back(type, param);
        }
        return out;
    }

    size_t countOpcode(uint16_t opcode) {
        size_t n = 0;
        for (auto& p : sentPackets()) {
            if (p.opcode == opcode) ++n;
        }
        return n;
    }

    void die() { eq.ReplayZonePacket(HC_OP_Death, kT311SelfDeathBody, sizeof(kT311SelfDeathBody)); }

    void zonePlayerToBind() {
        eq.ReplayZonePacket(HC_OP_ZonePlayerToBind, kT311ZonePlayerToBindBody,
                            sizeof(kT311ZonePlayerToBindBody));
    }

    void clearSent() { eq.Test_ClearQueuedPackets(); }

    EverQuest eq;
    SilentBridge bridge;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    EQ::Test::FakeChannel c2s, s2c;
};

// ------------------------------------------------------------------
// Step 1 -- the Lying appearance.
// ------------------------------------------------------------------
TEST(SelfDeathSequence, SelfDeath_SendsExactlyOneLyingAppearance) {
    DeathHarness h;
    h.spawnSelf();
    h.clearSent();

    h.die();

    auto appearances = h.sentAppearances();
    ASSERT_EQ(appearances.size(), 1u)
        << "the official client answers its own OP_Death with exactly one "
           "OP_SpawnAppearance (T311 Leg A, all four self-death windows)";
    EXPECT_EQ(appearances[0].first, static_cast<uint16_t>(AT_ANIMATION))
        << "type 0x0e in the capture";
    EXPECT_EQ(appearances[0].second, 115u) << "param 0x73 = Animation::Lying";
    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Dead);
}

// A death that is not OURS must stay silent -- the appearance is keyed on
// spawn id, not on "an OP_Death arrived".
TEST(SelfDeathSequence, OtherSpawnDeath_SendsNoAppearance) {
    DeathHarness h;
    h.spawnSelf();
    ReplayZone(h.eq, HC_OP_NewSpawn, NewSpawnPayloadAt(500, "Guard_Hezlan000", 0, 12, 0));
    h.clearSent();

    uint8_t body[32] = {0};
    body[0] = 0xf4; body[1] = 0x01;  // victim 500
    body[4] = 0x11; body[5] = 0x01;  // killer = us
    h.eq.ReplayZonePacket(HC_OP_Death, body, sizeof(body));

    EXPECT_TRUE(h.sentAppearances().empty());
    EXPECT_NE(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Dead);
}

// ------------------------------------------------------------------
// Step 2 -- silence while dead.
// ------------------------------------------------------------------
TEST(SelfDeathSequence, WhileDead_NoClientUpdateEverGoesOut) {
    DeathHarness h;
    h.spawnSelf();
    h.clearSent();

    // Dying WHILE MOVING is the case T344 recorded as owed:
    // ApplyPositionState(POS_DEAD) takes its StopMovement() arm, and
    // StopMovement() ends in a SendPositionUpdate() of its own. The gate has
    // to be inside SendPositionUpdate for that one to be caught too -- which
    // is why ApplyPositionState writes positionState BEFORE the switch.
    h.eq.GetGameState().player().setMoving(true);

    h.die();
    // N ticks of the one choke point every C->S OP_ClientUpdate funnels
    // through (the graphics path only pushes an intent; bridge_component
    // calls exactly this).
    for (int i = 0; i < 8; ++i) {
        h.eq.Movement().SendPositionUpdate();
    }

    EXPECT_EQ(h.countOpcode(HC_OP_ClientUpdate), 0u)
        << "T311 Leg A: zero C->S OP_ClientUpdate between the death and the "
           "zone teardown, in all four windows -- including the one "
           "ApplyPositionState(POS_DEAD)'s StopMovement() used to fire";
}

// Alive, the same pump still sends -- the gate is a death gate, not a mute.
TEST(SelfDeathSequence, WhileAlive_ClientUpdateStillGoesOut) {
    DeathHarness h;
    h.spawnSelf();
    h.clearSent();

    h.eq.Movement().SendPositionUpdate();

    EXPECT_GE(h.countOpcode(HC_OP_ClientUpdate), 1u)
        << "the living client must still talk";
}

// And the gate must lift on the far side of the bind re-entry, or the
// respawned character would never move again.
TEST(SelfDeathSequence, AfterZoneInPlayerProfile_ClientUpdateResumes) {
    DeathHarness h;
    h.spawnSelf();
    h.die();
    ASSERT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Dead);

    // A PlayerProfile is what every zone-in delivers; it is the authoritative
    // "you are in a zone now, alive" statement.
    std::vector<uint8_t> pp(20000, 0);
    h.eq.ReplayZonePacket(HC_OP_PlayerProfile, pp.data(), pp.size());
    EXPECT_NE(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Dead)
        << "zoning in clears Dead; otherwise the death gate is permanent";

    // The all-zero profile above also zeroes our spawn id, which
    // SendMovementUpdate refuses to send with. That is the fixture's doing,
    // not the gate's; restore it so the assertion is about the gate.
    h.eq.Test_SetMySpawnIdForSeam(kSelfId);
    h.clearSent();
    h.eq.Movement().SendPositionUpdate();
    EXPECT_GE(h.countOpcode(HC_OP_ClientUpdate), 1u)
        << "the death gate must lift when the bind re-entry completes";
}

// ------------------------------------------------------------------
// Step 3 -- the bind re-entry, and the golden.
// ------------------------------------------------------------------
TEST(SelfDeathSequence, ZonePlayerToBind_RequestsTheBindZoneChange) {
    DeathHarness h;
    h.spawnSelf();
    h.die();
    h.clearSent();

    h.zonePlayerToBind();

    ASSERT_EQ(h.countOpcode(HC_OP_ZoneChange), 1u)
        << "the official client answers OP_ZonePlayerToBind with exactly one "
           "C->S OP_ZoneChange";
    EXPECT_EQ(h.eq.Zone().Test_GetLastZoneChangeRequest().zone_id, 1u)
        << "bind_zone_id 1 from the capture (South Qeynos)";
}

std::vector<std::string> ReadGolden() {
    std::vector<std::string> lines;
    std::ifstream in(kGolden);
    EXPECT_TRUE(in.is_open()) << "missing golden: " << kGolden.string();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        lines.push_back(line);
    }
    return lines;
}

TEST(SelfDeathSequence, T311Capture_OutboundSequenceMatchesTheOracle) {
    auto expected = ReadGolden();
    ASSERT_EQ(expected.size(), 2u)
        << "the golden is the oracle's compared death projection: exactly the "
           "Lying appearance and the bind OP_ZoneChange, and no OP_ClientUpdate "
           "between them. If this size changed, the capture was re-read -- "
           "say so in the unit, do not re-bless.";

    DeathHarness h;
    h.spawnSelf();
    h.clearSent();

    // The capture, replayed in capture order.
    h.die();
    for (int i = 0; i < 4; ++i) h.eq.Movement().SendPositionUpdate();
    h.zonePlayerToBind();
    for (int i = 0; i < 4; ++i) h.eq.Movement().SendPositionUpdate();

    auto actual = h.deathSequence();

    std::ostringstream got;
    for (auto& l : actual) got << "\n  " << l;
    ASSERT_EQ(actual.size(), expected.size())
        << "fable's outbound death sequence:" << got.str();
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i], expected[i])
            << "step " << i << " diverges from the oracle; fable sent:" << got.str();
    }
}

}  // namespace
