#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/door_sounds.h"
#include "client/audio/sound_assets.h"
#include "client/graphics/eq/pfs.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// DoorSounds Sound ID Tests
// =============================================================================

class DoorSoundsTest : public ::testing::Test {
protected:
    // Helper to convert string to lowercase
    static std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return result;
    }
};

// Test metal door sounds
TEST_F(DoorSoundsTest, MetalDoorOpenSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Metal, true);
    EXPECT_EQ(soundId, DoorSoundId::METAL_DOOR_OPEN);
    EXPECT_EQ(soundId, 176u);
}

TEST_F(DoorSoundsTest, MetalDoorCloseSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Metal, false);
    EXPECT_EQ(soundId, DoorSoundId::METAL_DOOR_CLOSE);
    EXPECT_EQ(soundId, 175u);
}

// Test stone door sounds
TEST_F(DoorSoundsTest, StoneDoorOpenSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Stone, true);
    EXPECT_EQ(soundId, DoorSoundId::STONE_DOOR_OPEN);
    EXPECT_EQ(soundId, 179u);
}

TEST_F(DoorSoundsTest, StoneDoorCloseSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Stone, false);
    EXPECT_EQ(soundId, DoorSoundId::STONE_DOOR_CLOSE);
    EXPECT_EQ(soundId, 178u);
}

// Test wood door sounds
TEST_F(DoorSoundsTest, WoodDoorOpenSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Wood, true);
    EXPECT_EQ(soundId, DoorSoundId::WOOD_DOOR_OPEN);
    EXPECT_EQ(soundId, 135u);
}

TEST_F(DoorSoundsTest, WoodDoorCloseSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Wood, false);
    EXPECT_EQ(soundId, DoorSoundId::WOOD_DOOR_CLOSE);
    EXPECT_EQ(soundId, 136u);
}

// Test secret door sounds
TEST_F(DoorSoundsTest, SecretDoorOpenSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Secret, true);
    EXPECT_EQ(soundId, DoorSoundId::SECRET_DOOR);
    EXPECT_EQ(soundId, 177u);
}

TEST_F(DoorSoundsTest, SecretDoorCloseSound) {
    // Secret doors use the same sound for open and close
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Secret, false);
    EXPECT_EQ(soundId, DoorSoundId::SECRET_DOOR);
    EXPECT_EQ(soundId, 177u);
}

// Test sliding door sounds
TEST_F(DoorSoundsTest, SlidingDoorOpenSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Sliding, true);
    EXPECT_EQ(soundId, DoorSoundId::SLIDING_DOOR_OPEN);
    EXPECT_EQ(soundId, 184u);
}

TEST_F(DoorSoundsTest, SlidingDoorCloseSound) {
    uint32_t soundId = DoorSounds::getDoorSound(DoorType::Sliding, false);
    EXPECT_EQ(soundId, DoorSoundId::SLIDING_DOOR_CLOSE);
    EXPECT_EQ(soundId, 183u);
}

// =============================================================================
// ObjectSounds Sound ID Tests
// =============================================================================

TEST_F(DoorSoundsTest, LeverSound) {
    uint32_t soundId = DoorSounds::getObjectSound(ObjectType::Lever);
    EXPECT_EQ(soundId, ObjectSoundId::LEVER);
    EXPECT_EQ(soundId, 180u);
}

TEST_F(DoorSoundsTest, DrawbridgeSound) {
    uint32_t soundId = DoorSounds::getObjectSound(ObjectType::Drawbridge);
    EXPECT_EQ(soundId, ObjectSoundId::DRAWBRIDGE_LOOP);
    EXPECT_EQ(soundId, 173u);
}

TEST_F(DoorSoundsTest, ElevatorSound) {
    uint32_t soundId = DoorSounds::getObjectSound(ObjectType::Elevator);
    EXPECT_EQ(soundId, ObjectSoundId::ELEVATOR_LOOP);
    EXPECT_EQ(soundId, 185u);
}

TEST_F(DoorSoundsTest, PortcullisSound) {
    uint32_t soundId = DoorSounds::getObjectSound(ObjectType::Portcullis);
    EXPECT_EQ(soundId, ObjectSoundId::PORTCULLIS_LOOP);
    EXPECT_EQ(soundId, 181u);
}

TEST_F(DoorSoundsTest, SpearTrapDownSound) {
    uint32_t soundId = DoorSounds::getObjectSound(ObjectType::SpearTrapDown);
    EXPECT_EQ(soundId, ObjectSoundId::SPEAR_DOWN);
    EXPECT_EQ(soundId, 187u);
}

TEST_F(DoorSoundsTest, SpearTrapUpSound) {
    uint32_t soundId = DoorSounds::getObjectSound(ObjectType::SpearTrapUp);
    EXPECT_EQ(soundId, ObjectSoundId::SPEAR_UP);
    EXPECT_EQ(soundId, 188u);
}

TEST_F(DoorSoundsTest, TrapDoorSound) {
    uint32_t soundId = DoorSounds::getObjectSound(ObjectType::TrapDoor);
    EXPECT_EQ(soundId, ObjectSoundId::TRAP_DOOR);
    EXPECT_EQ(soundId, 189u);
}

// =============================================================================
// Door Sound Filename Tests
// =============================================================================

TEST_F(DoorSoundsTest, MetalDoorOpenFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Metal, true);
    EXPECT_EQ(toLower(filename), "doormt_o.wav");
}

TEST_F(DoorSoundsTest, MetalDoorCloseFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Metal, false);
    EXPECT_EQ(toLower(filename), "doormt_c.wav");
}

TEST_F(DoorSoundsTest, StoneDoorOpenFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Stone, true);
    EXPECT_EQ(toLower(filename), "doorst_o.wav");
}

TEST_F(DoorSoundsTest, StoneDoorCloseFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Stone, false);
    EXPECT_EQ(toLower(filename), "doorst_c.wav");
}

TEST_F(DoorSoundsTest, WoodDoorOpenFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Wood, true);
    EXPECT_EQ(toLower(filename), "doorwd_o.wav");
}

TEST_F(DoorSoundsTest, WoodDoorCloseFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Wood, false);
    EXPECT_EQ(toLower(filename), "doorwd_c.wav");
}

TEST_F(DoorSoundsTest, SecretDoorFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Secret, true);
    EXPECT_EQ(toLower(filename), "doorsecr.wav");
    // Same for close
    filename = DoorSounds::getDoorSoundFilename(DoorType::Secret, false);
    EXPECT_EQ(toLower(filename), "doorsecr.wav");
}

TEST_F(DoorSoundsTest, SlidingDoorOpenFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Sliding, true);
    EXPECT_EQ(toLower(filename), "sldorsto.wav");
}

TEST_F(DoorSoundsTest, SlidingDoorCloseFilename) {
    std::string filename = DoorSounds::getDoorSoundFilename(DoorType::Sliding, false);
    EXPECT_EQ(toLower(filename), "sldorstc.wav");
}

// =============================================================================
// Object Sound Filename Tests
// =============================================================================

TEST_F(DoorSoundsTest, LeverFilename) {
    std::string filename = DoorSounds::getObjectSoundFilename(ObjectType::Lever);
    EXPECT_EQ(toLower(filename), "lever.wav");
}

TEST_F(DoorSoundsTest, DrawbridgeFilename) {
    std::string filename = DoorSounds::getObjectSoundFilename(ObjectType::Drawbridge);
    EXPECT_EQ(toLower(filename), "dbrdg_lp.wav");
}

TEST_F(DoorSoundsTest, ElevatorFilename) {
    std::string filename = DoorSounds::getObjectSoundFilename(ObjectType::Elevator);
    EXPECT_EQ(toLower(filename), "elevloop.wav");
}

TEST_F(DoorSoundsTest, PortcullisFilename) {
    std::string filename = DoorSounds::getObjectSoundFilename(ObjectType::Portcullis);
    EXPECT_EQ(toLower(filename), "portc_lp.wav");
}

TEST_F(DoorSoundsTest, SpearTrapDownFilename) {
    std::string filename = DoorSounds::getObjectSoundFilename(ObjectType::SpearTrapDown);
    EXPECT_EQ(toLower(filename), "speardn.wav");
}

TEST_F(DoorSoundsTest, SpearTrapUpFilename) {
    std::string filename = DoorSounds::getObjectSoundFilename(ObjectType::SpearTrapUp);
    EXPECT_EQ(toLower(filename), "spearup.wav");
}

TEST_F(DoorSoundsTest, TrapDoorFilename) {
    std::string filename = DoorSounds::getObjectSoundFilename(ObjectType::TrapDoor);
    EXPECT_EQ(toLower(filename), "trapdoor.wav");
}

// =============================================================================
// HasSeparateOpenCloseSound Tests
// =============================================================================

TEST_F(DoorSoundsTest, MetalDoorHasSeparateSounds) {
    EXPECT_TRUE(DoorSounds::hasSeparateOpenCloseSound(DoorType::Metal));
}

TEST_F(DoorSoundsTest, StoneDoorHasSeparateSounds) {
    EXPECT_TRUE(DoorSounds::hasSeparateOpenCloseSound(DoorType::Stone));
}

TEST_F(DoorSoundsTest, WoodDoorHasSeparateSounds) {
    EXPECT_TRUE(DoorSounds::hasSeparateOpenCloseSound(DoorType::Wood));
}

TEST_F(DoorSoundsTest, SecretDoorHasNoSeparateSounds) {
    EXPECT_FALSE(DoorSounds::hasSeparateOpenCloseSound(DoorType::Secret));
}

TEST_F(DoorSoundsTest, SlidingDoorHasSeparateSounds) {
    EXPECT_TRUE(DoorSounds::hasSeparateOpenCloseSound(DoorType::Sliding));
}

// =============================================================================
// SoundAssets Integration Tests (verify sound IDs exist in SoundAssets.txt)
// =============================================================================

class DoorSoundsAssetsTest : public ::testing::Test {
protected:
    SoundAssets assets_;

    void SetUp() override {
        std::string assetsPath = std::string(EQ_PATH) + "/SoundAssets.txt";
        if (!std::filesystem::exists(assetsPath)) {
            GTEST_SKIP() << "SoundAssets.txt not found at: " << assetsPath;
        }
        if (!assets_.loadFromFile(assetsPath)) {
            GTEST_SKIP() << "Failed to load SoundAssets.txt";
        }
    }
};

TEST_F(DoorSoundsAssetsTest, AllDoorSoundIdsExist) {
    // Metal doors
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::METAL_DOOR_OPEN))
        << "Metal door open sound ID " << DoorSoundId::METAL_DOOR_OPEN << " not found";
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::METAL_DOOR_CLOSE))
        << "Metal door close sound ID " << DoorSoundId::METAL_DOOR_CLOSE << " not found";

    // Stone doors
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::STONE_DOOR_OPEN))
        << "Stone door open sound ID " << DoorSoundId::STONE_DOOR_OPEN << " not found";
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::STONE_DOOR_CLOSE))
        << "Stone door close sound ID " << DoorSoundId::STONE_DOOR_CLOSE << " not found";

    // Wood doors
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::WOOD_DOOR_OPEN))
        << "Wood door open sound ID " << DoorSoundId::WOOD_DOOR_OPEN << " not found";
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::WOOD_DOOR_CLOSE))
        << "Wood door close sound ID " << DoorSoundId::WOOD_DOOR_CLOSE << " not found";

    // Secret doors
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::SECRET_DOOR))
        << "Secret door sound ID " << DoorSoundId::SECRET_DOOR << " not found";

    // Sliding doors
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::SLIDING_DOOR_OPEN))
        << "Sliding door open sound ID " << DoorSoundId::SLIDING_DOOR_OPEN << " not found";
    EXPECT_TRUE(assets_.hasSound(DoorSoundId::SLIDING_DOOR_CLOSE))
        << "Sliding door close sound ID " << DoorSoundId::SLIDING_DOOR_CLOSE << " not found";
}

TEST_F(DoorSoundsAssetsTest, AllObjectSoundIdsExist) {
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::LEVER))
        << "Lever sound ID " << ObjectSoundId::LEVER << " not found";
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::DRAWBRIDGE_LOOP))
        << "Drawbridge loop sound ID " << ObjectSoundId::DRAWBRIDGE_LOOP << " not found";
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::DRAWBRIDGE_STOP))
        << "Drawbridge stop sound ID " << ObjectSoundId::DRAWBRIDGE_STOP << " not found";
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::ELEVATOR_LOOP))
        << "Elevator loop sound ID " << ObjectSoundId::ELEVATOR_LOOP << " not found";
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::PORTCULLIS_LOOP))
        << "Portcullis loop sound ID " << ObjectSoundId::PORTCULLIS_LOOP << " not found";
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::PORTCULLIS_STOP))
        << "Portcullis stop sound ID " << ObjectSoundId::PORTCULLIS_STOP << " not found";
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::SPEAR_DOWN))
        << "Spear down sound ID " << ObjectSoundId::SPEAR_DOWN << " not found";
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::SPEAR_UP))
        << "Spear up sound ID " << ObjectSoundId::SPEAR_UP << " not found";
    EXPECT_TRUE(assets_.hasSound(ObjectSoundId::TRAP_DOOR))
        << "Trap door sound ID " << ObjectSoundId::TRAP_DOOR << " not found";
}

TEST_F(DoorSoundsAssetsTest, DoorSoundFilenamesMatchAssets) {
    // Verify that our filename constants match what SoundAssets.txt says
    auto checkFilename = [this](uint32_t soundId, const std::string& expectedFilename) {
        std::string actualFilename = assets_.getFilename(soundId);
        // Case-insensitive comparison
        std::string lowerActual = actualFilename;
        std::string lowerExpected = expectedFilename;
        std::transform(lowerActual.begin(), lowerActual.end(), lowerActual.begin(), ::tolower);
        std::transform(lowerExpected.begin(), lowerExpected.end(), lowerExpected.begin(), ::tolower);
        EXPECT_EQ(lowerActual, lowerExpected)
            << "Sound ID " << soundId << " filename mismatch: expected " << expectedFilename
            << ", got " << actualFilename;
    };

    // Check door sounds
    checkFilename(DoorSoundId::METAL_DOOR_OPEN, "doormt_o.wav");
    checkFilename(DoorSoundId::METAL_DOOR_CLOSE, "doormt_c.wav");
    checkFilename(DoorSoundId::STONE_DOOR_OPEN, "doorst_o.wav");
    checkFilename(DoorSoundId::STONE_DOOR_CLOSE, "doorst_c.wav");
    checkFilename(DoorSoundId::WOOD_DOOR_OPEN, "doorwd_o.wav");
    checkFilename(DoorSoundId::WOOD_DOOR_CLOSE, "doorwd_c.wav");
    checkFilename(DoorSoundId::SECRET_DOOR, "doorsecr.wav");
    checkFilename(DoorSoundId::SLIDING_DOOR_OPEN, "sldorsto.wav");
    checkFilename(DoorSoundId::SLIDING_DOOR_CLOSE, "sldorstc.wav");

    // Check object sounds
    checkFilename(ObjectSoundId::LEVER, "lever.wav");
    checkFilename(ObjectSoundId::DRAWBRIDGE_LOOP, "dbrdg_lp.wav");
    checkFilename(ObjectSoundId::TRAP_DOOR, "trapdoor.wav");
    checkFilename(ObjectSoundId::SPEAR_DOWN, "speardn.wav");
    checkFilename(ObjectSoundId::SPEAR_UP, "spearup.wav");
}

// =============================================================================
// PFS Archive Integration Tests (verify sound files exist in snd*.pfs archives)
// =============================================================================

class DoorSoundsPfsTest : public ::testing::Test {
protected:
    std::vector<std::unique_ptr<EQT::Graphics::PfsArchive>> archives_;

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

        if (archives_.empty()) {
            GTEST_SKIP() << "No snd*.pfs archives found in EQ client directory";
        }
    }

    // Helper to check if a file exists in any loaded archive
    bool existsInAnyArchive(const std::string& upperFilename, const std::string& lowerFilename) {
        for (const auto& archive : archives_) {
            if (archive->exists(upperFilename) || archive->exists(lowerFilename)) {
                return true;
            }
        }
        return false;
    }
};

TEST_F(DoorSoundsPfsTest, DoorSoundFilesExist) {
    // Check that all door sound files exist in some snd*.pfs archive
    // Note: PFS filenames are case-insensitive internally, but we check both cases

    EXPECT_TRUE(existsInAnyArchive("DoorMt_O.WAV", "doormt_o.wav"))
        << "Metal door open sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("DoorMt_C.WAV", "doormt_c.wav"))
        << "Metal door close sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("DoorSt_O.WAV", "doorst_o.wav"))
        << "Stone door open sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("DoorSt_C.WAV", "doorst_c.wav"))
        << "Stone door close sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("DoorWd_O.WAV", "doorwd_o.wav"))
        << "Wood door open sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("DoorWd_C.WAV", "doorwd_c.wav"))
        << "Wood door close sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("DoorSecr.WAV", "doorsecr.wav"))
        << "Secret door sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("SlDorStO.WAV", "sldorsto.wav"))
        << "Sliding door open sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("SlDorStC.WAV", "sldorstc.wav"))
        << "Sliding door close sound not found in any snd*.pfs archive";
}

TEST_F(DoorSoundsPfsTest, ObjectSoundFilesExist) {
    // Check that all object sound files exist in some snd*.pfs archive

    EXPECT_TRUE(existsInAnyArchive("Lever.WAV", "lever.wav"))
        << "Lever sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("Dbrdg_Lp.WAV", "dbrdg_lp.wav"))
        << "Drawbridge loop sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("ElevLoop.wav", "elevloop.wav"))
        << "Elevator loop sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("PortC_Lp.WAV", "portc_lp.wav"))
        << "Portcullis loop sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("SpearDn.WAV", "speardn.wav"))
        << "Spear down sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("SpearUp.WAV", "spearup.wav"))
        << "Spear up sound not found in any snd*.pfs archive";
    EXPECT_TRUE(existsInAnyArchive("TrapDoor.WAV", "trapdoor.wav"))
        << "Trap door sound not found in any snd*.pfs archive";
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, DoorSoundsNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
