// B977 (2026-10-01): audio bytes are charged to the `audio` ledger category
// whichever thread asks for them.
//
// Before B977 only AudioManager::initialize(), ZoneAudioManager::loadZone() and
// the RDP pump thread opened a MemoryScope(Audio). Everything audio loaded
// LATER was charged to the CALLER's scope: on the client that is the game
// thread, i.e. `game_state`. Two things load later:
//
//  * the custom SoundFont (synthus2.sf2). T504 deferred it from init() to the
//    first selectSoundFont(Custom), which AudioManager::playMusic() makes for
//    the 31 Velious/Luclin XMI files. tsf holds its samples as float, so the
//    12,036,844-byte file becomes ~24 MB. MEASURED on the Orange Pi (B854
//    sweep, velketor): `audio` 1,625,664 B at ready AND at steady_60s, while
//    `game_state` went 4,850,304 -> 29,661,808 B across the load.
//  * every WAV, decoded to float on its first play (AudioManager's buffer cache;
//    the SfxManager's second copy of the same samples was B989's defect).
//
// So the `audio` budget (memoryLedgerBudgets.audio) bounded only the part of
// audio that exists at startup. Each case below opens a GameState scope -- the
// game thread's -- calls the public entry point the game thread calls, and
// asserts the bytes landed in `audio` and NOT in `game_state`.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "client/audio/audio_manager.h"
#include "client/audio/zone_audio_manager.h"
#include "client/core/memory_ledger.h"

namespace {

using eqt::memory::Category;

std::string EqClientPath() {
#ifdef WILLEQ_EQ_CLIENT_PATH
    return WILLEQ_EQ_CLIENT_PATH;
#else
    return std::string();
#endif
}

bool Have(const std::string& name) {
    const std::string p = EqClientPath();
    return !p.empty() && std::filesystem::exists(p + "/" + name);
}

uint64_t Live(Category c) { return eqt::memory::stats(c).liveBytes; }

// A per-process sound index cache, so this binary neither reads nor rewrites
// the shared build/bin/cache copy (the B845 rule: a set WILLEQ_CACHE_DIR is
// authoritative).
class PrivateCacheDir {
public:
    PrivateCacheDir() {
        static std::atomic<uint64_t> counter{0};
        dir_ = std::filesystem::temp_directory_path() /
               ("b977_audio_attr_" + std::to_string(::getpid()) + "_" +
                std::to_string(counter.fetch_add(1)));
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
        std::filesystem::create_directories(dir_, ec);
        ::setenv("WILLEQ_CACHE_DIR", dir_.c_str(), 1);
    }
    ~PrivateCacheDir() {
        std::error_code ec;
        ::unsetenv("WILLEQ_CACHE_DIR");
        std::filesystem::remove_all(dir_, ec);
    }
private:
    std::filesystem::path dir_;
};

}  // namespace

// THE RED for the music half: before B977 the whole font landed in game_state.
TEST(AudioLedgerAttribution, CustomSoundFontLoadedByPlayMusicIsChargedToAudio) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!Have("synthusr.sf2") || !Have("synthus2.sf2") || !Have("velketor.xmi")) {
        GTEST_SKIP() << "no synthusr.sf2 / synthus2.sf2 / velketor.xmi under " << EqClientPath();
    }

    PrivateCacheDir cache;
    EQT::Audio::AudioManager am;
    ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));

    uint64_t audioBefore = 0, gsBefore = 0, audioAfter = 0, gsAfter = 0;
    {
        eqt::memory::MemoryScope gameThread(Category::GameState);
        audioBefore = Live(Category::Audio);
        gsBefore = Live(Category::GameState);
        // velketor.xmi is on AudioManager's synthus2 list: this is the call that
        // loads the custom font on demand (T504).
        am.playMusic(EqClientPath() + "/velketor.xmi", /*loop=*/true, 0);
        audioAfter = Live(Category::Audio);
        gsAfter = Live(Category::GameState);
    }
    const int64_t audioGrew = static_cast<int64_t>(audioAfter) - static_cast<int64_t>(audioBefore);
    const int64_t gsGrew = static_cast<int64_t>(gsAfter) - static_cast<int64_t>(gsBefore);
    std::cout << "[measured] playMusic(velketor.xmi): audio +" << audioGrew
              << " B, game_state +" << gsGrew << " B\n";
    // The font alone is ~24 MB of float samples; 8 MiB is a floor no
    // plausible decode of a 12 MB file could miss.
    EXPECT_GE(audioGrew, 8ll * 1024 * 1024)
        << "the custom SoundFont load was not charged to `audio`";
    EXPECT_LT(gsGrew, 1ll * 1024 * 1024)
        << "the custom SoundFont load was charged to the CALLER's category (game_state)";
    am.shutdown();
}

// The SFX half: a WAV decoded on its first play, from the game thread.
TEST(AudioLedgerAttribution, SoundDecodedOnFirstPlayIsChargedToAudio) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!Have("snd1.pfs")) GTEST_SKIP() << "no snd1.pfs under " << EqClientPath();

    PrivateCacheDir cache;
    EQT::Audio::AudioManager am;
    ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
    const size_t loadedBefore = am.getLoadedSoundCount();

    uint64_t audioBefore = 0, gsBefore = 0, audioAfter = 0, gsAfter = 0;
    {
        eqt::memory::MemoryScope gameThread(Category::GameState);
        audioBefore = Live(Category::Audio);
        gsBefore = Live(Category::GameState);
        // Ambient/weather WAVs of the kind a ZoneSoundEmitter plays by name on
        // the game thread; any one of them that exists in the archives will do.
        for (const char* name : {"thunder1.wav", "thunder2.wav", "wind_lp1.wav",
                                 "steamlp.wav", "darkwds1.wav", "fire_lp.wav"}) {
            am.playSoundByName(name, glm::vec3(1.0f, 2.0f, 3.0f));
        }
        audioAfter = Live(Category::Audio);
        gsAfter = Live(Category::GameState);
    }
    if (am.getLoadedSoundCount() == loadedBefore) {
        GTEST_SKIP() << "none of the probe WAVs is in this client's archives";
    }
    const int64_t audioGrew = static_cast<int64_t>(audioAfter) - static_cast<int64_t>(audioBefore);
    const int64_t gsGrew = static_cast<int64_t>(gsAfter) - static_cast<int64_t>(gsBefore);
    std::cout << "[measured] playSoundByName x" << (am.getLoadedSoundCount() - loadedBefore)
              << ": audio +" << audioGrew << " B, game_state +" << gsGrew << " B\n";
    EXPECT_GT(audioGrew, 0) << "the decoded WAVs were not charged to `audio`";
    // A few hundred bytes of incidental game-thread work are allowed; a
    // decoded WAV is tens of kilobytes.
    EXPECT_LT(gsGrew, 4096) << "the decoded WAVs were charged to game_state";
    am.shutdown();
}
