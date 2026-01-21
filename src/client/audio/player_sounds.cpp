#ifdef WITH_AUDIO

#include "client/audio/player_sounds.h"

namespace EQT {
namespace Audio {

// EverQuest playable race IDs (Titanium era)
namespace RaceId {
    constexpr uint16_t Human = 1;
    constexpr uint16_t Barbarian = 2;
    constexpr uint16_t Erudite = 3;
    constexpr uint16_t WoodElf = 4;
    constexpr uint16_t HighElf = 5;
    constexpr uint16_t DarkElf = 6;
    constexpr uint16_t HalfElf = 7;
    constexpr uint16_t Dwarf = 8;
    constexpr uint16_t Troll = 9;
    constexpr uint16_t Ogre = 10;
    constexpr uint16_t Halfling = 11;
    constexpr uint16_t Gnome = 12;
    constexpr uint16_t Iksar = 128;
    constexpr uint16_t VahShir = 130;
    constexpr uint16_t Froglok = 330;
}

// Gender IDs
namespace GenderId {
    constexpr uint8_t Male = 0;
    constexpr uint8_t Female = 1;
}

bool PlayerSounds::isValidRace(uint16_t raceId) {
    switch (raceId) {
        case RaceId::Human:
        case RaceId::Barbarian:
        case RaceId::Erudite:
        case RaceId::WoodElf:
        case RaceId::HighElf:
        case RaceId::DarkElf:
        case RaceId::HalfElf:
        case RaceId::Dwarf:
        case RaceId::Troll:
        case RaceId::Ogre:
        case RaceId::Halfling:
        case RaceId::Gnome:
        case RaceId::Iksar:
        case RaceId::VahShir:
        case RaceId::Froglok:
            return true;
        default:
            return false;
    }
}

bool PlayerSounds::isValidGender(uint8_t gender) {
    return gender == GenderId::Male || gender == GenderId::Female;
}

RaceCategory PlayerSounds::getRaceCategory(uint16_t raceId) {
    switch (raceId) {
        // Light voice races
        case RaceId::WoodElf:
        case RaceId::HighElf:
        case RaceId::HalfElf:
        case RaceId::Halfling:
        case RaceId::Gnome:
            return RaceCategory::Light;

        // Barbarian (unique deep voice)
        case RaceId::Barbarian:
            return RaceCategory::Barbarian;

        // Standard voice races (Human, Erudite, Dark Elf, Troll, Ogre, Iksar, Dwarf, Vah Shir, Froglok)
        case RaceId::Human:
        case RaceId::Erudite:
        case RaceId::DarkElf:
        case RaceId::Dwarf:
        case RaceId::Troll:
        case RaceId::Ogre:
        case RaceId::Iksar:
        case RaceId::VahShir:
        case RaceId::Froglok:
        default:
            return RaceCategory::Standard;
    }
}

std::string PlayerSounds::getSuffix(uint16_t raceId, uint8_t gender) {
    if (!isValidRace(raceId) || !isValidGender(gender)) {
        return "";
    }

    RaceCategory category = getRaceCategory(raceId);
    bool isMale = (gender == GenderId::Male);

    switch (category) {
        case RaceCategory::Light:
            return isMale ? "_ML" : "_FL";
        case RaceCategory::Barbarian:
            return isMale ? "_MB" : "_FB";
        case RaceCategory::Standard:
        default:
            return isMale ? "_M" : "_F";
    }
}

std::string PlayerSounds::getSoundFile(PlayerSoundType type, uint16_t raceId, uint8_t gender) {
    if (!isValidRace(raceId) || !isValidGender(gender)) {
        return "";
    }

    std::string suffix = getSuffix(raceId, gender);
    if (suffix.empty()) {
        return "";
    }

    // Build filename based on sound type
    std::string baseName;
    switch (type) {
        case PlayerSoundType::Death:
            baseName = "Death";
            break;
        case PlayerSoundType::Drown:
            baseName = "Drown";
            break;
        case PlayerSoundType::Jump:
            // Jump files use slightly different naming
            if (gender == GenderId::Male) {
                baseName = "JumpM_1";
            } else {
                baseName = "JumpF_1";
            }
            // Remove standard suffix, add category suffix
            if (getRaceCategory(raceId) == RaceCategory::Light) {
                return baseName + "L.WAV";
            } else if (getRaceCategory(raceId) == RaceCategory::Barbarian) {
                return baseName + "B.WAV";
            }
            return baseName + ".WAV";
        case PlayerSoundType::GetHit1:
            baseName = "GetHit1";
            break;
        case PlayerSoundType::GetHit2:
            baseName = "GetHit2";
            break;
        case PlayerSoundType::GetHit3:
            baseName = "GetHit3";
            break;
        case PlayerSoundType::GetHit4:
            baseName = "GetHit4";
            break;
        case PlayerSoundType::Gasp1:
            baseName = "Gasp1";
            break;
        case PlayerSoundType::Gasp2:
            baseName = "Gasp2";
            break;
        default:
            return "";
    }

    // GetHit files use uppercase suffix (e.g., GetHit1M, GetHit2ML)
    if (type == PlayerSoundType::GetHit1 || type == PlayerSoundType::GetHit2 ||
        type == PlayerSoundType::GetHit3 || type == PlayerSoundType::GetHit4 ||
        type == PlayerSoundType::Gasp1 || type == PlayerSoundType::Gasp2) {
        // These files don't have underscore in the suffix
        std::string shortSuffix;
        if (gender == GenderId::Male) {
            switch (getRaceCategory(raceId)) {
                case RaceCategory::Light:
                    shortSuffix = "ML";
                    break;
                case RaceCategory::Barbarian:
                    shortSuffix = "MB";
                    break;
                case RaceCategory::Standard:
                default:
                    shortSuffix = "M";
                    break;
            }
        } else {
            switch (getRaceCategory(raceId)) {
                case RaceCategory::Light:
                    shortSuffix = "FL";
                    break;
                case RaceCategory::Barbarian:
                    shortSuffix = "FB";
                    break;
                case RaceCategory::Standard:
                default:
                    shortSuffix = "F";
                    break;
            }
        }
        return baseName + shortSuffix + ".WAV";
    }

    // Standard files (Death, Drown) use underscore in suffix
    return baseName + suffix + ".WAV";
}

uint32_t PlayerSounds::getSoundId(PlayerSoundType type, uint16_t raceId, uint8_t gender) {
    if (!isValidRace(raceId) || !isValidGender(gender)) {
        return 0;
    }

    // Calculate offset based on race category and gender
    int32_t offset = 0;
    RaceCategory category = getRaceCategory(raceId);
    bool isMale = (gender == GenderId::Male);

    if (category == RaceCategory::Standard) {
        offset = isMale ? 0 : OFFSET_FEMALE_STANDARD;
    } else if (category == RaceCategory::Light) {
        offset = isMale ? OFFSET_MALE_LIGHT : OFFSET_FEMALE_LIGHT;
    } else if (category == RaceCategory::Barbarian) {
        offset = isMale ? OFFSET_MALE_BARBARIAN : OFFSET_FEMALE_BARBARIAN;
    }

    // Get base sound ID and apply offset
    uint32_t baseId = 0;
    switch (type) {
        case PlayerSoundType::Death:
            baseId = SOUND_ID_DEATH_M;
            break;
        case PlayerSoundType::Drown:
            baseId = SOUND_ID_DROWN_M;
            break;
        case PlayerSoundType::Jump:
            baseId = SOUND_ID_JUMP_M;
            break;
        case PlayerSoundType::GetHit1:
            baseId = SOUND_ID_GETHIT1_M;
            break;
        case PlayerSoundType::GetHit2:
            baseId = SOUND_ID_GETHIT2_M;
            break;
        case PlayerSoundType::GetHit3:
            baseId = SOUND_ID_GETHIT3_M;
            break;
        case PlayerSoundType::GetHit4:
            baseId = SOUND_ID_GETHIT4_M;
            break;
        case PlayerSoundType::Gasp1:
            baseId = SOUND_ID_GASP1_M;
            break;
        case PlayerSoundType::Gasp2:
            baseId = SOUND_ID_GASP2_M;
            break;
        default:
            return 0;
    }

    return baseId + offset;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
