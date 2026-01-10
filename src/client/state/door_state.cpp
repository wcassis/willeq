#include "client/state/door_state.h"
#include "client/state/event_bus.h"
#include <cmath>
#include <limits>

namespace eqt {
namespace state {

DoorState::DoorState() = default;

// ========== Door Management ==========

bool DoorState::addDoor(const Door& door) {
    if (m_doors.find(door.doorId) != m_doors.end()) {
        return false;  // Already exists
    }
    m_doors[door.doorId] = door;
    fireDoorSpawnedEvent(door);
    return true;
}

bool DoorState::removeDoor(uint8_t doorId) {
    auto it = m_doors.find(doorId);
    if (it == m_doors.end()) {
        return false;
    }
    m_doors.erase(it);
    m_pendingClicks.erase(doorId);
    return true;
}

bool DoorState::setDoorState(uint8_t doorId, bool isOpen) {
    auto it = m_doors.find(doorId);
    if (it == m_doors.end()) {
        return false;
    }

    uint8_t newState = isOpen ? 1 : 0;
    if (it->second.state != newState) {
        it->second.state = newState;
        fireDoorStateChangedEvent(doorId, isOpen);
    }

    // Remove from pending clicks when we get a state update
    m_pendingClicks.erase(doorId);
    return true;
}

bool DoorState::toggleDoorState(uint8_t doorId) {
    auto it = m_doors.find(doorId);
    if (it == m_doors.end()) {
        return false;
    }

    bool newState = !it->second.isOpen();
    return setDoorState(doorId, newState);
}

void DoorState::clear() {
    m_doors.clear();
    m_pendingClicks.clear();
}

// ========== Door Queries ==========

const Door* DoorState::getDoor(uint8_t doorId) const {
    auto it = m_doors.find(doorId);
    if (it == m_doors.end()) {
        return nullptr;
    }
    return &it->second;
}

Door* DoorState::getDoorMutable(uint8_t doorId) {
    auto it = m_doors.find(doorId);
    if (it == m_doors.end()) {
        return nullptr;
    }
    return &it->second;
}

const Door* DoorState::findDoorByName(const std::string& name) const {
    for (const auto& pair : m_doors) {
        if (pair.second.name == name) {
            return &pair.second;
        }
    }
    return nullptr;
}

bool DoorState::hasDoor(uint8_t doorId) const {
    return m_doors.find(doorId) != m_doors.end();
}

// ========== Spatial Queries ==========

std::vector<uint8_t> DoorState::getDoorsInRange(float x, float y, float z, float range) const {
    std::vector<uint8_t> result;
    float rangeSq = range * range;

    for (const auto& pair : m_doors) {
        const Door& door = pair.second;
        float dx = door.x - x;
        float dy = door.y - y;
        float dz = door.z - z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= rangeSq) {
            result.push_back(pair.first);
        }
    }
    return result;
}

const Door* DoorState::getNearestDoor(float x, float y, float z) const {
    const Door* nearest = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();

    for (const auto& pair : m_doors) {
        const Door& door = pair.second;
        float dx = door.x - x;
        float dy = door.y - y;
        float dz = door.z - z;
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearest = &door;
        }
    }
    return nearest;
}

// ========== Pending Door Clicks ==========

void DoorState::addPendingClick(uint8_t doorId) {
    m_pendingClicks.insert(doorId);
}

void DoorState::removePendingClick(uint8_t doorId) {
    m_pendingClicks.erase(doorId);
}

bool DoorState::isClickPending(uint8_t doorId) const {
    return m_pendingClicks.find(doorId) != m_pendingClicks.end();
}

void DoorState::clearPendingClicks() {
    m_pendingClicks.clear();
}

// ========== Iteration ==========

void DoorState::forEachDoor(std::function<void(const Door&)> callback) const {
    for (const auto& pair : m_doors) {
        callback(pair.second);
    }
}

// ========== Event Firing ==========

void DoorState::fireDoorSpawnedEvent(const Door& door) {
    if (m_eventBus) {
        DoorSpawnedData data;
        data.doorId = door.doorId;
        data.name = door.name;
        data.x = door.x;
        data.y = door.y;
        data.z = door.z;
        data.heading = door.heading;
        data.state = door.state;
        m_eventBus->publish(GameEventType::DoorSpawned, data);
    }
}

void DoorState::fireDoorStateChangedEvent(uint8_t doorId, bool isOpen) {
    if (m_eventBus) {
        DoorStateChangedData data;
        data.doorId = doorId;
        data.isOpen = isOpen;
        m_eventBus->publish(GameEventType::DoorStateChanged, data);
    }
}

float DoorState::calculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace state
} // namespace eqt
