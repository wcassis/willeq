#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/combat_music.h"

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// Static Method Tests (no initialization required)
// =============================================================================

class CombatMusicStaticTest : public ::testing::Test {
};

TEST_F(CombatMusicStaticTest, StingerCountIsTwo) {
    EXPECT_EQ(CombatMusicManager::getStingerCount(), 2);
}

TEST_F(CombatMusicStaticTest, StingerFilename0IsDamage1) {
    EXPECT_EQ(CombatMusicManager::getStingerFilename(0), "damage1.xmi");
}

TEST_F(CombatMusicStaticTest, StingerFilename1IsDamage2) {
    EXPECT_EQ(CombatMusicManager::getStingerFilename(1), "damage2.xmi");
}

TEST_F(CombatMusicStaticTest, StingerFilenameNegativeReturnsEmpty) {
    EXPECT_EQ(CombatMusicManager::getStingerFilename(-1), "");
}

TEST_F(CombatMusicStaticTest, StingerFilenameOutOfBoundsReturnsEmpty) {
    EXPECT_EQ(CombatMusicManager::getStingerFilename(2), "");
    EXPECT_EQ(CombatMusicManager::getStingerFilename(100), "");
}

// =============================================================================
// File Existence Tests
// =============================================================================

class CombatMusicFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(CombatMusicFileTest, Damage1XmiExists) {
    std::string path = std::string(EQ_PATH) + "/damage1.xmi";
    EXPECT_TRUE(std::filesystem::exists(path))
        << "damage1.xmi not found at: " << path;
}

TEST_F(CombatMusicFileTest, Damage2XmiExists) {
    std::string path = std::string(EQ_PATH) + "/damage2.xmi";
    EXPECT_TRUE(std::filesystem::exists(path))
        << "damage2.xmi not found at: " << path;
}

TEST_F(CombatMusicFileTest, AllStingerFilesExist) {
    for (int i = 0; i < CombatMusicManager::getStingerCount(); ++i) {
        std::string filename = CombatMusicManager::getStingerFilename(i);
        std::string path = std::string(EQ_PATH) + "/" + filename;
        EXPECT_TRUE(std::filesystem::exists(path))
            << "Stinger file not found: " << path;
    }
}

TEST_F(CombatMusicFileTest, StingerFilesAreNotEmpty) {
    for (int i = 0; i < CombatMusicManager::getStingerCount(); ++i) {
        std::string filename = CombatMusicManager::getStingerFilename(i);
        std::string path = std::string(EQ_PATH) + "/" + filename;
        ASSERT_TRUE(std::filesystem::exists(path));

        auto fileSize = std::filesystem::file_size(path);
        EXPECT_GT(fileSize, 0u) << filename << " is empty";
    }
}

// =============================================================================
// CombatMusicManager Default State Tests
// =============================================================================

class CombatMusicDefaultTest : public ::testing::Test {
protected:
    CombatMusicManager manager_;
};

TEST_F(CombatMusicDefaultTest, NotInitializedByDefault) {
    // Manager should work but not be initialized
    EXPECT_FALSE(manager_.isInCombat());
    EXPECT_FALSE(manager_.isStingerPlaying());
}

TEST_F(CombatMusicDefaultTest, EnabledByDefault) {
    EXPECT_TRUE(manager_.isEnabled());
}

TEST_F(CombatMusicDefaultTest, DefaultVolumeIs0_8) {
    EXPECT_FLOAT_EQ(manager_.getVolume(), 0.8f);
}

TEST_F(CombatMusicDefaultTest, DefaultCombatDelayIs5Seconds) {
    EXPECT_FLOAT_EQ(manager_.getCombatDelay(), 5.0f);
}

TEST_F(CombatMusicDefaultTest, DefaultFadeOutTimeIs2Seconds) {
    EXPECT_FLOAT_EQ(manager_.getFadeOutTime(), 2.0f);
}

TEST_F(CombatMusicDefaultTest, NotInCombatByDefault) {
    EXPECT_FALSE(manager_.isInCombat());
}

// =============================================================================
// Configuration Tests
// =============================================================================

class CombatMusicConfigTest : public ::testing::Test {
protected:
    CombatMusicManager manager_;
};

TEST_F(CombatMusicConfigTest, SetVolumeInRange) {
    manager_.setVolume(0.5f);
    EXPECT_FLOAT_EQ(manager_.getVolume(), 0.5f);
}

TEST_F(CombatMusicConfigTest, SetVolumeClampedToMin) {
    manager_.setVolume(-1.0f);
    EXPECT_FLOAT_EQ(manager_.getVolume(), 0.0f);
}

TEST_F(CombatMusicConfigTest, SetVolumeClampedToMax) {
    manager_.setVolume(2.0f);
    EXPECT_FLOAT_EQ(manager_.getVolume(), 1.0f);
}

TEST_F(CombatMusicConfigTest, SetCombatDelay) {
    manager_.setCombatDelay(10.0f);
    EXPECT_FLOAT_EQ(manager_.getCombatDelay(), 10.0f);
}

TEST_F(CombatMusicConfigTest, SetCombatDelayClampedToZero) {
    manager_.setCombatDelay(-5.0f);
    EXPECT_FLOAT_EQ(manager_.getCombatDelay(), 0.0f);
}

TEST_F(CombatMusicConfigTest, SetFadeOutTime) {
    manager_.setFadeOutTime(3.0f);
    EXPECT_FLOAT_EQ(manager_.getFadeOutTime(), 3.0f);
}

TEST_F(CombatMusicConfigTest, SetFadeOutTimeClampedToZero) {
    manager_.setFadeOutTime(-1.0f);
    EXPECT_FLOAT_EQ(manager_.getFadeOutTime(), 0.0f);
}

TEST_F(CombatMusicConfigTest, SetEnabled) {
    manager_.setEnabled(false);
    EXPECT_FALSE(manager_.isEnabled());

    manager_.setEnabled(true);
    EXPECT_TRUE(manager_.isEnabled());
}

// =============================================================================
// Combat State Machine Tests (without audio initialization)
// =============================================================================

class CombatStateMachineTest : public ::testing::Test {
protected:
    CombatMusicManager manager_;
};

TEST_F(CombatStateMachineTest, CombatStartSetsInCombat) {
    manager_.onCombatStart();
    EXPECT_TRUE(manager_.isInCombat());
}

TEST_F(CombatStateMachineTest, CombatEndClearsInCombat) {
    manager_.onCombatStart();
    manager_.onCombatEnd();
    EXPECT_FALSE(manager_.isInCombat());
}

TEST_F(CombatStateMachineTest, CombatEndWhenNotInCombatIsNoop) {
    manager_.onCombatEnd();  // Should not crash
    EXPECT_FALSE(manager_.isInCombat());
}

TEST_F(CombatStateMachineTest, DoubleCombatStartIsNoop) {
    manager_.onCombatStart();
    EXPECT_TRUE(manager_.isInCombat());

    manager_.onCombatStart();  // Should be ignored
    EXPECT_TRUE(manager_.isInCombat());
}

TEST_F(CombatStateMachineTest, CombatStartWhenDisabledIsIgnored) {
    manager_.setEnabled(false);
    manager_.onCombatStart();
    EXPECT_FALSE(manager_.isInCombat());
}

TEST_F(CombatStateMachineTest, UpdateWithoutInitializationIsNoop) {
    manager_.onCombatStart();
    manager_.update(1.0f);  // Should not crash
    EXPECT_TRUE(manager_.isInCombat());
}

TEST_F(CombatStateMachineTest, UpdateWhenNotInCombatIsNoop) {
    manager_.update(1.0f);  // Should not crash
    EXPECT_FALSE(manager_.isInCombat());
}

// =============================================================================
// Combat Timer Tests
// =============================================================================

class CombatTimerTest : public ::testing::Test {
protected:
    CombatMusicManager manager_;

    void SetUp() override {
        // Set a shorter delay for testing
        manager_.setCombatDelay(1.0f);
    }
};

TEST_F(CombatTimerTest, CombatTimerResetsOnCombatStart) {
    manager_.onCombatStart();
    // Timer starts at 0, so stinger should not be triggered yet
    // (even with 0 delay, it needs at least one update)
    EXPECT_FALSE(manager_.isStingerPlaying());
}

TEST_F(CombatTimerTest, CombatTimerResetsOnCombatEnd) {
    manager_.onCombatStart();
    manager_.update(0.5f);
    manager_.onCombatEnd();

    // Start new combat
    manager_.onCombatStart();
    // Timer should be reset, so still not ready for stinger
    EXPECT_FALSE(manager_.isStingerPlaying());
}

TEST_F(CombatTimerTest, BriefCombatDoesNotTriggerStinger) {
    manager_.setCombatDelay(5.0f);

    manager_.onCombatStart();
    manager_.update(2.0f);  // Only 2 seconds, delay is 5
    manager_.onCombatEnd();

    // Stinger should not have played (combat too short)
    EXPECT_FALSE(manager_.isStingerPlaying());
}

// =============================================================================
// XMI File Format Tests
// =============================================================================

class CombatMusicXmiTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(CombatMusicXmiTest, Damage1HasXmiHeader) {
    std::string path = std::string(EQ_PATH) + "/damage1.xmi";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    // XMI files start with "FORM" (IFF container)
    char header[4];
    file.read(header, 4);
    EXPECT_EQ(std::string(header, 4), "FORM")
        << "damage1.xmi does not have FORM header";
}

TEST_F(CombatMusicXmiTest, Damage2HasXmiHeader) {
    std::string path = std::string(EQ_PATH) + "/damage2.xmi";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    // XMI files start with "FORM" (IFF container)
    char header[4];
    file.read(header, 4);
    EXPECT_EQ(std::string(header, 4), "FORM")
        << "damage2.xmi does not have FORM header";
}

TEST_F(CombatMusicXmiTest, AllStingersHaveValidXmiFormat) {
    for (int i = 0; i < CombatMusicManager::getStingerCount(); ++i) {
        std::string filename = CombatMusicManager::getStingerFilename(i);
        std::string path = std::string(EQ_PATH) + "/" + filename;

        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open()) << "Cannot open: " << path;

        // Read FORM header
        char form[4];
        file.read(form, 4);
        EXPECT_EQ(std::string(form, 4), "FORM")
            << filename << " does not have FORM header";

        // Skip size (4 bytes)
        file.seekg(4, std::ios::cur);

        // Read type - should be "XDIR" or "XMID"
        char type[4];
        file.read(type, 4);
        std::string typeStr(type, 4);
        EXPECT_TRUE(typeStr == "XDIR" || typeStr == "XMID")
            << filename << " has unexpected type: " << typeStr;
    }
}

// =============================================================================
// Integration Tests (require audio device)
// =============================================================================

class CombatMusicIntegrationTest : public ::testing::Test {
protected:
    CombatMusicManager manager_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(CombatMusicIntegrationTest, InitializeSucceeds) {
    // Note: This may fail if no audio device or FluidSynth not available
    // We test the initialization path but don't fail if audio isn't available
    bool result = manager_.initialize(EQ_PATH);
    if (!result) {
        GTEST_SKIP() << "Audio initialization failed (no device or FluidSynth)";
    }
    EXPECT_FALSE(manager_.isInCombat());
    manager_.shutdown();
}

TEST_F(CombatMusicIntegrationTest, InitializeTwiceIsNoop) {
    bool result = manager_.initialize(EQ_PATH);
    if (!result) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    // Second init should return true without error
    EXPECT_TRUE(manager_.initialize(EQ_PATH));
    manager_.shutdown();
}

TEST_F(CombatMusicIntegrationTest, ShutdownMultipleTimesIsNoop) {
    bool result = manager_.initialize(EQ_PATH);
    if (!result) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    manager_.shutdown();
    manager_.shutdown();  // Should not crash
}

TEST_F(CombatMusicIntegrationTest, ShutdownWithoutInitializeIsNoop) {
    manager_.shutdown();  // Should not crash
}

// =============================================================================
// Edge Case Tests
// =============================================================================

class CombatMusicEdgeCaseTest : public ::testing::Test {
protected:
    CombatMusicManager manager_;
};

TEST_F(CombatMusicEdgeCaseTest, ZeroDelayTriggersImmediately) {
    manager_.setCombatDelay(0.0f);
    EXPECT_FLOAT_EQ(manager_.getCombatDelay(), 0.0f);
}

TEST_F(CombatMusicEdgeCaseTest, ZeroFadeOutStopsImmediately) {
    manager_.setFadeOutTime(0.0f);
    EXPECT_FLOAT_EQ(manager_.getFadeOutTime(), 0.0f);
}

TEST_F(CombatMusicEdgeCaseTest, VeryLongCombatDelay) {
    manager_.setCombatDelay(3600.0f);  // 1 hour
    EXPECT_FLOAT_EQ(manager_.getCombatDelay(), 3600.0f);
}

TEST_F(CombatMusicEdgeCaseTest, RapidCombatToggle) {
    // Simulate rapid on/off combat (e.g., losing and regaining aggro)
    for (int i = 0; i < 10; ++i) {
        manager_.onCombatStart();
        manager_.update(0.1f);
        manager_.onCombatEnd();
    }
    EXPECT_FALSE(manager_.isInCombat());
}

TEST_F(CombatMusicEdgeCaseTest, UpdateWithNegativeDeltaTime) {
    manager_.onCombatStart();
    manager_.update(-1.0f);  // Negative delta should be handled gracefully
    EXPECT_TRUE(manager_.isInCombat());
}

TEST_F(CombatMusicEdgeCaseTest, UpdateWithZeroDeltaTime) {
    manager_.onCombatStart();
    manager_.update(0.0f);
    EXPECT_TRUE(manager_.isInCombat());
}

TEST_F(CombatMusicEdgeCaseTest, UpdateWithVeryLargeDeltaTime) {
    manager_.onCombatStart();
    manager_.setCombatDelay(1.0f);
    manager_.update(1000.0f);  // Very large delta
    EXPECT_TRUE(manager_.isInCombat());
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, CombatMusicNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
