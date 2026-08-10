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
//     PlayerProfile handler in eq.cpp.
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
    eq.ReplayWorldPacket(opcode, payload.data(), payload.size());
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
// T55: Producer C (ZoneProcessZonePlayerToBind, eq.cpp) -- death/bind respawn.
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
// T55: the propagation gap -- SetPosition (eq.cpp) has zero production
// callers, so forced relocations write the raw m_x/m_y/m_z members but never
// the state layer's PlayerState. These pin the fix at the two forced-
// relocation sites in this unit's territory that had no local position write
// at all before the fix: the cross-zone and same-zone branches of
// ZoneProcessZonePlayerToBind (producer C, death/bind respawn).
// ---------------------------------------------------------------------------

TEST(ZonePositionConvention, ForcedRelocation_DeathBindPropagatesToGameStatePlayerPosition) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // zone_id=99, distinct from the default m_current_zone_id (0) -- the
    // cross-zone branch of ZoneProcessZonePlayerToBind.
    auto payload = BuildZonePlayerToBindPacket(/*zone_id=*/99, /*instance_id=*/0,
                                                 /*wire_x=*/50.0f, /*wire_y=*/75.0f,
                                                 /*wire_z=*/3.0f, /*heading=*/100.0f);
    ReplayZone(eq, 0x385e, payload);

    // Established convention: client_x = wire's y-field (75), client_y =
    // wire's x-field (50).
    //
    // T55: before this unit's fix, ZoneProcessZonePlayerToBind stored the
    // pending zone coordinates but made NO local position write at all --
    // this would still read (0,0,0), the EverQuest constructor's default,
    // completely unchanged by the packet just replayed.
    glm::vec3 statePos = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(statePos.x, 75.0f);
    EXPECT_FLOAT_EQ(statePos.y, 50.0f);
    EXPECT_FLOAT_EQ(statePos.z, 3.0f);

    // SetPosition writes both homes -- the legacy raw members must agree.
    glm::vec3 rawPos = eq.GetPosition();
    EXPECT_FLOAT_EQ(rawPos.x, statePos.x);
    EXPECT_FLOAT_EQ(rawPos.y, statePos.y);
    EXPECT_FLOAT_EQ(rawPos.z, statePos.z);
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
