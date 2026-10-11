// B990 (issue #309) + B1014 (issue #335): the custom orchestral SoundFont
// (synthus2.sf2, 12,036,844 B on disk) is loaded on demand by the first
// Velious/Luclin XMI -- T504 deferred it from startup to
// MidiPlayer::selectSoundFont(Custom) -- and B977 measured it was NEVER
// released before B990: 24,162,944 B (23.04 MiB) of float samples lived for
// the rest of the session, 84 % of the `audio` category's steady-state worst
// (velketor, x86; refactor/B977/audio_ledger_census.tsv).
//
// B990's original fix released the font in MusicPlayer::stop() -- "the one
// place a song ends" -- but MusicPlayer::play() calls stop() FIRST, and
// AudioManager::playMusic selects the font BEFORE play(), so a custom-font
// song that FOLLOWED any playing song had its font freed by the very next
// call and played on GM. B1014 moved the release to a real end only: the
// public MusicPlayer::stop() (a song ending with nothing following) and the
// fade's completion. The font's lifetime is now: resident while a custom-font
// song plays, released at the mechanism that really ends a song -- and a
// following custom-font song reloads it (the T504 on-demand path, unchanged).
// These tests pin that lifetime at both mechanisms AND assert the BEHAVIOUR:
// the active font the next song actually plays on (getActiveSoundFont()), not
// only isCustomSoundFontResident() -- the proxy assertion is exactly why
// B990's defect got through review.
//
// Why the release points are thread-safe (the T248 render-barrier contract,
// include/client/audio/render_barrier.h):
//   * Immediate stop (MusicPlayer::stop(0.0)): the channel is freed first,
//     and AudioMixer::freeChannel() waits for the mixer to quiesce, so no
//     render pass is in flight when the release runs -- and since B1014 it
//     runs on the game thread (this thread), never on the audio thread.
//   * Faded stop (stopMusic(fade>0)): the stop is DEFERRED to the render
//     callback. The callback's MidiPlayer::stop() is safe from that thread
//     (a re-entrant waitQuiescent returns immediately), but the RELEASE is
//     only a FLAG set there: the audio thread never frees, and the game
//     thread frees it on the next public MusicPlayer call or the audio tick
//     (drainPendingFontRelease, which the faded case below drives directly).
//     That is what B1014 changed from B990 -- the old code tsf_closed on the
//     audio thread while the game thread could be in
//     selectSoundFont(Custom) or MidiPlayer::play().
//
// We assert through isCustomSoundFontResident() (the tsf handle the card
// names null) AND through the ledger's `audio` category live bytes, because
// that category is what the live ledger leg (criterion 3) gates on -- the
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

#include "asset_requirement.h"
#include "client/audio/audio_manager.h"
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

// B990/BOARD residual, confirmed: the `au.soundfont_custom` owner row keeps
// 2,976 B / 32 blocks live in every run (B990 census, GM-only qeynos2 legs
// included). The allocation charged there is NOT selectSoundFont bookkeeping
// (it allocates nothing: the custom font is loaded into a slot that
// selectSoundFont(Custom) fills on demand). It is the first-call
// construction of getSoundFontTypeForFile()'s function-local
// `static const std::unordered_set<std::string> synthus2Files`
// (audio_manager.cpp) -- 31 Velious/Luclin names (8 + 23), 31 node
// allocations plus the bucket array, charged to whatever scope first calls
// it. In the live client that first call is playMusic's selectSoundFont
// block, wrapped in the `au.soundfont_custom` StartupScope -- hence the row.
// This test measures the static's construction in exactly that scope and
// pins the byte/block census, so the residual is named by a live number,
// not by inference. Declared FIRST so that in a plain run this is the first
// call in the process (delta = the full 2,976 B / 32 blocks); under
// --gtest_repeat or after a later-built binary it is already built, and the
// call must then allocate nothing (delta = 0) -- both halves pinned.
TEST(SoundFontRelease, SoundFontCustomScopeFirstCallIsTheSynthus2NameSet) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";

    // Constructed OUTSIDE the scope: its own allocations must not enter the
    // measurement.
    EQT::Audio::AudioManager am;
    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    const auto baselineStats = eqt::memory::stats(eqt::memory::Category::Audio);
    const uint64_t baseline = baselineStats.liveBytes;
    const uint64_t baselineBlocks = baselineStats.liveBlocks;
    // A qeynos stem: not on the synthus2 list, so the call builds the static
    // and answers GM without loading anything (the same shape as the live
    // first playMusic of a classic zone).
    ASSERT_EQ(EQT::Audio::SoundFontType::GM,
              am.getSoundFontTypeForFile("/x/qeynos.xmi"));
    const auto live = eqt::memory::stats(eqt::memory::Category::Audio);
    const uint64_t delta = live.liveBytes - baseline;
    const uint64_t blocksDelta = live.liveBlocks - baselineBlocks;
    std::cout << "[measured] getSoundFontTypeForFile call: "
              << delta << " B / " << blocksDelta << " blocks"
              << " (B990 census residual: 2,976 B / 32 blocks)\n";
    if (delta > 0) {
        // This call BUILT the static (the first call in the process): the
        // whole residual is its, and it must be exactly the census number.
        EXPECT_EQ(delta, 2976u)
            << "the residual charged under au.soundfont_custom is the "
               "synthus2 name set's construction (31 names); a different "
               "size means a different allocation is now charged there";
        EXPECT_EQ(blocksDelta, 32u)
            << "31 names = 31 node blocks + 1 bucket-array block";
    } else {
        // The static was already built (a repeat, or an earlier test): this
        // call allocates nothing, which is exactly what the GM-only qeynos2
        // legs show -- the 2,976/32 appears once, on first playMusic.
        EXPECT_EQ(delta, 0u)
            << "a getSoundFontTypeForFile call after the first must allocate "
               "nothing (the name set is function-local static)";
    }
}

// THE RED (immediate stop). At the B990 base tip this fails: playing a
// custom-font XMI loads synthus2.sf2, but the immediate-stop path
// (MusicPlayer::stop(0.0) -- what AudioManager::stopMusic does for region-exit
// and music-off) never releases it, so the font is still resident after stop.
TEST(SoundFontRelease, ImmediateStopReleasesTheCustomFont) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    eqt::test::requireEqClientAssets(EqPath());
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
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::Custom)
        << "the song that was just played must play on the font it selected";

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
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM)
        << "releasing the custom font puts the player back on GM";
    EXPECT_LE(after, kReleasedBound)
        << "stopMusic leaked " << after
        << " B of the custom SoundFont (B977 measured 24,162,944 B at this point)";

    // Reload: a following custom-font song re-loads it (T504 unchanged), then
    // stopping that song releases it again.
    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);
    ASSERT_TRUE(m.midi.isCustomSoundFontResident())
        << "a subsequent custom-font song must reload the font (the T504 on-demand path)";
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true));
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::Custom)
        << "the RELOAD must be the font the next song plays on -- resident "
        << "without being active is exactly the proxy B990 got through on";
    m.mp.stop(0.0f);
    EXPECT_FALSE(m.midi.isCustomSoundFontResident());
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM);

    pump.stop();
}

// B1014 probe 1 (issue #335): the AudioManager::playMusic order --
// selectSoundFont(type) THEN MusicPlayer::play(), whose first act is stop().
// GM song -> custom song: at the B990 tip the stop inside play() freed the
// Custom font playMusic had just loaded (resident=0 active=GM), and the
// custom song played on the GM font.
TEST(SoundFontRelease, CustomSongStartedWhileGmSongPlays) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    eqt::test::requireEqClientAssets(EqPath());
    if (!HaveCustomFont()) GTEST_SKIP() << "no synthus2/GM font under " << EqPath();
    if (!HaveXmi("velketor")) GTEST_SKIP() << "no velketor.xmi under " << EqPath();
    if (!HaveXmi("qeynos2")) GTEST_SKIP() << "no qeynos2.xmi under " << EqPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    MusicMechanism m;
    ASSERT_TRUE(m.init(EqPath()));
    RenderPump pump(m.mixer);
    pump.start();

    ASSERT_TRUE(m.mp.play(Abs("qeynos2.xmi"), /*loop=*/true));          // GM song playing
    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);          // playMusic(velketor) step 1
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true));         // playMusic step 2
    std::cout << "[audit] after GM->custom: resident=" << m.midi.isCustomSoundFontResident()
              << " active=" << (m.midi.getActiveSoundFont() == EQT::Audio::SoundFontType::Custom ? "Custom" : "GM") << "\n";
    EXPECT_TRUE(m.midi.isCustomSoundFontResident())
        << "playMusic selected the custom font one call before play(); the "
           "stop inside play() must not have freed it";
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::Custom)
        << "the custom song is playing on the GM font -- B1014's defect";

    m.mp.stop(0.0f);
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "a song that really ends still releases the font (B990 kept)";
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM);

    pump.stop();
}

// B1014 probe 2 (issue #335): the setDayNight / music-region order --
// stopMusic(1.0) (a FADE) then playMusic of the next track of the same custom
// XMI (zone_audio_manager.cpp:136-137). At the B990 tip the fade-completion
// release ran on the audio thread and freed the Custom font playMusic had
// just reselected (same resident=0 active=GM as probe 1).
TEST(SoundFontRelease, CustomTrackSwitchWhileCustomPlays) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    eqt::test::requireEqClientAssets(EqPath());
    if (!HaveCustomFont()) GTEST_SKIP() << "no synthus2/GM font under " << EqPath();
    if (!HaveXmi("velketor")) GTEST_SKIP() << "no velketor.xmi under " << EqPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    MusicMechanism m;
    ASSERT_TRUE(m.init(EqPath()));
    RenderPump pump(m.mixer);
    pump.start();

    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true, 0));
    ASSERT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::Custom);

    m.mp.stop(1.0f);                                             // ZoneAudioManager::setDayNight
    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);   // playMusic(velketor, track 1)
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true, 1));
    // Let the audio thread run past the faded song's fade completion (1.0 s
    // of audio = ~43 render periods at 512 frames). This is the window where
    // B990's audio-thread release fired: the fade completes while track 1
    // plays, so B990 tsf_closed the font track 1 is using. The fixed code
    // only FLAGS the end there; the drain guard refuses to free while track
    // 1 plays, and the flag stays for the song's own real end.
    const auto pumpUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < pumpUntil) {
        std::this_thread::yield();
    }
    std::cout << "[audit] after custom->custom: resident=" << m.midi.isCustomSoundFontResident()
              << " active=" << (m.midi.getActiveSoundFont() == EQT::Audio::SoundFontType::Custom ? "Custom" : "GM") << "\n";
    EXPECT_TRUE(m.midi.isCustomSoundFontResident())
        << "the faded stop plus the next playMusic must not leave the custom "
           "font released out from under its own next song";
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::Custom)
        << "the next track is playing on the GM font -- B1014's defect";

    // The song really ends here. (It loops, so a natural end never comes:
    // the public stop is the real end, and it drains the pending flag the
    // faded stop left behind.)
    m.mp.stop(0.0f);
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "a song that really ends still releases the font (B990 kept)";
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM);

    pump.stop();
}

// B1014 (issue #335): a FAILED custom-font reload (the font file gone at
// select time) is a defined, logged outcome -- the player stays on GM, plays
// the song on GM, does not crash and does not leave a half-open instance.
TEST(SoundFontRelease, FailedReloadLeavesThePlayerOnGm) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    eqt::test::requireEqClientAssets(EqPath());
    if (!HaveCustomFont()) GTEST_SKIP() << "no synthus2/GM font under " << EqPath();
    if (!HaveXmi("velketor")) GTEST_SKIP() << "no velketor.xmi under " << EqPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    MusicMechanism m;
    ASSERT_TRUE(m.init(EqPath()));
    RenderPump pump(m.mixer);
    pump.start();

    // Get into the "custom font resident, song playing" state first.
    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true));
    ASSERT_TRUE(m.midi.isCustomSoundFontResident());
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::Custom);

    // The font file is gone at the next selection (a corrupt/incomplete EQ
    // install): point the on-demand load at a path that does not exist.
    m.mp.stop(0.0f);
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "the previous song's end already released the font";
    m.midi.setCustomSoundFontPath("/nonexistent/b1014-missing.sf2");
    m.midi.selectSoundFont(EQT::Audio::SoundFontType::Custom);  // must FAIL
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "a failed load must not leave a half-open instance";
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM)
        << "a failed custom load stays on GM (the T504 failure path)";

    // The song must still play -- on GM -- with a defined, logged outcome.
    ASSERT_TRUE(m.mp.play(Abs("velketor.xmi"), /*loop=*/true));
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM)
        << "the song plays on GM after the failed reload, not on a freed font";

    m.mp.stop(0.0f);
    EXPECT_FALSE(m.midi.isCustomSoundFontResident());
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM);

    pump.stop();
}

// A GM-font song must NEVER load the custom font, even though the font is
// available. It is resident exactly while a song that needs it is playing,
// never as session-long state.
TEST(SoundFontRelease, GmSongNeverLoadsTheCustomFont) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    eqt::test::requireEqClientAssets(EqPath());
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
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM)
        << "a GM-font song plays on GM";
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "a GM-font XMI must not load the custom SoundFont";
    EXPECT_LE(AudioLive() - baseline, kReleasedBound)
        << "a GM-font song retained custom-territory bytes it never loaded";
    m.mp.stop(0.0f);
    EXPECT_FALSE(m.midi.isCustomSoundFontResident());
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM);

    pump.stop();
}

// THE RED (faded stop). The live region-exit (ZoneAudioManager: 1.0 s fade)
// and day/night (1.0 s) transitions stop the song with a FADE, so the stop is
// deferred to the render callback and the custom font is still resident when
// stop() RETURNS (the immediate test above does not cover it). This drives the
// fade to completion through a render pump -- the same
// AudioMixer::render() the miniaudio worker runs -- and requires the font to
// be released once the fade finishes. Since B1014 the fade completion only
// FLAGS the release on the audio thread; the game thread performs it (here
// through drainPendingFontRelease(), the same call the audio tick makes). At
// the B990 base tip the fade completion only calls MidiPlayer::stop(); it
// never releases the font.
TEST(SoundFontRelease, FadedStopReleasesTheCustomFontWhenTheFadeCompletes) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    eqt::test::requireEqClientAssets(EqPath());
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
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::Custom);

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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (m.mp.isPlaying() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_FALSE(m.mp.isPlaying()) << "the fade did not complete in time";

    // B1014: the audio thread only FLAGGED the release at fade completion;
    // the game thread performs it (AudioManager::update() drains every game
    // tick). The flag is stored AFTER the audio thread's MidiPlayer::stop(),
    // so a single drain right after isPlaying() turns false can land before
    // the flag is set -- poll the way the tick does, until the release lands.
    const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (m.midi.isCustomSoundFontResident() && std::chrono::steady_clock::now() < drainDeadline) {
        m.mp.drainPendingFontRelease();
        std::this_thread::yield();
    }
    pump.stop();

    // THE MECHANISM: once the song is actually ended, the font is gone.
    const uint64_t after = AudioLive() - baseline;
    std::cout << "[measured] audio live after fade completion: " << after
              << " bytes\n";
    EXPECT_FALSE(m.midi.isCustomSoundFontResident())
        << "the custom SoundFont was not released when the faded song ended -- "
        << after << " B held (B977 measured 24,162,944 B at steady_60s)";
    EXPECT_EQ(m.midi.getActiveSoundFont(), EQT::Audio::SoundFontType::GM)
        << "the player is back on GM once the font is released";
    EXPECT_LE(after, kReleasedBound);
}
