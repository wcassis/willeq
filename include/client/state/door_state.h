#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <functional>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

/**
 * Door - Represents a door in the game world.
 *
 * This struct matches the Door struct in eq.h but is defined here to
 * decouple the state layer from the EverQuest class.
 */
struct Door {
    uint8_t doorId;           // Unique door identifier
    std::string name;         // Model name (matches zone object)
    float x, y, z;            // Position in EQ coords
    float heading;            // Closed rotation (0-360 degrees)
    uint32_t incline;         // Open rotation offset
    uint16_t size;            // Scale (100 = normal)
    uint8_t opentype;         // Door behavior type
    uint8_t state;            // 0=closed, 1=open
    bool invertState;         // If true, door normally spawns open
    uint32_t doorParam;       // Lock type / key item ID

    bool isOpen() const { return state != 0; }
    bool isClosed() const { return state == 0; }
    bool isLocked() const { return doorParam != 0; }
};

/**
 * DoorState - Manages all doors in the current zone.
 *
 * This class handles door tracking including spawning, state changes,
 * and pending interactions. It fires events through the EventBus when
 * doors are added or their state changes.
 */
class DoorState {
public:
    DoorState();
    ~DoorState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Door Management ==========

    /**
     * Add a new door. Fires DoorSpawned event.
     * @param door The door to add
     * @return true if added, false if door ID already exists
     */
    bool addDoor(const Door& door);

    /**
     * Remove a door by ID.
     * @param doorId The door ID to remove
     * @return true if removed, false if not found
     */
    bool removeDoor(uint8_t doorId);

    /**
     * Update a door's state (open/closed). Fires DoorStateChanged event.
     * @param doorId The door ID
     * @param isOpen true = open, false = closed
     * @return true if updated, false if door not found
     */
    bool setDoorState(uint8_t doorId, bool isOpen);

    /**
     * Toggle a door's state.
     * @param doorId The door ID
     * @return true if toggled, false if door not found
     */
    bool toggleDoorState(uint8_t doorId);

    /**
     * Clear all doors.
     */
    void clear();

    // ========== Door Queries ==========

    /**
     * Get a door by ID.
     * @param doorId The door ID to look up
     * @return Pointer to door or nullptr if not found
     */
    const Door* getDoor(uint8_t doorId) const;

    /**
     * Get a mutable door by ID.
     * @param doorId The door ID to look up
     * @return Pointer to door or nullptr if not found
     */
    Door* getDoorMutable(uint8_t doorId);

    /**
     * Find a door by name.
     * @param name Door name to search for
     * @return Pointer to door or nullptr if not found
     */
    const Door* findDoorByName(const std::string& name) const;

    /**
     * Get all doors.
     * @return Reference to the door map
     */
    const std::map<uint8_t, Door>& getAllDoors() const { return m_doors; }

    /**
     * Get the number of doors.
     * @return Door count
     */
    size_t getDoorCount() const { return m_doors.size(); }

    /**
     * Check if a door exists.
     * @param doorId The door ID to check
     * @return true if door exists
     */
    bool hasDoor(uint8_t doorId) const;

    // ========== Spatial Queries ==========

    /**
     * Get doors within a certain range of a position.
     * @param x Center X position
     * @param y Center Y position
     * @param z Center Z position
     * @param range Maximum distance
     * @return Vector of door IDs within range
     */
    std::vector<uint8_t> getDoorsInRange(float x, float y, float z, float range) const;

    /**
     * Get the nearest door to a position.
     * @param x Center X position
     * @param y Center Y position
     * @param z Center Z position
     * @return Pointer to nearest door or nullptr if no doors
     */
    const Door* getNearestDoor(float x, float y, float z) const;

    // ========== Pending Door Clicks ==========

    // Track doors that user clicked, awaiting server response

    /**
     * Add a door to pending clicks.
     * @param doorId Door ID that was clicked
     */
    void addPendingClick(uint8_t doorId);

    /**
     * Remove a door from pending clicks.
     * @param doorId Door ID to remove
     */
    void removePendingClick(uint8_t doorId);

    /**
     * Check if a door click is pending.
     * @param doorId Door ID to check
     * @return true if click is pending
     */
    bool isClickPending(uint8_t doorId) const;

    /**
     * Clear all pending clicks.
     */
    void clearPendingClicks();

    /**
     * Get all pending door click IDs.
     * @return Set of pending door IDs
     */
    const std::set<uint8_t>& getPendingClicks() const { return m_pendingClicks; }

    // ========== Iteration ==========

    /**
     * Iterate over all doors.
     * @param callback Function to call for each door
     */
    void forEachDoor(std::function<void(const Door&)> callback) const;

private:
    void fireDoorSpawnedEvent(const Door& door);
    void fireDoorStateChangedEvent(uint8_t doorId, bool isOpen);

    float calculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const;

    EventBus* m_eventBus = nullptr;
    std::map<uint8_t, Door> m_doors;
    std::set<uint8_t> m_pendingClicks;
};

} // namespace state
} // namespace eqt
