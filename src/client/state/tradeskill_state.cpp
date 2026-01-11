#include "client/state/tradeskill_state.h"
#include "client/state/event_bus.h"

namespace eqt {
namespace state {

TradeskillState::TradeskillState() = default;

bool TradeskillState::isContainerOpen() const {
    return m_activeObjectId != 0 || m_activeInventorySlot >= 0;
}

void TradeskillState::openWorldContainer(uint32_t dropId, const std::string& name, uint8_t type, uint8_t slots) {
    // Close any existing container first
    if (isContainerOpen()) {
        closeContainer();
    }

    m_activeObjectId = dropId;
    m_activeInventorySlot = -1;
    m_containerName = name;
    m_containerType = type;
    m_slotCount = slots;

    fireTradeskillContainerOpenedEvent();
}

void TradeskillState::openInventoryContainer(int16_t slot, const std::string& name, uint8_t type, uint8_t slots) {
    // Close any existing container first
    if (isContainerOpen()) {
        closeContainer();
    }

    m_activeObjectId = 0;
    m_activeInventorySlot = slot;
    m_containerName = name;
    m_containerType = type;
    m_slotCount = slots;

    fireTradeskillContainerOpenedEvent();
}

void TradeskillState::closeContainer() {
    if (!isContainerOpen()) {
        return;
    }

    fireTradeskillContainerClosedEvent();

    m_activeObjectId = 0;
    m_activeInventorySlot = -1;
    m_containerName.clear();
    m_containerType = 0;
    m_slotCount = 0;
}

void TradeskillState::reset() {
    // Don't fire events on reset - this is typically called during zone changes
    m_activeObjectId = 0;
    m_activeInventorySlot = -1;
    m_containerName.clear();
    m_containerType = 0;
    m_slotCount = 0;
}

void TradeskillState::fireTradeskillContainerOpenedEvent() {
    if (!m_eventBus) {
        return;
    }

    TradeskillContainerOpenedEvent event;
    event.isWorldObject = isWorldContainer();
    event.objectId = m_activeObjectId;
    event.inventorySlot = m_activeInventorySlot;
    event.containerName = m_containerName;
    event.containerType = m_containerType;
    event.slotCount = m_slotCount;

    m_eventBus->publish(GameEventType::TradeskillContainerOpened, event);
}

void TradeskillState::fireTradeskillContainerClosedEvent() {
    if (!m_eventBus) {
        return;
    }

    TradeskillContainerClosedEvent event;
    event.wasWorldObject = isWorldContainer();
    event.objectId = m_activeObjectId;
    event.inventorySlot = m_activeInventorySlot;

    m_eventBus->publish(GameEventType::TradeskillContainerClosed, event);
}

} // namespace state
} // namespace eqt
