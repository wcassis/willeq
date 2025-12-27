#include "client/graphics/ui/inventory_constants.h"
#include <sstream>

namespace eqt {
namespace inventory {

std::string buildClassString(uint32_t classMask) {
    if (classMask == 0 || classMask == CLASS_ALL) {
        return "ALL";
    }

    std::ostringstream ss;
    bool first = true;

    const struct { uint32_t bit; const char* abbrev; } classes[] = {
        { CLASS_WARRIOR, "WAR" },
        { CLASS_CLERIC, "CLR" },
        { CLASS_PALADIN, "PAL" },
        { CLASS_RANGER, "RNG" },
        { CLASS_SHADOWKNIGHT, "SHD" },
        { CLASS_DRUID, "DRU" },
        { CLASS_MONK, "MNK" },
        { CLASS_BARD, "BRD" },
        { CLASS_ROGUE, "ROG" },
        { CLASS_SHAMAN, "SHM" },
        { CLASS_NECROMANCER, "NEC" },
        { CLASS_WIZARD, "WIZ" },
        { CLASS_MAGICIAN, "MAG" },
        { CLASS_ENCHANTER, "ENC" },
        { CLASS_BEASTLORD, "BST" },
        { CLASS_BERSERKER, "BER" }
    };

    for (const auto& c : classes) {
        if (classMask & c.bit) {
            if (!first) ss << " ";
            ss << c.abbrev;
            first = false;
        }
    }

    return ss.str();
}

std::string buildRaceString(uint32_t raceMask) {
    if (raceMask == 0 || raceMask == RACE_ALL) {
        return "ALL";
    }

    std::ostringstream ss;
    bool first = true;

    const struct { uint32_t bit; const char* name; } races[] = {
        { RACE_HUMAN, "HUM" },
        { RACE_BARBARIAN, "BAR" },
        { RACE_ERUDITE, "ERU" },
        { RACE_WOOD_ELF, "ELF" },
        { RACE_HIGH_ELF, "HIE" },
        { RACE_DARK_ELF, "DEF" },
        { RACE_HALF_ELF, "HEF" },
        { RACE_DWARF, "DWF" },
        { RACE_TROLL, "TRL" },
        { RACE_OGRE, "OGR" },
        { RACE_HALFLING, "HFL" },
        { RACE_GNOME, "GNM" },
        { RACE_IKSAR, "IKS" },
        { RACE_VAH_SHIR, "VAH" },
        { RACE_FROGLOK, "FRG" },
        { RACE_DRAKKIN, "DRK" }
    };

    for (const auto& r : races) {
        if (raceMask & r.bit) {
            if (!first) ss << " ";
            ss << r.name;
            first = false;
        }
    }

    return ss.str();
}

std::string buildSlotsString(uint32_t slotMask) {
    if (slotMask == 0) {
        return "NONE";
    }

    std::ostringstream ss;
    bool first = true;

    const struct { uint32_t bit; const char* name; } slots[] = {
        { EQUIPSLOT_CHARM, "Charm" },
        { EQUIPSLOT_EAR, "Ear" },
        { EQUIPSLOT_HEAD, "Head" },
        { EQUIPSLOT_FACE, "Face" },
        { EQUIPSLOT_NECK, "Neck" },
        { EQUIPSLOT_SHOULDERS, "Shoulders" },
        { EQUIPSLOT_ARMS, "Arms" },
        { EQUIPSLOT_BACK, "Back" },
        { EQUIPSLOT_WRIST, "Wrist" },
        { EQUIPSLOT_RANGE, "Range" },
        { EQUIPSLOT_HANDS, "Hands" },
        { EQUIPSLOT_PRIMARY, "Primary" },
        { EQUIPSLOT_SECONDARY, "Secondary" },
        { EQUIPSLOT_FINGER, "Fingers" },
        { EQUIPSLOT_CHEST, "Chest" },
        { EQUIPSLOT_LEGS, "Legs" },
        { EQUIPSLOT_FEET, "Feet" },
        { EQUIPSLOT_WAIST, "Waist" },
        { EQUIPSLOT_AMMO, "Ammo" }
    };

    for (const auto& s : slots) {
        if (slotMask & s.bit) {
            if (!first) ss << " ";
            ss << s.name;
            first = false;
        }
    }

    return ss.str();
}

} // namespace inventory
} // namespace eqt
