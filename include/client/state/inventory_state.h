#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

// Slot counts matching inventory_constants.h
constexpr int16_t INV_EQUIPMENT_COUNT = 22;
constexpr int16_t INV_GENERAL_COUNT = 8;
constexpr int16_t INV_BANK_COUNT = 16;
constexpr int16_t INV_SHARED_BANK_COUNT = 2;
constexpr int16_t INV_BAG_SLOT_COUNT = 10;

/**
 * InventoryState - Contains inventory state data for event-driven updates.
 *
 * This class tracks simplified inventory state that can be synchronized from
 * the InventoryManager. It provides slot occupancy tracking and summary
 * information for UI updates without duplicating full item data.
 *
 * The InventoryManager remains the source of truth for detailed item data.
 * This state class provides event notifications for inventory changes.
 */
class InventoryState {
public:
    InventoryState();
    ~InventoryState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Slot Occupancy ==========

    /**
     * Check if an equipment slot has an item.
     * @param slot Equipment slot (0-21)
     */
    bool hasEquipment(int16_t slot) const;

    /**
     * Check if a general inventory slot has an item.
     * @param slot General slot index (0-7)
     */
    bool hasGeneralItem(int16_t slot) const;

    /**
     * Check if a bank slot has an item.
     * @param slot Bank slot index (0-15)
     */
    bool hasBankItem(int16_t slot) const;

    /**
     * Set equipment slot occupancy.
     * Fires InventoryChanged event if changed.
     */
    void setEquipmentOccupied(int16_t slot, bool occupied);

    /**
     * Set general slot occupancy.
     * Fires InventoryChanged event if changed.
     */
    void setGeneralOccupied(int16_t slot, bool occupied);

    /**
     * Set bank slot occupancy.
     * Fires InventoryChanged event if changed.
     */
    void setBankOccupied(int16_t slot, bool occupied);

    // ========== Cursor State ==========

    /**
     * Check if there's an item on cursor.
     */
    bool hasCursorItem() const { return m_hasCursorItem; }

    /**
     * Set cursor item state.
     * Fires CursorItemChanged event if changed.
     */
    void setCursorItem(bool hasItem);

    /**
     * Get number of items in cursor queue.
     */
    uint8_t cursorQueueSize() const { return m_cursorQueueSize; }

    /**
     * Set cursor queue size.
     */
    void setCursorQueueSize(uint8_t size);

    // ========== Bag State ==========

    /**
     * Check if a general slot contains a bag.
     * @param slot General slot index (0-7)
     */
    bool isBag(int16_t slot) const;

    /**
     * Get bag size for a general slot.
     * @param slot General slot index (0-7)
     * @return Bag size (0-10), 0 if not a bag
     */
    uint8_t bagSize(int16_t slot) const;

    /**
     * Set bag info for a general slot.
     * @param slot General slot index (0-7)
     * @param size Bag size (0 = not a bag, 1-10 = bag slots)
     */
    void setBagInfo(int16_t slot, uint8_t size);

    // ========== Summary Counts ==========

    /**
     * Get count of occupied equipment slots.
     */
    uint8_t equippedCount() const;

    /**
     * Get count of occupied general slots.
     */
    uint8_t generalItemCount() const;

    /**
     * Get count of occupied bank slots.
     */
    uint8_t bankItemCount() const;

    /**
     * Get count of free general slots.
     */
    uint8_t freeGeneralSlots() const;

    /**
     * Get count of free bag slots across all bags.
     */
    uint16_t freeBagSlots() const;

    // ========== Equipment Stats Summary ==========

    /**
     * Get total AC from equipment.
     */
    int32_t equipmentAC() const { return m_equipmentAC; }

    /**
     * Get total ATK from equipment.
     */
    int32_t equipmentATK() const { return m_equipmentATK; }

    /**
     * Get total HP from equipment.
     */
    int32_t equipmentHP() const { return m_equipmentHP; }

    /**
     * Get total mana from equipment.
     */
    int32_t equipmentMana() const { return m_equipmentMana; }

    /**
     * Get total weight from all inventory.
     */
    float totalWeight() const { return m_totalWeight; }

    /**
     * Update equipment stats summary.
     * Fires EquipmentStatsChanged event if changed.
     */
    void setEquipmentStats(int32_t ac, int32_t atk, int32_t hp, int32_t mana, float weight);

    // ========== Reset ==========

    /**
     * Reset all inventory state.
     */
    void reset();

    /**
     * Clear bank slots (for bank window close).
     */
    void clearBank();

private:
    void fireInventoryChangedEvent(int16_t slot);
    void fireCursorItemChangedEvent();
    void fireEquipmentStatsChangedEvent();

    EventBus* m_eventBus = nullptr;

    // Equipment slot occupancy (22 slots: 0-21)
    std::array<bool, INV_EQUIPMENT_COUNT> m_equipmentOccupied = {};

    // General slot occupancy (8 slots: 0-7)
    std::array<bool, INV_GENERAL_COUNT> m_generalOccupied = {};

    // Bag sizes for general slots (0 = not a bag)
    std::array<uint8_t, INV_GENERAL_COUNT> m_bagSizes = {};

    // Bank slot occupancy (16 slots)
    std::array<bool, INV_BANK_COUNT> m_bankOccupied = {};

    // Cursor state
    bool m_hasCursorItem = false;
    uint8_t m_cursorQueueSize = 0;

    // Equipment stats summary
    int32_t m_equipmentAC = 0;
    int32_t m_equipmentATK = 0;
    int32_t m_equipmentHP = 0;
    int32_t m_equipmentMana = 0;
    float m_totalWeight = 0.0f;
};

} // namespace state
} // namespace eqt
