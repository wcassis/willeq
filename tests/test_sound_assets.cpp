#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/sound_assets.h"
#include "client/audio/sound_buffer.h"
#include "client/audio/audio_manager.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <filesystem>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// SoundAssets Parsing Tests
// =============================================================================

class SoundAssetsTest : public ::testing::Test {
protected:
    SoundAssets assets_;

    void SetUp() override {
        std::string assetsPath = std::string(EQ_PATH) + "/SoundAssets.txt";
        if (std::filesystem::exists(assetsPath)) {
            ASSERT_TRUE(assets_.loadFromFile(assetsPath));
        } else {
            GTEST_SKIP() << "SoundAssets.txt not found at: " << assetsPath;
        }
    }
};

TEST_F(SoundAssetsTest, LoadsEntries) {
    // SoundAssets.txt has ~2000+ entries
    EXPECT_GT(assets_.size(), 100);
}

TEST_F(SoundAssetsTest, FindsSwingSound) {
    // Sound ID 118 is Swing.WAV
    EXPECT_TRUE(assets_.hasSound(118));
    std::string filename = assets_.getFilename(118);
    EXPECT_FALSE(filename.empty());
    // Case-insensitive check
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    EXPECT_TRUE(lower.find("swing") != std::string::npos);
}

TEST_F(SoundAssetsTest, FindsLevelUpSound) {
    // Sound ID 139 is LevelUp.WAV
    EXPECT_TRUE(assets_.hasSound(139));
    std::string filename = assets_.getFilename(139);
    EXPECT_FALSE(filename.empty());
}

TEST_F(SoundAssetsTest, FindsSpellCastSound) {
    // Sound ID 108 is SpelCast.WAV
    EXPECT_TRUE(assets_.hasSound(108));
    std::string filename = assets_.getFilename(108);
    EXPECT_FALSE(filename.empty());
}

TEST_F(SoundAssetsTest, UnknownSoundsExcluded) {
    // Sound ID 1 is "Unknown" and should be excluded
    EXPECT_FALSE(assets_.hasSound(1));
}

TEST_F(SoundAssetsTest, ForEachIteratesAllEntries) {
    size_t count = 0;
    assets_.forEach([&count](uint32_t id, const std::string& filename, float volume) {
        ++count;
        EXPECT_FALSE(filename.empty());
        EXPECT_GT(volume, 0.0f);
        EXPECT_LE(volume, 1.0f);
    });
    EXPECT_EQ(count, assets_.size());
}

TEST_F(SoundAssetsTest, GetAllSoundIds) {
    auto ids = assets_.getAllSoundIds();
    EXPECT_EQ(ids.size(), assets_.size());

    // Check that some known IDs are present
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), 118) != ids.end());  // Swing
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), 139) != ids.end());  // LevelUp
}

// =============================================================================
// SoundBuffer WAV Loading Tests
// =============================================================================

class SoundBufferTest : public ::testing::Test {
protected:
    std::string soundsDir_;
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;

    void SetUp() override {
        soundsDir_ = std::string(EQ_PATH) + "/sounds/";
        if (!std::filesystem::exists(soundsDir_)) {
            GTEST_SKIP() << "Sounds directory not found at: " << soundsDir_;
        }

        // Initialize OpenAL context (required for buffer creation)
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
    }

    void TearDown() override {
        alcMakeContextCurrent(nullptr);
        if (context_) {
            alcDestroyContext(context_);
        }
        if (device_) {
            alcCloseDevice(device_);
        }
    }
};

TEST_F(SoundBufferTest, LoadSwingWav) {
    SoundBuffer buffer;
    std::string filepath = soundsDir_ + "Swing.WAV";

    if (!std::filesystem::exists(filepath)) {
        // Try lowercase
        filepath = soundsDir_ + "swing.wav";
    }

    if (std::filesystem::exists(filepath)) {
        EXPECT_TRUE(buffer.loadFromFile(filepath));
        EXPECT_TRUE(buffer.isValid());
        EXPECT_GT(buffer.getSampleRate(), 0u);
        EXPECT_GT(buffer.getChannels(), 0);
        EXPECT_GT(buffer.getDuration(), 0.0f);
    } else {
        GTEST_SKIP() << "Swing.WAV not found";
    }
}

TEST_F(SoundBufferTest, LoadCreatureSound) {
    SoundBuffer buffer;
    // Try loading a creature attack sound
    std::string filepath = soundsDir_ + "ans_atk.wav";

    if (std::filesystem::exists(filepath)) {
        EXPECT_TRUE(buffer.loadFromFile(filepath));
        EXPECT_TRUE(buffer.isValid());
    } else {
        GTEST_SKIP() << "ans_atk.wav not found";
    }
}

TEST_F(SoundBufferTest, InvalidFileReturnsError) {
    SoundBuffer buffer;
    EXPECT_FALSE(buffer.loadFromFile("/nonexistent/path/sound.wav"));
    EXPECT_FALSE(buffer.isValid());
}

TEST_F(SoundBufferTest, MoveSemantics) {
    SoundBuffer buffer1;
    std::string filepath = soundsDir_ + "ans_atk.wav";

    if (!std::filesystem::exists(filepath)) {
        GTEST_SKIP() << "Test sound file not found";
    }

    ASSERT_TRUE(buffer1.loadFromFile(filepath));
    ALuint originalHandle = buffer1.getBuffer();
    EXPECT_NE(originalHandle, 0u);

    // Move to buffer2
    SoundBuffer buffer2 = std::move(buffer1);
    EXPECT_EQ(buffer2.getBuffer(), originalHandle);
    EXPECT_EQ(buffer1.getBuffer(), 0u);  // buffer1 should be empty
    EXPECT_TRUE(buffer2.isValid());
    EXPECT_FALSE(buffer1.isValid());
}

// =============================================================================
// AudioManager Integration Tests (requires OpenAL device)
// =============================================================================

class AudioManagerTest : public ::testing::Test {
protected:
    std::unique_ptr<AudioManager> manager_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }

        manager_ = std::make_unique<AudioManager>();
        // Note: initialization may fail if no audio device available
    }

    void TearDown() override {
        if (manager_) {
            manager_->shutdown();
        }
    }
};

TEST_F(AudioManagerTest, InitializeLoadsAssets) {
    if (!manager_->initialize(EQ_PATH)) {
        GTEST_SKIP() << "Audio initialization failed (no audio device?)";
    }

    EXPECT_TRUE(manager_->isInitialized());
}

TEST_F(AudioManagerTest, PreloadCommonSounds) {
    if (!manager_->initialize(EQ_PATH)) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    size_t beforeCount = manager_->getLoadedSoundCount();
    manager_->preloadCommonSounds();
    size_t afterCount = manager_->getLoadedSoundCount();

    // Should have loaded some sounds
    EXPECT_GT(afterCount, beforeCount);
}

TEST_F(AudioManagerTest, VolumeControls) {
    if (!manager_->initialize(EQ_PATH)) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    manager_->setMasterVolume(0.5f);
    EXPECT_FLOAT_EQ(manager_->getMasterVolume(), 0.5f);

    manager_->setMusicVolume(0.3f);
    EXPECT_FLOAT_EQ(manager_->getMusicVolume(), 0.3f);

    manager_->setEffectsVolume(0.8f);
    EXPECT_FLOAT_EQ(manager_->getEffectsVolume(), 0.8f);

    // Test clamping
    manager_->setMasterVolume(1.5f);
    EXPECT_FLOAT_EQ(manager_->getMasterVolume(), 1.0f);

    manager_->setMasterVolume(-0.5f);
    EXPECT_FLOAT_EQ(manager_->getMasterVolume(), 0.0f);
}

TEST_F(AudioManagerTest, AudioEnableDisable) {
    if (!manager_->initialize(EQ_PATH)) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    EXPECT_TRUE(manager_->isAudioEnabled());

    manager_->setAudioEnabled(false);
    EXPECT_FALSE(manager_->isAudioEnabled());

    manager_->setAudioEnabled(true);
    EXPECT_TRUE(manager_->isAudioEnabled());
}

TEST_F(AudioManagerTest, ShutdownAndReinitialize) {
    if (!manager_->initialize(EQ_PATH)) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    EXPECT_TRUE(manager_->isInitialized());

    // Shutdown
    manager_->shutdown();
    EXPECT_FALSE(manager_->isInitialized());

    // Re-initialize should work
    EXPECT_TRUE(manager_->initialize(EQ_PATH));
    EXPECT_TRUE(manager_->isInitialized());
}

TEST_F(AudioManagerTest, MultipleShutdownsSafe) {
    if (!manager_->initialize(EQ_PATH)) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    // Multiple shutdowns should be safe
    manager_->shutdown();
    manager_->shutdown();
    manager_->shutdown();

    EXPECT_FALSE(manager_->isInitialized());
}

TEST_F(AudioManagerTest, InitializeWithInvalidPath) {
    // AudioManager initializes even with invalid path (audio device still works)
    // It just won't have any sounds to play
    bool result = manager_->initialize("/nonexistent/path/to/eq");
    if (result) {
        // Initialized but no sounds loaded
        EXPECT_TRUE(manager_->isInitialized());
        EXPECT_EQ(manager_->getLoadedSoundCount(), 0u);
    } else {
        // May fail if no audio device available
        EXPECT_FALSE(manager_->isInitialized());
    }
}

TEST_F(AudioManagerTest, PlaySoundByName) {
    if (!manager_->initialize(EQ_PATH)) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    // Playing a sound by name should work (or gracefully fail if not found)
    manager_->playSoundByName("swing.wav");
    manager_->playSoundByName("nonexistent.wav");  // Should not crash
}

TEST_F(AudioManagerTest, StopAllSounds) {
    if (!manager_->initialize(EQ_PATH)) {
        GTEST_SKIP() << "Audio initialization failed";
    }

    // Play some sounds then stop all
    manager_->playSoundByName("swing.wav");
    manager_->playSoundByName("kick1.wav");
    manager_->stopAllSounds();

    // Should not crash and complete successfully
    EXPECT_TRUE(manager_->isInitialized());
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, AudioNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
