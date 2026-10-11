// B989 (#308): every played WAV is held TWICE as 32-bit float PCM.
//
// `AudioManager::bufferCache_` (ledger owner `au.sound_buffers`, the decode
// on the first play) and `SfxManager::cache_` (ledger owner `au.sfx_cache`,
// the second copy `SfxManager::preload()` used to make of the SAME samples)
// each held the decoded PCM of every sound that had been played, each capped
// separately at the preset's `soundCache` -- up to 16 MiB of duplicated
// decoded PCM the 1999 client did not carry twice (B977's census: x86
// befallen steady_60s, `au.sound_buffers` 2,322,320 B + `au.sfx_cache`
// 310,640 B).
//
// THE RED is the copy's category-level signature: every time playSound()
// found the sound in bufferCache_ but NOT in SfxManager::cache_, it CHARGED
// the `audio` category a second full copy of the already-decoded PCM. These
// cases play real archive sounds through the client's own paths
// (AudioManager::playSound -> loadSound -> SfxManager::preload ->
// SfxManager::play) and assert:
//
//   * a FRESH manager's first play of a sound grows the `audio` category by
//     the decode ONCE, not twice (the copy is the defect's measurable
//     signature);
//   * a sound already in its buffer cache plays back WITHOUT growing the
//     `audio` category (pre-B989 the resident-but-not-sfx-cached sound was
//     re-copied on every such play -- measured below);
//   * the preset's soundCache cap bounds the SINGLE cache of decoded SFX PCM:
//     after playing three distinct sounds under a cap smaller than the two
//     larger ones, at most the most recently used sound is resident (the LRU
//     behaviour T412 pinned for the sfx cache -- now that of the one cache).
//
// The live-leg owner rows (`au.sound_buffers` vs `au.sfx_cache`, quoted in
// refactor/B989/README.md) are the WILLEQ_LEDGER_OWNERS=audio lane's job; a
// bare test process does not run that mode. The gate never constructs
// AudioManager, so this is the ctest half.
#ifdef WITH_AUDIO

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "client/data_paths.h"

#include "client/audio/audio_manager.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_buffer.h"
#include "client/core/memory_ledger.h"
#include "client/willeq_database.h"

namespace EQT {
namespace Audio {
namespace {

using eqt::memory::Category;

// getSoundById() (and so playSound()) resolves sound ids through
// WilleqDatabase's global instance, exactly like the client's startup does.
// Without it, every id maps to "" and the sound silently never loads.
EQT::WilleqDatabase g_willeq_db;
bool g_willeq_db_available = false;

class WilleqDbEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        const char* env_path = std::getenv("WILLEQ_DB_PATH");
        const std::string db_path = env_path ? env_path : eqt::ResolveDataPath("data/willeq.db");
        std::ifstream probe(db_path);
        if (probe.good()) {
            probe.close();
            if (g_willeq_db.open(db_path)) {
                EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
                g_willeq_db_available = true;
            }
        }
    }
};
::testing::Environment* const g_willeq_db_env =
    ::testing::AddGlobalTestEnvironment(new WilleqDbEnvironment);

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

// A per-process sound index cache, so this binary neither reads nor rewrites
// the shared build/bin/cache copy (the B845 rule: a set WILLEQ_CACHE_DIR is
// authoritative) -- the same device as test_audio_ledger_attribution.cpp.
class PrivateCacheDir {
public:
    PrivateCacheDir() {
        static std::atomic<uint64_t> counter{0};
        dir_ = std::filesystem::temp_directory_path() /
               ("b989_sfx_once_" + std::to_string(::getpid()) + "_" +
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

uint64_t AudioLive() { return eqt::memory::stats(Category::Audio).liveBytes; }

}  // namespace

// THE RED: the second copy. A sound that is in the buffer cache but not yet
// in the SfxManager cache used to be COPIED into it on the very play that
// served it from cache -- the `audio` category grew by the full decoded PCM
// for a sound it already held.
TEST(SfxSinglePcmCache, ABufferCachedSoundPlaysBackWithoutCopying) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!g_willeq_db_available) GTEST_SKIP() << "willeq.db not found";
    if (!Have("snd1.pfs")) GTEST_SKIP() << "no snd1.pfs under " << EqClientPath();

    PrivateCacheDir cache;

    // Sound id 126 is one of the 17 common sounds the client preloads at
    // startup (audio_manager.cpp preloadCommonSounds) -- present in the
    // Titanium archives in every installation.
    constexpr uint32_t kSoundId = 126;
    EQT::Audio::AudioManager am;
    ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));

    // The decode, charged before the measurement window (and into the buffer
    // cache, which playSound() then serves from).
    auto buffer = am.getSoundBuffer(kSoundId);
    ASSERT_NE(buffer, nullptr) << "sound id 126 did not load";
    const size_t pcm = buffer->getMemorySize();
    ASSERT_GT(pcm, 16ll * 1024) << "a probe this small cannot separate a copy from overhead";

    // Play 1: buffer cache hit, sfx cache miss.
    const uint64_t before1 = AudioLive();
    {
        eqt::memory::MemoryScope audio(Category::Audio);  // the game thread's entry point
        am.playSound(kSoundId, glm::vec3(1.0f, 2.0f, 3.0f));
    }
    const uint64_t grew1 = AudioLive() - before1;
    std::cout << "[measured] first play of a buffer-cached sound: pcm=" << pcm
              << " audio +" << grew1 << " B\n";

    // Play 2: buffer cache hit, sfx cache hit -- nothing to decode, nothing
    // to copy. A non-zero delta here is the copy's second half of the
    // defect (the channel setup is a handful of bytes, not a full PCM).
    const uint64_t before2 = AudioLive();
    {
        eqt::memory::MemoryScope audio(Category::Audio);
        am.playSound(kSoundId, glm::vec3(1.0f, 2.0f, 3.0f));
    }
    const uint64_t grew2 = AudioLive() - before2;
    std::cout << "[measured] second play of the same sound: audio +" << grew2 << " B\n";
    am.shutdown();

    // THE RED: pre-B989 play 1 charged the second full copy (the sfx cache
    // entry) for a sound the buffer cache already held.
    EXPECT_LT(grew1, static_cast<uint64_t>(pcm) / 2)
        << "playing a buffer-cached sound copied its decoded PCM into a "
           "second cache (B989): audio grew by " << grew1 << " B for a "
           "sound of " << pcm << " B already resident";
    EXPECT_LT(grew2, 4096u)
        << "replaying a fully resident sound still grew `audio` by "
           << grew2 << " B";
}

// The FRESH-decode half: a manager whose caches are empty plays a sound. The
// decode lands in `audio` exactly ONCE. Pre-B989 the SAME play also copied
// the samples into the sfx cache, so the category grew by the PCM plus a
// full second copy.
TEST(SfxSinglePcmCache, AFreshDecodeIsChargedToAudioOnce) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!g_willeq_db_available) GTEST_SKIP() << "willeq.db not found";
    if (!Have("snd1.pfs")) GTEST_SKIP() << "no snd1.pfs under " << EqClientPath();

    PrivateCacheDir cache;

    // Phase 1: an uncapped manager learns the probe sound's exact decoded
    // size and warms the shared sound index.
    constexpr uint32_t kSoundId = 126;
    size_t pcm = 0;
    {
        EQT::Audio::AudioManager am;
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        auto buffer = am.getSoundBuffer(kSoundId);
        ASSERT_NE(buffer, nullptr) << "sound id 126 did not load";
        pcm = buffer->getMemorySize();
        am.shutdown();
    }
    ASSERT_GT(pcm, 16ll * 1024) << "a probe this small cannot separate a copy from overhead";

    // Phase 2: a FRESH manager. Its first play of the sound is the case under
    // test: ONE decode, charged ONCE. (The one-off first-play overhead --
    // opening the archive map, the db -- is bounded by the margin below; the
    // sound index was warmed in phase 1, same WILLEQ_CACHE_DIR.)
    {
        EQT::Audio::AudioManager am;
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        // A ceiling far above anything this test holds (two probe sounds):
        // it evicts nothing, but it turns ON the cache accounting --
        // getSoundBufferCacheBytes() tracks only while a cap is set.
        am.setMemoryConstraints(/*soundCacheBytes=*/pcm * 8, /*lazyPfs=*/false);

        // Warm the archive map OUTSIDE the window with a different common
        // sound (id 127), so the measured play is a decode, not an archive
        // open.
        ASSERT_NE(am.getSoundBuffer(127u), nullptr) << "sound id 127 did not load";

        const uint64_t audioBefore = AudioLive();
        const size_t buffersBefore = am.getSoundBufferCacheBytes();
        {
            eqt::memory::MemoryScope audio(Category::Audio);  // the game thread's entry point
            am.playSound(kSoundId, glm::vec3(1.0f, 2.0f, 3.0f));
        }
        const uint64_t audioAfter = AudioLive();
        const size_t buffersAfter = am.getSoundBufferCacheBytes();
        am.shutdown();

        const int64_t audioGrew = static_cast<int64_t>(audioAfter) - static_cast<int64_t>(audioBefore);
        const int64_t buffersGrew = static_cast<int64_t>(buffersAfter) - static_cast<int64_t>(buffersBefore);
        std::cout << "[measured] fresh decode of sound 126: pcm=" << pcm
                  << " audio +" << audioGrew
                  << " bufferCache +" << buffersGrew << " B\n";

        EXPECT_GE(buffersGrew, static_cast<int64_t>(pcm))
            << "the decoded PCM is not in the buffer cache at all";
        // Held twice, the `audio` total carries the PCM TWICE; held once,
        // once. The margin covers the one-off first-play overhead (the db
        // connection, the pfs file index read) but not a second full copy.
        EXPECT_LT(audioGrew, static_cast<int64_t>(pcm) + 512ll * 1024)
            << "the `audio` category grew by the decoded PCM plus a second "
               "copy -- the sound is in two caches (B989)";
    }
}

// The preset's soundCache cap bounds the SINGLE cache of decoded SFX PCM.
// Before B989 the cap ran on BOTH caches separately (up to 2x the cap of
// decoded PCM live); after it, one cache holds at most the cap. The LRU half
// (least-recently-used goes first, the most-recently-used survives) is the
// behaviour T412 pinned for the sfx cache -- now that of the one cache.
TEST(SfxSinglePcmCache, TheSoundCacheCapBoundsTheSingleCache) {
    if (!g_willeq_db_available) GTEST_SKIP() << "willeq.db not found";
    if (!Have("snd1.pfs")) GTEST_SKIP() << "no snd1.pfs under " << EqClientPath();

    PrivateCacheDir cache;

    constexpr uint32_t kIds[] = {126, 130, 131};

    // Phase 1 (no cap): learn the probe sounds' decoded sizes. A cap would
    // make the LRU evict during measurement, so sizes come from an uncapped
    // manager. This also proves the sounds exist in the archives.
    std::vector<uint32_t> ids;
    std::vector<size_t> sizes;
    {
        EQT::Audio::AudioManager am;
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        for (uint32_t id : kIds) {
            auto buffer = am.getSoundBuffer(id);
            if (buffer) {
                ids.push_back(id);
                sizes.push_back(buffer->getMemorySize());
            }
        }
        am.shutdown();
    }
    ASSERT_GE(ids.size(), 2u) << "too few probe sounds present in the archives";

    // A ceiling under the SECOND-smallest decoded size: after playing them
    // all, the cache can hold at most the smallest sound.
    size_t smallest = sizes[0], secondSmallest = sizes[0];
    for (size_t sz : sizes) {
        if (sz < smallest) {
            secondSmallest = smallest;
            smallest = sz;
        } else if (sz < secondSmallest) {
            secondSmallest = sz;
        }
    }
    ASSERT_LT(smallest, secondSmallest) << "the probe sounds need two distinct sizes";

    // Phase 2 (the cap): a FRESH manager with the ceiling set BEFORE the
    // first decode, so the LRU tracks every insertion and evicts as it goes.
    {
        EQT::Audio::AudioManager am;
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        am.setMemoryConstraints(/*soundCacheBytes=*/secondSmallest - 1, /*lazyPfs=*/false);
        ASSERT_EQ(am.getSoundBufferCacheMaxBytes(), secondSmallest - 1);

        for (size_t i = 0; i < ids.size(); ++i) {
            eqt::memory::MemoryScope audio(Category::Audio);
            am.playSound(ids[i], glm::vec3(0.0f, 0.0f, 0.0f));
        }

        // The LRU behaviour of the single cache: only the MOST recently used
        // sound is resident; the earlier ones were evicted (T412's rule, now
        // on the one cache). getSoundBuffer() re-decodes on a MISS, so the
        // resident COUNT is read before probing, and the probe is the most
        // recent sound only (a hit).
        const size_t resident = am.getSoundBufferCacheBytes();
        std::cout << "[measured] after playing " << ids.size()
                  << " sounds under cap " << (secondSmallest - 1)
                  << ": decoded PCM live=" << resident << " B\n";
        EXPECT_LE(resident, smallest)
            << "the decoded SFX PCM holds more than the soundCache cap allows -- "
               "the cap is not the single cache's ceiling";

        // Replaying the most recently used sound is a cache hit: it grows
        // `audio` by nothing. (A miss would re-decode it, which is how the
        // survivor's identity is proven.)
        const uint64_t audioBefore = AudioLive();
        {
            eqt::memory::MemoryScope audio(Category::Audio);
            am.playSound(ids.back(), glm::vec3(0.0f, 0.0f, 0.0f));
        }
        const uint64_t audioAfter = AudioLive();
        EXPECT_LT(audioAfter - audioBefore, 4096)
            << "replaying the most recently used sound decoded it again -- "
               "the LRU survivor is not resident";
        am.shutdown();
    }
}

}  // namespace Audio
}  // namespace EQT

#endif // WITH_AUDIO
