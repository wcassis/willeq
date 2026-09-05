// T288: position/heading, spawn id and zone identity have ONE store each.
//
// T286 swept 107 duplicate members out of EverQuest but kept eight, seven of
// them (m_x/m_y/m_z/m_heading, m_my_spawn_id, m_current_zone_name/
// m_current_zone_id) on a size-and-risk judgement rather than a wire-path
// need. This file pins the convergence. Every assertion here is a
// COHERENCE assertion: the value the public EverQuest accessor reports and
// the value the state layer reports must be the same value, because after
// T288 they ARE the same storage.
//
// Each test was measured RED against the pre-T288 source (see the unit's
// Review table): the packet handlers below wrote EverQuest's private copy
// and left PlayerState/WorldState stale, so anything reading the state layer
// -- EverQuest::GetHeading(), EqActionHandler::clickNearestDoor(),
// getNearestPlayer(), GameState::playerPosition() -- saw the zone-in value
// forever.
//
// TRAPS honoured here (they burned earlier units, do not "harmonise" them):
//   - Coordinate order. The server sends INVERSEXY: client x <- server y,
//     client y <- server x. Every expectation below states which order the
//     number is in.
//   - Heading. The player's own heading carries a deliberate 90-degree flip
//     (client_heading = 90 - server_heading_deg) that NPCs do not share.
//     These tests assert the flipped value; they do not assert that players
//     and NPCs agree, because they must not.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <string>
#include <vector>

using namespace eqt;
using namespace zonetest;

namespace {

constexpr uint16_t kSelfId = 460;

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// ClientUpdate: spawn_id (2) + the 20-byte bit-packed position block
// (eq_entities.cpp ZoneProcessClientUpdate). Same builder shape as
// test_game_state_source_writes.cpp.
std::vector<uint8_t> ClientUpdatePayload(uint16_t spawn_id, int32_t server_x, int32_t server_y,
                                         int32_t z, uint32_t raw_heading, uint32_t animation) {
    PosFields f = EncodePos(server_x, server_y, z, raw_heading, animation);
    return BuildPayload(24, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawn_id);
        p.PutUInt32(4, f.field1);
        p.PutUInt32(8, f.field2);
        p.PutUInt32(12, f.field3);
        p.PutUInt32(16, f.field4);
        p.PutUInt32(20, 0);
    });
}

// PlayerProfile, populated only at the fields ZoneProcessPlayerProfile's
// position/heading/entity-id block reads (offsets include the 2-byte opcode).
std::vector<uint8_t> PlayerProfilePayload(float server_x, float server_y, float z,
                                          float wire_heading, uint32_t entity_id) {
    return BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutFloat(2 + 13116, server_x);
        p.PutFloat(2 + 13120, server_y);
        p.PutFloat(2 + 13124, z);
        p.PutFloat(2 + 13128, wire_heading);
        p.PutUInt32(2 + 14384, entity_id);
    });
}

// NewZone: char_name[64] then zone_short_name at packet offset 66, zone_id at
// packet offset 686, safe coords at 494/498/502
// (ZoneProcessNewZone, eq_zone_entry.cpp).
std::vector<uint8_t> NewZonePayload(const std::string& shortName, uint16_t zoneId,
                                    float safe_server_x, float safe_server_y, float safe_z) {
    return BuildPayload(2 + 700, [&](EQ::Net::DynamicPacket& p) {
        std::memset(p.Data(), 0, 2 + 700);
        std::memcpy(static_cast<uint8_t*>(p.Data()) + 66, shortName.c_str(), shortName.size() + 1);
        p.PutFloat(494, safe_server_y);   // struct safe_y = server Y
        p.PutFloat(498, safe_server_x);   // struct safe_x = server X
        p.PutFloat(502, safe_z);
        p.PutUInt16(686, zoneId);
    });
}

}  // namespace

// ---------------------------------------------------------------------------
// Position and heading
// ---------------------------------------------------------------------------

TEST(PositionStoreCoherence, SelfClientUpdate_PositionAndHeadingReachTheStateLayer) {
    // RED pre-T288: ZoneProcessClientUpdate's "this is us" branch wrote
    // m_x/m_y/m_z/m_heading and nothing else, so PlayerState kept whatever
    // the last SetPosition() left there -- (0,0,0) in this fixture. Every
    // state-layer reader of the player's position (clickNearestDoor,
    // getNearestPlayer, GameState::playerPosition) was reading that stale
    // value for the whole session.
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kSelfId);

    // server_x=80 raw (/8 -> 10), server_y=160 (-> 20), z=24 (-> 3),
    // raw heading 512 of 2048 -> server_h = 90 deg.
    ReplayZone(eq, HC_OP_ClientUpdate, ClientUpdatePayload(kSelfId, 80, 160, 24, 512, 0));

    // INVERSEXY: client x <- server y (20), client y <- server x (10).
    const glm::vec3 pos = eq.GetPosition();
    EXPECT_FLOAT_EQ(pos.x, 20.0f);
    EXPECT_FLOAT_EQ(pos.y, 10.0f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);

    // One store: the state layer reports the same numbers, in the same order.
    const glm::vec3 statePos = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(statePos.x, pos.x);
    EXPECT_FLOAT_EQ(statePos.y, pos.y);
    EXPECT_FLOAT_EQ(statePos.z, pos.z);
    EXPECT_FLOAT_EQ(eq.GetGameState().playerPosition().x, pos.x);

    // Player's own heading carries the deliberate 90-degree flip:
    // 90 - 90 = 0. GetHeading() reads the state layer.
    EXPECT_FLOAT_EQ(eq.GetHeading(), 0.0f);
}

TEST(PositionStoreCoherence, SelfClientUpdate_HeadingFlipIsStillPlayerOnly) {
    // The 90-degree flip is applied to the PLAYER and deliberately not to
    // other entities (docs/CLAUDE.md, Heading/Rotation Conventions;
    // heading-90-offset-load-bearing). Converging the position STORE must not
    // converge the two conventions -- this asserts they still differ.
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kSelfId);

    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0, /*level=*/5)));
    // raw heading 1024 of 2048 -> server_h = 180 deg for both.
    ReplayZone(eq, HC_OP_ClientUpdate, ClientUpdatePayload(100, 80, 160, 24, 1024, 0));
    ReplayZone(eq, HC_OP_ClientUpdate, ClientUpdatePayload(kSelfId, 80, 160, 24, 1024, 0));

    const auto* rat = eq.GetGameState().entities().getEntity(100);
    ASSERT_NE(rat, nullptr);
    EXPECT_FLOAT_EQ(rat->heading, 180.0f);          // NPC: server heading verbatim
    EXPECT_FLOAT_EQ(eq.GetHeading(), 270.0f);       // player: 90 - 180 -> 270
}

TEST(PositionStoreCoherence, PlayerProfile_PositionAndHeadingAgreeAcrossAccessors) {
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);

    // wire heading 128 of 512 -> server_heading_deg = 90 -> client 90-90 = 0.
    ReplayZone(eq, HC_OP_PlayerProfile,
               PlayerProfilePayload(/*server_x=*/50.0f, /*server_y=*/75.0f, /*z=*/3.0f,
                                    /*wire_heading=*/128.0f, /*entity_id=*/kSelfId));

    const glm::vec3 pos = eq.GetPosition();
    EXPECT_FLOAT_EQ(pos.x, 75.0f);   // client x <- server y
    EXPECT_FLOAT_EQ(pos.y, 50.0f);   // client y <- server x
    EXPECT_FLOAT_EQ(pos.z, 3.0f);

    const glm::vec3 statePos = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(statePos.x, pos.x);
    EXPECT_FLOAT_EQ(statePos.y, pos.y);
    EXPECT_FLOAT_EQ(statePos.z, pos.z);
    EXPECT_FLOAT_EQ(eq.GetHeading(), 0.0f);
}

TEST(PositionStoreCoherence, MovementIntegratorStepIsVisibleToTheStateLayer) {
    // RED pre-T288: UpdateMovement's arrival branch assigned the movement
    // target into m_x/m_y/m_z directly. The state layer never saw a single
    // step of the integrator.
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kSelfId);

    eq.Movement().MoveTo(30.0f, 40.0f, 5.0f);
    eq.Movement().Test_SnapToMovementTargetForSeam();

    const glm::vec3 pos = eq.GetPosition();
    EXPECT_FLOAT_EQ(pos.x, 30.0f);
    EXPECT_FLOAT_EQ(pos.y, 40.0f);
    EXPECT_FLOAT_EQ(pos.z, 5.0f);

    const glm::vec3 statePos = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(statePos.x, pos.x);
    EXPECT_FLOAT_EQ(statePos.y, pos.y);
    EXPECT_FLOAT_EQ(statePos.z, pos.z);
}

// ---------------------------------------------------------------------------
// Spawn id
// ---------------------------------------------------------------------------

TEST(PositionStoreCoherence, PlayerProfile_SpawnIdReachesTheStateLayer) {
    // RED pre-T288: ZoneProcessPlayerProfile wrote m_my_spawn_id from the
    // profile's entity-id field and did NOT mirror it to
    // PlayerState::m_spawnId (four other write sites did). On an ordinary
    // zone-in the profile is where the id comes from, so
    // EqActionHandler::targetNearestPlayer()'s "skip myself" argument
    // (player.spawnId()) was 0 and could never exclude the player.
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);

    ReplayZone(eq, HC_OP_PlayerProfile,
               PlayerProfilePayload(0.0f, 0.0f, 0.0f, 0.0f, /*entity_id=*/kSelfId));

    EXPECT_EQ(eq.GetMySpawnID(), kSelfId);
    EXPECT_EQ(eq.GetEntityID(), kSelfId);
    EXPECT_EQ(eq.GetGameState().player().spawnId(), kSelfId);
}

TEST(PositionStoreCoherence, ClientUpdateAdoptsSpawnIdIntoTheOneStore) {
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    ASSERT_EQ(eq.GetMySpawnID(), 0);

    // With no spawn id yet, the self branch is not taken; the id is adopted
    // only by the "spawn id still zero" path below it. Drive it by replaying
    // an update for an id we do not yet know.
    ReplayZone(eq, HC_OP_ClientUpdate, ClientUpdatePayload(0, 80, 160, 24, 512, 0));

    EXPECT_EQ(eq.GetMySpawnID(), eq.GetGameState().player().spawnId());
}

// ---------------------------------------------------------------------------
// Zone identity
// ---------------------------------------------------------------------------

TEST(PositionStoreCoherence, NewZoneWritesOneZoneIdentity) {
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);

    ReplayZone(eq, HC_OP_NewZone, NewZonePayload("befallen", 36, 10.0f, 20.0f, 3.0f));

    EXPECT_EQ(eq.GetCurrentZoneName(), "befallen");
    EXPECT_EQ(eq.GetGameState().world().zoneName(), "befallen");
    EXPECT_EQ(eq.GetGameState().world().zoneId(), 36);
}

TEST(PositionStoreCoherence, CleanupZoneClearsTheOneZoneIdentity) {
    // RED pre-T288: CleanupZone() cleared EverQuest's m_current_zone_name /
    // m_current_zone_id but left WorldState holding the departed zone, so
    // GetCurrentZoneName() (which already read WorldState) and the zone-load
    // snapshot (which read the EverQuest copy) disagreed for the whole
    // teardown window. T288 clears the single store; ConsoleRenderer must
    // NOT be told the zone "changed" to the empty string, so the clear is
    // deliberately event-free (WorldState::clearZone).
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    RecordingBridge bridge;
    eq.setBridge(&bridge);

    ReplayZone(eq, HC_OP_NewZone, NewZonePayload("befallen", 36, 10.0f, 20.0f, 3.0f));
    ASSERT_EQ(eq.GetCurrentZoneName(), "befallen");

    int zoneChangedOnStateBus = 0;
    eq.GetGameState().events().subscribe(
        eqt::state::GameEventType::ZoneChanged,
        [&](const eqt::state::GameEvent&) { ++zoneChangedOnStateBus; });

    eq.Zone().CleanupZone();

    EXPECT_EQ(eq.GetCurrentZoneName(), "");
    EXPECT_EQ(eq.GetGameState().world().zoneName(), "");
    EXPECT_EQ(eq.GetGameState().world().zoneId(), 0);
    EXPECT_EQ(zoneChangedOnStateBus, 0)
        << "teardown must not publish a ZoneChanged to the empty string";
}

TEST(PositionStoreCoherence, ZoneToSecondZoneAndBackKeepsIdentityCoherent) {
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);

    ReplayZone(eq, HC_OP_NewZone, NewZonePayload("befallen", 36, 10.0f, 20.0f, 3.0f));
    ASSERT_EQ(eq.GetGameState().world().zoneId(), 36);

    eq.Zone().CleanupZone();
    ReplayZone(eq, HC_OP_NewZone, NewZonePayload("arena", 77, 1.0f, 2.0f, 3.0f));
    EXPECT_EQ(eq.GetCurrentZoneName(), "arena");
    EXPECT_EQ(eq.GetGameState().world().zoneId(), 77);

    eq.Zone().CleanupZone();
    ReplayZone(eq, HC_OP_NewZone, NewZonePayload("befallen", 36, 10.0f, 20.0f, 3.0f));
    EXPECT_EQ(eq.GetCurrentZoneName(), "befallen");
    EXPECT_EQ(eq.GetGameState().world().zoneId(), 36);
}
