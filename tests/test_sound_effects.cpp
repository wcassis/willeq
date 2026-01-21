#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/audio_manager.h"
#include "client/audio/sound_assets.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <filesystem>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// Sound ID Constants Tests (don't require audio device)
// =============================================================================

TEST(SoundIdConstantsTest, CombatSoundsHaveValidIds) {
    // Combat sound IDs should be distinct
    EXPECT_NE(SoundId::MELEE_SWING, SoundId::MELEE_HIT);
    EXPECT_NE(SoundId::MELEE_HIT, SoundId::MELEE_MISS);
    EXPECT_NE(SoundId::KICK, SoundId::PUNCH);
}

TEST(SoundIdConstantsTest, SpellSoundsHaveValidIds) {
    EXPECT_NE(SoundId::SPELL_CAST, SoundId::SPELL_FIZZLE);
    EXPECT_GT(SoundId::SPELL_CAST, 0u);
    EXPECT_GT(SoundId::SPELL_FIZZLE, 0u);
}

TEST(SoundIdConstantsTest, UiSoundsHaveValidIds) {
    EXPECT_GT(SoundId::BUTTON_CLICK, 0u);
    EXPECT_GT(SoundId::OPEN_WINDOW, 0u);
    EXPECT_GT(SoundId::CLOSE_WINDOW, 0u);
}

TEST(SoundIdConstantsTest, EnvironmentSoundsHaveValidIds) {
    EXPECT_GT(SoundId::WATER_IN, 0u);
    EXPECT_GT(SoundId::WATER_OUT, 0u);
    EXPECT_GT(SoundId::TELEPORT, 0u);
}

TEST(SoundIdConstantsTest, PlayerSoundsHaveValidIds) {
    EXPECT_GT(SoundId::LEVEL_UP, 0u);
    EXPECT_GT(SoundId::DEATH, 0u);
}

// =============================================================================
// Sound Asset File Tests (don't require audio device)
// =============================================================================

class SoundEffectFileTest : public ::testing::Test {
protected:
    std::string eqPath_;
    std::string soundsPath_;

    void SetUp() override {
        eqPath_ = std::string(EQ_PATH);
        soundsPath_ = eqPath_ + "/sounds";
        if (!std::filesystem::exists(soundsPath_)) {
            GTEST_SKIP() << "EQ sounds directory not found at: " << soundsPath_;
        }
    }
};

TEST_F(SoundEffectFileTest, SoundDirectoryHasWavFiles) {
    size_t wavCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(soundsPath_)) {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".wav") {
            wavCount++;
        }
    }

    std::cout << "WAV files found in sounds/: " << wavCount << std::endl;
    EXPECT_GT(wavCount, 100) << "Expected many WAV files in sounds directory";
}

TEST_F(SoundEffectFileTest, CreatureCombatSoundsExist) {
    // Creature sounds use patterns like: {race}_atk.wav, {race}_dam.wav, {race}_dth.wav
    // Check for a few known creature prefixes (from EQ sound file naming)
    std::vector<std::string> creaturePrefixes = {
        "ans", "bas", "bgb", "bon", "box", "btn", "brl"
    };

    size_t found = 0;
    for (const auto& prefix : creaturePrefixes) {
        std::string atkPath = soundsPath_ + "/" + prefix + "_atk.wav";
        std::string damPath = soundsPath_ + "/" + prefix + "_dam.wav";
        std::string dthPath = soundsPath_ + "/" + prefix + "_dth.wav";

        if (std::filesystem::exists(atkPath)) found++;
        if (std::filesystem::exists(damPath)) found++;
        if (std::filesystem::exists(dthPath)) found++;
    }

    EXPECT_GT(found, 0) << "Expected at least some creature combat sound files";
}

TEST_F(SoundEffectFileTest, CreatureSpellSoundsExist) {
    // Creature spell sounds use pattern: {race}_spl.wav
    // Check for a few known creature prefixes (from EQ sound file naming)
    std::vector<std::string> creaturePrefixes = {
        "ans", "bas", "bgb"
    };

    size_t found = 0;
    for (const auto& prefix : creaturePrefixes) {
        std::string splPath = soundsPath_ + "/" + prefix + "_spl.wav";
        if (std::filesystem::exists(splPath)) found++;
    }

    EXPECT_GT(found, 0) << "Expected at least some creature spell sound files";
}

// =============================================================================
// AudioManager Sound Effect Tests (require audio device)
// =============================================================================

class SoundEffectAudioTest : public ::testing::Test {
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

TEST_F(SoundEffectAudioTest, PlaySoundById) {
    ASSERT_TRUE(manager_->isInitialized());

    // Play a known sound ID - should not crash
    manager_->playSound(SoundId::MELEE_SWING);
    manager_->playSound(SoundId::SPELL_CAST);

    // Play with position
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST_F(SoundEffectAudioTest, PlaySoundAtPosition) {
    ASSERT_TRUE(manager_->isInitialized());

    // Play positional sounds at different locations
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(10.0f, 0.0f, 0.0f));
    manager_->playSound(SoundId::MELEE_MISS, glm::vec3(-10.0f, 0.0f, 0.0f));
    manager_->playSound(SoundId::DEATH, glm::vec3(0.0f, 10.0f, 5.0f));
}

TEST_F(SoundEffectAudioTest, VolumeControlsAffectSounds) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set effects volume
    manager_->setEffectsVolume(0.5f);
    EXPECT_FLOAT_EQ(manager_->getEffectsVolume(), 0.5f);

    // Play sound at half volume
    manager_->playSound(SoundId::MELEE_SWING);

    // Mute effects
    manager_->setEffectsVolume(0.0f);
    EXPECT_FLOAT_EQ(manager_->getEffectsVolume(), 0.0f);

    // Restore
    manager_->setEffectsVolume(1.0f);
}

TEST_F(SoundEffectAudioTest, InvalidSoundIdDoesNotCrash) {
    ASSERT_TRUE(manager_->isInitialized());

    // Playing invalid sound IDs should not crash
    manager_->playSound(99999);
    manager_->playSound(0);
    manager_->playSound(UINT32_MAX);
}

TEST_F(SoundEffectAudioTest, MultipleConcurrentSounds) {
    ASSERT_TRUE(manager_->isInitialized());

    // Play multiple sounds concurrently (tests source pooling)
    for (int i = 0; i < 10; i++) {
        manager_->playSound(SoundId::MELEE_SWING, glm::vec3(i * 2.0f, 0.0f, 0.0f));
    }
}

TEST_F(SoundEffectAudioTest, SetListenerPosition) {
    ASSERT_TRUE(manager_->isInitialized());

    // Set listener position (camera/player position)
    manager_->setListenerPosition(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),  // forward
        glm::vec3(0.0f, 0.0f, 1.0f)   // up
    );

    // Play positional sound - should be positioned relative to listener
    manager_->playSound(SoundId::MELEE_HIT, glm::vec3(5.0f, 0.0f, 0.0f));
}

TEST_F(SoundEffectAudioTest, AudioDisableStopsSounds) {
    ASSERT_TRUE(manager_->isInitialized());
    EXPECT_TRUE(manager_->isAudioEnabled());

    // Play a sound
    manager_->playSound(SoundId::MELEE_SWING);

    // Disable audio - should stop all sounds
    manager_->setAudioEnabled(false);
    EXPECT_FALSE(manager_->isAudioEnabled());

    // Playing sounds while disabled should be a no-op
    manager_->playSound(SoundId::MELEE_HIT);

    // Re-enable audio
    manager_->setAudioEnabled(true);
    EXPECT_TRUE(manager_->isAudioEnabled());
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, SoundEffectsNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
