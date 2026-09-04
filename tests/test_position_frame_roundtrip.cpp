// T88: pins the inbound (server->client) / outbound (client->server)
// position-frame round trip headlessly, settling the question T86 raised
// but did not test: is WillEQ's encoder/decoder pair for position actually
// broken, or was T85's harness handed server-frame numbers on a
// client-frame command surface?
//
// See refactor/T88_position_frame_determination.md's Review for the full
// write-up. Summary of what these tests establish:
//
//   - Inbound (ZoneProcessPlayerProfile, eq_zone_entry.cpp): m_x = server_y,
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
#include "client/position_frames.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
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
// position/heading fields, at eq_zone_entry.cpp's read offsets (server_x@13116,
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
    eq.Movement().SendPositionUpdate();
    auto sent = eq.Movement().Test_GetLastComputedPositionUpdate();

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

    eq.Movement().SendPositionUpdate();
    auto sent = eq.Movement().Test_GetLastComputedPositionUpdate();

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
    eq.Movement().UpdateMovement();  // snaps m_x/m_y to target (distance < 2.0), then StopMovement() sends

    glm::vec3 finalPos = eq.GetPosition();
    ASSERT_FLOAT_EQ(finalPos.x, client_target_x)
        << "Move() operates in client frame: m_x must equal the target x passed in, unswapped";
    ASSERT_FLOAT_EQ(finalPos.y, client_target_y);

    auto sent = eq.Movement().Test_GetLastComputedPositionUpdate();

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
    eq.Movement().Test_SnapToMovementTargetForSeam();

    glm::vec3 finalPos = eq.GetPosition();
    EXPECT_FLOAT_EQ(finalPos.x, issued_x);
    EXPECT_FLOAT_EQ(finalPos.y, issued_y);

    auto sent = eq.Movement().Test_GetLastComputedPositionUpdate();

    // Client (-650, 310) transmits as server (x=310, y=-650) -- exactly the
    // mirrored pair T86 measured on the wire (306.462, -646.949), well
    // within the documented waypoint slop. This is what a CORRECT client
    // does when handed server-frame numbers on a client-frame command.
    EXPECT_FLOAT_EQ(sent.x_pos, issued_y);
    EXPECT_FLOAT_EQ(sent.y_pos, issued_x);
}

// ---------------------------------------------------------------------------
// Part 2 (T311): the WIRE z. The frame round trip above is about x/y; this is
// about the third axis, and it is NOT an identity.
//
// T311 Leg A measured the official Titanium client on the wire against a live
// server: at a qeynos2 spot whose server raycast reports Best Z = -0.00 it
// transmits z = 3.751 for a default size-6 Human, and the value tracks size
// exactly linearly (size 3 -> 1.878, size 12 -> 7.501) with no race term. So
//
//     wireZ = groundZ + 0.625 * size
//
// while fable's GameState z is the RENDER frame -- the model node's origin,
// which the renderer snaps to groundZ + collisionZOffset (3.004 for a Human
// male, measured off the shipped mesh in T310 Leg B). The two differ, and
// SendPositionUpdate() has to convert. Before T311 it sent the render z
// verbatim: 3.00 where the official client sends 3.751.
//
// These tests drive the REAL wiring, not a seam: the renderer publishes its
// measured collisionZOffset as a PlayerModelGeometryChanged intent, which
// ProcessBridgeIntents() stores, and SendPositionUpdate() reads.
// ---------------------------------------------------------------------------

namespace {

class IntentBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// Human male, measured T310 Leg B: |bbox.MinEdge.Y| * scale.
constexpr float kHumanMaleCollisionZOffset = 3.004f;

// Stand the client at a spot whose collision ground is groundZ, with the
// renderer's measured mesh offset delivered the way production delivers it.
void StandAt(EverQuest& eq, eqt::bridge::GameStateBridge& bridge,
             float groundZ, float collisionZOffset, float size) {
    bridge.pushIntent(eqt::events::PlayerModelGeometryChanged{collisionZOffset});
    eq.Bridge().ProcessBridgeIntents();
    // What the renderer's ground snap produces: node origin = ground + offset.
    eq.SetPosition(eq.GetPosition().x, eq.GetPosition().y, groundZ + collisionZOffset);
    eq.Test_SetPlayerSizeForSeam(size);
}

}  // namespace

TEST(WireZ, DefaultHumanoidOnZeroGround_TransmitsTheOfficialClientsValue) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    IntentBridge bridge;
    eq.setBridge(&bridge);

    // The canonical Leg A packet: qeynos2 (-74, 428), server Best Z = -0.00,
    // Human male size 6. The official client put 3.75100040435791 on the wire.
    StandAt(eq, bridge, /*groundZ=*/0.0f, kHumanMaleCollisionZOffset, /*size=*/6.0f);

    // The render frame is unchanged -- the model's feet are still on the ground.
    EXPECT_FLOAT_EQ(eq.GetPosition().z, kHumanMaleCollisionZOffset);

    eq.Movement().SendPositionUpdate();
    auto sent = eq.Movement().Test_GetLastComputedPositionUpdate();

    // 0.625 * 6 == 3.75. The official client's extra +0.001 is a collision
    // epsilon it accumulates as it settles, and is deliberately not copied.
    EXPECT_NEAR(sent.z_pos, 3.75f, 1e-4f)
        << "fable must transmit what the official client transmits (T311 Leg A). "
           "3.00 here is the pre-T311 defect: the render-frame node origin sent verbatim.";
    EXPECT_NE(sent.z_pos, eq.GetPosition().z)
        << "the wire z and the render node origin are different quantities; "
           "if these are equal the two frames have been conflated again";
}

// Size-linearity is the MEASURED property, so pin a non-default size -- and one
// whose 0.625*size lands nowhere near size/2 or the mesh offset, so a
// regression to either of the two wrong expressions is unmistakable.
TEST(WireZ, NonDefaultSize_ScalesLinearlyWithNoRaceTerm) {
    struct Case { float size; float expected; };  // expected = 0.625 * size
    const Case cases[] = {
        {3.0f,  1.875f},   // Leg A measured 1.8780 (epsilon +0.003)
        {6.0f,  3.750f},   // Leg A measured 3.7510
        {7.0f,  4.375f},   // Leg A measured 4.3760 (a real Barbarian character)
        {12.0f, 7.500f},   // Leg A measured 7.5010
    };

    for (const auto& c : cases) {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(460);
        IntentBridge bridge;
        eq.setBridge(&bridge);

        StandAt(eq, bridge, /*groundZ=*/0.0f, kHumanMaleCollisionZOffset, c.size);
        eq.Movement().SendPositionUpdate();

        EXPECT_NEAR(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, c.expected, 1e-4f)
            << "size " << c.size << ": wire z must be 0.625 * size above ground";
        // The mesh offset is a property of the MESH, not of size -- holding it
        // fixed across the sweep is the point. If the transmitted value tracked
        // collisionZOffset instead, every row here would read 3.004.
    }
}

// Ground is not always zero: Leg A's bind-point rows were at ground 28.00,
// where the official client transmitted 31.751.
TEST(WireZ, NonZeroGround_IsGroundPlusTheOffset) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    IntentBridge bridge;
    eq.setBridge(&bridge);

    StandAt(eq, bridge, /*groundZ=*/28.0f, kHumanMaleCollisionZOffset, /*size=*/6.0f);
    eq.Movement().SendPositionUpdate();

    EXPECT_NEAR(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, 31.75f, 1e-4f);
}

// The mesh offset is an INPUT, not a constant: a mesh with a different
// origin-to-feet distance stands at a different node origin for the same
// ground, and must still transmit the same wire z.
TEST(WireZ, DifferentMeshOffset_SameGround_SameWireZ) {
    for (float meshOffset : {2.0f, 3.004f, 4.5f}) {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(460);
        IntentBridge bridge;
        eq.setBridge(&bridge);

        StandAt(eq, bridge, /*groundZ=*/0.0f, meshOffset, /*size=*/6.0f);
        EXPECT_FLOAT_EQ(eq.GetPosition().z, meshOffset);  // render frame follows the mesh

        eq.Movement().SendPositionUpdate();
        EXPECT_NEAR(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, 3.75f, 1e-4f)
            << "mesh offset " << meshOffset << ": the WIRE z is ground-relative and "
               "must not depend on which mesh we happen to be wearing";
    }
}

// Until the player model has loaded there is no measured mesh offset, and
// guessing one would put the player at a made-up height. Both conversions are
// the identity in that state -- the pre-T311 behaviour -- and the renderer's
// ground snap corrects it on the first movement frame after the model arrives.
TEST(WireZ, UnknownMeshOffset_ConversionIsTheIdentity) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    IntentBridge bridge;
    eq.setBridge(&bridge);

    ASSERT_FLOAT_EQ(eq.GetPlayerCollisionZOffset(), 0.0f) << "no model loaded yet";
    eq.SetPosition(0.0f, 0.0f, 3.0f);
    eq.Movement().SendPositionUpdate();
    EXPECT_FLOAT_EQ(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, 3.0f);
}

// The two conversions must be an exact inverse pair, or a value that goes out
// and comes back moves the player. This is what makes the inbound self-spawn
// path (eq_entities.cpp) and the outbound send path (eq_movement.cpp) safe to
// change together.
TEST(WireZ, RenderToWireToRender_IsIdentity) {
    for (float size : {3.0f, 6.0f, 7.0f, 12.0f}) {
        for (float offset : {0.0f, 2.0f, 3.004f, 4.5f}) {
            for (float nodeZ : {-120.5f, 0.0f, 3.004f, 987.25f}) {
                const float wire = eqt::renderZToWireZ(nodeZ, offset, size);
                EXPECT_NEAR(eqt::wireZToRenderZ(wire, offset, size), nodeZ, 1e-3f)
                    << "size " << size << " offset " << offset << " nodeZ " << nodeZ;
            }
        }
    }
}

// The constant is 3.125-per-0.2-size for EVERY model. EQEmu's Mob::GetZOffset()
// uses a per-race table (Dwarf is 0.7, not 3.125) and it is NOT what the client
// does: T311 Leg A put a Dwarf illusion on a Human and the server's own Fixed Z
// moved to 28.84 while the client went on transmitting 31.75. Reimplementing
// GetZOffset() here would be copying a coincidence of EQEmu's default.
TEST(WireZ, TheConstantIsThreePointOneTwoFive_NotEQEmusPerRaceTable) {
    EXPECT_FLOAT_EQ(eqt::kWireZOffsetPerSize, 0.2f * 3.125f);
    EXPECT_FLOAT_EQ(eqt::wireZOffsetForSize(6.0f), 3.75f);
    // A size of 0 means "the entity reported none", not "no offset".
    EXPECT_FLOAT_EQ(eqt::wireZOffsetForSize(0.0f), eqt::wireZOffsetForSize(6.0f));
}

// ---------------------------------------------------------------------------
// Part 3 (T311, LIVE-FOUND): the self-spawn arrives BEFORE the model, so the
// two conversions must not disagree ACROSS TIME.
//
// Found on a live server, not in review. The self spawn stores its z while the
// mesh offset is still unknown, so the inbound conversion is the identity and
// the stored z is still a raw WIRE value. The send path then saw a known offset
// (the model had loaded by then) and converted it -- adding 0.625*size to a
// value that already contained it. The server saves what a client sends and
// hands it back on the next login, so the error COMPOUNDED: three successive
// logins of the same character transmitted 3.63063, then 4.25, then 4.880627,
// against the official client's 3.751 at the same spot. (0.625 exactly per
// login, which is how it was identified: 3.75 - the measured mesh offset.)
//
// PlayerState::zAwaitingFrameConversion is the fix, and this is its pin.
//
// T332: this is the HEADLESS ARM, and it is not sufficient on its own. With no
// renderer nothing raises PlayerPositionChanged, so OnGraphicsMovement never
// runs and the flag is never touched -- which is why these two tests stayed
// green while T331 measured the graphical client still compounding 0.625 per
// login (B049). The renderer arm is tests/test_zone_in_z_conversion.cpp; keep
// both, they fail on different halves.
// ---------------------------------------------------------------------------

TEST(WireZ, SelfSpawnBeforeModelLoads_DoesNotDoubleConvert) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    IntentBridge bridge;
    eq.setBridge(&bridge);

    // Zone in: the server hands us the wire z, and no model has loaded.
    ASSERT_FLOAT_EQ(eq.GetPlayerCollisionZOffset(), 0.0f);
    const float wireFromServer = 3.751f;
    eq.SetPosition(0.0f, 0.0f, wireFromServer);
    eq.Test_SetZAwaitingFrameConversionForSeam(true);
    eq.Test_SetPlayerSizeForSeam(6.0f);

    // Standing still, before the model arrives: send back exactly what we were
    // given. Converting here is what produced the drift.
    eq.Movement().SendPositionUpdate();
    EXPECT_FLOAT_EQ(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, wireFromServer)
        << "an unconverted wire z must go back out verbatim";

    // The model loads. The deferred half of the conversion runs exactly once.
    bridge.pushIntent(eqt::events::PlayerModelGeometryChanged{kHumanMaleCollisionZOffset});
    eq.Bridge().ProcessBridgeIntents();
    EXPECT_FALSE(eq.Test_GetZAwaitingFrameConversionForSeam());
    EXPECT_NEAR(eq.GetPosition().z, wireFromServer - 3.75f + kHumanMaleCollisionZOffset, 1e-4f)
        << "the stored z must now be in the render frame";

    // And the round trip is closed: what we transmit is what we were handed.
    // Before the fix this read 4.376 -- 0.625 too high, and it compounded.
    eq.Movement().SendPositionUpdate();
    EXPECT_NEAR(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, wireFromServer, 1e-4f);

    // Repeating the geometry intent must not convert a second time.
    bridge.pushIntent(eqt::events::PlayerModelGeometryChanged{kHumanMaleCollisionZOffset});
    eq.Bridge().ProcessBridgeIntents();
    eq.Movement().SendPositionUpdate();
    EXPECT_NEAR(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, wireFromServer, 1e-4f)
        << "the deferred conversion must be idempotent";
}

// Three simulated logins of the same character, each starting from what the
// previous one transmitted -- the exact loop that was measured drifting.
TEST(WireZ, RepeatedZoneInsDoNotAccumulate) {
    float carried = 3.751f;
    for (int login = 0; login < 3; ++login) {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(460);
        IntentBridge bridge;
        eq.setBridge(&bridge);

        eq.SetPosition(0.0f, 0.0f, carried);
        eq.Test_SetZAwaitingFrameConversionForSeam(true);
        eq.Test_SetPlayerSizeForSeam(6.0f);
        bridge.pushIntent(eqt::events::PlayerModelGeometryChanged{kHumanMaleCollisionZOffset});
        eq.Bridge().ProcessBridgeIntents();
        eq.Movement().SendPositionUpdate();

        carried = eq.Movement().Test_GetLastComputedPositionUpdate().z_pos;
        EXPECT_NEAR(carried, 3.751f, 1e-3f)
            << "login " << login << ": a standing zone-in must be a fixed point, "
               "not a value that creeps 0.625 every time";
    }
}
