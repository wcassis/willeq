// B990 (issue #309): the custom orchestral SoundFont (synthus2.sf2, 12,036,844 B
// on disk) is loaded on demand by the first Velious/Luclin XMI -- T504 deferred
// it from startup to MidiPlayer::selectSoundFont(Custom) -- and B977 measured
// it is NEVER released: 24,162,944 B (23.16 MiB) of float samples live for the
// rest of the session, 84 % of the `audio` category's steady-state worst
// (velketor, x86; refactor/B977/audio_ledger_census.tsv).
//
// A free path EXISTS -- MidiPlayer::selectSoundFont(GM) closes the custom
// font (the T504 comment in midi_player.h) -- but nothing drives it after a
// custom-font song ends or the zone changes. These tests pin the lifetime at
// the MECHANISM that ends a song (MusicPlayer::stop and its fade completion):
// a custom-font song that STOPS must release the font, a following custom-font
// song reloads it (the T504 on-demand path, unchanged), and a GM-font song
// never loads it. The production stop path routes here: AudioManager::stopMusic
// -> MusicPlayer::stop (immediate) or its fade completion (faded), and the zone
// change / region-exit / music-off sites all funnel into one of the two.
//
// Why the two release points are thread-safe (the T248 render-barrier contract,
// include/client/audio/render_barrier.h):
//   * Immediate stop (MusicPlayer::stop(0.0)): the channel is freed, and
//     AudioMixer::freeChannel() waits for the mixer to quiesce, so no render
//     pass is in flight when stop() then selects GM.
//   * Faded stop (stopMusic(fade>0)): the stop is DEFERRED to the render
//     callback; the fade completes there and ALREADY calls MidiPlayer::stop()
//     from the render thread -- a teardown the barrier documents as safe
//     (a re-entrant waitQuiescent returns immediately). Releasing the font
//     there is safe the same way, and the NEXT render pass sees
//     playing_==false before it dereferences the freed pointer.
//
// We assert through isCustomSoundFontResident() (the tsf handle the card names
// null) AND through the ledger's `audio` category live bytes, because that
// category is what the live ledger leg (criterion 3) gates on -- the
// `au.soundfont_custom` owner row is only armed at a live recordPoint, so a
// unit test measures the same bytes through the category instead (exactly as
// tests/test_music_player_soundfont_lifetime.cpp does).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "client/audio/audio_mixer.h"
#include "client/audio/midi_player.h"
#include "client/audio/music_player.h"
#include "client/audio/xmi_decoder.h"
#include "client/core/memory_ledger.h"

namespace {

std::string EqPath() {
#ifdef WILLEQ_EQ_CLIENT_PATH
    return WILLEQ_EQ_CLIENT_PATH;
#else
    return std::string();
#endif
}

std::string Abs(const std::string& name) {
    return EqPath() + "/" + name;
}

bool HaveCustomFont() {
    const std::string p = EqPath();
    return !p.empty() && std::filesystem::exists(Abs("synthus2.sf2")) &&
           (std::filesystem::exists(Abs("synthusr.sf2")) ||
            std::filesystem::exists(Abs("1mgm.sf2")));
}

bool HaveXmi(const std::string& zone) {
    return !EqPath().empty() && std::filesystem::exists(Abs(zone + ".xmi"));
}

uint64_t AudioLive() {
    return eqt::memory::stats(eqt::memory::Category::Audio).liveBytes;
}

// A released custom font may leave behind the GM font, the current song's
// decoded MIDI (a few hundred KB) and tsf bookkeeping. 2 MiB is comfortably
// above any of that and far below the ~23 MiB custom font (B977: 24,162,944 B).
constexpr uint64_t kReleasedBound = 2ull * 1024 * 1024;

// The stand-in for the miniaudio worker: renders 512-frame periods
// (MiniaudioBackend's period size) back-to-back, the same drive
// tests/test_midi_stop_race.cpp uses for the render half of the T248 handshake.
class RenderPump {
public:
    explicit RenderPump(EQT::Audio::AudioMixer& mixer) : mixer_(mixer) {}
    void start() {
        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread([this] {
            std::vector<float> out(512 * 2);
            while (running_.load(std::memory_order_relaxed)) {
                mixer_.render(out.data(), 512);
            }
        });
    }
    void stop() {
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) thread_.join();
    }
    ~RenderPump() { stop(); }
private:
    EQT::Audio::AudioMixer& mixer_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// The full music mechanism: a dual MidiPlayer (GM + custom) and the MusicPlayer
// that plays XMI through it, wired exactly as AudioManager::initialize does
// (setMidiPlayer before initialize, so no fallback second font is loaded).
struct MusicMechanism {
    EQT::Audio::MidiPlayer midi;
    EQT::Audio::AudioMixer mixer;
    EQT::Audio::MusicPlayer mp;
    bool ready = false;

    bool init(const std::string& eqPath) {
        if (!midi.init(Abs("synthusr.sf2"), Abs("synthus2.sf2"),
                       EQT::Audio::AudioMixer::SAMPLE_RATE)) {
            return false;
        }
        mp.setMidiPlayer(&midi);
        mp.setMixer(&mixer);
        return mp.initialize(eqPath, "");
    }
};

}  // namespace

// THE RED (immediate stop). At the B990 base tip this fails: playing a
// custom-font XMI loads synthus2.sf2, but the immediate-stop path
// (MusicPlayer::stop(0.0) -- what AudioManager::stopMusic does for region-exit
// and music-off) never releases it, so the font is still resident after stop.
TEST(SoundFontRelease, ImmediateStopReleasesTheCustomFont) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!HaveCustomFont()) GTEST_SKIP() << "no synthus2/GM font under " << EqPath();
    if (!HaveXmi("velketor")) GTEST_SKIP() << "no velketor.xmi under " << EqPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    MusicMechanism m;
    ASSERT_TRUE(m.init(EqPath()));
    RenderPump pump(m.mixer);
    pump.start();

    ASSERT_FALSE(m.midi.isCustomSoundFontResident())
        << "the custom font must not be resident before any custom-font song (T504 deferral)";
    const uint64_t baseline = AudioLive();

    // Custom-font song: select Custom (as AudioManager::playMusic does for a
    // velketor XMI), then play it. The font loads on demand.
    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);
    ASSERT_TRUE(m.midi.isCustomSoundFontResident());
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true));

    const uint64_t whilePlaying = AudioLive() - baseline;
    std::cout << "[measured] audio live while the custom-font song plays: " << whilePlaying
              << " bytes (" << (whilePlaying / 1048576.0) << " MiB)\n";
    EXPECT_GT(whilePlaying, 1ull * 1024 * 1024)
        << "if the custom font is this cheap the release below proves nothing";

    // THE MECHANISM: stop the song. B977 measured 24,162,944 B still live at
    // this exact point on the unfixed client (velketor steady_60s).
    m.mp.stop(0.0f);
    const uint64_t after = AudioLive() - baseline;
    std::cout << "[measured] audio live after stop(0.0): " << after
              << " bytes\n";
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "the custom SoundFont was not released when the song stopped -- "
        << after << " B (au.soundfont_custom) held for the rest of the session";
    EXPECT_LE(after, kReleasedBound)
        << "stopMusic leaked " << after
        << " B of the custom SoundFont (B977 measured 24,162,944 B at this point)";

    // Reload: a following custom-font song re-loads it (T504 unchanged), then
    // stopping that song releases it again.
    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);
    ASSERT_TRUE(m.midi.isCustomSoundFontResident())
        << "a subsequent custom-font song must reload the font (the T504 on-demand path)";
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true));
    m.mp.stop(0.0f);
    EXPECT_FALSE(m.midi.isCustomSoundFontResident());

    pump.stop();
}

// A GM-font song must NEVER load the custom font, even though the font is
// available. It is resident exactly while a song that needs it is playing,
// never as session-long state.
TEST(SoundFontRelease, GmSongNeverLoadsTheCustomFont) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!HaveCustomFont()) GTEST_SKIP() << "no synthus2/GM font under " << EqPath();
    if (!HaveXmi("qeynos2")) GTEST_SKIP() << "no qeynos2.xmi under " << EqPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    MusicMechanism m;
    ASSERT_TRUE(m.init(EqPath()));
    RenderPump pump(m.mixer);
    pump.start();

    const uint64_t baseline = AudioLive();

    // GM-font song: qeynos2 is not on the synthus2 list, so the active font
    // stays GM and the custom font is never selected or loaded.
    ASSERT_FALSE(m.midi.isCustomSoundFontResident());
    ASSERT_TRUE(m.mp.play(Abs("qeynos2.xmi"), /*loop=*/true));
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "a GM-font XMI must not load the custom SoundFont";
    EXPECT_LE(AudioLive() - baseline, kReleasedBound)
        << "a GM-font song retained custom-territory bytes it never loaded";
    m.mp.stop(0.0f);
    EXPECT_FALSE(m.midi.isCustomSoundFontResident());

    pump.stop();
}

// THE RED (faded stop). The live region-exit (ZoneAudioManager: 1.0 s fade)
// and day/night (1.0 s) transitions stop the song with a FADE, so the stop is
// deferred to the render callback and the custom font is still resident when
// stop() RETURNS (the immediate test above does not cover it). This drives the
// fade to completion through a render pump -- the same
// AudioMixer::render() the miniaudio worker runs -- and requires the font to
// be released once the fade finishes. At the B990 base tip the fade completion
// only calls MidiPlayer::stop(); it never releases the font.
TEST(SoundFontRelease, FadedStopReleasesTheCustomFontWhenTheFadeCompletes) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!HaveCustomFont()) GTEST_SKIP() << "no synthus2/GM font under " << EqPath();
    if (!HaveXmi("velketor")) GTEST_SKIP() << "no velketor.xmi under " << EqPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    MusicMechanism m;
    ASSERT_TRUE(m.init(EqPath()));
    RenderPump pump(m.mixer);
    pump.start();

    const uint64_t baseline = AudioLive();

    // Custom-font song: select Custom, then play it.
    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);
    ASSERT_TRUE(m.midi.isCustomSoundFontResident());
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true));

    const uint64_t whilePlaying = AudioLive() - baseline;
    std::cout << "[measured] audio live while the custom-font song plays: " << whilePlaying
              << " bytes (" << (whilePlaying / 1048576.0) << " MiB)\n";
    EXPECT_GT(whilePlaying, 1ull * 1024 * 1024)
        << "if the custom font is this cheap the release below proves nothing";

    // Faded stop: the stop is deferred to the fade, so the font is STILL
    // resident immediately after this returns (the deferred-stop contract).
    m.mp.stop(0.5f);
    EXPECT_TRUE(m.midi.isCustomSoundFontResident())
        << "the deferred stop must keep the font resident until the fade finishes";

    // Pump until the fade completes (playing_ false). 0.5 s of audio at
    // 22050 Hz is ~24 render periods, so this is well within budget.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (m.mp.isPlaying() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_FALSE(m.mp.isPlaying()) << "the fade did not complete in time";
    pump.stop();

    // THE MECHANISM: once the song is actually ended, the font is gone.
    const uint64_t after = AudioLive() - baseline;
    std::cout << "[measured] audio live after fade completion: " << after
              << " bytes\n";
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "the custom SoundFont was not released when the faded song ended -- "
        << after << " B held (B977 measured 24,162,944 B at steady_60s)";
    EXPECT_LE(after, kReleasedBound);
}
