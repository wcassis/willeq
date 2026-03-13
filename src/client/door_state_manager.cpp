#include "client/door_state_manager.h"
#include <cmath>
#include <limits>

namespace EQT {

DoorStateManager::DoorStateManager() = default;

void DoorStateManager::addDoor(const DoorState& door)
{
    doors_[door.doorId] = door;
    if (doorSpawnCallback_) {
        doorSpawnCallback_(door);
    }
}

void DoorStateManager::setDoorState(uint8_t doorId, bool open)
{
    auto it = doors_.find(doorId);
    if (it != doors_.end()) {
        if (it->second.isOpen != open) {
            it->second.isOpen = open;
            if (doorStateChangeCallback_) {
                doorStateChangeCallback_(doorId, open);
            }
        }
        // Remove from pending clicks when we get a state update
        pendingClicks_.erase(doorId);
    }
}

const DoorState* DoorStateManager::getDoor(uint8_t doorId) const
{
    auto it = doors_.find(doorId);
    if (it != doors_.end()) {
        return &it->second;
    }
    return nullptr;
}

void DoorStateManager::clear()
{
    doors_.clear();
    pendingClicks_.clear();
}

bool DoorStateManager::hasDoor(uint8_t doorId) const
{
    return doors_.find(doorId) != doors_.end();
}

const DoorState* DoorStateManager::getNearestDoor(float x, float y, float z) const
{
    const DoorState* nearest = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();

    for (const auto& [id, door] : doors_) {
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

std::vector<const DoorState*> DoorStateManager::getDoorsInRadius(
    float x, float y, float z, float radius) const
{
    std::vector<const DoorState*> result;
    float radiusSq = radius * radius;
    for (const auto& [id, door] : doors_) {
        float dx = door.x - x;
        float dy = door.y - y;
        float dz = door.z - z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= radiusSq) {
            result.push_back(&door);
        }
    }
    return result;
}

} // namespace EQT
