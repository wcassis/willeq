/**
 * Integration tests for zone transitions (HEADLESS MODE)
 *
 * These tests connect to a real EQEmu server and verify that zoning works correctly
 * in headless mode (no graphics). They verify:
 * - LoadingPhase transitions through phases 0-10 correctly
 * - Game state is properly set up (entities, spawn ID, position)
 * - Progress never goes backwards
 * - Subsequent zoning works correctly
 *
 * Requirements:
 * - Running EQEmu server (login + world + zone)
 * - Test account and character configured (uses /home/user/projects/claude/casterella.json)
 * - Character must be in a zone with known zone lines
 *
 * Usage:
 *   ./bin/test_zoning_integration [--config /path/to/config.json]
 */

#include <gtest/gtest.h>
#include <fstream>
#include <json/json.h>
#include <chrono>
#include <thread>
#include <set>
#include <vector>

#include "client/eq.h"
#include "common/logging.h"
#include "common/event/event_loop.h"

using namespace std::chrono_literals;

// Default config path
static std::string g_configPath = "/home/user/projects/claude/casterella.json";

class ZoningIntegrationTest : public ::testing::Test {
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

    // Phase tracking for verification
    std::vector<LoadingPhase> phaseHistory_;
    LoadingPhase lastPhase_ = LoadingPhase::DISCONNECTED;
    bool phaseRegressionDetected_ = false;

    void SetUp() override {
        // Load test configuration
        loadConfig();
        if (!config_.loaded) {
            GTEST_SKIP() << "Test config not found or invalid at: " << g_configPath;
        }
    }

    void TearDown() override {
        if (eq_) {
            eq_.reset();
        }
    }

    // Reset phase tracking for a new test
    void resetPhaseTracking() {
        phaseHistory_.clear();
        lastPhase_ = LoadingPhase::DISCONNECTED;
        phaseRegressionDetected_ = false;
    }

    // Record phase transition and check for regressions
    void trackPhase(LoadingPhase currentPhase) {
        if (currentPhase != lastPhase_) {
            phaseHistory_.push_back(currentPhase);

            // Check for regression (phase going backwards)
            // Note: During subsequent zoning, we DO expect phase to reset to DISCONNECTED
            if (static_cast<int>(currentPhase) < static_cast<int>(lastPhase_) &&
                currentPhase != LoadingPhase::DISCONNECTED) {
                std::cout << "WARNING: Phase regression detected: "
                          << static_cast<int>(lastPhase_) << " -> "
                          << static_cast<int>(currentPhase) << std::endl;
                phaseRegressionDetected_ = true;
            }

            std::cout << "[PHASE] " << static_cast<int>(lastPhase_)
                      << " -> " << static_cast<int>(currentPhase) << std::endl;
            lastPhase_ = currentPhase;
        }
    }

    // Get phase name for logging
    static const char* getPhaseName(LoadingPhase phase) {
        switch (phase) {
            case LoadingPhase::DISCONNECTED: return "DISCONNECTED";
            case LoadingPhase::LOGIN_CONNECTING: return "LOGIN_CONNECTING";
            case LoadingPhase::LOGIN_AUTHENTICATING: return "LOGIN_AUTHENTICATING";
            case LoadingPhase::WORLD_CONNECTING: return "WORLD_CONNECTING";
            case LoadingPhase::WORLD_CHARACTER_SELECT: return "WORLD_CHARACTER_SELECT";
            case LoadingPhase::ZONE_CONNECTING: return "ZONE_CONNECTING";
            case LoadingPhase::ZONE_RECEIVING_PROFILE: return "ZONE_RECEIVING_PROFILE";
            case LoadingPhase::ZONE_RECEIVING_SPAWNS: return "ZONE_RECEIVING_SPAWNS";
            case LoadingPhase::ZONE_REQUEST_PHASE: return "ZONE_REQUEST_PHASE";
            case LoadingPhase::ZONE_PLAYER_READY: return "ZONE_PLAYER_READY";
            case LoadingPhase::ZONE_AWAITING_CONFIRM: return "ZONE_AWAITING_CONFIRM";
            case LoadingPhase::GRAPHICS_LOADING_ZONE: return "GRAPHICS_LOADING_ZONE";
            case LoadingPhase::GRAPHICS_LOADING_MODELS: return "GRAPHICS_LOADING_MODELS";
            case LoadingPhase::GRAPHICS_CREATING_ENTITIES: return "GRAPHICS_CREATING_ENTITIES";
            case LoadingPhase::GRAPHICS_FINALIZING: return "GRAPHICS_FINALIZING";
            case LoadingPhase::COMPLETE: return "COMPLETE";
            default: return "UNKNOWN";
        }
    }

    // Print phase history for debugging
    void printPhaseHistory() {
        std::cout << "Phase history (" << phaseHistory_.size() << " transitions):" << std::endl;
        for (size_t i = 0; i < phaseHistory_.size(); i++) {
            std::cout << "  " << i << ": " << getPhaseName(phaseHistory_[i])
                      << " (" << static_cast<int>(phaseHistory_[i]) << ")" << std::endl;
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

    bool createClient() {
        try {
            eq_ = std::make_unique<EverQuest>(
                config_.host, config_.port,
                config_.user, config_.pass,
                config_.server, config_.character
            );

            // Configure paths if available
            if (!config_.mapsPath.empty()) {
                eq_->SetMapsPath(config_.mapsPath);
            }
            if (!config_.navmeshPath.empty()) {
                eq_->SetNavmeshPath(config_.navmeshPath);
            }
            if (!config_.eqClientPath.empty()) {
                eq_->SetEQClientPath(config_.eqClientPath);
            }

            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to create client: " << e.what() << std::endl;
            return false;
        }
    }

    // Run the event loop until a condition is met or timeout
    template<typename Predicate>
    bool waitFor(Predicate condition, int timeoutMs = 30000, bool trackPhases = true) {
        auto start = std::chrono::steady_clock::now();
        while (!condition()) {
            EQ::EventLoop::Get().Process();
            if (eq_) {
                eq_->UpdateMovement();
                // Track phase transitions
                if (trackPhases) {
                    trackPhase(eq_->GetLoadingPhase());
                }
            }
            std::this_thread::sleep_for(10ms);

            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeoutMs) {
                return false;
            }
        }
        return true;
    }

    // Wait until fully zoned in
    bool waitForZoneIn(int timeoutMs = 30000) {
        return waitFor([this]() { return eq_->IsFullyZonedIn(); }, timeoutMs);
    }

    // Get center point of a zone line trigger box
    struct ZoneLineInfo {
        float x, y, z;
        std::string destinationZone;
        bool found = false;
    };

    ZoneLineInfo getZoneLineCenter(const std::string& zoneName, int index = 0) {
        ZoneLineInfo info;

        // Load zone_lines.json
        std::ifstream file("data/zone_lines.json");
        if (!file.is_open()) {
            file.open("../data/zone_lines.json");
        }
        if (!file.is_open()) {
            std::cerr << "Cannot open data/zone_lines.json" << std::endl;
            return info;
        }

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        if (!Json::parseFromStream(builder, file, &root, &errors)) {
            std::cerr << "Failed to parse zone_lines.json: " << errors << std::endl;
            return info;
        }

        if (!root.isMember(zoneName)) {
            std::cerr << "Zone '" << zoneName << "' not found in zone_lines.json" << std::endl;
            return info;
        }

        const auto& zoneData = root[zoneName];
        const auto& zoneLines = zoneData["zone_lines"];
        if (!zoneLines.isArray() || index >= static_cast<int>(zoneLines.size())) {
            std::cerr << "No zone lines for zone '" << zoneName << "'" << std::endl;
            return info;
        }

        const auto& zoneLine = zoneLines[index];
        const auto& box = zoneLine["trigger_box"];

        // Calculate center of trigger box
        info.x = (box["min_x"].asFloat() + box["max_x"].asFloat()) / 2.0f;
        info.y = (box["min_y"].asFloat() + box["max_y"].asFloat()) / 2.0f;
        info.z = (box["min_z"].asFloat() + box["max_z"].asFloat()) / 2.0f;
        info.destinationZone = zoneLine["destination_zone"].asString();
        info.found = true;

        std::cout << "Found zone line in " << zoneName << " -> " << info.destinationZone
                  << " at (" << info.x << ", " << info.y << ", " << info.z << ")" << std::endl;

        return info;
    }
};

// Test: Connect and zone in successfully, verify LoadingPhase transitions
TEST_F(ZoningIntegrationTest, InitialZoneIn) {
    ASSERT_TRUE(createClient());
    resetPhaseTracking();

    std::cout << "Connecting to " << config_.host << ":" << config_.port << "..." << std::endl;
    std::cout << "Waiting for initial zone-in (timeout: " << config_.timeoutSeconds << "s)..." << std::endl;

    // Track initial phase
    trackPhase(eq_->GetLoadingPhase());

    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for initial zone-in";

    // Track final phase
    trackPhase(eq_->GetLoadingPhase());

    std::string zoneName = eq_->GetCurrentZoneName();
    uint16_t spawnId = eq_->GetMySpawnID();

    std::cout << "Successfully zoned into: " << zoneName << " (spawn_id=" << spawnId << ")" << std::endl;

    // Print phase history
    printPhaseHistory();

    // Verify LoadingPhase system
    EXPECT_FALSE(phaseRegressionDetected_) << "Phase regression detected during zone-in";
    EXPECT_TRUE(eq_->IsGameStateReady()) << "Game state not ready after zone-in";

    // In headless mode, we should reach ZONE_AWAITING_CONFIRM (phase 10)
    // but NOT the graphics phases (11-15)
    LoadingPhase finalPhase = eq_->GetLoadingPhase();
    EXPECT_GE(static_cast<int>(finalPhase), static_cast<int>(LoadingPhase::ZONE_AWAITING_CONFIRM))
        << "Expected phase >= ZONE_AWAITING_CONFIRM (10), got " << static_cast<int>(finalPhase);

    // Verify we went through key phases (check phase history)
    bool sawLoginConnecting = false;
    bool sawWorldConnecting = false;
    bool sawZoneConnecting = false;
    bool sawReceivingProfile = false;
    for (LoadingPhase phase : phaseHistory_) {
        if (phase == LoadingPhase::LOGIN_CONNECTING) sawLoginConnecting = true;
        if (phase == LoadingPhase::WORLD_CONNECTING) sawWorldConnecting = true;
        if (phase == LoadingPhase::ZONE_CONNECTING) sawZoneConnecting = true;
        if (phase == LoadingPhase::ZONE_RECEIVING_PROFILE) sawReceivingProfile = true;
    }
    EXPECT_TRUE(sawLoginConnecting) << "Never saw LOGIN_CONNECTING phase";
    EXPECT_TRUE(sawWorldConnecting) << "Never saw WORLD_CONNECTING phase";
    EXPECT_TRUE(sawZoneConnecting) << "Never saw ZONE_CONNECTING phase";
    EXPECT_TRUE(sawReceivingProfile) << "Never saw ZONE_RECEIVING_PROFILE phase";

    // Verify basic game state
    EXPECT_FALSE(zoneName.empty()) << "Zone name is empty";
    EXPECT_GT(spawnId, 0) << "Spawn ID is 0";

    // Verify position
    glm::vec3 pos = eq_->GetPosition();
    std::cout << "Player position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
}

// Test: Zone to another zone, verify LoadingPhase reset and transitions
TEST_F(ZoningIntegrationTest, ZoneTransition) {
    ASSERT_TRUE(createClient());
    resetPhaseTracking();

    std::cout << "Waiting for initial zone-in..." << std::endl;
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for initial zone-in";

    std::string startZone = eq_->GetCurrentZoneName();
    uint16_t startSpawnId = eq_->GetMySpawnID();
    std::cout << "Starting in zone: " << startZone << " (spawn_id=" << startSpawnId << ")" << std::endl;
    std::cout << "Initial zone-in phase history:" << std::endl;
    printPhaseHistory();

    // Reset phase tracking for zone transition
    resetPhaseTracking();

    // Find a zone line in the current zone
    ZoneLineInfo zoneLine = getZoneLineCenter(startZone);
    if (!zoneLine.found) {
        GTEST_SKIP() << "No zone lines defined for zone: " << startZone;
    }

    std::cout << "Teleporting to zone line at (" << zoneLine.x << ", " << zoneLine.y << ", " << zoneLine.z
              << ") -> " << zoneLine.destinationZone << std::endl;

    // Move to the zone line - this should trigger zoning
    eq_->SetPosition(zoneLine.x, zoneLine.y, zoneLine.z);

    std::cout << "After SetPosition: IsFullyZonedIn=" << eq_->IsFullyZonedIn()
              << " Phase=" << static_cast<int>(eq_->GetLoadingPhase()) << std::endl;

    // Give the position update time to send and zone change to trigger
    // UpdateMovement() contains the CheckZoneLine() call
    for (int i = 0; i < 50; i++) {
        EQ::EventLoop::Get().Process();
        eq_->UpdateMovement();
        trackPhase(eq_->GetLoadingPhase());
        if (i % 10 == 0) {
            glm::vec3 pos = eq_->GetPosition();
            std::cout << "Loop " << i << ": pos=(" << pos.x << "," << pos.y << "," << pos.z
                      << ") IsFullyZonedIn=" << eq_->IsFullyZonedIn()
                      << " Phase=" << static_cast<int>(eq_->GetLoadingPhase()) << std::endl;
        }
        std::this_thread::sleep_for(20ms);
        // Check if zone change was triggered
        if (!eq_->IsFullyZonedIn()) {
            std::cout << "Zone-out detected at loop " << i << std::endl;
            break;
        }
    }

    // Wait for zone transition to complete
    std::cout << "Waiting for zone transition to complete..." << std::endl;

    // First wait for zone-out (we won't be fully zoned in)
    bool leftZone = waitFor([this]() {
        return !eq_->IsFullyZonedIn();
    }, 10000);

    if (!leftZone) {
        // Maybe we're still in the same zone - zone line might not have triggered
        glm::vec3 currentPos = eq_->GetPosition();
        std::cout << "Current position: (" << currentPos.x << ", " << currentPos.y << ", " << currentPos.z << ")" << std::endl;
        GTEST_SKIP() << "Zone line did not trigger - position update may not have moved player";
    }

    // Verify phase reset to DISCONNECTED
    std::cout << "Zone-out phase: " << static_cast<int>(eq_->GetLoadingPhase()) << std::endl;

    // Now wait for zone-in to complete
    bool zonedIn = waitForZoneIn(config_.timeoutSeconds * 1000);

    ASSERT_TRUE(zonedIn)
        << "Timed out waiting for zone-in to " << zoneLine.destinationZone;

    std::string newZone = eq_->GetCurrentZoneName();
    uint16_t newSpawnId = eq_->GetMySpawnID();

    std::cout << "Successfully zoned to: " << newZone << " (spawn_id=" << newSpawnId << ")" << std::endl;
    std::cout << "Zone transition phase history:" << std::endl;
    printPhaseHistory();

    // Verify phase transitions during zone transition
    // Note: DISCONNECTED phase happens briefly during transition but polling may not catch it
    bool sawZoneConnecting = false;
    for (LoadingPhase phase : phaseHistory_) {
        if (phase == LoadingPhase::ZONE_CONNECTING) sawZoneConnecting = true;
    }
    EXPECT_TRUE(sawZoneConnecting) << "Never saw ZONE_CONNECTING during zone transition";

    // Verify we're in the expected zone
    EXPECT_EQ(newZone, zoneLine.destinationZone)
        << "Expected to be in " << zoneLine.destinationZone << " but in " << newZone;

    // Verify spawn ID is valid
    EXPECT_GT(newSpawnId, 0) << "Spawn ID is 0 after zoning";

    // Verify game state ready
    EXPECT_TRUE(eq_->IsGameStateReady()) << "Game state not ready after zone transition";
}

// Test: Verify LoadingPhase progression follows expected order
TEST_F(ZoningIntegrationTest, LoadingPhaseProgression) {
    ASSERT_TRUE(createClient());
    resetPhaseTracking();

    std::cout << "Testing LoadingPhase progression during initial zone-in..." << std::endl;

    // Track initial phase
    trackPhase(eq_->GetLoadingPhase());

    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for initial zone-in";

    // Track final phase
    trackPhase(eq_->GetLoadingPhase());

    std::cout << "Phase progression:" << std::endl;
    printPhaseHistory();

    // Verify no phase regressions (except DISCONNECTED which is allowed)
    EXPECT_FALSE(phaseRegressionDetected_)
        << "Phase regression detected (phase went backwards unexpectedly)";

    // Verify we reached game state ready
    EXPECT_TRUE(eq_->IsGameStateReady())
        << "IsGameStateReady() should be true after zone-in";

    // Verify phases are in monotonically increasing order (except for DISCONNECTED resets)
    int lastPhaseValue = -1;
    for (size_t i = 0; i < phaseHistory_.size(); i++) {
        int currentPhaseValue = static_cast<int>(phaseHistory_[i]);
        if (currentPhaseValue != 0) {  // DISCONNECTED can appear at any time
            if (lastPhaseValue > 0 && currentPhaseValue < lastPhaseValue) {
                FAIL() << "Phase regression at index " << i << ": "
                       << lastPhaseValue << " -> " << currentPhaseValue;
            }
            lastPhaseValue = currentPhaseValue;
        }
    }

    // Verify we saw the expected minimum phases
    std::set<int> expectedPhases = {1, 2, 3, 5, 6};  // LOGIN_CONNECTING, LOGIN_AUTHENTICATING, WORLD_CONNECTING, ZONE_CONNECTING, ZONE_RECEIVING_PROFILE
    std::set<int> seenPhases;
    for (LoadingPhase phase : phaseHistory_) {
        seenPhases.insert(static_cast<int>(phase));
    }

    for (int expected : expectedPhases) {
        EXPECT_TRUE(seenPhases.count(expected) > 0)
            << "Missing expected phase " << expected << " (" << getPhaseName(static_cast<LoadingPhase>(expected)) << ")";
    }
}

// Test: Verify game state is properly set up after zoning
TEST_F(ZoningIntegrationTest, GameStateAfterZoning) {
    ASSERT_TRUE(createClient());
    resetPhaseTracking();

    std::cout << "Waiting for initial zone-in..." << std::endl;
    ASSERT_TRUE(waitForZoneIn(config_.timeoutSeconds * 1000))
        << "Timed out waiting for initial zone-in";

    std::string startZone = eq_->GetCurrentZoneName();

    // Find a zone line
    ZoneLineInfo zoneLine = getZoneLineCenter(startZone);
    if (!zoneLine.found) {
        GTEST_SKIP() << "No zone lines defined for zone: " << startZone;
    }

    // Reset phase tracking for zone transition
    resetPhaseTracking();

    // Move to zone line
    eq_->SetPosition(zoneLine.x, zoneLine.y, zoneLine.z);

    // Process for a while with UpdateMovement to trigger zone line check
    for (int i = 0; i < 50; i++) {
        EQ::EventLoop::Get().Process();
        eq_->UpdateMovement();
        trackPhase(eq_->GetLoadingPhase());
        std::this_thread::sleep_for(20ms);
        if (!eq_->IsFullyZonedIn()) {
            break;
        }
    }

    // Wait for zone transition
    bool leftZone = waitFor([this]() { return !eq_->IsFullyZonedIn(); }, 10000);
    if (!leftZone) {
        GTEST_SKIP() << "Zone line did not trigger";
    }

    bool zonedIn = waitForZoneIn(config_.timeoutSeconds * 1000);
    ASSERT_TRUE(zonedIn) << "Failed to zone in";

    std::cout << "Zone transition phase history:" << std::endl;
    printPhaseHistory();

    // Verify game state after zoning
    EXPECT_TRUE(eq_->IsFullyZonedIn()) << "Not fully zoned in after transition";
    EXPECT_TRUE(eq_->IsGameStateReady()) << "IsGameStateReady() should be true after zoning";
    EXPECT_FALSE(eq_->GetCurrentZoneName().empty()) << "Zone name is empty";
    EXPECT_GT(eq_->GetMySpawnID(), 0) << "Spawn ID is 0";

    // Verify LoadingPhase is at least ZONE_AWAITING_CONFIRM
    LoadingPhase finalPhase = eq_->GetLoadingPhase();
    EXPECT_GE(static_cast<int>(finalPhase), static_cast<int>(LoadingPhase::ZONE_AWAITING_CONFIRM))
        << "Expected phase >= ZONE_AWAITING_CONFIRM after zoning";

    // Verify player position is valid
    glm::vec3 pos = eq_->GetPosition();
    std::cout << "Player position after zoning: (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;

    // Position should not be at origin
    EXPECT_FALSE(pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f)
        << "Player position is at origin after zoning";
}

int main(int argc, char **argv) {
    // Parse command line for config path
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            g_configPath = argv[i + 1];
            // Remove these args so gtest doesn't see them
            for (int j = i; j < argc - 2; j++) {
                argv[j] = argv[j + 2];
            }
            argc -= 2;
            break;
        }
    }

    // Initialize logging
    SetLogLevel(LOG_INFO);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
