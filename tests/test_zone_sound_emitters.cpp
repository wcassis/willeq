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
// ZoneSoundEmitter Unit Tests (no OpenAL required)
// =============================================================================

class ZoneSoundEmitterUnitTest : public ::testing::Test {
protected:
    ZoneSoundEmitter emitter_;
};

TEST_F(ZoneSoundEmitterUnitTest, DefaultState) {
    EXPECT_EQ(emitter_.getSequence(), 0);
    EXPECT_EQ(emitter_.getRadius(), 0.0f);
    EXPECT_FALSE(emitter_.isPlaying());
    EXPECT_FALSE(emitter_.isMusic());
}

TEST_F(ZoneSoundEmitterUnitTest, VolumeCalculationType0) {
    // Type 0: Constant volume within radius
    emitter_.initialize(
        1,                          // sequence
        glm::vec3(0, 0, 0),         // position
        100.0f,                     // radius
        EmitterSoundType::DayNightConstant,
        "test_sound",               // soundFile1
        "test_sound_night",         // soundFile2
        5000,                       // cooldown1
        5000,                       // cooldown2
        1000,                       // randomDelay
        0,                          // asDistance (ignored for type 0)
        1000,                       // fadeOutMs
        50                          // fullVolRange
    );

    // Within radius = full volume
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(50.0f), 1.0f);
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(99.0f), 1.0f);

    // Outside radius = no volume
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(101.0f), 0.0f);
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(200.0f), 0.0f);
}

TEST_F(ZoneSoundEmitterUnitTest, VolumeCalculationType2) {
    // Type 2: Distance-based volume (AsDistance controls max volume)
    emitter_.initialize(
        1,
        glm::vec3(0, 0, 0),
        100.0f,
        EmitterSoundType::StaticEffect,
        "test_sound",
        "",                         // no night sound for type 2
        5000,
        5000,
        1000,
        1500,                       // asDistance = 1500 -> volume = (3000-1500)/3000 = 0.5
        1000,
        50
    );

    // Volume should be 0.5 based on asDistance
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(0.0f), 0.5f);
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(50.0f), 0.5f);

    // Outside radius = 0
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(101.0f), 0.0f);
}

TEST_F(ZoneSoundEmitterUnitTest, VolumeCalculationAsDistanceEdgeCases) {
    // AsDistance = 0 -> volume = 1.0
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 0, 1000, 50);
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(0.0f), 1.0f);

    // AsDistance = 3000 -> volume = 0.0
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 3000, 1000, 50);
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(0.0f), 0.0f);

    // AsDistance > 3000 -> volume = 0.0
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 5000, 1000, 50);
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(0.0f), 0.0f);

    // AsDistance < 0 -> volume = 0.0
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, -100, 1000, 50);
    EXPECT_FLOAT_EQ(emitter_.calculateVolume(0.0f), 0.0f);
}

TEST_F(ZoneSoundEmitterUnitTest, RangeCheck) {
    emitter_.initialize(1, glm::vec3(100, 200, 50), 75.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 0, 1000, 50);

    // At position
    EXPECT_TRUE(emitter_.isInRange(glm::vec3(100, 200, 50)));

    // Within radius
    EXPECT_TRUE(emitter_.isInRange(glm::vec3(150, 200, 50)));  // 50 units away
    EXPECT_TRUE(emitter_.isInRange(glm::vec3(100, 275, 50)));  // 75 units away (edge)

    // Outside radius
    EXPECT_FALSE(emitter_.isInRange(glm::vec3(100, 276, 50)));  // 76 units away
    EXPECT_FALSE(emitter_.isInRange(glm::vec3(200, 200, 50)));  // 100 units away
}

TEST_F(ZoneSoundEmitterUnitTest, IsMusicType) {
    // Type 0 - not music
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::DayNightConstant, "test", "test2", 0, 0, 0, 0, 1000, 50);
    EXPECT_FALSE(emitter_.isMusic());

    // Type 1 - music
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::BackgroundMusic, "test", "test2", 0, 0, 0, 0, 1000, 50);
    EXPECT_TRUE(emitter_.isMusic());

    // Type 2 - not music
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 0, 1000, 50);
    EXPECT_FALSE(emitter_.isMusic());

    // Type 3 - not music
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::DayNightDistance, "test", "test2", 0, 0, 0, 0, 1000, 50);
    EXPECT_FALSE(emitter_.isMusic());
}

TEST_F(ZoneSoundEmitterUnitTest, MoveSemantics) {
    emitter_.initialize(42, glm::vec3(100, 200, 300), 150.0f,
        EmitterSoundType::StaticEffect, "sound1", "sound2", 1000, 2000, 500, 1000, 1500, 75);

    ZoneSoundEmitter moved = std::move(emitter_);

    EXPECT_EQ(moved.getSequence(), 42);
    EXPECT_EQ(moved.getPosition(), glm::vec3(100, 200, 300));
    EXPECT_EQ(moved.getRadius(), 150.0f);
    EXPECT_EQ(moved.getType(), EmitterSoundType::StaticEffect);
}

// =============================================================================
// ZoneAudioManager Unit Tests (no OpenAL required for loading)
// =============================================================================

class ZoneAudioManagerTest : public ::testing::Test {
protected:
    ZoneAudioManager manager_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(ZoneAudioManagerTest, InitialState) {
    EXPECT_FALSE(manager_.isZoneLoaded());
    EXPECT_TRUE(manager_.getCurrentZone().empty());
    EXPECT_EQ(manager_.getEmitterCount(), 0u);
    EXPECT_TRUE(manager_.isDaytime());
}

TEST_F(ZoneAudioManagerTest, LoadGfaydark) {
    EXPECT_TRUE(manager_.loadZone("gfaydark", EQ_PATH));
    EXPECT_TRUE(manager_.isZoneLoaded());
    EXPECT_EQ(manager_.getCurrentZone(), "gfaydark");
    EXPECT_GT(manager_.getEmitterCount(), 0u);
}

TEST_F(ZoneAudioManagerTest, LoadHalas) {
    EXPECT_TRUE(manager_.loadZone("halas", EQ_PATH));
    EXPECT_TRUE(manager_.isZoneLoaded());
    EXPECT_EQ(manager_.getCurrentZone(), "halas");
    EXPECT_GT(manager_.getEmitterCount(), 0u);

    // Halas has location-based music
    EXPECT_GT(manager_.getMusicEmitterCount(), 0u);
}

TEST_F(ZoneAudioManagerTest, LoadFreporte) {
    EXPECT_TRUE(manager_.loadZone("freporte", EQ_PATH));
    EXPECT_GT(manager_.getEmitterCount(), 0u);
}

TEST_F(ZoneAudioManagerTest, LoadNonexistentZone) {
    EXPECT_FALSE(manager_.loadZone("notarealzone", EQ_PATH));
    EXPECT_FALSE(manager_.isZoneLoaded());
    EXPECT_EQ(manager_.getEmitterCount(), 0u);
}

TEST_F(ZoneAudioManagerTest, UnloadZone) {
    manager_.loadZone("gfaydark", EQ_PATH);
    EXPECT_GT(manager_.getEmitterCount(), 0u);

    manager_.unloadZone();
    EXPECT_FALSE(manager_.isZoneLoaded());
    EXPECT_EQ(manager_.getEmitterCount(), 0u);
    EXPECT_TRUE(manager_.getCurrentZone().empty());
}

TEST_F(ZoneAudioManagerTest, ReloadZone) {
    manager_.loadZone("gfaydark", EQ_PATH);
    size_t count1 = manager_.getEmitterCount();

    manager_.loadZone("halas", EQ_PATH);
    EXPECT_EQ(manager_.getCurrentZone(), "halas");
    // Loading a new zone should clear the old one
    EXPECT_GT(manager_.getEmitterCount(), 0u);
}

TEST_F(ZoneAudioManagerTest, DayNightState) {
    manager_.loadZone("gfaydark", EQ_PATH);

    EXPECT_TRUE(manager_.isDaytime());

    manager_.setDayNight(false);
    EXPECT_FALSE(manager_.isDaytime());

    manager_.setDayNight(true);
    EXPECT_TRUE(manager_.isDaytime());
}

TEST_F(ZoneAudioManagerTest, PauseResume) {
    manager_.loadZone("gfaydark", EQ_PATH);

    manager_.pause();
    // After pause, update should not change state
    // (we can't fully test without AudioManager)

    manager_.resume();
}

TEST_F(ZoneAudioManagerTest, ActiveEmitterCount) {
    manager_.loadZone("gfaydark", EQ_PATH);

    // Without audio manager, no emitters should be active
    EXPECT_EQ(manager_.getActiveEmitterCount(), 0u);
}

// =============================================================================
// Integration with EFF Loader Tests
// =============================================================================

TEST_F(ZoneAudioManagerTest, EffLoaderIntegration) {
    manager_.loadZone("gfaydark", EQ_PATH);

    // Verify emitter count matches EFF file
    EffLoader loader;
    loader.loadZone("gfaydark", EQ_PATH);

    EXPECT_EQ(manager_.getEmitterCount(), loader.getEntryCount());
}

TEST_F(ZoneAudioManagerTest, MusicEmitterCounting) {
    manager_.loadZone("gfaydark", EQ_PATH);

    EffLoader loader;
    loader.loadZone("gfaydark", EQ_PATH);

    EXPECT_EQ(manager_.getMusicEmitterCount(), loader.getMusicEntryCount());
}

// =============================================================================
// OpenAL Integration Tests (requires audio device)
// =============================================================================

class ZoneSoundEmitterAudioTest : public ::testing::Test {
protected:
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    ZoneSoundEmitter emitter_;

    void SetUp() override {
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

TEST_F(ZoneSoundEmitterAudioTest, InitializeCreatesSource) {
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 0, 1000, 50);

    // After initialization with OpenAL context, source should be created
    // We can't directly access the source, but stop() should not crash
    emitter_.stop();
}

TEST_F(ZoneSoundEmitterAudioTest, StopWhenNotPlaying) {
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 0, 1000, 50);

    // Calling stop when not playing should be safe
    emitter_.stop();
    EXPECT_FALSE(emitter_.isPlaying());
}

TEST_F(ZoneSoundEmitterAudioTest, MovePreservesState) {
    emitter_.initialize(1, glm::vec3(100, 200, 300), 150.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 0, 1000, 50);

    ZoneSoundEmitter moved = std::move(emitter_);

    // Moved emitter should work
    moved.stop();
    EXPECT_FALSE(moved.isPlaying());

    // Original should be in empty state
    EXPECT_FALSE(emitter_.isPlaying());
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, ZoneSoundEmittersNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
