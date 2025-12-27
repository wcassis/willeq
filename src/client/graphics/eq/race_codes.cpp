#include "client/graphics/eq/race_codes.h"
#include <algorithm>

namespace EQT {
namespace Graphics {

std::string getGenderedRaceCode(const std::string& maleCode, uint8_t gender) {
    if (gender != 1 || maleCode.empty()) {
        return maleCode;  // Male or neuter - return as-is
    }

    // Playable races have 3-letter codes ending in M for male
    // Female versions end in F
    if (maleCode.length() == 3 && maleCode.back() == 'M') {
        std::string femaleCode = maleCode;
        femaleCode.back() = 'F';
        return femaleCode;
    }

    return maleCode;  // Non-playable races don't have gendered variants
}

std::string getRaceCode(uint16_t raceId) {
    // Returns base race code (male variant for playable races)
    // Use getGenderedRaceCode() to get gender-specific code
    // Format: XXM for male, XXF for female (where XX is 2-letter race code)
    switch (raceId) {
        case 1:   return "HUM";  // Human Male / HUF for female
        case 2:   return "BAM";  // Barbarian Male / BAF for female
        case 3:   return "ERM";  // Erudite Male / ERF for female
        case 4:   return "ELM";  // Wood Elf Male / ELF for female
        case 5:   return "HIM";  // High Elf Male / HIF for female
        case 6:   return "DAM";  // Dark Elf Male / DAF for female
        case 7:   return "HAM";  // Half Elf Male / HAF for female
        case 8:   return "DWM";  // Dwarf Male / DWF for female
        case 9:   return "TRM";  // Troll Male / TRF for female
        case 10:  return "OGM";  // Ogre Male / OGF for female
        case 11:  return "HOM";  // Halfling Male / HOF for female
        case 12:  return "GNM";  // Gnome Male / GNF for female
        case 128: return "IKM";  // Iksar Male / IKF for female
        case 130: return "KEM";  // Vah Shir Male / KEF for female
        case 330: return "FRG";  // Froglok (use globalfroglok_chr.s3d)

        // Common monsters/NPCs
        case 13:  return "WOL";  // Wolf
        case 14:  return "BEA";  // Bear
        case 15:  return "FEL";  // Freeport Guard (Human variant)
        case 17:  return "ORC";  // Orc
        case 18:  return "ORC";  // Orc (variant)
        case 19:  return "ORC";  // Orc (variant 2)
        case 20:  return "BRO";  // Brownie
        case 21:  return "SKE";  // Skeleton
        case 22:  return "BET";  // Beetle / Fire Beetle / Klicnik (BET_HS_DEF in zone files)
        case 23:  return "KOD";  // Kodiak Bear
        case 24:  return "FIS";  // Fish / Koalindl (FIS_HS_DEF in zone files)
        case 25:  return "ALL";  // Alligator
        case 26:  return "SNA";  // Snake (SNA_HS_DEF in zone files)
        case 27:  return "SPE";  // Spectre
        case 28:  return "WER";  // Werewolf (variant)
        case 29:  return "WOL";  // Wolf (variant)
        case 30:  return "BEA";  // Bear (variant)
        case 31:  return "FER";  // Freeport Citizen
        case 32:  return "FER";  // Freeport Citizen (variant)
        case 33:  return "GHO";  // Ghost
        case 34:  return "BAT";  // Giant Bat
        case 35:  return "RAT";  // Rat (smaller variant)
        case 36:  return "RAT";  // Giant Rat / Large Rat
        case 37:  return "SNA";  // Giant Snake / Grass Snake (SNA_HS_DEF in zone files)
        case 38:  return "SCA";  // Scarecrow
        case 39:  return "GNN";  // Gnoll Pup (GNN_HS_DEF in zone files)
        case 40:  return "GOR";  // Gorilla
        case 41:  return "UND";  // Undead Pet
        case 42:  return "RAT";  // Rat
        case 43:  return "BAT";  // Bat
        case 44:  return "GNN";  // Gnoll (GNN_HS_DEF in zone files)
        case 45:  return "SNA";  // Sand Elf (unused)
        case 46:  return "GOB";  // Goblin
        case 47:  return "COR";  // Corn (unused)
        case 48:  return "SPI";  // Spider (small)
        case 49:  return "SPI";  // Spider (large)
        case 50:  return "DEE";  // Deer
        case 51:  return "LIO";  // Lion
        case 52:  return "LIZ";  // Lizard Man
        case 53:  return "MIN";  // Minotaur (variant)
        case 54:  return "WAR";  // Warslik
        case 55:  return "LAV";  // Lava Crawler
        case 56:  return "ORC";  // Orc (Crushbone)
        case 57:  return "PIR";  // Piranha
        case 58:  return "ELE";  // Elemental
        case 59:  return "PUM";  // Puma
        case 60:  return "SKE";  // Skeleton (variant)
        case 61:  return "NER";  // Neriak Citizen
        case 62:  return "ERU";  // Erudite Citizen
        case 63:  return "GHO";  // Ghost (variant)
        case 64:  return "GRE";  // Greater Skeleton
        case 65:  return "GUL";  // Ghoul
        case 66:  return "HAG";  // Hag
        case 67:  return "ZOM";  // Zombie
        case 68:  return "SPH";  // Sphinx
        case 69:  return "ARM";  // Armadillo
        case 70:  return "CRE";  // Clock Creature
        case 71:  return "QCM";  // Qeynos Citizen Male / QCF for female (fallback to HUM if not found)
        case 72:  return "EAR";  // Earth Elemental
        case 73:  return "AIR";  // Air Elemental
        case 74:  return "WAT";  // Water Elemental
        case 75:  return "FIR";  // Fire Elemental
        case 76:  return "WEP";  // Weapon Stand
        case 77:  return "WER";  // Werewolf
        case 78:  return "CAT";  // Cat
        case 79:  return "ELF";  // Elf (generic)
        case 80:  return "KAR";  // Karana Citizen
        case 81:  return "VMP";  // Vampire
        case 82:  return "HIK";  // Highpass Citizen
        case 83:  return "GHO";  // Ghost (alternate)
        case 84:  return "DWA";  // Dwarf Citizen
        case 85:  return "DRG";  // Dragon
        case 86:  return "DAR";  // Dark Elf Citizen
        case 87:  return "GNN";  // Gnoll (variant, GNN_HS_DEF in zone files)
        case 88:  return "MOS";  // Mosquito
        case 89:  return "IMP";  // Imp
        case 90:  return "GRI";  // Griffin
        case 91:  return "KOB";  // Kobold
        case 92:  return "DRA";  // Drake (small)
        case 93:  return "ALL";  // Alligator (swamp)
        case 94:  return "MIN";  // Minotaur
        case 95:  return "DRA";  // Drake
        case 96:  return "GEN";  // Genieish
        case 120: return "EYE";  // Eye of Zomm
        case 127: return "INV";  // Invisible Man
        case 240: return "INV";  // Invisible Man / Zone Controller / Marker

        // Higher race IDs (expansions)
        case 367: return "SKE";  // Decaying Skeleton (same as skeleton model)

        default:
            // Return empty for unknown races - will use placeholder
            return "";
    }
}

std::string getZoneSpecificRaceCode(uint16_t raceId, uint8_t gender, const std::string& zoneName) {
    // Convert zone name to lowercase for matching
    std::string lowerZone = zoneName;
    std::transform(lowerZone.begin(), lowerZone.end(), lowerZone.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Race 71 = Qeynos Citizen - uses QCM/QCF in Qeynos zones
    if (raceId == 71) {
        if (lowerZone.find("qeynos") != std::string::npos ||
            lowerZone.find("qey") != std::string::npos) {
            return (gender == 1) ? "QCF" : "QCM";
        }
    }

    // Race 77 = Freeport Citizen - uses FCM/FCF in Freeport zones
    // (if such models exist - adding for completeness)
    if (raceId == 77 || raceId == 15) {  // 15 is Freeport Guard
        if (lowerZone.find("freeport") != std::string::npos ||
            lowerZone.find("freport") != std::string::npos) {
            return (gender == 1) ? "FCF" : "FCM";
        }
    }

    // Race 61 = Neriak Citizen - uses NCM/NCF in Neriak zones
    if (raceId == 61) {
        if (lowerZone.find("neriak") != std::string::npos ||
            lowerZone.find("neriaka") != std::string::npos ||
            lowerZone.find("neriakb") != std::string::npos ||
            lowerZone.find("neriakc") != std::string::npos) {
            return (gender == 1) ? "NCF" : "NCM";
        }
    }

    // Race 80 = Karana Citizen - uses KCM/KCF in Karana zones
    if (raceId == 80) {
        if (lowerZone.find("karana") != std::string::npos ||
            lowerZone.find("qey2hh") != std::string::npos) {  // QeynHills
            return (gender == 1) ? "KCF" : "KCM";
        }
    }

    return "";  // No zone-specific model
}

std::string getFallbackRaceCode(uint16_t raceId, uint8_t gender) {
    switch (raceId) {
        case 71:  // Qeynos Citizen -> Human
        case 77:  // (potential Freeport citizen) -> Human
        case 15:  // Freeport Guard -> Human
        case 80:  // Karana Citizen -> Human
            return (gender == 1) ? "HUF" : "HUM";

        case 61:  // Neriak Citizen -> Dark Elf
            return (gender == 1) ? "DAF" : "DAM";

        case 62:  // Erudite Citizen -> Erudite
            return (gender == 1) ? "ERF" : "ERM";

        case 82:  // Highpass Citizen -> Human
            return (gender == 1) ? "HUF" : "HUM";

        case 84:  // Dwarf Citizen -> Dwarf
            return (gender == 1) ? "DWF" : "DWM";

        default:
            return "";
    }
}

std::string getRaceModelFilename(uint16_t raceId, uint8_t gender) {
    // Special case: Froglok uses different naming convention
    if (raceId == 330) {
        return "globalfroglok_chr.s3d";
    }

    std::string code = getRaceCode(raceId);
    if (code.empty()) {
        return "";
    }

    // Many races have gender-specific models
    // Male models typically end with M, female with F
    // Some races (like wolf, skeleton) are genderless

    // For playable races, adjust suffix based on gender
    if (raceId <= 12 || raceId == 128 || raceId == 130) {
        // These typically have gendered versions
        if (code.length() >= 3) {
            char lastChar = code.back();
            // If ends with M or F, adjust based on gender
            if (lastChar == 'M' || lastChar == 'F') {
                code[code.length() - 1] = (gender == 1) ? 'F' : 'M';
            }
        }
    }

    // Build the S3D filename: global{code}_chr.s3d
    std::string lower = code;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return "global" + lower + "_chr.s3d";
}

float getRaceScale(uint16_t raceId) {
    // Scale factors for races (relative to human size)
    switch (raceId) {
        // Playable races
        case 2:   return 1.2f;   // Barbarian - larger
        case 8:   return 0.7f;   // Dwarf - shorter
        case 9:   return 1.4f;   // Troll - large
        case 10:  return 1.5f;   // Ogre - largest playable
        case 11:  return 0.5f;   // Halfling - small
        case 12:  return 0.6f;   // Gnome - small
        case 128: return 1.1f;   // Iksar - slightly larger
        case 130: return 1.1f;   // Vah Shir - slightly larger

        // Monsters - small creatures
        case 13:  return 0.8f;   // Wolf
        case 22:  return 0.4f;   // Beetle / Fire Beetle
        case 24:  return 0.3f;   // Fish / Koalindl
        case 34:  return 0.4f;   // Giant Bat
        case 35:  return 0.2f;   // Rat (small)
        case 36:  return 0.4f;   // Giant Rat / Large Rat
        case 37:  return 0.5f;   // Giant Snake / Grass Snake
        case 39:  return 0.6f;   // Gnoll Pup (smaller gnoll)
        case 42:  return 0.3f;   // Rat - tiny
        case 43:  return 0.4f;   // Bat - small
        case 48:  return 0.3f;   // Spider (small)
        case 49:  return 0.6f;   // Spider (large)
        case 57:  return 0.2f;   // Piranha
        case 78:  return 0.4f;   // Cat
        case 88:  return 0.2f;   // Mosquito

        // Monsters - medium creatures
        case 14:  return 1.2f;   // Bear
        case 17:  return 1.0f;   // Orc
        case 21:  return 1.0f;   // Skeleton
        case 26:  return 0.7f;   // Snake
        case 44:  return 1.0f;   // Gnoll
        case 46:  return 0.8f;   // Goblin
        case 52:  return 1.1f;   // Lizard Man
        case 65:  return 1.0f;   // Ghoul
        case 67:  return 1.0f;   // Zombie
        case 77:  return 1.2f;   // Werewolf
        case 91:  return 0.7f;   // Kobold
        case 367: return 1.0f;   // Decaying Skeleton

        // Monsters - large creatures
        case 23:  return 1.4f;   // Kodiak Bear
        case 25:  return 1.3f;   // Alligator
        case 40:  return 1.3f;   // Gorilla
        case 51:  return 1.2f;   // Lion
        case 59:  return 1.0f;   // Puma
        case 85:  return 3.0f;   // Dragon - huge
        case 90:  return 2.0f;   // Griffin
        case 94:  return 1.5f;   // Minotaur
        case 95:  return 1.5f;   // Drake

        // Invisible / Zone entities
        case 127: return 0.0f;   // Invisible Man - don't render
        case 240: return 0.0f;   // Zone Controller / Marker - don't render

        default:
            return 1.0f;
    }
}

std::string getRaceName(uint16_t raceId) {
    switch (raceId) {
        // Playable races
        case 1:   return "Human";
        case 2:   return "Barbarian";
        case 3:   return "Erudite";
        case 4:   return "Wood Elf";
        case 5:   return "High Elf";
        case 6:   return "Dark Elf";
        case 7:   return "Half Elf";
        case 8:   return "Dwarf";
        case 9:   return "Troll";
        case 10:  return "Ogre";
        case 11:  return "Halfling";
        case 12:  return "Gnome";
        case 128: return "Iksar";
        case 130: return "Vah Shir";
        case 330: return "Froglok";

        // Common NPCs/monsters
        case 13:  return "Wolf";
        case 14:  return "Bear";
        case 15:  return "Freeport Guard";
        case 17:
        case 18:
        case 19:
        case 56:  return "Orc";
        case 20:  return "Brownie";
        case 21:
        case 60:  return "Skeleton";
        case 22:  return "Beetle";
        case 23:  return "Kodiak Bear";
        case 24:  return "Fish";
        case 25:
        case 93:  return "Alligator";
        case 26:
        case 37:  return "Snake";
        case 27:  return "Spectre";
        case 28:  return "Werewolf";
        case 31:
        case 32:  return "Freeport Citizen";
        case 33:
        case 63:
        case 83:  return "Ghost";
        case 34:  return "Giant Bat";
        case 35:
        case 36:
        case 42:  return "Rat";
        case 38:  return "Scarecrow";
        case 39:
        case 44:
        case 87:  return "Gnoll";
        case 40:  return "Gorilla";
        case 41:  return "Undead Pet";
        case 43:  return "Bat";
        case 46:  return "Goblin";
        case 48:
        case 49:  return "Spider";
        case 50:  return "Deer";
        case 51:  return "Lion";
        case 52:  return "Lizard Man";
        case 53:
        case 94:  return "Minotaur";
        case 54:  return "Warslik";
        case 55:  return "Lava Crawler";
        case 57:  return "Piranha";
        case 58:  return "Elemental";
        case 59:  return "Puma";
        case 61:  return "Neriak Citizen";
        case 62:  return "Erudite Citizen";
        case 64:  return "Greater Skeleton";
        case 65:  return "Ghoul";
        case 66:  return "Hag";
        case 67:  return "Zombie";
        case 68:  return "Sphinx";
        case 69:  return "Armadillo";
        case 70:  return "Clock Creature";
        case 71:  return "Qeynos Citizen";
        case 72:  return "Earth Elemental";
        case 73:  return "Air Elemental";
        case 74:  return "Water Elemental";
        case 75:  return "Fire Elemental";
        case 76:  return "Weapon Stand";
        case 77:  return "Werewolf";
        case 78:  return "Cat";
        case 79:  return "Elf";
        case 80:  return "Karana Citizen";
        case 81:  return "Vampire";
        case 82:  return "Highpass Citizen";
        case 84:  return "Dwarf Citizen";
        case 85:  return "Dragon";
        case 86:  return "Dark Elf Citizen";
        case 88:  return "Mosquito";
        case 89:  return "Imp";
        case 90:  return "Griffin";
        case 91:  return "Kobold";
        case 92:
        case 95:  return "Drake";
        case 96:  return "Genie";
        case 120: return "Eye of Zomm";
        case 127:
        case 240: return "Invisible";
        case 367: return "Decaying Skeleton";

        default:
            return "Unknown (" + std::to_string(raceId) + ")";
    }
}

std::string getClassName(uint8_t classId) {
    switch (classId) {
        case 1:  return "Warrior";
        case 2:  return "Cleric";
        case 3:  return "Paladin";
        case 4:  return "Ranger";
        case 5:  return "Shadow Knight";
        case 6:  return "Druid";
        case 7:  return "Monk";
        case 8:  return "Bard";
        case 9:  return "Rogue";
        case 10: return "Shaman";
        case 11: return "Necromancer";
        case 12: return "Wizard";
        case 13: return "Magician";
        case 14: return "Enchanter";
        case 15: return "Beastlord";
        case 16: return "Berserker";
        case 20: return "GM Warrior";
        case 21: return "GM Cleric";
        case 32: return "Banker";
        case 40: return "Merchant";
        case 41: return "Merchant";
        case 60: return "GM Quest";
        case 61: return "Trainer";
        case 63: return "Tribute Master";
        default: return "";
    }
}

std::string getGenderName(uint8_t gender) {
    switch (gender) {
        case 0: return "Male";
        case 1: return "Female";
        case 2: return "Neuter";
        default: return "Unknown";
    }
}

} // namespace Graphics
} // namespace EQT
