// T423 (board B362): the scene ambient recomposes on EVERY input that feeds
// it, not only on a clock packet.
//
// WHAT WAS WRONG. IrrlichtRenderer::updateTimeOfDay() is the only production
// site that calls composeSceneAmbient() and smgr_->setAmbientLight(), and its
// callers were the OP_TimeOfDay bridge arm, zone-in, and
// PlayerCameraComponent::refreshVisionLighting(). eqemu sends OP_TimeOfDay at
// zone-in and on `#set time` -- not on a timer. So between two clock packets
// the weather modifier and the zone-light gate were frozen at whatever they
// were when the last packet landed. T418 found this by accident: with the
// PRE-FIX 0.460 weather modifier live in the controller, a full day block of
// forced rain at intensities 2, 5 and 9 and snow produced geometry ratios of
// 0.998-1.014 and ZERO `updateTimeOfDay: weatherMod ...` lines in the client's
// own log (review/T418/logs/fable_session_before.log.gz). The unit's
// before/after pair had to force the recompute with a second `#set time` after
// every trigger to make the modifier visible at all.
//
// WHAT IS PINNED HERE. One dirty flag on IrrlichtRenderer, set by every writer
// of a non-clock ambient input and consumed once per frame by
// FrameComponent::recomposeSceneAmbientIfDirty() (whose position in the draw
// order is pinned separately, in tests/goldens/frame_render_order_g05.txt).
// The behavioural cases below drive the REAL components on a
// default-constructed, never-Init()'d IrrlichtRenderer -- the headless idiom
// of test_diagnostics_component.cpp; nothing here touches a GL context. The
// text cases pin the wiring at the sites a headless renderer cannot reach,
// because their bodies are guarded on a weatherEffects_ that only
// initDeferredGraphics() builds.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/diagnostics/diagnostics_component.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/cmake/renderer_guards.cmake)"
#endif

namespace fs = std::filesystem;

using namespace EQT::Graphics;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The body of `sig` up to the first column-0 closing brace.
std::string bodyOf(const std::string& src, const std::string& sig) {
    const std::size_t at = src.find(sig);
    EXPECT_NE(at, std::string::npos) << "no definition of " << sig;
    if (at == std::string::npos) return "";
    const std::size_t end = src.find("\n}\n", at);
    return src.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The flag itself.
// ---------------------------------------------------------------------------

// A renderer that has never composed an ambient must compose one on its first
// frame, whether or not a clock packet has arrived yet.
TEST(SceneAmbientRecomputeTest, AFreshRendererStartsDirty) {
    IrrlichtRenderer renderer;
    EXPECT_TRUE(renderer.isSceneAmbientDirty());
}

// updateTimeOfDay() IS the recompute, so it settles the mark -- otherwise a
// clock packet followed by a frame would recompose twice, and the frame pass
// would recompose every frame forever.
TEST(SceneAmbientRecomputeTest, TheRecomposeConsumesTheMark) {
    IrrlichtRenderer renderer;
    renderer.markSceneAmbientDirty();
    ASSERT_TRUE(renderer.isSceneAmbientDirty());
    renderer.updateTimeOfDay(12, 0);
    EXPECT_FALSE(renderer.isSceneAmbientDirty())
        << "the recompute did not consume the mark, so the frame pass would "
           "re-run it on every frame for ever";
    renderer.updateTimeOfDay(13, 0);
    EXPECT_FALSE(renderer.isSceneAmbientDirty());
}

// ---------------------------------------------------------------------------
// 2. Each input marks it -- with NO clock packet anywhere.
// ---------------------------------------------------------------------------

TEST(SceneAmbientRecomputeTest, AWeatherPacketMarksTheAmbientWithNoClockPacket) {
    IrrlichtRenderer renderer;
    renderer.updateTimeOfDay(12, 0);              // the last clock packet
    ASSERT_FALSE(renderer.isSceneAmbientDirty());

    renderer.ZoneLifecycle().setWeather(0, 9);    // the literal OP_Weather rain payload
    EXPECT_TRUE(renderer.isSceneAmbientDirty())
        << "server rain at intensity 9 did not ask for a recompose; this is "
           "exactly the state T418's first live session sat in for a whole "
           "day block";
}

TEST(SceneAmbientRecomputeTest, AVisionChangeMarksTheAmbientWithNoClockPacket) {
    IrrlichtRenderer renderer;

    // The racial base. Race 6 (Dark Elf) is the one race the oracle puts on
    // Ultravision (T361), so this moves the vision floor composeSceneAmbient()
    // applies last.
    renderer.updateTimeOfDay(12, 0);
    ASSERT_FALSE(renderer.isSceneAmbientDirty());
    renderer.PlayerCamera().setPlayerRace(6);
    EXPECT_TRUE(renderer.isSceneAmbientDirty());
    ASSERT_EQ(renderer.Context().currentVision, VisionType::Ultravision);

    // A buff landing above the base.
    renderer.updateTimeOfDay(12, 0);
    renderer.PlayerCamera().setPlayerRace(1);     // Human -> Normal
    renderer.updateTimeOfDay(12, 0);
    ASSERT_FALSE(renderer.isSceneAmbientDirty());
    renderer.PlayerCamera().setVisionType(VisionType::Ultravision);
    EXPECT_TRUE(renderer.isSceneAmbientDirty());

    // And the fade back to the racial floor.
    renderer.updateTimeOfDay(12, 0);
    ASSERT_FALSE(renderer.isSceneAmbientDirty());
    renderer.PlayerCamera().resetVisionToBase();
    EXPECT_TRUE(renderer.isSceneAmbientDirty());
}

// The zone-light gate is READ by updateTimeOfDay() (it early-returns while
// zone lights are off), so the toggle is an ambient input like any other.
TEST(SceneAmbientRecomputeTest, TheZoneLightToggleMarksTheAmbient) {
    IrrlichtRenderer renderer;
    renderer.updateTimeOfDay(12, 0);
    ASSERT_FALSE(renderer.isSceneAmbientDirty());
    ASSERT_FALSE(renderer.Diagnostics().zoneLightsEnabled());

    renderer.Diagnostics().toggleZoneLights();     // state 1 -> state 2
    EXPECT_TRUE(renderer.Diagnostics().zoneLightsEnabled());
    EXPECT_TRUE(renderer.isSceneAmbientDirty());
}

TEST(SceneAmbientRecomputeTest, TheDirectionalLightToggleMarksTheAmbient) {
    IrrlichtRenderer renderer;
    // Off then on: the restore arm is the one that has to recompose.
    renderer.Diagnostics().toggleDirectionalLight();
    renderer.updateTimeOfDay(12, 0);
    ASSERT_FALSE(renderer.isSceneAmbientDirty());
    renderer.Diagnostics().toggleDirectionalLight();
    EXPECT_TRUE(renderer.isSceneAmbientDirty());
}

// ---------------------------------------------------------------------------
// 3. ONE path, not two.
// ---------------------------------------------------------------------------
//
// T405's vision floors already forced the recompute themselves, through
// refreshVisionLighting() -> updateTimeOfDay(). That was a SECOND path into
// the recompute that only the vision input had; weather and the zone-light
// toggle had none, which is B362. There is one path now: every non-clock
// writer marks, and the frame pass runs the recompute.

TEST(SceneAmbientRecomputeTest, TheVisionPathMarksInsteadOfRecomposingInline) {
    const std::string body = bodyOf(
        readFile(kRoot / "src/client/graphics/player/renderer_camera.cpp"),
        "void PlayerCameraComponent::refreshVisionLighting()");
    EXPECT_NE(body.find("owner_.markSceneAmbientDirty();"), std::string::npos);
    EXPECT_EQ(body.find("owner_.updateTimeOfDay("), std::string::npos)
        << "the vision floor still has its own private path into the "
           "recompute, so the four inputs are not on one path after all";
}

TEST(SceneAmbientRecomputeTest, TheWeatherWriterMarksTheAmbient) {
    const std::string body = bodyOf(
        readFile(kRoot / "src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp"),
        "void ZoneLifecycleComponent::setWeather(");
    EXPECT_NE(body.find("owner_.markSceneAmbientDirty();"), std::string::npos);
}

// The modifier is not a pure function of the OP_Weather packet: a lightning
// flash lifts it and drops it again with no packet either way, and that lands
// on the render thread through the worker's applyState hop.
TEST(SceneAmbientRecomputeTest, TheWorkerWeatherApplyMarksOnAModifierMove) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/sim/renderer_sim_bridge.cpp");
    const std::size_t at = src.find("ctx_.weatherEffects->applyState(wes);");
    ASSERT_NE(at, std::string::npos);
    const std::string around = src.substr(at > 400 ? at - 400 : 0, 900);
    EXPECT_NE(around.find("owner_.markSceneAmbientDirty();"), std::string::npos)
        << "the worker's weather state lands here and nothing asks for a "
           "recompose, so a lightning flash never reaches the scene ambient";
    EXPECT_NE(around.find("getAmbientLightModifier() - modBefore"), std::string::npos)
        << "the mark must be taken on the modifier MOVING, not on every apply "
           "-- weather is enabled for whole zones and this runs every frame";
}

TEST(SceneAmbientRecomputeTest, ZoneEnterMarksTheAmbient) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/zone_load/renderer_zone_load_env.cpp");
    const std::size_t at = src.find("ctx_.weatherEffects->onZoneEnter(ctx_.currentZoneName);");
    ASSERT_NE(at, std::string::npos);
    EXPECT_NE(src.substr(at, 600).find("owner_.markSceneAmbientDirty();"), std::string::npos);
}

// The consumer. Its POSITION in the draw order is pinned by the G05 golden;
// what is pinned here is that it exists, that it is gated on the flag, and
// that it calls the one recompute.
TEST(SceneAmbientRecomputeTest, TheFrameConsumesTheMarkOncePerFrame) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/frame/renderer_frame_render.cpp");
    const std::string body =
        bodyOf(src, "void FrameComponent::recomposeSceneAmbientIfDirty()");
    EXPECT_NE(body.find("owner_.isSceneAmbientDirty()"), std::string::npos);
    EXPECT_NE(body.find("owner_.updateTimeOfDay(ctx_.currentHour, ctx_.currentMinute)"),
              std::string::npos);

    const std::string driver = bodyOf(src, "bool FrameComponent::processFrameRender(");
    const std::size_t pass = driver.find("recomposeSceneAmbientIfDirty();");
    const std::size_t begin = driver.find("beginSceneAndDrawWorld(deltaTime);");
    ASSERT_NE(pass, std::string::npos) << "the pass is never called";
    ASSERT_NE(begin, std::string::npos);
    EXPECT_LT(pass, begin)
        << "the recompose must precede beginScene(): the ambient it sets is "
           "read by the same frame's drawAll() and zone-shader uniforms";
}
