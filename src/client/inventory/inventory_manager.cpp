#include "client/inventory/inventory_manager.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "common/net/packet.h"
#include "common/packet_structs.h"  // T282: EQT::ITEM_PACKET_WORLD_CONTAINER
#include <iostream>
#include "common/logging.h"
#include <sstream>
#include <charconv>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace eqt {
namespace inventory {

// ============================================================================
// InventoryManager Implementation
// ============================================================================

InventoryManager::InventoryManager() = default;
InventoryManager::~InventoryManager() = default;

void InventoryManager::setPlayerInfo(uint32_t race, uint32_t classId, uint8_t level) {
    playerRace_ = race;
    playerClass_ = classId;
    playerLevel_ = level;
}

const ItemInstance* InventoryManager::getItem(int16_t slotId) const {
    // Handle cursor slot specially
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        return cursorQueue_.empty() ? nullptr : cursorQueue_.front().get();
    }
    auto it = items_.find(slotId);
    if (it != items_.end()) {
        return it->second.get();
    }
    return nullptr;
}

ItemInstance* InventoryManager::getItemMutable(int16_t slotId) {
    // Handle cursor slot specially
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        return cursorQueue_.empty() ? nullptr : cursorQueue_.front().get();
    }
    auto it = items_.find(slotId);
    if (it != items_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool InventoryManager::hasItem(int16_t slotId) const {
    // Handle cursor slot specially
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        return !cursorQueue_.empty();
    }
    return items_.find(slotId) != items_.end();
}

const ItemInstance* InventoryManager::getItemById(uint32_t itemId) const {
    // First check the ID cache
    auto it = itemCacheById_.find(itemId);
    if (it != itemCacheById_.end()) {
        return it->second.get();
    }

    // Fall back to searching inventory slots (slower but covers any edge cases)
    for (const auto& [slotId, item] : items_) {
        if (item && item->itemId == itemId) {
            return item.get();
        }
    }

    // Check cursor queue
    for (const auto& item : cursorQueue_) {
        if (item && item->itemId == itemId) {
            return item.get();
        }
    }

    return nullptr;
}

int16_t InventoryManager::findItemSlotByItemId(uint32_t itemId) const {
    // Search equipment slots first (0-21) as these are typically clickable items
    for (int16_t slot = 0; slot < EQUIPMENT_COUNT; slot++) {
        auto it = items_.find(slot);
        if (it != items_.end() && it->second && it->second->itemId == itemId) {
            return slot;
        }
    }

    // Search general inventory slots (22-29)
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; slot++) {
        auto it = items_.find(slot);
        if (it != items_.end() && it->second && it->second->itemId == itemId) {
            return slot;
        }
    }

    // Search bag contents (251-330)
    for (int16_t slot = GENERAL_BAGS_BEGIN; slot <= GENERAL_BAGS_END; slot++) {
        auto it = items_.find(slot);
        if (it != items_.end() && it->second && it->second->itemId == itemId) {
            return slot;
        }
    }

    return SLOT_INVALID;
}

bool InventoryManager::addQuantityToExistingStack(uint32_t itemId, int32_t quantity) {
    // Search inventory slots for a stackable item with this ID
    for (auto& [slotId, item] : items_) {
        if (item && item->itemId == itemId && item->stackable) {
            int32_t newQuantity = item->quantity + quantity;
            // Clamp to stack size if defined
            if (item->stackSize > 0) {
                newQuantity = std::min(newQuantity, static_cast<int32_t>(item->stackSize));
            }
            item->quantity = newQuantity;
            LOG_DEBUG(MOD_UI, "Updated stack quantity for item {} in slot {}: {} -> {}",
                     item->name, slotId, item->quantity - quantity, item->quantity);
            return true;
        }
    }
    return false;
}

void InventoryManager::cacheItemById(const ItemInstance& item) {
    if (item.itemId == 0) {
        return;  // Don't cache items with no ID
    }

    // Check if already cached
    if (itemCacheById_.find(item.itemId) != itemCacheById_.end()) {
        return;  // Already have this item cached
    }

    // Evict old entries if cache is full
    if (itemCacheById_.size() >= MAX_ITEM_CACHE_SIZE) {
        // Simple eviction: remove the first entry (oldest by map order)
        itemCacheById_.erase(itemCacheById_.begin());
    }

    // Cache a copy of the item
    itemCacheById_[item.itemId] = std::make_unique<ItemInstance>(item);
}

void InventoryManager::setItem(int16_t slotId, const ItemInstance& item) {
    // Cache the item by ID for item link lookups
    cacheItemById(item);

    auto newItem = std::make_unique<ItemInstance>(item);
    newItem->currentSlot = slotId;

    // Special handling for cursor slot - server uses 33, client uses 30
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        newItem->currentSlot = CURSOR_SLOT;  // Normalize to client cursor slot
        cursorQueue_.push_back(std::move(newItem));
        LOG_DEBUG(MOD_UI, "InventoryManager Item added to cursor queue: {} (queue size: {})", cursorQueue_.back()->name, cursorQueue_.size());
        // If this is the first item, set source to CURSOR_SLOT (came from server)
        if (cursorQueue_.size() == 1) {
            cursorSourceSlot_ = CURSOR_SLOT;
        }
    } else {
        items_[slotId] = std::move(newItem);
    }
    ++changeCounter_;
}

void InventoryManager::setItem(int16_t slotId, std::unique_ptr<ItemInstance> item) {
    if (item) {
        // Cache the item by ID for item link lookups (before moving)
        cacheItemById(*item);

        item->currentSlot = slotId;

        // Special handling for cursor slot - server uses 33, client uses 30
        if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
            item->currentSlot = CURSOR_SLOT;  // Normalize to client cursor slot
            cursorQueue_.push_back(std::move(item));
            LOG_DEBUG(MOD_UI, "InventoryManager Item added to cursor queue: {} (queue size: {})", cursorQueue_.back()->name, cursorQueue_.size());
            // If this is the first item, set source to CURSOR_SLOT (came from server)
            if (cursorQueue_.size() == 1) {
                cursorSourceSlot_ = CURSOR_SLOT;
            }
        } else {
            // Check if we're adding to an existing stackable item
            auto existingIt = items_.find(slotId);
            if (existingIt != items_.end() && existingIt->second &&
                existingIt->second->stackable && item->stackable &&
                existingIt->second->itemId == item->itemId && item->itemId != 0) {
                // Same stackable item - add to existing quantity
                int32_t oldQty = existingIt->second->quantity;
                int32_t addQty = item->quantity;
                int32_t newQty = oldQty + addQty;
                // Clamp to stack size if defined
                if (existingIt->second->stackSize > 0) {
                    newQty = std::min(newQty, static_cast<int32_t>(existingIt->second->stackSize));
                }
                existingIt->second->quantity = newQty;
                LOG_DEBUG(MOD_UI, "InventoryManager Stack update: {} in slot {} qty {} + {} = {}",
                         existingIt->second->name, slotId, oldQty, addQty, newQty);
            } else {
                // New item or different item - replace
                items_[slotId] = std::move(item);
            }
        }
        ++changeCounter_;
    }
}

void InventoryManager::removeItem(int16_t slotId) {
    // Handle cursor slot specially
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        if (!cursorQueue_.empty()) {
            dropCursorFrontContents();  // T255: the bag leaves with its contents
            cursorQueue_.pop_front();
            // If queue is now empty, reset source slot
            if (cursorQueue_.empty()) {
                cursorSourceSlot_ = SLOT_INVALID;
            } else {
                // Next item in queue becomes active, it came from server
                cursorSourceSlot_ = CURSOR_SLOT;
                promoteCursorFrontContents();
            }
        }
    } else {
        items_.erase(slotId);
    }
    ++changeCounter_;
}

void InventoryManager::clearAll() {
    items_.clear();
    cursorQueue_.clear();
    queuedCursorBagContents_.clear();
    cursorSourceSlot_ = SLOT_INVALID;
    itemCacheById_.clear();
    ++changeCounter_;
}

void InventoryManager::clearTradeSlots() {
    // Clear items in trade slots (3000-3007) AND their bag-content ranges
    // (3031-3110). T259: the server never sends a client update for the
    // giver's trade slots -- Client::FinishTrade / ResetTrade
    // (zone/trading.cpp) DeleteItemInInventory(trade_slot) them with
    // client_update=false -- so on OP_FinishTrade / OP_CancelTrade the
    // client must drop them itself, contents included. A bag returned by a
    // cancel arrives separately as an ItemPacketTrade at a general slot,
    // never here.
    size_t cleared = 0;
    for (int16_t slot = TRADE_BEGIN; slot <= TRADE_END; slot++) {
        cleared += items_.erase(slot);
    }
    for (int16_t slot = TRADE_BAGS_BEGIN; slot <= TRADE_BAGS_END; slot++) {
        cleared += items_.erase(slot);
    }
    if (cleared > 0) {
        ++changeCounter_;
    }
    LOG_DEBUG(MOD_UI, "InventoryManager Cleared trade slots {}-{} and {}-{} ({} items)",
              TRADE_BEGIN, TRADE_END, TRADE_BAGS_BEGIN, TRADE_BAGS_END, cleared);
}

void InventoryManager::clearBankSlots() {
    // Clear items in main bank slots (2000-2015) and bank bag slots (2031-2190)
    for (int16_t slot = BANK_BEGIN; slot <= BANK_END; slot++) {
        items_.erase(slot);
    }
    for (int16_t slot = BANK_BAGS_BEGIN; slot <= BANK_BAGS_END; slot++) {
        items_.erase(slot);
    }

    // Clear shared bank slots (2500-2501) and shared bank bag slots (2531-2550)
    for (int16_t slot = SHARED_BANK_BEGIN; slot <= SHARED_BANK_END; slot++) {
        items_.erase(slot);
    }
    for (int16_t slot = SHARED_BANK_BAGS_BEGIN; slot <= SHARED_BANK_BAGS_END; slot++) {
        items_.erase(slot);
    }

    LOG_DEBUG(MOD_UI, "InventoryManager Cleared bank slots");
}

// T282 -- mirrors EverQuest::m_active_tradeskill_object_id, whose only
// writer is the SERVER's OP_ClickObjectAction (plus the local
// /closecontainer, which eqemu never echoes). The model needs it because a
// world-slot move is only legal while the server holds a tradeskill object
// for us -- see canPlaceInWorldContainer() for what happens otherwise.
void InventoryManager::setWorldContainerOpen(bool open) {
    if (worldContainerOpen_ == open) {
        return;
    }
    worldContainerOpen_ = open;
    LOG_DEBUG(MOD_UI, "InventoryManager world container {}", open ? "opened" : "closed");
}

void InventoryManager::clearWorldContainerSlots() {
    // Clear items in world container slots (4000-4009)
    for (int16_t slot = WORLD_BEGIN; slot <= WORLD_END; slot++) {
        items_.erase(slot);
    }
    LOG_DEBUG(MOD_UI, "InventoryManager Cleared world container slots {}-{}", WORLD_BEGIN, WORLD_END);
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getBankItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;

    // Main bank slots (2000-2015)
    for (int16_t slot = BANK_BEGIN; slot <= BANK_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }

    // Bank bag contents (2031-2190)
    for (int16_t slot = BANK_BAGS_BEGIN; slot <= BANK_BAGS_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }

    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getSharedBankItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;

    // Shared bank slots (2500-2501)
    for (int16_t slot = SHARED_BANK_BEGIN; slot <= SHARED_BANK_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }

    // Shared bank bag contents (2531-2550)
    for (int16_t slot = SHARED_BANK_BAGS_BEGIN; slot <= SHARED_BANK_BAGS_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }

    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getEquipmentItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;
    for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }
    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getGeneralItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }
    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getSellableItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;

    LOG_DEBUG(MOD_UI, "getSellableItems: Checking inventory for sellable items (items_.size={})", items_.size());

    // Check general inventory slots (22-29) for non-container items
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            LOG_DEBUG(MOD_UI, "  General slot {}: '{}' container={} noDrop={}",
                slot, item->name, item->isContainer(), item->noDrop);
            if (!item->isContainer() && !item->noDrop) {
                result.emplace_back(slot, item);
            }
        }
    }

    // Check bag contents (251-330)
    for (int16_t slot = GENERAL_BAGS_BEGIN; slot <= GENERAL_BAGS_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            LOG_DEBUG(MOD_UI, "  Bag slot {}: '{}' noDrop={}", slot, item->name, item->noDrop);
            if (!item->noDrop) {
                result.emplace_back(slot, item);
            }
        }
    }

    LOG_DEBUG(MOD_UI, "getSellableItems: Found {} sellable items", result.size());
    return result;
}

EquipmentStats InventoryManager::calculateEquipmentStats() const {
    EquipmentStats stats;

    // Sum stats from all equipped items
    for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (!item) continue;

        stats.ac += item->ac;
        stats.atk += item->attack;
        stats.hp += item->hp;
        stats.mana += item->mana;
        stats.endurance += item->endurance;

        stats.str += item->str;
        stats.sta += item->sta;
        stats.agi += item->agi;
        stats.dex += item->dex;
        stats.wis += item->wis;
        stats.int_ += item->int_;
        stats.cha += item->cha;

        stats.magicResist += item->magicResist;
        stats.fireResist += item->fireResist;
        stats.coldResist += item->coldResist;
        stats.diseaseResist += item->diseaseResist;
        stats.poisonResist += item->poisonResist;

        stats.hpRegen += item->hpRegen;
        stats.manaRegen += item->manaRegen;

        // Haste: take highest value (doesn't stack)
        if (item->haste > stats.haste) {
            stats.haste = item->haste;
        }

        stats.weight += item->weight;
    }

    return stats;
}

float InventoryManager::calculateTotalWeight() const {
    float totalWeight = 0.0f;

    // Iterate over all items
    for (const auto& [slot, item] : items_) {
        if (!item) continue;

        float itemWeight = item->weight;

        // Apply bag weight reduction if this item is in a bag
        if (isBagSlot(slot)) {
            int16_t parentSlot = getParentGeneralSlot(slot);
            const ItemInstance* bag = getItem(parentSlot);
            if (bag && bag->bagWR > 0) {
                // Weight reduction is a percentage
                float reduction = static_cast<float>(bag->bagWR) / 100.0f;
                itemWeight *= (1.0f - reduction);
            }
        }

        totalWeight += itemWeight * item->quantity;
    }

    return totalWeight;
}

const ItemInstance* InventoryManager::getBagAtSlot(int16_t bagSlot) const {
    int16_t parentSlot = getParentGeneralSlot(bagSlot);
    if (parentSlot == SLOT_INVALID) {
        return nullptr;
    }
    return getItem(parentSlot);
}

bool InventoryManager::isValidSlot(int16_t slotId) const {
    return isEquipmentSlot(slotId) ||
           isGeneralSlot(slotId) ||
           isBagSlot(slotId) ||
           isCursorSlot(slotId) ||
           isBankSlot(slotId) ||
           isSharedBankSlot(slotId) ||
           isBankBagSlot(slotId) ||
           isSharedBankBagSlot(slotId) ||
           isTradeSlot(slotId) ||
           isTradeBagSlot(slotId) ||
           // T282 -- the world tradeskill container's item slots (4000-4009).
           // eqemu zone/inventory.cpp Client::IsValidSlot() accepts the whole
           // range unconditionally, and common/patches/titanium.cpp maps it
           // through both slot converters untranslated, so 4000 on the wire
           // IS slot 4000. Rejecting it here is what stopped the client from
           // ever driving the WORLD form of /combine (T278 hand-back).
           // WHETHER a world-slot move may be SENT is a separate question --
           // see moveItem(), which additionally requires an open container.
           isWorldContainerItemSlot(slotId);
}

const ItemInstance* InventoryManager::getContainerAtSlot(int16_t bagSlot) const {
    // Use the universal helper to get the parent container slot
    int16_t parentSlot = getParentContainerSlot(bagSlot);
    if (parentSlot == SLOT_INVALID) {
        return nullptr;
    }
    return getItem(parentSlot);
}

bool InventoryManager::destroyItem(int16_t slotId) {
    if (!hasItem(slotId)) {
        return false;
    }

    const ItemInstance* item = getItem(slotId);
    if (item && item->noDestroy) {
        return false;  // Can't destroy this item
    }

    // If it's a bag, check if empty
    if (item && item->isContainer()) {
        if (!isBagEmpty(slotId)) {
            return false;  // Can't destroy non-empty bag
        }
    }

    removeItem(slotId);

    // Notify server
    if (bridge_) {
        bridge_->pushIntent(eqt::events::DeleteItemIntent{slotId});
    }

    return true;
}

bool InventoryManager::destroyCursorItem() {
    if (cursorQueue_.empty()) {
        return false;
    }

    const auto& cursorItem = cursorQueue_.front();
    if (cursorItem->noDestroy) {
        return false;
    }

    LOG_DEBUG(MOD_UI, "InventoryManager destroyCursorItem: {} (cursorSourceSlot={})",
              cursorItem->name, cursorSourceSlot_);

    // A bag on the cursor CAN carry contents (T252); they are destroyed with
    // it, and the next queued bag's contents (if any) become the range.
    dropCursorFrontContents();
    cursorQueue_.pop_front();
    promoteCursorFrontContents();

    // Update source slot for next item (if any)
    if (cursorQueue_.empty()) {
        cursorSourceSlot_ = SLOT_INVALID;
    } else {
        // If more items in queue, send pop notification
        if (bridge_) {
            bridge_->pushIntent(eqt::events::MoveItemIntent{CURSOR_SLOT, CURSOR_SLOT, 0});
        }
        cursorSourceSlot_ = CURSOR_SLOT;  // Next item is from server queue
    }

    // Notify server - item is on CURSOR_SLOT, not the original source slot
    if (bridge_) {
        bridge_->pushIntent(eqt::events::DeleteItemIntent{CURSOR_SLOT});
    }

    return true;
}

} // namespace inventory
} // namespace eqt
