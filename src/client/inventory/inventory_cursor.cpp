#include "client/inventory/inventory_manager.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "common/net/packet.h"
#include "common/packet_structs.h"  // T282: EQT::ITEM_PACKET_WORLD_CONTAINER
#ifdef WITH_AUDIO
#include "client/audio/ui_sounds.h"   // T328: the measured UI sound ids
#endif
#include <iostream>
#include "common/logging.h"
#include <sstream>
#include <charconv>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace eqt {
namespace inventory {

bool InventoryManager::pickupItem(int16_t slotId) {
    // Can't pick up if already holding something on cursor
    if (!cursorQueue_.empty()) {
        return false;
    }

    auto it = items_.find(slotId);
    if (it == items_.end()) {
        return false;  // No item at this slot
    }

    // Move item to cursor (push to front since this becomes active)
    LOG_DEBUG(MOD_UI, "InventoryManager Picked up '{}' from slot {}", it->second->name, slotId);
    bool pickedUpBag = it->second->isContainer();
    cursorQueue_.push_front(std::move(it->second));
    cursorSourceSlot_ = slotId;
    items_.erase(it);

    // T252 -- a bag on the cursor keeps its contents (server: inside the
    // ItemInstance, addressed as the cursor bag range 331-340). Mirror that
    // so the contents never sit under whatever lands in the vacated slot.
    // T255: the source may be a bank / shared-bank slot too.
    if (pickedUpBag && isBagParentSlot(slotId)) {
        relocateBagContents(slotId, CURSOR_SLOT);
    }

    // Notify server of pickup (move from slot to cursor)
    // quantity = 0 means move entire item/stack
    if (bridge_) {
        bridge_->pushIntent(eqt::events::MoveItemIntent{slotId, CURSOR_SLOT, 0});
    }

    // Notify equipment change callback if picking up from equipment slot
    if (isEquipmentSlot(slotId) && bridge_) {
        bridge_->pushIntent(eqt::events::EquipmentChangedIntent{slotId});
    }

    return true;
}

bool InventoryManager::placeItem(int16_t targetSlot) {
    if (cursorQueue_.empty()) {
        return false;  // Nothing to place
    }

    // Get the front (active) cursor item
    auto& cursorItem = cursorQueue_.front();

    // Check if placement is valid
    if (!canPlaceItemInSlot(cursorItem.get(), targetSlot)) {
        return false;
    }

    auto it = items_.find(targetSlot);
    if (it != items_.end()) {
        // Slot is occupied - swap items
        auto targetItem = std::move(it->second);

        // T249 -- no displaced-side check here, deliberately. The server's
        // rule (eqemu common/inventory_profile.cpp, InventoryProfile::
        // SwapItem, the destination_item_instance block at :359-:365)
        // validates the displaced item against the move's SOURCE slot, and
        // for a cursor placement the source IS the cursor (30) -- which
        // IsSlotAllowed() unconditionally permits (item_instance.cpp:892,
        // `slot_id > EQUIPMENT_END`). The server never sees
        // cursorSourceSlot_ (where the cursor item was picked up FROM), so
        // the old check against it refused swaps the server applies
        // (T236 Review, section 3; T249). The forward check above -- the
        // cursor item must fit targetSlot -- is the one the server does
        // enforce (inventory_profile.cpp:325) and stays.

        // Save info about items being swapped (before we move them)
        uint32_t placedQuantity = cursorItem->quantity;
        bool cursorItemIsBag = cursorItem->isContainer();
        bool targetItemIsBag = targetItem->isContainer();

        // Place cursor item in target slot
        cursorItem->currentSlot = targetSlot;
        items_[targetSlot] = std::move(cursorItem);

        // Pop the placed item from queue
        cursorQueue_.pop_front();

        // T252 -- the two contents ranges trade places: the cursor bag's
        // contents (cursor range) go under targetSlot, the displaced bag's
        // contents (targetSlot's range) go to the cursor range with it.
        // Either side may be empty or not a bag. This is what eqemu's
        // SwapItem does by swapping the two ItemInstances wholesale.
        // T255: targetSlot may be a bank / shared-bank slot. The queue's
        // next item is NOT promoted here -- the displaced item is about to
        // become the front, and its contents are the swapped-in range.
        if ((cursorItemIsBag || targetItemIsBag) && isBagParentSlot(targetSlot)) {
            swapBagContents(CURSOR_SLOT, targetSlot);
        }

        // Put target item on cursor (push to front - it becomes the new active item)
        cursorQueue_.push_front(std::move(targetItem));
        // T250: cursorSourceSlot_ means "where the CURRENT cursor front came
        // from". The front is now the displaced item, which came from
        // targetSlot -- not the original pickup slot. Leaving the old value
        // here made relocateBagContents(cursorSourceSlot_, ...) move the
        // wrong bag's contents on the next placement when the displaced
        // item is a bag (T249 finding).
        int16_t previousSource = cursorSourceSlot_;
        cursorSourceSlot_ = targetSlot;

        // Notify server of swap - use CURSOR_SLOT as source since item is on cursor
        // quantity = 0 means move entire item/stack
        LOG_DEBUG(MOD_UI, "InventoryManager Swapped items: cursor(from {}) <-> {} (cursor source now {})",
                  previousSource, targetSlot, cursorSourceSlot_);
        if (bridge_) {
            bridge_->pushIntent(eqt::events::MoveItemIntent{CURSOR_SLOT, targetSlot, 0});
        }

        // Notify equipment change callback if target is an equipment slot
        if (isEquipmentSlot(targetSlot) && bridge_) {
            bridge_->pushIntent(eqt::events::EquipmentChangedIntent{targetSlot});
        }
    } else {
        // Slot is empty - just place
        LOG_DEBUG(MOD_UI, "InventoryManager Placed '{}' in slot {}", cursorItem->name, targetSlot);
        bool cursorItemIsBag = cursorItem->isContainer();

        cursorItem->currentSlot = targetSlot;
        items_[targetSlot] = std::move(cursorItem);

        // Pop the placed item from queue
        cursorQueue_.pop_front();

        // T252 -- the bag's contents come from the cursor range (put there
        // by pickupItem() / the displaced side of a swap), not from the
        // slot the bag was originally picked up from.
        // T255: targetSlot may be a bank / shared-bank slot.
        if (cursorItemIsBag && isBagParentSlot(targetSlot)) {
            relocateBagContents(CURSOR_SLOT, targetSlot);
        }

        // Notify server - use CURSOR_SLOT as source since item is on cursor
        // quantity = 0 means move entire item/stack
        if (bridge_) {
            bridge_->pushIntent(eqt::events::MoveItemIntent{CURSOR_SLOT, targetSlot, 0});
        }

        // If queue still has items, send pop notification to server
        if (!cursorQueue_.empty()) {
            LOG_DEBUG(MOD_UI, "InventoryManager Cursor queue has {} more items, sending pop notification", cursorQueue_.size());
            if (bridge_) {
                // Send cursor-to-cursor move to pop the next item on server
                bridge_->pushIntent(eqt::events::MoveItemIntent{CURSOR_SLOT, CURSOR_SLOT, 0});
            }
            cursorSourceSlot_ = CURSOR_SLOT;  // Next item is from server queue
            promoteCursorFrontContents();     // T255: the new front's bag contents
        } else {
            cursorSourceSlot_ = SLOT_INVALID;
        }

        // Notify equipment change callback if target is an equipment slot
        if (isEquipmentSlot(targetSlot) && bridge_) {
            bridge_->pushIntent(eqt::events::EquipmentChangedIntent{targetSlot});
        }
    }

    // T328 Leg B: the official Titanium client plays wearclth.WAV (146) when
    // an item is DROPPED into an inventory or bag slot
    // (review/T328/README.md row 6). Picking an item UP onto the cursor is
    // measured SILENT (row 7), so pickupItem() gets nothing.
    //
    // Played on the LOCAL drop rather than on the server's MoveItem echo, for
    // two reasons: it matches the oracle's timing (the official client is
    // authoritative over its own cursor and makes the sound on the click,
    // not a round trip later), and the server sends no per-move reply to
    // hang it off -- processMoveItemResponse() exists but eqemu answers a
    // successful SwapItem with silence.
#ifdef WITH_AUDIO
    if (bridge_) {
        bridge_->pushIntent(eqt::events::PlaySoundIntent{
            EQT::Audio::UISoundId::WEAR_CLOTH});
    }
#endif

    return true;
}

bool InventoryManager::pickupPartialStack(int16_t slotId, int32_t quantity) {
    // Can't pick up if already holding something on cursor
    if (!cursorQueue_.empty()) {
        return false;
    }

    auto it = items_.find(slotId);
    if (it == items_.end()) {
        return false;  // No item at this slot
    }

    auto& sourceItem = it->second;
    if (!sourceItem->stackable || sourceItem->quantity <= 1) {
        // Not stackable or only 1 item - just pick up the whole thing
        return pickupItem(slotId);
    }

    // Clamp quantity to available amount
    quantity = std::min(quantity, sourceItem->quantity);
    if (quantity <= 0) {
        return false;
    }

    if (quantity >= sourceItem->quantity) {
        // Picking up entire stack - use normal pickup
        return pickupItem(slotId);
    }

    // Split the stack: create a new item on cursor with requested quantity
    auto cursorItem = std::make_unique<ItemInstance>(*sourceItem);
    cursorItem->quantity = quantity;
    cursorItem->currentSlot = CURSOR_SLOT;

    // Reduce source stack quantity
    sourceItem->quantity -= quantity;

    // Move new item to cursor
    LOG_DEBUG(MOD_UI, "InventoryManager Picked up {} of '{}' from slot {} (leaving {})",
              quantity, sourceItem->name, slotId, sourceItem->quantity);
    cursorQueue_.push_front(std::move(cursorItem));
    cursorSourceSlot_ = slotId;

    // Notify server of partial pickup
    if (bridge_) {
        bridge_->pushIntent(eqt::events::MoveItemIntent{slotId, CURSOR_SLOT, quantity});
    }

    return true;
}

bool InventoryManager::placeOnMatchingStack(int16_t targetSlot) {
    if (cursorQueue_.empty()) {
        return false;  // Nothing to place
    }

    auto& cursorItem = cursorQueue_.front();
    auto it = items_.find(targetSlot);
    if (it == items_.end()) {
        // No item at target - just place normally
        return placeItem(targetSlot);
    }

    auto& targetItem = it->second;

    // Check if items can stack
    if (!cursorItem->stackable || !targetItem->stackable ||
        cursorItem->itemId != targetItem->itemId) {
        // Can't stack - do normal swap
        return placeItem(targetSlot);
    }

    // Check stack size limits
    int32_t targetMax = targetItem->stackSize;  // stackSize of 0 means not stackable
    int32_t availableSpace = targetMax - targetItem->quantity;

    if (availableSpace <= 0) {
        // Target stack is full - do normal swap
        return placeItem(targetSlot);
    }

    // Calculate how many items to transfer
    int32_t toTransfer = std::min(cursorItem->quantity, availableSpace);

    // Update quantities
    targetItem->quantity += toTransfer;
    cursorItem->quantity -= toTransfer;

    LOG_DEBUG(MOD_UI, "InventoryManager Stacked {} items onto slot {} (now {} total)",
              toTransfer, targetSlot, targetItem->quantity);

    // Notify server of stack operation
    if (bridge_) {
        bridge_->pushIntent(eqt::events::MoveItemIntent{CURSOR_SLOT, targetSlot, toTransfer});
    }

    // If cursor item is now empty, remove it from cursor
    if (cursorItem->quantity <= 0) {
        cursorQueue_.pop_front();
        promoteCursorFrontContents();  // T255
        if (cursorQueue_.empty()) {
            cursorSourceSlot_ = SLOT_INVALID;
        } else {
            cursorSourceSlot_ = CURSOR_SLOT;  // Next item from server queue
        }
    }

    // T328 Leg B: same event, same sound -- Leg A measured a stack deposited
    // into a bag as wearclth.WAV (review/T328/README.md row 6, capture
    // E24_deposit_stack_in_bag.wav). The three early returns above delegate
    // to placeItem(), which plays it itself; only this merge-into-a-stack
    // branch needs its own.
#ifdef WITH_AUDIO
    if (bridge_) {
        bridge_->pushIntent(eqt::events::PlaySoundIntent{
            EQT::Audio::UISoundId::WEAR_CLOTH});
    }
#endif

    return true;
}

bool InventoryManager::hasCursorItem() const {
    return !cursorQueue_.empty();
}

const ItemInstance* InventoryManager::getCursorItem() const {
    return cursorQueue_.empty() ? nullptr : cursorQueue_.front().get();
}

void InventoryManager::popCursorItem() {
    if (cursorQueue_.empty()) {
        return;
    }

    cursorQueue_.pop_front();
    promoteCursorFrontContents();  // T255

    // If queue still has items, send pop notification to server
    if (!cursorQueue_.empty()) {
        LOG_DEBUG(MOD_UI, "InventoryManager popCursorItem: {} items remaining in cursor queue", cursorQueue_.size());
        if (bridge_) {
            // Send cursor-to-cursor move to pop the next item on server
            bridge_->pushIntent(eqt::events::MoveItemIntent{CURSOR_SLOT, CURSOR_SLOT, 0});
        }
        cursorSourceSlot_ = CURSOR_SLOT;  // Next item is from server queue
    } else {
        cursorSourceSlot_ = SLOT_INVALID;
    }
}

size_t InventoryManager::getCursorQueueSize() const {
    return cursorQueue_.size();
}

void InventoryManager::pickupMoney(CursorMoneyType type, uint32_t amount) {
    if (amount == 0 || type == CursorMoneyType::None) {
        return;
    }
    cursorMoneyType_ = type;
    cursorMoneyAmount_ = amount;
    LOG_DEBUG(MOD_UI, "InventoryManager picked up {} of money type {}",
              amount, static_cast<int>(type));

    // T328 Leg B: BuyItem.WAV (138) is the generic COIN-MOVE sound, not a
    // purchase sound -- Leg A measured it firing when 1pp moved from a money
    // slot to the cursor with no merchant in the zone, while the same
    // Quantity dialog for an ITEM stack was silent (review/T328/README.md
    // row 8 and finding 3). The reverse gesture, coin returned from the
    // cursor to a money slot, is measured SILENT (row 9), so
    // returnCursorMoney() gets nothing.
    //
    // REACHABILITY: the coin gesture is the production caller (B442) --
    // RunCursorCoinGesture() calls this for the to-cursor half of a coin
    // move. There is still no money-slot UI: nothing under
    // src/client/graphics/ui mentions money, so the gesture is reached
    // only from the trade window (B896) and the bank's coin rows (B996).
    // The sound is wired at the correct publisher for the moment a
    // money-slot UI exists; boarded on B036.
#ifdef WITH_AUDIO
    if (bridge_) {
        bridge_->pushIntent(eqt::events::PlaySoundIntent{
            EQT::Audio::UISoundId::BUY_ITEM});
    }
#endif
}

void InventoryManager::clearCursorMoney() {
    cursorMoneyType_ = CursorMoneyType::None;
    cursorMoneyAmount_ = 0;
}

void InventoryManager::returnCursorMoney() {
    // Money is returned by simply clearing it - the callback to update
    // player money should be handled by WindowManager
    clearCursorMoney();
}

void InventoryManager::setSpellForScribe(uint32_t spellId, int16_t sourceSlot) {
    scribeSpellId_ = spellId;
    scribeSourceSlot_ = sourceSlot;
    LOG_DEBUG(MOD_UI, "InventoryManager: Set spell {} from slot {} for scribing",
              spellId, sourceSlot);

    // Move the scroll to cursor slot - this sends MoveItem to server
    // The server expects the scroll to be on cursor when we send OP_MemorizeSpell
    if (!pickupItem(sourceSlot)) {
        LOG_WARN(MOD_UI, "InventoryManager: Failed to pick up scroll from slot {} for scribing",
                 sourceSlot);
    }
}

void InventoryManager::clearSpellForScribe() {
    if (scribeSpellId_ != 0) {
        LOG_DEBUG(MOD_UI, "InventoryManager: Cleared spell scribing state (was spell {})",
                  scribeSpellId_);
    }
    scribeSpellId_ = 0;
    scribeSourceSlot_ = SLOT_INVALID;
}

bool InventoryManager::consumeScribedScroll() {
    clearSpellForScribe();
    if (cursorQueue_.empty()) {
        return false;
    }

    LOG_DEBUG(MOD_UI, "InventoryManager: scroll '{}' consumed by the server after scribing",
              cursorQueue_.front()->name);

    // Same local bookkeeping as destroyCursorItem(), WITHOUT the
    // DeleteItemIntent -- the server deleted it and told us so.
    return discardCursorFront();
}

bool InventoryManager::discardCursorFront() {
    if (cursorQueue_.empty()) {
        return false;
    }
    dropCursorFrontContents();
    cursorQueue_.pop_front();
    promoteCursorFrontContents();
    cursorSourceSlot_ = cursorQueue_.empty() ? SLOT_INVALID : CURSOR_SLOT;
    return true;
}

bool InventoryManager::swapItems(int16_t fromSlot, int16_t toSlot) {
    auto fromIt = items_.find(fromSlot);
    auto toIt = items_.find(toSlot);

    if (fromIt == items_.end()) {
        return false;  // No item at source
    }

    // Check if from item can go to target
    if (!canPlaceItemInSlot(fromIt->second.get(), toSlot)) {
        return false;
    }

    if (toIt != items_.end()) {
        // Check if target item can go to source
        if (!canPlaceItemInSlot(toIt->second.get(), fromSlot)) {
            return false;
        }

        // Swap
        std::swap(fromIt->second, toIt->second);
        fromIt->second->currentSlot = fromSlot;
        toIt->second->currentSlot = toSlot;
    } else {
        // Just move
        auto item = std::move(fromIt->second);
        item->currentSlot = toSlot;
        items_.erase(fromIt);
        items_[toSlot] = std::move(item);
    }

    // Notify server - quantity = 0 means move entire item/stack
    if (bridge_) {
        bridge_->pushIntent(eqt::events::MoveItemIntent{fromSlot, toSlot, 0});
    }

    return true;
}

bool InventoryManager::canPlaceItemInSlot(const ItemInstance* item, int16_t targetSlot) const {
    if (!item) {
        return false;
    }

    // Check for lore item conflicts (can't have duplicate lore items)
    // Exclude cursor source slot since we're moving from there
    if (hasLoreConflict(item, cursorSourceSlot_)) {
        return false;
    }

    // Equipment slots have restrictions
    if (isEquipmentSlot(targetSlot)) {
        return canEquipItem(item, targetSlot);
    }

    // General inventory slots - anything goes
    if (isGeneralSlot(targetSlot)) {
        return true;
    }

    // Bag slots - check container constraints
    if (isBagSlot(targetSlot)) {
        return canPlaceInBag(item, targetSlot);
    }

    // Bank slots (main and shared, plus bag slots within)
    if (isBankSlot(targetSlot) || isSharedBankSlot(targetSlot) ||
        isBankBagSlot(targetSlot) || isSharedBankBagSlot(targetSlot)) {
        return canPlaceInBankSlot(item, targetSlot);
    }

    // Trade slots - NPC trades allow all items, player trades restrict NO_DROP
    if (isTradeSlot(targetSlot)) {
        // NPC trades (quests) allow all item types including NO_DROP
        if (isNpcTrade_) {
            return true;
        }
        return !item->noDrop;  // Only non-nodrop items can be traded to players
    }

    // Trade bag slots - check container constraints and tradeability
    if (isTradeBagSlot(targetSlot)) {
        // NPC trades allow all items in bags too
        if (isNpcTrade_) {
            if (item->isContainer()) {
                return false;  // Still can't put containers in containers
            }
            const ItemInstance* container = getContainerAtSlot(targetSlot);
            if (!container) {
                return false;
            }
            return item->canFitInContainer(container->bagSize);
        }
        if (item->noDrop || item->isContainer()) {
            return false;
        }
        const ItemInstance* container = getContainerAtSlot(targetSlot);
        if (!container) {
            return false;
        }
        return item->canFitInContainer(container->bagSize);
    }

    // World container slots (tradeskill containers like forges, looms)
    if (isWorldContainerItemSlot(targetSlot)) {
        return canPlaceInWorldContainer(item, targetSlot);
    }

    // Cursor slot
    if (isCursorSlot(targetSlot)) {
        return true;
    }

    return false;
}

bool InventoryManager::canEquipItem(const ItemInstance* item, int16_t equipSlot) const {
    if (!item) {
        return false;
    }

    // Check if item can go in this slot type
    if (!item->canEquipInSlot(equipSlot)) {
        LOG_DEBUG(MOD_UI, "canEquipItem: '{}' cannot go in slot {} (item slots bitmask=0x{:08X})",
                  item->name, equipSlot, item->slots);
        return false;
    }

    // Check player restrictions
    if (!meetsRestrictions(item)) {
        LOG_DEBUG(MOD_UI, "canEquipItem: '{}' fails restrictions (playerClass={}, playerRace={}, playerLevel={}, "
                  "itemClasses=0x{:X}, itemRaces=0x{:X}, itemReqLevel={})",
                  item->name, playerClass_, playerRace_, playerLevel_,
                  item->classes, item->races, item->reqLevel);
        return false;
    }

    return true;
}

// ============================================================================
// T141 -- equip orchestration, extracted verbatim (behavior-preserving) from
// eq_commands.cpp's RegisterEquipmentCommands() handler and its file-local
// helpers (CheckEquipDestination/ResolveEquipDestination/
// FindItemsByNameSubstring/the doMove lambda). See
// refactor/T141_equip_extraction.md's Review for the full argument for this
// home. No legality logic is duplicated: checkEquipDestination() reuses
// ItemInstance::canEquipInSlot() and this->canEquipItem(); moveItem() reuses
// this->isValidSlot() (previously mirrored by a second, file-local copy in
// eq_commands.cpp -- that duplication is gone now that the caller is a
// member function).
// ============================================================================

std::string InventoryManager::checkEquipDestination(const ItemInstance* item, int16_t toSlot) const {
    if (!item || !isEquipmentSlot(toSlot)) {
        return {};
    }
    if (!item->canEquipInSlot(toSlot)) {
        return item->name + " cannot go in " + describeSlot(toSlot) + " -- wrong slot type for this item.";
    }
    if (!canEquipItem(item, toSlot)) {
        return item->name + " cannot go in " + describeSlot(toSlot) +
               " -- your character does not meet its class/race/level requirements.";
    }
    return {};
}

InventoryManager::EquipDestinationResult
InventoryManager::resolveEquipDestination(const ItemInstance* item) const {
    EquipDestinationResult result;
    if (!item) {
        return result;
    }
    std::vector<int16_t> candidates;
    for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END; ++slot) {
        if (item->canEquipInSlot(slot)) {
            candidates.push_back(slot);
        }
    }
    if (candidates.empty()) {
        return result;
    }
    for (int16_t slot : candidates) {
        if (!getItem(slot)) {
            result.slot = slot;
            return result;
        }
    }
    result.slot = candidates.front();
    result.allCandidatesOccupied = true;
    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>>
InventoryManager::findItemsByNameSubstring(const std::string& needle) const {
    std::vector<std::pair<int16_t, const ItemInstance*>> matches;
    if (needle.empty()) {
        return matches;
    }
    std::string lowerNeedle = needle;
    std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), ::tolower);

    auto tryMatch = [&](int16_t slot, const ItemInstance* item) {
        if (!item) return;
        std::string lowerName = item->name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName.find(lowerNeedle) != std::string::npos) {
            matches.emplace_back(slot, item);
        }
    };

    for (const auto& [slot, item] : getEquipmentItems()) {
        tryMatch(slot, item);
    }
    for (const auto& [slot, item] : getGeneralItems()) {
        tryMatch(slot, item);
        if (item && item->isContainer()) {
            for (int16_t bagSlot : getBagContentsSlots(slot)) {
                tryMatch(bagSlot, getItem(bagSlot));
            }
        }
    }
    if (hasCursorItem()) {
        tryMatch(CURSOR_SLOT, getCursorItem());
    }
    return matches;
}

InventoryManager::MoveItemOutcome InventoryManager::moveItem(int16_t fromSlot, int16_t toSlot) {
    MoveItemOutcome outcome;

    if (!isValidSlot(fromSlot)) {
        // T494: describeSlot() prints the user-facing (`inventory.slot_id`)
        // number, so a refusal quotes back what the user typed.
        outcome.message = "Invalid source slot: " + describeSlot(fromSlot);
        return outcome;
    }
    if (!isValidSlot(toSlot)) {
        outcome.message = "Invalid destination slot: " + describeSlot(toSlot);
        return outcome;
    }
    if (fromSlot == toSlot) {
        outcome.message = "Source and destination are the same slot (" + describeSlot(fromSlot) +
                           ") -- nothing to do.";
        return outcome;
    }
    // Preserved verbatim from the pre-extraction handler: refuse the WHOLE
    // move (no mutation, no intent) if there is nowhere to send it, rather
    // than mutating the local model and silently dropping the wire
    // request -- pickupItem()/placeItem()/setItem() below tolerate
    // bridge_==nullptr on their own (push-if-possible), but moveItem()'s
    // caller-facing contract is stricter than theirs, per the original
    // handler this was extracted from.
    if (!bridge_) {
        outcome.message = "Cannot move item - no bridge attached.";
        return outcome;
    }

    // T77 -- pre-flight. Determine the source item first (same cursor-vs-
    // slot distinction the execution branches below make), so both "is
    // there anything to move" and "can it legally go there" are checked --
    // and reported specifically -- before any state is mutated or any
    // packet queued.
    const ItemInstance* sourceItem = isCursorSlot(fromSlot)
        ? (hasCursorItem() ? getCursorItem() : nullptr)
        : getItem(fromSlot);
    if (!sourceItem) {
        outcome.message = "Nothing to move: " + describeSlot(fromSlot) + " is empty.";
        return outcome;
    }
    std::string legalityError = checkEquipDestination(sourceItem, toSlot);
    if (!legalityError.empty()) {
        outcome.message = legalityError;
        return outcome;
    }
    // T259 -- eqemu Client::SwapItem (zone/inventory.cpp, "Step 4: Check
    // for entity trade") accepts a MoveItem into a trade slot ONLY from the
    // cursor and answers anything else with Kick("Trade with non-cursor
    // item"). Refuse it here, before any mutation or packet, rather than
    // let /equip 22 3000 disconnect the player.
    if (isTradeSlot(toSlot) && !isCursorSlot(fromSlot)) {
        // T494 (board B796): the numbers in a hint a user is meant to TYPE
        // are `inventory.slot_id`, so they go through clientSlotToServer()
        // exactly as describeSlot() does -- this line used to print the raw
        // model slot and the cursor as 30, neither of which /equip accepts.
        outcome.message = "Items can only be offered in a trade from the cursor: pick up " +
                          describeSlot(fromSlot) + " first (/equip " +
                          std::to_string(clientSlotToServer(fromSlot)) + " " +
                          std::to_string(SERVER_CURSOR_SLOT) + ", then /equip " +
                          std::to_string(SERVER_CURSOR_SLOT) + " " +
                          std::to_string(clientSlotToServer(toSlot)) + ").";
        return outcome;
    }

    // T282 -- the world tradeskill container (a forge/oven on the GROUND),
    // slots 4000-4009. Every rule below is eqemu's, read off
    // zone/inventory.cpp Client::SwapItem() "Step 3: Check for interaction
    // with World Container" rather than off a member name (T273/T277).
    if (isWorldContainerItemSlot(fromSlot) || isWorldContainerItemSlot(toSlot)) {
        // (a) Step 3 only runs while the server holds a tradeskill object
        // for us. Otherwise the move reaches Step 5 and the item is
        // silently DESTROYED (see canPlaceInWorldContainer()). Refuse here.
        if (!worldContainerOpen_) {
            outcome.message = "You must have a tradeskill container open to use " +
                              describeSlot(isWorldContainerItemSlot(toSlot) ? toSlot : fromSlot) +
                              " -- /clickobject a forge or oven first.";
            return outcome;
        }
        // (b) Taking an item OUT lands via Client::PutItemInInventory(dst,
        // inst, false), whose m_inv.PutItem() calls DeleteItem(slot_id)
        // FIRST -- an occupied destination is destroyed, with no packet
        // back. The cursor is safe: PutItemInInventory forwards a cursor
        // destination to PushItemOnCursor().
        if (isWorldContainerItemSlot(fromSlot) && !isWorldContainerItemSlot(toSlot) &&
            !isCursorSlot(toSlot) && getItem(toSlot) != nullptr) {
            outcome.message = "Taking an item out of a tradeskill container needs an empty "
                              "destination (or the cursor): " + describeSlot(toSlot) +
                              " already holds " + getItem(toSlot)->name + ".";
            return outcome;
        }
        // (c) The destination's own rules.
        if (isWorldContainerItemSlot(toSlot) && !canPlaceInWorldContainer(sourceItem, toSlot)) {
            outcome.message = sourceItem->name + " cannot go in " + describeSlot(toSlot) +
                              (sourceItem->isContainer()
                                   ? " -- a container cannot be placed inside a tradeskill container."
                                   : ".");
            return outcome;
        }
    }

    // T236 -- the OTHER half of a direct slot<->slot swap: when toSlot is
    // occupied, the displaced item lands in fromSlot, and the server
    // validates that side too. eqemu's rule (common/inventory_profile.cpp,
    // InventoryProfile::SwapItem, the `destination_item_instance` block):
    // the displaced item must IsSlotAllowed(source_slot) and, when
    // source_slot is an equipment slot, pass the race/class, deity and
    // level checks -- the same predicate checkEquipDestination() already
    // applies to the forward direction, so it is reused verbatim (no second
    // legality copy). IsSlotAllowed() is unconditionally true for every
    // non-equipment slot (item_instance.cpp: `slot_id > EQUIPMENT_END`), and
    // checkEquipDestination() passes for those too, so this refuses exactly
    // the swaps the server refuses and nothing else. T82 hit the gap live
    // (`/equip 13 22`, Skin of Milk in 22 -> "The selected slot was
    // invalid." + resync after the client had already swapped locally).
    // Cursor-source swaps are not affected: the displaced item goes to the
    // cursor, which IsSlotAllowed() always permits -- placeItem() handles
    // that branch and the server applies such swaps (T236 live leg).
    if (!isCursorSlot(fromSlot) && !isCursorSlot(toSlot)) {
        if (const ItemInstance* displacedItem = getItem(toSlot)) {
            std::string swapError = checkEquipDestination(displacedItem, fromSlot);
            if (!swapError.empty()) {
                outcome.message = "Cannot swap: " + swapError + " (it would be displaced there by " +
                                  sourceItem->name + ").";
                return outcome;
            }
        }
    }

    // T71 -- Stage 1 fix (see refactor/T71_equip_fix_and_first_person.md
    // Review for the full characterisation of why each branch below is
    // shaped this way; unchanged by T77's pre-flight above, which only
    // decides whether execution reaches here at all).
    if (isCursorSlot(fromSlot) && !isCursorSlot(toSlot)) {
        // e.g. /equip 30 14 -- cursor -> a real slot.
        if (!placeItem(toSlot)) {
            outcome.message = "Could not place cursor item in " + describeSlot(toSlot) + ".";
            return outcome;
        }
    } else if (!isCursorSlot(fromSlot) && isCursorSlot(toSlot)) {
        // e.g. /equip 13 30 -- a real slot -> cursor.
        if (!pickupItem(fromSlot)) {
            outcome.message = "Could not pick up item from " + describeSlot(fromSlot) + ".";
            return outcome;
        }
    } else if (!isCursorSlot(fromSlot) && !isCursorSlot(toSlot)) {
        // e.g. /equip 13 14 -- direct slot<->slot swap, no cursor staging
        // (matches the server's own atomic SwapItem(), proven live in
        // T71). Pre-flight above already confirmed sourceItem exists and
        // (if applicable) the destination is legal.
        ItemInstance movedFromItem = *sourceItem;  // copy: setItem() below invalidates sourceItem
        const ItemInstance* toItemPtr = getItem(toSlot);
        bool anyBag = movedFromItem.isContainer() || (toItemPtr && toItemPtr->isContainer());
        if (toItemPtr) {
            ItemInstance movedToItem = *toItemPtr;
            setItem(toSlot, movedFromItem);
            setItem(fromSlot, movedToItem);
            // T252 -- bag contents follow their bags, both ways at once
            // (eqemu SwapItem swaps whole ItemInstances, contents included).
            // T255: either side may be a bank / shared-bank slot.
            if (anyBag && isBagParentSlot(fromSlot) && isBagParentSlot(toSlot)) {
                swapBagContents(fromSlot, toSlot);
            }
        } else {
            setItem(toSlot, movedFromItem);
            removeItem(fromSlot);
            if (anyBag && isBagParentSlot(fromSlot) && isBagParentSlot(toSlot)) {
                relocateBagContents(fromSlot, toSlot);
            }
        }
        if (bridge_) {
            bridge_->pushIntent(eqt::events::MoveItemIntent{fromSlot, toSlot, /*quantity=*/0});
            if (isEquipmentSlot(fromSlot)) {
                bridge_->pushIntent(eqt::events::EquipmentChangedIntent{fromSlot});
            }
            if (isEquipmentSlot(toSlot)) {
                bridge_->pushIntent(eqt::events::EquipmentChangedIntent{toSlot});
            }
        }
    } else {
        // Both endpoints are cursor slots (e.g. /equip 30 33) -- nothing
        // meaningful to do locally; fall through to the same wire request
        // as before for parity with pre-T71 behaviour.
        if (bridge_) {
            bridge_->pushIntent(eqt::events::MoveItemIntent{fromSlot, toSlot, /*quantity=*/0});
        }
    }

    outcome.success = true;
    outcome.message = "Moving item: " + describeSlot(fromSlot) + " -> " + describeSlot(toSlot);
    return outcome;
}

} // namespace inventory
} // namespace eqt
