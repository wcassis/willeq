// T52: pins the x/y coordinate convention at the three sites that touch
// m_pending_zone_x/y/z (two producers, one consumer), so a future edit that
// re-introduces a swap fails a fast test rather than a play session.
//
// Ground truth established by this unit (see
// refactor/T52_zone_position_convention.md Review for the full wire-capture
// evidence this is pinned against):
//
//   - Producer A (ZoneProcessZoneChange, eq_zone.cpp): parses the server's
//     ZoneChange_Struct reply (offset 70 = server's y-field, offset 74 =
//     server's x-field, both AFTER the 2-byte opcode prefix) and swaps into
//     CLIENT format: m_pending_zone_x = wire-offset-70 (server's y),
//     m_pending_zone_y = wire-offset-74 (server's x). This is CORRECT and
//     matches the same convention used by the (separately verified, live)
//     PlayerProfile handler (ZoneProcessPlayerProfile, eq_zone_entry.cpp).
//   - Producer B (CheckZoneLine, eq_zone.cpp): stores result.targetX/Y/Z
//     straight through with NO swap -- confirmed live to already be in
//     CLIENT format (sourced from the zone's own WLD-derived zone-line
//     data, which shares the client's coordinate space per docs/CLAUDE.md
//     convention #3). Pinned here at the EQT::ZoneLines level (the class
//     CheckZoneLine delegates to), since CheckZoneLine itself is private
//     with no live-connection-free way to drive it end to end.
//   - Consumer (WorldProcessZoneServerInfo, eq_login_world.cpp): used to
//     apply a SECOND swap on the (already-client-format) producer-A output,
//     which would have transposed x/y for every server-solicited teleport
//     if this branch ever ran. Fixed to a straight assignment. Pinned here
//     via Test_SetZoneConnectedForSeam(true) (a T52 test-only seam) since,
//     per this unit's live evidence, the branch's real gating condition
//     (`m_zone_connected || m_zone_connection != nullptr`) is never true in
//     the traced production flows -- ProcessDeferredZoneChange always
//     disconnects before WorldProcessZoneServerInfo runs.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "client/zone_lines.h"
#include "common/net/packet.h"

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

namespace {

// Same idiom as test_zone_connection_flow.cpp / test_session_replay.cpp:
// build the full packet at the offsets the real handler reads (offsets
// include the 2-byte opcode prefix), then strip the prefix for the replay
// seam (which re-adds it).
std::vector<uint8_t> BuildPayload(size_t totalPacketSize,
                                   const std::function<void(EQ::Net::DynamicPacket&)>& fill) {
    EQ::Net::DynamicPacket p;
    p.Resize(totalPacketSize);
    fill(p);
    std::vector<uint8_t> payload(totalPacketSize - 2);
    std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    return payload;
}

void ReplayZone(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.ReplayZonePacket(opcode, payload.data(), payload.size());
}

void ReplayWorld(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.LoginWorld().ReplayWorldPacket(opcode, payload.data(), payload.size());
}

// Builds a synthetic OP_ZoneChange server reply (ZoneChange_Struct, 88 bytes
// + 2-byte opcode = 90), with distinct, unambiguous wire-offset values so an
// x/y transposition is unmistakable in the assertion.
std::vector<uint8_t> BuildZoneChangeReply(uint16_t zone_id, float wire_y_field, float wire_x_field,
                                           float wire_z, int32_t success) {
    return BuildPayload(90, [&](EQ::Net::DynamicPacket& p) {
        std::memset(p.Data(), 0, 90);
        p.PutUInt16(0, 0x5dd8);  // HC_OP_ZoneChange (must match the opcode passed to Replay)
        // char_name[64] at offset 2 -- left zero, not read for this test
        p.PutUInt16(66, zone_id);
        p.PutUInt16(68, 0);       // instance_id
        p.PutFloat(70, wire_y_field);  // "server sends y first"
        p.PutFloat(74, wire_x_field);  // "then x"
        p.PutFloat(78, wire_z);
        p.PutUInt32(82, 0);       // zone_reason (unused by this test)
        p.PutInt32(86, success);
    });
}

// Builds a synthetic OP_ZonePlayerToBind packet (ZonePlayerToBind_Struct,
// 20 fixed bytes + 2-byte opcode = 22, no zone_name), with distinct,
// unambiguous wire-offset values so an x/y transposition is unmistakable.
std::vector<uint8_t> BuildZonePlayerToBindPacket(uint16_t zone_id, uint16_t instance_id,
                                                  float wire_x, float wire_y, float wire_z,
                                                  float heading) {
    return BuildPayload(22, [&](EQ::Net::DynamicPacket& p) {
        std::memset(p.Data(), 0, 22);
        p.PutUInt16(0, 0x385e);   // HC_OP_ZonePlayerToBind
        p.PutUInt16(2, zone_id);
        p.PutUInt16(4, instance_id);
        p.PutFloat(6, wire_x);    // "Server x"
        p.PutFloat(10, wire_y);   // "Server y"
        p.PutFloat(14, wire_z);
        p.PutFloat(18, heading);
    });
}

std::vector<uint8_t> BuildZoneServerInfoPacket() {
    return BuildPayload(132, [&](EQ::Net::DynamicPacket& p) {
        std::memset(p.Data(), 0, 132);
        // GetCString(2) reads a NUL-terminated host string starting at
        // offset 2; an all-zero buffer yields an empty string, which is
        // fine -- WorldProcessZoneServerInfo's position-transfer branch
        // doesn't care about the host/port fields at all.
        p.PutUInt16(130, 0);  // port
    });
}

// T58: builds a full-size (19588 fixed body + 2-byte opcode) PlayerProfile
// payload, populated ONLY at the fields ZoneProcessPlayerProfile's
// position/heading block reads (eq_zone_entry.cpp since J02 -- server_x@13116,
// server_y@13120, z@13124, heading@13128, all offsets +base(=2)). Same idiom
// as test_zone_connection_flow.cpp's PlayerProfile builders.
std::vector<uint8_t> BuildPlayerProfilePayload(float server_x, float server_y, float z,
                                                float wire_heading) {
    return BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutFloat(2 + 13116, server_x);
        p.PutFloat(2 + 13120, server_y);
        p.PutFloat(2 + 13124, z);
        p.PutFloat(2 + 13128, wire_heading);
    });
}

// T58: records every event pushed to the bridge (does not apply them to a
// renderer), so tests can assert exactly how many PlayerMoved events reach
// the renderer-facing queue and in what format -- same idiom as
// test_zone_connection_flow.cpp's RecordingBridge.
class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

}  // namespace

// ---------------------------------------------------------------------------
// Producer A: ZoneProcessZoneChange (eq_zone.cpp)
// ---------------------------------------------------------------------------

TEST(ZonePositionConvention, ProducerA_SwapsWireOffsetsIntoClientFormat) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Distinct values so a transposition is unmistakable: wire's y-field
    // (offset 70) = 111.0, wire's x-field (offset 74) = 222.0.
    auto payload = BuildZoneChangeReply(/*zone_id=*/36, /*wire_y_field=*/111.0f,
                                         /*wire_x_field=*/222.0f, /*wire_z=*/3.0f,
                                         /*success=*/1);
    ReplayZone(eq, 0x5dd8, payload);

    // Established convention: client_x = wire's y-field, client_y = wire's
    // x-field (matches the live-verified PlayerProfile handler's identical
    // relationship). A regression that removes/inverts the swap would make
    // this 222.0/111.0 instead.
    EXPECT_FLOAT_EQ(eq.GetPendingZoneX(), 111.0f);
    EXPECT_FLOAT_EQ(eq.GetPendingZoneY(), 222.0f);
    EXPECT_FLOAT_EQ(eq.GetPendingZoneZ(), 3.0f);
    EXPECT_EQ(eq.GetPendingZoneId(), 36);
}

TEST(ZonePositionConvention, ProducerA_DeniedChangeClearsPendingZone) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto payload = BuildZoneChangeReply(36, 111.0f, 222.0f, 3.0f, /*success=*/0);
    ReplayZone(eq, 0x5dd8, payload);

    EXPECT_EQ(eq.GetPendingZoneId(), 0);
}

// ---------------------------------------------------------------------------
// Producer B: CheckZoneLine's data source (EQT::ZoneLines::checkPosition)
// ---------------------------------------------------------------------------
//
// CheckZoneLine itself is private with no live-connection-free replay path
// (it is driven from UpdateMovement() on real position deltas against a
// loaded WLD zone), so this pins the convention at the class it delegates
// to: given a destination, checkPosition() must return it UNCHANGED (no
// swap) -- eq_zone.cpp's CheckZoneLine then copies result.targetX/Y/Z
// straight into m_pending_zone_x/y/z with no further transformation
// (verified by inspection at the exact assignment site, eq_zone.cpp
// "Trigger zone line!" block).

TEST(ZonePositionConvention, ProducerB_ZoneLinesPassesDestinationThroughUnswapped) {
    // Real fixture, same one test_zone_lines.cpp's pre-existing
    // LegacyFormat_AkanonPinnedFields test uses (data/zone_lines.json,
    // "akanon" entry) -- that test already independently pins this exact
    // property (target coordinates equal the JSON's raw "destination"
    // verbatim, no swap); re-asserted here under this unit's own test name
    // so a regression in either producer's convention is caught by a test
    // whose name says which of the three sites (A/B/consumer) broke,
    // matching this unit's explicit ask to pin the convention "at each of
    // the three sites."
    EQT::ZoneLines zl;
    ASSERT_TRUE(zl.loadFromExtractedJson("akanon", std::string(WILLEQ_REPO_ROOT) + "/data/zone_lines.json"));

    EQT::ZoneLineResult result = zl.checkPosition(54.0f, -77.0f, 0.0f);
    ASSERT_TRUE(result.isZoneLine);
    EXPECT_EQ(result.targetZoneId, 56u);  // steamfont
    // No swap: the JSON's "destination":{x:528,y:-2062,z:-110} passes
    // straight through as targetX/Y/Z -- this IS producer B's convention
    // (client format already, per docs/CLAUDE.md's S3D/WLD convention #3),
    // matching what CheckZoneLine (eq_zone.cpp) then copies verbatim into
    // m_pending_zone_x/y/z.
    EXPECT_FLOAT_EQ(result.targetX, 528.0f);
    EXPECT_FLOAT_EQ(result.targetY, -2062.0f);
    EXPECT_FLOAT_EQ(result.targetZ, -110.0f);
}

// ---------------------------------------------------------------------------
// Consumer: WorldProcessZoneServerInfo (eq_login_world.cpp)
// ---------------------------------------------------------------------------

TEST(ZonePositionConvention, Consumer_AppliesPendingZoneWithoutASecondSwap) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Prime m_pending_zone_x/y/z/id via producer A (already client-format).
    auto zc_payload = BuildZoneChangeReply(36, /*wire_y_field=*/111.0f,
                                            /*wire_x_field=*/222.0f, /*wire_z=*/3.0f,
                                            /*success=*/1);
    ReplayZone(eq, 0x5dd8, zc_payload);
    ASSERT_EQ(eq.GetPendingZoneId(), 36);
    ASSERT_FLOAT_EQ(eq.GetPendingZoneX(), 111.0f);
    ASSERT_FLOAT_EQ(eq.GetPendingZoneY(), 222.0f);

    // T52 test seam: force the consumer's real gating condition true so its
    // (fixed) position-transfer branch actually executes. See this test
    // file's header comment and Test_SetZoneConnectedForSeam's own comment
    // in eq.h for why this cannot be reached via ordinary replay.
    eq.Test_SetZoneConnectedForSeam(true);

    auto zsi_payload = BuildZoneServerInfoPacket();
    ReplayWorld(eq, 0x61b6, zsi_payload);  // HC_OP_ZoneServerInfo

    // Fixed behavior: straight assignment, no second swap. Before this
    // unit's fix, this would have been (222.0, 111.0) instead.
    auto pos = eq.GetPosition();
    EXPECT_FLOAT_EQ(pos.x, 111.0f);
    EXPECT_FLOAT_EQ(pos.y, 222.0f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);
}

// ---------------------------------------------------------------------------
// T55: Producer C (ZoneProcessZonePlayerToBind, eq_player_state.cpp) -- death/bind respawn.
//
// T52 found this producer and confirmed it uses the same correct swap as
// producer A, but it had NO test at all. This closes that coverage gap.
// ---------------------------------------------------------------------------

TEST(ZonePositionConvention, ProducerC_SwapsWireOffsetsIntoClientFormat) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Distinct values so a transposition is unmistakable: wire's x-field
    // (offset 6) = 333.0, wire's y-field (offset 10) = 444.0.
    auto payload = BuildZonePlayerToBindPacket(/*zone_id=*/36, /*instance_id=*/0,
                                                 /*wire_x=*/333.0f, /*wire_y=*/444.0f,
                                                 /*wire_z=*/5.0f, /*heading=*/128.0f);
    ReplayZone(eq, 0x385e, payload);

    // Same convention as producers A/B: client_x = wire's y-field,
    // client_y = wire's x-field.
    EXPECT_FLOAT_EQ(eq.GetPendingZoneX(), 444.0f);
    EXPECT_FLOAT_EQ(eq.GetPendingZoneY(), 333.0f);
    EXPECT_FLOAT_EQ(eq.GetPendingZoneZ(), 5.0f);
    EXPECT_EQ(eq.GetPendingZoneId(), 36);
}

// ---------------------------------------------------------------------------
// T55: the propagation gap -- SetPosition (eq_player_state.cpp) has zero production
// callers, so forced relocations write the raw m_x/m_y/m_z members but never
// the state layer's PlayerState. These pin the fix at the two forced-
// relocation sites in this unit's territory that had no local position write
// at all before the fix: the cross-zone and same-zone branches of
// ZoneProcessZonePlayerToBind (producer C, death/bind respawn).
// ---------------------------------------------------------------------------

TEST(ZonePositionConvention, ForcedRelocation_CrossZoneDeathRespawnDoesNotWriteLocalPosition) {
    // T55 (coordinator review, post-initial-landing): the FIRST version of
    // this unit wrote the bind position unconditionally, including on the
    // cross-zone branch. That was a defect: at the moment this handler
    // runs, the client is still fully zoned in to the OLD zone --
    // IsFullyZonedIn() is true and m_zone_connection is still live; the
    // disconnect only happens later, when ProcessDeferredZoneChange runs
    // after the OP_ZoneChange handshake completes. SendPositionUpdate()
    // (eq_movement.cpp) is gated on that exact same
    // IsFullyZonedIn()+m_zone_connection check, throttled only by a 250ms
    // timer -- so writing the bind ZONE's coordinates into m_x/m_y/m_z here
    // on a cross-zone death would let the client transmit them to the OLD
    // zone's server on its very next position update, which will accept
    // and persist them. T52's failure mode, reintroduced -- and worse on
    // the denied path (see ProducerA_DeniedChangeClearsPendingZone above):
    // a denied cross-zone change would leave the character sitting in the
    // OLD zone at coordinates belonging to a different zone. This pins the
    // fix: the cross-zone branch must NOT write local position. The
    // far-side landing position is the zone-in consumer's job
    // (WorldProcessZoneServerInfo / PlayerProfile), not this handler's.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // zone_id=99, distinct from the default m_current_zone_id (0) -- the
    // cross-zone branch of ZoneProcessZonePlayerToBind.
    auto payload = BuildZonePlayerToBindPacket(/*zone_id=*/99, /*instance_id=*/0,
                                                 /*wire_x=*/50.0f, /*wire_y=*/75.0f,
                                                 /*wire_z=*/3.0f, /*heading=*/100.0f);
    ReplayZone(eq, 0x385e, payload);

    // Local position (both homes) must stay untouched -- still the
    // constructor's (0,0,0) default.
    glm::vec3 statePos = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(statePos.x, 0.0f);
    EXPECT_FLOAT_EQ(statePos.y, 0.0f);
    EXPECT_FLOAT_EQ(statePos.z, 0.0f);

    glm::vec3 rawPos = eq.GetPosition();
    EXPECT_FLOAT_EQ(rawPos.x, 0.0f);
    EXPECT_FLOAT_EQ(rawPos.y, 0.0f);
    EXPECT_FLOAT_EQ(rawPos.z, 0.0f);

    // But the pending-zone state IS populated (established convention:
    // client_x = wire's y-field (75), client_y = wire's x-field (50)) --
    // that part is still correct and unaffected by this fix.
    EXPECT_EQ(eq.GetPendingZoneId(), 99);
    EXPECT_FLOAT_EQ(eq.GetPendingZoneX(), 75.0f);
    EXPECT_FLOAT_EQ(eq.GetPendingZoneY(), 50.0f);
    EXPECT_FLOAT_EQ(eq.GetPendingZoneZ(), 3.0f);
}

TEST(ZonePositionConvention, ForcedRelocation_SameZoneDeathRespawnPropagatesLocally) {
    // T55 headline finding: ZoneProcessZonePlayerToBind's same-zone branch
    // (target_zone_id == m_current_zone_id, or == 0) logs "Same-zone
    // respawn" but, before this unit's fix, performed NO local position
    // write at all -- relying entirely on a server PlayerProfile round-trip
    // that may arrive some time later (and, per PlayerProfile's own code in
    // this same file, doesn't even write the state layer's PlayerState when
    // it does arrive -- see this unit's Review). SUSPECTED by the plan;
    // CONFIRMED here.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // zone_id=0 unambiguously selects the same-zone branch regardless of
    // m_current_zone_id's default value (ZoneProcessZonePlayerToBind treats
    // target_zone_id==0 as same-zone too).
    auto payload = BuildZonePlayerToBindPacket(/*zone_id=*/0, /*instance_id=*/0,
                                                 /*wire_x=*/10.0f, /*wire_y=*/20.0f,
                                                 /*wire_z=*/1.0f, /*heading=*/64.0f);
    ReplayZone(eq, 0x385e, payload);

    // client_x = wire's y-field (20), client_y = wire's x-field (10).
    glm::vec3 statePos = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(statePos.x, 20.0f);
    EXPECT_FLOAT_EQ(statePos.y, 10.0f);
    EXPECT_FLOAT_EQ(statePos.z, 1.0f);

    glm::vec3 rawPos = eq.GetPosition();
    EXPECT_FLOAT_EQ(rawPos.x, 20.0f);
    EXPECT_FLOAT_EQ(rawPos.y, 10.0f);
    EXPECT_FLOAT_EQ(rawPos.z, 1.0f);
}

// ---------------------------------------------------------------------------
// T58: ZoneProcessPlayerProfile (eq_zone_entry.cpp) -- the packet that actually
// determines landing position on ordinary zone-in. T55 deliberately left
// this site alone (own territory, own risk); this unit closes it.
//
// Step 1's traced finding (see refactor/T58_playerprofile_propagation.md
// Review for the full evidence): EverQuest::m_game_state's state-layer
// EventBus (fired by SetPosition/SetHeading via PlayerState::setPosition/
// setHeading) has ZERO production listeners and is never forwarded to the
// GameStateBridge -- it is a completely different queue from the one
// ZoneProcessPlayerProfile's hand-rolled `m_bridge->pushEvent(...)` call
// feeds. Routing the state write through SetPosition/SetHeading is
// therefore additive, not a double-publish: exactly one PlayerMoved still
// reaches the bridge, from the existing hand-rolled push, unchanged.
// ---------------------------------------------------------------------------

TEST(ZonePositionConvention, PlayerProfile_PropagatesToGameStatePlayerPosition) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Distinct values so an x/y transposition is unmistakable: server_x=333,
    // server_y=444. Established convention (ZoneProcessPlayerProfile,
    // eq_zone_entry.cpp): client m_x = server_y, client m_y = server_x.
    auto payload = BuildPlayerProfilePayload(/*server_x=*/333.0f, /*server_y=*/444.0f,
                                              /*z=*/5.0f, /*wire_heading=*/128.0f);
    ReplayZone(eq, HC_OP_PlayerProfile, payload);

    // Legacy raw members (already correct pre-fix -- not what this unit
    // changes).
    glm::vec3 rawPos = eq.GetPosition();
    EXPECT_FLOAT_EQ(rawPos.x, 444.0f);
    EXPECT_FLOAT_EQ(rawPos.y, 333.0f);
    EXPECT_FLOAT_EQ(rawPos.z, 5.0f);

    // The state layer's own copy -- this is the propagation gap. Pre-fix,
    // ZoneProcessPlayerProfile never wrote m_game_state.player(), so this
    // stays at the constructor's (0,0,0) default.
    glm::vec3 statePos = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(statePos.x, 444.0f);
    EXPECT_FLOAT_EQ(statePos.y, 333.0f);
    EXPECT_FLOAT_EQ(statePos.z, 5.0f);
}

TEST(ZonePositionConvention, PlayerProfile_HeadingConversionPreservedBitForBit) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // wire_heading=128.0 (12-bit-scaled-value/4 units, per
    // ZoneProcessPlayerProfile in eq_zone_entry.cpp):
    // server_heading_deg = 128 * 360 / 512 = 90.0; m_heading = 90 - 90 = 0.0.
    // Chosen deliberately non-degenerate for the m_heading<0/>=360 wrap
    // branches to stay exercised elsewhere (ProducerC's own test uses a
    // different wire_heading); this test's job is only to confirm the exact
    // same arithmetic result reaches the state layer.
    auto payload = BuildPlayerProfilePayload(/*server_x=*/1.0f, /*server_y=*/2.0f,
                                              /*z=*/3.0f, /*wire_heading=*/64.0f);
    ReplayZone(eq, HC_OP_PlayerProfile, payload);

    // wire_heading=64: server_heading_deg = 64*360/512 = 45.0;
    // m_heading = 90 - 45 = 45.0.
    const float kExpectedHeadingDeg = 45.0f;

    // GetHeading() already reads m_game_state.player().heading() (it always
    // has -- see EverQuest::GetHeading() in eq_player_state.cpp, unrelated to this unit). Pre-fix,
    // ZoneProcessPlayerProfile never wrote that field on the ordinary
    // zone-in path, so GetHeading() would return the constructor's 0.0
    // default here instead of the computed value -- this is the "renders in
    // the wrong facing direction on zone-in" half of the gap.
    EXPECT_FLOAT_EQ(eq.GetHeading(), kExpectedHeadingDeg);
}

TEST(ZonePositionConvention, PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);

    // wire_heading=64 -> m_heading=45.0deg (see the test above) ->
    // heading512 = 45.0 * 512/360 = 64.0 (the format the hand-rolled bridge
    // publish in ZoneProcessPlayerProfile (eq_zone_entry.cpp) has always sent -- this unit must not
    // change it, only add the missing state write alongside it).
    auto payload = BuildPlayerProfilePayload(/*server_x=*/1.0f, /*server_y=*/2.0f,
                                              /*z=*/3.0f, /*wire_heading=*/64.0f);
    ReplayZone(eq, HC_OP_PlayerProfile, payload);

    auto events = bridge.drainEvents();
    int playerMovedCount = 0;
    float lastHeading512 = -1.0f;
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::PlayerMoved) {
            playerMovedCount++;
            const auto& d = std::get<eqt::state::PlayerMovedData>(e.data);
            lastHeading512 = d.heading;
        }
    }

    // The headline risk this unit's plan calls out by name: the naive fix
    // (swap raw writes for SetPosition, leave the hand-rolled publish alone)
    // double-publishes because SetPosition/SetHeading also fire PlayerMoved
    // -- but on the state layer's OWN EventBus, which Step 1 established is
    // never forwarded to this bridge. So exactly one PlayerMoved must reach
    // it, still in the 512-scaled heading format the renderer expects.
    EXPECT_EQ(playerMovedCount, 1)
        << "expected exactly one PlayerMoved on the bridge per PlayerProfile zone-in";
    EXPECT_FLOAT_EQ(lastHeading512, 64.0f);
}

// ---------------------------------------------------------------------------
// T231: same-zone OP_RequestClientZoneChange relocates locally, no reconnect
// ---------------------------------------------------------------------------
//
// eqemu's Client::ZonePC (akk-stack zone/zoning.cpp 1055-1068) sends
// OP_RequestClientZoneChange naming the CURRENT zone for every same-zone
// teleport (GM summon, #goto, /rewind ...) and states the contract in its
// own comment: "Client wont generate a zone change packet to the server in
// this case" -- it resets zone_mode to ZoneUnsolicited and waits for the
// client's next OP_ClientUpdate. Pre-T231 ZoneProcessRequestClientZoneChange
// replied with OP_ZoneChange unconditionally, which the server answered with
// SendZoneCancel (zoning.cpp 113) -> teardown -> reconnect -> landed where it
// started (T229 Review, review/T229/).

namespace {

// RequestClientZoneChange_Struct (24 bytes) + 2-byte opcode = 26. Offsets
// are the ones ZoneProcessRequestClientZoneChange reads: zone_id@2,
// instance_id@4, wire y@6 ("server sends y first"), wire x@10, z@14,
// heading@18 (float, eqemu 0-512 units), type@22.
std::vector<uint8_t> BuildRequestClientZoneChange(uint16_t zone_id, uint16_t instance_id,
                                                   float wire_y, float wire_x, float wire_z,
                                                   float wire_heading) {
    return BuildPayload(26, [&](EQ::Net::DynamicPacket& p) {
        std::memset(p.Data(), 0, 26);
        p.PutUInt16(0, 0x7834);   // HC_OP_RequestClientZoneChange
        p.PutUInt16(2, zone_id);
        p.PutUInt16(4, instance_id);
        p.PutFloat(6, wire_y);
        p.PutFloat(10, wire_x);
        p.PutFloat(14, wire_z);
        p.PutFloat(18, wire_heading);
        p.PutUInt32(22, 1);       // type: the observed value the server sends
    });
}

// NewZone_Struct: zone_short_name@66, zone_id@686 (the field that sets
// m_current_zone_id). Same shape test_zone_connection_flow.cpp uses.
void ReplayNewZone(EverQuest& eq, uint16_t zone_id) {
    auto payload = BuildPayload(2 + 700, [&](EQ::Net::DynamicPacket& p) {
        std::memset(p.Data(), 0, 2 + 700);
        p.PutCString(66, "arena");
        p.PutUInt16(686, zone_id);
    });
    ReplayZone(eq, HC_OP_NewZone, payload);
}

int CountPlayerMoved(const std::vector<eqt::state::GameEvent>& events, float* lastHeading512) {
    int n = 0;
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::PlayerMoved) {
            n++;
            if (lastHeading512) {
                *lastHeading512 = std::get<eqt::state::PlayerMovedData>(e.data).heading;
            }
        }
    }
    return n;
}

}  // namespace

TEST(SameZoneRelocation, SameZone_NoZoneChangeReply_PositionHeadingAndBridgeApplied) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);
    ReplayNewZone(eq, 77);
    (void)bridge.drainEvents();  // discard whatever NewZone published

    // T229's live summon: Testchar at server (x=146, y=-1009, z=51). The
    // wire carries y first. wire_heading=64 -> server_heading_deg=45 ->
    // m_heading = 90 - 45 = 45 (the PlayerProfile player-heading rule).
    auto payload = BuildRequestClientZoneChange(/*zone_id=*/77, /*instance_id=*/0,
                                                /*wire_y=*/-1009.0f, /*wire_x=*/146.0f,
                                                /*wire_z=*/51.0f, /*wire_heading=*/64.0f);
    ReplayZone(eq, HC_OP_RequestClientZoneChange, payload);

    // The defect: no OP_ZoneChange decision at all for a same-zone request.
    EXPECT_EQ(eq.Zone().Test_GetLastZoneChangeRequest().attempts, 0)
        << "same-zone request must not reply with OP_ZoneChange";

    // Client coords: m_x = wire y, m_y = wire x (T52 convention).
    glm::vec3 rawPos = eq.GetPosition();
    EXPECT_FLOAT_EQ(rawPos.x, -1009.0f);
    EXPECT_FLOAT_EQ(rawPos.y, 146.0f);
    EXPECT_FLOAT_EQ(rawPos.z, 51.0f);
    glm::vec3 statePos = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(statePos.x, -1009.0f);
    EXPECT_FLOAT_EQ(statePos.y, 146.0f);
    EXPECT_FLOAT_EQ(statePos.z, 51.0f);
    EXPECT_FLOAT_EQ(eq.GetHeading(), 45.0f);
    EXPECT_FALSE(eq.IsMoving());

    // Exactly one PlayerMoved reaches the renderer-facing bridge, heading in
    // the 512 format setPlayerPosition() expects: 45deg * 512/360 = 64.
    float heading512 = -1.0f;
    EXPECT_EQ(CountPlayerMoved(bridge.drainEvents(), &heading512), 1);
    EXPECT_FLOAT_EQ(heading512, 64.0f);
}

TEST(SameZoneRelocation, SameZone_ForcedClientUpdateCarriesDestination) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);   // no live socket in a unit test
    eq.Test_SetMySpawnIdForSeam(42);
    ReplayNewZone(eq, 77);

    auto payload = BuildRequestClientZoneChange(77, 0, /*wire_y=*/-1009.0f, /*wire_x=*/146.0f,
                                                /*wire_z=*/51.0f, /*wire_heading=*/64.0f);
    ReplayZone(eq, HC_OP_RequestClientZoneChange, payload);

    // The OP_ClientUpdate the handler sends must put the destination back in
    // SERVER coordinates: x_pos = m_y (server x), y_pos = m_x (server y);
    // heading 45deg -> server 90-45=45deg -> 45*2048/360 = 256 (12-bit).
    auto sent = eq.Movement().Test_GetLastComputedPositionUpdate();
    EXPECT_FLOAT_EQ(sent.x_pos, 146.0f);
    EXPECT_FLOAT_EQ(sent.y_pos, -1009.0f);
    EXPECT_FLOAT_EQ(sent.z_pos, 51.0f);
    EXPECT_EQ(sent.heading_scaled, 256u);
}

TEST(SameZoneRelocation, CrossZone_StillRepliesWithZoneChange_NoLocalWrite) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);
    ReplayNewZone(eq, 77);
    (void)bridge.drainEvents();

    auto payload = BuildRequestClientZoneChange(/*zone_id=*/2, 0, -1009.0f, 146.0f, 51.0f, 64.0f);
    ReplayZone(eq, HC_OP_RequestClientZoneChange, payload);

    auto req = eq.Zone().Test_GetLastZoneChangeRequest();
    EXPECT_EQ(req.attempts, 1) << "cross-zone request must still answer with OP_ZoneChange";
    EXPECT_EQ(req.zone_id, 2);
    // Cross-zone: the far side's PlayerProfile sets the landing position;
    // nothing is written locally (T55's cross-zone rule).
    glm::vec3 rawPos = eq.GetPosition();
    EXPECT_FLOAT_EQ(rawPos.x, 0.0f);
    EXPECT_FLOAT_EQ(rawPos.y, 0.0f);
    EXPECT_FLOAT_EQ(rawPos.z, 0.0f);
    EXPECT_EQ(CountPlayerMoved(bridge.drainEvents(), nullptr), 0);
}

TEST(SameZoneRelocation, UnknownCurrentZone_KeepsHandshake) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    // No NewZone replayed: m_current_zone_id is 0 -- never guess same-zone.
    auto payload = BuildRequestClientZoneChange(77, 0, -1009.0f, 146.0f, 51.0f, 64.0f);
    ReplayZone(eq, HC_OP_RequestClientZoneChange, payload);
    EXPECT_EQ(eq.Zone().Test_GetLastZoneChangeRequest().attempts, 1);
    EXPECT_FLOAT_EQ(eq.GetPosition().x, 0.0f);
}

TEST(SameZoneRelocation, NonZeroInstance_KeepsHandshake) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    ReplayNewZone(eq, 77);
    // Same zone id but a different instance is a real move (the server's
    // own test is zoneID && instance_id; this client always sits in 0).
    auto payload = BuildRequestClientZoneChange(77, /*instance_id=*/3, -1009.0f, 146.0f, 51.0f, 64.0f);
    ReplayZone(eq, HC_OP_RequestClientZoneChange, payload);
    EXPECT_EQ(eq.Zone().Test_GetLastZoneChangeRequest().attempts, 1);
    EXPECT_FLOAT_EQ(eq.GetPosition().x, 0.0f);
}
