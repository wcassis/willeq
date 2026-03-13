#ifndef EQT_DOOR_STATE_MANAGER_H
#define EQT_DOOR_STATE_MANAGER_H

#include "client/door_state.h"
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace EQT {

// Callbacks for door state change notifications.
// Used by GameState to fire EventBus events without coupling this class
// to the EventBus directly.
using DoorSpawnCallback = std::function<void(const DoorState& door)>;
using DoorStateChangeCallback = std::function<void(uint8_t doorId, bool isOpen)>;

// Game-state manager for doors. No renderer dependencies.
// Created at startup with pre-allocated storage. Provides door
// position/state queries for pathing, LOS, and collision systems
// independent of any renderer.
class DoorStateManager {
public:
    DoorStateManager();

    // Set callbacks for event notification (wired by GameState)
    void setDoorSpawnCallback(DoorSpawnCallback cb) { doorSpawnCallback_ = std::move(cb); }
    void setDoorStateChangeCallback(DoorStateChangeCallback cb) { doorStateChangeCallback_ = std::move(cb); }

    // Add a door (from SpawnDoor packet). Fires spawn callback.
    void addDoor(const DoorState& door);

    // Update door open/closed state (from MoveDoor packet). Fires state change callback.
    void setDoorState(uint8_t doorId, bool open);

    // Lookup by ID (returns nullptr if not found)
    const DoorState* getDoor(uint8_t doorId) const;

    // Full read access
    const std::map<uint8_t, DoorState>& getAllDoors() const { return doors_; }

    // Zone change cleanup
    void clear();

    // Door count
    size_t getDoorCount() const { return doors_.size(); }

    // Check if a door exists
    bool hasDoor(uint8_t doorId) const;

    // Find the nearest door to a position (returns nullptr if no doors)
    const DoorState* getNearestDoor(float x, float y, float z) const;

    // Find doors within radius of a point (for pathing/collision queries)
    std::vector<const DoorState*> getDoorsInRadius(float x, float y, float z, float radius) const;

    // Pending click tracking (doors clicked, awaiting server response)
    void addPendingClick(uint8_t doorId) { pendingClicks_.insert(doorId); }
    void removePendingClick(uint8_t doorId) { pendingClicks_.erase(doorId); }
    bool isClickPending(uint8_t doorId) const { return pendingClicks_.count(doorId) > 0; }
    void clearPendingClicks() { pendingClicks_.clear(); }

private:
    std::map<uint8_t, DoorState> doors_;
    std::set<uint8_t> pendingClicks_;
    DoorSpawnCallback doorSpawnCallback_;
    DoorStateChangeCallback doorStateChangeCallback_;
};

} // namespace EQT

#endif // EQT_DOOR_STATE_MANAGER_H
