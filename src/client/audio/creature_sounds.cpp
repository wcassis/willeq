#ifdef WITH_AUDIO

#include "client/audio/creature_sounds.h"

#include <algorithm>
#include <unordered_map>

namespace EQT {
namespace Audio {

// Race ID to sound prefix mapping
// These prefixes are used to construct sound filenames like "rat_atk.wav"
// Based on EverQuest Titanium client sound files in snd*.pfs archives
static const std::unordered_map<uint16_t, std::string> s_raceSoundPrefixes = {
    // Playable races (use lowercase race codes)
    {1, "hum"},      // Human
    {2, "bar"},      // Barbarian
    {3, "eru"},      // Erudite
    {4, "elf"},      // Wood Elf
    {5, "hie"},      // High Elf
    {6, "dae"},      // Dark Elf
    {7, "hae"},      // Half Elf
    {8, "dwf"},      // Dwarf
    {9, "trl"},      // Troll
    {10, "ogr"},     // Ogre
    {11, "hfl"},     // Halfling
    {12, "gnm"},     // Gnome
    {128, "iks"},    // Iksar
    {130, "vah"},    // Vah Shir

    // Animals - Canines
    {13, "wol"},     // Wolf
    {29, "wol"},     // Wolf (variant)
    {42, "wol"},     // Wolf (variant 2)

    // Animals - Bears
    {14, "bea"},     // Bear
    {23, "bea"},     // Kodiak Bear
    {30, "bea"},     // Bear (variant)
    {43, "bea"},     // Bear (variant 2)

    // Animals - Felines
    {50, "lio"},     // Lion Male
    {51, "lio"},     // Lion
    {59, "pum"},     // Puma
    {76, "pum"},     // Cat/Puma
    {78, "cat"},     // Cat

    // Animals - Rodents
    {35, "rat"},     // Rat
    {36, "rat"},     // Giant Rat

    // Animals - Reptiles
    {25, "all"},     // Alligator
    {26, "sna"},     // Snake
    {37, "sna"},     // Snake (variant)
    {93, "all"},     // Alligator (variant)

    // Animals - Aquatic
    {24, "fis"},     // Fish
    {57, "pir"},     // Piranha

    // Animals - Other
    {22, "bet"},     // Beetle
    {34, "bat"},     // Giant Bat
    {40, "gor"},     // Gorilla
    {47, "gri"},     // Griffin
    {48, "spi"},     // Spider (small)
    {49, "spi"},     // Spider (large)
    {88, "mos"},     // Mosquito
    {90, "gri"},     // Griffin (variant)

    // Humanoids - Orcs
    {17, "orc"},     // Orc
    {18, "orc"},     // Orc (variant)
    {19, "orc"},     // Orc (variant 2)
    {54, "orc"},     // Orc (variant 3)
    {56, "orc"},     // Orc (Crushbone)

    // Humanoids - Gnolls
    {39, "gno"},     // Gnoll Pup
    {44, "gno"},     // Gnoll
    {87, "gno"},     // Gnoll (variant)

    // Humanoids - Goblins
    {46, "gob"},     // Goblin

    // Humanoids - Lizard Folk
    {52, "liz"},     // Lizard Man

    // Humanoids - Other
    {20, "brn"},     // Brownie
    {53, "min"},     // Minotaur
    {91, "kob"},     // Kobold
    {94, "min"},     // Minotaur (variant)

    // Undead
    {21, "ske"},     // Skeleton
    {27, "spc"},     // Spectre
    {33, "ghu"},     // Ghoul
    {41, "ske"},     // Undead Pet (skeleton)
    {60, "ske"},     // Skeleton (variant)
    {63, "gho"},     // Ghost
    {64, "ske"},     // Greater Skeleton
    {65, "ghu"},     // Ghoul (variant)
    {67, "zom"},     // Zombie
    {70, "zom"},     // Zombie (variant)
    {81, "vam"},     // Vampire
    {83, "gho"},     // Ghost (variant)
    {367, "ske"},    // Decaying Skeleton

    // Lycanthropes
    {28, "wer"},     // Werewolf
    {77, "wer"},     // Werewolf (variant)

    // Elementals
    {58, "ele"},     // Elemental
    {72, "ear"},     // Earth Elemental
    {73, "air"},     // Air Elemental
    {74, "wat"},     // Water Elemental
    {75, "fir"},     // Fire Elemental

    // Dragons/Drakes
    {85, "dra"},     // Dragon
    {92, "drk"},     // Drake
    {95, "drk"},     // Drake (variant)

    // Magical Creatures
    {66, "hag"},     // Hag
    {68, "sph"},     // Sphinx
    {69, "wsp"},     // Will-o-Wisp
    {89, "imp"},     // Imp
    {96, "djn"},     // Genie/Djinn
    {38, "scr"},     // Scarecrow
    {55, "lvc"},     // Lava Crawler

    // Eye creatures
    {120, "eye"},    // Eye of Zomm

    // NPC/Citizens (typically use humanoid sounds)
    {15, "hum"},     // Freeport Guard (Human)
    {31, "hum"},     // Freeport Citizen Male
    {32, "hum"},     // Freeport Citizen (variant)
    {61, "dae"},     // Neriak Citizen (Dark Elf sounds)
    {62, "eru"},     // Erudite Citizen
    {71, "hum"},     // Qeynos Citizen
    {79, "elf"},     // Elf
    {80, "hum"},     // Karana Citizen
    {82, "hum"},     // Highpass Citizen
    {84, "dwf"},     // Dwarf Citizen
    {86, "dae"},     // Dark Elf Citizen
};

// Sound types that use variant numbering (e.g., rat_atk1.wav, rat_atk2.wav)
// Most creatures have at least 1-2 variants for attack and damage sounds
static const int MAX_SOUND_VARIANTS = 3;

std::string CreatureSounds::getSoundFile(CreatureSoundType type, uint16_t raceId) {
    std::string prefix = getRacePrefix(raceId);
    if (prefix.empty()) {
        return "";
    }
    return buildFilename(prefix, type);
}

std::vector<std::string> CreatureSounds::getSoundFileVariants(CreatureSoundType type, uint16_t raceId) {
    std::vector<std::string> variants;
    std::string prefix = getRacePrefix(raceId);
    if (prefix.empty()) {
        return variants;
    }

    // Add base filename (no number suffix)
    variants.push_back(buildFilename(prefix, type, 0));

    // Add numbered variants (1, 2, 3, etc.)
    for (int i = 1; i <= MAX_SOUND_VARIANTS; ++i) {
        variants.push_back(buildFilename(prefix, type, i));
    }

    return variants;
}

bool CreatureSounds::hasSoundFile(CreatureSoundType type, uint16_t raceId) {
    return !getRacePrefix(raceId).empty();
}

std::string CreatureSounds::getRacePrefix(uint16_t raceId) {
    auto it = s_raceSoundPrefixes.find(raceId);
    if (it != s_raceSoundPrefixes.end()) {
        return it->second;
    }
    return "";
}

std::vector<uint16_t> CreatureSounds::getRacesWithSounds() {
    std::vector<uint16_t> races;
    races.reserve(s_raceSoundPrefixes.size());
    for (const auto& [raceId, prefix] : s_raceSoundPrefixes) {
        races.push_back(raceId);
    }
    std::sort(races.begin(), races.end());
    return races;
}

std::string CreatureSounds::getSoundTypeSuffix(CreatureSoundType type) {
    switch (type) {
        case CreatureSoundType::Attack:  return "atk";
        case CreatureSoundType::Damage:  return "dam";
        case CreatureSoundType::Death:   return "dth";
        case CreatureSoundType::Idle:    return "idl";
        case CreatureSoundType::Special: return "spl";
        case CreatureSoundType::Run:     return "run";
        case CreatureSoundType::Walk:    return "wlk";
        default:                         return "";
    }
}

std::string CreatureSounds::buildFilename(const std::string& prefix, CreatureSoundType type, int variant) {
    std::string suffix = getSoundTypeSuffix(type);
    if (suffix.empty()) {
        return "";
    }

    // Format: {prefix}_{suffix}.wav or {prefix}_{suffix}{variant}.wav
    std::string filename = prefix + "_" + suffix;
    if (variant > 0) {
        filename += std::to_string(variant);
    }
    filename += ".wav";
    return filename;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
