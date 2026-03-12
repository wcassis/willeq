#ifndef EQT_DOOR_STATE_MANAGER_H
#define EQT_DOOR_STATE_MANAGER_H

#include "client/door_state.h"
#include <cstdint>
#include <map>
#include <vector>

namespace EQT {

// Game-state manager for doors. No renderer dependencies.
// Created at startup with pre-allocated storage. Provides door
// position/state queries for pathing, LOS, and collision systems
// independent of any renderer.
class DoorStateManager {
public:
    DoorStateManager();

    // Add a door (from SpawnDoor packet)
    void addDoor(const DoorState& door);

    // Update door open/closed state (from MoveDoor packet)
    void setDoorState(uint8_t doorId, bool open);

    // Lookup by ID (returns nullptr if not found)
    const DoorState* getDoor(uint8_t doorId) const;

    // Full read access
    const std::map<uint8_t, DoorState>& getAllDoors() const { return doors_; }

    // Zone change cleanup
    void clear();

    // Door count
    size_t getDoorCount() const { return doors_.size(); }

    // Find doors within radius of a point (for pathing/collision queries)
    std::vector<const DoorState*> getDoorsInRadius(float x, float y, float z, float radius) const;

private:
    std::map<uint8_t, DoorState> doors_;
};

} // namespace EQT

#endif // EQT_DOOR_STATE_MANAGER_H
