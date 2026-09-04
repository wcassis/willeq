// T344 (BOARD B001): a server-forced pose cannot be applied without echoing
// it back.
//
// MovementComponent::SetPositionState() ended every arm in
// SendSpawnAppearance, so there was no way to record a pose the SERVER told
// us about without immediately telling the server about it again -- and its
// POS_DEAD arm sent AT_DIE, an appearance type T311 Leg A measured appearing
// ZERO times in either direction of a real official-client session
// (review/T311/legA/decode/T311A_table2_opcode_timeline.tsv). The split is
// SetPositionState (user-initiated: state + send) and ApplyPositionState
// (server-forced: state only).
//
// The three legs below are the unit's acceptance:
//   (a) a forced pose through the self OP_SpawnAppearance handler sets
//       positionState and queues NOTHING outbound;
//   (b) /sit still queues exactly one OP_SpawnAppearance (the preservation
//       pin -- the send must not have been lost in the split);
//   (c) the T311 self-death OP_Death bytes, replayed verbatim, set
//       positionState to Dead and queue no appearance at all.
//
// Same harness idiom as test_eq_action_handler_transforms_real.cpp (T234)
// and test_tradeskill_combine.cpp: a real DaybreakConnection over
// FakeUdpTransport installed as m_zone_connection via the T17
// `#define private public` idiom, so the outbound bytes are the real ones.
// SendSpawnAppearance() bypasses PacketSender (it calls
// zone_connection->QueuePacket directly), so the T268 capture seam cannot
// see it and the connection IS the seam. No network, no graphics, no eqemu.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/movement/movement_component.h"
#include "client/eq_action_handler.h"
#include "common/net/daybreak_connection.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace eqt;
using namespace zonetest;

namespace {

// The self spawn id from the T311 Leg A death capture (0x0111).
constexpr uint16_t kSelfId = 0x0111;

class SilentBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

// A headless EverQuest that is FULLY ZONED IN (SendSpawnAppearance's own
// guard) with a readable outbound queue.
class PoseHarness {
public:
    PoseHarness() : eq("127.0.0.1", 0, "test", "test", "test", "Testchar"), handler(eq) {
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
        // All three terms of IsFullyZonedIn(), which SendSpawnAppearance
        // returns early on -- without them leg (b) could not tell a lost
        // send from a gated one.
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        eq.m_update_running = true;
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(kSelfId);
    }

    ~PoseHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    void spawnSelf() {
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(kSelfId, "Testchar", 0, 10, 0, 5, /*npc_type=*/0));
        ASSERT_NE(eq.GetEntities().find(kSelfId), eq.GetEntities().end());
        eq.Test_SetMySpawnIdForSeam(kSelfId);
    }

    // Every application packet queued so far. These are all small (a
    // SpawnAppearance is 10 bytes), so no fragment reassembly is needed --
    // see test_eq_action_handler_transforms_real.cpp for the general decoder.
    std::vector<SentAppPacket> sentPackets() {
        std::vector<SentAppPacket> out;
        for (auto& [seq, sent] : conn->m_streams[0].sent_packets) {
            const auto* d = static_cast<const uint8_t*>(sent.packet.Data());
            size_t n = sent.packet.Length();
            size_t off = EQ::Net::DaybreakReliableHeader::size();
            if (n < off + 2) continue;
            if (d[off] == 0) ++off;  // low-byte-zero opcode escape
            if (off + 2 > n) continue;
            SentAppPacket pkt;
            pkt.opcode = static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
            pkt.body.assign(d + off + 2, d + n);
            out.push_back(std::move(pkt));
        }
        return out;
    }

    // The outbound SpawnAppearance packets as (type, parameter).
    std::vector<std::pair<uint16_t, uint32_t>> sentAppearances() {
        std::vector<std::pair<uint16_t, uint32_t>> out;
        for (auto& p : sentPackets()) {
            if (p.opcode != HC_OP_SpawnAppearance || p.body.size() < 8) continue;
            uint16_t type = static_cast<uint16_t>(p.body[2] | (p.body[3] << 8));
            uint32_t param = static_cast<uint32_t>(p.body[4]) |
                             (static_cast<uint32_t>(p.body[5]) << 8) |
                             (static_cast<uint32_t>(p.body[6]) << 16) |
                             (static_cast<uint32_t>(p.body[7]) << 24);
            out.emplace_back(type, param);
        }
        return out;
    }

    void clearSent() { conn->m_streams[0].sent_packets.clear(); }

    EverQuest eq;
    EqActionHandler handler;
    SilentBridge bridge;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    EQ::Test::FakeChannel c2s, s2c;
};

// S->C OP_SpawnAppearance, exactly the 8-byte wire body the official client
// receives: spawn_id u16, type u16, parameter u32.
std::vector<uint8_t> AppearancePayload(uint16_t spawn_id, uint16_t type, uint32_t parameter) {
    return BuildPayload(10, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawn_id);
        p.PutUInt16(4, type);
        p.PutUInt32(6, parameter);
    });
}

// ------------------------------------------------------------------
// (a) A SERVER-FORCED pose reaches positionState and sends nothing back.
// ------------------------------------------------------------------
TEST(ForcedPoseNoEcho, SelfAppearanceSitting_SetsStateAndQueuesNothing) {
    PoseHarness h;
    h.spawnSelf();
    h.clearSent();

    ReplayZone(h.eq, HC_OP_SpawnAppearance, AppearancePayload(kSelfId, AT_ANIMATION, ANIM_SITTING));

    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Sitting)
        << "a pose the server forced on us must reach the player's positionState";
    EXPECT_TRUE(h.sentAppearances().empty())
        << "and must NOT be echoed back to the server that sent it";
}

TEST(ForcedPoseNoEcho, SelfAppearanceStandingAgain_SetsStateAndQueuesNothing) {
    PoseHarness h;
    h.spawnSelf();
    ReplayZone(h.eq, HC_OP_SpawnAppearance, AppearancePayload(kSelfId, AT_ANIMATION, ANIM_SITTING));
    h.clearSent();

    ReplayZone(h.eq, HC_OP_SpawnAppearance, AppearancePayload(kSelfId, AT_ANIMATION, ANIM_STANDING));

    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Standing);
    EXPECT_TRUE(h.sentAppearances().empty());
}

// Another spawn's forced pose must not touch OUR state (and still sends
// nothing): the handler's self test is on spawn id, not on "any animation".
TEST(ForcedPoseNoEcho, OtherSpawnAppearance_LeavesOurStateAlone) {
    PoseHarness h;
    h.spawnSelf();
    ReplayZone(h.eq, HC_OP_NewSpawn, NewSpawnPayloadAt(500, "Guard_Hezlan000", 0, 12, 0));
    h.clearSent();

    ReplayZone(h.eq, HC_OP_SpawnAppearance, AppearancePayload(500, AT_ANIMATION, ANIM_SITTING));

    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Standing);
    EXPECT_TRUE(h.sentAppearances().empty());
}

// ------------------------------------------------------------------
// (b) The USER-INITIATED path still sends. Preservation pin for the split.
// ------------------------------------------------------------------
TEST(ForcedPoseNoEcho, UserSit_StillSendsExactlyOneAppearance) {
    PoseHarness h;
    h.spawnSelf();
    h.clearSent();

    h.handler.setPositionState(POS_SITTING);  // the /sit path

    auto appearances = h.sentAppearances();
    ASSERT_EQ(appearances.size(), 1u) << "/sit must still tell the server exactly once";
    EXPECT_EQ(appearances[0].first, AT_ANIMATION);
    EXPECT_EQ(appearances[0].second, 110u) << "Animation::Sitting";
    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Sitting);
}

TEST(ForcedPoseNoEcho, UserStand_StillSendsExactlyOneAppearance) {
    PoseHarness h;
    h.spawnSelf();
    h.handler.setPositionState(POS_SITTING);
    h.clearSent();

    h.handler.setPositionState(POS_STANDING);

    auto appearances = h.sentAppearances();
    ASSERT_EQ(appearances.size(), 1u);
    EXPECT_EQ(appearances[0].first, AT_ANIMATION);
    EXPECT_EQ(appearances[0].second, 100u) << "Animation::Standing";
}

// The AT_DIE echo is gone: the one path that could reach POS_DEAD from the
// user side (the setPositionState(4) action) must now send nothing at all.
// T311 Leg A: AT_DIE (type 0) appears zero times in either direction.
TEST(ForcedPoseNoEcho, UserDeadState_NeverSendsAtDie) {
    PoseHarness h;
    h.spawnSelf();
    h.clearSent();

    h.handler.setPositionState(POS_DEAD);

    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Dead)
        << "the state write survives; only the send is gone";
    for (auto& a : h.sentAppearances()) {
        EXPECT_NE(a.first, static_cast<uint16_t>(AT_DIE))
            << "AT_DIE is not an appearance any client ever sends";
    }
    EXPECT_TRUE(h.sentAppearances().empty());
}

// ------------------------------------------------------------------
// (c) The T311 death capture. These are the 32 payload bytes of the S->C
// OP_Death that killed the captured character, copied verbatim from
// review/T311/legA/decode/T311A_table2_opcode_timeline.tsv (the
// 09-01-2026 20:12:31 record): spawn_id 0x111, killer 0x111, corpse 0x111,
// bindzone 1, spell 0xffffffff, skill 4, damage 100000. The official client
// answered it with AT_ANIMATION 115 (Lying) and OP_ZoneChange -- never
// AT_DIE.
// ------------------------------------------------------------------
const uint8_t kT311SelfDeathBody[32] = {
    0x11, 0x01, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x04, 0x00, 0x00, 0x00,
    0xa0, 0x86, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

TEST(ForcedPoseNoEcho, T311SelfDeathCapture_SetsDeadAndSendsNoAppearance) {
    PoseHarness h;
    h.spawnSelf();
    h.clearSent();

    h.eq.ReplayZonePacket(HC_OP_Death, kT311SelfDeathBody, sizeof(kT311SelfDeathBody));

    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Dead)
        << "OP_Death for our own spawn id is what drives self's positionState";
    EXPECT_TRUE(h.sentAppearances().empty())
        << "no appearance at all goes out from the death path in this unit; "
           "the oracle's Lying + OP_ZoneChange re-entry is a separate unit";
}

// The Lying appearance that follows a death must not downgrade Dead to
// FeignDeath -- the wire cannot tell them apart, so the death we already
// know about wins.
TEST(ForcedPoseNoEcho, LyingAppearanceAfterDeath_DoesNotDowngradeDead) {
    PoseHarness h;
    h.spawnSelf();
    h.eq.ReplayZonePacket(HC_OP_Death, kT311SelfDeathBody, sizeof(kT311SelfDeathBody));
    h.clearSent();

    ReplayZone(h.eq, HC_OP_SpawnAppearance, AppearancePayload(kSelfId, AT_ANIMATION, ANIM_LYING));

    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Dead);
    EXPECT_TRUE(h.sentAppearances().empty());
}

}  // namespace
