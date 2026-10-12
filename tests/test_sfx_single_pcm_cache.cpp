// B1015 (#336, supersedes the B989 #308 RED): every played WAV is held in the
// ONE decoded-PCM cache of SFX PCM.
//
// B989 removed the COPY (`SfxManager::preload()` no longer makes a second
// `WavData`) but left TWO filename-keyed LRUs with two caps:
// `AudioManager::bufferCache_` and `SfxManager::cache_`. They evicted
// different victims (the sfx LRU skipped in-use entries, bufferCache_ did
// not), so bufferCache_ could evict a sound the sfx cache still held -- the
// next play RE-DECODED it, two decoded copies resident -- the cap bound a
// PROJECTION of the resident PCM (the sfx cache's 21,920 B of a cap case sat
// OUTSIDE getSoundBufferCacheBytes()), and when every older sfx entry was
// playing the sfx LRU evicted the entry it had JUST preloaded, so play()
// returned -1 and the requested sound silently never played. B1015 removed
// `SfxManager::cache_`: the buffer cache is the one cache of decoded SFX PCM
// under the one cap, with in-use pinning in it, and it never evicts the
// entry being inserted.
//
// These cases play real archive sounds through the client's own paths
// (AudioManager::playSound -> loadSound -> SfxManager::play) and assert:
//
//   * a FRESH manager's first play of a sound grows the `audio` category by
//     the decode ONCE, not twice (the copy was the B989 defect's measurable
//     signature; the re-decode duplicate is B1015's, and it has the same
//     signature);
//   * a sound already resident in the one cache plays back WITHOUT growing
//     the `audio` category;
//   * a NON-PLAYING fetch that evicts a sound, followed by its replay,
//     leaves the decoded PCM resident EXACTLY ONCE (B1015 finding 1);
//   * the preset's soundCache cap bounds the TOTAL resident decoded PCM
//     (B1015 finding 2 -- the B989 cap case only read
//     getSoundBufferCacheBytes(), which pre-fix was 11,072 B while the sfx
//     cache held another 21,920 B), and the survivor is the most recently
//     played;
//   * the entry being inserted is never the one evicted: with every older
//     entry playing, the requested sound is what ends up resident, so its
//     play is not the silent -1 (B1015 finding 3, pre-existing since T412).
//
// The live-leg owner rows (the `audio` category's live bytes at steady_60s,
// quoted in refactor/B989/README.md) are the WILLEQ_LEDGER_OWNERS=audio
// lane's job; a bare test process does not run that mode. The gate never
// constructs AudioManager, so this is the ctest half.
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

// ---------------------------------------------------------------------------
// B1015 (#336) RED: the auditor's probes as behaviour tests (AUDIT-WAVE7 on
// MR !131; the probe outputs are pasted in refactor/B1015/README.md). These
// assert the BEHAVIOUR the B989 tests missed: they read the TOTAL decoded PCM
// (the `audio` category, which every decode charges) and the actual buffer
// identity, not the single-cache projection getSoundBufferCacheBytes()
// reports. All three FAIL on the pre-fix two-cache code.
// ---------------------------------------------------------------------------

// B1015 finding 1: a NON-PLAYING fetch (the preloadCommonSounds /
// getSoundBuffer path) evicts a sound from the buffer cache while the sfx
// cache still references it. The next play RE-DECODES it -- two decoded
// copies of the same WAV are resident (audit probe: Kick1.WAV 21,920 B in
// the sfx cache @0x5dfc6a2e9ff0 AND in the buffer cache @0x5dfc6a2ea560).
TEST(SfxSinglePcmCache, BufferCacheEvictionResurrectsTheDuplicate) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!g_willeq_db_available) GTEST_SKIP() << "willeq.db not found";
    if (!Have("snd1.pfs")) GTEST_SKIP() << "no snd1.pfs under " << EqClientPath();

    PrivateCacheDir cache;
    const uint32_t kA = 126, kB = 130;
    size_t sa = 0, sb = 0;
    {
        EQT::Audio::AudioManager am;
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        auto a = am.getSoundBuffer(kA);
        auto b = am.getSoundBuffer(kB);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        sa = a->getMemorySize();
        sb = b->getMemorySize();
        am.shutdown();
    }
    const std::string fa = EQT::WilleqDatabase::globalInstance()->getSoundAsset(kA);

    EQT::Audio::AudioManager am;
    ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
    // The cap holds the LARGER sound: the non-play fetch of the other evicts
    // the first from the buffer cache.
    am.setMemoryConstraints(std::max(sa, sb), /*lazyPfs=*/false);
    auto* sfx = am.getSfxManager();
    {
        eqt::memory::MemoryScope audio(Category::Audio);
        am.playSound(kA, glm::vec3(0.0f));
    }
    // A non-play fetch (the preloadCommonSounds / getSoundBuffer path)
    // touches only the buffer cache -> evicts A there. The sfx cache keeps A.
    {
        eqt::memory::MemoryScope audio(Category::Audio);
        (void)am.getSoundBuffer(kB);
    }
    const uint64_t before = AudioLive();
    {
        eqt::memory::MemoryScope audio(Category::Audio);
        am.playSound(kA, glm::vec3(0.0f));  // the replay
    }
    const uint64_t grew = AudioLive() - before;
    const SoundBuffer* sfxA = sfx->getWavData(fa);
    auto bcA = am.getSoundBufferByName(fa);
    std::cout << "[measured] cap=" << std::max(sa, sb)
              << " replay grew audio by " << grew << " B (pcm " << sa << ")"
              << " sfxA=" << sfxA << " bufferCacheA=" << (void*)bcA.get()
              << " sfxCacheBytes=" << sfx->getCacheSize()
              << " bufferCacheBytes=" << am.getSoundBufferCacheBytes() << "\n";
    // ONE decoded copy: the sfx entry and the buffer-cache entry are the
    // SAME object (or the sfx entry does not exist at all). A resurrected
    // second copy is a different object, and the replay grew `audio` by a
    // full PCM.
    EXPECT_EQ(sfxA, bcA.get())
        << "two decoded copies of " << fa << " are resident -- the buffer "
           "cache evicted the sound the sfx cache still held (B1015 finding 1)";
    EXPECT_LE(grew, static_cast<uint64_t>(sa))
        << "the replay re-decoded a second resident copy: `audio` grew by "
           << grew << " B for a sound of " << sa << " B";
    bcA.reset();
    am.shutdown();
}

// B1015 finding 2: the MR's own cap case (ids 126/130/131, cap = middle size
// - 1) must keep the TOTAL resident decoded PCM within the cap, and the
// survivor is the most recently played. Pre-fix the two LRUs pick different
// victims: audit probe, cap 21,919 -> buffer cache 11,072 B (sound 131) +
// sfx cache 21,920 B (sound 126, the FIRST one played) = 32,992 B of PCM
// live under 21,919.
TEST(SfxSinglePcmCache, TheCapBoundsTheTotalResidentPcm) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!g_willeq_db_available) GTEST_SKIP() << "willeq.db not found";
    if (!Have("snd1.pfs")) GTEST_SKIP() << "no snd1.pfs under " << EqClientPath();

    PrivateCacheDir cache;
    constexpr uint32_t kIds[] = {126, 130, 131};
    std::vector<size_t> sizes;
    {
        EQT::Audio::AudioManager am;
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        for (uint32_t id : kIds) {
            auto buffer = am.getSoundBuffer(id);
            ASSERT_NE(buffer, nullptr) << "sound id " << id << " did not load";
            sizes.push_back(buffer->getMemorySize());
        }
        am.shutdown();
    }
    std::vector<size_t> sorted = sizes;
    std::sort(sorted.begin(), sorted.end());
    const size_t cap = sorted[1] - 1;  // the MR's own cap: middle size - 1

    EQT::Audio::AudioManager am;
    ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
    am.setMemoryConstraints(cap, /*lazyPfs=*/false);
    const uint64_t before = AudioLive();
    for (uint32_t id : kIds) {
        eqt::memory::MemoryScope audio(Category::Audio);
        am.playSound(id, glm::vec3(0.0f));
    }
    const uint64_t pcmDelta = AudioLive() - before;
    auto* sfx = am.getSfxManager();
    std::cout << "[measured] cap case: sizes=" << sizes[0] << "," << sizes[1]
              << "," << sizes[2] << " cap=" << cap
              << " total decoded PCM live=" << pcmDelta << " B (bound "
              << cap + 16ll * 1024 << ")"
              << " bufferCacheBytes=" << am.getSoundBufferCacheBytes()
              << " sfxCacheBytes=" << sfx->getCacheSize()
              << " sfxCount=" << sfx->getCachedSoundCount()
              << " sfxEvictions=" << sfx->getEvictionCount() << "\n";
    // The TOTAL resident decoded PCM (the `audio` category's growth across
    // the whole sequence -- every decode charges it, whichever cache owns
    // the result) must hold the cap plus channel overhead.
    EXPECT_LE(pcmDelta, static_cast<uint64_t>(cap) + 16ll * 1024)
        << "the plays left " << pcmDelta
           << " B of decoded PCM live under cap " << cap
           << " -- the total resident decoded PCM is not bounded by the "
              "soundCache cap: the two caches pick different victims "
              "(B1015 finding 2)";
    // The survivor is the most recently played (131): replaying it is a hit
    // that re-decodes nothing.
    const uint64_t before2 = AudioLive();
    {
        eqt::memory::MemoryScope audio(Category::Audio);
        am.playSound(kIds[2], glm::vec3(0.0f));
    }
    EXPECT_LT(AudioLive() - before2, 4096u)
        << "replaying the most recently played sound decoded it again -- the "
           "LRU survivor is not the most recently played";
    am.shutdown();
}

// B1015 finding 3 (pre-existing since T412): when every older sfx entry is
// playing, the sfx LRU evicts the entry it had JUST preloaded -- its play()
// gets nullptr, returns -1, and the requested sound silently never plays
// (audit probe, cap 21,919: sfxCount=1, sfxEvictions=2 -- 131 and 130 were
// each evicted at their own preload). Here: under a cap that holds the
// smallest sound, C plays (its channel is pinned and running for ~0.53 s),
// A plays while C is still running, and B plays while C AND A are still
// running. B's insertion is over budget with every older entry pinned: the
// pre-fix sfx LRU cannot evict the pinned older entries, and the entry it
// evicts is B itself -- the one just preloaded.
TEST(SfxSinglePcmCache, TheSoundBeingInsertedIsNeverEvicted) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!g_willeq_db_available) GTEST_SKIP() << "willeq.db not found";
    if (!Have("snd1.pfs")) GTEST_SKIP() << "no snd1.pfs under " << EqClientPath();

    PrivateCacheDir cache;
    constexpr uint32_t kIds[] = {126, 130, 131};  // C (largest), B, A (smallest)
    std::vector<size_t> sizes;
    {
        EQT::Audio::AudioManager am;
        ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
        for (uint32_t id : kIds) {
            auto buffer = am.getSoundBuffer(id);
            ASSERT_NE(buffer, nullptr) << "sound id " << id << " did not load";
            sizes.push_back(buffer->getMemorySize());
        }
        am.shutdown();
    }
    const size_t sC = sizes[0], sB = sizes[1], sA = sizes[2];
    ASSERT_GT(sC, sB) << "the probe needs three distinct sizes (C > B)";
    ASSERT_GT(sB, sA) << "the probe needs three distinct sizes (B > A)";

    // A cap that holds A alone (the audit probe's shape: middle size - 1
    // held the smallest entry, not the one inserted): every insertion after
    // the first is over budget.
    const size_t cap = sA - 1;
    EQT::Audio::AudioManager am;
    ASSERT_TRUE(am.initialize(EqClientPath(), /*forceLoopback=*/true));
    am.setMemoryConstraints(cap, /*lazyPfs=*/false);
    // C (126) runs ~0.53 s and A (131) ~0.27 s at their archive rates: every
    // play here lands inside the earlier channels, so the "every older
    // entry is playing" state holds.
    for (size_t i = 0; i < 3; ++i) {
        const uint64_t before = AudioLive();
        eqt::memory::MemoryScope audio(Category::Audio);
        am.playSound(kIds[i], glm::vec3(0.0f));
        const uint64_t grew = AudioLive() - before;
        std::cout << "[measured] play " << kIds[i] << " (pcm " << sizes[i]
                  << " B, cap " << cap << "): audio +" << grew << " B\n";
        // A play() that returned -1 grows `audio` by nothing but a few
        // bytes: the sound was evicted at its own insertion and then not
        // found. A successful play grows it by the decode plus the channel.
        EXPECT_GE(grew, sizes[i] / 2)
            << "sound " << kIds[i]
               << " never played (play() returned -1): it was evicted at its "
                  "own insertion while every older entry was still playing "
                  "(B1015 finding 3)";
    }
    auto* sfx = am.getSfxManager();
    std::cout << "[measured] sfxCount=" << sfx->getCachedSoundCount()
              << " sfxEvictions=" << sfx->getEvictionCount()
              << " sfxCacheBytes=" << sfx->getCacheSize() << "\n";
    am.shutdown();
}

}  // namespace Audio
}  // namespace EQT

#endif // WITH_AUDIO
