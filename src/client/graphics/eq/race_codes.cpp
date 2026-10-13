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

bool loadRaceMappings() {
    // Race mappings are served on-demand from SQLite; the only thing that can
    // fail here is having no database (T402, board B246).
    s_mappingsLoaded = (WilleqDatabase::globalInstance() != nullptr);
    if (s_mappingsLoaded) {
        LOG_INFO(MOD_GRAPHICS, "Race mappings using on-demand SQLite lookup");
    }
    return s_mappingsLoaded;
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

std::string getAnimationSourceS3DFile(uint16_t raceId) {
    auto* db = WilleqDatabase::globalInstance();
    if (db) {
        auto info = db->getRaceModel(raceId);
        if (info.found) return info.animationSourceS3d;
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

// T271: db only, the same rule T270 applied to the code functions. The
// fallback switch this replaces was keyed on the pre-JSON numbering and so
// answered with another race's size whenever a row was missing: 13 was 0.8
// "Wolf" (row 13 is AVI Aviak, 1.2), 35 was 0.2 "Rat" (EEL Eel, 1.0), 36 was
// 0.4 "Giant Rat" (RAT Rat, 0.3), 39 was 0.6 "Gnoll Pup" (GNN Gnoll, 1.0),
// 46 was 0.8 "Goblin" (IMP Imp, 0.5), 48 was 0.3 "Spider" (KOB Kobold, 0.7),
// 49 was 0.6 "Spider" (DRA Lava Dragon, 3.0), 91 was 0.7 "Kobold" (ALL
// Alligator, 1.3), 94 was 1.5 "Minotaur" (KAM Kaladim Citizen, 0.7), 95 was
// 1.5 "Drake" (CAZ Cazic Thule, 3.0). With no row there is no scale data, so
// 1.0 -- the switch's own default -- is the only honest answer.
float getRaceScale(uint16_t raceId) {
    auto* db = WilleqDatabase::globalInstance();
    if (db) {
        auto info = db->getRaceModel(raceId);
        if (info.found) return info.scale;
    }
    return 1.0f;
}

// T304 Leg 2: the placeholder-cube tint, db only -- the same rule T270/T271
// applied to the code and scale functions. This replaces the hand-written
// switch in EntityRenderer::getColorForRace(), which was the LAST surviving
// copy of the pre-JSON race numbering: it answered 21 as "Skeleton" (row 21
// is BEH Evil Eye), 26 "Snake" (FRO Froglok), 33 "Ghost" (GHU Ghoul), 35
// "Rat" (EEL Eel), 42 "Rat" (WOL Wolf), 43 "Bat" (BEA Bear), 46 "Goblin"
// (IMP Imp), 48/49 "Spider" (KOB Kobold / DRA Lava Dragon) and 85 "Dragon"
// (SPE Spectre). The column is keyed on each row's own name instead.
//
// THE COLUMN'S VALUES ARE INVENTED. Will ruled on 2026-08-28 that inventing
// them is acceptable; the bar is "plausible and distinguishable", not
// authenticity. They are not sourced from EverQuest data -- see the _notes in
// config/race_models.json. With no row there is no colour data, so the
// switch's own generic grey is the only honest answer.
uint32_t getRaceColorARGB(uint16_t raceId) {
    auto* db = WilleqDatabase::globalInstance();
    if (db) {
        auto info = db->getRaceModel(raceId);
        if (info.found && info.colorARGB != 0) return info.colorARGB;
        // A found row whose colour is exactly 0 is the fully transparent
        // invisible tint (races 127/240), and that is deliberate.
        if (info.found && info.invisible) return 0;
    }
    return kDefaultRaceColorARGB;
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


} // namespace Graphics
} // namespace EQT
