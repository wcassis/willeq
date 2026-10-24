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

bool InventoryManager::canPlaceInBag(const ItemInstance* item, int16_t bagSlot) const {
    if (!item) {
        return false;
    }

    // Can't put bags in bags
    if (item->isContainer()) {
        return false;
    }

    // Get the bag containing this slot
    const ItemInstance* bag = getBagAtSlot(bagSlot);
    if (!bag) {
        return false;  // No bag at this location
    }

    // Check size constraint
    if (!item->canFitInContainer(bag->bagSize)) {
        return false;
    }

    return true;
}

bool InventoryManager::canPlaceInWorldContainer(const ItemInstance* item, int16_t slot) const {
    if (!item) {
        return false;
    }

    // T282 -- only while the server has a world container open for us.
    // eqemu zone/inventory.cpp Client::SwapItem() handles 4000-4009 in
    // "Step 3: Check for interaction with World Container", and that whole
    // step is gated on `m_tradeskill_object != nullptr`. Without it the move
    // falls through to Step 5's m_inv.SwapItem(), whose _PutItem(4000, inst)
    // finds CalcSlotId(4000) == INVALID_INDEX (common/inventory_profile.cpp
    // CalcSlotId has no WORLD branch), takes the "must be within a bag"
    // else-arm, gets a null parent bag and calls MarkDirty(inst): the item is
    // DESTROYED, SwapItem still returns true, and the client is told nothing.
    if (!worldContainerOpen_) {
        LOG_DEBUG(MOD_UI, "No world container open -- refusing slot {}", slot);
        return false;
    }

    // Can't put containers in world containers (no nested containers).
    // eqemu does not check this, but Object::PutItem() stores the bag by
    // value into the world container's own ItemInstance and
    // ZoneDatabase::SaveWorldContainer() persists one flat level of
    // object_contents, so a nested bag's contents would not survive; the
    // official client does not offer the move either.
    if (item->isContainer()) {
        LOG_DEBUG(MOD_UI, "Cannot place container in world container slot {}", slot);
        return false;
    }

    // T282 -- NO DROP is allowed, deliberately. The old refusal here was a
    // client-side invention: eqemu's "No Drop Hack" block in SwapItem lists
    // trade and shared-bank destinations only, never WORLD_BEGIN..WORLD_END,
    // which is exactly what makes NO DROP quest combines possible.

    // World containers don't have size restrictions like bags do
    // The server will validate if the item is appropriate for the tradeskill

    return true;
}

bool InventoryManager::meetsRestrictions(const ItemInstance* item) const {
    if (!item) {
        return false;
    }

    return item->isUsableBy(playerClass_, playerRace_, playerLevel_);
}

bool InventoryManager::hasLoreConflict(const ItemInstance* item, int16_t excludeSlot) const {
    if (!item || !item->loreItem) {
        return false;  // Not a lore item, no conflict
    }

    // Check all inventory slots for a matching lore item
    for (const auto& [slotId, existingItem] : items_) {
        if (slotId == excludeSlot) {
            continue;  // Skip the slot we're moving from
        }

        if (!existingItem || !existingItem->loreItem) {
            continue;
        }

        // Check for lore conflict:
        // - If both items have a lore group, they conflict if groups match
        // - If either has no lore group (0), they conflict if item IDs match
        if (item->loreGroup != 0 && existingItem->loreGroup != 0) {
            if (item->loreGroup == existingItem->loreGroup) {
                return true;  // Same lore group = conflict
            }
        } else {
            // No lore group, check by item ID
            if (item->itemId == existingItem->itemId) {
                return true;  // Same item ID = conflict
            }
        }
    }

    // Also check cursor item if we're placing into inventory
    if (!cursorQueue_.empty()) {
        const auto& cursorItem = cursorQueue_.front();
        if (cursorItem.get() != item && cursorItem->loreItem) {
            if (item->loreGroup != 0 && cursorItem->loreGroup != 0) {
                if (item->loreGroup == cursorItem->loreGroup) {
                    return true;
                }
            } else if (item->itemId == cursorItem->itemId) {
                return true;
            }
        }
    }

    return false;
}

bool InventoryManager::isBag(int16_t slotId) const {
    const ItemInstance* item = getItem(slotId);
    return item && item->isContainer();
}

int InventoryManager::getBagSize(int16_t slotId) const {
    const ItemInstance* item = getItem(slotId);
    if (item && item->isContainer()) {
        return item->bagSlots;
    }
    return 0;
}

std::vector<int16_t> InventoryManager::getBagContentsSlots(int16_t generalSlot) const {
    std::vector<int16_t> slots;

    if (!isGeneralSlot(generalSlot) && generalSlot != CURSOR_SLOT) {
        return slots;
    }

    const ItemInstance* bag = getItem(generalSlot);
    if (!bag || !bag->isContainer()) {
        return slots;
    }

    int bagSize = bag->bagSlots;
    for (int i = 0; i < bagSize; i++) {
        int16_t bagSlot;
        if (generalSlot == CURSOR_SLOT) {
            bagSlot = calcCursorBagSlotId(i);
        } else {
            bagSlot = calcBagSlotId(generalSlot, i);
        }
        if (bagSlot != SLOT_INVALID) {
            slots.push_back(bagSlot);
        }
    }

    return slots;
}

bool InventoryManager::isBagEmpty(int16_t generalSlot) const {
    auto slots = getBagContentsSlots(generalSlot);
    for (int16_t slot : slots) {
        if (hasItem(slot)) {
            return false;
        }
    }
    return true;
}

bool InventoryManager::isBagParentSlot(int16_t slotId) {
    // T255: bank (2000-2015 -> 2031-2190) and shared bank (2500-2501 ->
    // 2531-2550) parents join general and cursor. eqemu carries the
    // contents inside the bag's ItemInstance for every one of these, and
    // CalcSlotId (common/patches/titanium.cpp) numbers them all
    // parent-relative in steps of 10 on the wire.
    // T259: trade-window slots (3000-3007 -> 3031-3110) too. eqemu
    // Trade::AddEntity (zone/trading.cpp) PutItemInInventory()s the whole
    // ItemInstance into the trade slot, contents and all, numbered
    // TRADE_BAGS_BEGIN + (trade-3000)*10 + idx on the Titanium wire
    // (common/patches/titanium_limits.h:229, titanium.cpp:3630/3717).
    return isGeneralSlot(slotId) || slotId == CURSOR_SLOT ||
           isBankSlot(slotId) || isSharedBankSlot(slotId) ||
           isTradeSlot(slotId);
}

void InventoryManager::promoteCursorFrontContents() {
    if (cursorQueue_.empty()) {
        return;
    }
    auto it = queuedCursorBagContents_.find(cursorQueue_.front().get());
    if (it == queuedCursorBagContents_.end()) {
        return;
    }
    TakenBagContents contents = std::move(it->second);
    queuedCursorBagContents_.erase(it);
    LOG_DEBUG(MOD_UI, "InventoryManager Cursor front '{}' promoted with {} queued bag contents",
              cursorQueue_.front()->name, contents.size());
    putBagContents(CURSOR_SLOT, contents);
    ++changeCounter_;
}

void InventoryManager::dropCursorFrontContents() {
    if (cursorQueue_.empty()) {
        return;
    }
    const ItemInstance* front = cursorQueue_.front().get();
    queuedCursorBagContents_.erase(front);
    if (front->isContainer()) {
        auto gone = takeBagContents(CURSOR_SLOT);
        if (!gone.empty()) {
            LOG_DEBUG(MOD_UI, "InventoryManager Cursor bag '{}' leaves with {} contents",
                      front->name, gone.size());
            ++changeCounter_;
        }
    }
}

InventoryManager::TakenBagContents InventoryManager::takeBagContents(int16_t parentSlot) {
    TakenBagContents taken;
    for (int16_t i = 0; i < BAG_SLOT_COUNT; i++) {
        int16_t slot = calcContainerSlotId(parentSlot, i);
        if (slot == SLOT_INVALID) {
            continue;
        }
        auto it = items_.find(slot);
        if (it != items_.end()) {
            taken.emplace_back(i, std::move(it->second));
            items_.erase(it);
        }
    }
    return taken;
}

void InventoryManager::putBagContents(int16_t parentSlot, TakenBagContents& contents) {
    for (auto& [index, item] : contents) {
        int16_t slot = calcContainerSlotId(parentSlot, index);
        if (slot == SLOT_INVALID || !item) {
            continue;
        }
        if (items_.count(slot)) {
            // Cannot happen if the callers' invariants hold (a range is only
            // written after it was emptied); say so rather than lose an item
            // silently the way the pre-T252 code did.
            LOG_WARN(MOD_UI, "InventoryManager putBagContents: slot {} already holds '{}' -- overwritten by '{}'",
                     slot, items_[slot]->name, item->name);
        }
        LOG_DEBUG(MOD_UI, "InventoryManager Bag content '{}' -> slot {} (parent {}, index {})",
                  item->name, slot, parentSlot, index);
        item->currentSlot = slot;
        items_[slot] = std::move(item);
    }
    contents.clear();
}

void InventoryManager::relocateBagContents(int16_t fromParentSlot, int16_t toParentSlot) {
    // Move bag contents from one parent slot's range to another. Local
    // bookkeeping only -- the server carries contents inside the bag's
    // ItemInstance and needs no extra packet (eqemu InventoryProfile::
    // SwapItem, inventory_profile.cpp:392-393).
    if (!isBagParentSlot(fromParentSlot) || !isBagParentSlot(toParentSlot) ||
        fromParentSlot == toParentSlot) {
        return;
    }

    auto contents = takeBagContents(fromParentSlot);
    if (contents.empty()) {
        return;
    }
    LOG_DEBUG(MOD_UI, "InventoryManager Relocating {} bag contents from parent slot {} to {}",
              contents.size(), fromParentSlot, toParentSlot);
    putBagContents(toParentSlot, contents);
    ++changeCounter_;
}

void InventoryManager::swapBagContents(int16_t parentSlotA, int16_t parentSlotB) {
    // T252 -- both ranges are lifted out BEFORE either is written, so a bag
    // with contents dropped onto another bag with contents cannot overwrite
    // the displaced bag's items at matching indices (the T250 finding).
    if (!isBagParentSlot(parentSlotA) || !isBagParentSlot(parentSlotB) ||
        parentSlotA == parentSlotB) {
        return;
    }

    auto contentsA = takeBagContents(parentSlotA);
    auto contentsB = takeBagContents(parentSlotB);
    if (contentsA.empty() && contentsB.empty()) {
        return;
    }
    LOG_DEBUG(MOD_UI, "InventoryManager Swapping bag contents: parent {} ({} items) <-> parent {} ({} items)",
              parentSlotA, contentsA.size(), parentSlotB, contentsB.size());
    putBagContents(parentSlotB, contentsA);
    putBagContents(parentSlotA, contentsB);
    ++changeCounter_;
}

bool InventoryManager::isBankBag(int16_t slotId) const {
    // Check if slot is a bank slot (main or shared) containing a container
    if (!isBankSlot(slotId) && !isSharedBankSlot(slotId)) {
        return false;
    }
    const ItemInstance* item = getItem(slotId);
    return item && item->isContainer();
}

int InventoryManager::getBankBagSize(int16_t slotId) const {
    if (!isBankSlot(slotId) && !isSharedBankSlot(slotId)) {
        return 0;
    }
    const ItemInstance* item = getItem(slotId);
    if (item && item->isContainer()) {
        return item->bagSlots;
    }
    return 0;
}

std::vector<int16_t> InventoryManager::getBankBagContentsSlots(int16_t bankSlot) const {
    std::vector<int16_t> slots;

    // Must be a bank or shared bank slot
    if (!isBankSlot(bankSlot) && !isSharedBankSlot(bankSlot)) {
        return slots;
    }

    const ItemInstance* bag = getItem(bankSlot);
    if (!bag || !bag->isContainer()) {
        return slots;
    }

    int bagSize = bag->bagSlots;
    for (int i = 0; i < bagSize; i++) {
        int16_t bagSlot = calcContainerSlotId(bankSlot, i);
        if (bagSlot != SLOT_INVALID) {
            slots.push_back(bagSlot);
        }
    }

    return slots;
}

bool InventoryManager::isBankBagEmpty(int16_t bankSlot) const {
    auto slots = getBankBagContentsSlots(bankSlot);
    for (int16_t slot : slots) {
        if (hasItem(slot)) {
            return false;
        }
    }
    return true;
}

bool InventoryManager::canPlaceInBankSlot(const ItemInstance* item, int16_t targetSlot) const {
    if (!item) {
        return false;
    }

    // Check if this is a main bank slot
    if (isBankSlot(targetSlot)) {
        return true;  // Any item can go in main bank slots
    }

    // Check if this is a shared bank slot
    if (isSharedBankSlot(targetSlot)) {
        // Only non-nodrop items can go in shared bank (account-wide storage)
        return !item->noDrop;
    }

    // Check if this is a bank bag slot
    if (isBankBagSlot(targetSlot) || isSharedBankBagSlot(targetSlot)) {
        // Can't put bags in bags
        if (item->isContainer()) {
            return false;
        }

        // Get the parent bank slot to find the container
        const ItemInstance* container = getContainerAtSlot(targetSlot);
        if (!container) {
            return false;  // No container at this location
        }

        // Check size constraint
        if (!item->canFitInContainer(container->bagSize)) {
            return false;
        }

        // For shared bank bag slots, check nodrop
        if (isSharedBankBagSlot(targetSlot) && item->noDrop) {
            return false;
        }

        return true;
    }

    return false;
}

// B1140: /autoinventory's slot choice -- see the declaration for the oracle
// rows each step reproduces.
int16_t InventoryManager::findAutoInventorySlot(const ItemInstance& item) const {
    // The carried search order: the general row, then each general-row bag's
    // contents in turn.
    std::vector<int16_t> order;
    for (int16_t g = GENERAL_BEGIN; g <= GENERAL_END; ++g) {
        order.push_back(g);
    }
    for (int16_t g = GENERAL_BEGIN; g <= GENERAL_END; ++g) {
        for (int16_t b : getBagContentsSlots(g)) {
            order.push_back(b);
        }
    }
    if (item.stackable && item.itemId != 0) {
        for (int16_t slot : order) {
            const ItemInstance* held = getItem(slot);
            if (held && held->stackable && held->itemId == item.itemId &&
                held->stackSize > 0 && held->quantity < held->stackSize) {
                return slot;
            }
        }
    }
    for (int16_t slot : order) {
        if (!hasItem(slot) && canPlaceItemInSlot(&item, slot)) {
            return slot;
        }
    }
    return SLOT_INVALID;
}

} // namespace inventory
} // namespace eqt
