// B448 (GitLab #29): a race change on the player's OWN character reaches the
// vision system.
//
// THE DEFECT. PlayerCameraComponent::setPlayerRace() -- the one writer of the
// racial vision floor (baseVision_) -- was called only from
// IrrlichtRenderer::registerEntity(), i.e. when the player entity is
// REGISTERED at zone-in (T348, B056). A race change on a character already in
// the world -- an illusion spell, or a GM `#race N` -- arrives as OP_Illusion,
// which EntitiesComponent::ZoneProcessIllusion() decodes and publishes as
// EntityAppearanceChanged (isPlayer = the spawn is MySpawnId). The bridge arm
// forwarded that to IrrlichtRenderer::updatePlayerAppearance(), whose body was
// a LOG_DEBUG and nothing else. MEASURED T428 twice: `#race 330` confirmed in
// chat, no "Player race 330 -> base vision:" line, the vision ratio across
// the window 0.95-0.99 (drift only).
//
// WHAT IS PINNED HERE
//   Part 1: the whole game-thread -> render-thread path. A real EverQuest
//           decodes a real OP_Illusion for its own spawn id; the events it
//           publishes are applied to a real IrrlichtBridge over a bare,
//           never-Init()'d IrrlichtRenderer (the test_vision_buff_fade_wire
//           idiom: nothing on this path touches an Irrlicht device).
//   Part 2: the composition with buff vision. A race change moves the FLOOR;
//           it must not wipe a vision buff that is up, and an appearance
//           event that does not change the race (OP_WearChange, inventory)
//           must not touch vision at all.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "client/bridge/irrlicht_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/renderer_types.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;
using EQT::Graphics::IrrlichtRenderer;
using EQT::Graphics::VisionType;

namespace {

constexpr uint16_t kMySpawn = 77;
constexpr uint16_t kPeerSpawn = 42;
constexpr uint32_t kHuman = 1;
constexpr uint32_t kDarkElf = 6;
constexpr uint32_t kFroglok = 330;

std::vector<uint8_t> IllusionPayload(uint32_t spawnId, int32_t race) {
    return BuildPayload(2 + sizeof(EQT::Illusion_Struct), [&](EQ::Net::DynamicPacket& p) {
        EQT::Illusion_Struct body{};
        std::memset(&body, 0, sizeof(body));
        body.spawnid = spawnId;
        body.race = race;
        body.gender = 0;
        body.size = 6.0f;
        std::memcpy(static_cast<uint8_t*>(p.Data()) + 2, &body, sizeof(body));
    });
}

eqt::state::GameEvent visionChanged(uint8_t visionType) {
    eqt::state::VisionChangedData d;
    d.visionType = visionType;
    return eqt::state::GameEvent(eqt::state::GameEventType::VisionChanged, d);
}

// An appearance event that does NOT change the race (what OP_WearChange and
// UpdatePlayerAppearanceFromInventory publish for the player).
eqt::state::GameEvent selfAppearance(uint16_t race) {
    eqt::state::EntityAppearanceChangedData d;
    d.spawnId = kMySpawn;
    d.raceId = race;
    d.isPlayer = true;
    return eqt::state::GameEvent(eqt::state::GameEventType::EntityAppearanceChanged, d);
}

// Game thread (EverQuest) on one side, render thread (IrrlichtBridge +
// IrrlichtRenderer) on the other; Pump() is the frame boundary.
class SelfRaceVisionTest : public ::testing::Test {
protected:
    SelfRaceVisionTest() : eq(MakeEq()) {
        eq.setBridge(&publisher);
        eq.Test_SetMySpawnIdForSeam(kMySpawn);
        consumer.setRenderer(&renderer);
    }
    ~SelfRaceVisionTest() override { eq.setBridge(nullptr); }

    // Zone-in as `race`: the self spawn on the wire, and the racial floor
    // registerEntity() sets (the path T348 pinned; it needs an Irrlicht
    // device to run for real, so it is stood in for here).
    void ZoneInAs(uint32_t race) {
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(kMySpawn, "Testchar", 10, 20, 3, 50,
                                     /*npc_type=*/0, 1, 100, race));
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(kPeerSpawn, "Peer", 30, 40, 3, 50,
                                     /*npc_type=*/0, 1, 100, kHuman));
        publisher.drainEvents();
        renderer.PlayerCamera().setPlayerRace(static_cast<uint16_t>(race));
    }

    void Pump() {
        for (const auto& ev : publisher.drainEvents()) consumer.applyEvent(ev);
    }

    EverQuest eq;
    TestBridge publisher;
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge consumer;
};

// =========================================================================
// Part 1 -- the wire to the vision floor.
// =========================================================================

TEST_F(SelfRaceVisionTest, RaceThreeThirtyOnSelfMovesTheBaseToFroglokVision) {
    ZoneInAs(kHuman);
    ASSERT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::Normal);

    // `#race 330`: eqemu Mob::SendIllusionPacket -> OP_Illusion, self included.
    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(kMySpawn, kFroglok));
    Pump();

    EXPECT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::FroglokVision)
        << "B448: the race change on self never reached setPlayerRace()";
    EXPECT_EQ(renderer.getVisionType(), VisionType::FroglokVision);
}

TEST_F(SelfRaceVisionTest, RaceChangeBackRestoresTheOriginalFloor) {
    ZoneInAs(kHuman);
    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(kMySpawn, kFroglok));
    Pump();
    ASSERT_EQ(renderer.getVisionType(), VisionType::FroglokVision);

    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(kMySpawn, kHuman));
    Pump();
    EXPECT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::Normal);
    EXPECT_EQ(renderer.getVisionType(), VisionType::Normal)
        << "a Human again sees as a Human";
}

TEST_F(SelfRaceVisionTest, ADarkElfIllusionedToHumanLosesUltravision) {
    ZoneInAs(kDarkElf);
    ASSERT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(kMySpawn, kHuman));
    Pump();
    EXPECT_EQ(renderer.getVisionType(), VisionType::Normal);
}

TEST_F(SelfRaceVisionTest, APeersIllusionDoesNotMoveMyVision) {
    ZoneInAs(kHuman);
    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(kPeerSpawn, kDarkElf));
    Pump();
    EXPECT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::Normal);
    EXPECT_EQ(renderer.getVisionType(), VisionType::Normal);
}

// =========================================================================
// Part 2 -- the floor composes with buff vision.
// =========================================================================

TEST_F(SelfRaceVisionTest, ARaceChangeUnderAVisionBuffKeepsTheBuff) {
    ZoneInAs(kHuman);
    consumer.applyEvent(visionChanged(2));  // Ultravision buff lands
    ASSERT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // Illusion to Froglok while the buff is up: the floor moves, the buff
    // (ranked above FroglokVision) still wins.
    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(kMySpawn, kFroglok));
    Pump();
    EXPECT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::FroglokVision);
    EXPECT_EQ(renderer.getVisionType(), VisionType::Ultravision)
        << "a race change must not wipe a vision buff that is still up";

    // The buff fades: the NEW floor, not the zone-in race's.
    consumer.applyEvent(visionChanged(0));
    EXPECT_EQ(renderer.getVisionType(), VisionType::FroglokVision);
}

TEST_F(SelfRaceVisionTest, ARaceChangeAboveTheBuffRaisesVision) {
    ZoneInAs(kHuman);
    consumer.applyEvent(visionChanged(1));  // Infravision buff
    ASSERT_EQ(renderer.getVisionType(), VisionType::Infravision);

    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(kMySpawn, kDarkElf));
    Pump();
    EXPECT_EQ(renderer.getVisionType(), VisionType::Ultravision);
}

TEST_F(SelfRaceVisionTest, AnAppearanceEventWithTheSameRaceLeavesABuffAlone) {
    ZoneInAs(kHuman);
    consumer.applyEvent(visionChanged(2));
    ASSERT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // OP_WearChange / inventory re-publish the player's appearance with the
    // race it already has. That is not a race change.
    consumer.applyEvent(selfAppearance(kHuman));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Ultravision);
    EXPECT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::Normal);
}

}  // namespace
