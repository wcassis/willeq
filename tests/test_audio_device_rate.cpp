// B1069 (GitLab #389): the playback DEVICE rate.
//
// The defect: MiniaudioBackend opened the device at AudioMixer::SAMPLE_RATE
// (22050, the EQ asset rate). The ODROID XU4's Exynos I2S/HDMI path has no
// 22050, ALSA rounded to 32000 (measured: /proc/asound/card0/pcm0p/sub0/
// hw_params "rate: 32000" with the client RUNNING), and the HDMI monitor
// drops 32 kHz -- no music, no effects. The fix opens the device at 48000 by
// default (`audio.device_sample_rate`, `--audio-rate`; 0 = native), keeps the
// mixer at 22050 and resamples once in the backend (DeviceRateAdapter; the
// design measurement is refactor/B1069/README.md).
//
// Pinned here, with no audio hardware:
//   1. config: default 48000, the JSON key honoured, 0 passes through, the
//      CLI mirror wins over the file (loaded after the arguments);
//   2. the adapter: a 22050 Hz asset played through a 48000 Hz pull keeps
//      its duration and the mixer is pulled at exactly 22050 frames/s over
//      10 s of callbacks (no drift), for the period size the backend asks
//      for AND for ragged pulls; equal rates are a bit-exact pass-through;
//   3. the backend on miniaudio's NULL device: requested 48000 configures
//      48000 and resamples, 0 takes the native rate, and the init log carries
//      the one grep-able "Audio device: requested=... native=..." line.
#ifdef WITH_AUDIO
#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "client/application.h"
#include "client/audio/audio_backend.h"
#include "client/audio/audio_device_rate.h"
#include "client/audio/audio_manager.h"
#include "client/audio/audio_mixer.h"
#include "common/logging.h"

using EQT::Audio::AudioMixer;
using EQT::Audio::DeviceRateAdapter;
using EQT::Audio::MiniaudioBackend;
using EQT::Audio::kDefaultDeviceSampleRate;

namespace {

std::string WriteAudioConfig(const std::string& name, const std::string& audioBody) {
    const std::string path = ::testing::TempDir() + name;
    std::ofstream out(path);
    out << "{\n  \"audio\": {\n    \"enabled\": true" << audioBody << "\n  }\n}\n";
    return path;
}

eqt::ApplicationConfig ParseArgs(std::vector<std::string> args) {
    std::vector<char*> argv;
    static std::string prog = "willeq";
    argv.push_back(prog.data());
    for (auto& a : args) argv.push_back(a.data());
    return eqt::Application::parseArguments(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

// ─── 1. config ──────────────────────────────────────────────────────────────

TEST(AudioDeviceRateConfig, DefaultIs48000Everywhere) {
    EXPECT_EQ(kDefaultDeviceSampleRate, 48000u);
    EXPECT_EQ(eqt::ApplicationConfig{}.audioDeviceSampleRate, kDefaultDeviceSampleRate);
    EXPECT_EQ(EQT::Audio::AudioManager{}.getDeviceSampleRate(), kDefaultDeviceSampleRate);
    EXPECT_EQ(MiniaudioBackend{}.requestedDeviceSampleRate(), kDefaultDeviceSampleRate);
    EXPECT_EQ(ParseArgs({}).audioDeviceSampleRate, kDefaultDeviceSampleRate);
    // The mixer itself stays at the EQ asset rate: the fix is the DEVICE.
    EXPECT_EQ(AudioMixer::SAMPLE_RATE, 22050u);
}

TEST(AudioDeviceRateConfig, ConfigKeyAbsentKeepsDefault) {
    eqt::ApplicationConfig config;
    ASSERT_TRUE(eqt::Application::loadConfigFile(WriteAudioConfig("b1069_absent.json", ""), config));
    EXPECT_EQ(config.audioDeviceSampleRate, 48000u);
}

TEST(AudioDeviceRateConfig, ConfigKeyHonoured) {
    eqt::ApplicationConfig config;
    ASSERT_TRUE(eqt::Application::loadConfigFile(
        WriteAudioConfig("b1069_44100.json", ",\n    \"device_sample_rate\": 44100"), config));
    EXPECT_EQ(config.audioDeviceSampleRate, 44100u);
}

TEST(AudioDeviceRateConfig, ZeroPassesThrough) {
    eqt::ApplicationConfig config;
    ASSERT_TRUE(eqt::Application::loadConfigFile(
        WriteAudioConfig("b1069_zero.json", ",\n    \"device_sample_rate\": 0"), config));
    EXPECT_EQ(config.audioDeviceSampleRate, 0u) << "0 = let the backend choose; must not be defaulted";
    EXPECT_EQ(ParseArgs({"--audio-rate", "0"}).audioDeviceSampleRate, 0u);
}

TEST(AudioDeviceRateConfig, CliMirrorWinsOverTheFile) {
    eqt::ApplicationConfig config = ParseArgs({"--audio-rate", "32000"});
    EXPECT_EQ(config.audioDeviceSampleRate, 32000u);
    ASSERT_TRUE(eqt::Application::loadConfigFile(
        WriteAudioConfig("b1069_cli.json", ",\n    \"device_sample_rate\": 44100"), config));
    EXPECT_EQ(config.audioDeviceSampleRate, 32000u);
}

TEST(AudioDeviceRateConfig, ManagerCarriesTheRate) {
    EQT::Audio::AudioManager mgr;
    mgr.setDeviceSampleRate(0);
    EXPECT_EQ(mgr.getDeviceSampleRate(), 0u);
    mgr.setDeviceSampleRate(44100);
    EXPECT_EQ(mgr.getDeviceSampleRate(), 44100u);
}

// main.cpp publishes the parsed rate process-wide (audio_device_rate.h: the
// G04 golden pins initEverQuestClient()); a manager built after that takes
// it as its default, 0 included.
TEST(AudioDeviceRateConfig, PublishedRateIsTheManagersDefault) {
    const uint32_t saved = EQT::Audio::configuredDeviceSampleRate();
    EQT::Audio::setConfiguredDeviceSampleRate(32000);
    EXPECT_EQ(EQT::Audio::AudioManager{}.getDeviceSampleRate(), 32000u);
    EQT::Audio::setConfiguredDeviceSampleRate(0);
    EXPECT_EQ(EQT::Audio::AudioManager{}.getDeviceSampleRate(), 0u);
    EQT::Audio::setConfiguredDeviceSampleRate(saved);
}

// ─── 2. the adapter ─────────────────────────────────────────────────────────

namespace {

// One second of a 22050 Hz mono asset at a constant level, played on one SFX
// channel. A constant survives the linear resampler's low-pass, so "how many
// device frames carry it" is the asset's duration at the device rate.
struct OneSecondAsset {
    std::vector<float> pcm = std::vector<float>(22050, 0.5f);
    void startOn(AudioMixer& mixer) {
        const int h = mixer.allocSfxChannel();
        ASSERT_GE(h, 0);
        auto* ch = mixer.getChannel(h);
        ch->samples = pcm.data();
        ch->sampleRate = 22050;
        ch->channels = 1;
        ch->totalFrames = pcm.size();
        ch->position = 0.0;
        ch->looping = false;
        ch->active.store(true, std::memory_order_release);
    }
};

struct PullResult {
    uint64_t deviceFrames = 0;
    uint64_t audibleFrames = 0;
    uint64_t mixerFrames = 0;
};

PullResult PullSeconds(uint32_t deviceRate, double seconds, const std::vector<uint32_t>& sizes) {
    AudioMixer mixer;
    OneSecondAsset asset;
    asset.startOn(mixer);
    DeviceRateAdapter adapter;
    EXPECT_TRUE(adapter.init(&mixer, AudioMixer::SAMPLE_RATE, deviceRate));
    const uint64_t total = static_cast<uint64_t>(seconds * deviceRate);
    std::vector<float> buf;
    PullResult r;
    size_t k = 0;
    while (r.deviceFrames < total) {
        uint32_t n = sizes[k++ % sizes.size()];
        if (r.deviceFrames + n > total) n = static_cast<uint32_t>(total - r.deviceFrames);
        buf.assign(static_cast<size_t>(n) * 2, -9.0f);
        adapter.render(buf.data(), n);
        for (uint32_t i = 0; i < n; ++i) {
            EXPECT_NE(buf[i * 2], -9.0f) << "adapter left a frame unwritten";
            // 0.5 at centre pan is 0.25 per side (audio_mixer.cpp gain);
            // half of that marks "the asset is playing".
            if (buf[i * 2] > 0.125f) ++r.audibleFrames;
        }
        r.deviceFrames += n;
    }
    r.mixerFrames = adapter.mixerFramesPulled();
    return r;
}

}  // namespace

TEST(DeviceRateAdapter, MixerPulledAtItsOwnRateNoDriftOver10s) {
    // 1115 = the period the backend configures at 48000 (512 mixer frames
    // of time, audio_backend.cpp kMixerPeriodFrames).
    const PullResult r = PullSeconds(48000, 10.0, {1115});
    EXPECT_EQ(r.deviceFrames, 480000u);
    // Exactly 10 s of mixer time; the linear resampler holds at most a frame
    // or two of look-ahead, never a growing debt.
    EXPECT_NEAR(static_cast<double>(r.mixerFrames), 220500.0, 2.0);
}

TEST(DeviceRateAdapter, RaggedPullsAndOversizedPullsDoNotDrift) {
    // Odd sizes, a 1-frame pull, and a pull larger than the adapter's chunk.
    const PullResult r = PullSeconds(48000, 10.0, {1, 333, 1115, 4097, 64, 2048, 7});
    EXPECT_NEAR(static_cast<double>(r.mixerFrames), 220500.0, 2.0);
}

TEST(DeviceRateAdapter, A22050HzAssetKeepsItsDurationAt48000) {
    const PullResult r = PullSeconds(48000, 2.0, {1115});
    // One second of asset == 48000 device frames (+/- the resampler's edge).
    EXPECT_NEAR(static_cast<double>(r.audibleFrames), 48000.0, 4.0);
}

TEST(DeviceRateAdapter, A22050HzAssetKeepsItsDurationAt44100) {
    const PullResult r = PullSeconds(44100, 2.0, {1024});
    EXPECT_NEAR(static_cast<double>(r.audibleFrames), 44100.0, 4.0);
    EXPECT_NEAR(static_cast<double>(r.mixerFrames), 44100.0, 2.0);
}

TEST(DeviceRateAdapter, EqualRatesAreABitExactPassThrough) {
    AudioMixer a, b;
    OneSecondAsset assetA, assetB;
    assetA.startOn(a);
    assetB.startOn(b);
    DeviceRateAdapter adapter;
    ASSERT_TRUE(adapter.init(&a, 22050, 22050));
    EXPECT_TRUE(adapter.isPassthrough());
    std::vector<float> viaAdapter(512 * 2), direct(512 * 2);
    for (int i = 0; i < 50; ++i) {
        adapter.render(viaAdapter.data(), 512);
        b.render(direct.data(), 512);
        ASSERT_EQ(viaAdapter, direct);
    }
    EXPECT_EQ(adapter.mixerFramesPulled(), 50u * 512u);
}

// ─── 3. the backend on miniaudio's null device ──────────────────────────────

namespace {

// fd 1 to a temp file while the logging drain thread writes there (the T31
// idiom, as test_vendor_music_config_warn.cpp).
class StdoutCapture {
public:
    StdoutCapture() {
        char tmpl[] = "/tmp/willeq_test_b1069_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) close(fd);
        path_ = tmpl;
        fflush(stdout);
        int file_fd = open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        saved_ = dup(1);
        dup2(file_fd, 1);
        close(file_fd);
    }
    ~StdoutCapture() {
        fflush(stdout);
        if (saved_ >= 0) {
            dup2(saved_, 1);
            close(saved_);
        }
        unlink(path_.c_str());
    }
    std::string waitFor(const std::string& needle) const {
        std::string text;
        for (int i = 0; i < 40; ++i) {
            FlushThreadLog();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            std::ifstream in(path_);
            std::stringstream ss;
            ss << in.rdbuf();
            text = ss.str();
            if (text.find(needle) != std::string::npos) break;
        }
        return text;
    }

private:
    std::string path_;
    int saved_ = -1;
};

}  // namespace

TEST(MiniaudioBackendDeviceRate, Requested48000OpensAt48000AndResamples) {
    AudioMixer mixer;
    MiniaudioBackend backend;
    backend.setUseNullBackend(true);
    ASSERT_TRUE(backend.init(&mixer, AudioMixer::SAMPLE_RATE));
    EXPECT_EQ(backend.configuredSampleRate(), 48000u);
    EXPECT_TRUE(backend.isResampling());
    backend.stop();
}

TEST(MiniaudioBackendDeviceRate, ZeroTakesTheNativeRate) {
    AudioMixer mixer;
    MiniaudioBackend backend;
    backend.setUseNullBackend(true);
    backend.setDeviceSampleRate(0);
    ASSERT_TRUE(backend.init(&mixer, AudioMixer::SAMPLE_RATE));
    EXPECT_EQ(backend.requestedDeviceSampleRate(), 0u);
    EXPECT_GT(backend.configuredSampleRate(), 0u);
    EXPECT_EQ(backend.configuredSampleRate(), backend.internalSampleRate())
        << "0 must mean 'whatever the device runs at', not a fixed rate";
    backend.stop();
}

TEST(MiniaudioBackendDeviceRate, MixerRateRequestIsThePreB1069PassThrough) {
    AudioMixer mixer;
    MiniaudioBackend backend;
    backend.setUseNullBackend(true);
    backend.setDeviceSampleRate(AudioMixer::SAMPLE_RATE);
    ASSERT_TRUE(backend.init(&mixer, AudioMixer::SAMPLE_RATE));
    EXPECT_EQ(backend.configuredSampleRate(), 22050u);
    EXPECT_FALSE(backend.isResampling());
    backend.stop();
}

TEST(MiniaudioBackendDeviceRate, InitLogNamesRequestedConfiguredAndNativeRate) {
    const int savedLevel = GetLogLevel();
    SetLogLevel(LOG_INFO);
    std::string text;
    {
        StdoutCapture cap;
        AudioMixer mixer;
        MiniaudioBackend backend;
        backend.setUseNullBackend(true);
        ASSERT_TRUE(backend.init(&mixer, AudioMixer::SAMPLE_RATE));
        backend.start();
        text = cap.waitFor("Audio device: requested=");
        backend.stop();
    }
    SetLogLevel(savedLevel);
    EXPECT_NE(text.find("Audio device: requested=48000Hz configured=48000Hz"), std::string::npos)
        << "the one grep-able init line is missing. Captured: " << text;
    EXPECT_NE(text.find("mixer=22050Hz resample=linear"), std::string::npos) << text;
    EXPECT_NE(text.find("native="), std::string::npos) << text;
    EXPECT_NE(text.find("backend=Null"), std::string::npos) << text;
}

// The whole chain below main.cpp: the published rate -> AudioManager ->
// MiniaudioBackend, read off the init line. AudioManager opens the DEFAULT
// backend list here (a real device where there is one; miniaudio's null
// device last), so only the requested/mixer fields are asserted.
TEST(MiniaudioBackendDeviceRate, AudioManagerOpensThePublishedRate) {
    const uint32_t saved = EQT::Audio::configuredDeviceSampleRate();
    const int savedLevel = GetLogLevel();
    SetLogLevel(LOG_INFO);
    EQT::Audio::setConfiguredDeviceSampleRate(44100);
    std::string text;
    bool loopback = true;
    {
        StdoutCapture cap;
        EQT::Audio::AudioManager mgr;
        ASSERT_TRUE(mgr.initialize(::testing::TempDir(), /*forceLoopback=*/false));
        loopback = mgr.isLoopbackMode();
        text = cap.waitFor("Audio device: requested=");
        mgr.shutdown();
    }
    SetLogLevel(savedLevel);
    EQT::Audio::setConfiguredDeviceSampleRate(saved);
    if (loopback) GTEST_SKIP() << "no miniaudio device opened (loopback fallback)";
    EXPECT_NE(text.find("Audio device: requested=44100Hz configured=44100Hz"), std::string::npos)
        << text;
    EXPECT_NE(text.find("mixer=22050Hz resample=linear"), std::string::npos) << text;
}

#endif  // WITH_AUDIO
