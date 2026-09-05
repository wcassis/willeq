// T374 Leg A (BOARD B136): pending door clicks must not survive a zone
// change.
//
// eqt::entities::EntitiesComponent::m_pending_door_clicks is the set of door
// ids the USER clicked, awaiting the server's OP_MoveDoor confirmation. Door
// ids are uint8_t and every zone reissues them from its own door list, so a
// click sent moments before zoning left a stale entry behind: the NEXT zone's
// first OP_MoveDoor carrying the same numeric id was reported as
// user-initiated. That is wrong twice over -- the DoorStateChanged event the
// renderer consumes carries userInitiated=true for a door the player never
// touched, and the debug log claims a click that did not happen.
//
// The fix clears the set on the one zone-cleanup path (ZoneComponent::
// CleanupZone, beside the door-state-manager clear). This file is the pin:
// click a door, zone, then take an OP_MoveDoor for the SAME id in the new
// zone and assert userInitiated == false. The companion case proves the
// mechanism still works WITHIN a zone, so the fix cannot be "never report a
// user click again".
//
// No network, no graphics, no eqemu. SendClickDoor is gated on
// IsFullyZonedIn(), whose third term (EverQuest::m_update_running) has no
// public setter, and it queues straight onto m_zone_connection (it is one of
// the senders that still bypasses PacketSender -- boarded as B140), so a
// harness without a connection segfaults in the send rather than reaching the
// code under test. This file therefore uses the same seam as
// test_forced_pose_no_echo.cpp / test_self_death_sequence.cpp: a real
// DaybreakConnection over FakeUdpTransport installed as m_zone_connection via
// the T17 `#define private public` idiom.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/entities/entities_component.h"
#include "client/zone/zone_component.h"
#include "common/net/daybreak_connection.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace zonetest;

namespace {

constexpr uint8_t kDoorId = 3;

class DoorHarness {
public:
    DoorHarness() : eq(MakeEq()) {
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        manager = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
        manager->OnNewConnection(
            [this](std::shared_ptr<EQ::Net::DaybreakConnection> c) { conn = c; });
        manager->Connect("peer", 12345);
        EXPECT_NE(conn, nullptr);

        eq.setBridge(&bridge);
        eq.m_zone_connection = conn;  // private: T17 idiom
        SetZonedIn();
    }

    ~DoorHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    // All three terms of IsFullyZonedIn(); SendClickDoor returns early
    // without them and would never record the pending click at all.
    void SetZonedIn() {
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        eq.m_update_running = true;
    }

    // A door with this id exists in the zone the client is standing in.
    void SpawnDoor(uint8_t door_id) {
        EQT::DoorState d;
        d.doorId = door_id;
        d.name = "DOOR_T374";
        eq.GetGameState().doors().addDoor(d);
    }

    // The server's OP_MoveDoor: opcode(2) + doorid(1) + action(1).
    // action 0x03 == open.
    void ReceiveMoveDoor(uint8_t door_id, uint8_t action = 0x03) {
        std::vector<uint8_t> payload{door_id, action};
        ReplayZone(eq, HC_OP_MoveDoor, payload);
    }

    // The userInitiated flag of the last DoorStateChanged event pushed.
    // Fails the calling test if none was pushed.
    bool LastUserInitiated() {
        bool found = false;
        bool value = false;
        for (const auto& ev : bridge.drainEvents()) {
            if (ev.type == eqt::state::GameEventType::DoorStateChanged) {
                value = std::get<eqt::state::DoorStateChangedData>(ev.data).userInitiated;
                found = true;
            }
        }
        EXPECT_TRUE(found) << "no DoorStateChanged event was published";
        return found && value;
    }

    EQ::Test::FakeChannel c2s, s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    TestBridge bridge;
    EverQuest eq;
};

// The preservation pin: within one zone, a click the user made is still
// reported as user-initiated.
TEST(DoorClickZoneReset, ClickThenMoveDoorInTheSameZoneIsUserInitiated) {
    DoorHarness h;
    h.SpawnDoor(kDoorId);
    h.eq.Entities().SendClickDoor(kDoorId, 0);
    h.ReceiveMoveDoor(kDoorId);
    EXPECT_TRUE(h.LastUserInitiated());
}

// RED pre-T374: the stale click survived CleanupZone() and flagged the new
// zone's door 3 as user-initiated.
TEST(DoorClickZoneReset, PendingClickDoesNotSurviveAZoneChange) {
    DoorHarness h;
    h.SpawnDoor(kDoorId);
    h.eq.Entities().SendClickDoor(kDoorId, 0);
    h.bridge.drainEvents();  // discard anything the click itself produced

    // Zone out: the same cleanup that clears the entity map and the doors.
    h.eq.Zone().CleanupZone();

    // Zone in somewhere else. Door ids are per-zone; this door 3 is a
    // different door the player has never touched.
    h.SetZonedIn();
    h.SpawnDoor(kDoorId);
    h.ReceiveMoveDoor(kDoorId);

    EXPECT_FALSE(h.LastUserInitiated())
        << "a door click made in the PREVIOUS zone was carried across "
           "CleanupZone() and credited to the new zone's door "
        << static_cast<int>(kDoorId);
}

}  // namespace
