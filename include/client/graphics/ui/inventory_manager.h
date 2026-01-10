#pragma once

#include "inventory_constants.h"
#include "item_instance.h"
#include <map>
#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <string_view>

namespace EQ {
namespace Net {
class Packet;
}
}

namespace eqt {
namespace inventory {

// Callback for sending item movement to server
using MoveItemCallback = std::function<void(int16_t fromSlot, int16_t toSlot, uint32_t quantity)>;
using DeleteItemCallback = std::function<void(int16_t slot)>;

// Aggregate stats from equipped items
struct EquipmentStats {
    int32_t ac = 0;
    int32_t atk = 0;
    int32_t hp = 0;
    int32_t mana = 0;
    int32_t endurance = 0;

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
    int32_t haste = 0;

    // Total weight
    float weight = 0.0f;
};

// Titanium item serialization parser
class TitaniumItemParser {
public:
    // Parse a single serialized item string into an ItemInstance
    // Returns nullptr if parsing fails
    // Also populates subItems map with bag contents (keyed by bag slot ID)
    static std::unique_ptr<ItemInstance> parseItem(const char* data, size_t length, int16_t& slotId,
                                                    std::map<int16_t, std::unique_ptr<ItemInstance>>* subItems = nullptr);

    // Parse a single serialized item string (null-terminated)
    static std::unique_ptr<ItemInstance> parseItem(const std::string& data, int16_t& slotId,
                                                    std::map<int16_t, std::unique_ptr<ItemInstance>>* subItems = nullptr);

private:
    // Helper to split pipe-delimited string
    static std::vector<std::string_view> splitByPipe(std::string_view str);

    // Parse instance data (before the quoted static data)
    static bool parseInstanceData(const std::vector<std::string_view>& fields,
                                  ItemInstance& item, int16_t& slotId);

    // Parse static item data (the quoted section)
    static bool parseStaticData(std::string_view quotedData, ItemInstance& item);

    // Parse sub-items (bag contents) after the closing quote
    static void parseSubItems(std::string_view data, size_t afterQuotePos, int16_t parentSlot,
                              std::map<int16_t, std::unique_ptr<ItemInstance>>& subItems);

    // Helper to convert string to int safely
    static int32_t toInt(std::string_view str, int32_t defaultVal = 0);
    static float toFloat(std::string_view str, float defaultVal = 0.0f);
};

class InventoryManager {
public:
    InventoryManager();
    ~InventoryManager();

    // Player info for validation
    void setPlayerInfo(uint32_t race, uint32_t classId, uint8_t level);
    uint32_t getPlayerRace() const { return playerRace_; }
    uint32_t getPlayerClass() const { return playerClass_; }
    uint8_t getPlayerLevel() const { return playerLevel_; }

    // Item access by slot
    const ItemInstance* getItem(int16_t slotId) const;
    ItemInstance* getItemMutable(int16_t slotId);
    bool hasItem(int16_t slotId) const;

    // Item access by ID (for item link tooltips)
    const ItemInstance* getItemById(uint32_t itemId) const;

    // Find slot containing item by item ID (for hotbar item activation)
    // Searches equipment slots first, then general/bag slots
    // Returns SLOT_INVALID if not found
    int16_t findItemSlotByItemId(uint32_t itemId) const;

    // Stack quantity updates (for vendor purchases that stack onto existing items)
    bool addQuantityToExistingStack(uint32_t itemId, int32_t quantity);

    // Add/remove items (called from packet handlers)
    void setItem(int16_t slotId, const ItemInstance& item);
    void setItem(int16_t slotId, std::unique_ptr<ItemInstance> item);
    void removeItem(int16_t slotId);
    void clearAll();
    void clearTradeSlots();  // Clear items in trade slots (3000-3007)
    void clearBankSlots();   // Clear items in bank slots (2000-2190, 2500-2550)
    void clearWorldContainerSlots();  // Clear items in world container slots (4000-4009)

    // Cursor operations
    bool pickupItem(int16_t slotId);
    bool pickupPartialStack(int16_t slotId, int32_t quantity);  // Pick up specific quantity from stack
    bool placeItem(int16_t targetSlot);
    bool placeOnMatchingStack(int16_t targetSlot);  // Auto-stack cursor item onto matching stack
    bool hasCursorItem() const;
    const ItemInstance* getCursorItem() const;
    int16_t getCursorSourceSlot() const { return cursorSourceSlot_; }
    void returnCursorItem();  // Return cursor item to original slot
    void popCursorItem();     // Pop front item from cursor queue and notify server
    size_t getCursorQueueSize() const;  // Get number of items in cursor queue

    // Cursor money operations
    enum class CursorMoneyType { None, Platinum, Gold, Silver, Copper };
    bool hasCursorMoney() const { return cursorMoneyAmount_ > 0 && cursorMoneyType_ != CursorMoneyType::None; }
    CursorMoneyType getCursorMoneyType() const { return cursorMoneyType_; }
    uint32_t getCursorMoneyAmount() const { return cursorMoneyAmount_; }
    void pickupMoney(CursorMoneyType type, uint32_t amount);
    void clearCursorMoney();
    void returnCursorMoney();  // Return cursor money to player's coin

    // Spell scroll scribing cursor state
    // When a player Ctrl+clicks a spell scroll, we track it here until they click a spellbook slot
    bool isHoldingSpellForScribe() const { return scribeSpellId_ != 0; }
    uint32_t getScribeSpellId() const { return scribeSpellId_; }
    int16_t getScribeSourceSlot() const { return scribeSourceSlot_; }
    void setSpellForScribe(uint32_t spellId, int16_t sourceSlot);
    void clearSpellForScribe();

    // Swap items between slots
    bool swapItems(int16_t fromSlot, int16_t toSlot);

    // Validation
    bool canPlaceItemInSlot(const ItemInstance* item, int16_t targetSlot) const;
    bool canEquipItem(const ItemInstance* item, int16_t equipSlot) const;
    bool canPlaceInBag(const ItemInstance* item, int16_t bagSlot) const;
    bool canPlaceInWorldContainer(const ItemInstance* item, int16_t slot) const;
    bool meetsRestrictions(const ItemInstance* item) const;
    bool hasLoreConflict(const ItemInstance* item, int16_t excludeSlot = SLOT_INVALID) const;

    // Bag utilities
    bool isBag(int16_t slotId) const;
    int getBagSize(int16_t slotId) const;
    std::vector<int16_t> getBagContentsSlots(int16_t generalSlot) const;
    bool isBagEmpty(int16_t generalSlot) const;
    void relocateBagContents(int16_t fromGeneralSlot, int16_t toGeneralSlot);

    // Bank utilities
    bool isBankBag(int16_t slotId) const;           // Check if bank/shared bank slot contains a bag
    int getBankBagSize(int16_t slotId) const;       // Get bag capacity for bank container
    std::vector<int16_t> getBankBagContentsSlots(int16_t bankSlot) const;  // Get slots inside bank bag
    bool isBankBagEmpty(int16_t bankSlot) const;    // Check if bank bag is empty
    bool canPlaceInBankSlot(const ItemInstance* item, int16_t targetSlot) const;  // Validate bank placement
    std::vector<std::pair<int16_t, const ItemInstance*>> getBankItems() const;    // Get all bank items
    std::vector<std::pair<int16_t, const ItemInstance*>> getSharedBankItems() const;  // Get shared bank items

    // Get all items in a slot range
    std::vector<std::pair<int16_t, const ItemInstance*>> getEquipmentItems() const;
    std::vector<std::pair<int16_t, const ItemInstance*>> getGeneralItems() const;

    // Get all sellable items (excludes NO_DROP items)
    // Returns items from general slots (22-29) and bag contents (251-330)
    // Each pair is (titanium_slot_id, item_pointer)
    std::vector<std::pair<int16_t, const ItemInstance*>> getSellableItems() const;

    // Calculate aggregate stats from equipped items
    EquipmentStats calculateEquipmentStats() const;

    // Calculate total inventory weight (all items including bags and contents)
    float calculateTotalWeight() const;

    // Server communication callbacks
    void setMoveItemCallback(MoveItemCallback callback) { moveItemCallback_ = callback; }
    void setDeleteItemCallback(DeleteItemCallback callback) { deleteItemCallback_ = callback; }

    // Packet processing
    void processCharInventory(const EQ::Net::Packet& packet);
    void processItemPacket(const EQ::Net::Packet& packet);
    void processMoveItemResponse(const EQ::Net::Packet& packet);
    void processDeleteItemResponse(const EQ::Net::Packet& packet);

    // Destroy item (sends delete to server if callback set)
    bool destroyItem(int16_t slotId);
    bool destroyCursorItem();

    // Debug
    void dumpInventory() const;

private:
    // Item storage - slot ID to item
    std::map<int16_t, std::unique_ptr<ItemInstance>> items_;

    // Item cache by ID (for quick lookup by item ID from chat links)
    // Stores copies of items seen (limited to MAX_ITEM_CACHE_SIZE)
    std::map<uint32_t, std::unique_ptr<ItemInstance>> itemCacheById_;
    static constexpr size_t MAX_ITEM_CACHE_SIZE = 500;

    // Cache an item by its ID
    void cacheItemById(const ItemInstance& item);

    // Cursor queue - multiple items can be on cursor (e.g., zone-in with items on cursor)
    // Front item is the "active" cursor item shown to user
    std::deque<std::unique_ptr<ItemInstance>> cursorQueue_;
    int16_t cursorSourceSlot_ = SLOT_INVALID;  // Source slot of the front cursor item

    // Cursor money - separate from cursor items
    CursorMoneyType cursorMoneyType_ = CursorMoneyType::None;
    uint32_t cursorMoneyAmount_ = 0;

    // Spell scroll scribing state
    uint32_t scribeSpellId_ = 0;       // Spell ID being scribed (0 = not scribing)
    int16_t scribeSourceSlot_ = SLOT_INVALID;  // Inventory slot containing the scroll

    // Player info for validation
    uint32_t playerRace_ = 0;
    uint32_t playerClass_ = 0;
    uint8_t playerLevel_ = 1;

    // Callbacks
    MoveItemCallback moveItemCallback_;
    DeleteItemCallback deleteItemCallback_;

    // Internal helpers
    bool isValidSlot(int16_t slotId) const;
    const ItemInstance* getBagAtSlot(int16_t bagSlot) const;       // Get container for general/cursor bag slots
    const ItemInstance* getContainerAtSlot(int16_t bagSlot) const; // Get container for any bag slot (general, bank, trade)
};

} // namespace inventory
} // namespace eqt
