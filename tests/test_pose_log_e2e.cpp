// B874 audit (task/pose-log-e2e-test): --pose-log END TO END.
//
// tests/test_pose_log_flags.cpp pins the flags and the row FORMAT with a
// hand-built PoseLogRow. Nothing there reaches the GATHER --
// IrrlichtRenderer::getPoseData() plus the frame number and the wall clock
// the writer adds -- so a wrong field in the gather (heading units, camera
// frame, timestamp clock; review rounds 1-3 found one of each BY READING)
// would pass CI. This file closes that:
//
//   * a real IrrlichtRenderer, brought up by the production
//     initLoadingScreen() on the headless GLES2 device (Mesa surfaceless EGL,
//     llvmpipe -- the same device every GLES2 test in this tree uses), with
//     the loading screen dismissed by the production hideLoadingScreen();
//   * a COMMANDED pose through the production entry points
//     (setPlayerPosition(), the one the zone-in snapshot and PlayerMoved
//     reach, and the `/camera chase` slash command);
//   * N real processFrame() calls, each followed by the SAME
//     PoseLogWriter::onRenderedFrame() call Application::render() makes
//     (include/client/graphics/pose_log_writer.h);
//   * then the file is parsed and every column is checked against the
//     commanded pose in the DOCUMENTED units (pose_log_format.h).
//
// No zone is loaded and no server is involved: the renderer draws an empty
// scene, which is all the gather needs -- the pose it reports is the pose the
// frame was drawn with. The one production statement NOT reached here is the
// single `m_poseLog.onRenderedFrame(*m_renderer)` call site in
// Application::render(), which needs a zoned-in EverQuest.
//
// Registered in ctest (tests/cmake/commands.cmake) under the graphics tree,
// so the CI `test` job runs it; it FAILS (does not skip) when the headless
// device cannot be created, because a skipped end-to-end test is the silent
// no-output class this file exists to remove.
//
// WILLEQ_POSE_E2E_KEEP=<path>: copy the TSV the chase run wrote to <path>
// (evidence capture, refactor/B874/); unset, nothing outside the temp dir is
// touched.

#include <gtest/gtest.h>

#include <irrlicht.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "chase_rig_expected.h"
#include "scoped_temp_dir.h"

#include "client/bridge/game_state_bridge.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/willeq_database.h"
#include "client/graphics/player/camera_mode.h"
#include "client/graphics/player/camera_rig.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/pose_log_format.h"
#include "client/graphics/pose_log_writer.h"
#include "client/state/event_bus.h"

using namespace EQT::Graphics;

namespace {

constexpr float kPi = 3.14159265358979f;

// The commanded pose, CLIENT-internal EQ frame (pose_log_format.h). A
// non-integer heading on purpose: the pre-audit uint16_t column truncated
// it, and a x512/360 or x360/512 rescale moves it far outside tolerance.
constexpr float kX = 428.0f;
constexpr float kY = -74.0f;
constexpr float kZ = 3.119f;
constexpr float kHeading = 123.25f;
constexpr int kFrames = 24;

class NullBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

struct ParsedFile {
    std::string header;
    std::vector<std::vector<std::string>> rows;
};

ParsedFile ReadTsv(const std::string& path) {
    ParsedFile f;
    std::ifstream in(path);
    if (!in.good()) return f;
    std::getline(in, f.header);
    std::string line;
    while (std::getline(in, line)) f.rows.push_back(SplitTabs(line));
    return f;
}

double D(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

// The test's OWN wall clock -- deliberately not PoseLogWriter::unixMsNow(),
// or a writer that switched clocks would move the bounds with it.
int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

class PoseLogE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        // The shipped db, opened the way EverQuest::OpenDatabase() does:
        // initLoadingScreen() binds the race mappings to it.
        ASSERT_TRUE(db_.open(WILLEQ_DB_REAL_PATH)) << WILLEQ_DB_REAL_PATH;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        // The x86 client's own configuration: the default orangepi preset
        // with DRM off (Application::initConstrainedRendererConfig turns DRM
        // on only for --drm), handed over as Custom exactly like
        // BridgeComponent::InitGraphics() does. No EQE1 pack and no EQ client
        // path: the sky, the UI atlas and the particle atlas are skipped with
        // a warning, none of which the pose depends on.
        RendererConfig cfg;
        cfg.width = 320;
        cfg.height = 240;
        cfg.windowTitle = "pose-log-e2e";
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
    }
    void TearDown() override {
        if (renderer_) {
            renderer_->shutdown();
            renderer_.reset();
        }
        cache_.reset();
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // One rendered frame, exactly as Application::render() sequences it:
    // processFrame(), then the writer on success.
    void RenderFrames(PoseLogWriter& w, int n) {
        for (int i = 0; i < n; ++i) {
            ASSERT_TRUE(renderer_->processFrame(1.0f / 60.0f)) << "frame " << i;
            w.onRenderedFrame(*renderer_);
        }
    }

    void RunSlashCommand(const std::string& cmd) {
        renderer_->processSlashCommand(cmd);
        bridge_.drainEvents();
    }

    EQT::WilleqDatabase db_;
    NullBridge bridge_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<IrrlichtRenderer> renderer_;
};

// The one assertion set a wrong gather field cannot pass.
TEST_F(PoseLogE2ETest, ChaseRunWritesTheCommandedPoseInTheDocumentedUnits) {
    const willeq_test::ScopedTempDir tmp("pose_log_e2e");  // B571
    const std::string path = (tmp / "pose.tsv").string();

    renderer_->setPlayerPosition(kX, kY, kZ, kHeading);
    RunSlashCommand("/camera chase");
    ASSERT_EQ(renderer_->getCameraMode(), CameraMode::Chase);

    PoseLogWriter writer;
    std::string err;
    ASSERT_TRUE(writer.open(path, /*every=*/1, /*maxRows=*/600, &err)) << err;
    const int64_t before = NowMs();
    const int firstFrame = renderer_->getFrameNumber() + 1;
    RenderFrames(writer, kFrames);
    const int64_t after = NowMs();
    EXPECT_TRUE(writer.active()) << "24 rows is under the 600 cap";

    if (const char* keep = std::getenv("WILLEQ_POSE_E2E_KEEP")) {
        std::error_code ec;
        std::filesystem::copy_file(path, keep,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        EXPECT_FALSE(ec) << "WILLEQ_POSE_E2E_KEEP copy failed: " << ec.message();
    }

    const ParsedFile f = ReadTsv(path);
    ASSERT_FALSE(f.header.empty()) << "no file / no header at " << path;
    // Header: byte-exact against the spec (minus its '\n').
    const std::string spec = poseLogHeaderLine();
    EXPECT_EQ(f.header, spec.substr(0, spec.size() - 1));
    ASSERT_EQ(f.rows.size(), static_cast<size_t>(kFrames));

    // The Chase fit (camera_rig.h kChaseRig), placed around the commanded
    // pose. No player model is loaded, so the eye scale is the fit's own
    // (scaleChaseRigForEye is the identity) and the feet are playerZ. Written
    // out here from the rig's documented meaning -- azimuth 0 = straight
    // behind the heading, pitch above horizontal, distance from the look-at,
    // look-at `height` above the feet -- not by calling the production
    // computeCameraRigPose(), so the check is not circular.
    const float h = kHeading / 512.0f * 2.0f * kPi;
    const float p = kChaseRig.pitchDeg * kPi / 180.0f;
    const float horiz = kChaseRig.distance * std::cos(p);
    const double wantTx = kX, wantTy = kY, wantTz = kZ + kChaseRig.height;
    const double wantCx = kX - std::sin(h) * horiz;
    const double wantCy = kY - std::cos(h) * horiz;
    const double wantCz = kZ + kChaseRig.height + kChaseRig.distance * std::sin(p);

    int64_t lastTs = 0;
    for (size_t i = 0; i < f.rows.size(); ++i) {
        const auto& r = f.rows[i];
        SCOPED_TRACE("row " + std::to_string(i));
        ASSERT_EQ(r.size(), 14u) << "every row is 14 TAB-separated columns";

        // ts: unix epoch ms, taken during this run, non-decreasing.
        const int64_t ts = std::atoll(r[0].c_str());
        EXPECT_GE(ts, before - 2000) << "ts is not unix-ms wall clock";
        EXPECT_LE(ts, after + 2000) << "ts is not unix-ms wall clock";
        EXPECT_GE(ts, lastTs) << "ts went backwards";
        lastTs = ts;

        // frame: the renderer's own counter for the frame just drawn.
        EXPECT_EQ(std::atoi(r[1].c_str()), firstFrame + static_cast<int>(i));

        EXPECT_EQ(r[2], "Chase Camera") << "cameramode is the canonical eqstr name";

        // Camera and look-at, client EQ frame (Z up).
        EXPECT_NEAR(D(r[3]), wantCx, 1e-3) << "camX";
        EXPECT_NEAR(D(r[4]), wantCy, 1e-3) << "camY (EQ Y is Irrlicht Z)";
        EXPECT_NEAR(D(r[5]), wantCz, 1e-3) << "camZ (EQ Z is Irrlicht Y, the vertical)";
        EXPECT_NEAR(D(r[6]), wantTx, 1e-3) << "targetX";
        EXPECT_NEAR(D(r[7]), wantTy, 1e-3) << "targetY";
        EXPECT_NEAR(D(r[8]), wantTz, 1e-3) << "targetZ";
        // The same fit restated as rig geometry, so a failure names the axis.
        const double dx = D(r[3]) - D(r[6]), dy = D(r[4]) - D(r[7]), dz = D(r[5]) - D(r[8]);
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), kChaseRig.distance, 1e-3)
            << "camera is not kChaseRig.distance from the look-at";
        EXPECT_GT(dz, 0.0) << "the Chase camera sits ABOVE its look-at (pitch 6)";

        // The player, exactly as commanded, heading in EQ 0-512 units.
        EXPECT_NEAR(D(r[9]), kX, 1e-3) << "playerX";
        EXPECT_NEAR(D(r[10]), kY, 1e-3) << "playerY";
        EXPECT_NEAR(D(r[11]), kZ, 1e-3) << "playerZ";
        EXPECT_NEAR(D(r[12]), kHeading, 1e-3) << "heading0_512 must be EQ units, unscaled";

        EXPECT_EQ(r[13], "0") << "no input: idle";
    }
}

// The cadence, the cap and a camera-mode change are gather-side too: every
// Nth frame, frame numbers N apart, the file closed at the cap, and the
// column following the mode the frame was drawn in.
TEST_F(PoseLogE2ETest, EveryNthFrameCapAndModeFollowTheRenderLoop) {
    const willeq_test::ScopedTempDir tmp("pose_log_e2e_cadence");  // B571
    const std::string path = (tmp / "pose.tsv").string();

    renderer_->setPlayerPosition(kX, kY, kZ, kHeading);
    RunSlashCommand("/camera firstperson");
    ASSERT_EQ(renderer_->getCameraMode(), CameraMode::FirstPerson);

    PoseLogWriter writer;
    std::string err;
    ASSERT_TRUE(writer.open(path, /*every=*/3, /*maxRows=*/4, &err)) << err;
    RenderFrames(writer, 6);   // rows at frames 3 and 6
    RunSlashCommand("/camera chase");
    RenderFrames(writer, 12);  // rows at 9 and 12, then the cap closes it
    EXPECT_FALSE(writer.active()) << "the cap must close the file";
    EXPECT_TRUE(writer.capReached());

    const ParsedFile f = ReadTsv(path);
    ASSERT_EQ(f.rows.size(), 4u);
    for (size_t i = 1; i < f.rows.size(); ++i) {
        EXPECT_EQ(std::atoi(f.rows[i][1].c_str()) - std::atoi(f.rows[i - 1][1].c_str()), 3)
            << "rows are every 3rd RENDERED frame";
    }
    EXPECT_EQ(f.rows[0][2], "First Person Camera");
    EXPECT_EQ(f.rows[1][2], "First Person Camera");
    EXPECT_EQ(f.rows[2][2], "Chase Camera");
    EXPECT_EQ(f.rows[3][2], "Chase Camera");
    for (const auto& r : f.rows) {
        ASSERT_EQ(r.size(), 14u);
        EXPECT_NEAR(D(r[12]), kHeading, 1e-3);
    }
}

// ---------------------------------------------------------------------------
// B875 (issue #219): ChaseRigPinnedE2E -- the renderer half of the Chase pin
// (the device-free half is tests/test_chase_rig_pinned.cpp). The test above
// derives its expectation FROM kChaseRig, so it stays green whatever
// kChaseRig says; these compare the camera the real renderer places against
// the COMMITTED literals in tests/chase_rig_expected.h, so an edit to the rig,
// to its wiring into PlayerCameraComponent, or to the projection is red.
//
// Those literals are fable's CURRENT constants, not an official-client
// measurement (chase_rig_expected.h says why): this catches drift, it does not
// prove oracle parity.
//
// No player model is loaded, so the eye is the fitted one and the race scale
// is the identity here; the scale itself is pinned device-free at a second
// eye height (ChaseRigPinned.ProductionPoseEqualsTheClosedFormOfTheExpectedValues).
// ---------------------------------------------------------------------------

class ChaseRigPinnedE2E : public PoseLogE2ETest {
protected:
    // Command a pose through the production entry point, draw two frames and
    // read back the pose those frames were drawn with (the gather the
    // --pose-log writer uses).
    PoseLogRow DrawAt(float x, float y, float z, float heading) {
        renderer_->setPlayerPosition(x, y, z, heading);
        for (int i = 0; i < 2; ++i) {
            EXPECT_TRUE(renderer_->processFrame(1.0f / 60.0f)) << "frame " << i;
        }
        PoseLogRow row;
        renderer_->getPoseData(row);
        return row;
    }
};

TEST_F(ChaseRigPinnedE2E, RenderedChaseCameraEqualsTheClosedFormOfTheCommittedValues) {
    namespace ex = chase_rig_expected;
    renderer_->setPlayerPosition(kX, kY, kZ, kHeading);
    RunSlashCommand("/camera chase");
    ASSERT_EQ(renderer_->getCameraMode(), CameraMode::Chase);

    struct Where { float x, y, z; };
    const Where positions[] = {{kX, kY, kZ}, {-150.75f, 212.5f, -12.25f}};
    const float headings[] = {0.0f, 123.25f, 256.0f, 400.5f};
    // float rounding in the production path at these magnitudes is ~3e-5;
    // 1e-4 turns a 0.001 change of distance or height, or a 0.01 degree change
    // of pitch or azimuth, red (refactor/B875/README.md has the RED runs).
    constexpr double kTol = 1e-4;
    constexpr double kAngleTolDeg = 2e-3;

    for (const Where& w : positions) {
        for (float h : headings) {
            SCOPED_TRACE(::testing::Message() << "pos (" << w.x << ", " << w.y << ", " << w.z
                                              << ") heading " << h);
            const PoseLogRow r = DrawAt(w.x, w.y, w.z, h);
            ASSERT_EQ(r.cameraMode, CameraMode::Chase);
            ASSERT_NEAR(r.playerX, w.x, 1e-4);
            ASSERT_NEAR(r.playerY, w.y, 1e-4);
            ASSERT_NEAR(r.playerZ, w.z, 1e-4) << "no zone: the feet stay where commanded";

            // The closed form of the committed values, double precision.
            const double hr = h / 512.0 * 2.0 * kPi + ex::kAzimuthDeg * kPi / 180.0;
            const double pr = ex::kPitchDeg * kPi / 180.0;
            const double horiz = ex::kDistance * std::cos(pr);
            const double tz = w.z + ex::kHeight;
            EXPECT_NEAR(r.targetX, w.x, kTol) << "targetX";
            EXPECT_NEAR(r.targetY, w.y, kTol) << "targetY";
            EXPECT_NEAR(r.targetZ, tz, kTol) << "targetZ = feet + height";
            EXPECT_NEAR(r.camX, w.x - std::sin(hr) * horiz, kTol) << "camX";
            EXPECT_NEAR(r.camY, w.y - std::cos(hr) * horiz, kTol) << "camY";
            EXPECT_NEAR(r.camZ, tz + ex::kDistance * std::sin(pr), kTol) << "camZ";

            // The same, recovered as rig geometry, so a failure names the constant.
            const double dx = r.camX - r.targetX, dy = r.camY - r.targetY, dz = r.camZ - r.targetZ;
            const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double pitchDeg = std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 180.0 / kPi;
            // Behind = -forward: the camera offset points along -(sin a, cos a).
            double azDeg = (std::atan2(-dx, -dy) - h / 512.0 * 2.0 * kPi) * 180.0 / kPi;
            azDeg = std::remainder(azDeg, 360.0);
            EXPECT_NEAR(dist, ex::kDistance, kTol) << "recovered distance";
            EXPECT_NEAR(pitchDeg, ex::kPitchDeg, kAngleTolDeg) << "recovered pitch (deg)";
            EXPECT_NEAR(azDeg, ex::kAzimuthDeg, kAngleTolDeg) << "recovered azimuth (deg)";
            EXPECT_NEAR(r.targetZ - r.playerZ, ex::kHeight, kTol) << "recovered look-at height";
        }
    }
}

TEST_F(ChaseRigPinnedE2E, RenderedChaseFieldOfViewIsTheCommittedValue) {
    namespace ex = chase_rig_expected;
    renderer_->setPlayerPosition(kX, kY, kZ, kHeading);
    RunSlashCommand("/camera chase");
    ASSERT_EQ(renderer_->getCameraMode(), CameraMode::Chase);
    ASSERT_TRUE(renderer_->processFrame(1.0f / 60.0f));

    irr::scene::ICameraSceneNode* cam = renderer_->getDevice()->getSceneManager()->getActiveCamera();
    ASSERT_NE(cam, nullptr);
    // Irrlicht holds the VERTICAL angle; the committed value is horizontal.
    const double hfovDeg =
        2.0 * std::atan(std::tan(cam->getFOV() / 2.0) * cam->getAspectRatio()) * 180.0 / kPi;
    EXPECT_NEAR(hfovDeg, ex::kHorizontalFovDeg, 1e-3) << "Chase horizontal FOV (deg)";
    EXPECT_NEAR(cam->getAspectRatio(), 320.0 / 240.0, 1e-4) << "the harness's 320x240 device";
}

}  // namespace
