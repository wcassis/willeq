#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/ui_sounds.h"
#include "client/audio/sound_assets.h"
#include "client/graphics/eq/pfs.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <memory>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// UISounds Basic Tests
// =============================================================================

class UISoundsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Nothing to set up for basic tests
    }
};

TEST_F(UISoundsTest, AllTypesHaveFilenames) {
    // Test that all UI sound types (except Count) have valid filenames
    for (int i = 0; i < static_cast<int>(UISoundType::Count); ++i) {
        UISoundType type = static_cast<UISoundType>(i);
        std::string filename = UISounds::getSoundFile(type);
        EXPECT_FALSE(filename.empty())
            << "UISoundType::" << UISounds::getTypeName(type) << " has no filename";
    }
}

TEST_F(UISoundsTest, CountTypeIsInvalid) {
    EXPECT_FALSE(UISounds::isValid(UISoundType::Count));
    EXPECT_TRUE(UISounds::getSoundFile(UISoundType::Count).empty());
}

TEST_F(UISoundsTest, AllValidTypesAreMarkedValid) {
    for (int i = 0; i < static_cast<int>(UISoundType::Count); ++i) {
        UISoundType type = static_cast<UISoundType>(i);
        EXPECT_TRUE(UISounds::isValid(type))
            << "UISoundType::" << UISounds::getTypeName(type) << " should be valid";
    }
}

TEST_F(UISoundsTest, AllTypesHaveNames) {
    for (int i = 0; i < static_cast<int>(UISoundType::Count); ++i) {
        UISoundType type = static_cast<UISoundType>(i);
        std::string name = UISounds::getTypeName(type);
        EXPECT_FALSE(name.empty())
            << "UISoundType " << i << " has no name";
        EXPECT_NE(name, "Unknown")
            << "UISoundType " << i << " has Unknown name";
    }
}

TEST_F(UISoundsTest, MostTypesHaveSoundIds) {
    // Most types should have sound IDs (except YouveGotMail which uses string key)
    int typesWithIds = 0;
    for (int i = 0; i < static_cast<int>(UISoundType::Count); ++i) {
        UISoundType type = static_cast<UISoundType>(i);
        if (UISounds::getSoundId(type).has_value()) {
            ++typesWithIds;
        }
    }

    // At least 15 types should have sound IDs
    EXPECT_GE(typesWithIds, 15)
        << "Expected at least 15 UI sound types with sound IDs";
}

// =============================================================================
// Specific UI Sound Tests
// =============================================================================

TEST_F(UISoundsTest, LevelUpSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::LevelUp), "LevelUp.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::LevelUp).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::LevelUp).value(), UISoundId::LEVEL_UP);
}

TEST_F(UISoundsTest, LevelDownSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::LevelDown), "LevDn.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::LevelDown).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::LevelDown).value(), UISoundId::LEVEL_DOWN);
}

TEST_F(UISoundsTest, BoatBellSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::BoatBell), "BoatBell.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::BoatBell).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::BoatBell).value(), UISoundId::BOAT_BELL);
}

TEST_F(UISoundsTest, ButtonClickSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::ButtonClick), "Button_1.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::ButtonClick).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::ButtonClick).value(), UISoundId::BUTTON_CLICK);
}

TEST_F(UISoundsTest, BuyItemSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::BuyItem), "BuyItem.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::BuyItem).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::BuyItem).value(), UISoundId::BUY_ITEM);
}

TEST_F(UISoundsTest, SellItemReusesBuySound) {
    // SellItem should reuse BuyItem sound
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::SellItem),
              UISounds::getSoundFile(UISoundType::BuyItem));
    EXPECT_EQ(UISounds::getSoundId(UISoundType::SellItem),
              UISounds::getSoundId(UISoundType::BuyItem));
}

TEST_F(UISoundsTest, ChestSounds) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::ChestOpen), "Chest_Op.WAV");
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::ChestClose), "Chest_Cl.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::ChestOpen).has_value());
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::ChestClose).has_value());
}

TEST_F(UISoundsTest, WoodDoorSounds) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::DoorWoodOpen), "DoorWd_O.WAV");
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::DoorWoodClose), "DoorWd_C.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::DoorWoodOpen).has_value());
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::DoorWoodClose).has_value());
}

TEST_F(UISoundsTest, MetalDoorSounds) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::DoorMetalOpen), "DoorMt_O.WAV");
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::DoorMetalClose), "DoorMt_C.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::DoorMetalOpen).has_value());
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::DoorMetalClose).has_value());
}

TEST_F(UISoundsTest, StoneDoorSounds) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::DoorStoneOpen), "DoorSt_O.WAV");
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::DoorStoneClose), "DoorSt_C.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::DoorStoneOpen).has_value());
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::DoorStoneClose).has_value());
}

TEST_F(UISoundsTest, SpecialDoorSounds) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::DoorSecret), "DoorSecr.WAV");
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::TrapDoor), "TrapDoor.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::DoorSecret).has_value());
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::TrapDoor).has_value());
}

TEST_F(UISoundsTest, TeleportSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::Teleport), "Teleport.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::Teleport).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::Teleport).value(), UISoundId::TELEPORT);
}

TEST_F(UISoundsTest, YouveGotMailSound) {
    // YouveGotMail uses a string key, not numeric ID
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::YouveGotMail), "mail1.wav");
    EXPECT_FALSE(UISounds::getSoundId(UISoundType::YouveGotMail).has_value());
}

TEST_F(UISoundsTest, EndQuestSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::EndQuest), "EndQuest.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::EndQuest).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::EndQuest).value(), UISoundId::END_QUEST);
}

TEST_F(UISoundsTest, DrinkSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::Drink), "Drink.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::Drink).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::Drink).value(), UISoundId::DRINK);
}

// =============================================================================
// Sound ID Constants Tests
// =============================================================================

TEST_F(UISoundsTest, SoundIdConstants) {
    // Verify sound ID constants match expected values from SoundAssets.txt
    EXPECT_EQ(UISoundId::LEVEL_UP, 139u);
    EXPECT_EQ(UISoundId::LEVEL_DOWN, 140u);
    EXPECT_EQ(UISoundId::END_QUEST, 141u);
    EXPECT_EQ(UISoundId::BOAT_BELL, 170u);
    EXPECT_EQ(UISoundId::BUY_ITEM, 138u);
    EXPECT_EQ(UISoundId::CHEST_CLOSE, 133u);
    EXPECT_EQ(UISoundId::CHEST_OPEN, 134u);
    EXPECT_EQ(UISoundId::DOOR_WOOD_OPEN, 135u);
    EXPECT_EQ(UISoundId::DOOR_WOOD_CLOSE, 136u);
    EXPECT_EQ(UISoundId::DOOR_METAL_OPEN, 176u);
    EXPECT_EQ(UISoundId::DOOR_METAL_CLOSE, 175u);
    EXPECT_EQ(UISoundId::DOOR_STONE_OPEN, 179u);
    EXPECT_EQ(UISoundId::DOOR_STONE_CLOSE, 178u);
    EXPECT_EQ(UISoundId::DOOR_SECRET, 177u);
    EXPECT_EQ(UISoundId::TRAP_DOOR, 189u);
    EXPECT_EQ(UISoundId::BUTTON_CLICK, 142u);
    EXPECT_EQ(UISoundId::TELEPORT, 137u);
    EXPECT_EQ(UISoundId::DRINK, 149u);
}

// =============================================================================
// SoundAssets Integration Tests (requires EQ client files)
// =============================================================================

class UISoundsIntegrationTest : public ::testing::Test {
protected:
    SoundAssets assets_;
    bool assetsLoaded_ = false;

    void SetUp() override {
        std::string assetsPath = std::string(EQ_PATH) + "/SoundAssets.txt";
        if (std::filesystem::exists(assetsPath)) {
            assetsLoaded_ = assets_.loadFromFile(assetsPath);
        }
    }

    void SkipIfNoAssets() {
        if (!assetsLoaded_) {
            GTEST_SKIP() << "SoundAssets.txt not found or failed to load";
        }
    }
};

TEST_F(UISoundsIntegrationTest, SoundIdsExistInSoundAssets) {
    SkipIfNoAssets();

    // Verify all UI sound IDs exist in SoundAssets.txt
    for (int i = 0; i < static_cast<int>(UISoundType::Count); ++i) {
        UISoundType type = static_cast<UISoundType>(i);
        auto soundId = UISounds::getSoundId(type);

        if (soundId.has_value()) {
            EXPECT_TRUE(assets_.hasSound(soundId.value()))
                << "UISoundType::" << UISounds::getTypeName(type)
                << " (ID " << soundId.value() << ") not found in SoundAssets.txt";
        }
    }
}

TEST_F(UISoundsIntegrationTest, SoundFilenamesMatchSoundAssets) {
    SkipIfNoAssets();

    // Verify filenames match what's in SoundAssets.txt
    for (int i = 0; i < static_cast<int>(UISoundType::Count); ++i) {
        UISoundType type = static_cast<UISoundType>(i);
        auto soundId = UISounds::getSoundId(type);

        if (soundId.has_value()) {
            std::string expected = UISounds::getSoundFile(type);
            std::string actual = assets_.getFilename(soundId.value());

            // Case-insensitive comparison
            std::string expectedLower = expected;
            std::string actualLower = actual;
            std::transform(expectedLower.begin(), expectedLower.end(),
                           expectedLower.begin(), ::tolower);
            std::transform(actualLower.begin(), actualLower.end(),
                           actualLower.begin(), ::tolower);

            EXPECT_EQ(expectedLower, actualLower)
                << "UISoundType::" << UISounds::getTypeName(type)
                << " filename mismatch: expected '" << expected
                << "', got '" << actual << "'";
        }
    }
}

// =============================================================================
// Sound File Existence Tests (requires EQ client PFS archives)
// =============================================================================

class UISoundsPfsTest : public ::testing::Test {
protected:
    std::vector<std::unique_ptr<EQT::Graphics::PfsArchive>> archives_;
    std::string soundsDir_;

    void SetUp() override {
        // Load all snd*.pfs archives to search for sound files
        for (int i = 1; i <= 17; ++i) {
            std::string archivePath = std::string(EQ_PATH) + "/snd" + std::to_string(i) + ".pfs";
            if (std::filesystem::exists(archivePath)) {
                auto archive = std::make_unique<EQT::Graphics::PfsArchive>();
                if (archive->open(archivePath)) {
                    archives_.push_back(std::move(archive));
                }
            }
        }

        // Also set up sounds directory path (some sounds are loose files)
        soundsDir_ = std::string(EQ_PATH) + "/sounds/";

        if (archives_.empty() && !std::filesystem::exists(soundsDir_)) {
            GTEST_SKIP() << "No snd*.pfs archives or sounds directory found";
        }
    }

    // Helper to check if a file exists in any loaded archive or sounds directory
    bool existsInAnyArchive(const std::string& filename) {
        // First check PFS archives
        for (const auto& archive : archives_) {
            if (archive->exists(filename)) {
                return true;
            }
        }

        // Try lowercase in archives
        std::string lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (const auto& archive : archives_) {
            if (archive->exists(lower)) {
                return true;
            }
        }

        // Try uppercase in archives
        std::string upper = filename;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        for (const auto& archive : archives_) {
            if (archive->exists(upper)) {
                return true;
            }
        }

        // Also check sounds directory (some sounds are loose files, not in PFS)
        if (std::filesystem::exists(soundsDir_ + filename) ||
            std::filesystem::exists(soundsDir_ + lower) ||
            std::filesystem::exists(soundsDir_ + upper)) {
            return true;
        }

        return false;
    }
};

TEST_F(UISoundsPfsTest, LevelUpWavExists) {
    EXPECT_TRUE(existsInAnyArchive("LevelUp.WAV"))
        << "LevelUp.WAV not found in any snd*.pfs archive";
}

TEST_F(UISoundsPfsTest, BoatBellWavExists) {
    EXPECT_TRUE(existsInAnyArchive("BoatBell.WAV"))
        << "BoatBell.WAV not found in any snd*.pfs archive";
}

TEST_F(UISoundsPfsTest, ButtonClickWavExists) {
    EXPECT_TRUE(existsInAnyArchive("Button_1.WAV"))
        << "Button_1.WAV not found in any snd*.pfs archive";
}

TEST_F(UISoundsPfsTest, BuyItemWavExists) {
    EXPECT_TRUE(existsInAnyArchive("BuyItem.WAV"))
        << "BuyItem.WAV not found in any snd*.pfs archive";
}

TEST_F(UISoundsPfsTest, AllUISoundFilesExist) {
    int missingCount = 0;
    for (int i = 0; i < static_cast<int>(UISoundType::Count); ++i) {
        UISoundType type = static_cast<UISoundType>(i);
        std::string filename = UISounds::getSoundFile(type);

        if (!existsInAnyArchive(filename)) {
            ++missingCount;
            ADD_FAILURE() << "UI sound file not found: " << filename
                          << " (for " << UISounds::getTypeName(type) << ")";
        }
    }

    // All UI sound files should exist in the PFS archives
    EXPECT_EQ(missingCount, 0)
        << "Some UI sound files missing from snd*.pfs archives";
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, UISoundsAudioNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
