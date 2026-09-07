// T435 Leg B (BOARD B428): the OP_MoveDoor action byte means "open" or "close"
// only after invert_state is applied.
//
// eqemu, zone/doors.cpp:37-40:
//     #define OPEN_DOOR      0x02
//     #define CLOSE_DOOR     0x03
//     #define OPEN_INVDOOR   0x03
//     #define CLOSE_INVDOOR  0x02
// and every one of the nine emitters picks between the pairs the same way --
// `m_invert_state == 0 ? OPEN_DOOR : OPEN_INVDOOR` (doors.cpp:274, 314, 376,
// 402, 434, 474, 631, 663, 734).
//
// fable read `is_open = (action == 0x03)` with no inversion at all. Note what
// that actually means, because T433's board entry got the direction backwards:
// 0x03 is OPEN for an INVERTED door and CLOSE for a NORMAL one, so the old
// reading was the inverted-door mapping applied to every door in the game --
// the ~19,200 normal rows read BACKWARDS, and the invert_state=1 rows (the
// three Kelethin FAYLEVATORs among them) happened to read right.
//
// No network beyond the FakeUdpTransport seam, no graphics, no eqemu. Same
// harness idiom as test_door_click_zone_reset.cpp.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/entities/entities_component.h"
#include "common/net/daybreak_connection.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace zonetest;

namespace {

constexpr uint8_t kDoorId = 9;

// eqemu's four action bytes, by their own names.
constexpr uint8_t kOpenDoor = 0x02;
constexpr uint8_t kCloseDoor = 0x03;
constexpr uint8_t kOpenInvDoor = 0x03;
constexpr uint8_t kCloseInvDoor = 0x02;

class InvertHarness {
public:
    InvertHarness() : eq(MakeEq()) {
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
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        eq.m_update_running = true;
    }

    ~InvertHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    // `invert` is the wire field: gfaydark's three FAYLEVATOR lifts (doorids
    // 69 / 77 / 80) carry invert_state = 1; nearly everything else carries 0.
    void SpawnDoor(uint8_t door_id, bool invert) {
        EQT::DoorState d;
        d.doorId = door_id;
        d.name = invert ? "FAYLEVATOR" : "FAYDOOR";
        d.opentype = invert ? 59 : 0;
        d.invertState = invert;
        d.isOpen = false;
        eq.GetGameState().doors().addDoor(d);
    }

    void ReceiveMoveDoor(uint8_t door_id, uint8_t action) {
        std::vector<uint8_t> payload{door_id, action};
        ReplayZone(eq, HC_OP_MoveDoor, payload);
    }

    // The isOpen of the last DoorStateChanged event the game published. Fails
    // the calling test if none was published.
    bool LastIsOpen() {
        bool found = false;
        bool value = false;
        for (const auto& ev : bridge.drainEvents()) {
            if (ev.type == eqt::state::GameEventType::DoorStateChanged) {
                value = std::get<eqt::state::DoorStateChangedData>(ev.data).isOpen;
                found = true;
            }
        }
        EXPECT_TRUE(found) << "no DoorStateChanged event was published";
        return found && value;
    }

    // What the game state itself now holds, which is what the next click reads.
    bool StoredIsOpen() {
        const EQT::DoorState* d = eq.GetGameState().doors().getDoor(kDoorId);
        EXPECT_NE(d, nullptr);
        return d && d->isOpen;
    }

    EQ::Test::FakeChannel c2s, s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    TestBridge bridge;
    EverQuest eq;
};

// RED pre-T435: `is_open = (action == 0x03)` made OPEN_DOOR (0x02) read as
// closed, so an ordinary door reported the wrong state on every single packet.
TEST(DoorInvertState, NormalDoorOpensOn0x02) {
    InvertHarness h;
    h.SpawnDoor(kDoorId, /*invert=*/false);
    h.ReceiveMoveDoor(kDoorId, kOpenDoor);
    EXPECT_TRUE(h.LastIsOpen())
        << "eqemu sends OPEN_DOOR 0x02 for a door with invert_state = 0";
    EXPECT_TRUE(h.StoredIsOpen());
}

// RED pre-T435 for the same reason, the other way round.
TEST(DoorInvertState, NormalDoorClosesOn0x03) {
    InvertHarness h;
    h.SpawnDoor(kDoorId, /*invert=*/false);
    h.ReceiveMoveDoor(kDoorId, kOpenDoor);
    h.bridge.drainEvents();
    h.ReceiveMoveDoor(kDoorId, kCloseDoor);
    EXPECT_FALSE(h.LastIsOpen())
        << "eqemu sends CLOSE_DOOR 0x03 for a door with invert_state = 0";
    EXPECT_FALSE(h.StoredIsOpen());
}

// The FAYLEVATOR shape. This pair was green before the fix -- by accident, and
// it must stay green after it.
TEST(DoorInvertState, InvertedDoorOpensOn0x03) {
    InvertHarness h;
    h.SpawnDoor(kDoorId, /*invert=*/true);
    h.ReceiveMoveDoor(kDoorId, kOpenInvDoor);
    EXPECT_TRUE(h.LastIsOpen())
        << "eqemu sends OPEN_INVDOOR 0x03 for gfaydark's FAYLEVATOR lifts "
           "(invert_state = 1)";
    EXPECT_TRUE(h.StoredIsOpen());
}

TEST(DoorInvertState, InvertedDoorClosesOn0x02) {
    InvertHarness h;
    h.SpawnDoor(kDoorId, /*invert=*/true);
    h.ReceiveMoveDoor(kDoorId, kOpenInvDoor);
    h.bridge.drainEvents();
    h.ReceiveMoveDoor(kDoorId, kCloseInvDoor);
    EXPECT_FALSE(h.LastIsOpen())
        << "eqemu sends CLOSE_INVDOOR 0x02 for an invert_state = 1 door";
    EXPECT_FALSE(h.StoredIsOpen());
}

// The two doors read the SAME byte in opposite directions. This is the whole
// content of B428, in one assertion.
TEST(DoorInvertState, TheSameByteMeansOppositeThingsToTheTwoDoorKinds) {
    InvertHarness normal;
    normal.SpawnDoor(kDoorId, /*invert=*/false);
    normal.ReceiveMoveDoor(kDoorId, 0x03);

    InvertHarness inverted;
    inverted.SpawnDoor(kDoorId, /*invert=*/true);
    inverted.ReceiveMoveDoor(kDoorId, 0x03);

    EXPECT_FALSE(normal.LastIsOpen()) << "0x03 is CLOSE_DOOR for a normal door";
    EXPECT_TRUE(inverted.LastIsOpen()) << "0x03 is OPEN_INVDOOR for an inverted one";
}

}  // namespace
