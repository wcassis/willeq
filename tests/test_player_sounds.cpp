#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/player_sounds.h"

using namespace EQT::Audio;

// =============================================================================
// Race Category Tests
// =============================================================================

class PlayerSoundsRaceCategoryTest : public ::testing::Test {
protected:
    // EverQuest playable race IDs
    static constexpr uint16_t Human = 1;
    static constexpr uint16_t Barbarian = 2;
    static constexpr uint16_t Erudite = 3;
    static constexpr uint16_t WoodElf = 4;
    static constexpr uint16_t HighElf = 5;
    static constexpr uint16_t DarkElf = 6;
    static constexpr uint16_t HalfElf = 7;
    static constexpr uint16_t Dwarf = 8;
    static constexpr uint16_t Troll = 9;
    static constexpr uint16_t Ogre = 10;
    static constexpr uint16_t Halfling = 11;
    static constexpr uint16_t Gnome = 12;
    static constexpr uint16_t Iksar = 128;
    static constexpr uint16_t VahShir = 130;
    static constexpr uint16_t Froglok = 330;
};

TEST_F(PlayerSoundsRaceCategoryTest, StandardRaces) {
    // Standard voice races: Human, Erudite, Dark Elf, Troll, Ogre, Iksar, Dwarf, Vah Shir, Froglok
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(Human));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(Erudite));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(DarkElf));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(Dwarf));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(Troll));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(Ogre));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(Iksar));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(VahShir));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(Froglok));
}

TEST_F(PlayerSoundsRaceCategoryTest, BarbarianCategory) {
    // Only Barbarian has the Barbarian voice category
    EXPECT_EQ(RaceCategory::Barbarian, PlayerSounds::getRaceCategory(Barbarian));
}

TEST_F(PlayerSoundsRaceCategoryTest, LightRaces) {
    // Light voice races: Wood Elf, High Elf, Half Elf, Halfling, Gnome
    EXPECT_EQ(RaceCategory::Light, PlayerSounds::getRaceCategory(WoodElf));
    EXPECT_EQ(RaceCategory::Light, PlayerSounds::getRaceCategory(HighElf));
    EXPECT_EQ(RaceCategory::Light, PlayerSounds::getRaceCategory(HalfElf));
    EXPECT_EQ(RaceCategory::Light, PlayerSounds::getRaceCategory(Halfling));
    EXPECT_EQ(RaceCategory::Light, PlayerSounds::getRaceCategory(Gnome));
}

// =============================================================================
// Race/Gender Validation Tests
// =============================================================================

class PlayerSoundsValidationTest : public ::testing::Test {
protected:
    static constexpr uint16_t Human = 1;
    static constexpr uint16_t InvalidRace = 999;
    static constexpr uint8_t Male = 0;
    static constexpr uint8_t Female = 1;
    static constexpr uint8_t InvalidGender = 2;
};

TEST_F(PlayerSoundsValidationTest, ValidRaces) {
    EXPECT_TRUE(PlayerSounds::isValidRace(1));    // Human
    EXPECT_TRUE(PlayerSounds::isValidRace(2));    // Barbarian
    EXPECT_TRUE(PlayerSounds::isValidRace(3));    // Erudite
    EXPECT_TRUE(PlayerSounds::isValidRace(4));    // Wood Elf
    EXPECT_TRUE(PlayerSounds::isValidRace(5));    // High Elf
    EXPECT_TRUE(PlayerSounds::isValidRace(6));    // Dark Elf
    EXPECT_TRUE(PlayerSounds::isValidRace(7));    // Half Elf
    EXPECT_TRUE(PlayerSounds::isValidRace(8));    // Dwarf
    EXPECT_TRUE(PlayerSounds::isValidRace(9));    // Troll
    EXPECT_TRUE(PlayerSounds::isValidRace(10));   // Ogre
    EXPECT_TRUE(PlayerSounds::isValidRace(11));   // Halfling
    EXPECT_TRUE(PlayerSounds::isValidRace(12));   // Gnome
    EXPECT_TRUE(PlayerSounds::isValidRace(128));  // Iksar
    EXPECT_TRUE(PlayerSounds::isValidRace(130));  // Vah Shir
    EXPECT_TRUE(PlayerSounds::isValidRace(330));  // Froglok
}

TEST_F(PlayerSoundsValidationTest, InvalidRaces) {
    EXPECT_FALSE(PlayerSounds::isValidRace(0));
    EXPECT_FALSE(PlayerSounds::isValidRace(13));   // Gap between 12 and 128
    EXPECT_FALSE(PlayerSounds::isValidRace(127));
    EXPECT_FALSE(PlayerSounds::isValidRace(129));
    EXPECT_FALSE(PlayerSounds::isValidRace(329));
    EXPECT_FALSE(PlayerSounds::isValidRace(331));
    EXPECT_FALSE(PlayerSounds::isValidRace(999));
    EXPECT_FALSE(PlayerSounds::isValidRace(65535));
}

TEST_F(PlayerSoundsValidationTest, ValidGenders) {
    EXPECT_TRUE(PlayerSounds::isValidGender(Male));
    EXPECT_TRUE(PlayerSounds::isValidGender(Female));
}

TEST_F(PlayerSoundsValidationTest, InvalidGenders) {
    EXPECT_FALSE(PlayerSounds::isValidGender(2));   // Neuter
    EXPECT_FALSE(PlayerSounds::isValidGender(3));
    EXPECT_FALSE(PlayerSounds::isValidGender(255));
}

// =============================================================================
// Suffix Tests
// =============================================================================

class PlayerSoundsSuffixTest : public ::testing::Test {
protected:
    static constexpr uint8_t Male = 0;
    static constexpr uint8_t Female = 1;
};

TEST_F(PlayerSoundsSuffixTest, StandardMaleSuffix) {
    // Standard races should get _M suffix for males
    EXPECT_EQ("_M", PlayerSounds::getSuffix(1, Male));   // Human
    EXPECT_EQ("_M", PlayerSounds::getSuffix(3, Male));   // Erudite
    EXPECT_EQ("_M", PlayerSounds::getSuffix(6, Male));   // Dark Elf
    EXPECT_EQ("_M", PlayerSounds::getSuffix(8, Male));   // Dwarf
    EXPECT_EQ("_M", PlayerSounds::getSuffix(9, Male));   // Troll
    EXPECT_EQ("_M", PlayerSounds::getSuffix(10, Male));  // Ogre
    EXPECT_EQ("_M", PlayerSounds::getSuffix(128, Male)); // Iksar
}

TEST_F(PlayerSoundsSuffixTest, StandardFemaleSuffix) {
    // Standard races should get _F suffix for females
    EXPECT_EQ("_F", PlayerSounds::getSuffix(1, Female));   // Human
    EXPECT_EQ("_F", PlayerSounds::getSuffix(3, Female));   // Erudite
    EXPECT_EQ("_F", PlayerSounds::getSuffix(6, Female));   // Dark Elf
    EXPECT_EQ("_F", PlayerSounds::getSuffix(8, Female));   // Dwarf
    EXPECT_EQ("_F", PlayerSounds::getSuffix(9, Female));   // Troll
    EXPECT_EQ("_F", PlayerSounds::getSuffix(10, Female));  // Ogre
    EXPECT_EQ("_F", PlayerSounds::getSuffix(128, Female)); // Iksar
}

TEST_F(PlayerSoundsSuffixTest, BarbarianMaleSuffix) {
    EXPECT_EQ("_MB", PlayerSounds::getSuffix(2, Male));  // Barbarian male
}

TEST_F(PlayerSoundsSuffixTest, BarbarianFemaleSuffix) {
    EXPECT_EQ("_FB", PlayerSounds::getSuffix(2, Female)); // Barbarian female
}

TEST_F(PlayerSoundsSuffixTest, LightMaleSuffix) {
    // Light races should get _ML suffix for males
    EXPECT_EQ("_ML", PlayerSounds::getSuffix(4, Male));   // Wood Elf
    EXPECT_EQ("_ML", PlayerSounds::getSuffix(5, Male));   // High Elf
    EXPECT_EQ("_ML", PlayerSounds::getSuffix(7, Male));   // Half Elf
    EXPECT_EQ("_ML", PlayerSounds::getSuffix(11, Male));  // Halfling
    EXPECT_EQ("_ML", PlayerSounds::getSuffix(12, Male));  // Gnome
}

TEST_F(PlayerSoundsSuffixTest, LightFemaleSuffix) {
    // Light races should get _FL suffix for females
    EXPECT_EQ("_FL", PlayerSounds::getSuffix(4, Female));   // Wood Elf
    EXPECT_EQ("_FL", PlayerSounds::getSuffix(5, Female));   // High Elf
    EXPECT_EQ("_FL", PlayerSounds::getSuffix(7, Female));   // Half Elf
    EXPECT_EQ("_FL", PlayerSounds::getSuffix(11, Female));  // Halfling
    EXPECT_EQ("_FL", PlayerSounds::getSuffix(12, Female));  // Gnome
}

TEST_F(PlayerSoundsSuffixTest, InvalidRaceReturnsEmpty) {
    EXPECT_EQ("", PlayerSounds::getSuffix(999, Male));
    EXPECT_EQ("", PlayerSounds::getSuffix(999, Female));
}

TEST_F(PlayerSoundsSuffixTest, InvalidGenderReturnsEmpty) {
    EXPECT_EQ("", PlayerSounds::getSuffix(1, 2));   // Neuter
    EXPECT_EQ("", PlayerSounds::getSuffix(1, 255)); // Invalid
}

// =============================================================================
// Sound File Name Tests
// =============================================================================

class PlayerSoundFileTest : public ::testing::Test {
protected:
    static constexpr uint16_t Human = 1;
    static constexpr uint16_t Barbarian = 2;
    static constexpr uint16_t WoodElf = 4;
    static constexpr uint8_t Male = 0;
    static constexpr uint8_t Female = 1;
};

TEST_F(PlayerSoundFileTest, DeathSounds) {
    // Standard
    EXPECT_EQ("Death_M.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Death, Human, Male));
    EXPECT_EQ("Death_F.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Death, Human, Female));

    // Barbarian
    EXPECT_EQ("Death_MB.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Death, Barbarian, Male));
    EXPECT_EQ("Death_FB.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Death, Barbarian, Female));

    // Light
    EXPECT_EQ("Death_ML.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Death, WoodElf, Male));
    EXPECT_EQ("Death_FL.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Death, WoodElf, Female));
}

TEST_F(PlayerSoundFileTest, DrownSounds) {
    EXPECT_EQ("Drown_M.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Drown, Human, Male));
    EXPECT_EQ("Drown_F.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Drown, Human, Female));
    EXPECT_EQ("Drown_MB.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Drown, Barbarian, Male));
    EXPECT_EQ("Drown_FL.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Drown, WoodElf, Female));
}

TEST_F(PlayerSoundFileTest, JumpSounds) {
    // Jump sounds use a different naming pattern: JumpM_1 / JumpF_1 + category suffix
    EXPECT_EQ("JumpM_1.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Jump, Human, Male));
    EXPECT_EQ("JumpF_1.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Jump, Human, Female));
    EXPECT_EQ("JumpM_1B.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Jump, Barbarian, Male));
    EXPECT_EQ("JumpF_1B.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Jump, Barbarian, Female));
    EXPECT_EQ("JumpM_1L.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Jump, WoodElf, Male));
    EXPECT_EQ("JumpF_1L.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Jump, WoodElf, Female));
}

TEST_F(PlayerSoundFileTest, GetHitSounds) {
    // GetHit sounds don't use underscore in suffix
    EXPECT_EQ("GetHit1M.WAV", PlayerSounds::getSoundFile(PlayerSoundType::GetHit1, Human, Male));
    EXPECT_EQ("GetHit2F.WAV", PlayerSounds::getSoundFile(PlayerSoundType::GetHit2, Human, Female));
    EXPECT_EQ("GetHit3MB.WAV", PlayerSounds::getSoundFile(PlayerSoundType::GetHit3, Barbarian, Male));
    EXPECT_EQ("GetHit4FB.WAV", PlayerSounds::getSoundFile(PlayerSoundType::GetHit4, Barbarian, Female));
    EXPECT_EQ("GetHit1ML.WAV", PlayerSounds::getSoundFile(PlayerSoundType::GetHit1, WoodElf, Male));
    EXPECT_EQ("GetHit2FL.WAV", PlayerSounds::getSoundFile(PlayerSoundType::GetHit2, WoodElf, Female));
}

TEST_F(PlayerSoundFileTest, GaspSounds) {
    // Gasp sounds don't use underscore in suffix
    EXPECT_EQ("Gasp1M.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Gasp1, Human, Male));
    EXPECT_EQ("Gasp2F.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Gasp2, Human, Female));
    EXPECT_EQ("Gasp1MB.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Gasp1, Barbarian, Male));
    EXPECT_EQ("Gasp2FB.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Gasp2, Barbarian, Female));
    EXPECT_EQ("Gasp1ML.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Gasp1, WoodElf, Male));
    EXPECT_EQ("Gasp2FL.WAV", PlayerSounds::getSoundFile(PlayerSoundType::Gasp2, WoodElf, Female));
}

TEST_F(PlayerSoundFileTest, AllSoundTypesGenerateFilenames) {
    // Verify all sound types generate valid filenames for all category combinations
    const uint16_t testRaces[] = {Human, Barbarian, WoodElf};
    const uint8_t testGenders[] = {Male, Female};
    const PlayerSoundType testTypes[] = {
        PlayerSoundType::Death,
        PlayerSoundType::Drown,
        PlayerSoundType::Jump,
        PlayerSoundType::GetHit1,
        PlayerSoundType::GetHit2,
        PlayerSoundType::GetHit3,
        PlayerSoundType::GetHit4,
        PlayerSoundType::Gasp1,
        PlayerSoundType::Gasp2
    };

    for (uint16_t race : testRaces) {
        for (uint8_t gender : testGenders) {
            for (PlayerSoundType type : testTypes) {
                std::string filename = PlayerSounds::getSoundFile(type, race, gender);
                EXPECT_FALSE(filename.empty())
                    << "Empty filename for race=" << race << ", gender=" << (int)gender
                    << ", type=" << static_cast<int>(type);
                EXPECT_TRUE(filename.find(".WAV") != std::string::npos)
                    << "Missing .WAV extension: " << filename;
            }
        }
    }
}

TEST_F(PlayerSoundFileTest, InvalidRaceReturnsEmpty) {
    EXPECT_EQ("", PlayerSounds::getSoundFile(PlayerSoundType::Death, 999, Male));
    EXPECT_EQ("", PlayerSounds::getSoundFile(PlayerSoundType::Jump, 0, Female));
}

TEST_F(PlayerSoundFileTest, InvalidGenderReturnsEmpty) {
    EXPECT_EQ("", PlayerSounds::getSoundFile(PlayerSoundType::Death, Human, 2));
    EXPECT_EQ("", PlayerSounds::getSoundFile(PlayerSoundType::GetHit1, Barbarian, 255));
}

// =============================================================================
// Sound ID Tests
// =============================================================================

class PlayerSoundIdTest : public ::testing::Test {
protected:
    static constexpr uint16_t Human = 1;
    static constexpr uint16_t Barbarian = 2;
    static constexpr uint16_t WoodElf = 4;
    static constexpr uint8_t Male = 0;
    static constexpr uint8_t Female = 1;
};

TEST_F(PlayerSoundIdTest, StandardMaleSoundIds) {
    // Standard male uses base IDs
    EXPECT_EQ(39u, PlayerSounds::getSoundId(PlayerSoundType::Death, Human, Male));
    EXPECT_EQ(40u, PlayerSounds::getSoundId(PlayerSoundType::Drown, Human, Male));
    EXPECT_EQ(32u, PlayerSounds::getSoundId(PlayerSoundType::Jump, Human, Male));
    EXPECT_EQ(33u, PlayerSounds::getSoundId(PlayerSoundType::GetHit1, Human, Male));
    EXPECT_EQ(34u, PlayerSounds::getSoundId(PlayerSoundType::GetHit2, Human, Male));
    EXPECT_EQ(35u, PlayerSounds::getSoundId(PlayerSoundType::GetHit3, Human, Male));
    EXPECT_EQ(36u, PlayerSounds::getSoundId(PlayerSoundType::GetHit4, Human, Male));
    EXPECT_EQ(37u, PlayerSounds::getSoundId(PlayerSoundType::Gasp1, Human, Male));
    EXPECT_EQ(38u, PlayerSounds::getSoundId(PlayerSoundType::Gasp2, Human, Male));
}

TEST_F(PlayerSoundIdTest, StandardFemaleSoundIds) {
    // Standard female uses base IDs + 10
    EXPECT_EQ(49u, PlayerSounds::getSoundId(PlayerSoundType::Death, Human, Female));
    EXPECT_EQ(50u, PlayerSounds::getSoundId(PlayerSoundType::Drown, Human, Female));
    EXPECT_EQ(42u, PlayerSounds::getSoundId(PlayerSoundType::Jump, Human, Female));
    EXPECT_EQ(43u, PlayerSounds::getSoundId(PlayerSoundType::GetHit1, Human, Female));
}

TEST_F(PlayerSoundIdTest, LightMaleSoundIds) {
    // Light male uses base IDs + 20
    EXPECT_EQ(59u, PlayerSounds::getSoundId(PlayerSoundType::Death, WoodElf, Male));
    EXPECT_EQ(60u, PlayerSounds::getSoundId(PlayerSoundType::Drown, WoodElf, Male));
    EXPECT_EQ(52u, PlayerSounds::getSoundId(PlayerSoundType::Jump, WoodElf, Male));
    EXPECT_EQ(53u, PlayerSounds::getSoundId(PlayerSoundType::GetHit1, WoodElf, Male));
}

TEST_F(PlayerSoundIdTest, LightFemaleSoundIds) {
    // Light female uses base IDs + 30
    EXPECT_EQ(69u, PlayerSounds::getSoundId(PlayerSoundType::Death, WoodElf, Female));
    EXPECT_EQ(70u, PlayerSounds::getSoundId(PlayerSoundType::Drown, WoodElf, Female));
    EXPECT_EQ(62u, PlayerSounds::getSoundId(PlayerSoundType::Jump, WoodElf, Female));
    EXPECT_EQ(63u, PlayerSounds::getSoundId(PlayerSoundType::GetHit1, WoodElf, Female));
}

TEST_F(PlayerSoundIdTest, BarbarianMaleSoundIds) {
    // Barbarian male uses base IDs + 40
    EXPECT_EQ(79u, PlayerSounds::getSoundId(PlayerSoundType::Death, Barbarian, Male));
    EXPECT_EQ(80u, PlayerSounds::getSoundId(PlayerSoundType::Drown, Barbarian, Male));
    EXPECT_EQ(72u, PlayerSounds::getSoundId(PlayerSoundType::Jump, Barbarian, Male));
    EXPECT_EQ(73u, PlayerSounds::getSoundId(PlayerSoundType::GetHit1, Barbarian, Male));
}

TEST_F(PlayerSoundIdTest, BarbarianFemaleSoundIds) {
    // Barbarian female uses base IDs + 50
    EXPECT_EQ(89u, PlayerSounds::getSoundId(PlayerSoundType::Death, Barbarian, Female));
    EXPECT_EQ(90u, PlayerSounds::getSoundId(PlayerSoundType::Drown, Barbarian, Female));
    EXPECT_EQ(82u, PlayerSounds::getSoundId(PlayerSoundType::Jump, Barbarian, Female));
    EXPECT_EQ(83u, PlayerSounds::getSoundId(PlayerSoundType::GetHit1, Barbarian, Female));
}

TEST_F(PlayerSoundIdTest, InvalidRaceReturnsZero) {
    EXPECT_EQ(0u, PlayerSounds::getSoundId(PlayerSoundType::Death, 999, Male));
    EXPECT_EQ(0u, PlayerSounds::getSoundId(PlayerSoundType::Jump, 0, Female));
}

TEST_F(PlayerSoundIdTest, InvalidGenderReturnsZero) {
    EXPECT_EQ(0u, PlayerSounds::getSoundId(PlayerSoundType::Death, Human, 2));
    EXPECT_EQ(0u, PlayerSounds::getSoundId(PlayerSoundType::GetHit1, Barbarian, 255));
}

// =============================================================================
// Edge Case Tests
// =============================================================================

class PlayerSoundsEdgeCaseTest : public ::testing::Test {};

TEST_F(PlayerSoundsEdgeCaseTest, AllPlayableRaces) {
    // Verify all playable races work correctly
    const uint16_t races[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 128, 130, 330};

    for (uint16_t race : races) {
        EXPECT_TRUE(PlayerSounds::isValidRace(race)) << "Race " << race << " should be valid";

        std::string mSuffix = PlayerSounds::getSuffix(race, 0);
        std::string fSuffix = PlayerSounds::getSuffix(race, 1);

        EXPECT_FALSE(mSuffix.empty()) << "Race " << race << " male suffix should not be empty";
        EXPECT_FALSE(fSuffix.empty()) << "Race " << race << " female suffix should not be empty";

        // Male suffix should start with _M
        EXPECT_EQ(mSuffix[0], '_') << "Male suffix should start with underscore";
        EXPECT_EQ(mSuffix[1], 'M') << "Male suffix should have M as second char";

        // Female suffix should start with _F
        EXPECT_EQ(fSuffix[0], '_') << "Female suffix should start with underscore";
        EXPECT_EQ(fSuffix[1], 'F') << "Female suffix should have F as second char";
    }
}

TEST_F(PlayerSoundsEdgeCaseTest, HighRaceIds) {
    // Iksar (128), Vah Shir (130), Froglok (330) should work
    EXPECT_TRUE(PlayerSounds::isValidRace(128));
    EXPECT_TRUE(PlayerSounds::isValidRace(130));
    EXPECT_TRUE(PlayerSounds::isValidRace(330));

    // They should all use Standard category
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(128));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(130));
    EXPECT_EQ(RaceCategory::Standard, PlayerSounds::getRaceCategory(330));
}

#else // WITH_AUDIO

#include <gtest/gtest.h>

// Dummy test when audio is disabled
TEST(PlayerSoundsDisabled, AudioNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled (WITH_AUDIO not defined)";
}

#endif // WITH_AUDIO
