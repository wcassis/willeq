// K09 (Batch K, refactor/K09_zone_component.md) -- eqt::zone::ZoneComponent.
//
// Same shape as K01/K08/K10: no forwarders. EverQuest no longer declares
// DispatchZonePacket, any ZoneSend*, the zone-lifecycle methods or the zone
// handshake flags; every caller goes through EverQuest::Zone(). This file
// covers the component itself -- construction against a real EverQuest, the
// accessor identity, the state that moved into it (the handshake/zone-out
// flags, the GM-mode and zoning-enabled observers, the T231/T322 test seams),
// and the transition getters the entities/movement/player/login_world
// components now read through.
//
// test_zone_connection_flow_*.cpp, test_zone_out_handshake.cpp and
// test_zone_position_convention.cpp keep their own assertions untouched --
// only their call sites were retargeted, and THEY remain the coverage of what
// the moved bodies compute. Nothing here re-derives a wire format.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/zone/zone_component.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class ZoneComponentTest : public ::testing::Test {
protected:
    ZoneComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~ZoneComponentTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// The accessor is the only way in now, and it must name one stable object.
TEST_F(ZoneComponentTest, ZoneReturnsTheSameComponentEveryTime) {
    eqt::zone::ZoneComponent& a = eq.Zone();
    eqt::zone::ZoneComponent& b = eq.Zone();
    EXPECT_EQ(&a, &b);
}

// The inline eq.h accessors that read a moving member moved with it (K04
// correction f). A fresh client has none of the zone handshake done.
TEST_F(ZoneComponentTest, FreshClientHasNoZoneHandshakeState) {
    EXPECT_FALSE(eq.Zone().ZoneEntrySent());
    EXPECT_FALSE(eq.Zone().WeatherReceived());
    EXPECT_FALSE(eq.Zone().ReqNewZoneSent());
    EXPECT_FALSE(eq.Zone().ServerFilterSent());
    EXPECT_FALSE(eq.Zone().ExpZoneinSent());
    EXPECT_FALSE(eq.Zone().SendExpZoneinReceived());
    EXPECT_FALSE(eq.Zone().PlayerGraphicsEntityPending());
}

// T148: GM mode is populated once, from PlayerProfile.gm, by
// ZoneProcessPlayerProfile. It starts false and its accessor moved with it.
TEST_F(ZoneComponentTest, GmModeStartsFalseAndItsAccessorMoved) {
    EXPECT_FALSE(eq.Zone().GetGMMode());
}

// The zoning-enabled toggle (Ctrl+Z / the /zoning command) moved whole:
// member, setter and getter. B1068 (#388, Will 2026-10-15): zoning is ON by
// default -- the official client zones at every line.
TEST_F(ZoneComponentTest, ZoningEnabledRoundTripsThroughTheComponent) {
    EXPECT_TRUE(eq.Zone().IsZoningEnabled()) << "zoning is on by default (B1068)";
    eq.Zone().SetZoningEnabled(false);
    EXPECT_FALSE(eq.Zone().IsZoningEnabled());
    eq.Zone().SetZoningEnabled(true);
    EXPECT_TRUE(eq.Zone().IsZoningEnabled());
}

// T322: the zone-out handshake flags and their test seams moved together.
// test_zone_out_handshake.cpp drives the real hand-off; this only pins that
// the seams reach the component's own storage.
TEST_F(ZoneComponentTest, ZoneOutSeamsReadTheComponentsOwnFlags) {
    EXPECT_FALSE(eq.Zone().Test_IsZoneOutHandshakePending());
    EXPECT_FALSE(eq.Zone().Test_IsZoneOutAcknowledged());
    EXPECT_FALSE(eq.Zone().Test_IsZoneToZoneInProgress());

    eq.Zone().Test_SetZoneToZoneInProgressForSeam(true);
    EXPECT_TRUE(eq.Zone().Test_IsZoneToZoneInProgress());
    EXPECT_TRUE(eq.Zone().ZoneToZoneInProgress());

    eq.Zone().ZoneOutAcknowledged() = true;
    EXPECT_TRUE(eq.Zone().Test_IsZoneOutAcknowledged());
}

// T231: the zone-change request counter and its seam moved with the struct
// they are built on.
TEST_F(ZoneComponentTest, ZoneChangeRequestSeamMovedWithItsStruct) {
    eqt::zone::ZoneComponent::TestZoneChangeRequest req =
        eq.Zone().Test_GetLastZoneChangeRequest();
    EXPECT_EQ(req.attempts, 0);
    EXPECT_EQ(req.zone_id, 0);
}

// The transition getters the other components read through are references
// into the component, not copies -- a write through one is visible to the
// next read, which is what makes the retargeted call sites behave exactly as
// the old owner_.m_* did.
TEST_F(ZoneComponentTest, TransitionGettersAreReferencesIntoTheComponent) {
    eq.Zone().Exp() = 4242;
    EXPECT_EQ(eq.Zone().Exp(), 4242u);
    eq.Zone().AATableCount() = 7;
    EXPECT_EQ(eq.Zone().AATableCount(), 7);
    eq.Zone().PendingScribeSpellId() = 1234;
    EXPECT_EQ(eq.Zone().PendingScribeSpellId(), 1234u);
    EXPECT_TRUE(eq.Zone().MovementHistory().empty());
    EXPECT_TRUE(eq.Zone().NavmeshPath().empty());
    EXPECT_EQ(eq.Zone().ZoneLinesPtr(), nullptr);
}

// The path setters moved off EverQuest; Application reaches them through
// Zone(). They write the component's own storage.
TEST_F(ZoneComponentTest, PathSettersMovedWithTheirMembers) {
    eq.Zone().SetNavmeshPath("/tmp/k09-navmesh");
    EXPECT_EQ(eq.Zone().NavmeshPath(), "/tmp/k09-navmesh");
}

// EverQuest keeps the replay-suppressed-send counter (it is written by the
// login/world senders too) and the deferred-logout seam. Those did NOT move;
// this pins the K09 boundary so a later unit does not quietly move them
// without re-reading the Review.
TEST_F(ZoneComponentTest, TheReplayCounterAndLogoutSeamStayedOnEverQuest) {
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 0u);
    EXPECT_FALSE(eq.Test_IsPendingLogoutDisconnect());
}

}  // namespace
