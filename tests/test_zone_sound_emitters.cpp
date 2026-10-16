#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/zone_sound_emitter.h"
#include "client/audio/zone_audio_manager.h"
#include "client/audio/eff_loader.h"
#include "client/audio/audio_manager.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_buffer.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <unistd.h>
#include <glm/glm.hpp>

using namespace EQT::Audio;

// Path to EQ client files for testing
static std::string getEqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : EQ_CLIENT_DATA_PATH;
}
static const std::string EQ_PATH = getEqClientPath();

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
// Audio Integration Tests
// =============================================================================

class ZoneSoundEmitterAudioTest : public ::testing::Test {
protected:
    ZoneSoundEmitter emitter_;
};

TEST_F(ZoneSoundEmitterAudioTest, InitializeAndStop) {
    emitter_.initialize(1, glm::vec3(0, 0, 0), 100.0f,
        EmitterSoundType::StaticEffect, "test", "", 0, 0, 0, 0, 1000, 50);

    // stop() should not crash
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


// =============================================================================
// B1074 (#394): zero-cooldown emitters LOOP while in range
// =============================================================================
//
// Headless: the AudioManager runs on the forced loopback (RDP) backend, whose
// pump thread renders the mixer in real time with no device. The sample is a
// synthetic 100 ms WAV written to a private temp dir and played by ABSOLUTE
// path, so these cases need no EQ client files.

namespace {

// 22050 Hz mono s16 WAV of `frames` frames of a non-zero square wave.
void writeTestWav(const std::filesystem::path& path, uint32_t frames) {
    const uint32_t rate = 22050;
    const uint32_t dataBytes = frames * 2;
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&f](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&f](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        u16(static_cast<uint16_t>((i / 50) % 2 ? 8000 : -8000));
    }
}

bool waitFor(const std::function<bool()>& cond, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

}  // namespace

class ZoneEmitterLoopTest : public ::testing::Test {
protected:
    static constexpr uint32_t kFrames = 2205;  // 100 ms at 22050 Hz
    static constexpr int kSampleMs = 100;

    std::filesystem::path dir_;
    std::string wav_;
    // Declared BEFORE every emitter a case creates, so emitters (which stop
    // their loop through it) are destroyed first -- the client's order too
    // (ZoneAudioManager unloads before the AudioManager shuts down).
    std::unique_ptr<AudioManager> audio_;

    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("willeq_b1074_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::create_directories(dir_);
        wav_ = (dir_ / "loop_test.wav").string();
        writeTestWav(wav_, kFrames);

        audio_ = std::make_unique<AudioManager>();
        if (!audio_->initialize(dir_.string(), /*forceLoopback=*/true)) {
            GTEST_SKIP() << "AudioManager loopback init failed";
        }
        audio_->setListenerPosition(glm::vec3(0.0f), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
    }

    void TearDown() override {
        if (audio_) audio_->shutdown();
        audio_.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    void listenAt(const glm::vec3& pos) {
        audio_->setListenerPosition(pos, glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
    }

    // Every ACTIVE SFX channel reading the test sample.
    int activeChannelsOnSample(bool* anyLooping = nullptr) {
        auto buffer = audio_->getSoundBufferByName(wav_);
        if (!buffer) return 0;
        int n = 0;
        bool looping = false;
        for (int h = 0; h < AudioMixer::MAX_SFX_CHANNELS; ++h) {
            MixChannel* ch = audio_->getMixer()->getChannel(h);
            if (ch && ch->active.load() && ch->samples == buffer->getSamples()) {
                ++n;
                looping = looping || ch->looping;
            }
        }
        if (anyLooping) *anyLooping = looping;
        return n;
    }

    bool sampleInUse() {
        auto buffer = audio_->getSoundBufferByName(wav_);
        return buffer && audio_->getSfxManager()->isInUse(*buffer);
    }

    void initEmitter(ZoneSoundEmitter& e, EmitterSoundType type,
                     const std::string& day, const std::string& night,
                     int32_t cooldown, float radius = 100.0f) {
        e.initialize(7, glm::vec3(0.0f), radius, type, day, night,
                     cooldown, cooldown, 0, /*asDistance=*/100, /*fadeOutMs=*/1000, 2);
    }
};

TEST_F(ZoneEmitterLoopTest, ZeroCooldownInRangeLoopsPastSampleLength) {
    ZoneSoundEmitter e;
    initEmitter(e, EmitterSoundType::StaticEffect, wav_, "", 0);
    const glm::vec3 near(10.0f, 0.0f, 0.0f);
    listenAt(near);

    e.update(0.016f, near, true, audio_.get());
    ASSERT_TRUE(e.isPlaying());
    ASSERT_TRUE(e.isLooping()) << "zero-cooldown emitter must loop (B1074)";
    const int handle = e.getLoopHandle().handle;

    // Well past the 100 ms sample: the one channel is still live and looping.
    std::this_thread::sleep_for(std::chrono::milliseconds(kSampleMs * 4));
    bool looping = false;
    EXPECT_EQ(activeChannelsOnSample(&looping), 1);
    EXPECT_TRUE(looping);
    EXPECT_TRUE(audio_->isLoopingSoundActive(e.getLoopHandle()));

    // Further in-range frames keep the SAME channel (no restart, no second).
    for (int i = 0; i < 5; ++i) e.update(0.1f, near, true, audio_.get());
    EXPECT_EQ(e.getLoopHandle().handle, handle);
    EXPECT_EQ(activeChannelsOnSample(), 1);
}

TEST_F(ZoneEmitterLoopTest, LargeRadiusLoopStartsBeyondSfxDefaultMaxDist) {
    // qeynos2 seq 100884 is a type-0 night loop with r=700; SfxManager's
    // default maxDist of 500 refused it anywhere past 500.
    ZoneSoundEmitter e;
    initEmitter(e, EmitterSoundType::DayNightConstant, "", wav_, 0, 700.0f);
    const glm::vec3 far(600.0f, 0.0f, 0.0f);
    listenAt(far);
    e.update(0.016f, far, /*isDay=*/false, audio_.get());
    EXPECT_TRUE(e.isLooping());
    EXPECT_EQ(activeChannelsOnSample(), 1);
}

TEST_F(ZoneEmitterLoopTest, LeavingRangeStopsLoopAfterFade) {
    ZoneSoundEmitter e;
    initEmitter(e, EmitterSoundType::StaticEffect, wav_, "", 0);
    const glm::vec3 near(10.0f, 0.0f, 0.0f);
    const glm::vec3 away(400.0f, 0.0f, 0.0f);
    listenAt(near);
    e.update(0.016f, near, true, audio_.get());
    ASSERT_TRUE(e.isLooping());

    // First frame out of range: fading, still on its channel.
    listenAt(away);
    e.update(0.1f, away, true, audio_.get());
    EXPECT_TRUE(e.isLooping());

    // fadeOutMs = 1000: done within ~1 s of frames.
    for (int i = 0; i < 15; ++i) e.update(0.1f, away, true, audio_.get());
    EXPECT_FALSE(e.isLooping());
    EXPECT_FALSE(e.isPlaying());
    EXPECT_EQ(activeChannelsOnSample(), 0);
    EXPECT_FALSE(sampleInUse()) << "B1015 in-use pin must be released";

    // Re-entering starts it again.
    listenAt(near);
    e.update(0.016f, near, true, audio_.get());
    EXPECT_TRUE(e.isLooping());
}

TEST_F(ZoneEmitterLoopTest, CooldownEmitterStaysNonLoopingOneShot) {
    ZoneSoundEmitter e;
    initEmitter(e, EmitterSoundType::StaticEffect, wav_, "", /*cooldown=*/5000);
    const glm::vec3 near(10.0f, 0.0f, 0.0f);
    listenAt(near);
    e.update(0.016f, near, true, audio_.get());
    EXPECT_TRUE(e.isPlaying());
    EXPECT_FALSE(e.isLooping());

    bool looping = true;
    ASSERT_EQ(activeChannelsOnSample(&looping), 1);
    EXPECT_FALSE(looping);
    // The one-shot ends by itself after the sample length.
    EXPECT_TRUE(waitFor([this] { return activeChannelsOnSample() == 0; }, 2000));
    // ...and is NOT replayed inside its 5 s cooldown.
    e.update(0.5f, near, true, audio_.get());
    EXPECT_EQ(activeChannelsOnSample(), 0);
}

TEST_F(ZoneEmitterLoopTest, DayNightSwitchStopsNightOnlyLoop) {
    // qeynos2 seq 100883/100884: day id 0, night id 163 -- no day sound.
    ZoneSoundEmitter e;
    initEmitter(e, EmitterSoundType::DayNightConstant, "", wav_, 0, 330.0f);
    const glm::vec3 near(10.0f, 0.0f, 0.0f);
    listenAt(near);
    e.update(0.016f, near, /*isDay=*/false, audio_.get());
    ASSERT_TRUE(e.isLooping());

    e.transitionTo(true, 2000);  // what ZoneAudioManager::setDayNight does
    e.update(0.016f, near, /*isDay=*/true, audio_.get());
    EXPECT_FALSE(e.isLooping());
    EXPECT_EQ(activeChannelsOnSample(), 0);

    e.update(0.016f, near, /*isDay=*/false, audio_.get());
    EXPECT_TRUE(e.isLooping()) << "night returns -> the loop restarts";
}

TEST_F(ZoneEmitterLoopTest, StopAndDestructorFreeLoopChannel) {
    const glm::vec3 near(10.0f, 0.0f, 0.0f);
    listenAt(near);
    {
        ZoneSoundEmitter e;
        initEmitter(e, EmitterSoundType::StaticEffect, wav_, "", 0);
        e.update(0.016f, near, true, audio_.get());
        ASSERT_TRUE(e.isLooping());
        e.stop();
        EXPECT_EQ(activeChannelsOnSample(), 0);
        EXPECT_FALSE(sampleInUse());

        e.update(0.016f, near, true, audio_.get());
        ASSERT_TRUE(e.isLooping());
        ZoneSoundEmitter moved = std::move(e);
        EXPECT_FALSE(e.isLooping());
        EXPECT_TRUE(moved.isLooping());
        EXPECT_EQ(activeChannelsOnSample(), 1);
    }  // `moved` destroyed while looping
    EXPECT_EQ(activeChannelsOnSample(), 0);
    EXPECT_FALSE(sampleInUse());
}

TEST_F(ZoneEmitterLoopTest, LoopFreedBehindEmitterIsRestartedNotAliased) {
    ZoneSoundEmitter e;
    initEmitter(e, EmitterSoundType::StaticEffect, wav_, "", 0);
    const glm::vec3 near(10.0f, 0.0f, 0.0f);
    listenAt(near);
    e.update(0.016f, near, true, audio_.get());
    ASSERT_TRUE(e.isLooping());
    const SfxLoopHandle stale = e.getLoopHandle();

    audio_->stopAllSounds();  // frees every SFX channel behind the emitter
    EXPECT_FALSE(audio_->isLoopingSoundActive(stale));
    // A stale token never stops a channel that is not its own.
    audio_->playSoundByName(wav_);
    audio_->stopLoopingSound(stale);
    EXPECT_EQ(activeChannelsOnSample(), 1);
    EXPECT_TRUE(waitFor([this] { return activeChannelsOnSample() == 0; }, 2000));

    e.update(0.016f, near, true, audio_.get());
    EXPECT_TRUE(e.isLooping());
    EXPECT_EQ(activeChannelsOnSample(), 1);
}

// Zone unload frees every emitter channel: qeynos2 (the B1074 report zone,
// every emitter zero-cooldown) loaded from the EQ client files.
class ZoneAudioLoopUnloadTest : public ::testing::Test {
protected:
    std::unique_ptr<AudioManager> audio_;
    std::unique_ptr<ZoneAudioManager> zone_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH + "/qeynos2_sounds.eff")) {
            GTEST_SKIP() << "qeynos2_sounds.eff not found under " << EQ_PATH;
        }
        audio_ = std::make_unique<AudioManager>();
        if (!audio_->initialize(EQ_PATH, /*forceLoopback=*/true)) {
            GTEST_SKIP() << "AudioManager loopback init failed";
        }
        zone_ = std::make_unique<ZoneAudioManager>();
        zone_->setAudioManager(audio_.get());
    }

    void TearDown() override {
        zone_.reset();
        if (audio_) audio_->shutdown();
        audio_.reset();
    }

    int activeLoopingSfxChannels() {
        int n = 0;
        for (int h = 0; h < AudioMixer::MAX_SFX_CHANNELS; ++h) {
            MixChannel* ch = audio_->getMixer()->getChannel(h);
            if (ch && ch->active.load() && ch->looping) ++n;
        }
        return n;
    }
};

TEST_F(ZoneAudioLoopUnloadTest, Qeynos2AmbientLoopsAndUnloadFreesEveryChannel) {
    ASSERT_TRUE(zone_->loadZone("qeynos2", EQ_PATH));
    // Inside both seq 100880 ((-145,-408,42) r=190) and seq 100886
    // ((-122,-680,-11) r=100), each id 162 (the LOOP "space" sound) for day
    // and night: ~180 and ~93 units away.
    const glm::vec3 listener(-130.0f, -588.0f, 10.0f);
    audio_->setListenerPosition(listener, glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
    zone_->update(0.016f, listener, /*isDay=*/false);

    const int loops = activeLoopingSfxChannels();
    if (loops == 0) {
        GTEST_SKIP() << "qeynos2 ambient sample did not resolve from the client files";
    }
    EXPECT_GE(zone_->getActiveEmitterCount(), 1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    zone_->update(0.3f, listener, false);
    EXPECT_EQ(activeLoopingSfxChannels(), loops) << "the ambience keeps looping";

    zone_->unloadZone();
    EXPECT_EQ(activeLoopingSfxChannels(), 0);
    EXPECT_EQ(zone_->getActiveEmitterCount(), 0u);
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, ZoneSoundEmittersNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
