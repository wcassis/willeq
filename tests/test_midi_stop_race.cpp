// T248: MidiPlayer::stop() vs the audio render callback.
//
// The defect (TSAN, T220/T245 tours): the game thread's MidiPlayer::stop()
// (reached from play()/selectSoundFont()/the destructor) calls tsf_reset()
// on the TinySoundFont voice array while the miniaudio worker thread is
// still inside MidiPlayer::render() -> tsf_render_float()/tsf_channel_note_*
// on the same voices. Nothing ordered the two: stop() flipped playing_ and
// immediately reset the synth.
//
// This file is BOTH the ThreadSanitizer driver (run the binary from a
// -fsanitize=thread tree: BEFORE the fix it reports tsf.h / midi_player.cpp
// races, AFTER it must report none) and a normal unit test (the handshake's
// contract, checked without a sanitizer).
//
// The driver deliberately does not need an audio device or EQ data: a
// test-owned "render thread" hammers AudioMixer::render() back-to-back --
// the same call the miniaudio worker makes from miniaudioDataCallback() --
// with a music channel whose render callback delegates to
// MidiPlayer::render(), exactly as MusicPlayer::mixerRenderCallback does.
// The MIDI data is a tiny format-0 SMF generated in memory (16 channels of
// note-on/off pairs) so every render pass drives voices through
// tsf_channel_note_on/off, the frames TSAN flagged.

#include "client/audio/midi_player.h"
#include "client/audio/audio_mixer.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace EQT::Audio;

namespace {

std::string testSoundFontPath() {
    return std::string(WILLEQ_TEST_SF2_PATH);
}

// A format-0 Standard MIDI File: 96 ticks/quarter (default 120 bpm, so one
// tick is ~5.2 ms), a program change on every channel, then `rounds` rounds
// of one note-on/note-off pair per channel 12 ticks apart. Loops via
// MidiPlayer::play(loop=true), so it never runs out during the stress.
std::vector<uint8_t> makeStressMidi(int rounds) {
    std::vector<uint8_t> track;
    auto put = [&](std::initializer_list<uint8_t> bytes) {
        track.insert(track.end(), bytes.begin(), bytes.end());
    };
    for (uint8_t ch = 0; ch < 16; ++ch) {
        put({0x00, static_cast<uint8_t>(0xC0 | ch), static_cast<uint8_t>(ch * 4)});
    }
    for (int r = 0; r < rounds; ++r) {
        for (uint8_t ch = 0; ch < 16; ++ch) {
            const uint8_t key = static_cast<uint8_t>(48 + ((r + ch) % 24));
            put({0x00, static_cast<uint8_t>(0x90 | ch), key, 100});
            put({12,   static_cast<uint8_t>(0x80 | ch), key, 0});
        }
    }
    put({0x00, 0xFF, 0x2F, 0x00});  // end of track

    std::vector<uint8_t> smf = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,
        'M', 'T', 'r', 'k',
        static_cast<uint8_t>((track.size() >> 24) & 0xFF),
        static_cast<uint8_t>((track.size() >> 16) & 0xFF),
        static_cast<uint8_t>((track.size() >> 8) & 0xFF),
        static_cast<uint8_t>(track.size() & 0xFF),
    };
    smf.insert(smf.end(), track.begin(), track.end());
    return smf;
}

// What MusicPlayer::mixerRenderCallback does for the MIDI case, minus the
// fade bookkeeping: delegate the channel's buffer to MidiPlayer::render().
void midiChannelCallback(void* userData, float* buffer, int frameCount) {
    static_cast<MidiPlayer*>(userData)->render(buffer, frameCount);
}

class MidiStopRaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(testSoundFontPath())) {
            GTEST_SKIP() << "test SoundFont not found at: " << testSoundFontPath();
        }
        ASSERT_TRUE(player_.init(testSoundFontPath(), testSoundFontPath(), AudioMixer::SAMPLE_RATE));
        midi_ = makeStressMidi(32);

        channel_ = mixer_.allocMusicChannel();
        ASSERT_GE(channel_, 0);
        MixChannel* ch = mixer_.getChannel(channel_);
        ASSERT_NE(ch, nullptr);
        ch->renderCallback = &midiChannelCallback;
        ch->renderUserData = &player_;
        ch->active.store(true);
    }

    void TearDown() override {
        stopRenderThread();
    }

    // The stand-in for the miniaudio worker: renders 512-frame periods
    // (MiniaudioBackend's periodSizeInFrames) back-to-back with no sleep, so
    // the game-thread side below overlaps a render as often as possible.
    void startRenderThread() {
        renderRunning_.store(true);
        renderThread_ = std::thread([this] {
            std::vector<float> out(512 * 2);
            while (renderRunning_.load()) {
                mixer_.render(out.data(), 512);
                renderPasses_.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    void stopRenderThread() {
        renderRunning_.store(false);
        if (renderThread_.joinable()) renderThread_.join();
    }

    MidiPlayer player_;
    AudioMixer mixer_;
    std::vector<uint8_t> midi_;
    int channel_ = -1;
    std::atomic<bool> renderRunning_{false};
    std::atomic<uint64_t> renderPasses_{0};
    std::thread renderThread_;
};

}  // namespace

// The TSAN driver. Every iteration is one of the game-thread paths the
// T245 stacks showed reaching tsf_reset()/tml_free() under a live render:
// play() (which calls stop() first), a bare stop(), selectSoundFont()
// (stop() + active-pointer swap), and seekTo(). The render thread runs
// throughout. Under ThreadSanitizer this is the whole test: BEFORE the fix
// it produces tsf.h/midi_player.cpp race reports, AFTER it must produce
// none. Without a sanitizer it checks the observable contract: stop()
// returns with the player idle, and the render thread was actually
// rendering (not starved) while the churn happened.
TEST_F(MidiStopRaceTest, StopUnderLiveRenderThread) {
    startRenderThread();

    constexpr int kIterations = 300;
    for (int i = 0; i < kIterations; ++i) {
        ASSERT_TRUE(player_.play(midi_, /*loop=*/true, /*startTimeMs=*/(i % 5) * 40.0));
        // Let the render thread get inside render() with voices sounding.
        std::this_thread::sleep_for(std::chrono::microseconds(200 + (i % 7) * 150));
        switch (i % 4) {
            case 0: player_.stop(); break;
            case 1: player_.selectSoundFont(SoundFontType::Custom); break;
            case 2: player_.selectSoundFont(SoundFontType::GM); break;
            case 3: player_.seekTo(100.0); player_.stop(); break;
        }
        EXPECT_FALSE(player_.isPlaying());
    }

    stopRenderThread();
    EXPECT_GT(renderPasses_.load(), 0u);
}

// The observable contract of stop(): once it has returned, the synth is
// reset and a render pass produces silence without touching it. (The
// fixture's TearDown + member destruction also covers the destructor path
// MusicPlayer::shutdown()/AudioManager::shutdown() take -- ~MidiPlayer runs
// stop() with the mixer channel still wired to it.)
TEST_F(MidiStopRaceTest, PlayThenStopLeavesSilence) {
    startRenderThread();
    ASSERT_TRUE(player_.play(midi_, true));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    player_.stop();
    stopRenderThread();

    // After stop() has returned, a render pass must not touch the synth:
    // it must produce silence and leave the player stopped.
    std::vector<float> out(512 * 2, 1.0f);
    player_.render(out.data(), 512);
    for (float s : out) {
        ASSERT_EQ(s, 0.0f);
    }
    EXPECT_FALSE(player_.isPlaying());
    EXPECT_EQ(player_.getPlaybackTimeMs(), 0.0);
}

// ---------------------------------------------------------------------------
// The handshake primitive itself (include/client/audio/render_barrier.h).
// ---------------------------------------------------------------------------

#include "client/audio/render_barrier.h"

TEST(RenderBarrierTest, IdleReturnsImmediately) {
    RenderBarrier b;
    EXPECT_FALSE(b.inRender());
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(b.waitQuiescent());
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(50));
}

TEST(RenderBarrierTest, WaitsForInFlightRenderToExit) {
    RenderBarrier b;
    std::atomic<bool> renderDone{false};
    std::atomic<bool> released{false};

    std::thread render([&] {
        RenderBarrier::Scope s(b);
        while (!released.load()) std::this_thread::yield();
        renderDone.store(true);
    });
    while (!b.inRender()) std::this_thread::yield();

    // The teardown side must block until the render thread leaves.
    std::atomic<bool> waitReturned{false};
    std::thread teardown([&] {
        EXPECT_TRUE(b.waitQuiescent(std::chrono::milliseconds(2000)));
        waitReturned.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_FALSE(waitReturned.load()) << "waitQuiescent returned while a render pass was in flight";

    released.store(true);
    teardown.join();
    render.join();
    EXPECT_TRUE(renderDone.load());
    EXPECT_TRUE(waitReturned.load());
}

TEST(RenderBarrierTest, ReentrantFromRenderThreadDoesNotDeadlock) {
    // MusicPlayer's fade-out completion calls MidiPlayer::stop() from
    // inside the mixer callback; that must not wait for itself.
    RenderBarrier b;
    std::thread render([&] {
        RenderBarrier::Scope s(b);
        EXPECT_TRUE(b.waitQuiescent(std::chrono::milliseconds(2000)));
    });
    render.join();
}

TEST(RenderBarrierTest, TimesOutIfRenderNeverExits) {
    RenderBarrier b;
    std::atomic<bool> released{false};
    std::thread render([&] {
        RenderBarrier::Scope s(b);
        while (!released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (!b.inRender()) std::this_thread::yield();
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_FALSE(b.waitQuiescent(std::chrono::milliseconds(100)));
    EXPECT_GE(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(100));
    released.store(true);
    render.join();
}

// AudioMixer::freeChannel() must not return while render() is still inside
// the freed channel's callback -- that is the mixer half of the fix (the
// T245 reports at audio_mixer.cpp:36/48/54/59 were MusicPlayer::
// startMixerPlayback rewriting a channel the callback was reading).
namespace {
struct SlowChannel {
    std::atomic<bool> inside{false};
    std::atomic<int> calls{0};
    std::atomic<bool> hold{true};
};
void slowCallback(void* userData, float* buffer, int frameCount) {
    auto* sc = static_cast<SlowChannel*>(userData);
    sc->inside.store(true);
    sc->calls.fetch_add(1);
    // First call parks until the test releases it; later calls are quick.
    while (sc->hold.load()) std::this_thread::sleep_for(std::chrono::microseconds(200));
    std::memset(buffer, 0, frameCount * 2 * sizeof(float));
    sc->inside.store(false);
}
}  // namespace

TEST(AudioMixerRenderBarrierTest, FreeChannelWaitsForCallbackToLeave) {
    AudioMixer mixer;
    SlowChannel sc;
    const int handle = mixer.allocMusicChannel();
    ASSERT_GE(handle, 0);
    MixChannel* ch = mixer.getChannel(handle);
    ch->renderCallback = &slowCallback;
    ch->renderUserData = &sc;
    ch->active.store(true);

    std::atomic<bool> running{true};
    std::thread render([&] {
        std::vector<float> out(512 * 2);
        while (running.load()) mixer.render(out.data(), 512);
    });
    while (!sc.inside.load()) std::this_thread::yield();

    std::atomic<bool> freed{false};
    std::thread teardown([&] {
        mixer.freeChannel(handle);
        // On return the callback must have left and must never be
        // re-entered for this channel.
        EXPECT_FALSE(sc.inside.load());
        freed.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_FALSE(freed.load()) << "freeChannel returned while the callback was still inside";

    sc.hold.store(false);
    teardown.join();
    const int callsAtFree = sc.calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    running.store(false);
    render.join();
    EXPECT_EQ(sc.calls.load(), callsAtFree) << "callback was invoked again after freeChannel returned";
}
