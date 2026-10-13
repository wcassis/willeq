/**
 * B1015 (#336, supersedes the T412 B292 shape) -- the ONE decoded-PCM cache
 * of SFX PCM must honour its ceiling.
 *
 * T412 pinned this on SfxManager::cache_: the ceiling is enforced, LRU goes
 * first, the playing entry survives, lowering the ceiling bites at once.
 * B989 left that second LRU beside AudioManager::bufferCache_ -- two caps,
 * two victims (the audit in refactor/B1015). B1015 deleted the second LRU:
 * the ceiling now lives on SoundBufferCache (the old bufferCache_ grown
 * into a class), with the in-use pin in it (SfxManager::isInUse), and the
 * entry a fetch just inserted is never that fetch's victim.
 *
 * These tests drive the real SoundBufferCache (and, for the pin cases, a
 * real SfxManager + AudioMixer). No device is opened: the source builds a
 * SoundBuffer from raw PCM and the play() only needs a mixer to allocate a
 * channel. The pin's safety fact is unchanged from T412: play() hands the
 * mixer buffer->getSamples() and the audio thread reads through that
 * pointer, so the pin (not a shared_ptr) is what keeps a playing PCM
 * resident.
 */

#include <gtest/gtest.h>

#include "client/audio/audio_mixer.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_buffer.h"
#include "client/audio/sound_buffer_cache.h"

#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>

namespace EQT {
namespace Audio {
namespace {

// One "sound" is kFrames mono 16-bit frames -> kFrames floats =
// kSoundBytes of decoded PCM.
constexpr size_t kFrames = 256;
constexpr size_t kSoundBytes = kFrames * sizeof(float);

std::shared_ptr<SoundBuffer> makeSound(int16_t value) {
    std::vector<int16_t> raw(kFrames, value);
    auto buffer = std::make_shared<SoundBuffer>();
    // The production path decodes to float exactly this way
    // (SoundBuffer::loadFromPCM); memorySize_ is the decoded float PCM.
    if (!buffer->loadFromPCM(raw.data(), raw.size(), 22050, 1)) {
        return nullptr;
    }
    return buffer;
}

// The test double the cache's Source calls: a name -> PCM map, one "sound"
// per name.
class SourceMap {
public:
    void put(const std::string& name, int16_t value) {
        sounds_[name] = makeSound(value);
    }
    std::shared_ptr<SoundBuffer> get(const std::string& name) const {
        auto it = sounds_.find(name);
        return it == sounds_.end() ? nullptr : it->second;
    }
private:
    std::map<std::string, std::shared_ptr<SoundBuffer>> sounds_;
};

// Build the one cache over a source map, with the in-use pin wired through
// a REAL SfxManager + AudioMixer (the production wiring:
// AudioManager::initialize binds the same two callbacks).
void wire(SoundBufferCache& cache, SourceMap& src, SfxManager& sfx,
          AudioMixer* mixer = nullptr) {
    cache.setSource([&src](const std::string& name) { return src.get(name); });
    cache.setPin([&sfx](const SoundBuffer& buffer) { return sfx.isInUse(buffer); });
    sfx.setMixer(mixer);
}

// A non-loading residency check: returns the buffer if `name` is in the
// cache, else nullptr. Unlike getOrLoad it does NOT decode on a miss and
// does NOT touch the LRU, so the tests can observe what eviction left
// behind (a getOrLoad miss would re-load and turn "evicted" into
// "resident again").
std::shared_ptr<SoundBuffer> resident(const SoundBufferCache& cache,
                                      const std::string& name) {
    return cache.peek(name);
}

// A ceiling that holds three sounds but not four.
constexpr size_t kThreeSounds = kSoundBytes * 3;

TEST(SfxCacheEviction, UnlimitedByDefault) {
    SfxManager sfx;
    AudioMixer mixer;
    SourceMap src;
    SoundBufferCache cache;
    wire(cache, src, sfx, &mixer);

    for (int i = 0; i < 8; ++i) {
        src.put("s" + std::to_string(i), i + 1);
        auto buffer = cache.getOrLoad("s" + std::to_string(i));
        ASSERT_NE(buffer, nullptr);
    }
    EXPECT_EQ(8u, cache.getEntryCount());
    EXPECT_EQ(8u * kSoundBytes, cache.getSizeBytes());
}

// The headline: loading past the ceiling keeps the total at the ceiling.
// The ceiling is INCLUSIVE: eviction runs while the total is over, and stops
// at the ceiling (sizeBytes_ <= maxBytes_). The entry a fetch just inserted
// sits at the FRONT of the LRU, so it is the last thing that can go and
// never goes while anything older is evictable -- when the cap divides
// evenly (as here) the cache ends exactly AT the ceiling, holding the
// freshest three.
TEST(SfxCacheEviction, TotalStaysUnderTheCeiling) {
    SfxManager sfx;
    AudioMixer mixer;
    SourceMap src;
    SoundBufferCache cache;
    wire(cache, src, sfx, &mixer);
    cache.setMaxBytes(kThreeSounds);

    for (int i = 0; i < 10; ++i) {
        src.put("s" + std::to_string(i), i + 1);
        auto buffer = cache.getOrLoad("s" + std::to_string(i));
        ASSERT_NE(buffer, nullptr);
    }

    // Ten insertions evicted the seven oldest; the three freshest (7, 8, 9)
    // are resident at exactly the ceiling.
    EXPECT_EQ(kThreeSounds / kSoundBytes, cache.getEntryCount());
    EXPECT_EQ(kThreeSounds, cache.getSizeBytes())
        << "the ceiling is inclusive: the cache evicts down to it, no further";
    EXPECT_EQ(resident(cache, "s0"), nullptr) << "the oldest sound went first";
    EXPECT_NE(resident(cache, "s9"), nullptr) << "the freshest sound survived";
}

// ...and what goes is the least recently used, not the newest or an
// arbitrary bucket order. A hit is a touch (the read play() goes through).
// Residency is checked with a non-loading read (resident()): a getOrLoad
// would re-decode a miss and turn "evicted" into "resident again".
TEST(SfxCacheEviction, LeastRecentlyUsedGoesFirst) {
    SfxManager sfx;
    AudioMixer mixer;
    SourceMap src;
    SoundBufferCache cache;
    wire(cache, src, sfx, &mixer);
    cache.setMaxBytes(kThreeSounds);

    for (const char* name : {"a", "b", "c"}) {
        src.put(name, 1);
        ASSERT_NE(cache.getOrLoad(name), nullptr);
    }
    ASSERT_EQ(3u, cache.getEntryCount());

    // Touch "a" so "b" becomes the least recently used.
    ASSERT_NE(cache.getOrLoad("a"), nullptr);

    src.put("d", 4);
    ASSERT_NE(cache.getOrLoad("d"), nullptr);

    // Nothing is playing, so the cap evicts the LRU entry: "b".
    EXPECT_NE(resident(cache, "a"), nullptr) << "a was just used";
    EXPECT_EQ(resident(cache, "b"), nullptr) << "b was the LRU entry";
    EXPECT_NE(resident(cache, "c"), nullptr);
    EXPECT_NE(resident(cache, "d"), nullptr);
}

// Lowering the ceiling at runtime -- what setMemoryConstraints() does -- has
// to bite immediately, not at the next fetch. The ceiling is inclusive, so a
// cap of 2 sounds ends with exactly 2 resident (the two freshest).
TEST(SfxCacheEviction, LoweringTheCeilingEvictsAtOnce) {
    SfxManager sfx;
    AudioMixer mixer;
    SourceMap src;
    SoundBufferCache cache;
    wire(cache, src, sfx, &mixer);

    for (int i = 0; i < 6; ++i) {
        src.put("s" + std::to_string(i), i + 1);
        ASSERT_NE(cache.getOrLoad("s" + std::to_string(i)), nullptr);
    }
    ASSERT_EQ(6u * kSoundBytes, cache.getSizeBytes());

    cache.setMaxBytes(kSoundBytes * 2);

    // Four oldest entries go; the two freshest (4, 5) stay, at the ceiling.
    EXPECT_EQ(2u, cache.getEntryCount());
    EXPECT_EQ(kSoundBytes * 2, cache.getSizeBytes());
    EXPECT_EQ(resident(cache, "s0"), nullptr);
    EXPECT_EQ(resident(cache, "s3"), nullptr);
    EXPECT_NE(resident(cache, "s5"), nullptr) << "the freshest entry survives";
}

// A sound a channel is still playing must NOT be evicted: play() hands the
// mixer buffer->getSamples() and the audio thread reads through that
// pointer, so evicting it would free the PCM out from under the audio
// callback.
//
// The pinned sound is made the OLDEST entry by LRU order (the freshest ones
// come after it), so without the pin the eviction walker takes it first.
//
// B1015 (#336, audit finding 5): the test's OWN shared_ptr no longer keeps
// the buffer alive -- the cache entry is the last owner. (The T412 version
// held `playing` for the whole test, so the in-use rule could break and the
// PCM would still survive through the test's own reference.) The pin is
// asserted on the buffer the CACHE hands out.
TEST(SfxCacheEviction, APlayingSoundIsNotEvicted) {
    AudioMixer mixer;
    SfxManager sfx;
    SourceMap src;
    SoundBufferCache cache;
    wire(cache, src, sfx, &mixer);
    cache.setMaxBytes(kThreeSounds);

    // The cache is the last owner: the test only ever looks at the buffer
    // through the cache itself.
    src.put("playing", 12345);
    auto playing = cache.getOrLoad("playing");
    ASSERT_NE(playing, nullptr);
    const float* playingSamples = playing->getSamples();
    playing.reset();  // B1015: the cache entry is now the last owner

    // Play the buffer the cache hands out -- the production path
    // (playSoundInternal) resolves the buffer through the one cache and
    // plays it.
    auto toPlay = cache.getOrLoad("playing");
    ASSERT_NE(toPlay, nullptr);
    int handle = sfx.play(toPlay.get(), 1.0f, 0.0f, /*loop=*/false);
    ASSERT_GE(handle, 0);
    MixChannel* ch = mixer.getChannel(handle);
    ASSERT_NE(ch, nullptr);
    ASSERT_TRUE(ch->active.load());
    ASSERT_EQ(playingSamples, ch->samples);
    toPlay.reset();  // the cache (and the channel's read) own the PCM now

    // Flood the cache well past the ceiling. "playing" is the OLDEST entry
    // by LRU order, so without the pin it is the first thing to go -- and
    // with no other owner, evicting it would free the PCM out from under
    // the audio thread.
    for (int i = 0; i < 10; ++i) {
        src.put("flood" + std::to_string(i), i + 1);
        auto buffer = cache.getOrLoad("flood" + std::to_string(i));
        ASSERT_NE(buffer, nullptr);
    }

    // "playing" is resident (pinned by the live channel): a non-loading read
    // proves the eviction walker skipped it even though it is the LRU entry.
    auto residentPlaying = resident(cache, "playing");
    EXPECT_NE(residentPlaying, nullptr)
        << "an entry a live mixer channel points into must survive eviction";
    if (residentPlaying) {
        EXPECT_EQ(playingSamples, residentPlaying->getSamples())
            << "the channel's sample pointer must still be the cached buffer";
    }

    // And once it stops, it becomes evictable like anything else. (A
    // non-loading read: getOrLoad would re-decode the miss and hide the
    // eviction.)
    sfx.stopChannel(handle);
    for (int i = 10; i < 20; ++i) {
        src.put("flood" + std::to_string(i), i + 1);
        auto buffer = cache.getOrLoad("flood" + std::to_string(i));
        ASSERT_NE(buffer, nullptr);
    }
    EXPECT_EQ(resident(cache, "playing"), nullptr)
        << "a stopped sound is no longer pinned";
}

}  // namespace
}  // namespace Audio
}  // namespace EQT
