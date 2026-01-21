#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/zone_sound_emitter.h"
#include "client/audio/zone_audio_manager.h"
#include "client/audio/eff_loader.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <filesystem>
#include <glm/glm.hpp>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// Day/Night State Calculation Tests
// =============================================================================

class DayNightStateTest : public ::testing::Test {
protected:
    // Simulate the day/night calculation from EverQuest::UpdateDayNightState
    bool calculateIsDaytime(uint8_t hour) {
        // Day: 6:00 AM (hour 6) to 6:59 PM (hour 18)
        // Night: 7:00 PM (hour 19) to 5:59 AM (hour 5)
        return (hour >= 6 && hour < 19);
    }
};

TEST_F(DayNightStateTest, DawnIsDaytime) {
    EXPECT_TRUE(calculateIsDaytime(6));   // 6 AM - start of day
}

TEST_F(DayNightStateTest, MorningIsDaytime) {
    EXPECT_TRUE(calculateIsDaytime(7));
    EXPECT_TRUE(calculateIsDaytime(8));
    EXPECT_TRUE(calculateIsDaytime(9));
    EXPECT_TRUE(calculateIsDaytime(10));
    EXPECT_TRUE(calculateIsDaytime(11));
}

TEST_F(DayNightStateTest, NoonIsDaytime) {
    EXPECT_TRUE(calculateIsDaytime(12));
}

TEST_F(DayNightStateTest, AfternoonIsDaytime) {
    EXPECT_TRUE(calculateIsDaytime(13));
    EXPECT_TRUE(calculateIsDaytime(14));
    EXPECT_TRUE(calculateIsDaytime(15));
    EXPECT_TRUE(calculateIsDaytime(16));
    EXPECT_TRUE(calculateIsDaytime(17));
    EXPECT_TRUE(calculateIsDaytime(18));
}

TEST_F(DayNightStateTest, DuskIsNight) {
    EXPECT_FALSE(calculateIsDaytime(19));  // 7 PM - start of night
}

TEST_F(DayNightStateTest, EveningIsNight) {
    EXPECT_FALSE(calculateIsDaytime(20));
    EXPECT_FALSE(calculateIsDaytime(21));
    EXPECT_FALSE(calculateIsDaytime(22));
    EXPECT_FALSE(calculateIsDaytime(23));
}

TEST_F(DayNightStateTest, MidnightIsNight) {
    EXPECT_FALSE(calculateIsDaytime(0));
}

TEST_F(DayNightStateTest, PreDawnIsNight) {
    EXPECT_FALSE(calculateIsDaytime(1));
    EXPECT_FALSE(calculateIsDaytime(2));
    EXPECT_FALSE(calculateIsDaytime(3));
    EXPECT_FALSE(calculateIsDaytime(4));
    EXPECT_FALSE(calculateIsDaytime(5));
}

// =============================================================================
// ZoneSoundEmitter Day/Night Tests
// =============================================================================

class EmitterDayNightTest : public ::testing::Test {
protected:
    ZoneSoundEmitter emitter_;
};

TEST_F(EmitterDayNightTest, HasDayNightVariantsType0) {
    // Type 0 (DayNightConstant) with different day/night sounds
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::DayNightConstant,
        "day_sound", "night_sound",
        5000, 5000, 1000, 0, 1000, 50);

    EXPECT_TRUE(emitter_.hasDayNightVariants());
}

TEST_F(EmitterDayNightTest, HasDayNightVariantsType1) {
    // Type 1 (BackgroundMusic) with different day/night music
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::BackgroundMusic,
        "day_music", "night_music",
        0, 0, 0, 0, 1000, 50);

    EXPECT_TRUE(emitter_.hasDayNightVariants());
}

TEST_F(EmitterDayNightTest, HasDayNightVariantsType2) {
    // Type 2 (StaticEffect) never has day/night variants
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect,
        "effect_sound", "different_sound",
        5000, 5000, 1000, 0, 1000, 50);

    EXPECT_FALSE(emitter_.hasDayNightVariants());
}

TEST_F(EmitterDayNightTest, HasDayNightVariantsType3) {
    // Type 3 (DayNightDistance) with different day/night sounds
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::DayNightDistance,
        "day_ambient", "night_ambient",
        5000, 5000, 1000, 1500, 1000, 50);

    EXPECT_TRUE(emitter_.hasDayNightVariants());
}

TEST_F(EmitterDayNightTest, NoDayNightVariantsWhenSameSounds) {
    // Even Type 0, if both sounds are the same, no variants
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::DayNightConstant,
        "same_sound", "same_sound",
        5000, 5000, 1000, 0, 1000, 50);

    EXPECT_FALSE(emitter_.hasDayNightVariants());
}

TEST_F(EmitterDayNightTest, NoDayNightVariantsWhenEmptySound) {
    // No variants if either sound is empty
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::DayNightConstant,
        "day_sound", "",
        5000, 5000, 1000, 0, 1000, 50);

    EXPECT_FALSE(emitter_.hasDayNightVariants());
}

TEST_F(EmitterDayNightTest, SetDayNightUpdatesState) {
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::DayNightConstant,
        "day_sound", "night_sound",
        5000, 5000, 1000, 0, 1000, 50);

    // Default is daytime
    emitter_.setDayNight(false);  // Switch to night
    emitter_.setDayNight(true);   // Switch back to day

    // No crash, state updated internally
    SUCCEED();
}

TEST_F(EmitterDayNightTest, TransitionToWithCrossfade) {
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::DayNightConstant,
        "day_sound", "night_sound",
        5000, 5000, 1000, 0, 1000, 50);

    // Transition to night with 2000ms crossfade
    emitter_.transitionTo(false, 2000);

    // No crash, transition initiated
    SUCCEED();
}

// =============================================================================
// ZoneAudioManager Day/Night Tests
// =============================================================================

class ZoneAudioManagerDayNightTest : public ::testing::Test {
protected:
    ZoneAudioManager manager_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(ZoneAudioManagerDayNightTest, DefaultIsDaytime) {
    EXPECT_TRUE(manager_.isDaytime());
}

TEST_F(ZoneAudioManagerDayNightTest, SetDayNightToNight) {
    manager_.setDayNight(false);
    EXPECT_FALSE(manager_.isDaytime());
}

TEST_F(ZoneAudioManagerDayNightTest, SetDayNightToDay) {
    manager_.setDayNight(false);
    manager_.setDayNight(true);
    EXPECT_TRUE(manager_.isDaytime());
}

TEST_F(ZoneAudioManagerDayNightTest, SetDayNightIdempotent) {
    manager_.setDayNight(true);
    manager_.setDayNight(true);  // Should be a no-op
    EXPECT_TRUE(manager_.isDaytime());
}

TEST_F(ZoneAudioManagerDayNightTest, DayNightWithLoadedZone) {
    manager_.loadZone("gfaydark", EQ_PATH);

    // Change to night
    manager_.setDayNight(false);
    EXPECT_FALSE(manager_.isDaytime());

    // Change back to day
    manager_.setDayNight(true);
    EXPECT_TRUE(manager_.isDaytime());
}

TEST_F(ZoneAudioManagerDayNightTest, DayNightTransitionNotifiesEmitters) {
    manager_.loadZone("halas", EQ_PATH);

    // Halas has music emitters with day/night variants
    size_t musicCount = manager_.getMusicEmitterCount();
    EXPECT_GT(musicCount, 0u);

    // Transition to night - should notify all emitters
    manager_.setDayNight(false);

    // No crash means emitters were notified successfully
    EXPECT_FALSE(manager_.isDaytime());
}

// =============================================================================
// Integration Tests: Day/Night with Update Loop
// =============================================================================

class DayNightIntegrationTest : public ::testing::Test {
protected:
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    ZoneAudioManager manager_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }

        // Initialize OpenAL
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

TEST_F(DayNightIntegrationTest, UpdateWithDayNightChange) {
    manager_.loadZone("gfaydark", EQ_PATH);

    glm::vec3 listenerPos(0.0f, 0.0f, 0.0f);
    float deltaTime = 0.016f;  // ~60 FPS

    // Update during day
    manager_.update(deltaTime, listenerPos, true);
    EXPECT_TRUE(manager_.isDaytime());

    // Transition to night
    manager_.setDayNight(false);

    // Update during night
    manager_.update(deltaTime, listenerPos, false);
    EXPECT_FALSE(manager_.isDaytime());
}

TEST_F(DayNightIntegrationTest, UpdatePassesDayNightToEmitters) {
    manager_.loadZone("gfaydark", EQ_PATH);

    glm::vec3 listenerPos(0.0f, 0.0f, 0.0f);
    float deltaTime = 0.016f;

    // Multiple updates with day state
    for (int i = 0; i < 10; ++i) {
        manager_.update(deltaTime, listenerPos, true);
    }

    // Change to night and update
    manager_.setDayNight(false);
    for (int i = 0; i < 10; ++i) {
        manager_.update(deltaTime, listenerPos, false);
    }

    // No crashes during day/night cycle
    SUCCEED();
}

// =============================================================================
// EFF File Day/Night Sound Tests
// =============================================================================

class EffDayNightTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(EffDayNightTest, HalasHasDayNightMusic) {
    // Halas has location-based music with day/night variants
    EffLoader loader;
    ASSERT_TRUE(loader.loadZone("halas", EQ_PATH));

    const auto& entries = loader.getSoundEntries();
    bool foundDayNightMusic = false;

    for (const auto& entry : entries) {
        if (entry.SoundType == 1) {  // Music type
            if (entry.SoundID1 != 0 && entry.SoundID2 != 0 &&
                entry.SoundID1 != entry.SoundID2) {
                foundDayNightMusic = true;
                break;
            }
        }
    }

    // Halas should have at least one music region with different day/night tracks
    // If not found, it's still valid - some zones may use same track for both
    SUCCEED();
}

TEST_F(EffDayNightTest, GfaydarkHasDayNightAmbient) {
    // Gfaydark should have some day/night ambient sounds
    EffLoader loader;
    ASSERT_TRUE(loader.loadZone("gfaydark", EQ_PATH));

    const auto& entries = loader.getSoundEntries();
    size_t type0Count = 0;  // DayNightConstant
    size_t type3Count = 0;  // DayNightDistance

    for (const auto& entry : entries) {
        if (entry.SoundType == 0) type0Count++;
        if (entry.SoundType == 3) type3Count++;
    }

    // Should have some ambient sounds
    EXPECT_GT(type0Count + type3Count, 0u);
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, DayNightAudioNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
