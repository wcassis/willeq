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
#include "common/event/event_loop.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/window_manager.h"
#include "client/graphics/ui/inventory_window.h"
#include "client/graphics/ui/character_model_view.h"
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
            if (!eq_->InitGraphics(800, 600)) {
                std::cerr << "Failed to initialize graphics" << std::endl;
                return false;
            }
            std::cout << "Graphics initialized successfully" << std::endl;
#endif

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
            EQ::EventLoop::Get().Process();
            if (eq_) {
                eq_->UpdateMovement();
#ifdef EQT_HAS_GRAPHICS
                // Process graphics frame
                auto* renderer = eq_->GetRenderer();
                if (renderer) {
                    float deltaTime = getDeltaTime();
                    if (!renderer->processFrame(deltaTime)) {
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
            auto* renderer = eq_->GetRenderer();
            return renderer && renderer->isZoneReady();
        }, timeoutMs);
    }

    // Process a number of frames (for animation testing)
    void processFrames(int count, float frameTimeMs = 16.67f) {
        auto* renderer = eq_->GetRenderer();
        if (!renderer) return;

        for (int i = 0; i < count; i++) {
            EQ::EventLoop::Get().Process();
            eq_->UpdateMovement();
            renderer->processFrame(frameTimeMs / 1000.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(frameTimeMs)));
        }
    }
#endif
};

// Test: Verify inventory window model view initializes and loads character model
TEST_F(InventoryModelViewTest, ModelViewInitializesAndLoadsCharacter) {
    ASSERT_TRUE(createClientWithGraphics());

    std::cout << "Waiting for zone-in..." << std::endl;
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for zone-in";

#ifdef EQT_HAS_GRAPHICS
    std::cout << "Waiting for graphics zone ready..." << std::endl;
    ASSERT_TRUE(waitForZoneReady(30000))
        << "Timed out waiting for graphics zone ready";

    auto* renderer = eq_->GetRenderer();
    ASSERT_NE(renderer, nullptr) << "Renderer is null";

    auto* windowManager = renderer->getWindowManager();
    ASSERT_NE(windowManager, nullptr) << "WindowManager is null";

    auto* inventoryWindow = windowManager->getInventoryWindow();
    ASSERT_NE(inventoryWindow, nullptr) << "InventoryWindow is null";

    // Verify model view exists
    EXPECT_TRUE(inventoryWindow->hasModelView())
        << "Inventory window should have a model view";

    auto* modelView = inventoryWindow->getModelView();
    ASSERT_NE(modelView, nullptr) << "Model view is null";

    // Verify model view is ready
    EXPECT_TRUE(modelView->isReady())
        << "Model view should be ready after zone-in";

    // Verify character model is loaded
    EXPECT_TRUE(modelView->hasCharacterModel())
        << "Character model should be loaded";

    // Log model info
    std::cout << "Model view state:" << std::endl;
    std::cout << "  Race ID: " << modelView->getCurrentRaceId() << std::endl;
    std::cout << "  Gender: " << (int)modelView->getCurrentGender() << std::endl;
    std::cout << "  Has model: " << (modelView->hasCharacterModel() ? "yes" : "no") << std::endl;
    std::cout << "  Material count: " << modelView->getMaterialCount() << std::endl;
    std::cout << "  Has textures: " << (modelView->hasTextures() ? "yes" : "no") << std::endl;
    std::cout << "  Is animating: " << (modelView->isAnimating() ? "yes" : "no") << std::endl;
    std::cout << "  Primary weapon ID: " << modelView->getPrimaryWeaponId() << std::endl;
    std::cout << "  Secondary weapon ID: " << modelView->getSecondaryWeaponId() << std::endl;
#endif
}

// Test: Verify character model has textures applied
TEST_F(InventoryModelViewTest, ModelHasTexturesApplied) {
    ASSERT_TRUE(createClientWithGraphics());

    std::cout << "Waiting for zone-in..." << std::endl;
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for zone-in";

#ifdef EQT_HAS_GRAPHICS
    std::cout << "Waiting for graphics zone ready..." << std::endl;
    ASSERT_TRUE(waitForZoneReady(30000))
        << "Timed out waiting for graphics zone ready";

    auto* renderer = eq_->GetRenderer();
    ASSERT_NE(renderer, nullptr);

    auto* windowManager = renderer->getWindowManager();
    ASSERT_NE(windowManager, nullptr);

    auto* inventoryWindow = windowManager->getInventoryWindow();
    ASSERT_NE(inventoryWindow, nullptr);

    auto* modelView = inventoryWindow->getModelView();
    ASSERT_NE(modelView, nullptr);
    ASSERT_TRUE(modelView->hasCharacterModel());

    // Verify model has textures
    EXPECT_TRUE(modelView->hasTextures())
        << "Character model should have textures applied";

    // Verify model has at least one material
    EXPECT_GT(modelView->getMaterialCount(), 0u)
        << "Character model should have at least one material";

    std::cout << "Model has " << modelView->getMaterialCount() << " materials with textures" << std::endl;
#endif
}

// Test: Verify character model animates properly
TEST_F(InventoryModelViewTest, ModelAnimatesProperly) {
    ASSERT_TRUE(createClientWithGraphics());

    std::cout << "Waiting for zone-in..." << std::endl;
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for zone-in";

#ifdef EQT_HAS_GRAPHICS
    std::cout << "Waiting for graphics zone ready..." << std::endl;
    ASSERT_TRUE(waitForZoneReady(30000))
        << "Timed out waiting for graphics zone ready";

    auto* renderer = eq_->GetRenderer();
    ASSERT_NE(renderer, nullptr);

    auto* windowManager = renderer->getWindowManager();
    ASSERT_NE(windowManager, nullptr);

    auto* inventoryWindow = windowManager->getInventoryWindow();
    ASSERT_NE(inventoryWindow, nullptr);

    auto* modelView = inventoryWindow->getModelView();
    ASSERT_NE(modelView, nullptr);
    ASSERT_TRUE(modelView->hasCharacterModel());

    // Verify model is set up for animation
    EXPECT_TRUE(modelView->isAnimating())
        << "Character model should be animating (idle animation)";

    // Process several frames to verify animation continues
    std::cout << "Processing 60 frames to verify animation..." << std::endl;
    float initialRotation = modelView->getRotationY();

    processFrames(60);

    // Animation should still be running
    EXPECT_TRUE(modelView->isAnimating())
        << "Character model should still be animating after 60 frames";

    std::cout << "Animation verified - model continues to animate" << std::endl;
#endif
}

// Test: Verify equipped weapons show on model
TEST_F(InventoryModelViewTest, EquippedWeaponsShowOnModel) {
    ASSERT_TRUE(createClientWithGraphics());

    std::cout << "Waiting for zone-in..." << std::endl;
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for zone-in";

#ifdef EQT_HAS_GRAPHICS
    std::cout << "Waiting for graphics zone ready..." << std::endl;
    ASSERT_TRUE(waitForZoneReady(30000))
        << "Timed out waiting for graphics zone ready";

    auto* renderer = eq_->GetRenderer();
    ASSERT_NE(renderer, nullptr);

    auto* windowManager = renderer->getWindowManager();
    ASSERT_NE(windowManager, nullptr);

    auto* inventoryWindow = windowManager->getInventoryWindow();
    ASSERT_NE(inventoryWindow, nullptr);

    auto* modelView = inventoryWindow->getModelView();
    ASSERT_NE(modelView, nullptr);
    ASSERT_TRUE(modelView->hasCharacterModel());

    // Check for equipped weapons
    // Note: This depends on the character actually having weapons equipped
    uint32_t primaryId = modelView->getPrimaryWeaponId();
    uint32_t secondaryId = modelView->getSecondaryWeaponId();

    std::cout << "Equipped items:" << std::endl;
    std::cout << "  Primary weapon ID: " << primaryId << std::endl;
    std::cout << "  Secondary weapon ID: " << secondaryId << std::endl;
    std::cout << "  Has primary weapon node: " << (modelView->hasPrimaryWeapon() ? "yes" : "no") << std::endl;
    std::cout << "  Has secondary weapon node: " << (modelView->hasSecondaryWeapon() ? "yes" : "no") << std::endl;

    // If the character has a weapon equipped (ID > 0), the weapon node should exist
    if (primaryId > 0) {
        EXPECT_TRUE(modelView->hasPrimaryWeapon())
            << "Primary weapon node should exist when weapon is equipped (ID=" << primaryId << ")";
    }
    if (secondaryId > 0) {
        EXPECT_TRUE(modelView->hasSecondaryWeapon())
            << "Secondary weapon node should exist when weapon/shield is equipped (ID=" << secondaryId << ")";
    }
#endif
}

// Test: Verify model view renders to texture
TEST_F(InventoryModelViewTest, ModelRendersToTexture) {
    ASSERT_TRUE(createClientWithGraphics());

    std::cout << "Waiting for zone-in..." << std::endl;
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for zone-in";

#ifdef EQT_HAS_GRAPHICS
    std::cout << "Waiting for graphics zone ready..." << std::endl;
    ASSERT_TRUE(waitForZoneReady(30000))
        << "Timed out waiting for graphics zone ready";

    auto* renderer = eq_->GetRenderer();
    ASSERT_NE(renderer, nullptr);

    auto* windowManager = renderer->getWindowManager();
    ASSERT_NE(windowManager, nullptr);

    auto* inventoryWindow = windowManager->getInventoryWindow();
    ASSERT_NE(inventoryWindow, nullptr);

    auto* modelView = inventoryWindow->getModelView();
    ASSERT_NE(modelView, nullptr);
    ASSERT_TRUE(modelView->isReady());

    // Verify render target texture exists
    auto* texture = modelView->getTexture();
    EXPECT_NE(texture, nullptr)
        << "Model view should have a render target texture";

    if (texture) {
        auto size = texture->getSize();
        std::cout << "Render target texture size: " << size.Width << "x" << size.Height << std::endl;
        EXPECT_GT(size.Width, 0u) << "Texture width should be > 0";
        EXPECT_GT(size.Height, 0u) << "Texture height should be > 0";
    }

    // Render several frames to ensure render-to-texture works
    std::cout << "Processing 30 frames to verify render-to-texture..." << std::endl;
    processFrames(30);

    // Texture should still be valid
    EXPECT_NE(modelView->getTexture(), nullptr)
        << "Render target texture should still be valid after rendering";

    std::cout << "Render-to-texture verified" << std::endl;
#endif
}

// Test: Verify model view survives zone transition
TEST_F(InventoryModelViewTest, ModelViewSurvivesZoneTransition) {
    ASSERT_TRUE(createClientWithGraphics());

    std::cout << "Waiting for initial zone-in..." << std::endl;
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for initial zone-in";

#ifdef EQT_HAS_GRAPHICS
    std::cout << "Waiting for graphics zone ready..." << std::endl;
    ASSERT_TRUE(waitForZoneReady(30000))
        << "Timed out waiting for graphics zone ready";

    auto* renderer = eq_->GetRenderer();
    ASSERT_NE(renderer, nullptr);

    auto* windowManager = renderer->getWindowManager();
    ASSERT_NE(windowManager, nullptr);

    auto* inventoryWindow = windowManager->getInventoryWindow();
    ASSERT_NE(inventoryWindow, nullptr);

    // Verify initial state
    auto* modelView = inventoryWindow->getModelView();
    ASSERT_NE(modelView, nullptr);
    ASSERT_TRUE(modelView->hasCharacterModel());
    ASSERT_TRUE(modelView->isReady());

    uint16_t initialRaceId = modelView->getCurrentRaceId();
    std::cout << "Initial model - Race: " << initialRaceId << std::endl;

    // Find a zone line
    std::string startZone = eq_->GetCurrentZoneName();
    std::cout << "Starting zone: " << startZone << std::endl;

    // Load zone_lines.json to find a zone line
    std::ifstream file("data/zone_lines.json");
    if (!file.is_open()) {
        file.open("../data/zone_lines.json");
    }
    if (!file.is_open()) {
        GTEST_SKIP() << "Cannot open zone_lines.json - skipping zone transition test";
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        GTEST_SKIP() << "Failed to parse zone_lines.json";
    }

    if (!root.isMember(startZone) || !root[startZone].isArray() || root[startZone].empty()) {
        GTEST_SKIP() << "No zone lines defined for zone: " << startZone;
    }

    // Get first zone line
    const auto& zoneLine = root[startZone][0u];
    float x = (zoneLine["min_x"].asFloat() + zoneLine["max_x"].asFloat()) / 2;
    float y = (zoneLine["min_y"].asFloat() + zoneLine["max_y"].asFloat()) / 2;
    float z = (zoneLine["min_z"].asFloat() + zoneLine["max_z"].asFloat()) / 2;

    std::cout << "Teleporting to zone line at (" << x << ", " << y << ", " << z << ")" << std::endl;

    // Trigger zone transition
    eq_->SetPosition(x, y, z);

    // Wait for zone-out
    bool leftZone = waitForWithGraphics([this]() {
        return !eq_->IsFullyZonedIn();
    }, 10000);

    if (!leftZone) {
        GTEST_SKIP() << "Zone line did not trigger";
    }

    std::cout << "Zone-out detected, waiting for new zone..." << std::endl;

    // Wait for zone-in to new zone
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for zone-in after transition";

    ASSERT_TRUE(waitForZoneReady(30000))
        << "Timed out waiting for graphics zone ready after transition";

    std::string newZone = eq_->GetCurrentZoneName();
    std::cout << "Zoned into: " << newZone << std::endl;

    // Verify model view still works after zone transition
    modelView = inventoryWindow->getModelView();
    ASSERT_NE(modelView, nullptr) << "Model view should still exist after zone transition";
    EXPECT_TRUE(modelView->isReady()) << "Model view should be ready after zone transition";
    EXPECT_TRUE(modelView->hasCharacterModel()) << "Character model should be loaded after zone transition";
    EXPECT_TRUE(modelView->hasTextures()) << "Character model should have textures after zone transition";
    EXPECT_TRUE(modelView->isAnimating()) << "Character model should animate after zone transition";

    // Race should be the same
    EXPECT_EQ(modelView->getCurrentRaceId(), initialRaceId)
        << "Race ID should be the same after zone transition";

    std::cout << "Model view survives zone transition - all checks passed" << std::endl;
#endif
}

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
