#pragma once

#include <cstdint>
#include <string>

namespace eqt {
namespace inventory {

// Equipment slot IDs (0-21)
enum EquipmentSlot : int16_t {
    SLOT_CHARM = 0,
    SLOT_EAR1 = 1,
    SLOT_HEAD = 2,
    SLOT_FACE = 3,
    SLOT_EAR2 = 4,
    SLOT_NECK = 5,
    SLOT_SHOULDERS = 6,
    SLOT_ARMS = 7,
    SLOT_BACK = 8,
    SLOT_WRIST1 = 9,
    SLOT_WRIST2 = 10,
    SLOT_RANGE = 11,
    SLOT_HANDS = 12,
    SLOT_PRIMARY = 13,
    SLOT_SECONDARY = 14,
    SLOT_RING1 = 15,
    SLOT_RING2 = 16,
    SLOT_CHEST = 17,
    SLOT_LEGS = 18,
    SLOT_FEET = 19,
    SLOT_WAIST = 20,
    SLOT_AMMO = 21
};

// Slot ranges
constexpr int16_t EQUIPMENT_BEGIN = 0;
constexpr int16_t EQUIPMENT_END = 21;
constexpr int16_t EQUIPMENT_COUNT = 22;

constexpr int16_t GENERAL_BEGIN = 22;
constexpr int16_t GENERAL_END = 29;
constexpr int16_t GENERAL_COUNT = 8;

constexpr int16_t CURSOR_SLOT = 30;
constexpr int16_t SERVER_CURSOR_SLOT = 33;  // Server uses slot 33 for cursor
constexpr int16_t POSSESSIONS_COUNT = 31;

// Bag slot ranges (each bag has 10 slots)
constexpr int16_t BAG_SLOT_COUNT = 10;
constexpr int16_t GENERAL_BAGS_BEGIN = 251;
constexpr int16_t GENERAL_BAGS_END = 330;  // 8 bags * 10 slots = 80 slots
constexpr int16_t CURSOR_BAG_BEGIN = 331;
constexpr int16_t CURSOR_BAG_END = 340;

// Bank slots
constexpr int16_t BANK_BEGIN = 2000;
constexpr int16_t BANK_END = 2015;
constexpr int16_t BANK_COUNT = 16;
constexpr int16_t BANK_BAGS_BEGIN = 2031;
constexpr int16_t BANK_BAGS_END = 2190;

// Shared bank slots
constexpr int16_t SHARED_BANK_BEGIN = 2500;
constexpr int16_t SHARED_BANK_END = 2501;
constexpr int16_t SHARED_BANK_COUNT = 2;
constexpr int16_t SHARED_BANK_BAGS_BEGIN = 2531;
constexpr int16_t SHARED_BANK_BAGS_END = 2550;

// Trade slots
constexpr int16_t TRADE_BEGIN = 3000;
constexpr int16_t TRADE_END = 3007;
constexpr int16_t TRADE_COUNT = 8;
constexpr int16_t TRADE_BAGS_BEGIN = 3031;
constexpr int16_t TRADE_BAGS_END = 3110;

// World/ground items
constexpr int16_t WORLD_BEGIN = 4000;
constexpr int16_t WORLD_END = 4009;
constexpr int16_t WORLD_COUNT = 10;

// Invalid slot
constexpr int16_t SLOT_INVALID = -1;

// Item sizes
enum ItemSize : uint8_t {
    SIZE_TINY = 0,
    SIZE_SMALL = 1,
    SIZE_MEDIUM = 2,
    SIZE_LARGE = 3,
    SIZE_GIANT = 4,
    SIZE_GIGANTIC = 5
};

// Item flags (bitmask)
enum ItemFlags : uint32_t {
    ITEM_FLAG_NONE = 0,
    ITEM_FLAG_MAGIC = (1 << 0),
    ITEM_FLAG_LORE = (1 << 1),
    ITEM_FLAG_NODROP = (1 << 2),
    ITEM_FLAG_NORENT = (1 << 3),
    ITEM_FLAG_ARTIFACT = (1 << 4),
    ITEM_FLAG_SUMMONED = (1 << 5),
    ITEM_FLAG_NODESTROY = (1 << 6),
    ITEM_FLAG_STACKABLE = (1 << 7),
    ITEM_FLAG_EXPENDABLE = (1 << 8),
    ITEM_FLAG_QUESTITEM = (1 << 9)
};

// Equipment slot bitmask (for item.slots field)
enum EquipSlotBit : uint32_t {
    EQUIPSLOT_CHARM = (1 << 0),
    EQUIPSLOT_EAR = (1 << 1),      // Both ears
    EQUIPSLOT_HEAD = (1 << 2),
    EQUIPSLOT_FACE = (1 << 3),
    EQUIPSLOT_NECK = (1 << 5),
    EQUIPSLOT_SHOULDERS = (1 << 6),
    EQUIPSLOT_ARMS = (1 << 7),
    EQUIPSLOT_BACK = (1 << 8),
    EQUIPSLOT_WRIST = (1 << 9),    // Both wrists
    EQUIPSLOT_RANGE = (1 << 11),
    EQUIPSLOT_HANDS = (1 << 12),
    EQUIPSLOT_PRIMARY = (1 << 13),
    EQUIPSLOT_SECONDARY = (1 << 14),
    EQUIPSLOT_FINGER = (1 << 15),  // Both fingers
    EQUIPSLOT_CHEST = (1 << 17),
    EQUIPSLOT_LEGS = (1 << 18),
    EQUIPSLOT_FEET = (1 << 19),
    EQUIPSLOT_WAIST = (1 << 20),
    EQUIPSLOT_AMMO = (1 << 21)
};

// Class bitmask values
enum ClassBit : uint32_t {
    CLASS_WARRIOR = (1 << 0),
    CLASS_CLERIC = (1 << 1),
    CLASS_PALADIN = (1 << 2),
    CLASS_RANGER = (1 << 3),
    CLASS_SHADOWKNIGHT = (1 << 4),
    CLASS_DRUID = (1 << 5),
    CLASS_MONK = (1 << 6),
    CLASS_BARD = (1 << 7),
    CLASS_ROGUE = (1 << 8),
    CLASS_SHAMAN = (1 << 9),
    CLASS_NECROMANCER = (1 << 10),
    CLASS_WIZARD = (1 << 11),
    CLASS_MAGICIAN = (1 << 12),
    CLASS_ENCHANTER = (1 << 13),
    CLASS_BEASTLORD = (1 << 14),
    CLASS_BERSERKER = (1 << 15),
    CLASS_ALL = 0xFFFF
};

// Race bitmask values
enum RaceBit : uint32_t {
    RACE_HUMAN = (1 << 0),
    RACE_BARBARIAN = (1 << 1),
    RACE_ERUDITE = (1 << 2),
    RACE_WOOD_ELF = (1 << 3),
    RACE_HIGH_ELF = (1 << 4),
    RACE_DARK_ELF = (1 << 5),
    RACE_HALF_ELF = (1 << 6),
    RACE_DWARF = (1 << 7),
    RACE_TROLL = (1 << 8),
    RACE_OGRE = (1 << 9),
    RACE_HALFLING = (1 << 10),
    RACE_GNOME = (1 << 11),
    RACE_IKSAR = (1 << 12),
    RACE_VAH_SHIR = (1 << 13),
    RACE_FROGLOK = (1 << 14),
    RACE_DRAKKIN = (1 << 15),
    RACE_ALL = 0xFFFF
};

// Weapon skill types (for skillType field)
enum WeaponSkillType : uint8_t {
    SKILL_1H_SLASHING = 0,      // 1HS - 1-hand slashing
    SKILL_2H_SLASHING = 1,      // 2HS - 2-hand slashing
    SKILL_PIERCING = 2,         // 1HP - piercing
    SKILL_1H_BLUNT = 3,         // 1HB - 1-hand blunt
    SKILL_2H_BLUNT = 4,         // 2HB - 2-hand blunt
    SKILL_ARCHERY = 5,          // BOW - archery
    SKILL_THROWING = 6,         // THR - throwing
    SKILL_HAND_TO_HAND = 7,     // H2H - hand to hand
    SKILL_2H_PIERCING = 35,     // 2HP - 2-hand piercing
    SKILL_NONE = 255            // No weapon skill
};

// Get weapon type abbreviation string
inline const char* getWeaponTypeAbbrev(uint8_t skillType) {
    switch (skillType) {
        case SKILL_1H_SLASHING:  return "1HS";
        case SKILL_2H_SLASHING:  return "2HS";
        case SKILL_PIERCING:     return "1HP";
        case SKILL_1H_BLUNT:     return "1HB";
        case SKILL_2H_BLUNT:     return "2HB";
        case SKILL_ARCHERY:      return "BOW";
        case SKILL_THROWING:     return "THR";
        case SKILL_HAND_TO_HAND: return "H2H";
        case SKILL_2H_PIERCING:  return "2HP";
        default:                 return nullptr;
    }
}

// Augment type bitmask
enum AugTypeBit : uint32_t {
    AUG_TYPE_NONE = 0x00000000,
    AUG_TYPE_GENERAL_SINGLE_STAT = 0x00000001,
    AUG_TYPE_GENERAL_MULTIPLE_STAT = 0x00000002,
    AUG_TYPE_GENERAL_SPELL_EFFECT = 0x00000004,
    AUG_TYPE_WEAPON_GENERAL = 0x00000008,
    AUG_TYPE_WEAPON_ELEM_DAMAGE = 0x00000010,
    AUG_TYPE_WEAPON_BASE_DAMAGE = 0x00000020,
    AUG_TYPE_GENERAL_GROUP = 0x00000040,
    AUG_TYPE_GENERAL_RAID = 0x00000080,
    AUG_TYPE_ORNAMENTATION = 0x00080000,
    AUG_TYPE_SPECIAL_ORNAMENTATION = 0x00100000
};

// Maximum augment sockets per item
constexpr int MAX_AUGMENT_SLOTS = 5;

// Slot calculation utilities
inline int16_t calcBagSlotId(int16_t generalSlot, int16_t bagIndex) {
    if (generalSlot < GENERAL_BEGIN || generalSlot > GENERAL_END) {
        return SLOT_INVALID;
    }
    if (bagIndex < 0 || bagIndex >= BAG_SLOT_COUNT) {
        return SLOT_INVALID;
    }
    return GENERAL_BAGS_BEGIN + (generalSlot - GENERAL_BEGIN) * BAG_SLOT_COUNT + bagIndex;
}

inline int16_t calcCursorBagSlotId(int16_t bagIndex) {
    if (bagIndex < 0 || bagIndex >= BAG_SLOT_COUNT) {
        return SLOT_INVALID;
    }
    return CURSOR_BAG_BEGIN + bagIndex;
}

inline bool isEquipmentSlot(int16_t slotId) {
    return slotId >= EQUIPMENT_BEGIN && slotId <= EQUIPMENT_END;
}

inline bool isGeneralSlot(int16_t slotId) {
    return slotId >= GENERAL_BEGIN && slotId <= GENERAL_END;
}

inline bool isBagSlot(int16_t slotId) {
    return (slotId >= GENERAL_BAGS_BEGIN && slotId <= GENERAL_BAGS_END) ||
           (slotId >= CURSOR_BAG_BEGIN && slotId <= CURSOR_BAG_END);
}

inline bool isCursorSlot(int16_t slotId) {
    return slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT;
}

inline bool isTradeSlot(int16_t slotId) {
    return slotId >= TRADE_BEGIN && slotId <= TRADE_END;
}

// Get the parent general slot for a bag slot
inline int16_t getParentGeneralSlot(int16_t bagSlot) {
    if (bagSlot >= GENERAL_BAGS_BEGIN && bagSlot <= GENERAL_BAGS_END) {
        return GENERAL_BEGIN + (bagSlot - GENERAL_BAGS_BEGIN) / BAG_SLOT_COUNT;
    }
    if (bagSlot >= CURSOR_BAG_BEGIN && bagSlot <= CURSOR_BAG_END) {
        return CURSOR_SLOT;
    }
    return SLOT_INVALID;
}

// Get the index within a bag (0-9)
inline int16_t getBagIndex(int16_t bagSlot) {
    if (bagSlot >= GENERAL_BAGS_BEGIN && bagSlot <= GENERAL_BAGS_END) {
        return (bagSlot - GENERAL_BAGS_BEGIN) % BAG_SLOT_COUNT;
    }
    if (bagSlot >= CURSOR_BAG_BEGIN && bagSlot <= CURSOR_BAG_END) {
        return bagSlot - CURSOR_BAG_BEGIN;
    }
    return SLOT_INVALID;
}

// Get human-readable slot name
inline const char* getSlotName(int16_t slotId) {
    switch (slotId) {
        case SLOT_CHARM: return "Charm";
        case SLOT_EAR1: return "Ear";
        case SLOT_HEAD: return "Head";
        case SLOT_FACE: return "Face";
        case SLOT_EAR2: return "Ear";
        case SLOT_NECK: return "Neck";
        case SLOT_SHOULDERS: return "Shoulders";
        case SLOT_ARMS: return "Arms";
        case SLOT_BACK: return "Back";
        case SLOT_WRIST1: return "Wrist";
        case SLOT_WRIST2: return "Wrist";
        case SLOT_RANGE: return "Range";
        case SLOT_HANDS: return "Hands";
        case SLOT_PRIMARY: return "Primary";
        case SLOT_SECONDARY: return "Secondary";
        case SLOT_RING1: return "Fingers";
        case SLOT_RING2: return "Fingers";
        case SLOT_CHEST: return "Chest";
        case SLOT_LEGS: return "Legs";
        case SLOT_FEET: return "Feet";
        case SLOT_WAIST: return "Waist";
        case SLOT_AMMO: return "Ammo";
        default: return "Unknown";
    }
}

// Get size name
inline const char* getSizeName(uint8_t size) {
    switch (size) {
        case SIZE_TINY: return "TINY";
        case SIZE_SMALL: return "SMALL";
        case SIZE_MEDIUM: return "MEDIUM";
        case SIZE_LARGE: return "LARGE";
        case SIZE_GIANT: return "GIANT";
        case SIZE_GIGANTIC: return "GIGANTIC";
        default: return "UNKNOWN";
    }
}

// Class abbreviations for display
inline const char* getClassAbbrev(int classId) {
    switch (classId) {
        case 0: return "WAR";
        case 1: return "CLR";
        case 2: return "PAL";
        case 3: return "RNG";
        case 4: return "SHD";
        case 5: return "DRU";
        case 6: return "MNK";
        case 7: return "BRD";
        case 8: return "ROG";
        case 9: return "SHM";
        case 10: return "NEC";
        case 11: return "WIZ";
        case 12: return "MAG";
        case 13: return "ENC";
        case 14: return "BST";
        case 15: return "BER";
        default: return "???";
    }
}

// Build class restriction string from bitmask
std::string buildClassString(uint32_t classMask);

// Build race restriction string from bitmask
std::string buildRaceString(uint32_t raceMask);

// Build equippable slots string from bitmask
std::string buildSlotsString(uint32_t slotMask);

} // namespace inventory
} // namespace eqt
