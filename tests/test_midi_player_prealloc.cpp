// T131: verifies the invariant "every MidiPlayer construction path applies
// TSF's off-thread-render preconditions (tsf_set_max_voices + all 16 MIDI
// channels pre-created)" — the invariant the unit's plan calls out as most
// likely to rot (T115 was a member added directly beneath a comment warning
// about exactly this pattern: three existing construction paths, a fourth
// added without the same treatment).
//
// Covers all FOUR construction paths found during scoping:
//   1. MidiPlayer::init(path) — single SoundFont
//   2. MidiPlayer::init(gmPath, customPath) — dual SoundFont, both slots
//   3. MidiPlayer::loadSoundFont(path) — the runtime GM-slot reload
//   4. selectSoundFont() switching the active pointer between two
//      already-configured instances (not a construction path itself, but
//      proves BOTH instances from path 2 were configured independently,
//      not just whichever was active at check time)

#include "client/audio/midi_player.h"

#include <gtest/gtest.h>
#include <filesystem>

using namespace EQT::Audio;

namespace {
// data/1mgm.sf2 — the small General MIDI SoundFont already vendored for
// tests (see test_zone_music.cpp's use of the same data/ directory).
// WILLEQ_TEST_SF2_PATH is set via TARGET_COMPILE_DEFINITIONS in
// tests/CMakeLists.txt, same pattern as WILLEQ_DB_PATH elsewhere in this
// tree. Reused as BOTH the GM and "custom" slot in the dual-SoundFont test
// below; tsf_load_filename() doesn't care that it's the same file loaded
// twice into two independent tsf instances, which is exactly what's being
// tested (that each independently-loaded instance gets independently
// configured).
std::string testSoundFontPath() {
    return std::string(WILLEQ_TEST_SF2_PATH);
}
}  // namespace

class MidiPlayerPreallocTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(testSoundFontPath())) {
            GTEST_SKIP() << "test SoundFont not found at: " << testSoundFontPath();
        }
    }
};

TEST_F(MidiPlayerPreallocTest, SingleSoundFontInitPath) {
    MidiPlayer player;
    ASSERT_TRUE(player.init(testSoundFontPath(), 22050));

    EXPECT_EQ(player.getConfiguredMaxVoices(), MidiPlayer::kMaxVoices)
        << "tsf_set_max_voices was not applied by MidiPlayer::init(path)";
    EXPECT_EQ(player.getPreallocatedChannelCount(), 16)
        << "not all 16 MIDI channels were pre-created by MidiPlayer::init(path)";
}

TEST_F(MidiPlayerPreallocTest, DualSoundFontInitPath_BothSlotsConfigured) {
    MidiPlayer player;
    ASSERT_TRUE(player.init(testSoundFontPath(), testSoundFontPath(), 22050));

    // Active on init is GM.
    ASSERT_EQ(player.getActiveSoundFont(), SoundFontType::GM);
    EXPECT_EQ(player.getConfiguredMaxVoices(), MidiPlayer::kMaxVoices)
        << "tsf_set_max_voices was not applied to the GM slot";
    EXPECT_EQ(player.getPreallocatedChannelCount(), 16)
        << "not all 16 MIDI channels were pre-created for the GM slot";

    // Switch to the custom slot -- if it was NOT independently configured
    // (i.e. the fix only touched whichever instance happened to be active
    // at init time), this would now read 0 / unconfigured.
    player.selectSoundFont(SoundFontType::Custom);
    ASSERT_EQ(player.getActiveSoundFont(), SoundFontType::Custom);
    EXPECT_EQ(player.getConfiguredMaxVoices(), MidiPlayer::kMaxVoices)
        << "tsf_set_max_voices was not applied to the custom slot";
    EXPECT_EQ(player.getPreallocatedChannelCount(), 16)
        << "not all 16 MIDI channels were pre-created for the custom slot";
}

TEST_F(MidiPlayerPreallocTest, ReloadPathConfiguresTheNewInstance) {
    MidiPlayer player;
    ASSERT_TRUE(player.init(testSoundFontPath(), 22050));
    ASSERT_EQ(player.getConfiguredMaxVoices(), MidiPlayer::kMaxVoices);

    // loadSoundFont() builds a brand-new tsf instance (tsf_load_filename)
    // and swaps it into the GM slot -- this is the "fourth path" the plan
    // called out by name. If it were missed, the freshly-loaded instance
    // would read 0 configured voices / 0 pre-created channels even though
    // the FIRST instance (already torn down by this point) was configured.
    ASSERT_TRUE(player.loadSoundFont(testSoundFontPath()));

    EXPECT_EQ(player.getConfiguredMaxVoices(), MidiPlayer::kMaxVoices)
        << "tsf_set_max_voices was not applied by the loadSoundFont() reload path";
    EXPECT_EQ(player.getPreallocatedChannelCount(), 16)
        << "not all 16 MIDI channels were pre-created by the loadSoundFont() reload path";
}

TEST_F(MidiPlayerPreallocTest, NoActiveSoundFontReadsZero) {
    // Sanity check on the accessors themselves: a never-initialized player
    // must not report a false-positive "configured" state.
    MidiPlayer player;
    EXPECT_EQ(player.getConfiguredMaxVoices(), 0);
    EXPECT_EQ(player.getPreallocatedChannelCount(), 0);
}

// =====================================================================
// T359 (board B015) — loadSoundFont's precondition is ENFORCED.
//
// The GM-slot swap writes impl_->soundFont (a raw pointer the audio callback
// thread reads with no atomic and no mutex) and then frees the instance it
// replaced. Its one production call site runs before the audio backend exists,
// which is what makes it safe — a fact that lived only in a source comment, so
// a second caller added later (a "/soundfont reload" command, say) would have
// raced the audio thread and risked a use-after-free with nothing to stop it.
//
// AudioManager now calls setAudioThreadRunning(true) the moment the backend is
// started and (false) when it is torn down; loadSoundFont refuses and logs
// while that flag is set. No lock is taken on the realtime callback path.
// =====================================================================

TEST_F(MidiPlayerPreallocTest, LoadSoundFontRefusedWhileTheAudioThreadIsRunning) {
    MidiPlayer player;
    ASSERT_TRUE(player.init(testSoundFontPath(), 22050));
    EXPECT_FALSE(player.isAudioThreadRunning());

    // BEFORE the backend exists — the production ordering — it is accepted.
    ASSERT_TRUE(player.loadSoundFont(testSoundFontPath()));

    // AFTER the backend starts, the same call must be refused outright.
    player.setAudioThreadRunning(true);
    EXPECT_TRUE(player.isAudioThreadRunning());
    EXPECT_FALSE(player.loadSoundFont(testSoundFontPath()))
        << "the unsynchronised tsf* swap was allowed while the audio callback "
           "thread could be reading impl_->soundFont";

    // A refusal must leave a usable player, not a half-swapped one.
    EXPECT_EQ(player.getConfiguredMaxVoices(), MidiPlayer::kMaxVoices);
    EXPECT_EQ(player.getPreallocatedChannelCount(), 16);

    // And it must be reversible: after the backend is torn down it works again.
    player.setAudioThreadRunning(false);
    EXPECT_TRUE(player.loadSoundFont(testSoundFontPath()));
}

// A bad path is still rejected for being a bad path, and must not flip the
// precondition flag on its way out.
TEST_F(MidiPlayerPreallocTest, LoadSoundFontRefusalDoesNotDisturbTheAudioThreadFlag) {
    MidiPlayer player;
    ASSERT_TRUE(player.init(testSoundFontPath(), 22050));
    EXPECT_FALSE(player.loadSoundFont("/nonexistent/definitely-not-a.sf2"));
    EXPECT_FALSE(player.isAudioThreadRunning());
    EXPECT_TRUE(player.loadSoundFont(testSoundFontPath()));
}
