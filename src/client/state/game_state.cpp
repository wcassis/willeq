#include "client/state/game_state.h"

namespace eqt {
namespace state {

GameState::GameState() {
    // Connect event bus to all state managers
    m_playerState.setEventBus(&m_eventBus);
    m_entityManager.setEventBus(&m_eventBus);
    m_worldState.setEventBus(&m_eventBus);
    m_combatState.setEventBus(&m_eventBus);
    m_groupState.setEventBus(&m_eventBus);
    m_doorState.setEventBus(&m_eventBus);
}

void GameState::resetForZoneChange() {
    // Clear zone-specific state
    m_entityManager.clear();
    m_doorState.clear();
    m_worldState.resetZoneState();
    m_combatState.reset();

    // Player state persists across zones but we clear movement target
    m_playerState.clearMovementTarget();
    m_playerState.clearFollowTarget();
    m_playerState.setMoving(false);

    // Group state persists across zones but we update inZone flags
    // (this will be handled by the zone entry process)
}

void GameState::clearAll() {
    // Clear all state
    m_entityManager.clear();
    m_doorState.clear();
    m_worldState.resetZoneState();
    m_combatState.reset();
    m_groupState.clearGroup();

    // Reset player state to defaults
    m_playerState = PlayerState();
    m_playerState.setEventBus(&m_eventBus);

    // Clear all event listeners
    m_eventBus.clear();

    // Reconnect event bus after clearing
    m_playerState.setEventBus(&m_eventBus);
    m_entityManager.setEventBus(&m_eventBus);
    m_worldState.setEventBus(&m_eventBus);
    m_combatState.setEventBus(&m_eventBus);
    m_groupState.setEventBus(&m_eventBus);
    m_doorState.setEventBus(&m_eventBus);
}

} // namespace state
} // namespace eqt
