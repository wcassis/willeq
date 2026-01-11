#include "client/state/inventory_state.h"
#include "client/state/event_bus.h"

namespace eqt {
namespace state {

InventoryState::InventoryState() {
    m_equipmentOccupied.fill(false);
    m_generalOccupied.fill(false);
    m_bagSizes.fill(0);
    m_bankOccupied.fill(false);
}

// ========== Slot Occupancy ==========

bool InventoryState::hasEquipment(int16_t slot) const {
    if (slot < 0 || slot >= INV_EQUIPMENT_COUNT) {
        return false;
    }
    return m_equipmentOccupied[slot];
}

bool InventoryState::hasGeneralItem(int16_t slot) const {
    if (slot < 0 || slot >= INV_GENERAL_COUNT) {
        return false;
    }
    return m_generalOccupied[slot];
}

bool InventoryState::hasBankItem(int16_t slot) const {
    if (slot < 0 || slot >= INV_BANK_COUNT) {
        return false;
    }
    return m_bankOccupied[slot];
}

void InventoryState::setEquipmentOccupied(int16_t slot, bool occupied) {
    if (slot < 0 || slot >= INV_EQUIPMENT_COUNT) {
        return;
    }
    if (m_equipmentOccupied[slot] != occupied) {
        m_equipmentOccupied[slot] = occupied;
        fireInventoryChangedEvent(slot);
    }
}

void InventoryState::setGeneralOccupied(int16_t slot, bool occupied) {
    if (slot < 0 || slot >= INV_GENERAL_COUNT) {
        return;
    }
    if (m_generalOccupied[slot] != occupied) {
        m_generalOccupied[slot] = occupied;
        // General slots are offset by 22 in actual slot IDs
        fireInventoryChangedEvent(22 + slot);
    }
}

void InventoryState::setBankOccupied(int16_t slot, bool occupied) {
    if (slot < 0 || slot >= INV_BANK_COUNT) {
        return;
    }
    if (m_bankOccupied[slot] != occupied) {
        m_bankOccupied[slot] = occupied;
        // Bank slots start at 2000
        fireInventoryChangedEvent(2000 + slot);
    }
}

// ========== Cursor State ==========

void InventoryState::setCursorItem(bool hasItem) {
    if (m_hasCursorItem != hasItem) {
        m_hasCursorItem = hasItem;
        fireCursorItemChangedEvent();
    }
}

void InventoryState::setCursorQueueSize(uint8_t size) {
    if (m_cursorQueueSize != size) {
        m_cursorQueueSize = size;
        // Update cursor item state based on queue
        setCursorItem(size > 0);
    }
}

// ========== Bag State ==========

bool InventoryState::isBag(int16_t slot) const {
    if (slot < 0 || slot >= INV_GENERAL_COUNT) {
        return false;
    }
    return m_bagSizes[slot] > 0;
}

uint8_t InventoryState::bagSize(int16_t slot) const {
    if (slot < 0 || slot >= INV_GENERAL_COUNT) {
        return 0;
    }
    return m_bagSizes[slot];
}

void InventoryState::setBagInfo(int16_t slot, uint8_t size) {
    if (slot < 0 || slot >= INV_GENERAL_COUNT) {
        return;
    }
    m_bagSizes[slot] = size;
}

// ========== Summary Counts ==========

uint8_t InventoryState::equippedCount() const {
    uint8_t count = 0;
    for (bool occupied : m_equipmentOccupied) {
        if (occupied) {
            ++count;
        }
    }
    return count;
}

uint8_t InventoryState::generalItemCount() const {
    uint8_t count = 0;
    for (bool occupied : m_generalOccupied) {
        if (occupied) {
            ++count;
        }
    }
    return count;
}

uint8_t InventoryState::bankItemCount() const {
    uint8_t count = 0;
    for (bool occupied : m_bankOccupied) {
        if (occupied) {
            ++count;
        }
    }
    return count;
}

uint8_t InventoryState::freeGeneralSlots() const {
    return INV_GENERAL_COUNT - generalItemCount();
}

uint16_t InventoryState::freeBagSlots() const {
    uint16_t total = 0;
    for (int i = 0; i < INV_GENERAL_COUNT; ++i) {
        if (m_bagSizes[i] > 0) {
            // For simplicity, assume bag slots are available
            // A more accurate implementation would track bag contents
            total += m_bagSizes[i];
        }
    }
    return total;
}

// ========== Equipment Stats ==========

void InventoryState::setEquipmentStats(int32_t ac, int32_t atk, int32_t hp, int32_t mana, float weight) {
    bool changed = (m_equipmentAC != ac || m_equipmentATK != atk ||
                    m_equipmentHP != hp || m_equipmentMana != mana ||
                    m_totalWeight != weight);

    m_equipmentAC = ac;
    m_equipmentATK = atk;
    m_equipmentHP = hp;
    m_equipmentMana = mana;
    m_totalWeight = weight;

    if (changed) {
        fireEquipmentStatsChangedEvent();
    }
}

// ========== Reset ==========

void InventoryState::reset() {
    m_equipmentOccupied.fill(false);
    m_generalOccupied.fill(false);
    m_bagSizes.fill(0);
    m_bankOccupied.fill(false);
    m_hasCursorItem = false;
    m_cursorQueueSize = 0;
    m_equipmentAC = 0;
    m_equipmentATK = 0;
    m_equipmentHP = 0;
    m_equipmentMana = 0;
    m_totalWeight = 0.0f;
}

void InventoryState::clearBank() {
    m_bankOccupied.fill(false);
}

// ========== Event Firing ==========

void InventoryState::fireInventoryChangedEvent(int16_t slot) {
    if (!m_eventBus) {
        return;
    }

    InventorySlotChangedData event;
    event.slotId = slot;
    event.hasItem = true;  // Caller should update this based on context
    event.itemId = 0;
    m_eventBus->publish(GameEventType::InventorySlotChanged, event);
}

void InventoryState::fireCursorItemChangedEvent() {
    if (!m_eventBus) {
        return;
    }

    CursorItemChangedData event;
    event.hasCursorItem = m_hasCursorItem;
    event.queueSize = m_cursorQueueSize;
    m_eventBus->publish(GameEventType::CursorItemChanged, event);
}

void InventoryState::fireEquipmentStatsChangedEvent() {
    if (!m_eventBus) {
        return;
    }

    EquipmentStatsChangedData event;
    event.ac = m_equipmentAC;
    event.atk = m_equipmentATK;
    event.hp = m_equipmentHP;
    event.mana = m_equipmentMana;
    event.weight = m_totalWeight;
    m_eventBus->publish(GameEventType::EquipmentStatsChanged, event);
}

} // namespace state
} // namespace eqt
