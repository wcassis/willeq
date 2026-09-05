// T332 (B049): the zone-in z conversion, WITH A RENDERER PRESENT.
//
// T311 closed the packet-edge arithmetic and pinned it headlessly
// (test_position_frame_roundtrip.cpp, "Part 3"). T331 then MEASURED the live
// graphical client still compounding 0.625 per login -- Testchar at qeynos2
// (-74, 428) persisting 3.75 -> 4.38 -> 5.01 -> 5.63 while the official
// Titanium client re-persisted a steady 3.751 at the same spot in the same
// session. The headless test could not see it, and that is the point of this
// file:
//
//   Headless there is no renderer, so nothing ever raises
//   PlayerPositionChanged, so BridgeComponent::OnGraphicsMovement never runs,
//   so the pending-conversion flag is never touched and the raw wire z goes
//   back out verbatim -- ACCIDENTALLY correct. A three-login headless loop
//   CANNOT fail on this defect.
//
// With a renderer, the zone-load snapshot SEEDS the renderer with the raw wire
// z (Application::loadZoneGraphics* -> IrrlichtRenderer::setPlayerPosition) and
// the renderer echoes it straight back on its next intent. OnGraphicsMovement
// used to relabel that echo "render frame" and clear the pending conversion
// without performing it; SendPositionUpdate then added 0.625*size to a z that
// already contained it, the server persisted the result, and the next login
// started from it. Hence the compounding.
//
// So these tests build the real renderer object, seed it the way the zone-load
// snapshot does, and carry the provenance bit out of the renderer's own state
// into the intent -- exactly as renderer_player_physics.cpp does.
//
// The bare, never-Init()'d IrrlichtRenderer idiom is the one
// test_player_camera_component.cpp documents: the constructor only wires
// components and shutdown() null-guards everything, so no device and no GL
// context are needed. Free camera mode for the same reason it gives.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "client/eq.h"
#include "common/net/packet.h"
#include "client/position_frames.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"

using EQT::Graphics::IrrlichtRenderer;

namespace {

class IntentBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// Human male, measured T310 Leg B: |bbox.MinEdge.Y| * scale.
constexpr float kHumanMaleCollisionZOffset = 3.004f;
// What the official Titanium client transmits standing on ground Z = -0.00 at
// the canonical qeynos2 spot, measured T311 Leg A and re-measured T331.
constexpr float kOfficialWireZ = 3.751f;
constexpr float kSize = 6.0f;

constexpr float kX = 428.0f;
constexpr float kY = -74.0f;

// The renderer echo, built the way renderer_player_physics.cpp builds it: the
// position and the provenance both come out of the renderer's own state, so a
// regression that stops marking the seed shows up here and not only in the
// dedicated provenance test below.
eqt::events::PlayerPositionChanged RendererEcho(IrrlichtRenderer& renderer) {
    eqt::events::PlayerPositionChanged intent;
    intent.x = renderer.Context().playerX;
    intent.y = renderer.Context().playerY;
    intent.z = renderer.Context().playerZ;
    intent.heading = renderer.Context().playerHeading;
    intent.dx = intent.dy = intent.dz = 0.0f;
    intent.seededPosition = renderer.PlayerCamera().playerPositionIsSeed();
    return intent;
}

// One zone-in, in the order T331 measured it on the wire:
//   1. OP_PlayerProfile / self spawn stores the raw WIRE z, no model yet, so
//      the conversion is the identity and is marked pending.
//   2. the zone-load snapshot seeds the renderer with that same raw wire z.
//   3. the renderer's first intent echoes it back.
//   4. the model finally loads and publishes its measured mesh offset.
// Returns what the next OP_ClientUpdate would put on the wire.
float ZoneInAndTransmit(EverQuest& eq, IntentBridge& bridge, IrrlichtRenderer& renderer,
                        float wireZFromServer) {
    // 1. self spawn (entities_component.cpp): offset unknown -> stored raw.
    eq.SetPosition(kX, kY, wireZFromServer);
    eq.Test_SetZAwaitingFrameConversionForSeam(true);
    eq.Test_SetPlayerSizeForSeam(kSize);

    // 2. the seed (application_zone_loading.cpp:270/380).
    renderer.setPlayerPosition(kX, kY, wireZFromServer, /*heading512=*/0.0f);

    // 3. the echo. Before T332 this cleared the pending conversion.
    bridge.pushIntent(RendererEcho(renderer));
    eq.Bridge().ProcessBridgeIntents();

    // 4. the model loads.
    bridge.pushIntent(eqt::events::PlayerModelGeometryChanged{kHumanMaleCollisionZOffset});
    eq.Bridge().ProcessBridgeIntents();

    eq.Movement().SendPositionUpdate();
    return eq.Movement().Test_GetLastComputedPositionUpdate().z_pos;
}

// The PlayerProfile payload builder from test_position_frame_roundtrip.cpp --
// same offsets, same reason (SendPositionUpdate no-ops on a zero spawn id).
std::vector<uint8_t> BuildPlayerProfilePayload(float server_x, float server_y, float z,
                                               float wire_heading, uint32_t spawn_id = 460) {
    EQ::Net::DynamicPacket p;
    p.Resize(2 + 19588);
    p.PutFloat(2 + 13116, server_x);
    p.PutFloat(2 + 13120, server_y);
    p.PutFloat(2 + 13124, z);
    p.PutFloat(2 + 13128, wire_heading);
    p.PutUInt32(2 + 14384, spawn_id);
    std::vector<uint8_t> payload(2 + 19588 - 2);
    std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    return payload;
}

class ZoneInZ : public ::testing::Test {
protected:
    void SetUp() override {
        eq.Net().Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(460);
        eq.setBridge(&bridge);
        renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Free);
    }

    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    IntentBridge bridge;
    IrrlichtRenderer renderer;
};

}  // namespace

// ---------------------------------------------------------------------------
// The defect itself.
// ---------------------------------------------------------------------------

TEST_F(ZoneInZ, RendererEchoOfTheSeedDoesNotCountAsRenderFrame) {
    const float transmitted = ZoneInAndTransmit(eq, bridge, renderer, kOfficialWireZ);

    EXPECT_NEAR(transmitted, kOfficialWireZ, 1e-3f)
        << "B049: a standing zone-in must transmit back exactly what the server "
           "handed us. Reading 0.625*size - collisionZOffset high (4.497 here) "
           "means the renderer's ECHO of the seeded wire z was relabelled "
           "render-frame and the pending conversion was dropped.";

    // And the conversion really did happen, rather than the flag simply being
    // left set forever: the stored z is now the render frame.
    EXPECT_NEAR(eq.GetPosition().z,
                eqt::wireZToRenderZ(kOfficialWireZ, kHumanMaleCollisionZOffset, kSize), 1e-4f);
    EXPECT_FALSE(eq.Test_GetZAwaitingFrameConversionForSeam());
}

// The compounding is the reason this matters: the server persists what we send
// and hands it back at the next login. T331 measured 3.75 / 4.38 / 5.01 / 5.63.
TEST_F(ZoneInZ, ThreeConsecutiveZoneInsDoNotAccumulate) {
    float carried = kOfficialWireZ;
    for (int login = 0; login < 3; ++login) {
        EverQuest session("127.0.0.1", 0, "test", "test", "test", "Testchar");
        session.Net().Test_SetBypassSendGuardForSeam(true);
        session.Test_SetMySpawnIdForSeam(460);
        IntentBridge sessionBridge;
        session.setBridge(&sessionBridge);
        IrrlichtRenderer sessionRenderer;
        sessionRenderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Free);

        carried = ZoneInAndTransmit(session, sessionBridge, sessionRenderer, carried);
        EXPECT_NEAR(carried, kOfficialWireZ, 1e-3f)
            << "login " << login << ": a standing zone-in with a renderer present "
               "must be a fixed point. +0.625 per login is B049.";
    }
}

// ---------------------------------------------------------------------------
// The other half: a position the renderer really did produce must still be
// taken as the render frame, or the fix would have broken normal movement.
// ---------------------------------------------------------------------------

TEST_F(ZoneInZ, GenuineRendererMovementIsStillTheRenderFrame) {
    ASSERT_NEAR(ZoneInAndTransmit(eq, bridge, renderer, kOfficialWireZ), kOfficialWireZ, 1e-3f);

    // The movement integrator walks us uphill. This z is DERIVED by the
    // renderer (node origin = ground + mesh offset), so it is render frame and
    // the packet edge must convert it on the way out.
    const float movedRenderZ = 10.0f + kHumanMaleCollisionZOffset;
    eqt::events::PlayerPositionChanged moved;
    moved.x = kX + 5.0f;
    moved.y = kY + 5.0f;
    moved.z = movedRenderZ;
    moved.heading = 0.0f;
    moved.dx = 5.0f; moved.dy = 5.0f; moved.dz = 10.0f;
    moved.seededPosition = false;
    bridge.pushIntent(moved);
    eq.Bridge().ProcessBridgeIntents();

    EXPECT_FALSE(eq.Test_GetZAwaitingFrameConversionForSeam());
    EXPECT_FLOAT_EQ(eq.GetPosition().z, movedRenderZ)
        << "a z the renderer DERIVED must be stored verbatim";

    eq.Movement().SendPositionUpdate();
    EXPECT_NEAR(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos,
                eqt::renderZToWireZ(movedRenderZ, kHumanMaleCollisionZOffset, kSize), 1e-4f)
        << "ground 10.0 + 0.625*6 == 13.75 is what the official client would send";
}

// If the echo arrives before any mesh offset is measurable, there is nothing to
// convert through yet -- the conversion must stay OWED, not be silently
// dropped, so the model-load intent can still perform it.
TEST_F(ZoneInZ, EchoBeforeTheModelLoadsLeavesTheConversionOwed) {
    eq.SetPosition(kX, kY, kOfficialWireZ);
    eq.Test_SetZAwaitingFrameConversionForSeam(true);
    eq.Test_SetPlayerSizeForSeam(kSize);
    renderer.setPlayerPosition(kX, kY, kOfficialWireZ, 0.0f);

    bridge.pushIntent(RendererEcho(renderer));
    eq.Bridge().ProcessBridgeIntents();

    EXPECT_TRUE(eq.Test_GetZAwaitingFrameConversionForSeam());
    EXPECT_FLOAT_EQ(eq.GetPosition().z, kOfficialWireZ) << "still the raw wire z";
    // Standing still before the model arrives, we send it straight back out.
    eq.Movement().SendPositionUpdate();
    EXPECT_FLOAT_EQ(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, kOfficialWireZ);
}

// ---------------------------------------------------------------------------
// The provenance bit itself, on the renderer side.
// ---------------------------------------------------------------------------

TEST_F(ZoneInZ, SetPlayerPositionMarksThePositionAsSeeded) {
    // A renderer that has never been handed a position has produced nothing to
    // echo either; the default must be "not a seed" so an ordinary intent is
    // never mistaken for one.
    EXPECT_FALSE(renderer.PlayerCamera().playerPositionIsSeed());

    renderer.setPlayerPosition(kX, kY, kOfficialWireZ, 0.0f);
    EXPECT_TRUE(renderer.PlayerCamera().playerPositionIsSeed())
        << "setPlayerPosition is the ONLY external writer of ctx_.playerX/Y/Z "
           "(the zone-load snapshot seed and PlayerMoved relocations); it must "
           "mark what it wrote, or OnGraphicsMovement cannot tell an echo from "
           "a move. renderer_player_physics.cpp clears it the moment the "
           "integrator writes those members itself.";
    EXPECT_TRUE(RendererEcho(renderer).seededPosition)
        << "and the bit must reach the intent";
}

// ---------------------------------------------------------------------------
// The edge T332 MEASURED as the live writer.
//
// T331 named OnGraphicsMovement from the log, and it is a real hole (the four
// tests above are its pin). But instrumenting a live graphical login showed the
// pending-conversion flag was never even SET: on a real zone-in NEITHER
// self-SPAWN edge -- the two T311 marked -- is ever taken for our own character
// (no ZoneSpawns-self branch, no "Found our own spawn in NewSpawn"; the spawn id
// arrives with the PlayerProfile). OP_PlayerProfile is what seeds our position,
// and it stored the server's WIRE z raw and said nothing, so the deferred
// conversion had nothing to fire on and the send path added 0.625*size to a
// value that already contained it -- every login, compounding.
//
// This drives the REAL packet through the replay seam, so it fails on
// zone_entry.cpp alone.
// ---------------------------------------------------------------------------

TEST_F(ZoneInZ, PlayerProfileZIsAWireValueAndIsConvertedWhenTheModelLoads) {
    // qeynos2 (-74, 428), server Best Z = -0.00: the spot T311 Leg A and T331
    // both measured the official client at.
    auto payload = BuildPlayerProfilePayload(/*server_x=*/-74.0f, /*server_y=*/428.0f,
                                             kOfficialWireZ, /*wire_heading=*/160.0f);
    eq.ReplayZonePacket(HC_OP_PlayerProfile, payload.data(), payload.size());
    eq.Test_SetPlayerSizeForSeam(kSize);

    // No model yet, so the conversion is the identity and must be marked OWED.
    ASSERT_FLOAT_EQ(eq.GetPlayerCollisionZOffset(), 0.0f);
    EXPECT_TRUE(eq.Test_GetZAwaitingFrameConversionForSeam())
        << "B049: PlayerProfile's z is the WIRE frame. Storing it raw without "
           "saying so is what let the send path convert a value that was never "
           "in the render frame -- 0.625*size per login, compounding.";

    // Standing still before the model arrives: send it straight back.
    eq.Movement().SendPositionUpdate();
    EXPECT_NEAR(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, kOfficialWireZ, 1e-4f);

    // The model loads. One conversion, and the round trip closes.
    bridge.pushIntent(eqt::events::PlayerModelGeometryChanged{kHumanMaleCollisionZOffset});
    eq.Bridge().ProcessBridgeIntents();
    EXPECT_NEAR(eq.GetPosition().z,
                eqt::wireZToRenderZ(kOfficialWireZ, kHumanMaleCollisionZOffset, kSize), 1e-4f);

    eq.Movement().SendPositionUpdate();
    EXPECT_NEAR(eq.Movement().Test_GetLastComputedPositionUpdate().z_pos, kOfficialWireZ, 1e-3f)
        << "live, this read 4.38063 against the official client's steady 3.751";
}

// And the renderer has to be TOLD about the correction, or it goes on drawing us
// at the seeded wire height. SetPosition alone fires PlayerMoved on the state
// layer's own EventBus, which nothing forwards to the bridge.
TEST_F(ZoneInZ, TheDeferredConversionReachesTheRenderer) {
    auto payload = BuildPlayerProfilePayload(-74.0f, 428.0f, kOfficialWireZ, 160.0f);
    eq.ReplayZonePacket(HC_OP_PlayerProfile, payload.data(), payload.size());
    eq.Test_SetPlayerSizeForSeam(kSize);
    bridge.drainEvents();  // discard the zone-in PlayerMoved

    bridge.pushIntent(eqt::events::PlayerModelGeometryChanged{kHumanMaleCollisionZOffset});
    eq.Bridge().ProcessBridgeIntents();

    bool sawCorrectedZ = false;
    for (const auto& e : bridge.drainEvents()) {
        if (e.type != eqt::state::GameEventType::PlayerMoved) continue;
        const auto& d = std::get<eqt::state::PlayerMovedData>(e.data);
        if (std::abs(d.z - eqt::wireZToRenderZ(kOfficialWireZ, kHumanMaleCollisionZOffset, kSize))
            < 1e-3f) {
            sawCorrectedZ = true;
        }
    }
    EXPECT_TRUE(sawCorrectedZ)
        << "the corrected render-frame z must reach the bridge, or the renderer "
           "keeps the raw wire z it was seeded with";
}
