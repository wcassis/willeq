// B911 / B912 (GitLab #230 / #231): a pose change the GAME THREAD makes must
// reach the renderer, through the bridge, and show in what the renderer draws
// with (and logs).
//
// THE DEFECTS, measured by the first live pose capture
// (refactor/B874/live/, run-20260929T022314Z):
//   B911 -- `/turn 90`: the server's `#loc` read heading 0 one second later,
//           while every rendered row still had heading0_512 28 (server 100).
//           EqActionHandler::setHeading() wrote the one store and the wire, and
//           the store's own PlayerMoved goes to the STATE layer's EventBus,
//           which never reaches the renderer.
//   B912 -- `/move`: the player walked 10.47 units and every row said
//           movestate 0. The integrator's PlayerMoved carried isMoving=true;
//           IrrlichtBridge dropped it, and StopMovement() published nothing at
//           all when the walk ended.
//
// Seam: a bare EverQuest (zone_stream_test_utils.h) whose renderer bridge is a
// REAL IrrlichtBridge wired to a REAL IrrlichtRenderer on the headless GLES2
// device (the test_pose_log_e2e.cpp fixture). The commands enter where a typed
// or --script command does -- CommandProcessor -> ActionDispatcher ->
// EqActionHandler -- and the per-tick movement is the production
// MovementComponent::UpdateMovement(). Events cross by drainEvents() + each
// through IrrlichtBridge::applyEvent(), the renderer's half of the two-stage drain.
// The renderer side is read the way the pose log reads it:
// IrrlichtRenderer::getPoseData(), after a real processFrame().
//
// The keyboard case is here too, as the CONTROL: movement driven by the
// renderer's own input poll reports movestate 1 without any of this, before
// and after the fix (issue #231 asked for that to be measured, not assumed).

#include <gtest/gtest.h>

#include <irrlicht.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/bridge/irrlicht_bridge.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/pose_log_format.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/input/hotkey_manager.h"
#include "client/movement/movement_component.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

using namespace EQT::Graphics;

namespace {

constexpr uint16_t kMySpawn = 911;

// The live run's seed, CLIENT frame: server (x -74, y 428, z 3, heading 100).
// Internal heading = 90 - 100 * 360/512 = 19.6875 deg = 28 in the 512 unit.
constexpr float kX = 428.0f;
constexpr float kY = -74.0f;
constexpr float kZ = 3.0f;
constexpr float kSeedHeadingDeg = 19.6875f;
constexpr float kSeedHeading512 = 28.0f;

// (128 - heading0_512) mod 512: the documented client->server heading
// transform (pose_log_format.h, docs/CLAUDE.md).
float ToServerHeading512(float heading0_512) {
    float h = std::fmod(128.0f - heading0_512, 512.0f);
    if (h < 0.0f) h += 512.0f;
    return h;
}

float HeadingDelta512(float a, float b) {
    float d = std::fmod(a - b, 512.0f);
    if (d > 256.0f) d -= 512.0f;
    if (d < -256.0f) d += 512.0f;
    return d;
}

class CommandMovementRenderTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(db_.open(WILLEQ_DB_REAL_PATH)) << WILLEQ_DB_REAL_PATH;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        RendererConfig cfg;
        cfg.width = 320;
        cfg.height = 240;
        cfg.windowTitle = "command-movement-render";
        cfg.constrainedPreset = ConstrainedRenderingPreset::Custom;
        cfg.constrainedConfig = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
        cfg.constrainedConfig.useDRM = false;
        cfg.constrainedConfig.eqe1Path.clear();
        cache_ = std::make_unique<ConstrainedTextureCache>(cfg.constrainedConfig);
        renderer_ = std::make_unique<IrrlichtRenderer>();
        renderer_->setConstrainedTextureCache(cache_.get());
        renderer_->setBridge(&bridge_);
        ASSERT_TRUE(renderer_->initLoadingScreen(cfg))
            << "the headless GLES2 device (EGL surfaceless / llvmpipe) could not be "
               "created -- this test must run, not skip";
        renderer_->hideLoadingScreen();
        bridge_.setRenderer(renderer_.get());

        // The game side: our own spawn at the seed pose, zone "connected" so
        // the dispatcher's gate passes, sends captured rather than refused.
        // zonetest::MakeEq()'s arguments, on the heap (the fixture outlives
        // one expression).
        eq_ = std::make_unique<EverQuest>("127.0.0.1", 0, "test", "test", "test", "Testchar");
        eq_->setBridge(&bridge_);
        eq_->Net().Test_SetBypassSendGuardForSeam(true);
        eq_->Test_SetMySpawnIdForSeam(kMySpawn);
        eq_->Test_SetZoneConnectedForSeam(true);
        eq_->SetPosition(kX, kY, kZ);
        eq_->SetHeading(kSeedHeadingDeg);

        dispatcher_ = std::make_unique<eqt::action::ActionDispatcher>(eq_->GetGameState());
        handler_ = std::make_unique<eqt::EqActionHandler>(*eq_);
        dispatcher_->setActionHandler(handler_.get());
        processor_ = std::make_unique<eqt::action::CommandProcessor>(eq_->GetGameState(), *dispatcher_);

        // The renderer is handed the same seed, as the zone-in snapshot does.
        renderer_->setPlayerPosition(kX, kY, kZ, kSeedHeading512);
        bridge_.drainEvents();  // anything the seeding published
    }

    void TearDown() override {
        processor_.reset();
        dispatcher_.reset();
        handler_.reset();
        if (eq_) eq_->setBridge(nullptr);
        eq_.reset();
        if (renderer_) {
            renderer_->shutdown();
            renderer_.reset();
        }
        cache_.reset();
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // One game tick's worth of hand-over, then one rendered frame, then the
    // row the pose log would write for it.
    PoseLogRow FrameAfterDrain() {
        // GameStateSyncBridge::forwardStagedEvents()'s shape: drain (D24
        // coalescing included), then applyEvent() each, in order.
        for (const auto& e : bridge_.drainEvents()) bridge_.applyEvent(e);
        EXPECT_TRUE(renderer_->processFrame(1.0f / 60.0f));
        PoseLogRow row;
        renderer_->getPoseData(row);
        return row;
    }

    EQT::WilleqDatabase db_;
    eqt::bridge::IrrlichtBridge bridge_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<IrrlichtRenderer> renderer_;
    std::unique_ptr<EverQuest> eq_;
    std::unique_ptr<eqt::action::ActionDispatcher> dispatcher_;
    std::unique_ptr<eqt::EqActionHandler> handler_;
    std::unique_ptr<eqt::action::CommandProcessor> processor_;
};

// Sanity: the seed reaches the rendered pose, idle.
TEST_F(CommandMovementRenderTest, TheSeedIsTheRenderedPose) {
    const PoseLogRow r = FrameAfterDrain();
    EXPECT_NEAR(r.playerX, kX, 1e-3);
    EXPECT_NEAR(r.playerY, kY, 1e-3);
    EXPECT_NEAR(r.heading0_512, kSeedHeading512, 1e-3);
    EXPECT_EQ(r.movementState, 0);
}

// B911, PRE-FIX RED: the rendered heading stayed 28 after `/turn 90`.
TEST_F(CommandMovementRenderTest, TurnCommandReachesTheRenderedHeading) {
    ASSERT_TRUE(processor_->processCommand("/turn 90").success);

    const PoseLogRow r = FrameAfterDrain();
    const float game512 = eq_->GetHeading() * 512.0f / 360.0f;
    ASSERT_NEAR(game512, 128.0f, 1e-3) << "/turn 90 no longer stores 90 internal degrees";
    EXPECT_NEAR(r.heading0_512, game512, 1e-3)
        << "the renderer's player heading is not the game's after /turn (B911): "
        << "rendered " << r.heading0_512 << ", game " << game512;

    // The same heading, taken through the documented client->server transform,
    // is what the production encoder put on the wire (12-bit, /4 = 0..512).
    const auto sent = eq_->Movement().Test_GetLastComputedPositionUpdate();
    const float wire512 = sent.heading_scaled / 4.0f;
    EXPECT_LE(std::fabs(HeadingDelta512(ToServerHeading512(r.heading0_512), wire512)), 1.0f)
        << "rendered heading " << r.heading0_512 << " -> server "
        << ToServerHeading512(r.heading0_512) << ", but the wire carried " << wire512;

    // A turn is not a walk, and the position did not move.
    EXPECT_EQ(r.movementState, 0);
    EXPECT_NEAR(r.playerX, kX, 1e-3);
    EXPECT_NEAR(r.playerY, kY, 1e-3);
}

// /face goes through the same EqActionHandler::setHeading(); same net.
TEST_F(CommandMovementRenderTest, FaceLocationCommandReachesTheRenderedHeading) {
    // Due -y of the player in client space.
    ASSERT_TRUE(processor_->processCommand("/face 428 -100 3").success);
    const PoseLogRow r = FrameAfterDrain();
    const float game512 = eq_->GetHeading() * 512.0f / 360.0f;
    EXPECT_GT(std::fabs(HeadingDelta512(game512, kSeedHeading512)), 1.0f) << "/face did not turn";
    EXPECT_NEAR(r.heading0_512, game512, 1e-3);
}

// B912, PRE-FIX RED twice over: every row of a `/move` walk said movestate 0
// (the bridge dropped isMoving), and after the walk the renderer never heard
// that it had ended, nor where (the arrival snap was never published).
TEST_F(CommandMovementRenderTest, MoveCommandIsNonIdleWhileMovingAndIdleAfter) {
    constexpr float kToX = kX + 10.0f;  // 10 units along client +x
    ASSERT_TRUE(processor_->processCommand("/move 438 -74 3").success);
    ASSERT_TRUE(eq_->Movement().HasCommandMovement());

    std::vector<PoseLogRow> walking;
    PoseLogRow last{};
    for (int i = 0; i < 400 && eq_->Movement().HasCommandMovement(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        eq_->Movement().UpdateMovement();
        last = FrameAfterDrain();
        if (eq_->Movement().HasCommandMovement()) walking.push_back(last);
    }
    ASSERT_FALSE(eq_->Movement().HasCommandMovement()) << "the walk never finished";
    ASSERT_GE(walking.size(), 3u) << "the walk took fewer than three ticks";

    for (size_t i = 0; i < walking.size(); ++i) {
        SCOPED_TRACE("walking row " + std::to_string(i));
        EXPECT_EQ(walking[i].movementState, 1)
            << "the game thread is walking the player and the renderer says idle (B912)";
    }
    EXPECT_GT(walking.back().playerX, kX + 0.5f) << "the rendered position did not move";

    // After the stop: idle, and exactly where the game says the walk ended.
    const glm::vec3 game = eq_->GetPosition();
    EXPECT_EQ(last.movementState, 0) << "the walk ended and the renderer still says moving (B912)";
    EXPECT_NEAR(game.x, kToX, 1e-3) << "the arrival snap no longer lands on the target";
    EXPECT_NEAR(last.playerX, game.x, 1e-3) << "the arrival pose never reached the renderer";
    EXPECT_NEAR(last.playerY, game.y, 1e-3);

    // And it stays idle with no further game-side movement.
    const PoseLogRow after = FrameAfterDrain();
    EXPECT_EQ(after.movementState, 0);
}

// The CONTROL (issue #231: "is it only command-driven movement?"). The
// renderer's own input poll -- a held MoveForward key -- reports movestate 1
// through the unchanged keyboard flags, and 0 once it is released. This was
// true before the fix and must stay true after it.
TEST_F(CommandMovementRenderTest, KeyboardMovementIsNonIdleWhileHeldAndIdleAfter) {
    auto& hotkeys = eqt::input::HotkeyManager::instance();
    hotkeys.resetToDefaults();
    auto* rx = renderer_->getEventReceiver();
    ASSERT_NE(rx, nullptr);

    irr::SEvent key{};
    key.EventType = irr::EET_KEY_INPUT_EVENT;
    key.KeyInput.Key = irr::KEY_KEY_W;
    key.KeyInput.Char = 0;
    key.KeyInput.Shift = false;
    key.KeyInput.Control = false;
    key.KeyInput.PressedDown = true;
    rx->OnEvent(key);

    PoseLogRow held{};
    for (int i = 0; i < 10; ++i) held = FrameAfterDrain();
    EXPECT_EQ(held.movementState, 1) << "a held MoveForward key is not reported as moving";
    EXPECT_GT(std::hypot(held.playerX - kX, held.playerY - kY), 0.1f)
        << "the keyboard did not move the rendered player";
    EXPECT_FALSE(eq_->Movement().HasCommandMovement())
        << "keyboard movement must never look like a game-driven walk";

    key.KeyInput.PressedDown = false;
    rx->OnEvent(key);
    PoseLogRow released{};
    for (int i = 0; i < 3; ++i) released = FrameAfterDrain();
    EXPECT_EQ(released.movementState, 0);
}

}  // namespace
