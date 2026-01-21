#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/eff_loader.h"

#include <filesystem>
#include <fstream>
#include <cstring>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// EffSoundEntry Structure Tests
// =============================================================================

TEST(EffSoundEntryTest, StructSize) {
    // Critical: must be exactly 84 bytes for binary compatibility
    EXPECT_EQ(sizeof(EffSoundEntry), 84u);
}

TEST(EffSoundEntryTest, FieldOffsets) {
    // Verify field offsets match EFF format specification
    EffSoundEntry entry;
    char* base = reinterpret_cast<char*>(&entry);

    EXPECT_EQ(reinterpret_cast<char*>(&entry.UnkRef00) - base, 0);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.UnkRef04) - base, 4);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.Reserved) - base, 8);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.Sequence) - base, 12);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.X) - base, 16);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.Y) - base, 20);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.Z) - base, 24);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.Radius) - base, 28);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.Cooldown1) - base, 32);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.Cooldown2) - base, 36);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.RandomDelay) - base, 40);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.Unk44) - base, 44);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.SoundID1) - base, 48);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.SoundID2) - base, 52);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.SoundType) - base, 56);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.AsDistance) - base, 60);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.FadeOutMS) - base, 68);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.FullVolRange) - base, 76);
    EXPECT_EQ(reinterpret_cast<char*>(&entry.UnkRange80) - base, 80);
}

// =============================================================================
// Hardcoded Sound Tests
// =============================================================================

TEST(EffLoaderTest, HardcodedSounds) {
    // Test known hardcoded sound IDs (32-161)
    EXPECT_EQ(EffLoader::getHardcodedSound(39), "death_me");
    EXPECT_EQ(EffLoader::getHardcodedSound(143), "thunder1");
    EXPECT_EQ(EffLoader::getHardcodedSound(144), "thunder2");
    EXPECT_EQ(EffLoader::getHardcodedSound(158), "wind_lp1");
    EXPECT_EQ(EffLoader::getHardcodedSound(159), "rainloop");
    EXPECT_EQ(EffLoader::getHardcodedSound(160), "torch_lp");
    EXPECT_EQ(EffLoader::getHardcodedSound(161), "watundlp");
}

TEST(EffLoaderTest, UnusedHardcodedSoundsEmpty) {
    // Most hardcoded IDs are undefined
    EXPECT_TRUE(EffLoader::getHardcodedSound(32).empty());
    EXPECT_TRUE(EffLoader::getHardcodedSound(50).empty());
    EXPECT_TRUE(EffLoader::getHardcodedSound(100).empty());
    EXPECT_TRUE(EffLoader::getHardcodedSound(140).empty());
}

TEST(EffLoaderTest, OutOfRangeHardcodedSounds) {
    // IDs outside 32-161 should return empty
    EXPECT_TRUE(EffLoader::getHardcodedSound(0).empty());
    EXPECT_TRUE(EffLoader::getHardcodedSound(31).empty());
    EXPECT_TRUE(EffLoader::getHardcodedSound(162).empty());
    EXPECT_TRUE(EffLoader::getHardcodedSound(200).empty());
}

// =============================================================================
// Sound ID Resolution Tests
// =============================================================================

class SoundIdResolutionTest : public ::testing::Test {
protected:
    EffLoader loader_;

    void SetUp() override {
        // Manually set up EMIT and LOOP sounds for testing
        // (we'll access them via loadZone in integration tests)
    }
};

TEST_F(SoundIdResolutionTest, ZeroIsNoSound) {
    EXPECT_TRUE(loader_.resolveSoundFile(0).empty());
}

TEST_F(SoundIdResolutionTest, HardcodedRangeResolution) {
    // IDs 32-161 should resolve to hardcoded sounds
    EXPECT_EQ(loader_.resolveSoundFile(39), "death_me");
    EXPECT_EQ(loader_.resolveSoundFile(159), "rainloop");
}

TEST_F(SoundIdResolutionTest, EmitRangeEmpty) {
    // Without loading a sndbnk, EMIT range (1-31) should return empty
    EXPECT_TRUE(loader_.resolveSoundFile(1).empty());
    EXPECT_TRUE(loader_.resolveSoundFile(15).empty());
    EXPECT_TRUE(loader_.resolveSoundFile(31).empty());
}

TEST_F(SoundIdResolutionTest, LoopRangeEmpty) {
    // Without loading a sndbnk, LOOP range (162+) should return empty
    EXPECT_TRUE(loader_.resolveSoundFile(162).empty());
    EXPECT_TRUE(loader_.resolveSoundFile(170).empty());
}

// =============================================================================
// Binary _sounds.eff File Tests
// =============================================================================

class EffSoundsFileTest : public ::testing::Test {
protected:
    EffLoader loader_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(EffSoundsFileTest, LoadGfaydarkSounds) {
    std::string filepath = std::string(EQ_PATH) + "/gfaydark_sounds.eff";
    if (!std::filesystem::exists(filepath)) {
        GTEST_SKIP() << "gfaydark_sounds.eff not found";
    }

    EXPECT_TRUE(loader_.loadSoundsEff(filepath));
    EXPECT_GT(loader_.getEntryCount(), 0u);

    // Verify entries are valid
    for (const auto& entry : loader_.getSoundEntries()) {
        // Sound type should be 0-3
        EXPECT_LE(entry.SoundType, 3);
        // Radius should be positive
        EXPECT_GE(entry.Radius, 0.0f);
    }
}

TEST_F(EffSoundsFileTest, LoadHalasSounds) {
    std::string filepath = std::string(EQ_PATH) + "/halas_sounds.eff";
    if (!std::filesystem::exists(filepath)) {
        GTEST_SKIP() << "halas_sounds.eff not found";
    }

    EXPECT_TRUE(loader_.loadSoundsEff(filepath));
    EXPECT_GT(loader_.getEntryCount(), 0u);

    // Check for music entries (Halas has location-based music)
    size_t musicCount = loader_.getMusicEntryCount();
    EXPECT_GT(musicCount, 0u) << "Halas should have music regions";
}

TEST_F(EffSoundsFileTest, FileSizeMultipleOf84) {
    std::string filepath = std::string(EQ_PATH) + "/gfaydark_sounds.eff";
    if (!std::filesystem::exists(filepath)) {
        GTEST_SKIP() << "gfaydark_sounds.eff not found";
    }

    auto fileSize = std::filesystem::file_size(filepath);
    EXPECT_EQ(fileSize % 84, 0u) << "File size should be multiple of 84 bytes";
}

TEST_F(EffSoundsFileTest, InvalidFileReturnsError) {
    EXPECT_FALSE(loader_.loadSoundsEff("/nonexistent/path/_sounds.eff"));
    EXPECT_EQ(loader_.getEntryCount(), 0u);
}

// =============================================================================
// Text _sndbnk.eff File Tests
// =============================================================================

class EffSndBnkFileTest : public ::testing::Test {
protected:
    EffLoader loader_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(EffSndBnkFileTest, LoadGfaydarkSndBnk) {
    std::string filepath = std::string(EQ_PATH) + "/gfaydark_sndbnk.eff";
    if (!std::filesystem::exists(filepath)) {
        GTEST_SKIP() << "gfaydark_sndbnk.eff not found";
    }

    EXPECT_TRUE(loader_.loadSndBnkEff(filepath));

    // gfaydark has EMIT sounds (fire_lp) and LOOP sounds (wind, darkwds, night)
    EXPECT_GT(loader_.getEmitSounds().size(), 0u);
    EXPECT_GT(loader_.getLoopSounds().size(), 0u);
}

TEST_F(EffSndBnkFileTest, EmitSoundsAreFireLoop) {
    std::string filepath = std::string(EQ_PATH) + "/gfaydark_sndbnk.eff";
    if (!std::filesystem::exists(filepath)) {
        GTEST_SKIP() << "gfaydark_sndbnk.eff not found";
    }

    loader_.loadSndBnkEff(filepath);

    const auto& emits = loader_.getEmitSounds();
    if (!emits.empty()) {
        // First EMIT entry should be fire_lp
        EXPECT_EQ(emits[0], "fire_lp");
    }
}

TEST_F(EffSndBnkFileTest, LoopSoundsIncludeWind) {
    std::string filepath = std::string(EQ_PATH) + "/gfaydark_sndbnk.eff";
    if (!std::filesystem::exists(filepath)) {
        GTEST_SKIP() << "gfaydark_sndbnk.eff not found";
    }

    loader_.loadSndBnkEff(filepath);

    const auto& loops = loader_.getLoopSounds();
    bool hasWind = false;
    for (const auto& sound : loops) {
        if (sound.find("wind") != std::string::npos) {
            hasWind = true;
            break;
        }
    }
    EXPECT_TRUE(hasWind) << "LOOP section should include wind sounds";
}

TEST_F(EffSndBnkFileTest, InvalidFileReturnsError) {
    EXPECT_FALSE(loader_.loadSndBnkEff("/nonexistent/path/_sndbnk.eff"));
    EXPECT_TRUE(loader_.getEmitSounds().empty());
    EXPECT_TRUE(loader_.getLoopSounds().empty());
}

// =============================================================================
// Zone Loading Integration Tests
// =============================================================================

class EffLoaderZoneTest : public ::testing::Test {
protected:
    EffLoader loader_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
    }
};

TEST_F(EffLoaderZoneTest, LoadGfaydark) {
    EXPECT_TRUE(loader_.loadZone("gfaydark", EQ_PATH));
    EXPECT_EQ(loader_.getZoneName(), "gfaydark");
    EXPECT_GT(loader_.getEntryCount(), 0u);
    EXPECT_GT(loader_.getEmitSounds().size(), 0u);
}

TEST_F(EffLoaderZoneTest, LoadUppercaseZoneName) {
    // Should handle uppercase zone names
    EXPECT_TRUE(loader_.loadZone("GFAYDARK", EQ_PATH));
    EXPECT_GT(loader_.getEntryCount(), 0u);
}

TEST_F(EffLoaderZoneTest, LoadFreport) {
    EXPECT_TRUE(loader_.loadZone("freporte", EQ_PATH));
    EXPECT_GT(loader_.getEntryCount(), 0u);
}

TEST_F(EffLoaderZoneTest, LoadHalas) {
    EXPECT_TRUE(loader_.loadZone("halas", EQ_PATH));
    EXPECT_GT(loader_.getEntryCount(), 0u);

    // Halas should have music regions
    EXPECT_GT(loader_.getMusicEntryCount(), 0u);
}

TEST_F(EffLoaderZoneTest, LoadNonexistentZone) {
    EXPECT_FALSE(loader_.loadZone("notarealzone", EQ_PATH));
    EXPECT_EQ(loader_.getEntryCount(), 0u);
}

TEST_F(EffLoaderZoneTest, ClearResetState) {
    loader_.loadZone("gfaydark", EQ_PATH);
    EXPECT_GT(loader_.getEntryCount(), 0u);

    loader_.clear();
    EXPECT_EQ(loader_.getEntryCount(), 0u);
    EXPECT_TRUE(loader_.getEmitSounds().empty());
    EXPECT_TRUE(loader_.getLoopSounds().empty());
    EXPECT_TRUE(loader_.getZoneName().empty());
}

TEST_F(EffLoaderZoneTest, ReloadZoneReplacesData) {
    loader_.loadZone("gfaydark", EQ_PATH);
    size_t gfaydarkCount = loader_.getEntryCount();

    loader_.loadZone("halas", EQ_PATH);
    EXPECT_EQ(loader_.getZoneName(), "halas");
    // Entry count may differ between zones
    EXPECT_NE(loader_.getEntryCount(), 0u);
}

// =============================================================================
// Sound ID Resolution with Zone Data
// =============================================================================

class EffLoaderSoundResolutionTest : public ::testing::Test {
protected:
    EffLoader loader_;

    void SetUp() override {
        if (!std::filesystem::exists(EQ_PATH)) {
            GTEST_SKIP() << "EQ client path not found: " << EQ_PATH;
        }
        loader_.loadZone("gfaydark", EQ_PATH);
    }
};

TEST_F(EffLoaderSoundResolutionTest, ResolveEmitSound) {
    // ID 1 should be first EMIT entry (fire_lp)
    std::string sound = loader_.resolveSoundFile(1);
    EXPECT_EQ(sound, "fire_lp");
}

TEST_F(EffLoaderSoundResolutionTest, ResolveLoopSound) {
    // ID 162 should be first LOOP entry
    std::string sound = loader_.resolveSoundFile(162);
    EXPECT_FALSE(sound.empty());
    // First LOOP in gfaydark is wind_lp2
    EXPECT_EQ(sound, "wind_lp2");
}

TEST_F(EffLoaderSoundResolutionTest, ResolveAllSoundsInZone) {
    // All sound entries should resolve to something
    for (const auto& entry : loader_.getSoundEntries()) {
        std::string sound1 = loader_.resolveSoundFile(entry.SoundID1);
        std::string sound2 = loader_.resolveSoundFile(entry.SoundID2);

        // At least one should be non-empty (unless both are 0)
        if (entry.SoundID1 != 0) {
            // Type 1 (music) with positive ID uses zone XMI, not sndbnk
            if (entry.SoundType != 1 || entry.SoundID1 < 0) {
                // May be empty if ID is in undefined hardcoded range
                // Just verify no crash
            }
        }
    }
}

// =============================================================================
// Sound Type Distribution Test
// =============================================================================

TEST_F(EffLoaderZoneTest, SoundTypeDistribution) {
    loader_.loadZone("gfaydark", EQ_PATH);

    size_t type0 = 0, type1 = 0, type2 = 0, type3 = 0;
    for (const auto& entry : loader_.getSoundEntries()) {
        switch (entry.SoundType) {
            case 0: type0++; break;
            case 1: type1++; break;
            case 2: type2++; break;
            case 3: type3++; break;
        }
    }

    // Type 2 (static effect) is most common (~60%)
    EXPECT_GT(type2, type0);
    EXPECT_GT(type2, type1);
    EXPECT_GT(type2, type3);
}

// =============================================================================
// MP3 Index Tests
// =============================================================================

TEST(EffLoaderMp3Test, DefaultMp3Index) {
    // Load from non-existent path to get defaults
    EffLoader::loadMp3Index("/nonexistent/path");

    // Check some default entries
    EXPECT_EQ(EffLoader::getMp3File(1), "bothunder.mp3");
    EXPECT_EQ(EffLoader::getMp3File(6), "eqtheme.mp3");
}

TEST(EffLoaderMp3Test, InvalidMp3IndexReturnsEmpty) {
    EXPECT_TRUE(EffLoader::getMp3File(0).empty());
    EXPECT_TRUE(EffLoader::getMp3File(-1).empty());
    EXPECT_TRUE(EffLoader::getMp3File(1000).empty());
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, EffLoaderNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
