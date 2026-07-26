/**
 * Integration tests for the Inventory Window Character Model Preview
 *
 * These tests verify that the paperdoll character model in the inventory window:
 * - Loads correctly for different races/genders
 * - Has textures applied
 * - Shows equipped weapons
 * - Animates properly
 * - Renders to texture correctly
 *
 * Requirements:
 * - Running EQEmu server (login + world + zone)
 * - Test account and character configured (uses /home/user/projects/claude/casterella.json)
 * - X display available (use DISPLAY=:99 with Xvfb for headless testing)
 * - EQ client files available at configured eq_client_path
 *
 * Usage:
 *   DISPLAY=:99 ./bin/test_inventory_model_view [--config /path/to/config.json]
 */

#include <gtest/gtest.h>
#include <fstream>
#include <json/json.h>
#include <chrono>
#include <thread>

#include "client/eq.h"
#include "common/logging.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/bridge/irrlicht_bridge.h"
#include "client/bridge/game_state_bridge.h"
// WindowManager has been deleted — these tests skip at runtime
// #include "client/graphics/ui/window_manager.h"
// #include "client/graphics/ui/inventory_window.h"
// #include "client/graphics/ui/character_model_view.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#endif

using namespace std::chrono_literals;

// Default config path
static std::string g_configPath = "/home/user/projects/claude/casterella.json";

class InventoryModelViewTest : public ::testing::Test {
protected:
    struct TestConfig {
        std::string host = "127.0.0.1";
        int port = 5998;
        std::string user;
        std::string pass;
        std::string server;
        std::string character;
        std::string eqClientPath;
        std::string mapsPath;
        std::string navmeshPath;
        int timeoutSeconds = 60;
        bool loaded = false;
    };

    TestConfig config_;
    std::unique_ptr<EverQuest> eq_;
    std::unique_ptr<EQT::Graphics::IrrlichtRenderer> renderer_;
    std::unique_ptr<eqt::bridge::IrrlichtBridge> bridge_;
    std::chrono::steady_clock::time_point lastFrameTime_;

    void SetUp() override {
#ifndef EQT_HAS_GRAPHICS
        GTEST_SKIP() << "Graphics support not compiled in (EQT_HAS_GRAPHICS not defined)";
#endif

        // Check for DISPLAY environment variable
        const char* display = std::getenv("DISPLAY");
        if (!display || strlen(display) == 0) {
            GTEST_SKIP() << "DISPLAY environment variable not set. Use DISPLAY=:99 with Xvfb.";
        }
        std::cout << "Using DISPLAY=" << display << std::endl;

        // Load test configuration
        loadConfig();
        if (!config_.loaded) {
            GTEST_SKIP() << "Test config not found or invalid at: " << g_configPath;
        }

        // Check for EQ client path
        if (config_.eqClientPath.empty()) {
            GTEST_SKIP() << "eq_client_path not configured - required for graphics tests";
        }

        lastFrameTime_ = std::chrono::steady_clock::now();
    }

    void TearDown() override {
        if (eq_) {
            eq_.reset();
        }
    }

    void loadConfig() {
        std::ifstream file(g_configPath);
        if (!file.is_open()) {
            std::cerr << "Cannot open config file: " << g_configPath << std::endl;
            return;
        }

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        if (!Json::parseFromStream(builder, file, &root, &errors)) {
            std::cerr << "Failed to parse config: " << errors << std::endl;
            return;
        }

        // Config has a "clients" array, use first client
        if (!root.isMember("clients") || !root["clients"].isArray() || root["clients"].empty()) {
            std::cerr << "Config missing 'clients' array" << std::endl;
            return;
        }

        const auto& client = root["clients"][0];

        config_.host = client.get("host", "127.0.0.1").asString();
        config_.port = client.get("port", 5998).asInt();
        config_.user = client.get("user", "").asString();
        config_.pass = client.get("pass", "").asString();
        config_.server = client.get("server", "").asString();
        config_.character = client.get("character", "").asString();
        config_.eqClientPath = client.get("eq_client_path", "").asString();
        config_.mapsPath = client.get("maps_path", "").asString();
        config_.navmeshPath = client.get("navmesh_path", "").asString();
        config_.timeoutSeconds = client.get("timeout_seconds", 60).asInt();

        if (config_.user.empty() || config_.pass.empty() ||
            config_.server.empty() || config_.character.empty()) {
            std::cerr << "Missing required fields in config" << std::endl;
            return;
        }

        config_.loaded = true;
        std::cout << "Loaded config for " << config_.character << "@" << config_.server << std::endl;
    }

    bool createClientWithGraphics() {
        try {
            eq_ = std::make_unique<EverQuest>(
                config_.host, config_.port,
                config_.user, config_.pass,
                config_.server, config_.character
            );

            // Configure paths - EQ client path is required for graphics
            if (!config_.mapsPath.empty()) {
                eq_->SetMapsPath(config_.mapsPath);
            }
            if (!config_.navmeshPath.empty()) {
                eq_->SetNavmeshPath(config_.navmeshPath);
            }
            eq_->SetEQClientPath(config_.eqClientPath);

#ifdef EQT_HAS_GRAPHICS
            // Initialize graphics with a small window size for testing
            std::cout << "Initializing graphics (800x600)..." << std::endl;
            renderer_ = std::make_unique<EQT::Graphics::IrrlichtRenderer>();
            bridge_ = std::make_unique<eqt::bridge::IrrlichtBridge>();
            bridge_->setRenderer(renderer_.get());
            renderer_->setBridge(bridge_.get());
            if (!eq_->InitGraphics(800, 600, renderer_.get(), bridge_.get())) {
                std::cerr << "Failed to initialize graphics" << std::endl;
                return false;
            }
            std::cout << "Graphics initialized successfully" << std::endl;
#endif

            eq_->ConnectToLogin();

            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to create client: " << e.what() << std::endl;
            return false;
        }
    }

    // Calculate delta time since last frame
    float getDeltaTime() {
        auto now = std::chrono::steady_clock::now();
        float delta = std::chrono::duration<float>(now - lastFrameTime_).count();
        lastFrameTime_ = now;
        return delta;
    }

    // Run the event loop with graphics processing until a condition is met or timeout
    template<typename Predicate>
    bool waitForWithGraphics(Predicate condition, int timeoutMs = 30000) {
        auto start = std::chrono::steady_clock::now();
        while (!condition()) {
            eq_->TickNetwork();
            if (eq_) {
                eq_->UpdateMovement();
#ifdef EQT_HAS_GRAPHICS
                // Process graphics frame
                if (renderer_) {
                    float deltaTime = getDeltaTime();
                    if (!renderer_->processFrame(deltaTime)) {
                        // Window was closed
                        std::cerr << "Graphics window closed unexpectedly" << std::endl;
                        return false;
                    }
                }
#endif
            }
            std::this_thread::sleep_for(16ms);  // ~60 FPS

            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeoutMs) {
                return false;
            }
        }
        return true;
    }

    // Wait until fully zoned in (network)
    bool waitForZoneIn(int timeoutMs = 30000) {
        return waitForWithGraphics([this]() { return eq_->IsFullyZonedIn(); }, timeoutMs);
    }

#ifdef EQT_HAS_GRAPHICS
    // Wait until graphics zone is ready (zone geometry loaded, player entity created)
    bool waitForZoneReady(int timeoutMs = 30000) {
        return waitForWithGraphics([this]() {
            return renderer_ && renderer_->isZoneReady();
        }, timeoutMs);
    }

    // Process a number of frames (for animation testing)
    void processFrames(int count, float frameTimeMs = 16.67f) {
        if (!renderer_) return;

        for (int i = 0; i < count; i++) {
            eq_->TickNetwork();
            eq_->UpdateMovement();
            renderer_->processFrame(frameTimeMs / 1000.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(frameTimeMs)));
        }
    }
#endif
};

// Test: Verify inventory window model view initializes and loads character model
TEST_F(InventoryModelViewTest, ModelViewInitializesAndLoadsCharacter) {
    GTEST_SKIP() << "WindowManager has been deleted - test disabled";
}

// Test: Verify character model has textures applied
TEST_F(InventoryModelViewTest, ModelHasTexturesApplied) {
    GTEST_SKIP() << "WindowManager has been deleted - test disabled";
}

// Test: Verify character model animates properly
// P12B: this full-stack variant needs a live EQEmu server + the (deleted)
// WindowManager/CharacterModelView paperdoll UI, neither of which exists in
// CI — documented conditional skip, not fixable without a UI rewrite (out of
// scope here). See RaceModelAnimationHeadlessTest::ModelAnimatesProperly_Headless
// below for the headlessly-runnable variant of the same assertion (does the
// per-instance animated mesh actually move over time, at the RaceModelLoader/
// EQAnimatedMeshSceneNode level the paperdoll view itself renders through) —
// that variant DOES run in CI (EDT_NULL device, no DISPLAY/server needed).
TEST_F(InventoryModelViewTest, ModelAnimatesProperly) {
    GTEST_SKIP() << "WindowManager has been deleted - test disabled; "
                     "see RaceModelAnimationHeadlessTest::ModelAnimatesProperly_Headless "
                     "for a headlessly-runnable coverage of the same assertion";
}

// Test: Verify equipped weapons show on model
TEST_F(InventoryModelViewTest, EquippedWeaponsShowOnModel) {
    GTEST_SKIP() << "WindowManager has been deleted - test disabled";
}

// Test: Verify model view renders to texture
TEST_F(InventoryModelViewTest, ModelRendersToTexture) {
    GTEST_SKIP() << "WindowManager has been deleted - test disabled";
}

// Test: Verify model view survives zone transition
TEST_F(InventoryModelViewTest, ModelViewSurvivesZoneTransition) {
    GTEST_SKIP() << "WindowManager has been deleted - test disabled";
}

// ============================================================================
// P12B: Headless animation-coherence coverage
//
// The tests above need a live EQEmu server plus the paperdoll UI
// (WindowManager/CharacterModelView), which was deleted — genuinely
// un-runnable in CI without a UI rewrite (out of scope for this unit).
//
// This fixture exercises the SAME underlying mechanism the paperdoll view
// renders through — RaceModelLoader::createAnimatedNode() and
// EQAnimatedMeshSceneNode's per-instance OnAnimate()/applyAnimation() —
// using an EDT_NULL Irrlicht device (no window, no DISPLAY, no server).
// It asserts the class of regression P12b found and fixed (gles2_zone_
// benchmark's animated entities silently freezing at their raw, unposed,
// per-bone-local vertex layout — see refactor/P12b_benchmark_node_wiring.md):
// after playing an animation and stepping OnAnimate across several frames,
// the per-instance mesh's vertex data must actually move, and stay within a
// plausible bounding-box envelope for a humanoid model (not collapse to a
// degenerate point, not explode).
// ============================================================================
#ifdef EQT_HAS_GRAPHICS
class RaceModelAnimationHeadlessTest : public ::testing::Test {
protected:
    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::io::IFileSystem* fs_ = nullptr;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;

    void SetUp() override {
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        eqClientPath_ = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') {
            eqClientPath_ += '/';
        }
        if (!fileExists(eqClientPath_ + "global_chr.s3d")) {
            GTEST_SKIP() << "global_chr.s3d not found at " << eqClientPath_
                         << " - required for headless animation coverage";
        }

        // EDT_NULL: no window, no GPU, no DISPLAY needed — matches the
        // pattern in test_door_loading.cpp's DoorManagerGraphicsTest.
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        smgr_ = device_->getSceneManager();
        driver_ = device_->getVideoDriver();
        fs_ = device_->getFileSystem();

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(smgr_, driver_, fs_);
        loader_->setClientPath(eqClientPath_);
    }

    void TearDown() override {
        loader_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }
};

TEST_F(RaceModelAnimationHeadlessTest, ModelAnimatesProperly_Headless) {
    // Human male (1) and Barbarian male (2): two distinct skeletons/meshes,
    // matching the plan's "coherent walk/idle cycles across races" bar at a
    // scale that stays fast for CI.
    struct RaceCase { uint16_t raceId; uint8_t gender; const char* label; };
    const RaceCase races[] = {
        {1, 0, "HUM"},
        {2, 0, "BAM"},
    };

    for (const auto& rc : races) {
        SCOPED_TRACE(rc.label);

        auto* node = loader_->createAnimatedNode(rc.raceId, rc.gender,
                                                   smgr_->getRootSceneNode());
        if (!node) {
            // Some builds may not ship every race's chr data — skip that
            // race rather than fail the whole test on missing assets.
            std::cout << "  " << rc.label << ": createAnimatedNode() returned "
                         "null (race data unavailable) - skipping\n";
            continue;
        }
        ASSERT_NE(node->getInstanceMesh(), nullptr)
            << rc.label << ": animated node has no per-instance mesh";

        ASSERT_TRUE(node->playAnimation("l01", true) || node->playAnimation("o01", true))
            << rc.label << ": neither l01 (walk) nor o01 (idle) animation available";

        // Step OnAnimate across several synthetic frames, exactly like
        // production's drawAll()-driven idiom and the (now-fixed) benchmark's
        // OnAnimate(frame * 33.3ms) idiom.
        const irr::video::S3DVertex* firstBuf =
            static_cast<const irr::video::S3DVertex*>(
                node->getInstanceMesh()->getMeshBuffer(0)->getVertices());
        irr::core::vector3df vertexAtT0 = firstBuf[0].Pos;

        irr::u32 simTimeMs = 0;
        irr::core::aabbox3df bboxAtT[4];
        irr::core::vector3df vertexAtT[4];
        for (int f = 0; f < 4; ++f) {
            simTimeMs += 100;  // 100ms steps
            node->OnAnimate(simTimeMs);
            bboxAtT[f] = node->getBoundingBox();
            const irr::video::S3DVertex* buf =
                static_cast<const irr::video::S3DVertex*>(
                    node->getInstanceMesh()->getMeshBuffer(0)->getVertices());
            vertexAtT[f] = buf[0].Pos;
        }

        // 1) Not degenerate: a humanoid model's bounding box must have a
        //    plausible extent on every axis (the frozen-raw-mesh bug this
        //    guards produced a bbox that was either near-zero on some axis
        //    or wildly larger than a human, depending on race/pose — the
        //    P12b contact sheet in benchmarks/contact_sheets/p12b_animated/
        //    has visual references).
        for (int f = 0; f < 4; ++f) {
            auto ext = bboxAtT[f].getExtent();
            EXPECT_GT(ext.Y, 1.0f) << rc.label << " frame " << f
                << ": bounding-box height implausibly small (collapsed pose?)";
            EXPECT_LT(ext.Y, 20.0f) << rc.label << " frame " << f
                << ": bounding-box height implausibly large (exploded pose?)";
            EXPECT_LT(ext.X, 20.0f) << rc.label << " frame " << f
                << ": bounding-box width implausibly large (exploded pose?)";
            EXPECT_LT(ext.Z, 20.0f) << rc.label << " frame " << f
                << ": bounding-box depth implausibly large (exploded pose?)";
        }

        // 2) Actually animating: at least one of the sampled frames must
        //    show the tracked vertex having moved from both its bind-pose
        //    position AND from at least one other sampled frame. A node
        //    silently frozen at one pose (this unit's root cause, when a
        //    stale GPU snapshot was drawn instead of the live CPU vertex
        //    data) would show byte-identical positions across every frame.
        bool sawMovementFromBindPose = false;
        bool sawMovementBetweenFrames = false;
        for (int f = 0; f < 4; ++f) {
            if (vertexAtT[f].getDistanceFromSQ(vertexAtT0) > 1e-6f) {
                sawMovementFromBindPose = true;
            }
            if (f > 0 && vertexAtT[f].getDistanceFromSQ(vertexAtT[f - 1]) > 1e-6f) {
                sawMovementBetweenFrames = true;
            }
        }
        EXPECT_TRUE(sawMovementFromBindPose)
            << rc.label << ": tracked vertex never moved from its bind pose "
               "across 4 animated frames - model appears frozen";
        EXPECT_TRUE(sawMovementBetweenFrames)
            << rc.label << ": tracked vertex never moved between consecutive "
               "animated frames - model appears frozen mid-animation";
    }
}
#endif // EQT_HAS_GRAPHICS

// Main function to support custom config path
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Check for --config argument
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            g_configPath = argv[i + 1];
            std::cout << "Using config: " << g_configPath << std::endl;
        }
    }

    std::cout << "=== Inventory Model View Integration Tests ===" << std::endl;
    std::cout << "These tests require:" << std::endl;
    std::cout << "  - Running EQEmu server" << std::endl;
    std::cout << "  - X display (DISPLAY=:99 with Xvfb for headless)" << std::endl;
    std::cout << "  - EQ client files at configured eq_client_path" << std::endl;
    std::cout << std::endl;

    return RUN_ALL_TESTS();
}
