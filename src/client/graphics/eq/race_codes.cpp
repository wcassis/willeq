#include "client/graphics/eq/race_codes.h"
#include "common/logging.h"
#include <json/json.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>

namespace EQT {
namespace Graphics {

// Static storage for race mappings loaded from JSON
struct RaceMapping {
    std::string code;
    std::string name;
    std::string femaleCode;
    std::string s3dFile;
    std::string fallbackCode;
    std::string animationSource;     // Animation source code (e.g., "RAT" for ARM)
    std::string animationSourceS3d;  // S3D file containing animation source (e.g., "qeynos_chr.s3d")
    float scale = 1.0f;
    bool genderVariants = false;
    bool invisible = false;
};

static std::map<uint16_t, RaceMapping> s_raceMappings;
static std::string s_jsonPath;
static bool s_mappingsLoaded = false;
static std::mutex s_mappingsMutex;

bool loadRaceMappings(const std::string& jsonPath) {
    std::lock_guard<std::mutex> lock(s_mappingsMutex);

    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_WARN(MOD_GRAPHICS, "Could not open race mappings file: {}", jsonPath);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to parse race mappings JSON: {}", errors);
        return false;
    }

    s_raceMappings.clear();

    const Json::Value& races = root["races"];
    if (!races.isObject()) {
        LOG_ERROR(MOD_GRAPHICS, "race_models.json missing 'races' object");
        return false;
    }

    for (const auto& raceIdStr : races.getMemberNames()) {
        uint16_t raceId = static_cast<uint16_t>(std::stoi(raceIdStr));
        const Json::Value& raceData = races[raceIdStr];

        RaceMapping mapping;
        mapping.code = raceData.get("code", "").asString();
        mapping.name = raceData.get("name", "").asString();
        mapping.femaleCode = raceData.get("female_code", "").asString();
        mapping.s3dFile = raceData.get("s3d_file", "").asString();
        mapping.fallbackCode = raceData.get("fallback_code", "").asString();
        mapping.animationSource = raceData.get("animation_source", "").asString();
        mapping.animationSourceS3d = raceData.get("animation_source_s3d", "").asString();
        mapping.scale = static_cast<float>(raceData.get("scale", 1.0).asDouble());
        mapping.genderVariants = raceData.get("gender_variants", false).asBool();
        mapping.invisible = raceData.get("invisible", false).asBool();

        s_raceMappings[raceId] = mapping;
    }

    s_jsonPath = jsonPath;
    s_mappingsLoaded = true;
    LOG_INFO(MOD_GRAPHICS, "Loaded {} race mappings from {}", s_raceMappings.size(), jsonPath);
    return true;
}

bool reloadRaceMappings() {
    if (s_jsonPath.empty()) {
        LOG_WARN(MOD_GRAPHICS, "Cannot reload race mappings: no JSON file loaded");
        return false;
    }
    return loadRaceMappings(s_jsonPath);
}

bool areRaceMappingsLoaded() {
    return s_mappingsLoaded;
}

std::string getRaceS3DFile(uint16_t raceId) {
    std::lock_guard<std::mutex> lock(s_mappingsMutex);
    auto it = s_raceMappings.find(raceId);
    if (it != s_raceMappings.end()) {
        return it->second.s3dFile;
    }
    return "";
}

std::string getRaceS3DFileByCode(const std::string& raceCode) {
    std::lock_guard<std::mutex> lock(s_mappingsMutex);
    std::string upperCode = raceCode;
    std::transform(upperCode.begin(), upperCode.end(), upperCode.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Search all race mappings for one with this code
    for (const auto& [raceId, mapping] : s_raceMappings) {
        std::string mappingCode = mapping.code;
        std::transform(mappingCode.begin(), mappingCode.end(), mappingCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (mappingCode == upperCode && !mapping.s3dFile.empty()) {
            return mapping.s3dFile;
        }
    }
    return "";
}

std::string getAnimationSourceFromConfig(uint16_t raceId) {
    std::lock_guard<std::mutex> lock(s_mappingsMutex);
    auto it = s_raceMappings.find(raceId);
    if (it != s_raceMappings.end()) {
        return it->second.animationSource;
    }
    return "";
}

std::string getAnimationSourceS3DFile(uint16_t raceId) {
    std::lock_guard<std::mutex> lock(s_mappingsMutex);
    auto it = s_raceMappings.find(raceId);
    if (it != s_raceMappings.end()) {
        return it->second.animationSourceS3d;
    }
    return "";
}

std::string getAnimationSourceS3DFileByCode(const std::string& raceCode) {
    std::lock_guard<std::mutex> lock(s_mappingsMutex);
    std::string upperCode = raceCode;
    std::transform(upperCode.begin(), upperCode.end(), upperCode.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Search all race mappings for one with this code and an animation_source_s3d
    for (const auto& [raceId, mapping] : s_raceMappings) {
        std::string mappingCode = mapping.code;
        std::transform(mappingCode.begin(), mappingCode.end(), mappingCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (mappingCode == upperCode && !mapping.animationSourceS3d.empty()) {
            return mapping.animationSourceS3d;
        }
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

std::string getRaceCode(uint16_t raceId) {
    // First check JSON mappings
    {
        std::lock_guard<std::mutex> lock(s_mappingsMutex);
        auto it = s_raceMappings.find(raceId);
        if (it != s_raceMappings.end() && !it->second.code.empty()) {
            return it->second.code;
        }
    }

    // Fallback to hardcoded defaults for races not in JSON
    // These are kept for backwards compatibility and as fallback
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

        // Common monsters/NPCs - corrected mappings
        case 13:  return "WOL";  // Wolf
        case 14:  return "BEA";  // Bear
        case 15:  return "HUM";  // Freeport Guard (Human)
        case 17:  return "ORC";  // Orc
        case 18:  return "ORC";  // Orc (variant)
        case 19:  return "ORC";  // Orc (variant 2)
        case 20:  return "BRM";  // Brownie
        case 21:  return "SKE";  // Skeleton
        case 22:  return "BET";  // Beetle
        case 23:  return "BEA";  // Kodiak Bear (same model as bear)
        case 24:  return "FIS";  // Fish
        case 25:  return "ALL";  // Alligator
        case 26:  return "SNA";  // Snake
        case 27:  return "SPE";  // Spectre
        case 28:  return "WER";  // Werewolf
        case 29:  return "WOL";  // Wolf (variant)
        case 30:  return "BEA";  // Bear (variant)
        case 31:  return "FPM";  // Freeport Citizen Male
        case 32:  return "FPM";  // Freeport Citizen (variant)
        case 33:  return "GHU";  // Ghoul (NOT Ghost!)
        case 34:  return "BAT";  // Giant Bat
        case 35:  return "RAT";  // Rat
        case 36:  return "RAT";  // Giant Rat
        case 37:  return "SNA";  // Snake
        case 38:  return "SCA";  // Scarecrow
        case 39:  return "GNN";  // Gnoll Pup
        case 40:  return "GOR";  // Gorilla
        case 41:  return "SKE";  // Undead Pet (skeleton)
        case 42:  return "WOL";  // Wolf (NOT Rat!)
        case 43:  return "BEA";  // Bear (NOT Bat!)
        case 44:  return "GNN";  // Gnoll
        case 45:  return "SNA";  // Sand Elf (unused)
        case 46:  return "GOB";  // Goblin
        case 47:  return "GRI";  // Griffin (NOT Corn!)
        case 48:  return "SPI";  // Spider (small)
        case 49:  return "SPI";  // Spider (large)
        case 50:  return "LIM";  // Lion Male (NOT Deer!)
        case 51:  return "LIM";  // Lion
        case 52:  return "LIZ";  // Lizard Man
        case 53:  return "MIN";  // Minotaur
        case 54:  return "ORC";  // Orc (NOT Warslik!)
        case 55:  return "DRK";  // Lava Crawler
        case 56:  return "ORC";  // Orc (Crushbone)
        case 57:  return "PIR";  // Piranha
        case 58:  return "ELE";  // Elemental
        case 59:  return "PUM";  // Puma
        case 60:  return "SKE";  // Skeleton (variant)
        case 61:  return "NGM";  // Neriak Citizen Male
        case 62:  return "EGM";  // Erudite Citizen Male
        case 63:  return "GHO";  // Ghost
        case 64:  return "SKE";  // Greater Skeleton
        case 65:  return "GHU";  // Ghoul
        case 66:  return "VSM";  // Hag (Vampire model)
        case 67:  return "ZOM";  // Zombie
        case 68:  return "SPH";  // Sphinx
        case 69:  return "WIL";  // Will-o-Wisp (NOT Armadillo!)
        case 70:  return "ZOM";  // Zombie (NOT Clock Creature!)
        case 71:  return "QCM";  // Qeynos Citizen Male
        case 72:  return "ELE";  // Earth Elemental
        case 73:  return "ELE";  // Air Elemental
        case 74:  return "ELE";  // Water Elemental
        case 75:  return "ELE";  // Fire Elemental (generic ELE, NOT FIR!)
        case 76:  return "PUM";  // Cat/Puma (NOT Weapon Stand!)
        case 77:  return "WER";  // Werewolf
        case 78:  return "PUM";  // Cat
        case 79:  return "ELM";  // Elf
        case 80:  return "QCM";  // Karana Citizen
        case 81:  return "VSM";  // Vampire
        case 82:  return "HHM";  // Highpass Citizen Male
        case 83:  return "GHO";  // Ghost
        case 84:  return "DWM";  // Dwarf Citizen
        case 85:  return "DRA";  // Dragon
        case 86:  return "DAM";  // Dark Elf Citizen
        case 87:  return "GNN";  // Gnoll
        case 88:  return "MOS";  // Mosquito
        case 89:  return "IMP";  // Imp
        case 90:  return "GRI";  // Griffin
        case 91:  return "KOB";  // Kobold
        case 92:  return "DRA";  // Drake
        case 93:  return "ALL";  // Alligator
        case 94:  return "MIN";  // Minotaur
        case 95:  return "DRA";  // Drake
        case 96:  return "DJI";  // Genie
        case 120: return "EYE";  // Eye of Zomm
        case 127: return "INV";  // Invisible Man
        case 240: return "INV";  // Zone Controller / Marker

        // Higher race IDs
        case 367: return "SKE";  // Decaying Skeleton

        default:
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
    // First check JSON mappings for fallback_code
    {
        std::lock_guard<std::mutex> lock(s_mappingsMutex);
        auto it = s_raceMappings.find(raceId);
        if (it != s_raceMappings.end() && !it->second.fallbackCode.empty()) {
            std::string fallback = it->second.fallbackCode;
            // Apply gender suffix if it ends in M and gender is female
            if (gender == 1 && fallback.length() == 3 && fallback.back() == 'M') {
                fallback.back() = 'F';
            }
            return fallback;
        }
    }

    // Fallback to hardcoded defaults
    switch (raceId) {
        case 71:  // Qeynos Citizen -> Human
        case 15:  // Freeport Guard -> Human
        case 80:  // Karana Citizen -> Human
        case 82:  // Highpass Citizen -> Human
            return (gender == 1) ? "HUF" : "HUM";

        case 61:  // Neriak Citizen -> Dark Elf
        case 86:  // Dark Elf Citizen -> Dark Elf
            return (gender == 1) ? "DAF" : "DAM";

        case 62:  // Erudite Citizen -> Erudite
            return (gender == 1) ? "ERF" : "ERM";

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
    // First check JSON mappings
    {
        std::lock_guard<std::mutex> lock(s_mappingsMutex);
        auto it = s_raceMappings.find(raceId);
        if (it != s_raceMappings.end()) {
            return it->second.scale;
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
    // First check JSON mappings
    {
        std::lock_guard<std::mutex> lock(s_mappingsMutex);
        auto it = s_raceMappings.find(raceId);
        if (it != s_raceMappings.end() && !it->second.name.empty()) {
            return it->second.name;
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
