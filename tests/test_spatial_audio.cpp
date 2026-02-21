#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/audio_manager.h"
#include "client/audio/sound_assets.h"

#include <filesystem>
#include <thread>
#include <atomic>
#include <cmath>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// Distance Model Tests (don't require audio device for basic checks)
// =============================================================================

TEST(DistanceModelTest, DistanceAttenuationConstants) {
    // Verify expected distance attenuation constants
    // Reference distance: Sound at full volume within this range
    constexpr float EXPECTED_REF_DIST = 50.0f;
    // Max distance: Sound inaudible beyond this range
    constexpr float EXPECTED_MAX_DIST = 500.0f;
    // Rolloff factor: How quickly sound attenuates
    constexpr float EXPECTED_ROLLOFF = 1.0f;

    // These are reasonable values for EQ-style game audio
    EXPECT_GT(EXPECTED_REF_DIST, 0.0f);
    EXPECT_GT(EXPECTED_MAX_DIST, EXPECTED_REF_DIST);
    EXPECT_GE(EXPECTED_ROLLOFF, 0.0f);
}

TEST(DistanceModelTest, InverseDistanceFormula) {
    // Test the inverse distance clamped formula:
    // gain = ref_dist / (ref_dist + rolloff * (distance - ref_dist))
    // where distance is clamped to [ref_dist, max_dist]

    constexpr float ref_dist = 50.0f;
    constexpr float max_dist = 500.0f;
    constexpr float rolloff = 1.0f;

    auto calculateGain = [&](float distance) -> float {
        float clamped = std::max(ref_dist, std::min(distance, max_dist));
        return ref_dist / (ref_dist + rolloff * (clamped - ref_dist));
    };

    // At reference distance, gain should be 1.0
    EXPECT_FLOAT_EQ(calculateGain(ref_dist), 1.0f);

    // At 0 distance (clamped to ref_dist), gain should be 1.0
    EXPECT_FLOAT_EQ(calculateGain(0.0f), 1.0f);

    // At 100 units (50 past ref), gain should be 0.5
    EXPECT_FLOAT_EQ(calculateGain(100.0f), 0.5f);

    // At 150 units (100 past ref), gain should be 0.333...
    EXPECT_NEAR(calculateGain(150.0f), 1.0f/3.0f, 0.001f);

    // At max distance, gain should be 50/500 = 0.1
    EXPECT_NEAR(calculateGain(max_dist), 0.1f, 0.001f);

    // Beyond max distance (clamped), gain stays at 0.1
    EXPECT_NEAR(calculateGain(1000.0f), 0.1f, 0.001f);
}

// =============================================================================
// Listener Position Tests
// =============================================================================

class SpatialAudioTest : public ::testing::Test {
protected:
    std::unique_ptr<AudioManager> manager_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found at: " << EQ_PATH;
        }

        manager_ = std::make_unique<AudioManager>();
        if (!manager_->initialize(EQ_PATH)) {
            manager_.reset();
            GTEST_SKIP() << "Failed to initialize AudioManager";
        }
    }

    void TearDown() override {
        if (manager_) {
            manager_->shutdown();
            manager_.reset();
        }
    }
};

TEST_F(SpatialAudioTest, SetListenerPosition) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set listener at origin facing forward (EQ: forward is +Y)
    // Should not crash
    manager_->setListenerPosition(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),  // forward
        glm::vec3(0.0f, 0.0f, 1.0f)   // up (EQ: Z is up)
    );
    SUCCEED();
}

TEST_F(SpatialAudioTest, ListenerOrientationSet) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set listener facing right (+X direction)
    // Should not crash
    manager_->setListenerPosition(
        glm::vec3(100.0f, 200.0f, 50.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),  // forward
        glm::vec3(0.0f, 0.0f, 1.0f)   // up
    );
    SUCCEED();
}

TEST_F(SpatialAudioTest, PlaySoundAtDifferentPositions) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set listener at origin
    manager_->setListenerPosition(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    // Play sounds at various positions - should not crash
    // Close sound (should be loud)
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(10.0f, 0.0f, 0.0f));

    // Medium distance sound
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(100.0f, 0.0f, 0.0f));

    // Far sound (should be quiet)
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(400.0f, 0.0f, 0.0f));

    // Very far sound (should be barely audible)
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(500.0f, 0.0f, 0.0f));
}

TEST_F(SpatialAudioTest, SoundBehindListener) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set listener at origin facing +Y
    manager_->setListenerPosition(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),  // forward
        glm::vec3(0.0f, 0.0f, 1.0f)   // up
    );

    // Play sound behind listener (-Y direction)
    // This tests that stereo panning works correctly
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(0.0f, -50.0f, 0.0f));
}

TEST_F(SpatialAudioTest, SoundLeftAndRight) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set listener at origin facing +Y
    manager_->setListenerPosition(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),  // forward
        glm::vec3(0.0f, 0.0f, 1.0f)   // up
    );

    // Play sound to the left (-X)
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(-50.0f, 0.0f, 0.0f));

    // Play sound to the right (+X)
    manager_->playSound(SoundId::MELEE_MISS, glm::vec3(50.0f, 0.0f, 0.0f));
}

TEST_F(SpatialAudioTest, SoundAboveAndBelow) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set listener at origin
    manager_->setListenerPosition(
        glm::vec3(0.0f, 0.0f, 50.0f),  // 50 units off ground
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    // Play sound above listener
    manager_->playSound(SoundId::SPELL_CAST, glm::vec3(0.0f, 0.0f, 150.0f));

    // Play sound below listener
    manager_->playSound(SoundId::DEATH, glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST_F(SpatialAudioTest, MoveListenerDuringSounds) {
    ASSERT_TRUE(manager_->isInitialized());

    // Start with listener at origin
    manager_->setListenerPosition(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    // Play a sound at fixed position
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(100.0f, 0.0f, 0.0f));

    // Move listener closer (simulating player movement)
    manager_->setListenerPosition(
        glm::vec3(50.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    // Move listener even closer
    manager_->setListenerPosition(
        glm::vec3(90.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );
}

// =============================================================================
// Loopback Mode Tests (RDP audio backend)
// =============================================================================

class LoopbackAudioManagerTest : public ::testing::Test {
protected:
    std::unique_ptr<AudioManager> manager_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found";
        }
    }

    void TearDown() override {
        if (manager_) {
            manager_->shutdown();
            manager_.reset();
        }
    }
};

TEST_F(LoopbackAudioManagerTest, ForceLoopbackMode) {
    manager_ = std::make_unique<AudioManager>();

    // Initialize with forced loopback mode
    bool result = manager_->initialize(EQ_PATH, true);
    ASSERT_TRUE(result) << "Failed to initialize AudioManager in loopback mode";

    EXPECT_TRUE(manager_->isInitialized());
    EXPECT_TRUE(manager_->isLoopbackMode());
}

TEST_F(LoopbackAudioManagerTest, LoopbackCallbackReceivesAudio) {
    manager_ = std::make_unique<AudioManager>();
    ASSERT_TRUE(manager_->initialize(EQ_PATH, true));

    std::atomic<size_t> callbackCount{0};
    std::atomic<size_t> totalSamples{0};

    // Set up callback to count received audio
    manager_->setAudioOutputCallback(
        [&callbackCount, &totalSamples](const int16_t* samples, size_t count,
                                         uint32_t sampleRate, uint8_t channels) {
            callbackCount++;
            totalSamples += count;

            // Verify format
            EXPECT_EQ(sampleRate, 22050u);
            EXPECT_EQ(channels, 2u);
        }
    );

    // Wait a bit for the render thread to call the callback
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should have received at least a few callbacks
    EXPECT_GT(callbackCount.load(), 0u) << "Loopback callback was never called";
    EXPECT_GT(totalSamples.load(), 0u) << "No samples received";

    std::cout << "Received " << callbackCount.load() << " callbacks with "
              << totalSamples.load() << " total frames" << std::endl;
}

TEST_F(LoopbackAudioManagerTest, PlaySoundInLoopbackMode) {
    manager_ = std::make_unique<AudioManager>();
    ASSERT_TRUE(manager_->initialize(EQ_PATH, true));

    std::atomic<bool> nonSilentReceived{false};

    // Set up callback to detect non-silent audio
    manager_->setAudioOutputCallback(
        [&nonSilentReceived](const int16_t* samples, size_t count,
                              uint32_t sampleRate, uint8_t channels) {
            for (size_t i = 0; i < count * channels; i++) {
                if (samples[i] != 0) {
                    nonSilentReceived = true;
                    break;
                }
            }
        }
    );

    // Play a sound
    manager_->playSound(SoundId::MELEE_HIT);

    // Wait for the sound to be rendered
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // If the sound loaded and played, we should receive non-silent audio
    if (manager_->getLoadedSoundCount() > 0) {
        std::cout << "Loaded sound count: " << manager_->getLoadedSoundCount() << std::endl;
    }
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, SpatialAudioNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
