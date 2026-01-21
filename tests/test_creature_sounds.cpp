#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/creature_sounds.h"

#include <algorithm>

using namespace EQT::Audio;

// =============================================================================
// Race Prefix Tests
// =============================================================================

class CreatureSoundsPrefixTest : public ::testing::Test {
protected:
    // Known race ID to prefix mappings for verification
    struct RacePrefixMapping {
        uint16_t raceId;
        std::string expectedPrefix;
        std::string raceName;
    };

    std::vector<RacePrefixMapping> knownMappings = {
        // Playable races
        {1, "hum", "Human"},
        {2, "bar", "Barbarian"},
        {8, "dwf", "Dwarf"},
        {12, "gnm", "Gnome"},
        {128, "iks", "Iksar"},

        // Common monsters
        {13, "wol", "Wolf"},
        {14, "bea", "Bear"},
        {21, "ske", "Skeleton"},
        {35, "rat", "Rat"},
        {46, "gob", "Goblin"},
        {17, "orc", "Orc"},
        {44, "gno", "Gnoll"},
        {48, "spi", "Spider"},
        {67, "zom", "Zombie"},
        {63, "gho", "Ghost"},
        {85, "dra", "Dragon"},
    };
};

TEST_F(CreatureSoundsPrefixTest, KnownRacesReturnCorrectPrefix) {
    for (const auto& mapping : knownMappings) {
        std::string prefix = CreatureSounds::getRacePrefix(mapping.raceId);
        EXPECT_EQ(prefix, mapping.expectedPrefix)
            << "Race " << mapping.raceName << " (ID " << mapping.raceId
            << ") expected prefix '" << mapping.expectedPrefix
            << "' but got '" << prefix << "'";
    }
}

TEST_F(CreatureSoundsPrefixTest, UnknownRaceReturnsEmptyPrefix) {
    // Race 999 doesn't exist
    std::string prefix = CreatureSounds::getRacePrefix(999);
    EXPECT_TRUE(prefix.empty());

    // Race 0 is invalid
    prefix = CreatureSounds::getRacePrefix(0);
    EXPECT_TRUE(prefix.empty());

    // Race 127 is Invisible Man (no sounds)
    prefix = CreatureSounds::getRacePrefix(127);
    EXPECT_TRUE(prefix.empty());

    // Race 240 is Zone Controller (no sounds)
    prefix = CreatureSounds::getRacePrefix(240);
    EXPECT_TRUE(prefix.empty());
}

TEST_F(CreatureSoundsPrefixTest, VariantRacesSharePrefixes) {
    // Multiple orc races should share the same prefix
    std::string orc17 = CreatureSounds::getRacePrefix(17);
    std::string orc18 = CreatureSounds::getRacePrefix(18);
    std::string orc19 = CreatureSounds::getRacePrefix(19);
    std::string orc54 = CreatureSounds::getRacePrefix(54);
    EXPECT_EQ(orc17, orc18);
    EXPECT_EQ(orc18, orc19);
    EXPECT_EQ(orc19, orc54);

    // Multiple wolf races should share the same prefix
    std::string wolf13 = CreatureSounds::getRacePrefix(13);
    std::string wolf29 = CreatureSounds::getRacePrefix(29);
    std::string wolf42 = CreatureSounds::getRacePrefix(42);
    EXPECT_EQ(wolf13, wolf29);
    EXPECT_EQ(wolf29, wolf42);
}

// =============================================================================
// Sound File Generation Tests
// =============================================================================

class CreatureSoundsFileTest : public ::testing::Test {
};

TEST_F(CreatureSoundsFileTest, GetSoundFileReturnsCorrectFilename) {
    // Rat attack sound
    std::string ratAttack = CreatureSounds::getSoundFile(CreatureSoundType::Attack, 35);
    EXPECT_EQ(ratAttack, "rat_atk.wav");

    // Wolf damage sound
    std::string wolfDamage = CreatureSounds::getSoundFile(CreatureSoundType::Damage, 13);
    EXPECT_EQ(wolfDamage, "wol_dam.wav");

    // Skeleton death sound
    std::string skeDeath = CreatureSounds::getSoundFile(CreatureSoundType::Death, 21);
    EXPECT_EQ(skeDeath, "ske_dth.wav");

    // Bear idle sound
    std::string bearIdle = CreatureSounds::getSoundFile(CreatureSoundType::Idle, 14);
    EXPECT_EQ(bearIdle, "bea_idl.wav");

    // Human special sound
    std::string humSpecial = CreatureSounds::getSoundFile(CreatureSoundType::Special, 1);
    EXPECT_EQ(humSpecial, "hum_spl.wav");

    // Orc run sound
    std::string orcRun = CreatureSounds::getSoundFile(CreatureSoundType::Run, 17);
    EXPECT_EQ(orcRun, "orc_run.wav");

    // Gnoll walk sound
    std::string gnollWalk = CreatureSounds::getSoundFile(CreatureSoundType::Walk, 44);
    EXPECT_EQ(gnollWalk, "gno_wlk.wav");
}

TEST_F(CreatureSoundsFileTest, GetSoundFileReturnsEmptyForUnknownRace) {
    std::string result = CreatureSounds::getSoundFile(CreatureSoundType::Attack, 999);
    EXPECT_TRUE(result.empty());

    result = CreatureSounds::getSoundFile(CreatureSoundType::Damage, 0);
    EXPECT_TRUE(result.empty());
}

TEST_F(CreatureSoundsFileTest, GetSoundFileVariantsReturnsMultipleFiles) {
    auto variants = CreatureSounds::getSoundFileVariants(CreatureSoundType::Attack, 35);

    // Should have base file + numbered variants
    EXPECT_GE(variants.size(), 2u);

    // First should be base file (no number)
    EXPECT_EQ(variants[0], "rat_atk.wav");

    // Second should be variant 1
    EXPECT_EQ(variants[1], "rat_atk1.wav");

    // Third should be variant 2
    if (variants.size() >= 3) {
        EXPECT_EQ(variants[2], "rat_atk2.wav");
    }
}

TEST_F(CreatureSoundsFileTest, GetSoundFileVariantsReturnsEmptyForUnknownRace) {
    auto variants = CreatureSounds::getSoundFileVariants(CreatureSoundType::Attack, 999);
    EXPECT_TRUE(variants.empty());
}

// =============================================================================
// Sound Type Tests
// =============================================================================

class CreatureSoundsTypeTest : public ::testing::Test {
};

TEST_F(CreatureSoundsTypeTest, GetSoundTypeSuffixReturnsCorrectSuffix) {
    EXPECT_EQ(CreatureSounds::getSoundTypeSuffix(CreatureSoundType::Attack), "atk");
    EXPECT_EQ(CreatureSounds::getSoundTypeSuffix(CreatureSoundType::Damage), "dam");
    EXPECT_EQ(CreatureSounds::getSoundTypeSuffix(CreatureSoundType::Death), "dth");
    EXPECT_EQ(CreatureSounds::getSoundTypeSuffix(CreatureSoundType::Idle), "idl");
    EXPECT_EQ(CreatureSounds::getSoundTypeSuffix(CreatureSoundType::Special), "spl");
    EXPECT_EQ(CreatureSounds::getSoundTypeSuffix(CreatureSoundType::Run), "run");
    EXPECT_EQ(CreatureSounds::getSoundTypeSuffix(CreatureSoundType::Walk), "wlk");
}

TEST_F(CreatureSoundsTypeTest, HasSoundFileReturnsTrueForKnownRaces) {
    // Rat should have attack sounds
    EXPECT_TRUE(CreatureSounds::hasSoundFile(CreatureSoundType::Attack, 35));

    // Wolf should have damage sounds
    EXPECT_TRUE(CreatureSounds::hasSoundFile(CreatureSoundType::Damage, 13));

    // Skeleton should have death sounds
    EXPECT_TRUE(CreatureSounds::hasSoundFile(CreatureSoundType::Death, 21));
}

TEST_F(CreatureSoundsTypeTest, HasSoundFileReturnsFalseForUnknownRaces) {
    EXPECT_FALSE(CreatureSounds::hasSoundFile(CreatureSoundType::Attack, 999));
    EXPECT_FALSE(CreatureSounds::hasSoundFile(CreatureSoundType::Attack, 0));
    EXPECT_FALSE(CreatureSounds::hasSoundFile(CreatureSoundType::Attack, 127));
}

// =============================================================================
// Race List Tests
// =============================================================================

class CreatureSoundsRaceListTest : public ::testing::Test {
};

TEST_F(CreatureSoundsRaceListTest, GetRacesWithSoundsReturnsNonEmptyList) {
    auto races = CreatureSounds::getRacesWithSounds();
    EXPECT_FALSE(races.empty());

    // Should have at least playable races + common monsters
    EXPECT_GT(races.size(), 20u);
}

TEST_F(CreatureSoundsRaceListTest, GetRacesWithSoundsIsSorted) {
    auto races = CreatureSounds::getRacesWithSounds();
    EXPECT_TRUE(std::is_sorted(races.begin(), races.end()));
}

TEST_F(CreatureSoundsRaceListTest, GetRacesWithSoundsContainsKnownRaces) {
    auto races = CreatureSounds::getRacesWithSounds();

    // Check for Human (1)
    EXPECT_TRUE(std::find(races.begin(), races.end(), 1) != races.end());

    // Check for Rat (35)
    EXPECT_TRUE(std::find(races.begin(), races.end(), 35) != races.end());

    // Check for Skeleton (21)
    EXPECT_TRUE(std::find(races.begin(), races.end(), 21) != races.end());
}

TEST_F(CreatureSoundsRaceListTest, GetRacesWithSoundsDoesNotContainInvisibleRaces) {
    auto races = CreatureSounds::getRacesWithSounds();

    // Invisible Man (127) should not be in the list
    EXPECT_TRUE(std::find(races.begin(), races.end(), 127) == races.end());

    // Zone Controller (240) should not be in the list
    EXPECT_TRUE(std::find(races.begin(), races.end(), 240) == races.end());
}

// =============================================================================
// Edge Cases and Integration Tests
// =============================================================================

class CreatureSoundsEdgeCaseTest : public ::testing::Test {
};

TEST_F(CreatureSoundsEdgeCaseTest, AllSoundTypesWorkForPlayableRaces) {
    // Test all sound types for Human (race 1)
    std::vector<CreatureSoundType> types = {
        CreatureSoundType::Attack,
        CreatureSoundType::Damage,
        CreatureSoundType::Death,
        CreatureSoundType::Idle,
        CreatureSoundType::Special,
        CreatureSoundType::Run,
        CreatureSoundType::Walk
    };

    for (const auto& type : types) {
        std::string filename = CreatureSounds::getSoundFile(type, 1);
        EXPECT_FALSE(filename.empty()) << "Sound type should produce a filename for Human";
        EXPECT_TRUE(filename.find("hum_") == 0) << "Human sound should start with 'hum_'";
        EXPECT_TRUE(filename.find(".wav") != std::string::npos) << "Sound file should end with .wav";
    }
}

TEST_F(CreatureSoundsEdgeCaseTest, ElementalRacesHaveDifferentPrefixes) {
    // Different elemental types should have different prefixes
    std::string earth = CreatureSounds::getRacePrefix(72);  // Earth Elemental
    std::string air = CreatureSounds::getRacePrefix(73);    // Air Elemental
    std::string water = CreatureSounds::getRacePrefix(74);  // Water Elemental
    std::string fire = CreatureSounds::getRacePrefix(75);   // Fire Elemental

    // Each should be non-empty
    EXPECT_FALSE(earth.empty());
    EXPECT_FALSE(air.empty());
    EXPECT_FALSE(water.empty());
    EXPECT_FALSE(fire.empty());

    // They should be different from each other (elementals have unique sounds)
    EXPECT_NE(earth, air);
    EXPECT_NE(earth, water);
    EXPECT_NE(earth, fire);
    EXPECT_NE(air, water);
    EXPECT_NE(air, fire);
    EXPECT_NE(water, fire);
}

TEST_F(CreatureSoundsEdgeCaseTest, IksarHasSounds) {
    // Iksar (race 128) is a playable race added in Kunark
    std::string prefix = CreatureSounds::getRacePrefix(128);
    EXPECT_FALSE(prefix.empty());
    EXPECT_EQ(prefix, "iks");

    // Should be able to get all sound types
    EXPECT_FALSE(CreatureSounds::getSoundFile(CreatureSoundType::Attack, 128).empty());
    EXPECT_FALSE(CreatureSounds::getSoundFile(CreatureSoundType::Death, 128).empty());
}

TEST_F(CreatureSoundsEdgeCaseTest, DragonHasSounds) {
    // Dragon (race 85) should have sounds
    std::string prefix = CreatureSounds::getRacePrefix(85);
    EXPECT_FALSE(prefix.empty());
    EXPECT_EQ(prefix, "dra");

    std::string attackSound = CreatureSounds::getSoundFile(CreatureSoundType::Attack, 85);
    EXPECT_EQ(attackSound, "dra_atk.wav");
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, CreatureSoundsNotAvailable) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
