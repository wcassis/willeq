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

// Tradeskill world container slot (used when combining in a world object like a forge)
// This is the slot used in OP_TradeSkillCombine when using a world container
constexpr int16_t SLOT_TRADESKILL_EXPERIMENT_COMBINE = 1000;

// Invalid slot
constexpr int16_t SLOT_INVALID = -1;

// ============================================================================
// Tradeskill Container Types (bagType values)
// These correspond to EQEmu's ObjectType namespace values
// Used to identify what type of tradeskill a container supports
// ============================================================================
enum TradeskillContainerType : uint8_t {
    CONTAINER_SMALL_BAG = 0,
    CONTAINER_LARGE_BAG = 1,
    CONTAINER_QUIVER = 2,
    CONTAINER_BELT_POUCH = 3,
    CONTAINER_WRIST_POUCH = 4,
    CONTAINER_BACKPACK = 5,
    CONTAINER_SMALL_CHEST = 6,
    CONTAINER_LARGE_CHEST = 7,
    CONTAINER_BANDOLIER = 8,
    CONTAINER_MEDICINE = 9,
    CONTAINER_TINKERING = 10,
    CONTAINER_LEXICON = 11,
    CONTAINER_POISON_MAKING = 12,
    CONTAINER_QUEST = 13,
    CONTAINER_MIXING_BOWL = 14,
    CONTAINER_BAKING = 15,
    CONTAINER_TAILORING = 16,
    CONTAINER_BLACKSMITHING = 17,
    CONTAINER_FLETCHING = 18,
    CONTAINER_BREWING = 19,
    CONTAINER_JEWELRY_MAKING = 20,
    CONTAINER_POTTERY = 21,
    CONTAINER_KILN = 22,
    CONTAINER_KEY_MAKER = 23,
    CONTAINER_RESEARCH_WIZ = 24,
    CONTAINER_RESEARCH_MAG = 25,
    CONTAINER_RESEARCH_NEC = 26,
    CONTAINER_RESEARCH_ENC = 27,
    CONTAINER_UNKNOWN = 28,
    CONTAINER_RESEARCH_PRACTICE = 29,
    CONTAINER_ALCHEMY = 30,
    CONTAINER_HIGH_ELF_FORGE = 31,
    CONTAINER_DARK_ELF_FORGE = 32,
    CONTAINER_OGRE_FORGE = 33,
    CONTAINER_DWARF_FORGE = 34,
    CONTAINER_GNOME_FORGE = 35,
    CONTAINER_BARBARIAN_FORGE = 36,
    CONTAINER_IKSAR_FORGE = 37,
    CONTAINER_HUMAN_FORGE_1 = 38,
    CONTAINER_HUMAN_FORGE_2 = 39,
    CONTAINER_HALFLING_TAILORING_1 = 40,
    CONTAINER_HALFLING_TAILORING_2 = 41,
    CONTAINER_ERUDITE_TAILORING = 42,
    CONTAINER_WOOD_ELF_TAILORING = 43,
    CONTAINER_WOOD_ELF_FLETCHING = 44,
    CONTAINER_IKSAR_POTTERY = 45,
    CONTAINER_FISHING = 46,
    CONTAINER_TROLL_FORGE = 47,
    CONTAINER_WOOD_ELF_FORGE = 48,
    CONTAINER_HALFLING_FORGE = 49,
    CONTAINER_ERUDITE_FORGE = 50,
    CONTAINER_MERCHANT = 51,
    CONTAINER_FROGLOK_FORGE = 52,
    CONTAINER_AUGMENTER = 53,
    CONTAINER_CHURN = 54,
    CONTAINER_TRANSFORMATION_MOLD = 55,
    CONTAINER_DETRANSFORMATION_MOLD = 56,
    CONTAINER_UNATTUNER = 57,
    CONTAINER_TRADESKILL_BAG = 58,
    CONTAINER_COLLECTIBLE_BAG = 59,
    CONTAINER_NO_DEPOSIT = 60
};

// Check if a container type is a tradeskill container (not a regular bag)
// Tradeskill containers start at type 10 (tinkering) and go up
// Regular bag types are 0-9
inline bool isTradeskillContainerType(uint8_t containerType) {
    // Regular bags are types 0-9
    // Tradeskill containers start at 10 (tinkering)
    // But some special types like quest(13), merchant(51), etc. are not tradeskill
    switch (containerType) {
        case CONTAINER_TINKERING:
        case CONTAINER_LEXICON:
        case CONTAINER_POISON_MAKING:
        case CONTAINER_MIXING_BOWL:
        case CONTAINER_BAKING:
        case CONTAINER_TAILORING:
        case CONTAINER_BLACKSMITHING:
        case CONTAINER_FLETCHING:
        case CONTAINER_BREWING:
        case CONTAINER_JEWELRY_MAKING:
        case CONTAINER_POTTERY:
        case CONTAINER_KILN:
        case CONTAINER_KEY_MAKER:
        case CONTAINER_RESEARCH_WIZ:
        case CONTAINER_RESEARCH_MAG:
        case CONTAINER_RESEARCH_NEC:
        case CONTAINER_RESEARCH_ENC:
        case CONTAINER_RESEARCH_PRACTICE:
        case CONTAINER_ALCHEMY:
        case CONTAINER_HIGH_ELF_FORGE:
        case CONTAINER_DARK_ELF_FORGE:
        case CONTAINER_OGRE_FORGE:
        case CONTAINER_DWARF_FORGE:
        case CONTAINER_GNOME_FORGE:
        case CONTAINER_BARBARIAN_FORGE:
        case CONTAINER_IKSAR_FORGE:
        case CONTAINER_HUMAN_FORGE_1:
        case CONTAINER_HUMAN_FORGE_2:
        case CONTAINER_HALFLING_TAILORING_1:
        case CONTAINER_HALFLING_TAILORING_2:
        case CONTAINER_ERUDITE_TAILORING:
        case CONTAINER_WOOD_ELF_TAILORING:
        case CONTAINER_WOOD_ELF_FLETCHING:
        case CONTAINER_IKSAR_POTTERY:
        case CONTAINER_FISHING:
        case CONTAINER_TROLL_FORGE:
        case CONTAINER_WOOD_ELF_FORGE:
        case CONTAINER_HALFLING_FORGE:
        case CONTAINER_ERUDITE_FORGE:
        case CONTAINER_FROGLOK_FORGE:
        case CONTAINER_CHURN:
            return true;
        default:
            return false;
    }
}

// Check if a slot is a world container item slot (4000-4009)
inline bool isWorldContainerItemSlot(int16_t slotId) {
    return slotId >= WORLD_BEGIN && slotId <= WORLD_END;
}

// Check if a slot is any world container slot (combine slot 1000 or item slots 4000-4009)
inline bool isWorldContainerSlot(int16_t slotId) {
    return slotId == SLOT_TRADESKILL_EXPERIMENT_COMBINE || isWorldContainerItemSlot(slotId);
}

// Get the tradeskill name for a container type
inline const char* getTradeskillContainerName(uint8_t containerType) {
    switch (containerType) {
        case CONTAINER_SMALL_BAG: return "Small Bag";
        case CONTAINER_LARGE_BAG: return "Large Bag";
        case CONTAINER_QUIVER: return "Quiver";
        case CONTAINER_BELT_POUCH: return "Belt Pouch";
        case CONTAINER_WRIST_POUCH: return "Wrist Pouch";
        case CONTAINER_BACKPACK: return "Backpack";
        case CONTAINER_SMALL_CHEST: return "Small Chest";
        case CONTAINER_LARGE_CHEST: return "Large Chest";
        case CONTAINER_BANDOLIER: return "Bandolier";
        case CONTAINER_MEDICINE: return "Medicine Bag";
        case CONTAINER_TINKERING: return "Tinkering";
        case CONTAINER_LEXICON: return "Lexicon";
        case CONTAINER_POISON_MAKING: return "Mortar and Pestle";
        case CONTAINER_QUEST: return "Quest Container";
        case CONTAINER_MIXING_BOWL: return "Mixing Bowl";
        case CONTAINER_BAKING: return "Oven";
        case CONTAINER_TAILORING: return "Loom";
        case CONTAINER_BLACKSMITHING: return "Forge";
        case CONTAINER_FLETCHING: return "Fletching Kit";
        case CONTAINER_BREWING: return "Brew Barrel";
        case CONTAINER_JEWELRY_MAKING: return "Jeweler's Kit";
        case CONTAINER_POTTERY: return "Pottery Wheel";
        case CONTAINER_KILN: return "Kiln";
        case CONTAINER_KEY_MAKER: return "Key Maker";
        case CONTAINER_RESEARCH_WIZ: return "Wizard Lexicon";
        case CONTAINER_RESEARCH_MAG: return "Mage Lexicon";
        case CONTAINER_RESEARCH_NEC: return "Necromancer Lexicon";
        case CONTAINER_RESEARCH_ENC: return "Enchanter Lexicon";
        case CONTAINER_RESEARCH_PRACTICE: return "Practice Lexicon";
        case CONTAINER_ALCHEMY: return "Alchemy Table";
        case CONTAINER_HIGH_ELF_FORGE: return "High Elf Forge";
        case CONTAINER_DARK_ELF_FORGE: return "Dark Elf Forge";
        case CONTAINER_OGRE_FORGE: return "Ogre Forge";
        case CONTAINER_DWARF_FORGE: return "Dwarf Forge";
        case CONTAINER_GNOME_FORGE: return "Gnome Forge";
        case CONTAINER_BARBARIAN_FORGE: return "Barbarian Forge";
        case CONTAINER_IKSAR_FORGE: return "Iksar Forge";
        case CONTAINER_HUMAN_FORGE_1: return "Human Forge";
        case CONTAINER_HUMAN_FORGE_2: return "Human Forge";
        case CONTAINER_HALFLING_TAILORING_1: return "Halfling Loom";
        case CONTAINER_HALFLING_TAILORING_2: return "Halfling Loom";
        case CONTAINER_ERUDITE_TAILORING: return "Erudite Loom";
        case CONTAINER_WOOD_ELF_TAILORING: return "Wood Elf Loom";
        case CONTAINER_WOOD_ELF_FLETCHING: return "Wood Elf Fletching Kit";
        case CONTAINER_IKSAR_POTTERY: return "Iksar Pottery Wheel";
        case CONTAINER_FISHING: return "Tackle Box";
        case CONTAINER_TROLL_FORGE: return "Troll Forge";
        case CONTAINER_WOOD_ELF_FORGE: return "Wood Elf Forge";
        case CONTAINER_HALFLING_FORGE: return "Halfling Forge";
        case CONTAINER_ERUDITE_FORGE: return "Erudite Forge";
        case CONTAINER_MERCHANT: return "Merchant";
        case CONTAINER_FROGLOK_FORGE: return "Froglok Forge";
        case CONTAINER_AUGMENTER: return "Augmenter";
        case CONTAINER_CHURN: return "Churn";
        case CONTAINER_TRANSFORMATION_MOLD: return "Transformation Mold";
        case CONTAINER_DETRANSFORMATION_MOLD: return "Detransformation Mold";
        case CONTAINER_UNATTUNER: return "Unattuner";
        case CONTAINER_TRADESKILL_BAG: return "Tradeskill Bag";
        case CONTAINER_COLLECTIBLE_BAG: return "Collectible Bag";
        case CONTAINER_NO_DEPOSIT: return "No Deposit";
        default: return "Unknown Container";
    }
}

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
