// T496 Leg B (board B817): MusicPlayer's fallback SoundFont is loaded and then
// abandoned, and it is the head of the Orange Pi's shrink queue.
//
// MEASURED on the Pi (review/T496/pi/table_categories.tsv): the `audio` ledger
// category is **47.6 MiB live at every lifecycle point from `startup` to
// `steady_60s`** -- the single largest bucket at `ready` on every zone -- with a
// 171 MiB peak during startup. It is heap, not Mali/DRM: the [memmap] census
// puts it in glibc's mmap'd large-block region, and the allocator ledger
// charges every byte of it.
//
// The cause is a lifetime bug, not a size one. `AudioManager::initialize()`
// calls `musicPlayer_->initialize(eqPath, "")` BEFORE
// `musicPlayer_->setMidiPlayer(midiPlayer_.get())`. With no external player set
// yet, `MusicPlayer::initialize()` builds `ownedMidiPlayer_` and loads
// `synthus2.sf2` into it -- a second, complete copy of the same SoundFont
// AudioManager's own MidiPlayer already holds. `setMidiPlayer()` then
// overwrites the pointer and the owned player is retained, unused, for the
// life of the process.
//
// These tests assert BYTES, through the ledger's `audio` category, because
// that is the number the Pi measurement named.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "client/audio/music_player.h"
#include "client/audio/midi_player.h"
#include "client/core/memory_ledger.h"

namespace {

// A fallback SoundFont must retain no more than this once it has been
// superseded. The GM/instrument tables tsf keeps for a live font are tens of
// MiB (measured: see the fixture below); 1 MiB is comfortably above any
// bookkeeping a released player leaves behind and far below one font.
constexpr uint64_t kSupersededBound = 1ull * 1024 * 1024;

std::string EqClientPath() {
#ifdef WILLEQ_EQ_CLIENT_PATH
    return WILLEQ_EQ_CLIENT_PATH;
#else
    return std::string();
#endif
}

bool HaveSoundFont() {
    const std::string p = EqClientPath();
    if (p.empty()) return false;
    return std::filesystem::exists(p + "/synthus2.sf2") ||
           std::filesystem::exists(p + "/synthusr.sf2");
}

uint64_t AudioLive() {
    return eqt::memory::stats(eqt::memory::Category::Audio).liveBytes;
}

}  // namespace

// Baseline: this records what one fallback SoundFont actually costs, so the
// two bounds below are read against a measured number rather than a guess.
TEST(MusicPlayerSoundFontLifetime, AFallbackSoundFontIsNotSmall) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!HaveSoundFont()) GTEST_SKIP() << "no EQ client SoundFont at " << EqClientPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    const uint64_t before = AudioLive();
    {
        EQT::Audio::MusicPlayer mp;
        ASSERT_TRUE(mp.initialize(EqClientPath(), ""));
        const uint64_t loaded = AudioLive() - before;
        std::cout << "[measured] fallback SoundFont retains " << loaded
                  << " bytes (" << (loaded / 1048576.0) << " MiB)\n";
        EXPECT_GT(loaded, kSupersededBound)
            << "if a fallback font is this cheap, the bounds below say nothing";
    }
    EXPECT_LE(AudioLive(), before + kSupersededBound) << "destructor did not release it";
}

// THE RED. At the T496 base tip this fails: setMidiPlayer() overwrites the
// pointer and leaks the owned player's whole SoundFont for the process
// lifetime. This is the exact sequence AudioManager::initialize() performs.
TEST(MusicPlayerSoundFontLifetime, SupersedingTheOwnedMidiPlayerReleasesItsSoundFont) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!HaveSoundFont()) GTEST_SKIP() << "no EQ client SoundFont at " << EqClientPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    EQT::Audio::MidiPlayer external;
    const uint64_t before = AudioLive();

    EQT::Audio::MusicPlayer mp;
    ASSERT_TRUE(mp.initialize(EqClientPath(), ""));   // builds ownedMidiPlayer_
    mp.setMidiPlayer(&external);                      // supersedes it

    const uint64_t retained = AudioLive() - before;
    EXPECT_LE(retained, kSupersededBound)
        << "a superseded fallback MidiPlayer is still holding " << retained
        << " bytes (" << (retained / 1048576.0)
        << " MiB) of SoundFont that nothing can render from";
}

// The converse, and the shape AudioManager should wire: an external MidiPlayer
// supplied BEFORE initialize() must never cause a second font to be loaded at
// all -- not even transiently, which is what the Pi's 171 MiB startup peak is.
TEST(MusicPlayerSoundFontLifetime, AnExternalMidiPlayerSetFirstIsNeverDuplicated) {
    if (!eqt::memory::enabled()) GTEST_SKIP() << "ledger off in this build";
    if (!HaveSoundFont()) GTEST_SKIP() << "no EQ client SoundFont at " << EqClientPath();

    eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
    EQT::Audio::MidiPlayer external;
    const uint64_t before = AudioLive();

    EQT::Audio::MusicPlayer mp;
    mp.setMidiPlayer(&external);
    ASSERT_TRUE(mp.initialize(EqClientPath(), ""));

    const uint64_t retained = AudioLive() - before;
    EXPECT_LE(retained, kSupersededBound)
        << "MusicPlayer loaded its own SoundFont although one was already supplied: "
        << retained << " bytes";
}
