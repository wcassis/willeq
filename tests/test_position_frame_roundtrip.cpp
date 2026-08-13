// T88: pins the inbound (server->client) / outbound (client->server)
// position-frame round trip headlessly, settling the question T86 raised
// but did not test: is WillEQ's encoder/decoder pair for position actually
// broken, or was T85's harness handed server-frame numbers on a
// client-frame command surface?
//
// See refactor/T88_position_frame_determination.md's Review for the full
// write-up. Summary of what these tests establish:
//
//   - Inbound (ZoneProcessPlayerProfile, eq.cpp): m_x = server_y,
//     m_y = server_x (SetPosition(server_y, server_x, z)).
//   - Outbound (SendPositionUpdate, eq_movement.cpp): x_pos = m_y,
//     y_pos = m_x.
//   - Composing the two: server_x -> m_y -> x_pos == server_x, and
//     server_y -> m_x -> y_pos == server_y. The round trip is IDENTITY.
//     The encoder and decoder are a matched inverse pair; T86's ~1300-unit
//     mismatch was not caused by an asymmetry between them.
//   - Move()/MoveToWithPath() operates in CLIENT frame -- the same frame as
//     m_x/m_y -- confirmed by driving a target through the public /move
//     entry point (EqActionHandler::moveToLocation -> EverQuest::Move) and
//     reading back what SendPositionUpdate() transmits.
//
// These tests use the T88 test seam (Test_SetBypassSendGuardForSeam /
// Test_GetLastComputedPositionUpdate, eq.h) because SendPositionUpdate()'s
// top guard requires a live m_zone_connection that no headless test can
// construct -- everything past that guard already degrades gracefully with
// a null connection (SafeQueueZonePacket/SendMovementHistory both
// self-check), so the seam only skips that one guard plus the
// process-wide-static send throttle. See the seam's own comments in eq.h /
// eq_movement.cpp for why this is narrow and production-behavior-preserving.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "common/net/packet.h"

namespace {

std::vector<uint8_t> BuildPayload(size_t totalPacketSize,
                                   const std::function<void(EQ::Net::DynamicPacket&)>& fill) {
    EQ::Net::DynamicPacket p;
    p.Resize(totalPacketSize);
    fill(p);
    std::vector<uint8_t> payload(totalPacketSize - 2);
    std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    return payload;
}

// Same layout as test_zone_position_convention.cpp's builder: PlayerProfile
// position/heading fields, at eq.cpp's read offsets (server_x@13116,
// server_y@13120, z@13124, heading@13128, all +base(=2)) -- plus the
// entity/spawn id at offset 14384 (also +base), which test_zone_position_
// convention.cpp's builder didn't need but SendPositionUpdate() does: it
// silently no-ops (by design -- see eq_movement.cpp) when m_my_spawn_id is
// still 0, which it stays unless PlayerProfile carries a nonzero id here.
std::vector<uint8_t> BuildPlayerProfilePayload(float server_x, float server_y, float z,
                                                float wire_heading, uint32_t spawn_id = 460) {
    return BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutFloat(2 + 13116, server_x);
        p.PutFloat(2 + 13120, server_y);
        p.PutFloat(2 + 13124, z);
        p.PutFloat(2 + 13128, wire_heading);
        p.PutUInt32(2 + 14384, spawn_id);
    });
}

}  // namespace

// ---------------------------------------------------------------------------
// Part 1a: inbound decode -> outbound encode round trip is IDENTITY.
// ---------------------------------------------------------------------------

TEST(PositionFrameRoundTrip, ServerFrame_InboundThenOutbound_IsIdentity) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);

    // Distinct, unambiguous values -- a transposition or a broken inverse
    // is unmistakable in the assertion, not maskable by an accidental
    // symmetry.
    const float server_x = -650.0f;
    const float server_y = 310.0f;
    const float z = 3.75f;

    auto payload = BuildPlayerProfilePayload(server_x, server_y, z, /*wire_heading=*/0.0f);
    eq.ReplayZonePacket(HC_OP_PlayerProfile, payload.data(), payload.size());

    // Sanity: the inbound swap landed where T86/T87's own reviews and
    // docs/CLAUDE.md say it should (m_x = server_y, m_y = server_x).
    glm::vec3 clientPos = eq.GetPosition();
    ASSERT_FLOAT_EQ(clientPos.x, server_y);
    ASSERT_FLOAT_EQ(clientPos.y, server_x);

    // Now transmit. This must produce EXACTLY the original server-frame
    // position back out -- that is the whole round trip.
    eq.SendPositionUpdate();
    auto sent = eq.Test_GetLastComputedPositionUpdate();

    EXPECT_FLOAT_EQ(sent.x_pos, server_x)
        << "outbound x_pos must equal the ORIGINAL server_x for the round trip to be identity";
    EXPECT_FLOAT_EQ(sent.y_pos, server_y)
        << "outbound y_pos must equal the ORIGINAL server_y for the round trip to be identity";
    EXPECT_FLOAT_EQ(sent.z_pos, z);
}

// A second, independently-chosen pair (asymmetric magnitudes, mixed signs)
// so the first test's numbers can't accidentally happen to survive a bug
// that only shows up for certain sign/magnitude combinations.
TEST(PositionFrameRoundTrip, ServerFrame_InboundThenOutbound_IsIdentity_SecondSample) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);

    const float server_x = 1024.5f;
    const float server_y = -87.25f;
    const float z = 12.0f;

    auto payload = BuildPlayerProfilePayload(server_x, server_y, z, /*wire_heading=*/64.0f);
    eq.ReplayZonePacket(HC_OP_PlayerProfile, payload.data(), payload.size());

    eq.SendPositionUpdate();
    auto sent = eq.Test_GetLastComputedPositionUpdate();

    EXPECT_FLOAT_EQ(sent.x_pos, server_x);
    EXPECT_FLOAT_EQ(sent.y_pos, server_y);
    EXPECT_FLOAT_EQ(sent.z_pos, z);
}

// ---------------------------------------------------------------------------
// Part 1b: Move()/MoveToWithPath() operates in CLIENT frame -- proven by
// driving the SAME public entry point /move uses (EqActionHandler::
// moveToLocation) and reading back the transmitted server-frame position.
// ---------------------------------------------------------------------------

TEST(PositionFrameRoundTrip, MoveToLocation_ClientFrameTarget_TransmitsUnderDocumentedConvention) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    eqt::EqActionHandler handler(eq);

    // Player starts at the constructor default (0,0,0). Pick a client-frame
    // target within MoveTo's 2.0-unit arrival radius of the origin so
    // UpdateMovement() snaps to it deterministically on the very first
    // tick -- no time-based simulation, no flakiness.
    const float client_target_x = 1.0f;
    const float client_target_y = 1.5f;

    // This is the EXACT call /move's command handler makes
    // (eq_commands_movement.cpp: Move(x, y, z) from EqActionHandler::
    // moveToLocation), i.e. this test drives the real command surface, not
    // a lower-level shortcut.
    handler.moveToLocation(client_target_x, client_target_y, 0.0f);
    eq.UpdateMovement();  // snaps m_x/m_y to target (distance < 2.0), then StopMovement() sends

    glm::vec3 finalPos = eq.GetPosition();
    ASSERT_FLOAT_EQ(finalPos.x, client_target_x)
        << "Move() operates in client frame: m_x must equal the target x passed in, unswapped";
    ASSERT_FLOAT_EQ(finalPos.y, client_target_y);

    auto sent = eq.Test_GetLastComputedPositionUpdate();

    // Documented convention (docs/CLAUDE.md "EQ Server Coordinates":
    // client_x = server_y, client_y = server_x, i.e. the inverse is
    // server_x = client_y, server_y = client_x): a client-frame target of
    // (1.0, 1.5) must transmit as server (x=1.5, y=1.0).
    EXPECT_FLOAT_EQ(sent.x_pos, client_target_y)
        << "transmitted server x_pos must equal the CLIENT-frame target's y";
    EXPECT_FLOAT_EQ(sent.y_pos, client_target_x)
        << "transmitted server y_pos must equal the CLIENT-frame target's x";
}

// ---------------------------------------------------------------------------
// Part 1c: reproduces T86's exact numbers end-to-end, to make explicit which
// competing explanation the data supports.
//
// T85 issued `/move -650 310 3.75` -- server-frame numbers (Oracledelta's
// own wire self-report, per T86) fed to a client-frame command. If Move()
// is client-frame (proven above), that command should transmit
// server-frame (310, -650) -- i.e. mirrored relative to what T85's
// executor intended -- which is what T86 actually measured on the wire
// (306.462, -646.949, within the ~2-6 unit /move waypoint slop T87's Review
// already documented). This test pins that arithmetic explicitly so a
// reader doesn't have to do it by hand.
// ---------------------------------------------------------------------------

TEST(PositionFrameRoundTrip, T85Scenario_ServerFrameNumbersFedToClientFrameMoveProduceObservedMirroring) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    eqt::EqActionHandler handler(eq);

    // T85's executor issued exactly this to the /move command surface.
    const float issued_x = -650.0f;
    const float issued_y = 310.0f;

    // Not within MoveTo's 2.0-unit snap radius of the origin, so drive
    // UpdateMovement() by directly forcing arrival the same way the
    // production "reached target" branch does (m_x = m_target_x; m_y =
    // m_target_y -- eq_movement.cpp UpdateMovement(), distance < 2.0f
    // branch) rather than iterating real time-based movement steps, which
    // would just be re-testing the walk simulation this unit is not about.
    handler.moveToLocation(issued_x, issued_y, 3.75f);
    eq.Test_SnapToMovementTargetForSeam();

    glm::vec3 finalPos = eq.GetPosition();
    EXPECT_FLOAT_EQ(finalPos.x, issued_x);
    EXPECT_FLOAT_EQ(finalPos.y, issued_y);

    auto sent = eq.Test_GetLastComputedPositionUpdate();

    // Client (-650, 310) transmits as server (x=310, y=-650) -- exactly the
    // mirrored pair T86 measured on the wire (306.462, -646.949), well
    // within the documented waypoint slop. This is what a CORRECT client
    // does when handed server-frame numbers on a client-frame command.
    EXPECT_FLOAT_EQ(sent.x_pos, issued_y);
    EXPECT_FLOAT_EQ(sent.y_pos, issued_x);
}
