#include "client/graphics/eq/race_codes.h"
#include "client/willeq_database.h"
#include "common/logging.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

namespace EQT {
namespace Graphics {

// Race mappings now served on-demand from WilleqDatabase::globalInstance()
static bool s_mappingsLoaded = false;

bool loadRaceMappings(const std::string& jsonPath) {
    (void)jsonPath;
    // Race mappings now served on-demand from SQLite
    s_mappingsLoaded = (WilleqDatabase::globalInstance() != nullptr);
    if (s_mappingsLoaded) {
        LOG_INFO(MOD_GRAPHICS, "Race mappings using on-demand SQLite lookup");
    }
    return s_mappingsLoaded;
}

bool reloadRaceMappings() {
    return loadRaceMappings("");
}

bool areRaceMappingsLoaded() {
    return s_mappingsLoaded;
}

std::string getRaceS3DFile(uint16_t raceId) {
    auto* db = WilleqDatabase::globalInstance();
    if (db) {
        auto info = db->getRaceModel(raceId);
        if (info.found) return info.s3dFile;
    }
    return "";
}

std::string getRaceS3DFileByCode(const std::string& raceCode) {
    auto* db = WilleqDatabase::globalInstance();
    if (db) {
        auto info = db->getRaceModelByCode(raceCode);
        if (info.found) return info.s3dFile;
    }
    return "";
}

std::string getAnimationSourceFromConfig(uint16_t raceId) {
    auto* db = WilleqDatabase::globalInstance();
    if (db) {
        auto info = db->getRaceModel(raceId);
        if (info.found) return info.animationSource;
    }
    return "";
}

std::string getAnimationSourceS3DFile(uint16_t raceId) {
    auto* db = WilleqDatabase::globalInstance();
    if (db) {
        auto info = db->getRaceModel(raceId);
        if (info.found) return info.animationSourceS3d;
    }
    return "";
}

std::string getAnimationSourceS3DFileByCode(const std::string& raceCode) {
    auto* db = WilleqDatabase::globalInstance();
    if (!db) return "";

    auto info = db->getRaceModelByCode(raceCode);
    if (info.found && !info.animationSourceS3d.empty()) {
        return info.animationSourceS3d;
    }
    return "";
}

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

// T270: the db is the only source. Before this unit a null/absent row fell
// through to a pre-JSON hardcoded switch whose numbering contradicted the
// audited race_models rows wholesale (13 AVI vs WOL, 17 GOL vs ORC, 21 BEH vs
// SKE, 35 EEL vs RAT, 46 IMP vs GOB, 72 SHIP vs ELE, 85 SPE vs DRA, 127/240
// IVM vs INV, ...), i.e. it answered confidently and wrongly. With no row the
// honest answer is "unknown"; every caller reads an empty code as "no model"
// and takes the placeholder path (T213).
std::string getRaceCode(uint16_t raceId) {
    auto* db = WilleqDatabase::globalInstance();
    if (!db) return "";
    auto info = db->getRaceModel(raceId);
    if (info.found && !info.code.empty()) return info.code;
    return "";
}

// T270: db only, same rule as getRaceCode(). The deleted hardcoded switch
// fired whenever a row carried no fallback_code -- not only when the db was
// absent -- so e.g. race 61 (SHA, Shark) fell back to DAM/DAF and race 62
// (TUN, Tunare) to ERM/ERF, both from the pre-JSON citizen numbering.
std::string getFallbackRaceCode(uint16_t raceId, uint8_t gender) {
    auto* db = WilleqDatabase::globalInstance();
    if (!db) return "";
    auto info = db->getRaceModel(raceId);
    if (!info.found || info.fallbackCode.empty()) return "";
    std::string fallback = info.fallbackCode;
    // Apply gender suffix if it ends in M and gender is female
    if (gender == 1 && fallback.length() == 3 && fallback.back() == 'M') {
        fallback.back() = 'F';
    }
    return fallback;
}

// True for a per-race global archive name such as "globalpcfroglok_chr.s3d"
// or "globalhum_chr.s3d" -- i.e. "global" + letters + "_chr.s3d". False for
// the shared archives global_chr.s3d / global{N}_chr.s3d and for zone
// archives, which the loader's own search order already covers.
bool isRaceSpecificGlobalArchive(const std::string& s3dFile) {
    std::string lower = s3dFile;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::string kPrefix = "global";
    static const std::string kSuffix = "_chr.s3d";
    if (lower.size() <= kPrefix.size() + kSuffix.size()) return false;
    if (lower.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    if (lower.compare(lower.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) return false;
    std::string stem = lower.substr(kPrefix.size(), lower.size() - kPrefix.size() - kSuffix.size());
    if (stem.empty()) return false;
    for (unsigned char c : stem) {
        if (!std::isalpha(c)) return false;
    }
    return true;
}

std::string getRaceModelFilename(uint16_t raceId, uint8_t gender) {
    // T267: the race row's s3d_file is authoritative when it names a per-race
    // global archive (race 330, the playable Froglok, is globalpcfroglok_chr.s3d
    // -- FRM/FRF exist nowhere else). The previous hardcode sent 330 to
    // globalfroglok_chr.s3d, which holds only the monster frogloks FRO/FRG.
    {
        auto* db = WilleqDatabase::globalInstance();
        if (db) {
            auto info = db->getRaceModel(raceId);
            if (info.found && isRaceSpecificGlobalArchive(info.s3dFile)) {
                return info.s3dFile;
            }
        }
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
    // Check SQLite race_models table
    {
        auto* db = WilleqDatabase::globalInstance();
        if (db) {
            auto info = db->getRaceModel(raceId);
            if (info.found) return info.scale;
        }
    }

    // Fallback to hardcoded defaults
    switch (raceId) {
        // Playable races
        case 2:   return 1.2f;   // Barbarian
        case 8:   return 0.7f;   // Dwarf
        case 9:   return 1.4f;   // Troll
        case 10:  return 1.5f;   // Ogre
        case 11:  return 0.5f;   // Halfling
        case 12:  return 0.6f;   // Gnome
        case 128: return 1.1f;   // Iksar

        // Monsters - small
        case 13:  return 0.8f;   // Wolf
        case 22:  return 0.4f;   // Beetle
        case 24:  return 0.3f;   // Fish
        case 34:  return 0.6f;   // Giant Bat
        case 35:  return 0.2f;   // Rat
        case 36:  return 0.4f;   // Giant Rat
        case 37:  return 0.5f;   // Snake
        case 39:  return 0.6f;   // Gnoll Pup
        case 42:  return 0.8f;   // Wolf (was incorrectly 0.3 for rat)
        case 43:  return 1.2f;   // Bear (was incorrectly 0.4 for bat)
        case 48:  return 0.3f;   // Spider (small)
        case 49:  return 0.6f;   // Spider (large)
        case 57:  return 0.2f;   // Piranha
        case 69:  return 0.5f;   // Will-o-Wisp
        case 76:  return 0.8f;   // Cat/Puma
        case 78:  return 0.4f;   // Cat
        case 88:  return 0.2f;   // Mosquito

        // Monsters - medium
        case 14:  return 1.2f;   // Bear
        case 17:  return 1.0f;   // Orc
        case 21:  return 1.0f;   // Skeleton
        case 26:  return 0.7f;   // Snake
        case 33:  return 1.0f;   // Ghoul
        case 44:  return 1.0f;   // Gnoll
        case 46:  return 0.8f;   // Goblin
        case 47:  return 2.0f;   // Griffin
        case 50:  return 1.2f;   // Lion
        case 52:  return 1.1f;   // Lizard Man
        case 54:  return 1.0f;   // Orc
        case 65:  return 1.0f;   // Ghoul
        case 67:  return 1.0f;   // Zombie
        case 70:  return 1.0f;   // Zombie
        case 75:  return 1.0f;   // Elemental
        case 77:  return 1.2f;   // Werewolf
        case 91:  return 0.7f;   // Kobold
        case 367: return 1.0f;   // Decaying Skeleton

        // Monsters - large
        case 23:  return 1.4f;   // Kodiak Bear
        case 25:  return 1.3f;   // Alligator
        case 40:  return 1.3f;   // Gorilla
        case 51:  return 1.2f;   // Lion
        case 59:  return 1.0f;   // Puma
        case 85:  return 3.0f;   // Dragon
        case 90:  return 2.0f;   // Griffin
        case 94:  return 1.5f;   // Minotaur
        case 95:  return 1.5f;   // Drake

        // Invisible / Zone entities
        case 127: return 0.0f;   // Invisible Man
        case 240: return 0.0f;   // Zone Controller

        default:
            return 1.0f;
    }
}

std::string getRaceName(uint16_t raceId) {
    // Check SQLite race_models table
    {
        auto* db = WilleqDatabase::globalInstance();
        if (db) {
            auto info = db->getRaceModel(raceId);
            if (info.found && !info.name.empty()) return info.name;
        }
    }

    // Fallback to hardcoded defaults
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

        // Common NPCs/monsters - corrected names
        case 13:  return "Wolf";
        case 14:  return "Bear";
        case 15:  return "Freeport Guard";
        case 17:
        case 18:
        case 19:
        case 54:
        case 56:  return "Orc";
        case 20:  return "Brownie";
        case 21:
        case 60:
        case 64:
        case 367: return "Skeleton";
        case 22:  return "Beetle";
        case 23:  return "Kodiak Bear";
        case 24:  return "Fish";
        case 25:
        case 93:  return "Alligator";
        case 26:
        case 37:  return "Snake";
        case 27:  return "Spectre";
        case 28:
        case 77:  return "Werewolf";
        case 29:  return "Wolf";
        case 30:  return "Bear";
        case 31:
        case 32:  return "Freeport Citizen";
        case 33:
        case 65:  return "Ghoul";  // Corrected - was Ghost for 33
        case 34:  return "Giant Bat";
        case 35:
        case 36:  return "Rat";
        case 38:  return "Scarecrow";
        case 39:
        case 44:
        case 87:  return "Gnoll";
        case 40:  return "Gorilla";
        case 41:  return "Undead Pet";
        case 42:  return "Wolf";  // Corrected - was Rat
        case 43:  return "Bear";  // Corrected - was Bat
        case 46:  return "Goblin";
        case 47:
        case 90:  return "Griffin";  // Corrected - was not set for 47
        case 48:
        case 49:  return "Spider";
        case 50:
        case 51:  return "Lion";  // Corrected - 50 was Deer
        case 52:  return "Lizard Man";
        case 53:
        case 94:  return "Minotaur";
        case 55:  return "Lava Crawler";
        case 57:  return "Piranha";
        case 58:
        case 72:
        case 73:
        case 74:
        case 75:  return "Elemental";
        case 59:
        case 76:
        case 78:  return "Cat";  // 76 was Weapon Stand
        case 61:  return "Neriak Citizen";
        case 62:  return "Erudite Citizen";
        case 63:
        case 83:  return "Ghost";
        case 66:  return "Hag";
        case 67:
        case 70:  return "Zombie";  // 70 was Clock Creature
        case 68:  return "Sphinx";
        case 69:  return "Will-o-Wisp";  // Was Armadillo
        case 71:  return "Qeynos Citizen";
        case 79:  return "Elf";
        case 80:  return "Karana Citizen";
        case 81:  return "Vampire";
        case 82:  return "Highpass Citizen";
        case 84:  return "Dwarf Citizen";
        case 85:  return "Dragon";
        case 86:  return "Dark Elf Citizen";
        case 88:  return "Mosquito";
        case 89:  return "Imp";
        case 91:  return "Kobold";
        case 92:
        case 95:  return "Drake";
        case 96:  return "Genie";
        case 120: return "Eye of Zomm";
        case 127:
        case 240: return "Invisible";

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
