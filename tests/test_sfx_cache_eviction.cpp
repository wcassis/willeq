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
 * AudioMixer). No device is opened: preload() shares a decoded SoundBuffer
 * and play() only needs a mixer to allocate a channel.
 *
 * B989 (#308) updated the shape, not the LRU it pins: preload() no longer
 * COPIES the samples into a per-entry WavData (the second decoded-PCM copy
 * that made every played WAV held twice, au.sfx_cache). It shares the
 * SoundBuffer the AudioManager's buffer cache already holds, so a "sound"
 * below is a SoundBuffer built from raw PCM and shared into the cache the
 * same way the production path does. The behaviour every case asserts --
 * the ceiling is enforced, LRU goes first, the playing entry survives,
 * lowering the ceiling bites at once -- is unchanged.
 */

#include <gtest/gtest.h>

#include "client/audio/audio_mixer.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_buffer.h"

#include <string>
#include <vector>
#include <cmath>

namespace EQT {
namespace Audio {
namespace {

// One "sound" is kFrames mono 16-bit frames -> kFrames floats =
// kSoundBytes of decoded PCM.
constexpr size_t kFrames = 256;
constexpr size_t kSoundBytes = kFrames * sizeof(float);

std::vector<int16_t> makePcm16(int16_t value) {
    return std::vector<int16_t>(kFrames, value);
}

std::shared_ptr<SoundBuffer> makeSound(int16_t value) {
    auto raw = makePcm16(value);
    auto buffer = std::make_shared<SoundBuffer>();
    // The production path decodes to float exactly this way
    // (SoundBuffer::loadFromPCM); memorySize_ is the decoded float PCM.
    if (!buffer->loadFromPCM(raw.data(), raw.size(), 22050, 1)) {
        return nullptr;
    }
    return buffer;
}

void preloadSound(SfxManager& sfx, const std::string& name, int16_t value) {
    auto buffer = makeSound(value);
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(buffer->getMemorySize(), kSoundBytes);
    ASSERT_TRUE(sfx.preload(name, buffer));
}

// A ceiling that holds three sounds but not four.
constexpr size_t kThreeSounds = kSoundBytes * 3;

TEST(SfxCacheEviction, UnlimitedByDefault) {
    SfxManager sfx;
    EXPECT_EQ(0u, sfx.getCacheMaxBytes()) << "0 is the documented 'unlimited'";
    for (int i = 0; i < 8; ++i) preloadSound(sfx, "s" + std::to_string(i), i + 1);
    EXPECT_EQ(8u, sfx.getCachedSoundCount());
    EXPECT_EQ(0u, sfx.getEvictionCount());
}

TEST(SfxCacheEviction, ZeroMeansUnlimited) {
    SfxManager sfx;
    sfx.setCacheMaxBytes(0);
    for (int i = 0; i < 8; ++i) preloadSound(sfx, "s" + std::to_string(i), i + 1);
    EXPECT_EQ(8u * kSoundBytes, sfx.getCacheSize());
    EXPECT_EQ(0u, sfx.getEvictionCount());
}

// The headline: loading past the ceiling keeps the total under it.
TEST(SfxCacheEviction, TotalStaysUnderTheCeiling) {
    SfxManager sfx;
    sfx.setCacheMaxBytes(kThreeSounds);

    for (int i = 0; i < 10; ++i) preloadSound(sfx, "s" + std::to_string(i), i + 1);

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

    preloadSound(sfx, "a", 1);
    preloadSound(sfx, "b", 2);
    preloadSound(sfx, "c", 3);
    ASSERT_EQ(3u, sfx.getCachedSoundCount());

    // Touch "a" so "b" becomes the least recently used. getWavData() is the
    // read play() itself goes through.
    ASSERT_NE(nullptr, sfx.getWavData("a"));

    preloadSound(sfx, "d", 4);

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
    for (int i = 0; i < 6; ++i) preloadSound(sfx, "s" + std::to_string(i), i + 1);
    ASSERT_EQ(6u * kSoundBytes, sfx.getCacheSize());

    sfx.setCacheMaxBytes(kSoundBytes * 2);

    EXPECT_LE(sfx.getCacheSize(), kSoundBytes * 2);
    EXPECT_EQ(2u, sfx.getCachedSoundCount());
}

// A sound a channel is still playing must NOT be evicted: play() hands the
// mixer buffer->getSamples() and the audio thread reads through that pointer,
// so evicting it would free the PCM out from under the audio callback.
// (B989: the entry is a shared_ptr, so evicting drops one reference; the
// in-use check is what keeps the LAST reference out of a playing sound.)
TEST(SfxCacheEviction, APlayingSoundIsNotEvicted) {
    AudioMixer mixer;
    SfxManager sfx;
    sfx.setMixer(&mixer);
    sfx.setCacheMaxBytes(kThreeSounds);

    auto playing = makeSound(12345);
    ASSERT_NE(playing, nullptr);
    ASSERT_TRUE(sfx.preload("playing", playing));
    const SoundBuffer* playingData = sfx.getWavData("playing");
    ASSERT_NE(nullptr, playingData);
    const float* playingSamples = playingData->getSamples();

    int handle = sfx.play("playing", 1.0f, 0.0f, /*loop=*/true);
    ASSERT_GE(handle, 0);
    MixChannel* ch = mixer.getChannel(handle);
    ASSERT_NE(nullptr, ch);
    ASSERT_TRUE(ch->active.load());
    ASSERT_EQ(playingSamples, ch->samples);

    // Now flood the cache well past the ceiling. "playing" is the OLDEST entry
    // by LRU order, so without the in-use rule it is the first thing to go.
    for (int i = 0; i < 10; ++i) preloadSound(sfx, "flood" + std::to_string(i), i + 1);

    EXPECT_NE(nullptr, sfx.getWavData("playing"))
        << "an entry a live mixer channel points into must survive eviction";
    EXPECT_EQ(playingSamples, ch->samples)
        << "the channel's sample pointer must still be the cached buffer";

    // And once it stops, it becomes evictable like anything else.
    sfx.stopChannel(handle);
    for (int i = 10; i < 20; ++i) preloadSound(sfx, "flood" + std::to_string(i), i + 1);
    EXPECT_EQ(nullptr, sfx.getWavData("playing"))
        << "a stopped sound is no longer pinned";
}

}  // namespace
}  // namespace Audio
}  // namespace EQT
