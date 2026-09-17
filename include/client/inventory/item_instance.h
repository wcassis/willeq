#pragma once

#include "client/inventory/inventory_constants.h"
#include <string>
#include <array>
#include <cstdint>

namespace eqt {
namespace inventory {

// Augment slot information
struct AugmentSlot {
    uint32_t type = 0;           // Augment type bitmask this slot accepts
    uint32_t augmentId = 0;      // ID of augment in this slot (0 = empty)
    bool visible = true;         // Whether this slot is visible/usable
};

// Effect information for click/worn/focus/proc effects
struct ItemEffect {
    int32_t effectId = 0;        // Spell ID
    int32_t type = 0;            // Effect type
    int32_t level = 0;           // Required level for effect
    int32_t charges = -1;        // Number of charges (-1 = unlimited)
    int32_t castTime = 0;        // Cast time in ms
    int32_t recastDelay = 0;     // Recast delay in ms
    int32_t recastType = 0;      // Recast timer type
    std::string name;            // Effect/spell name
    std::string description;     // Effect description
};

// Complete item instance data
struct ItemInstance {
    // Identity
    uint32_t itemId = 0;
    std::string name;
    std::string lore;
    uint32_t loreGroup = 0;

    // Current slot (where this item is located)
    int16_t currentSlot = inventory::SLOT_INVALID;

    // Stack info
    int32_t charges = 0;
    int32_t maxCharges = 0;
    int32_t quantity = 1;
    int32_t stackSize = 1;
    bool stackable = false;

    // Flags
    uint32_t flags = 0;  // ItemFlags bitmask
    bool magic = false;
    bool loreItem = false;
    bool noDrop = false;
    bool noRent = false;
    bool artifact = false;
    bool summoned = false;
    bool noDestroy = false;
    bool questItem = false;
    // T425 Leg B (board B375): expendable is DERIVED, not a wire field, and
    // TitaniumItemParser now derives it. eqemu's own definition is
    // ItemInstance::IsExpendable() (/mnt/projects/akk-stack/code/common/item_instance.h):
    //     Click.Type == item::ItemEffectExpendable || ItemType == ItemTypePotion
    // and BOTH inputs are on Titanium's wire (Click.Type is field 135,
    // ItemType is field 0). It drives eqstr 3001 "EXPENDABLE     Charges: %1",
    // which BuildItemDisplayText() has printed since T421 and which nothing
    // could reach until now.
    bool expendable = false;

    // Restrictions
    uint32_t classes = 0;        // Class bitmask (0 = all classes)
    uint32_t races = 0;          // Race bitmask (0 = all races)
    uint32_t deity = 0;          // Deity bitmask (0 = all deities)
    uint8_t reqLevel = 0;        // Required level
    uint8_t recLevel = 0;        // Recommended level

    // Equipment info
    uint32_t slots = 0;          // Equippable slot bitmask
    uint8_t size = SIZE_MEDIUM;  // Item size (tiny/small/medium/large/giant)
    float weight = 0.0f;         // Item weight

    // Container info (for bags)
    uint8_t bagSlots = 0;        // Number of slots (0 if not a bag, 2-10 for bags)
    uint8_t bagSize = 0;         // Max size item it can hold (SIZE_* constant)
    uint8_t bagType = 0;         // Container type (tradeskill containers have specific types)
    uint8_t bagWR = 0;           // Weight reduction percentage

    // Display
    uint32_t icon = 0;           // Icon ID for display
    uint32_t color = 0;          // Tint color (0xAARRGGBB)
    uint8_t material = 0;        // Armor material/graphics ID
    // T498 (board B755/B823): the numeric part of the server's items.idfile,
    // Titanium static field 3 ("IT10004" -> 10004). This is a MATERIAL / IT
    // model number, the same unit the server puts in a spawn packet's
    // equipment[] -- Mob::GetEquipmentMaterial() is literally
    // atoi(&item->IDFile[2]) (akk-stack code/zone/mob_appearance.cpp). fable
    // skipped this field for its whole history and computed the local player's
    // weapon appearance from the DATABASE ITEM ID instead, through
    // data/item_models.json, which is a different rule and a stale table
    // (2,128 of its 3,661 rows disagree with the server -- B755). 0 = the
    // server sent no usable IT<n>, which is eqemu's own guard too.
    uint32_t idfileModel = 0;

    // Combat stats
    int32_t ac = 0;              // Armor class
    int32_t hp = 0;              // Hit points
    int32_t mana = 0;            // Mana
    int32_t endurance = 0;       // Endurance
    int32_t damage = 0;          // Weapon damage
    int32_t delay = 0;           // Weapon delay
    int32_t range = 0;           // Range (for ranged weapons)
    int32_t attack = 0;          // Attack bonus
    int32_t accuracy = 0;        // Accuracy bonus

    // Attributes
    int32_t str = 0;
    int32_t sta = 0;
    int32_t agi = 0;
    int32_t dex = 0;
    int32_t wis = 0;
    int32_t int_ = 0;
    int32_t cha = 0;

    // Resistances
    int32_t magicResist = 0;
    int32_t fireResist = 0;
    int32_t coldResist = 0;
    int32_t diseaseResist = 0;
    int32_t poisonResist = 0;

    // Regen and special
    int32_t hpRegen = 0;
    int32_t manaRegen = 0;
    int32_t enduranceRegen = 0;
    int32_t haste = 0;
    int32_t damageShield = 0;
    int32_t spellShield = 0;
    int32_t strikethrough = 0;
    int32_t stunResist = 0;
    int32_t avoidance = 0;
    int32_t shielding = 0;
    int32_t dotShield = 0;

    // T425 Leg B (board B375): the seven heroic stats are GONE. They are not
    // on Titanium's wire -- eqemu's titanium.cpp SerializeItem() never emits
    // an ItemData::Heroic* field (grep: zero hits in that function) -- and
    // eqstr_us.txt has no heroic-stat format string either, because heroic
    // stats postdate this client. Declared, default-initialised and assigned
    // by nothing: dead struct members, not a missing display.

    // Augments
    std::array<AugmentSlot, MAX_AUGMENT_SLOTS> augmentSlots;

    // Effects
    ItemEffect clickEffect;
    ItemEffect wornEffect;
    ItemEffect focusEffect;
    ItemEffect procEffect;
    ItemEffect scrollEffect;
    // T425 Leg B (board B375): `bardEffect` is GONE. Titanium's wire carries
    // FIVE ItemEffect blocks -- Click, Proc, Worn, Focus, Scroll (fields
    // 134-158) -- and no sixth. What it does carry is the scalar pair
    // BardType/BardValue at fields 41-42, which is a skill-modifier pair and
    // not an effect at all; eqstr_us.txt has no instrument-modifier item line
    // for it either, so there is nothing to parse it INTO and nothing to draw.

    // Vendor price
    int32_t price = 0;

    // Item type (weapon type, armor type, etc.)
    uint8_t itemType = 0;      // ItemClass (0=common, 1=container, etc.)
    uint8_t skillType = 0;     // Weapon skill type (for damage/delay display)

    // Book info (Titanium item fields 100/101/102, eqemu titanium.cpp
    // SerializeItem: Book | BookType | Filename). T273 corrected the
    // mapping -- field 100 is the IS-A-BOOK flag, not the book type, and
    // field 102 is the server-side text file NAME (what OP_ReadBook asks
    // for), not the text itself.
    uint8_t bookFlag = 0;          // eqemu ItemData::Book: 0=not a book, 1=book
    uint32_t bookType = 0;         // eqemu ItemData::BookType: 0=scroll, 1=book, 2=item info
    std::string bookFilename;      // eqemu ItemData::Filename -- the `books`.`name` key

    // Helper methods

    bool isContainer() const { return bagSlots > 0; }

    bool hasFlag(ItemFlags flag) const { return (flags & flag) != 0; }

    bool canEquipInSlot(int16_t slotId) const {
        if (slotId < EQUIPMENT_BEGIN || slotId > EQUIPMENT_END) {
            return false;
        }
        // Map slot ID to slot bitmask
        uint32_t slotBit = (1 << slotId);
        // Handle paired slots (ears, wrists, fingers)
        if (slotId == SLOT_EAR1 || slotId == SLOT_EAR2) {
            return (slots & EQUIPSLOT_EAR) != 0;
        }
        if (slotId == SLOT_WRIST1 || slotId == SLOT_WRIST2) {
            return (slots & EQUIPSLOT_WRIST) != 0;
        }
        if (slotId == SLOT_RING1 || slotId == SLOT_RING2) {
            return (slots & EQUIPSLOT_FINGER) != 0;
        }
        return (slots & slotBit) != 0;
    }

    bool canBeUsedByClass(uint32_t playerClass) const {
        if (classes == 0 || classes == CLASS_ALL) {
            return true;  // All classes
        }
        // EQ class bitmask: class 1 = bit 0, class 7 (Monk) = bit 6, etc.
        return (classes & (1 << (playerClass - 1))) != 0;
    }

    bool canBeUsedByRace(uint32_t playerRace) const {
        if (races == 0 || races == RACE_ALL) {
            return true;  // All races
        }
        // EQ race bitmask: race 1 = bit 0, race 2 = bit 1, etc.
        return (races & (1 << (playerRace - 1))) != 0;
    }

    bool canBeUsedAtLevel(uint8_t playerLevel) const {
        return playerLevel >= reqLevel;
    }

    bool canFitInContainer(uint8_t containerSize) const {
        // In EQ, containerSize=0 means NO size restriction (any size fits)
        // containerSize>0 means only items of that size or smaller fit
        if (containerSize == 0) {
            return true;  // No restriction
        }
        return size <= containerSize;
    }

    // Check if item is usable by player with given restrictions
    bool isUsableBy(uint32_t playerClass, uint32_t playerRace, uint8_t playerLevel) const {
        return canBeUsedByClass(playerClass) &&
               canBeUsedByRace(playerRace) &&
               canBeUsedAtLevel(playerLevel);
    }

    // Get string representation of item flags
    std::string getFlagsString() const {
        std::string result;
        if (magic) {
            result += "Magic";
        }
        if (noDrop) {
            if (!result.empty()) result += ", ";
            result += "No Trade";
        }
        if (loreItem) {
            if (!result.empty()) result += ", ";
            result += "Lore";
        }
        if (noRent) {
            if (!result.empty()) result += ", ";
            result += "No Rent";
        }
        if (artifact) {
            if (!result.empty()) result += ", ";
            result += "Artifact";
        }
        if (summoned) {
            if (!result.empty()) result += ", ";
            result += "Summoned";
        }
        if (questItem) {
            if (!result.empty()) result += ", ";
            result += "Quest";
        }
        return result;
    }

    // Check if this item has any stats worth displaying
    bool hasStats() const {
        return ac != 0 || hp != 0 || mana != 0 || endurance != 0 ||
               str != 0 || sta != 0 || agi != 0 || dex != 0 ||
               wis != 0 || int_ != 0 || cha != 0;
    }

    // Check if this item has any resistances worth displaying
    bool hasResists() const {
        return magicResist != 0 || fireResist != 0 || coldResist != 0 ||
               diseaseResist != 0 || poisonResist != 0;
    }

    // Check if this item has any effects
    bool hasEffects() const {
        return clickEffect.effectId != 0 || wornEffect.effectId != 0 ||
               focusEffect.effectId != 0 || procEffect.effectId != 0;
    }

    // Count visible augment slots
    int countAugmentSlots() const {
        int count = 0;
        for (const auto& slot : augmentSlots) {
            if (slot.visible && slot.type != 0) {
                count++;
            }
        }
        return count;
    }

    // Check if this item is a spell scroll that can be scribed
    // EQ spell scrolls have "Spell:" prefix in name and a scroll effect with spell ID
    bool isSpellScroll() const {
        // Check for "Spell:" prefix in name (case insensitive)
        if (name.size() >= 6) {
            std::string prefix = name.substr(0, 6);
            // Case-insensitive compare
            if ((prefix[0] == 'S' || prefix[0] == 's') &&
                (prefix[1] == 'P' || prefix[1] == 'p') &&
                (prefix[2] == 'E' || prefix[2] == 'e') &&
                (prefix[3] == 'L' || prefix[3] == 'l') &&
                (prefix[4] == 'L' || prefix[4] == 'l') &&
                prefix[5] == ':') {
                return true;
            }
        }
        // Also check if scroll effect has a valid spell ID
        return scrollEffect.effectId > 0;
    }

    // Get the spell ID this scroll teaches
    // Returns 0 if not a spell scroll or spell ID cannot be determined
    uint32_t getScrollSpellId() const {
        if (scrollEffect.effectId > 0) {
            return static_cast<uint32_t>(scrollEffect.effectId);
        }
        return 0;
    }
};

} // namespace inventory
} // namespace eqt
