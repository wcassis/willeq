#include "client/door_state_manager.h"
#include <cmath>

namespace EQT {

DoorStateManager::DoorStateManager() = default;

void DoorStateManager::addDoor(const DoorState& door)
{
    doors_[door.doorId] = door;
}

void DoorStateManager::setDoorState(uint8_t doorId, bool open)
{
    auto it = doors_.find(doorId);
    if (it != doors_.end()) {
        it->second.isOpen = open;
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
