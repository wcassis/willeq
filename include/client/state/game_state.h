#pragma once

#include "client/state/event_bus.h"
#include "client/state/player_state.h"
#include "client/state/entity_manager.h"
#include "client/state/world_state.h"
#include "client/state/combat_state.h"
#include "client/state/group_state.h"
#include "client/state/door_state.h"

#include <memory>

namespace eqt {
namespace state {

/**
 * GameState - Central container for all game state.
 *
 * This class aggregates all the individual state managers and provides
 * a single point of access to the game state. It owns the EventBus and
 * connects it to all state managers for event notification.
 *
 * Usage:
 *   GameState state;
 *
 *   // Access player state
 *   state.player().setPosition(100, 200, 300);
 *
 *   // Access entities
 *   state.entities().addEntity(entity);
 *
 *   // Subscribe to events
 *   state.events().subscribe([](const GameEvent& event) {
 *       // Handle event
 *   });
 *
 * The GameState owns all state objects and the EventBus. When GameState
 * is destroyed, all state is cleaned up.
 */
class GameState {
public:
    GameState();
    ~GameState() = default;

    // Non-copyable, movable
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    GameState(GameState&&) = default;
    GameState& operator=(GameState&&) = default;

    // ========== State Access ==========

    /**
     * Get the event bus for subscribing to state changes.
     */
    EventBus& events() { return m_eventBus; }
    const EventBus& events() const { return m_eventBus; }

    /**
     * Get the player state.
     */
    PlayerState& player() { return m_playerState; }
    const PlayerState& player() const { return m_playerState; }

    /**
     * Get the entity manager.
     */
    EntityManager& entities() { return m_entityManager; }
    const EntityManager& entities() const { return m_entityManager; }

    /**
     * Get the world state.
     */
    WorldState& world() { return m_worldState; }
    const WorldState& world() const { return m_worldState; }

    /**
     * Get the combat state.
     */
    CombatState& combat() { return m_combatState; }
    const CombatState& combat() const { return m_combatState; }

    /**
     * Get the group state.
     */
    GroupState& group() { return m_groupState; }
    const GroupState& group() const { return m_groupState; }

    /**
     * Get the door state.
     */
    DoorState& doors() { return m_doorState; }
    const DoorState& doors() const { return m_doorState; }

    // ========== Convenience Methods ==========

    /**
     * Check if the player is fully zoned in and ready.
     */
    bool isFullyZonedIn() const {
        return m_worldState.isFullyZonedIn();
    }

    /**
     * Get the player's current position.
     */
    glm::vec3 playerPosition() const {
        return m_playerState.position();
    }

    /**
     * Get the current zone name.
     */
    const std::string& currentZoneName() const {
        return m_worldState.zoneName();
    }

    // ========== Reset/Cleanup ==========

    /**
     * Reset all state for a zone change.
     * Clears entities, doors, and resets zone state.
     */
    void resetForZoneChange();

    /**
     * Clear all state completely.
     */
    void clearAll();

private:
    // Event bus (owned, shared with all state managers)
    EventBus m_eventBus;

    // State managers
    PlayerState m_playerState;
    EntityManager m_entityManager;
    WorldState m_worldState;
    CombatState m_combatState;
    GroupState m_groupState;
    DoorState m_doorState;
};

} // namespace state
} // namespace eqt
