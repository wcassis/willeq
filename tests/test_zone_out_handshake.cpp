// T322: pins the zone-transition CONNECTION LIFECYCLE.
//
// The measured ground truth (T320's oracle capture
// review/T320/legA_qeynos2_zonelog_slice.log, re-read by T322): the official
// Titanium client does NOT close its source-zone socket when it zones. After
// the server's OP_ZoneChange approval it sends
//
//     C->S OP_SaveOnZoneReq   (192-byte body, payload ignored by eqemu)
//     C->S OP_DeleteSpawn     (its own entity id)
//
// and lets the SERVER close the session (eqemu Handle_OP_DeleteSpawn queues
// OP_LogoutReply, broadcasts the despawn, then calls Disconnect()).
//
// That is not cosmetic. Closing the socket ourselves drops the session out of
// ESTABLISHED with no packet, which sends eqemu down
// Client::OnDisconnect(true) -- an unconditional LeaveGroup() that never
// consults bZoning -- and DISBANDS THE GROUP of everyone who zones. The
// graceful hand-off instead lands in the bZoning branch of Client::Process,
// which calls Group::MemberZoned() and keeps the group.
//
// The regression a careless "simplification" causes is the mirror image: the
// source zone's acknowledgement is the SAME OP_LogoutReply a real camp/logout
// produces, so routing it wrongly either quits the game mid-transition or
// stops a genuine logout from disconnecting. Both directions are pinned here.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "client/eq.h"
#include "common/net/packet.h"

namespace {

constexpr uint16_t kOpSaveOnZoneReq = 0x1540;  // HC_OP_SaveOnZoneReq
constexpr uint16_t kOpDeleteSpawn   = 0x55bc;  // HC_OP_DeleteSpawn
constexpr uint16_t kOpLogoutReply   = 0x3cdc;  // HC_OP_LogoutReply

EverQuest MakeClient() {
    return EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar");
}

void ReplayLogoutReply(EverQuest& eq) {
    // OP_LogoutReply is a bare opcode on the wire (size 2, empty body).
    eq.ReplayZonePacket(kOpLogoutReply, nullptr, 0);
}

}  // namespace

// ---------------------------------------------------------------------------
// The hand-off itself
// ---------------------------------------------------------------------------

TEST(ZoneOutHandshake, SendsSaveThenDeleteSpawnCarryingOurOwnEntityId) {
    EverQuest eq = MakeClient();
    eq.Test_SetMySpawnIdForSeam(0x0113);  // the oracle's own capture value
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();

    ASSERT_TRUE(eq.Test_BeginGracefulZoneOutForSeam());

    const auto& sent = eq.Test_GetQueuedPackets();
    ASSERT_EQ(sent.size(), 2u) << "the hand-off is exactly two packets, in order";

    EXPECT_EQ(sent[0].opcode, kOpSaveOnZoneReq);
    EXPECT_EQ(sent[0].bytes.size(), 192u) << "oracle sends 194 on the wire = 2 opcode + 192 body";

    EXPECT_EQ(sent[1].opcode, kOpDeleteSpawn);
    ASSERT_EQ(sent[1].bytes.size(), 4u) << "EntityId_Struct is one uint32";
    uint32_t entity_id = 0;
    std::memcpy(&entity_id, sent[1].bytes.data(), sizeof(entity_id));
    EXPECT_EQ(entity_id, 0x0113u)
        << "must be OUR entity id -- eqemu keys the graceful despawn on it";
}

TEST(ZoneOutHandshake, ArmsTheWaitRatherThanTearingDownImmediately) {
    EverQuest eq = MakeClient();
    ASSERT_TRUE(eq.Test_BeginGracefulZoneOutForSeam());

    EXPECT_TRUE(eq.Test_IsZoneOutHandshakePending())
        << "the source-zone session must stay up until the server answers";
    EXPECT_FALSE(eq.Test_IsZoneOutAcknowledged());
}

TEST(ZoneOutHandshake, WithoutALiveZoneConnectionItFallsBackToTheHardTeardown) {
    EverQuest eq = MakeClient();
    // No seam: the real gate applies. There is nobody to hand off to, so the
    // caller must be told to tear down instead of waiting for an
    // acknowledgement that can never arrive.
    EXPECT_FALSE(eq.Test_BeginGracefulZoneOutForSeam(/*force_gate=*/false));
    EXPECT_FALSE(eq.Test_IsZoneOutHandshakePending());
}

// ---------------------------------------------------------------------------
// OP_LogoutReply routing -- the same opcode means two different things
// ---------------------------------------------------------------------------

TEST(ZoneOutHandshake, LogoutReplyDuringAZoneOutIsAnAcknowledgementNotALogout) {
    EverQuest eq = MakeClient();
    ASSERT_TRUE(eq.Test_BeginGracefulZoneOutForSeam());

    ReplayLogoutReply(eq);

    EXPECT_TRUE(eq.Test_IsZoneOutAcknowledged());
    EXPECT_FALSE(eq.IsQuitRequested())
        << "quitting here would end the session in the middle of a zone change";
    EXPECT_FALSE(eq.Test_IsPendingLogoutDisconnect());
}

// THE regression guard. A logout genuinely should disconnect; the zone-out
// hand-off must not have taken that away.
TEST(ZoneOutHandshake, LogoutReplyWithNoZoneOutInFlightStillDisconnects) {
    EverQuest eq = MakeClient();
    ASSERT_FALSE(eq.Test_IsZoneOutHandshakePending());

    ReplayLogoutReply(eq);

    EXPECT_TRUE(eq.IsQuitRequested());
    EXPECT_TRUE(eq.Test_IsPendingLogoutDisconnect())
        << "UpdateMovement()'s logout branch calls DisconnectFromZone() off this flag";
    EXPECT_FALSE(eq.Test_IsZoneOutAcknowledged());
}

// And once a transition is over, a later logout must behave normally again --
// i.e. the flag is genuinely cleared by the teardown, not left latched.
TEST(ZoneOutHandshake, TeardownClearsTheHandshakeSoALaterLogoutIsUnaffected) {
    EverQuest eq = MakeClient();
    ASSERT_TRUE(eq.Test_BeginGracefulZoneOutForSeam());
    ReplayLogoutReply(eq);
    ASSERT_TRUE(eq.Test_IsZoneOutAcknowledged());

    eq.Test_DisconnectFromZoneForSeam();  // what UpdateMovement()'s wait branch reaches
    EXPECT_FALSE(eq.Test_IsZoneOutHandshakePending());
    EXPECT_FALSE(eq.Test_IsZoneOutAcknowledged());

    ReplayLogoutReply(eq);
    EXPECT_TRUE(eq.IsQuitRequested());
    EXPECT_TRUE(eq.Test_IsPendingLogoutDisconnect());
}

// ---------------------------------------------------------------------------
// The world reconnect's `zoning` byte -- the OTHER half of the lifecycle
// ---------------------------------------------------------------------------
//
// A zone change reconnects to WORLD between the two zone servers, and
// OP_SendLoginInfo carries a `zoning` byte at offset 188. eqemu's world reads
// it as "fresh login" when it is 0 and DELETES the character's group_id row
// (world/client.cpp:934) -- so the source zone can preserve the group
// perfectly and the zoning player still ends up out of it. Measured live
// (T322): fable sent zoning=0 on every login including the zone-to-zone
// reconnect, because the flags the byte was computed from are cleared by
// CleanupZone() -> resetZoneState() one line before the world reconnect.

TEST(ZoneOutHandshake, WorldLoginZoningByteIsLatchedAcrossTheTeardown) {
    EverQuest eq = MakeClient();
    // A fresh client is not zoning.
    EXPECT_FALSE(eq.Test_IsZoneToZoneInProgress());

    // What ProcessDeferredZoneChange() latches before DisconnectFromZone().
    eq.Test_SetZoneToZoneInProgressForSeam(true);

    // The teardown must NOT clear it -- that is the whole bug: the world
    // reconnect happens after this and has to still see "I am zoning".
    eq.Test_DisconnectFromZoneForSeam();
    EXPECT_TRUE(eq.Test_IsZoneToZoneInProgress())
        << "CleanupZone()/resetZoneState() must not be able to reach this flag";
}

TEST(ZoneOutHandshake, ALogoutClearsTheZoningLatch) {
    EverQuest eq = MakeClient();
    eq.Test_SetZoneToZoneInProgressForSeam(true);

    ReplayLogoutReply(eq);  // no hand-off in flight -> the real logout path

    EXPECT_FALSE(eq.Test_IsZoneToZoneInProgress())
        << "a logout must not leave the next world login claiming to be a zone change";
    EXPECT_TRUE(eq.IsQuitRequested());
    EXPECT_TRUE(eq.Test_IsPendingLogoutDisconnect());
}
