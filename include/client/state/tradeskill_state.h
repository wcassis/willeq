#pragma once

#include <cstdint>
#include <string>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

/**
 * TradeskillState - Contains tradeskill container state data.
 *
 * This class encapsulates state related to tradeskill containers including
 * world objects (forges, looms, etc.) and inventory containers (tradeskill bags).
 *
 * Only one container can be open at a time - either a world object or an
 * inventory container.
 */
class TradeskillState {
public:
    TradeskillState();
    ~TradeskillState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Container Status ==========

    /**
     * Check if any tradeskill container is currently open.
     */
    bool isContainerOpen() const;

    /**
     * Check if the open container is a world object (forge, loom, etc.).
     */
    bool isWorldContainer() const { return m_activeObjectId != 0; }

    /**
     * Check if the open container is an inventory container (bag).
     */
    bool isInventoryContainer() const { return m_activeInventorySlot >= 0; }

    // ========== World Object Container ==========

    /**
     * Get the active world object ID (drop ID).
     * @return Object ID, or 0 if no world container is open
     */
    uint32_t activeObjectId() const { return m_activeObjectId; }

    /**
     * Get the container name.
     */
    const std::string& containerName() const { return m_containerName; }

    /**
     * Get the container type (see tradeskill container types).
     */
    uint8_t containerType() const { return m_containerType; }

    /**
     * Get the number of slots in the container.
     */
    uint8_t slotCount() const { return m_slotCount; }

    /**
     * Open a world object container (forge, loom, etc.).
     * Fires TradeskillContainerOpened event.
     * @param dropId The world object's drop ID
     * @param name Name of the container
     * @param type Container type
     * @param slots Number of slots in the container
     */
    void openWorldContainer(uint32_t dropId, const std::string& name, uint8_t type, uint8_t slots);

    // ========== Inventory Container ==========

    /**
     * Get the active inventory slot.
     * @return Inventory slot, or -1 if no inventory container is open
     */
    int16_t activeInventorySlot() const { return m_activeInventorySlot; }

    /**
     * Open an inventory container (tradeskill bag).
     * Fires TradeskillContainerOpened event.
     * @param slot Inventory slot of the container
     * @param name Name of the container
     * @param type Container/bag type
     * @param slots Number of slots in the container
     */
    void openInventoryContainer(int16_t slot, const std::string& name, uint8_t type, uint8_t slots);

    // ========== Close/Reset ==========

    /**
     * Close the current container.
     * Fires TradeskillContainerClosed event.
     */
    void closeContainer();

    /**
     * Reset all state (for zone changes, logout, etc.).
     */
    void reset();

private:
    void fireTradeskillContainerOpenedEvent();
    void fireTradeskillContainerClosedEvent();

    EventBus* m_eventBus = nullptr;

    // World object container state
    uint32_t m_activeObjectId = 0;

    // Inventory container state
    int16_t m_activeInventorySlot = -1;

    // Shared container properties
    std::string m_containerName;
    uint8_t m_containerType = 0;
    uint8_t m_slotCount = 0;
};

} // namespace state
} // namespace eqt
