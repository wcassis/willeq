// T426 Leg A (refactor/T426_overcast_fog_heading_caster_landing.md, board
// B399): a server-authoritative relocation of OUR OWN character must reach the
// camera in the client's internal heading convention, not in the raw wire one.
//
// THE CONVENTION, and why there are two numbers. A player's own heading gets a
// `90 - server_deg` flip at every inbound edge (ZoneProcessPlayerProfile,
// ZoneComponent::RelocateWithinZone, and the self branch of
// ZoneProcessClientUpdate itself), and SendPositionUpdate inverts it on the way
// out. Other entities' headings are taken verbatim -- players and NPCs do not
// share a default model rotation and the asymmetry is load-bearing
// (docs/CLAUDE.md, Heading/Rotation Conventions). So one packet carries a
// heading that must be read TWO different ways: flipped for our camera,
// verbatim for the entity row.
//
// THE DEFECT. BridgeComponent::OnSpawnMovedGraphics is handed one `heading`
// argument and used it for both. ZoneProcessClientUpdate's self branch computes
// `self_heading = 90 - server_h` into ctx_.self and then passed `h_player`, the
// RAW server degrees, to OnSpawnMovedGraphics -- which converts it to the 512
// format and publishes it as PlayerMoved, i.e. straight into
// IrrlichtRenderer::setPlayerPosition and the camera.
//
// MEASURED LIVE (review/T426/logs/fable_sweep1.log, zone-in at 22:35:29):
//
//     setPlayerPosition ... heading=480.00     <- PlayerProfile, flipped
//     setPlayerPosition ... heading=160.00     <- THIS path, raw
//     setPlayerPosition ... heading=480.00
//
// 160 is the wire heading the server was given (`#goto -74 428 3.751 160`);
// 480 is that same heading in the internal convention. The camera took both.
//
// WHICH ONE IS RIGHT was settled against the official client and not by
// argument: review/T426/probe/T426_fp_pairs.png puts fable's first-person view
// beside the oracle's at the SAME spot for six wire headings (0/128/256/384/
// 160/288), and they match heading for heading -- so the flipped value is the
// one that aims where the server aimed us, and the raw value is 90 degrees plus
// a mirror away from it.
//
// Seam: zone_stream_test_utils.h's bare EverQuest + a recording bridge, driven
// through the real ReplayZonePacket dispatch -- the same seam
// tests/test_zone_position_convention.cpp uses for the other two self-heading
// edges.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "client/eq.h"
#include "client/eq_opcodes.h"
#include "client/state/event_bus.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

constexpr uint16_t kMySpawn = 276;

// server_h = 640 * 360 / 2048 = 112.5 deg  ->  internal 90 - 112.5 = 337.5 deg.
// Deliberately NOT a heading where the two conventions coincide: 337.5 deg is
// 480 in the 512 format the bridge publishes, and the raw 112.5 deg is 160.
constexpr uint32_t kRawHeading12Bit = 640;
constexpr float kInternalHeading512 = 480.0f;
constexpr float kRawHeading512 = 160.0f;

// The 22-byte bit-packed PlayerPositionUpdateServer_Struct
// ZoneProcessClientUpdate reads (offsets include the 2-byte opcode prefix).
std::vector<uint8_t> ClientUpdatePayload(uint16_t spawn_id, int32_t server_x,
                                          int32_t server_y, int32_t z,
                                          uint32_t raw_heading) {
    return BuildPayload(24, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawn_id);
        p.PutUInt32(4, (static_cast<uint32_t>(server_x) & 0x7FFFF) << 10);
        p.PutUInt32(8, static_cast<uint32_t>(server_y) & 0x7FFFF);
        p.PutUInt32(12, static_cast<uint32_t>(z) & 0x7FFFF);
        p.PutUInt32(16, (raw_heading & 0x7FF) << 13);
        p.PutUInt32(20, 0);
    });
}

// Last PlayerMoved on the renderer-facing bridge, or -1 if there was none.
float LastPlayerMovedHeading512(const std::vector<eqt::state::GameEvent>& events,
                                int* count = nullptr) {
    float last = -1.0f;
    int n = 0;
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::PlayerMoved) {
            ++n;
            last = std::get<eqt::state::PlayerMovedData>(e.data).heading;
        }
    }
    if (count) *count = n;
    return last;
}

// Our own spawn, by name, so MySpawnId and the entity row both exist -- the
// self branch of ZoneProcessClientUpdate needs both.
void SeedOurOwnSpawn(EverQuest& eq, TestBridge& bridge) {
    ReplayZone(eq, HC_OP_NewSpawn,
               NewSpawnPayloadAt(kMySpawn, "Testchar", /*server_x=*/428, /*server_y=*/-74,
                                 /*z=*/3, /*level=*/20, /*npc_type=*/0));
    ASSERT_EQ(eq.GetMySpawnID(), kMySpawn);
    bridge.drainEvents();
}

// Sanity: the store itself takes the flipped value. This has always been right
// and is what the camera should have been reading.
TEST(SelfRelocationHeading, TheSelfStoreTakesTheInternalConvention) {
    TestBridge bridge;
    EverQuest eq = MakeEq();
    eq.setBridge(&bridge);
    SeedOurOwnSpawn(eq, bridge);

    ReplayZone(eq, HC_OP_ClientUpdate,
               ClientUpdatePayload(kMySpawn, 428, -74, 3, kRawHeading12Bit));

    EXPECT_FLOAT_EQ(eq.GetHeading(), 337.5f)
        << "the self branch stopped applying the player-only 90 - server_deg flip";
}

// PRE-FIX RED, and the whole of B399.
//
// The heading the bridge publishes is the one that lands in
// IrrlichtRenderer::setPlayerPosition and drives CameraController. Pre-fix it
// was the RAW server heading (160), not the internal one (480) the same handler
// had just stored -- so a server relocation arriving as a self OP_ClientUpdate
// aimed the camera somewhere the server never asked for.
TEST(SelfRelocationHeading, AServerRelocationReachesTheCameraInTheInternalConvention) {
    TestBridge bridge;
    EverQuest eq = MakeEq();
    eq.setBridge(&bridge);
    SeedOurOwnSpawn(eq, bridge);

    ReplayZone(eq, HC_OP_ClientUpdate,
               ClientUpdatePayload(kMySpawn, 428, -74, 3, kRawHeading12Bit));

    int moved = 0;
    const float heading512 = LastPlayerMovedHeading512(bridge.drainEvents(), &moved);
    ASSERT_GE(moved, 1) << "no PlayerMoved reached the renderer at all";

    EXPECT_FLOAT_EQ(heading512, kInternalHeading512)
        << "the camera was handed the RAW wire heading (" << kRawHeading512
        << ") instead of the internal one (" << kInternalHeading512
        << ") that the same handler stored in ctx_.self -- B399";
    EXPECT_NE(heading512, kRawHeading512);
}

// The other half of the asymmetry must NOT move: an entity row still carries
// the server's heading verbatim. If this ever goes red, the fix above leaked
// into the entity convention, which is the thing docs/CLAUDE.md forbids
// unifying.
TEST(SelfRelocationHeading, TheEntityRowStillCarriesTheServerHeadingVerbatim) {
    TestBridge bridge;
    EverQuest eq = MakeEq();
    eq.setBridge(&bridge);
    SeedOurOwnSpawn(eq, bridge);

    ReplayZone(eq, HC_OP_ClientUpdate,
               ClientUpdatePayload(kMySpawn, 428, -74, 3, kRawHeading12Bit));

    float entityHeading = -1.0f;
    for (const auto& e : bridge.drainEvents()) {
        if (e.type == eqt::state::GameEventType::EntityMoved) {
            const auto& d = std::get<eqt::state::EntityMovedData>(e.data);
            if (d.spawnId == kMySpawn) entityHeading = d.heading;
        }
    }
    EXPECT_FLOAT_EQ(entityHeading, 112.5f)
        << "the entity row's heading is no longer the raw server degrees";
}

}  // namespace
