// T334 (refactor/T334_camera_audio_and_vision.md, board B046): the
// VisionChanged bridge arm.
//
// The board entry said the buff-fade hook did not exist. It does:
// EverQuest::InitializeSpellSystem() installs a BuffManager fade callback
// (everquest.cpp) that, on a UltraVision/InfraVision buff expiring,
// RECOMPUTES the vision from the buffs still up and publishes the result as a
// VisionChanged event. The gap was at the far end -- the arm called
// IrrlichtRenderer::setVisionType(), which only ever UPGRADED, so the
// recomputed value could raise vision and never lower it. The fade publish
// was a no-op and an Ultravision buff lasted the whole session.
//
// VisionChanged carries an ABSOLUTE recomputed vision, not a delta, so the
// arm must apply it as one. Racial vision is the floor, and it comes from
// baseVision_ (setPlayerRace), never from this event -- a fade to "no vision
// buffs left" publishes 0 and must land on the race's base, not on Normal.
//
// Same bare, never-Init()'d IrrlichtRenderer idiom as
// test_navmesh_overlay_wire.cpp / test_player_camera_component.cpp: nothing
// on this path touches an Irrlicht device.

#include <gtest/gtest.h>

#include "client/bridge/irrlicht_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/renderer_types.h"
#include "client/state/event_bus.h"

using namespace EQT::Graphics;

namespace {

// The publisher's encoding: 0 = normal, 1 = infravision, 2 = ultravision.
eqt::state::GameEvent visionChanged(uint8_t visionType) {
    eqt::state::VisionChangedData d;
    d.visionType = visionType;
    return eqt::state::GameEvent(eqt::state::GameEventType::VisionChanged, d);
}

TEST(VisionBuffFadeWireTest, AFadeToNoVisionBuffsReturnsAHumanToNormal) {
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);

    renderer.PlayerCamera().setPlayerRace(1);  // Human -- base Normal
    ASSERT_EQ(renderer.getVisionType(), VisionType::Normal);

    // A buff lands: the callback recomputes and publishes Ultravision.
    bridge.applyEvent(visionChanged(2));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // It fades with nothing else up: the callback publishes 0. Before T334
    // this stayed at Ultravision for the rest of the session.
    bridge.applyEvent(visionChanged(0));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Normal);
}

TEST(VisionBuffFadeWireTest, AFadeNeverDropsBelowTheRacialBase) {
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);

    renderer.PlayerCamera().setPlayerRace(8);  // Dwarf -- base Infravision
    ASSERT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::Infravision);
    ASSERT_EQ(renderer.getVisionType(), VisionType::Infravision);

    bridge.applyEvent(visionChanged(2));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // No vision buffs left. The Dwarf keeps his own infravision -- the event's
    // 0 is "no BUFF vision", not "no vision".
    bridge.applyEvent(visionChanged(0));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Infravision);
}

TEST(VisionBuffFadeWireTest, OneOfTwoStackedBuffsFadingLeavesTheOther) {
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);

    renderer.PlayerCamera().setPlayerRace(1);  // Human

    // Ultravision + Infravision up; the callback's loop reports the best.
    bridge.applyEvent(visionChanged(2));
    ASSERT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // The Ultravision one fades; the Infravision one is still up, so the
    // recomputed publish is 1 -- a genuine DOWNGRADE that must take effect.
    bridge.applyEvent(visionChanged(1));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Infravision);
}

}  // namespace
