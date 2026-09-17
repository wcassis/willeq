// T496 Leg D (board B830): the `audio` category's PEAK at startup.
//
// B817 took 24 MB off `audio`'s LIVE bytes. This is the other half: after that
// fix the Orange Pi still measured `audio` at **155.4 MiB peak for a 25.7 MiB
// steady state** -- a ~130 MiB transient during audio init. glibc took an arena
// to service it and never gave it back, which is the Pi's #2 bucket,
// `malloc_slack`, at 110 MiB (review/T496/pi/table_categories.tsv).
//
// The transient was `AudioManager::scanPfsArchives()`. It needs each archive's
// list of .wav names and nothing else, but it retained every opened archive
// whenever `lazyPfsLoading_` was false -- which it is on EVERY run at that
// point, because the preset's constraints arrive afterwards through
// `setMemoryConstraints()`. So the client loaded all seventeen `snd*.pfs`
// (124,397,766 bytes on disk), then freed them again when the constraint it
// was already configured with finally reached it.
//
// MEASURED across that one call, x86, orangepi preset:
//   live 25,277,200 -> 150,591,888,  peak -> 155,561,024.
//
// These tests assert PEAK bytes of `Category::Audio`, because a transient is
// invisible to a live-bytes assertion -- which is exactly how this survived
// B817's measurement.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <string>

#include "client/audio/audio_manager.h"
#include "client/graphics/eq/pfs.h"
#include "client/core/memory_ledger.h"
#include "client/data_paths.h"

namespace {

std::string EqClientPath() {
#ifdef WILLEQ_EQ_CLIENT_PATH
    return WILLEQ_EQ_CLIENT_PATH;
#else
    return std::string();
#endif
}

bool HaveSoundArchives() {
    const std::string p = EqClientPath();
    if (p.empty() || !std::filesystem::is_directory(p)) return false;
    for (const auto& e : std::filesystem::directory_iterator(p)) {
        const std::string n = e.path().filename().string();
        if (n.size() > 7 && n.compare(0, 3, "snd") == 0 &&
            n.compare(n.size() - 4, 4, ".pfs") == 0) {
            return true;
        }
    }
    return false;
}

// The two SoundFonts are ~25 MB of float samples and they are LIVE for the
// whole session, so they are in every figure below. On top of them, indexing
// the archives may hold at most ONE archive at a time; the largest snd*.pfs
// ships at 45,506,212 bytes, and a bound of 96 MiB leaves room for that plus
// the fonts plus decode staging while still being less than two thirds of the
// 155,561,024 B this replaced. It is deliberately NOT tight: the point is that
// the peak cannot be the SUM of the archives (124,397,766 B on disk) again.
constexpr uint64_t kAudioStartupPeakBound = 96ull * 1024 * 1024;

uint64_t AudioPeak() {
    return eqt::memory::stats(eqt::memory::Category::Audio).peakBytes;
}

// `scanPfsArchives()` returns IMMEDIATELY when `loadPfsIndexCache()` finds a
// warm `snd_index_cache.json`, and the resolver's last fallback is
// `<exe dir>/cache` -- i.e. `build/bin/cache/`, which any earlier client run in
// this tree has already populated. Measured: with that cache warm the peak is
// 26,279,360 B and BOTH cases below pass against the unfixed code. So each test
// points WILLEQ_CACHE_DIR at a fresh empty directory, and then PROVES the scan
// really ran by requiring the cache file to have been written -- a green that
// came from the short circuit is not a green.
//
// B845 (board B845): the original T496 fixture kept the shared
// `build/bin/cache/snd_index_cache.json` away by RENAMING it to a shared
// `.t496bak` in the ctor and restoring it in the dtor. Each ctest case is a
// separate process, so that rename was CROSS-PROCESS mutable state: under
// `ctest -j` a sibling's destructor could undo this process's hide between the
// hide and this process's read, the read then fell through to the warm shared
// file (ResolveCachePathForRead's `<exe dir>/cache` fallback) and
// short-circuited the scan -- `TheSteadyStateIsTheSoundFontsAndLittleElse`
// failed its own guard on T500's final -j4 run. The hide could not be made
// safe: every fallback directory is shared by definition, and 15 other test
// binaries (no WILLEQ_CACHE_DIR) read and write that same shared file.
//
// The root fix is in the resolver: a set WILLEQ_CACHE_DIR is now AUTHORITATIVE
// (see src/client/core/data_paths.cpp) -- read and write are confined to the
// directory, with no shared fallback. So this fixture only has to create a
// per-process directory, point the resolver at it, and PROVE the resolver
// honoured it. No rename, no restore, no cross-process state at all: the cases
// cannot share a cache with each other or with any other test process, and a
// run that somehow still hit a fallback directory fails its own ASSERT below
// instead of measuring nothing. A ctest RESOURCE_LOCK (tests/cmake/audio.cmake)
// is the second belt, not the fix.
class ColdSoundIndex {
public:
    ColdSoundIndex() {
        std::error_code ec;
        // Per-process: ctest gives each case its own process, so <pid> alone
        // already makes this unique among the three cases; the counter guards
        // a hypothetical same-process reuse.
        static std::atomic<uint64_t> counter{0};
        dir_ = std::filesystem::temp_directory_path() /
               ("t496_audio_peak_" + std::to_string(::getpid()) + "_" +
                std::to_string(counter.fetch_add(1)));
        std::filesystem::remove_all(dir_, ec);
        std::filesystem::create_directories(dir_, ec);
        ::setenv("WILLEQ_CACHE_DIR", dir_.c_str(), 1);
        // ASSERT the resolver actually used the directory we created -- for
        // BOTH directions. With B845's authoritative rule a set
        // WILLEQ_CACHE_DIR admits no fallback, so the write must land here;
        // and the read must report a MISS (the directory is empty) rather
        // than a shared build/bin/cache hit. If either ever resolves somewhere
        // else, the scan would short-circuit (or the proof file would land
        // elsewhere) and this case would pass without measuring anything.
        const std::string read = eqt::ResolveCachePathForRead("snd_index_cache.json");
        if (!read.empty()) {
            std::fprintf(stderr,
                         "B845: ResolveCachePathForRead hit '%s' even though the "
                         "per-process dir %s is the only candidate\n",
                         read.c_str(), dir_.c_str());
        }
        EXPECT_TRUE(read.empty())
            << "resolver did not confine the read to the per-process cache "
               "directory (it found '" << read << "'); the scan would be "
               "short-circuited by a shared warm cache -- refusing to measure";
        const std::string write = eqt::ResolveCachePathForWrite("snd_index_cache.json");
        EXPECT_EQ(write, dir_.string() + "/snd_index_cache.json")
            << "resolver would write the index to '" << write
            << "' instead of the per-process directory " << dir_
            << " -- the scanReallyRan() proof file would land elsewhere";
    }
    ~ColdSoundIndex() {
        std::error_code ec;
        ::unsetenv("WILLEQ_CACHE_DIR");
        std::filesystem::remove_all(dir_, ec);
    }
    // True once scanPfsArchives() has completed a real scan and saved its index.
    bool scanReallyRan() const {
        return std::filesystem::exists(dir_ / "snd_index_cache.json");
    }
private:
    std::filesystem::path dir_;
};

}  // namespace

// THE RED. Fails at the T496 Leg D base tip with peak ~155.5 MB.
TEST(AudioStartupPeak, IndexingTheSoundArchivesDoesNotHoldThemAllAtOnce) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!HaveSoundArchives()) GTEST_SKIP() << "no snd*.pfs under " << EqClientPath();

    ColdSoundIndex cold;
    eqt::memory::resetPeaks();
    {
        EQT::Audio::AudioManager am;
        // forceLoopback: no hardware device, and no audible output either way.
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        ASSERT_TRUE(cold.scanReallyRan())
            << "the archive scan was short-circuited by a warm index cache -- "
               "this case would pass without measuring anything";

        const uint64_t peak = AudioPeak();
        std::cout << "[measured] audio peak through initialize(): " << peak
                  << " bytes (" << (peak / 1048576.0) << " MiB)\n";
        EXPECT_LE(peak, kAudioStartupPeakBound)
            << "audio init peaked at " << (peak / 1048576.0)
            << " MiB -- the archive index is holding more than one archive";
        am.shutdown();
    }
}

// The converse, and the thing that made the transient invisible before: the
// steady state was already fine. A live-bytes bound alone would have passed
// against a 155 MiB peak, so this case exists to pin that the fix did not buy
// the peak back by leaking somewhere else.
TEST(AudioStartupPeak, TheSteadyStateIsTheSoundFontsAndLittleElse) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!HaveSoundArchives()) GTEST_SKIP() << "no snd*.pfs under " << EqClientPath();

    ColdSoundIndex cold;
    const uint64_t before = eqt::memory::stats(eqt::memory::Category::Audio).liveBytes;
    {
        EQT::Audio::AudioManager am;
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        ASSERT_TRUE(cold.scanReallyRan())
            << "the archive scan was short-circuited by a warm index cache";
        const uint64_t live =
            eqt::memory::stats(eqt::memory::Category::Audio).liveBytes - before;
        std::cout << "[measured] audio live after initialize(): " << live
                  << " bytes (" << (live / 1048576.0) << " MiB)\n";
        // Comfortably above the ~25 MB of SoundFont, far below the archives.
        EXPECT_LE(live, 48ull * 1024 * 1024)
            << "audio init RETAINED " << (live / 1048576.0) << " MiB";
        am.shutdown();
    }
}

// The index must not merely be CHEAP, it must be the SAME index. This pins
// `listFilenames()` against the `open()` + `getFilenames()` pair it replaced,
// over the real archives. MEASURED at the fix: 17 archives, 2,127 .wav names,
// zero mismatches.
TEST(AudioStartupPeak, ListFilenamesMatchesOpenPlusGetFilenames) {
    if (!HaveSoundArchives()) GTEST_SKIP() << "no snd*.pfs under " << EqClientPath();

    int archives = 0;
    size_t names = 0;
    for (const auto& e : std::filesystem::directory_iterator(EqClientPath())) {
        std::string n = e.path().filename().string();
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (!(l.size() > 7 && l.compare(0, 3, "snd") == 0 &&
              l.compare(l.size() - 4, 4, ".pfs") == 0)) {
            continue;
        }

        EQT::Graphics::PfsArchive a;
        ASSERT_TRUE(a.open(e.path().string())) << n;
        std::vector<std::string> viaOpen, viaList;
        a.getFilenames(".wav", viaOpen);
        EXPECT_TRUE(EQT::Graphics::PfsArchive::listFilenames(
            e.path().string(), ".wav", viaList)) << n;

        std::sort(viaOpen.begin(), viaOpen.end());
        std::sort(viaList.begin(), viaList.end());
        EXPECT_EQ(viaOpen, viaList) << "index drifted for " << n;
        ++archives;
        names += viaOpen.size();
    }
    EXPECT_GT(archives, 0);
    std::cout << "[measured] " << archives << " archives, " << names
              << " .wav names, indexes identical\n";
}
