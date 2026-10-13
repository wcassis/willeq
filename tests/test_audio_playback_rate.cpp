// T404 Leg A (board B250): every sound fable loads out of a snd*.pfs archive
// played at exactly DOUBLE rate -- an octave high and half as long.
//
// T401 measured k = 2.00 on three assets in a bit-exact ALSA-loopback
// recording (review/T401/ SS1d). The cause is NOT the decoder and NOT the PFS
// path: SoundBuffer::loadFromMemory reads the WAV header correctly and
// SfxManager::play carries the rate onto the channel. AudioMixer::render()
// then STEPS ch.position BY ONE OUTPUT FRAME and never reads ch.sampleRate at
// all, so a channel is always played at the device rate. The device is opened
// at AudioMixer::SAMPLE_RATE == 22050 and the shipped effects are 11025 Hz
// mono, hence exactly 2.00.
//
// These tests pin BOTH halves: the decoded rate of a real archive member
// (which was always right) and the rate the mixer actually plays it at (which
// was not). No audio device, no server.
#ifdef WITH_AUDIO

#include <gtest/gtest.h>

#include "client/audio/audio_manager.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_buffer.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace EQT::Audio;

namespace {

std::string EqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    std::string path = env ? env : EQ_CLIENT_DATA_PATH;
    std::error_code ec;
    auto canonical = std::filesystem::canonical(path, ec);
    return ec ? path : canonical.string();
}

class RepoRootCwd {
public:
    RepoRootCwd() : saved_(std::filesystem::current_path()) {
        std::filesystem::current_path(WILLEQ_REPO_ROOT);
    }
    ~RepoRootCwd() { std::filesystem::current_path(saved_); }
private:
    std::filesystem::path saved_;
};

// ---------------------------------------------------------------------------
// The mixer half: a synthetic channel, no assets, no device.
// ---------------------------------------------------------------------------

// Render `outFrames` output frames through a mixer holding one non-looping
// PCM channel and return how many output frames elapsed before the channel
// went inactive -- i.e. the sound's real duration in device frames.
size_t PlayoutFramesFor(uint32_t assetRate, size_t assetFrames) {
    AudioMixer mixer;
    int handle = mixer.allocSfxChannel();
    EXPECT_GE(handle, 0);
    MixChannel* ch = mixer.getChannel(handle);
    EXPECT_NE(ch, nullptr);

    static std::vector<float> samples;
    samples.assign(assetFrames, 0.5f);

    ch->type = MixChannelType::SFX;
    ch->volume = 1.0f;
    ch->pan = 0.0f;
    ch->samples = samples.data();
    ch->sampleRate = assetRate;
    ch->channels = 1;
    ch->totalFrames = assetFrames;
    ch->position = 0;
    ch->looping = false;
    ch->active.store(true);

    constexpr int kBlock = 64;
    std::vector<float> out(kBlock * 2);
    size_t rendered = 0;
    // Bound the loop generously: 8x the un-resampled length.
    const size_t limit = assetFrames * 8 + 1024;
    while (ch->active.load() && rendered < limit) {
        mixer.render(out.data(), kBlock);
        rendered += kBlock;
    }
    return rendered;
}

// An 11,025 Hz asset on a 22,050 Hz device must last TWICE as many output
// frames as it has source frames. Pre-T404 the mixer stepped one source frame
// per output frame, so it lasted exactly as many -- k = 2.00, T401's
// measurement.
TEST(AudioPlaybackRate, HalfRateAssetLastsTwiceAsManyDeviceFrames) {
    ASSERT_EQ(AudioMixer::SAMPLE_RATE, 22050u);
    const size_t assetFrames = 4096;
    const size_t got = PlayoutFramesFor(11025, assetFrames);
    // Rendering is blocked, so allow one block of slack either side.
    EXPECT_NEAR(static_cast<double>(got), 2.0 * assetFrames, 128.0)
        << "11025 Hz asset played out in " << got << " device frames; "
        << "expected ~" << (2 * assetFrames);
}

// The control: an asset already at the device rate is unchanged.
TEST(AudioPlaybackRate, DeviceRateAssetIsUnchanged) {
    const size_t assetFrames = 4096;
    const size_t got = PlayoutFramesFor(AudioMixer::SAMPLE_RATE, assetFrames);
    EXPECT_NEAR(static_cast<double>(got), 1.0 * assetFrames, 128.0);
}

// The other direction, so the fix cannot be a hard-coded halving: a 44,100 Hz
// asset must last HALF as many device frames.
TEST(AudioPlaybackRate, DoubleRateAssetLastsHalfAsManyDeviceFrames) {
    const size_t assetFrames = 8192;
    const size_t got = PlayoutFramesFor(44100, assetFrames);
    EXPECT_NEAR(static_cast<double>(got), 0.5 * assetFrames, 128.0);
}

// Rate conversion must not change the PITCH of a device-rate tone, and must
// halve the pitch of a half-rate one. Measure by zero crossings.
size_t ZeroCrossingsOfRenderedSine(uint32_t assetRate, double assetHz,
                                   size_t assetFrames, size_t outFrames) {
    AudioMixer mixer;
    int handle = mixer.allocSfxChannel();
    MixChannel* ch = mixer.getChannel(handle);

    static std::vector<float> samples;
    samples.resize(assetFrames);
    for (size_t i = 0; i < assetFrames; ++i) {
        samples[i] = static_cast<float>(
            std::sin(2.0 * M_PI * assetHz * static_cast<double>(i) / assetRate));
    }

    ch->type = MixChannelType::SFX;
    ch->volume = 1.0f;
    ch->pan = 0.0f;
    ch->samples = samples.data();
    ch->sampleRate = assetRate;
    ch->channels = 1;
    ch->totalFrames = assetFrames;
    ch->position = 0;
    ch->looping = false;
    ch->active.store(true);

    std::vector<float> out(outFrames * 2);
    mixer.render(out.data(), static_cast<int>(outFrames));

    size_t crossings = 0;
    for (size_t i = 1; i < outFrames; ++i) {
        if ((out[(i - 1) * 2] < 0.0f) != (out[i * 2] < 0.0f)) ++crossings;
    }
    return crossings;
}

// A 1,000 Hz tone stored at 11,025 Hz must come out as 1,000 Hz, not 2,000.
// 4,410 output frames = 0.2 s at 22,050 Hz => 400 zero crossings for 1 kHz.
TEST(AudioPlaybackRate, HalfRateToneKeepsItsPitch) {
    const size_t crossings = ZeroCrossingsOfRenderedSine(11025, 1000.0, 4096, 4410);
    EXPECT_NEAR(static_cast<double>(crossings), 400.0, 8.0)
        << "1 kHz tone stored at 11025 Hz rendered with " << crossings
        << " zero crossings in 0.2 s; 400 = 1 kHz, 800 = the B250 octave";
}

// ---------------------------------------------------------------------------
// The decoder half: the real archive members T401 measured.
// ---------------------------------------------------------------------------

class PfsSoundRateTest : public ::testing::Test {
protected:
    std::unique_ptr<RepoRootCwd> cwd_;
    std::unique_ptr<AudioManager> manager_;

    void SetUp() override {
        std::string eqPath = EqClientPath();
        if (!std::filesystem::exists(eqPath + "/snd2.pfs")) {
            GTEST_SKIP() << "snd2.pfs not found under " << eqPath;
        }
        cwd_ = std::make_unique<RepoRootCwd>();
        manager_ = std::make_unique<AudioManager>();
        if (!manager_->initialize(eqPath, /*forceLoopback=*/true)) {
            GTEST_SKIP() << "AudioManager::initialize failed";
        }
    }

    void TearDown() override {
        if (manager_) manager_->shutdown();
        manager_.reset();
        cwd_.reset();
    }

    // The decoded rate must equal the archive member's own RIFF header rate,
    // and the playout length at that rate must equal the header's duration.
    void ExpectDecodedRate(const char* name, uint32_t expectedRate) {
        auto buffer = manager_->getSoundBufferByName(name);
        ASSERT_NE(buffer, nullptr) << name;
        EXPECT_EQ(buffer->getSampleRate(), expectedRate) << name;
        ASSERT_GT(buffer->getFrameCount(), 0u) << name;

        const double srcSeconds =
            static_cast<double>(buffer->getFrameCount()) / buffer->getSampleRate();
        const size_t playout =
            PlayoutFramesFor(buffer->getSampleRate(), buffer->getFrameCount());
        const double outSeconds =
            static_cast<double>(playout) / AudioMixer::SAMPLE_RATE;
        EXPECT_NEAR(outSeconds, srcSeconds, 0.01)
            << name << ": header says " << srcSeconds << " s, mixer played "
            << outSeconds << " s (ratio " << (srcSeconds / outSeconds) << ")";
    }
};

// T401's three measured assets, all 11,025 Hz 16-bit mono.
TEST_F(PfsSoundRateTest, BuyItemDecodesAt11025) { ExpectDecodedRate("BuyItem.WAV", 11025); }
TEST_F(PfsSoundRateTest, LevDnDecodesAt11025)   { ExpectDecodedRate("LevDn.WAV", 11025); }
TEST_F(PfsSoundRateTest, SpelCastDecodesAt11025){ ExpectDecodedRate("SpelCast.WAV", 11025); }

// The loose-file path decodes the identical bytes to the identical rate --
// the PFS path must not be a special case (the pre-T392 route).
TEST_F(PfsSoundRateTest, SwordBlkDecodesAt11025){ ExpectDecodedRate("SwordBlk.WAV", 11025); }

} // namespace

#endif // WITH_AUDIO
