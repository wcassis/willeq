#pragma once

#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
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

namespace eqt { namespace bridge { class GameStateBridge; } }

namespace eqt {
namespace inventory {

// Callback for sending item movement to server
using MoveItemCallback = std::function<void(int16_t fromSlot, int16_t toSlot, uint32_t quantity)>;
using DeleteItemCallback = std::function<void(int16_t slot)>;
// Callback when equipment slot changes (for appearance updates)
using EquipmentChangedCallback = std::function<void(int16_t slotId)>;

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

    // Bridge for pushing intents to game thread (D20b1)
    void setBridge(eqt::bridge::GameStateBridge* bridge) { bridge_ = bridge; }

    // Player info for validation
    void setPlayerInfo(uint32_t race, uint32_t classId, uint8_t level);
    uint32_t getPlayerRace() const { return playerRace_; }
    uint32_t getPlayerClass() const { return playerClass_; }
    uint8_t getPlayerLevel() const { return playerLevel_; }

    // NPC trade flag - when true, allows all items (including NO_DROP) in trade slots
    void setNpcTrade(bool isNpcTrade) { isNpcTrade_ = isNpcTrade; }
    bool isNpcTrade() const { return isNpcTrade_; }

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
    void clearTradeSlots();  // Clear items in trade slots (3000-3007) and their bag ranges (3031-3110); T259
    void clearBankSlots();   // Clear items in bank slots (2000-2190, 2500-2550)
    void clearWorldContainerSlots();  // Clear items in world container slots (4000-4009)

    // T282 -- whether the SERVER currently holds a world tradeskill object
    // for us (EverQuest::m_active_tradeskill_object_id != 0). Moves into or
    // out of 4000-4009 are only legal while it does; see
    // canPlaceInWorldContainer() for what eqemu does otherwise.
    void setWorldContainerOpen(bool open);
    bool isWorldContainerOpen() const { return worldContainerOpen_; }

    // Cursor operations
    bool pickupItem(int16_t slotId);
    bool pickupPartialStack(int16_t slotId, int32_t quantity);  // Pick up specific quantity from stack
    bool placeItem(int16_t targetSlot);
    bool placeOnMatchingStack(int16_t targetSlot);  // Auto-stack cursor item onto matching stack
    bool hasCursorItem() const;
    const ItemInstance* getCursorItem() const;
    int16_t getCursorSourceSlot() const { return cursorSourceSlot_; }
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
    // T276: on a successful scribe the server consumes the scroll from the
    // CURSOR (eqemu Client::OPMemorizeSpell -> DeleteItemInInventory(
    // slotCursor, 1, true)) and confirms with OP_DeleteItem --
    // processDeleteItemResponse() trusts client-side deletion and does
    // nothing, so without this the client would keep showing the spent
    // scroll on the cursor and the NEXT /scribe would be refused for a full
    // cursor. Drops the cursor front locally; sends NOTHING, because the
    // server has already deleted it. Returns false if the cursor is empty.
    bool consumeScribedScroll();

    // B442b (#151 rows 33, 35): the cursor front leaves the client by a
    // packet the CALLER sends -- OP_GroundSpawn (a drop) or OP_MoveItem
    // 30 -> -1 (a destroy, the official wire; not OP_DeleteItem, which is
    // what destroyCursorItem() sends). The same local bookkeeping as
    // consumeScribedScroll(); pushes NOTHING. False if the cursor is empty.
    bool discardCursorFront();

    // Swap items between slots
    bool swapItems(int16_t fromSlot, int16_t toSlot);

    // Validation
    bool canPlaceItemInSlot(const ItemInstance* item, int16_t targetSlot) const;
    bool canEquipItem(const ItemInstance* item, int16_t equipSlot) const;
    bool canPlaceInBag(const ItemInstance* item, int16_t bagSlot) const;
    bool canPlaceInWorldContainer(const ItemInstance* item, int16_t slot) const;
    bool meetsRestrictions(const ItemInstance* item) const;
    bool hasLoreConflict(const ItemInstance* item, int16_t excludeSlot = SLOT_INVALID) const;

    // T141 -- equip orchestration, extracted from eq_commands.cpp's
    // RegisterEquipmentCommands() handler (see refactor/T141_equip_extraction.md).
    // 250 lines of inventory manipulation had accumulated in a command
    // handler despite InventoryManager already owning the legality question
    // (canEquipItem() above) and, since D20b1, its own bridge_ pointer for
    // pushing the resulting intents -- moveItem() below is a natural
    // extension of the SAME bridge-push pattern pickupItem()/placeItem()
    // already use, not new architecture.

    // Result of a destination-resolution search (name-form /equip and
    // /equip list): which equipment slot an item would land in, and
    // whether every candidate slot for a paired slot (ears/wrists/fingers)
    // is already occupied (so the caller can say "would replace").
    // SLOT_INVALID means the item fits no equipment slot at all.
    struct EquipDestinationResult {
        int16_t slot = SLOT_INVALID;
        bool allCandidatesOccupied = false;
    };

    // Result of a moveItem() orchestration: on failure, `message` is a
    // caller-actionable reason (never eqemu's opaque "The selected slot
    // was invalid"); on success it is the same "Moving item: X -> Y"
    // confirmation text the pre-extraction handler printed, so
    // EverQuest::RequestEquip() can pass it straight to
    // AddChatSystemMessage() without re-deriving it.
    struct MoveItemOutcome {
        bool success = false;
        std::string message;
    };

    // T77 -- pre-flight destination legality, reused by both /equip forms
    // (moveItem() below and the name-resolution path). Only meaningful
    // when toSlot is an equipment slot: canEquipItem()'s whole domain is
    // "does this item belong in this equip slot"; moving into
    // general/bag/bank/cursor has no equip-legality question and always
    // passes here (the server remains the sole arbiter of everything
    // else -- this is pre-flight to avoid sending a doomed packet, not a
    // claim to know better than the server). Reuses
    // ItemInstance::canEquipInSlot() and this->canEquipItem() -- no new
    // legality logic. Returns an empty string on success, else a reason
    // naming the item and the slot.
    //
    // T236 -- moveItem() also applies it in REVERSE for a direct
    // slot<->slot swap: the item displaced out of toSlot must be legal for
    // fromSlot (eqemu's InventoryProfile::SwapItem checks both directions;
    // only an equipment-slot source can fail on the displaced side).
    std::string checkEquipDestination(const ItemInstance* item, int16_t toSlot) const;

    // T77 -- resolves the destination equipment slot from the item's own
    // `slots` bitmask via ItemInstance::canEquipInSlot(). Prefers an empty
    // candidate so the name form does not silently clobber an
    // already-worn item; if every candidate is occupied, falls back to
    // the first candidate and flags allCandidatesOccupied.
    EquipDestinationResult resolveEquipDestination(const ItemInstance* item) const;

    // T77 -- name resolution for /equip's item-name form. Case-insensitive
    // substring match against ItemInstance::name, searching only
    // equipment + general (incl. bag contents) + cursor -- deliberately
    // NOT bank/shared-bank/trade: command-layer convenience for "equip
    // something you're carrying", not a general inventory search.
    std::vector<std::pair<int16_t, const ItemInstance*>>
    findItemsByNameSubstring(const std::string& needle) const;

    // T71/T77 -- shared execution for /equip's explicit
    // `<fromSlot> <toSlot>` form (and the resolved destination of the
    // name form): validates slot recognition (this->isValidSlot(), no
    // second copy of that predicate), rejects a no-op, pre-flights via
    // checkEquipDestination(), then executes the T71 branch logic
    // (cursor<->slot via pickupItem()/placeItem(), direct slot<->slot
    // swap, or cursor<->cursor parity fallback) and pushes MoveItemIntent/
    // EquipmentChangedIntent via bridge_ for the branch that doesn't
    // already get them from pickupItem()/placeItem(). No legality
    // judgment beyond checkEquipDestination() -- the server remains the
    // sole arbiter of everything else.
    MoveItemOutcome moveItem(int16_t fromSlot, int16_t toSlot);

    // Bag utilities
    bool isBag(int16_t slotId) const;
    int getBagSize(int16_t slotId) const;
    std::vector<int16_t> getBagContentsSlots(int16_t generalSlot) const;
    bool isBagEmpty(int16_t generalSlot) const;
    // T252 -- bag contents live FLAT in items_, keyed by the parent slot's
    // range (general 22-29 -> 251-330, cursor 30 -> 331-340, T255: bank
    // 2000-2015 -> 2031-2190, shared bank 2500-2501 -> 2531-2550; eqemu's own
    // CalcSlotId numbering). eqemu's SwapItem (common/inventory_profile.cpp
    // :392-393) swaps two ItemInstances that each CARRY their contents, so
    // whenever a bag moves, its contents range moves with it. Both parent
    // slots may be a general, bank, shared-bank slot or CURSOR_SLOT.
    //   relocateBagContents: fromParent's range -> toParent's range (toParent's
    //     range is expected empty; the bag moved to an empty slot / cursor).
    //   swapBagContents: the two ranges trade places without either
    //     overwriting the other (bag-on-bag swap, either side may be empty
    //     or not a bag at all -- an empty range just swaps with nothing).
    void relocateBagContents(int16_t fromParentSlot, int16_t toParentSlot);
    void swapBagContents(int16_t parentSlotA, int16_t parentSlotB);

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
    void setEquipmentChangedCallback(EquipmentChangedCallback callback) { equipmentChangedCallback_ = callback; }

    // Packet processing
    void processCharInventory(const EQ::Net::Packet& packet);
    // Returns the slot the parsed item landed in, or SLOT_INVALID if the
    // packet couldn't be parsed. Callers (the generic ItemPacket arm of
    // DispatchZonePacket, eq_zone_dispatch.cpp,
    // fallback, eq_merchant.cpp's loot-to-inventory path) use this to publish
    // InventorySlotChanged (T42) -- this is the single choke point for both
    // vendor-buy and bank-window item placement.
    int16_t processItemPacket(const EQ::Net::Packet& packet);
    void processMoveItemResponse(const EQ::Net::Packet& packet);
    void processDeleteItemResponse(const EQ::Net::Packet& packet);

    // Destroy item (sends delete to server if callback set)
    bool destroyItem(int16_t slotId);
    bool destroyCursorItem();

    // Debug
    void dumpInventory() const;

    // Change counter - incremented on every item add/remove/move
    // Used by UI to detect when inventory content has changed
    uint32_t getChangeCounter() const { return changeCounter_; }

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
    // T252 helpers for the bag-content range moves above: take every item
    // out of a parent slot's BAG_SLOT_COUNT-slot range (keeping its index),
    // and put such a set back under another parent.
    // T255: a parent may also be a bank / shared-bank slot (isBagParentSlot).
    // T259: or a trade-window slot (3000-3007 -> 3031-3110).
    using TakenBagContents = std::vector<std::pair<int16_t, std::unique_ptr<ItemInstance>>>;
    static bool isBagParentSlot(int16_t slotId);
    TakenBagContents takeBagContents(int16_t parentSlot);
    void putBagContents(int16_t parentSlot, TakenBagContents& contents);

    // T255 -- contents of bags QUEUED BEHIND the cursor front. The server
    // keeps every queued bag's contents inside that bag's own ItemInstance
    // and serialises each cursor item at slot 33, so two queued bags both
    // address the wire's cursor bag range 331-340; only the FRONT bag's
    // contents may occupy that range client-side. Each other queued bag's
    // contents are held here, keyed by the queued ItemInstance (stable:
    // the deque owns it by unique_ptr), and promoted into the cursor range
    // when that bag becomes the front. Every path that pops the front
    // calls promoteCursorFrontContents(); every path that discards the
    // front calls dropCursorFrontContents() first.
    std::map<const ItemInstance*, TakenBagContents> queuedCursorBagContents_;
    void promoteCursorFrontContents();
    void dropCursorFrontContents();
    // Shared tail of processCharInventory()/processItemPacket(): set the
    // item, then route its parsed sub-items either into the parent's range
    // or (a bag queued behind the cursor front) into queuedCursorBagContents_.
    // Returns how many sub-items were stored.
    int ingestParsedItem(int16_t slotId, std::unique_ptr<ItemInstance> item,
                         std::map<int16_t, std::unique_ptr<ItemInstance>>& bagContents);

    std::deque<std::unique_ptr<ItemInstance>> cursorQueue_;
    int16_t cursorSourceSlot_ = SLOT_INVALID;  // Slot the CURRENT cursor front came from (T250: updated on swap)

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

    // NPC trade state - when true, NO_DROP restrictions are bypassed for trade slots
    bool isNpcTrade_ = false;

    // T282 -- see setWorldContainerOpen().
    bool worldContainerOpen_ = false;

    // Change counter - bumped on every mutation
    uint32_t changeCounter_ = 0;

    // Bridge for pushing intents (D20b1)
    eqt::bridge::GameStateBridge* bridge_ = nullptr;

    // Callbacks (legacy — being replaced by bridge intents)
    MoveItemCallback moveItemCallback_;
    DeleteItemCallback deleteItemCallback_;
    EquipmentChangedCallback equipmentChangedCallback_;

    // Internal helpers
    bool isValidSlot(int16_t slotId) const;
    const ItemInstance* getBagAtSlot(int16_t bagSlot) const;       // Get container for general/cursor bag slots
    const ItemInstance* getContainerAtSlot(int16_t bagSlot) const; // Get container for any bag slot (general, bank, trade)
};

} // namespace inventory
} // namespace eqt
