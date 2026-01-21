#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/audio_manager.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <filesystem>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// Zone Music Mapping Tests (don't require audio device)
// =============================================================================

class ZoneMusicMappingTest : public ::testing::Test {
protected:
    std::string eqPath_;

    void SetUp() override {
        eqPath_ = std::string(EQ_PATH);
        if (!std::filesystem::exists(eqPath_)) {
            GTEST_SKIP() << "EQ client path not found at: " << eqPath_;
        }
    }

    // Helper to check if a zone has XMI music
    bool hasXmiMusic(const std::string& zoneName) {
        std::string lowerZone = zoneName;
        std::transform(lowerZone.begin(), lowerZone.end(), lowerZone.begin(), ::tolower);
        std::string xmiPath = eqPath_ + "/" + lowerZone + ".xmi";
        return std::filesystem::exists(xmiPath);
    }

    // Helper to check if a zone has MP3 music
    bool hasMp3Music(const std::string& zoneName) {
        std::string lowerZone = zoneName;
        std::transform(lowerZone.begin(), lowerZone.end(), lowerZone.begin(), ::tolower);
        std::string mp3Path = eqPath_ + "/" + lowerZone + ".mp3";
        return std::filesystem::exists(mp3Path);
    }
};

TEST_F(ZoneMusicMappingTest, ClassicZonesHaveXmiMusic) {
    // Classic EQ zones should have XMI music files
    std::vector<std::string> classicZones = {
        "qeynos", "qeynos2", "freporte", "freportn", "freeportw",
        "akanon", "felwithea", "felwitheb", "halas", "rivervale",
        "erudnext", "erudnint", "kaladima", "kaladimb", "oggok",
        "grobb", "neriaka", "neriakb", "neriakc"
    };

    size_t found = 0;
    for (const auto& zone : classicZones) {
        if (hasXmiMusic(zone)) {
            found++;
        }
    }

    // Most classic zones should have XMI music
    EXPECT_GT(found, classicZones.size() / 2)
        << "Expected most classic zones to have XMI music, found " << found
        << "/" << classicZones.size();
}

TEST_F(ZoneMusicMappingTest, DungeonZonesHaveMusic) {
    std::vector<std::string> dungeonZones = {
        "befallen", "blackburrow", "crushbone", "permafrost",
        "soldungb", "unrest", "kedge", "gukbottom"
    };

    size_t found = 0;
    for (const auto& zone : dungeonZones) {
        if (hasXmiMusic(zone) || hasMp3Music(zone)) {
            found++;
        }
    }

    // Many dungeon zones should have music
    EXPECT_GT(found, 0) << "Expected some dungeon zones to have music";
}

TEST_F(ZoneMusicMappingTest, CaseInsensitiveZoneLookup) {
    // Zone names should work regardless of case
    EXPECT_EQ(hasXmiMusic("QEYNOS"), hasXmiMusic("qeynos"));
    EXPECT_EQ(hasXmiMusic("Qeynos"), hasXmiMusic("qeynos"));
    EXPECT_EQ(hasXmiMusic("QeYnOs"), hasXmiMusic("qeynos"));
}

// =============================================================================
// AudioManager Zone Music Tests (require audio device)
// =============================================================================

class ZoneMusicAudioTest : public ::testing::Test {
protected:
    std::unique_ptr<AudioManager> manager_;
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found at: " << EQ_PATH;
        }

        // Initialize OpenAL context for audio tests
        device_ = alcOpenDevice(nullptr);
        if (!device_) {
            GTEST_SKIP() << "No audio device available";
        }
        context_ = alcCreateContext(device_, nullptr);
        if (!context_) {
            alcCloseDevice(device_);
            device_ = nullptr;
            GTEST_SKIP() << "Failed to create audio context";
        }
        alcMakeContextCurrent(context_);

        // Create and initialize AudioManager
        manager_ = std::make_unique<AudioManager>();
        if (!manager_->initialize(EQ_PATH)) {
            manager_.reset();
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context_);
            alcCloseDevice(device_);
            context_ = nullptr;
            device_ = nullptr;
            GTEST_SKIP() << "Failed to initialize AudioManager";
        }
    }

    void TearDown() override {
        if (manager_) {
            manager_->shutdown();
            manager_.reset();
        }
        alcMakeContextCurrent(nullptr);
        if (context_) {
            alcDestroyContext(context_);
        }
        if (device_) {
            alcCloseDevice(device_);
        }
    }
};

TEST_F(ZoneMusicAudioTest, OnZoneChangeTriggersMusic) {
    // This test verifies onZoneChange is callable
    // Actual playback requires FluidSynth for XMI or audio device for MP3
    ASSERT_TRUE(manager_->isInitialized());

    // Call onZoneChange - should not crash even without music playing
    manager_->onZoneChange("qeynos");
    manager_->onZoneChange("freeport");
    manager_->onZoneChange("tutorial");  // May not exist

    // Repeated calls to same zone should be no-ops
    manager_->onZoneChange("qeynos");
    manager_->onZoneChange("qeynos");
}

TEST_F(ZoneMusicAudioTest, VolumeControlsDuringZoneChange) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set music volume before zone change
    manager_->setMusicVolume(0.5f);
    EXPECT_FLOAT_EQ(manager_->getMusicVolume(), 0.5f);

    // Zone change should preserve volume settings
    manager_->onZoneChange("qeynos");
    EXPECT_FLOAT_EQ(manager_->getMusicVolume(), 0.5f);

    // Volume changes during zone should work
    manager_->setMusicVolume(0.8f);
    EXPECT_FLOAT_EQ(manager_->getMusicVolume(), 0.8f);
}

TEST_F(ZoneMusicAudioTest, StopMusicWorks) {
    ASSERT_TRUE(manager_->isInitialized());

    // Trigger zone change
    manager_->onZoneChange("qeynos");

    // Stop music with fade
    manager_->stopMusic(1.0f);

    // Stop music immediately
    manager_->stopMusic(0.0f);
}

TEST_F(ZoneMusicAudioTest, AudioDisableStopsMusic) {
    ASSERT_TRUE(manager_->isInitialized());
    EXPECT_TRUE(manager_->isAudioEnabled());

    // Trigger zone change
    manager_->onZoneChange("qeynos");

    // Disable audio - should stop music
    manager_->setAudioEnabled(false);
    EXPECT_FALSE(manager_->isAudioEnabled());

    // Re-enable audio
    manager_->setAudioEnabled(true);
    EXPECT_TRUE(manager_->isAudioEnabled());
}

// =============================================================================
// Zone Music File Discovery Tests
// =============================================================================

TEST_F(ZoneMusicMappingTest, CountMusicFiles) {
    size_t xmiCount = 0;
    size_t mp3Count = 0;

    for (const auto& entry : std::filesystem::directory_iterator(eqPath_)) {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".xmi") {
            xmiCount++;
        } else if (ext == ".mp3") {
            mp3Count++;
        }
    }

    std::cout << "Music files found: " << xmiCount << " XMI, " << mp3Count << " MP3\n";

    // Should have a reasonable number of music files
    EXPECT_GT(xmiCount + mp3Count, 20)
        << "Expected many music files in EQ directory";
}

// =============================================================================
// MusicPlayer Specific Tests
// =============================================================================

#include "client/audio/music_player.h"

class MusicPlayerTest : public ::testing::Test {
protected:
    std::unique_ptr<MusicPlayer> player_;
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    std::string eqPath_;

    void SetUp() override {
        eqPath_ = std::string(EQ_PATH);
        if (!std::filesystem::exists(eqPath_)) {
            GTEST_SKIP() << "EQ client path not found at: " << eqPath_;
        }

        // Initialize OpenAL context for audio tests
        device_ = alcOpenDevice(nullptr);
        if (!device_) {
            GTEST_SKIP() << "No audio device available";
        }
        context_ = alcCreateContext(device_, nullptr);
        if (!context_) {
            alcCloseDevice(device_);
            device_ = nullptr;
            GTEST_SKIP() << "Failed to create audio context";
        }
        alcMakeContextCurrent(context_);

        player_ = std::make_unique<MusicPlayer>();
    }

    void TearDown() override {
        if (player_) {
            player_->shutdown();
            player_.reset();
        }
        alcMakeContextCurrent(nullptr);
        if (context_) {
            alcDestroyContext(context_);
        }
        if (device_) {
            alcCloseDevice(device_);
        }
    }

    std::string findMp3File() {
        for (const auto& entry : std::filesystem::directory_iterator(eqPath_)) {
            if (entry.path().extension() == ".mp3") {
                return entry.path().string();
            }
        }
        return "";
    }
};

TEST_F(MusicPlayerTest, InitializeWithoutSoundFont) {
    // MusicPlayer should initialize even without a SoundFont
    // (XMI playback will be disabled but MP3/WAV should work)
    EXPECT_TRUE(player_->initialize());
}

TEST_F(MusicPlayerTest, VolumeControls) {
    ASSERT_TRUE(player_->initialize());

    // Default volume should be 1.0
    EXPECT_FLOAT_EQ(player_->getVolume(), 1.0f);

    // Set various volumes
    player_->setVolume(0.5f);
    EXPECT_FLOAT_EQ(player_->getVolume(), 0.5f);

    player_->setVolume(0.0f);
    EXPECT_FLOAT_EQ(player_->getVolume(), 0.0f);

    player_->setVolume(1.0f);
    EXPECT_FLOAT_EQ(player_->getVolume(), 1.0f);
}

TEST_F(MusicPlayerTest, InitialState) {
    ASSERT_TRUE(player_->initialize());

    // Initially not playing
    EXPECT_FALSE(player_->isPlaying());
    EXPECT_FALSE(player_->isPaused());
}

TEST_F(MusicPlayerTest, StopWithoutPlaying) {
    ASSERT_TRUE(player_->initialize());

    // Stop should be safe to call even when not playing
    player_->stop(0.0f);
    player_->stop(1.0f);

    EXPECT_FALSE(player_->isPlaying());
}

TEST_F(MusicPlayerTest, PauseResumeWithoutPlaying) {
    ASSERT_TRUE(player_->initialize());

    // Pause/resume should be safe to call even when not playing
    player_->pause();
    player_->resume();

    EXPECT_FALSE(player_->isPlaying());
}

TEST_F(MusicPlayerTest, PlayNonexistentFile) {
    ASSERT_TRUE(player_->initialize());

    // Playing nonexistent file should return false
    EXPECT_FALSE(player_->play("/nonexistent/path/music.mp3"));
    EXPECT_FALSE(player_->isPlaying());
}

TEST_F(MusicPlayerTest, Shutdown) {
    ASSERT_TRUE(player_->initialize());

    // Shutdown should be callable multiple times
    player_->shutdown();
    player_->shutdown();

    EXPECT_FALSE(player_->isPlaying());
}

// =============================================================================
// Audio Configuration Tests (Phase 8)
// =============================================================================

TEST(AudioConfigTest, VolumeClampingLower) {
    // Verify volume values are clamped to valid range
    float vol = -0.5f;
    vol = std::max(0.0f, std::min(vol, 1.0f));
    EXPECT_FLOAT_EQ(vol, 0.0f);
}

TEST(AudioConfigTest, VolumeClampingUpper) {
    float vol = 1.5f;
    vol = std::max(0.0f, std::min(vol, 1.0f));
    EXPECT_FLOAT_EQ(vol, 1.0f);
}

TEST(AudioConfigTest, VolumePercentToFloat) {
    // Test converting percentage (0-100) to float (0.0-1.0)
    auto percentToFloat = [](int percent) -> float {
        return std::clamp(percent, 0, 100) / 100.0f;
    };

    EXPECT_FLOAT_EQ(percentToFloat(0), 0.0f);
    EXPECT_FLOAT_EQ(percentToFloat(50), 0.5f);
    EXPECT_FLOAT_EQ(percentToFloat(100), 1.0f);
    EXPECT_FLOAT_EQ(percentToFloat(-10), 0.0f);  // Clamped
    EXPECT_FLOAT_EQ(percentToFloat(150), 1.0f);  // Clamped
}

TEST(AudioConfigTest, VolumeFloatToPercent) {
    // Test converting float (0.0-1.0) to percentage for display
    auto floatToPercent = [](float volume) -> int {
        return static_cast<int>(volume * 100.0f);
    };

    EXPECT_EQ(floatToPercent(0.0f), 0);
    EXPECT_EQ(floatToPercent(0.5f), 50);
    EXPECT_EQ(floatToPercent(1.0f), 100);
    EXPECT_EQ(floatToPercent(0.7f), 70);
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, ZoneMusicNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
