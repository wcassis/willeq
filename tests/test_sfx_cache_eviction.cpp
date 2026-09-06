/**
 * T412 Leg B (board B292) -- the SFX cache must honour its ceiling.
 *
 * AudioManager::setMemoryConstraints() has always passed the configured
 * sound-cache budget to SfxManager::setCacheMaxBytes(). The setter stored it
 * and nothing ever compared cacheSizeBytes_ against it, so the WAV cache grew
 * without bound on exactly the memory-constrained targets the budget exists
 * for -- T407's write-only field census found it.
 *
 * These tests drive the real SfxManager (and, for the in-use case, a real
 * AudioMixer). No device is opened: preload() takes raw float PCM and play()
 * only needs a mixer to allocate a channel.
 *
 * Pre-fix every clause below except UnlimitedByDefault and ZeroMeansUnlimited
 * FAILS: the cache keeps everything.
 */

#include <gtest/gtest.h>

#include "client/audio/audio_mixer.h"
#include "client/audio/sfx_manager.h"

#include <string>
#include <vector>

namespace EQT {
namespace Audio {
namespace {

// One "sound" is kFrames mono float frames = kSoundBytes bytes.
constexpr size_t kFrames = 256;
constexpr size_t kSoundBytes = kFrames * sizeof(float);

std::vector<float> makePcm(float value) {
    return std::vector<float>(kFrames, value);
}

void preloadSound(SfxManager& sfx, const std::string& name, float value) {
    auto pcm = makePcm(value);
    ASSERT_TRUE(sfx.preload(name, pcm.data(), kFrames, 22050, 1));
}

// A ceiling that holds three sounds but not four.
constexpr size_t kThreeSounds = kSoundBytes * 3;

TEST(SfxCacheEviction, UnlimitedByDefault) {
    SfxManager sfx;
    EXPECT_EQ(0u, sfx.getCacheMaxBytes()) << "0 is the documented 'unlimited'";
    for (int i = 0; i < 8; ++i) preloadSound(sfx, "s" + std::to_string(i), 0.1f * i);
    EXPECT_EQ(8u, sfx.getCachedSoundCount());
    EXPECT_EQ(0u, sfx.getEvictionCount());
}

TEST(SfxCacheEviction, ZeroMeansUnlimited) {
    SfxManager sfx;
    sfx.setCacheMaxBytes(0);
    for (int i = 0; i < 8; ++i) preloadSound(sfx, "s" + std::to_string(i), 0.1f * i);
    EXPECT_EQ(8u * kSoundBytes, sfx.getCacheSize());
    EXPECT_EQ(0u, sfx.getEvictionCount());
}

// The headline: loading past the ceiling keeps the total under it.
TEST(SfxCacheEviction, TotalStaysUnderTheCeiling) {
    SfxManager sfx;
    sfx.setCacheMaxBytes(kThreeSounds);

    for (int i = 0; i < 10; ++i) preloadSound(sfx, "s" + std::to_string(i), 0.1f * i);

    EXPECT_LE(sfx.getCacheSize(), kThreeSounds)
        << "cacheMaxBytes_ is a ceiling, not a decoration";
    EXPECT_EQ(3u, sfx.getCachedSoundCount());
    EXPECT_EQ(7u, sfx.getEvictionCount());
}

// ...and what goes is the least recently used, not the newest or an arbitrary
// bucket order.
TEST(SfxCacheEviction, LeastRecentlyUsedGoesFirst) {
    SfxManager sfx;
    sfx.setCacheMaxBytes(kThreeSounds);

    preloadSound(sfx, "a", 0.1f);
    preloadSound(sfx, "b", 0.2f);
    preloadSound(sfx, "c", 0.3f);
    ASSERT_EQ(3u, sfx.getCachedSoundCount());

    // Touch "a" so "b" becomes the least recently used. getWavData() is the
    // read play() itself goes through.
    ASSERT_NE(nullptr, sfx.getWavData("a"));

    preloadSound(sfx, "d", 0.4f);

    EXPECT_NE(nullptr, sfx.getWavData("a")) << "a was just used";
    EXPECT_EQ(nullptr, sfx.getWavData("b")) << "b was the LRU entry";
    EXPECT_NE(nullptr, sfx.getWavData("c"));
    EXPECT_NE(nullptr, sfx.getWavData("d"));
    EXPECT_EQ(1u, sfx.getEvictionCount());
}

// Lowering the ceiling at runtime -- what setMemoryConstraints() does -- has to
// bite immediately, not at the next preload.
TEST(SfxCacheEviction, LoweringTheCeilingEvictsAtOnce) {
    SfxManager sfx;
    for (int i = 0; i < 6; ++i) preloadSound(sfx, "s" + std::to_string(i), 0.1f * i);
    ASSERT_EQ(6u * kSoundBytes, sfx.getCacheSize());

    sfx.setCacheMaxBytes(kSoundBytes * 2);

    EXPECT_LE(sfx.getCacheSize(), kSoundBytes * 2);
    EXPECT_EQ(2u, sfx.getCachedSoundCount());
}

// A sound a channel is still playing must NOT be evicted: play() hands the
// mixer wav->samples.data() and the audio thread reads through that pointer, so
// evicting it is a use-after-free.
TEST(SfxCacheEviction, APlayingSoundIsNotEvicted) {
    AudioMixer mixer;
    SfxManager sfx;
    sfx.setMixer(&mixer);
    sfx.setCacheMaxBytes(kThreeSounds);

    preloadSound(sfx, "playing", 0.5f);
    const WavData* playingData = sfx.getWavData("playing");
    ASSERT_NE(nullptr, playingData);
    const float* playingSamples = playingData->samples.data();

    int handle = sfx.play("playing", 1.0f, 0.0f, /*loop=*/true);
    ASSERT_GE(handle, 0);
    MixChannel* ch = mixer.getChannel(handle);
    ASSERT_NE(nullptr, ch);
    ASSERT_TRUE(ch->active.load());
    ASSERT_EQ(playingSamples, ch->samples);

    // Now flood the cache well past the ceiling. "playing" is the OLDEST entry
    // by LRU order, so without the in-use rule it is the first thing to go.
    for (int i = 0; i < 10; ++i) preloadSound(sfx, "flood" + std::to_string(i), 0.1f * i);

    EXPECT_NE(nullptr, sfx.getWavData("playing"))
        << "an entry a live mixer channel points into must survive eviction";
    EXPECT_EQ(playingSamples, ch->samples)
        << "the channel's sample pointer must still be the cached vector";

    // And once it stops, it becomes evictable like anything else.
    sfx.stopChannel(handle);
    for (int i = 10; i < 20; ++i) preloadSound(sfx, "flood" + std::to_string(i), 0.1f * i);
    EXPECT_EQ(nullptr, sfx.getWavData("playing"))
        << "a stopped sound is no longer pinned";
}

}  // namespace
}  // namespace Audio
}  // namespace EQT
