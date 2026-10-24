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

void InventoryManager::processCharInventory(const EQ::Net::Packet& packet) {
    // OP_CharInventory contains multiple serialized items concatenated together
    // Each item is null-terminated
    LOG_DEBUG(MOD_UI, "InventoryManager Received CharInventory packet, size: {}", packet.Length());

    const char* data = reinterpret_cast<const char*>(packet.Data());
    size_t totalLen = packet.Length();
    size_t offset = 0;
    int itemCount = 0;
    int bagContentCount = 0;

    while (offset < totalLen) {
        // Find the end of this item (null terminator)
        size_t itemStart = offset;
        while (offset < totalLen && data[offset] != '\0') {
            offset++;
        }

        size_t itemLen = offset - itemStart;
        if (itemLen > 0) {
            // Parse this item and its bag contents
            int16_t slotId = SLOT_INVALID;
            std::map<int16_t, std::unique_ptr<ItemInstance>> bagContents;
            auto item = TitaniumItemParser::parseItem(data + itemStart, itemLen, slotId, &bagContents);

            if (item && slotId != SLOT_INVALID) {
                LOG_DEBUG(MOD_UI, "InventoryManager Parsed item: {} in slot {}", item->name, slotId);
                itemCount++;
                bagContentCount += ingestParsedItem(slotId, std::move(item), bagContents);
            }
        }

        // Skip the null terminator
        offset++;
    }

    LOG_DEBUG(MOD_UI, "InventoryManager Loaded {} items and {} bag contents from CharInventory", itemCount, bagContentCount);
    // Note: Items at CURSOR_SLOT are automatically handled by setItem() which adds to cursorQueue_
}

int16_t InventoryManager::processItemPacket(const EQ::Net::Packet& packet) {
    // The packet handed in by ZoneComponent::DispatchZonePacket() (and by the
    // loot path in eq_merchant.cpp, which forwards the same one) still
    // carries its 2-byte OPCODE, then ItemPacket_Struct: uint32 PacketType,
    // then the serialized item.
    //
    // T282: this read the type at offset 0 and the item at offset 4 -- two
    // bytes early on both, in a file where ZoneProcessVendorItemToUI() and
    // ZoneProcessTradePartnerItem() both correctly skip 2 + 4. It survived
    // because the stray "\0\0" prefix lands on instance field 0 and nothing
    // else... but instance field 0 is the STACK COUNT (titanium.cpp
    // SerializeItem), so from_chars() failed on it and every stacked item
    // arriving this way collapsed to a quantity of 1. The packet type was
    // likewise garbage, which is why nothing could dispatch on it.
    LOG_DEBUG(MOD_UI, "InventoryManager Received ItemPacket, size: {}", packet.Length());

    constexpr size_t kOpcodeBytes = 2;
    constexpr size_t kTypeBytes = 4;
    if (packet.Length() < kOpcodeBytes + kTypeBytes + 1) {
        LOG_DEBUG(MOD_UI, "ItemPacket too short");
        return SLOT_INVALID;
    }

    uint32_t packetType = packet.GetUInt32(kOpcodeBytes);

    // Rest is the serialized item (null-terminated)
    const char* itemData = reinterpret_cast<const char*>(packet.Data()) + kOpcodeBytes + kTypeBytes;
    size_t itemLen = packet.Length() - kOpcodeBytes - kTypeBytes;

    // Remove trailing null if present
    while (itemLen > 0 && itemData[itemLen - 1] == '\0') {
        itemLen--;
    }

    if (itemLen == 0) {
        LOG_DEBUG(MOD_UI, "ItemPacket has no item data");
        return SLOT_INVALID;
    }

    int16_t slotId = SLOT_INVALID;
    std::map<int16_t, std::unique_ptr<ItemInstance>> bagContents;
    auto item = TitaniumItemParser::parseItem(itemData, itemLen, slotId, &bagContents);

    // T282 -- eqemu zone/object.cpp Object::HandleClick sends a WORLD
    // tradeskill container's contents one packet per occupied index, and
    // SendItemPacket() is handed the container's BAG INDEX 0-9, not
    // 4000+index. Untranslated, index 0/1/2 IS equipment slot 0/1/2, so an
    // open forge holding anything used to overwrite the player's charm, ears
    // and head. Map it into the world range on arrival.
    if (packetType == EQT::ITEM_PACKET_WORLD_CONTAINER && slotId >= 0 && slotId < WORLD_COUNT) {
        LOG_DEBUG(MOD_UI, "InventoryManager World container index {} -> slot {}",
                  slotId, WORLD_BEGIN + slotId);
        slotId = static_cast<int16_t>(WORLD_BEGIN + slotId);
        if (item) {
            item->currentSlot = slotId;
        }
    }

    if (item && slotId != SLOT_INVALID) {
        LOG_DEBUG(MOD_UI, "InventoryManager Parsed item from ItemPacket: {} in slot {} (type: {})", item->name, slotId, packetType);
        // T255: an ItemPacket bag arrives with its contents serialised --
        // eqemu delivers every BANK item this way at zone-in (live leg, run
        // 2), and a bag pushed onto the cursor queue likewise. They were
        // never parsed here before, so bank bags always loaded empty.
        // ingestParsedItem() treats the packet as authoritative for the
        // bag's whole contents (stale range replaced, never stack-merged).
        ingestParsedItem(slotId, std::move(item), bagContents);
        return slotId;
    } else {
        LOG_DEBUG(MOD_UI, "Failed to parse item from ItemPacket");
        return SLOT_INVALID;
    }
}

int InventoryManager::ingestParsedItem(int16_t slotId, std::unique_ptr<ItemInstance> item,
                                       std::map<int16_t, std::unique_ptr<ItemInstance>>& bagContents) {
    const bool toCursor = isCursorSlot(slotId);
    const bool isBag = item && item->isContainer();
    setItem(slotId, std::move(item));
    // A serialised bag carries ALL of its contents: whatever the client held
    // under that parent before is stale. Lift it out rather than let
    // setItem()'s same-id stack merge double a count. (A cursor-bound bag's
    // range is the front's; only touch it when this bag IS the front.)
    if (isBag && (toCursor ? cursorQueue_.size() == 1 : isBagParentSlot(slotId))) {
        auto stale = takeBagContents(toCursor ? CURSOR_SLOT : slotId);
        if (!stale.empty()) {
            LOG_DEBUG(MOD_UI, "InventoryManager Replacing {} stale bag contents under parent slot {}",
                      stale.size(), toCursor ? CURSOR_SLOT : slotId);
        }
    }
    if (bagContents.empty()) {
        return 0;
    }
    int stored = 0;
    if (toCursor && cursorQueue_.size() > 1) {
        // T255 -- a bag queued BEHIND the front: its contents cannot share
        // the cursor bag range with the front's, so hold them against the
        // queued instance until it is promoted (promoteCursorFrontContents).
        const ItemInstance* owner = cursorQueue_.back().get();
        TakenBagContents held;
        for (auto& [bagSlotId, bagItem] : bagContents) {
            int16_t index = getContainerIndex(bagSlotId);
            if (index == SLOT_INVALID || !bagItem) {
                continue;
            }
            cacheItemById(*bagItem);
            held.emplace_back(index, std::move(bagItem));
            stored++;
        }
        LOG_DEBUG(MOD_UI, "InventoryManager Cursor bag '{}' queued at position {} holds {} contents (deferred)",
                  owner->name, cursorQueue_.size() - 1, held.size());
        queuedCursorBagContents_[owner] = std::move(held);
    } else {
        for (auto& [bagSlotId, bagItem] : bagContents) {
            setItem(bagSlotId, std::move(bagItem));
            stored++;
        }
    }
    bagContents.clear();
    return stored;
}

void InventoryManager::processMoveItemResponse(const EQ::Net::Packet& packet) {
    // Server sends back MoveItem_Struct to confirm the move or indicate server-side changes
    // MoveItem_Struct: from_slot(4), to_slot(4), number_in_stack(4) = 12 bytes
    // Packet: opcode(2) + data(12) = 14 bytes
    LOG_DEBUG(MOD_UI, "InventoryManager Received MoveItem response, size: {}", packet.Length());

    if (packet.Length() < 14) {
        LOG_WARN(MOD_UI, "MoveItem response too short: {} bytes", packet.Length());
        return;
    }

    // Parse the MoveItem struct (after 2-byte opcode)
    int32_t fromSlot = static_cast<int32_t>(packet.GetUInt32(2));
    int32_t toSlot = static_cast<int32_t>(packet.GetUInt32(6));
    uint32_t quantity = packet.GetUInt32(10);

    LOG_DEBUG(MOD_UI, "InventoryManager MoveItem: from={} to={} qty={}", fromSlot, toSlot, quantity);

    // Check for item deletion (to_slot == -1 / 0xFFFFFFFF)
    // This happens when:
    // 1. A spell scroll is consumed after successful scribing
    // 2. Tradeskill combine consumes ingredients
    // 3. Server explicitly deletes an item
    if (toSlot == -1) {
        int16_t slot = static_cast<int16_t>(fromSlot);
        LOG_INFO(MOD_UI, "Item deleted from slot {} via MoveItem (to=-1)", slot);

        // Check if this deletion is related to our pending scribe
        if (isHoldingSpellForScribe() && getScribeSourceSlot() == slot) {
            LOG_DEBUG(MOD_UI, "Scroll consumed after scribing from slot {}", slot);
            clearSpellForScribe();
        }

        // Remove the item from inventory
        removeItem(slot);
    }
    // Normal move operations are handled client-side
    // Server will send OP_ItemPacket if it needs to correct our state
}

void InventoryManager::processDeleteItemResponse(const EQ::Net::Packet& packet) {
    // B1140: a server-sent OP_DeleteItem is NOT an echo of the client's own
    // destroy (eqemu Handle_OP_DeleteItem deletes with client_update=false
    // and sends nothing back). It is eqemu Client::DeleteItemInInventory
    // taking ONE unit off a stack that survives (stackable, or arrows): a
    // consumed spell component, an arrow, a combine. A stack that empties
    // arrives as OP_MoveItem to=-1 instead (processMoveItemResponse). The
    // old body "trusted our client-side deletion" -- there is none for a
    // server-side consumption, so a 2-stack of reagents stayed 2 forever.
    // DeleteItem_Struct: from_slot(4), to_slot(4) = -1, number_in_stack(4)
    // = 0xFFFFFFFF; one packet per unit (eqemu queues it `quantity` times).
    LOG_DEBUG(MOD_UI, "InventoryManager Received DeleteItem, size: {}", packet.Length());
    if (packet.Length() < 6) {
        return;
    }
    const int16_t slot = static_cast<int16_t>(static_cast<int32_t>(packet.GetUInt32(2)));
    ItemInstance* item = getItemMutable(slot);
    if (!item) {
        LOG_DEBUG(MOD_UI, "InventoryManager DeleteItem: slot {} already empty", slot);
        return;
    }
    if (item->stackable && item->quantity > 1) {
        --item->quantity;
        ++changeCounter_;
        LOG_INFO(MOD_UI, "Item stack in slot {} ({}) decremented to {} via DeleteItem", slot, item->name, item->quantity);
    } else {
        LOG_INFO(MOD_UI, "Item deleted from slot {} ({}) via DeleteItem", slot, item->name);
        removeItem(slot);
    }
}

void InventoryManager::dumpInventory() const {
    LOG_DEBUG(MOD_UI, "=== Inventory Dump ===");

    LOG_DEBUG(MOD_UI, "Equipment:");
    for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            LOG_DEBUG(MOD_UI, "  [{}] {}: {} (ID: {})", slot, getSlotName(slot), item->name, item->itemId);
        }
    }

    LOG_DEBUG(MOD_UI, "General:");
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            if (item->isContainer()) {
                LOG_DEBUG(MOD_UI, "  [{}]: {} (Bag, {} slots)", slot, item->name, (int)item->bagSlots);
                // Show bag contents
                auto bagSlots = getBagContentsSlots(slot);
                for (int16_t bagSlot : bagSlots) {
                    const ItemInstance* bagItem = getItem(bagSlot);
                    if (bagItem) {
                        LOG_DEBUG(MOD_UI, "    [{}]: {}", bagSlot, bagItem->name);
                    }
                }
            } else {
                LOG_DEBUG(MOD_UI, "  [{}]: {}", slot, item->name);
            }
        }
    }

    if (!cursorQueue_.empty()) {
        LOG_DEBUG(MOD_UI, "Cursor Queue ({} items):", cursorQueue_.size());
        int idx = 0;
        for (const auto& item : cursorQueue_) {
            if (idx == 0) {
                LOG_DEBUG(MOD_UI, "  [{}]: {} (active, from slot {})", idx, item->name, cursorSourceSlot_);
            } else {
                auto held = queuedCursorBagContents_.find(item.get());
                size_t heldCount = held == queuedCursorBagContents_.end() ? 0 : held->second.size();
                LOG_DEBUG(MOD_UI, "  [{}]: {} ({} queued bag contents held)", idx, item->name, heldCount);
            }
            idx++;
        }
    }

    LOG_DEBUG(MOD_UI, "======================");
}

} // namespace inventory
} // namespace eqt
