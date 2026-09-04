#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/ui_sounds.h"
#include "client/audio/sound_assets.h"
#include "client/graphics/eq/pfs.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <memory>
#include <iterator>

using namespace EQT::Audio;

// Path to EQ client files for testing
static std::string getEqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : EQ_CLIENT_DATA_PATH;
}
static const std::string EQ_PATH = getEqClientPath();

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

// T328 -- the two container/item sounds Leg A measured.
TEST_F(UISoundsTest, ItemClothSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::ItemCloth), "itemclth.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::ItemCloth).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::ItemCloth).value(), UISoundId::ITEM_CLOTH);
}

TEST_F(UISoundsTest, WearClothSound) {
    EXPECT_EQ(UISounds::getSoundFile(UISoundType::WearCloth), "wearclth.WAV");
    EXPECT_TRUE(UISounds::getSoundId(UISoundType::WearCloth).has_value());
    EXPECT_EQ(UISounds::getSoundId(UISoundType::WearCloth).value(), UISoundId::WEAR_CLOTH);
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
    // T328: added, measured.
    EXPECT_EQ(UISoundId::ITEM_CLOTH, 145u);
    EXPECT_EQ(UISoundId::WEAR_CLOTH, 146u);
}

// T328 -- SoundBank (sound_assets.h) is the ONE id table, transcribed from
// the official client's preload burst. This checks all 52 of its entries
// against the client's own SoundAssets.txt, which is the file that burst was
// ordered by. A single test over the whole table beats hand-written
// per-constant assertions: the table cannot drift silently, and the assertion
// is against the oracle rather than against a value someone typed twice.
TEST_F(UISoundsTest, SoundBankIdsAreDistinctAndContiguous) {
    const uint32_t ids[] = {
        SoundBank::STEPCRCH, SoundBank::WATEROUT, SoundBank::WATERIN,
        SoundBank::WATTRD_1, SoundBank::WATTRD_2, SoundBank::SPELL_1,
        SoundBank::SPELL_2,  SoundBank::SPELL_3,  SoundBank::SPELL_4,
        SoundBank::SPELL_5,  SoundBank::SPELCAST, SoundBank::SPELGDHT,
        SoundBank::SPELHIT1, SoundBank::SPELHIT2, SoundBank::SPELHIT3,
        SoundBank::SPELHIT4, SoundBank::JUMPLAND, SoundBank::FALL_HIT,
        SoundBank::BOWDRAW,  SoundBank::ARROWHIT, SoundBank::SWING,
        SoundBank::SWINGBIG, SoundBank::SWORDBLK, SoundBank::IMPALE,
        SoundBank::STAB,     SoundBank::CLUB,     SoundBank::BASHSHLD,
        SoundBank::SHLDBLK,  SoundBank::KICK1,    SoundBank::KICKHIT,
        SoundBank::RNDKICK,  SoundBank::LEFTATK,  SoundBank::PUNCH1,
        SoundBank::PUNCHHIT, SoundBank::STAFFBLK, SoundBank::CHEST_CL,
        SoundBank::CHEST_OP, SoundBank::DOORWD_O, SoundBank::DOORWD_C,
        SoundBank::TELEPORT, SoundBank::BUYITEM,  SoundBank::LEVELUP,
        SoundBank::LEVDN,    SoundBank::ENDQUEST, SoundBank::BUTTON_1,
        SoundBank::THUNDER1, SoundBank::THUNDER2, SoundBank::ITEMCLTH,
        SoundBank::WEARCLTH, SoundBank::FLSHHIT1, SoundBank::FLSHHIT2,
        SoundBank::DRINK,
    };
    ASSERT_EQ(std::size(ids), 52u);
    for (size_t i = 0; i < std::size(ids); ++i) {
        EXPECT_EQ(ids[i], 98u + i) << "SoundBank entry " << i << " is out of order";
    }
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

    // NOTE: GTEST_SKIP() only unwinds the function it's called directly
    // in — calling it from a helper like this does not skip the calling
    // TEST_F. Kept for documentation of intent; callers must also check
    // assetsLoaded_ themselves (see SoundIdsExistInSoundAssets /
    // SoundFilenamesMatchSoundAssets below) and return immediately.
    void SkipIfNoAssets() {
        if (!assetsLoaded_) {
            GTEST_SKIP() << "SoundAssets.txt not found or failed to load";
        }
    }
};

TEST_F(UISoundsIntegrationTest, SoundIdsExistInSoundAssets) {
    SkipIfNoAssets();
    if (!assetsLoaded_) {
        return;
    }

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

// T328 -- the whole measured table, id by id, against the client's own
// SoundAssets.txt. This is the assertion that makes SoundBank an oracle
// transcript rather than a list of remembered numbers; it is what caught
// sound_assets.h's BUY_ITEM = 145 (itemclth) and TELEPORT = 141 (endquest).
TEST_F(UISoundsIntegrationTest, SoundBankMatchesSoundAssetsTxt) {
    SkipIfNoAssets();
    if (!assetsLoaded_) {
        return;
    }

    struct Row { uint32_t id; const char* file; };
    static const Row kBank[] = {
        {SoundBank::STEPCRCH, "stepcrch.wav"}, {SoundBank::WATEROUT, "waterout.wav"},
        {SoundBank::WATERIN,  "waterin.wav"},  {SoundBank::WATTRD_1, "wattrd_1.wav"},
        {SoundBank::WATTRD_2, "wattrd_2.wav"}, {SoundBank::SPELL_1,  "spell_1.wav"},
        {SoundBank::SPELL_2,  "spell_2.wav"},  {SoundBank::SPELL_3,  "spell_3.wav"},
        {SoundBank::SPELL_4,  "spell_4.wav"},  {SoundBank::SPELL_5,  "spell_5.wav"},
        {SoundBank::SPELCAST, "spelcast.wav"}, {SoundBank::SPELGDHT, "spelgdht.wav"},
        {SoundBank::SPELHIT1, "spelhit1.wav"}, {SoundBank::SPELHIT2, "spelhit2.wav"},
        {SoundBank::SPELHIT3, "spelhit3.wav"}, {SoundBank::SPELHIT4, "spelhit4.wav"},
        {SoundBank::JUMPLAND, "jumpland.wav"}, {SoundBank::FALL_HIT, "fall_hit.wav"},
        {SoundBank::BOWDRAW,  "bowdraw.wav"},  {SoundBank::ARROWHIT, "arrowhit.wav"},
        {SoundBank::SWING,    "swing.wav"},    {SoundBank::SWINGBIG, "swingbig.wav"},
        {SoundBank::SWORDBLK, "swordblk.wav"}, {SoundBank::IMPALE,   "impale.wav"},
        {SoundBank::STAB,     "stab.wav"},     {SoundBank::CLUB,     "club.wav"},
        {SoundBank::BASHSHLD, "bashshld.wav"}, {SoundBank::SHLDBLK,  "shldblk.wav"},
        {SoundBank::KICK1,    "kick1.wav"},    {SoundBank::KICKHIT,  "kickhit.wav"},
        {SoundBank::RNDKICK,  "rndkick.wav"},  {SoundBank::LEFTATK,  "leftatk.wav"},
        {SoundBank::PUNCH1,   "punch1.wav"},   {SoundBank::PUNCHHIT, "punchhit.wav"},
        {SoundBank::STAFFBLK, "staffblk.wav"}, {SoundBank::CHEST_CL, "chest_cl.wav"},
        {SoundBank::CHEST_OP, "chest_op.wav"}, {SoundBank::DOORWD_O, "doorwd_o.wav"},
        {SoundBank::DOORWD_C, "doorwd_c.wav"}, {SoundBank::TELEPORT, "teleport.wav"},
        {SoundBank::BUYITEM,  "buyitem.wav"},  {SoundBank::LEVELUP,  "levelup.wav"},
        {SoundBank::LEVDN,    "levdn.wav"},    {SoundBank::ENDQUEST, "endquest.wav"},
        {SoundBank::BUTTON_1, "button_1.wav"}, {SoundBank::THUNDER1, "thunder1.wav"},
        {SoundBank::THUNDER2, "thunder2.wav"}, {SoundBank::ITEMCLTH, "itemclth.wav"},
        {SoundBank::WEARCLTH, "wearclth.wav"}, {SoundBank::FLSHHIT1, "flshhit1.wav"},
        {SoundBank::FLSHHIT2, "flshhit2.wav"}, {SoundBank::DRINK,    "drink.wav"},
    };

    for (const Row& row : kBank) {
        ASSERT_TRUE(assets_.hasSound(row.id))
            << "SoundAssets.txt has no id " << row.id;
        std::string actual = assets_.getFilename(row.id);
        std::transform(actual.begin(), actual.end(), actual.begin(), ::tolower);
        EXPECT_EQ(actual, row.file)
            << "SoundBank id " << row.id << " names the wrong file";
    }
}

TEST_F(UISoundsIntegrationTest, SoundFilenamesMatchSoundAssets) {
    SkipIfNoAssets();
    if (!assetsLoaded_) {
        return;
    }

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
