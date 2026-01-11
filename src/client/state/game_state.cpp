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
    m_petState.setEventBus(&m_eventBus);
    m_tradeskillState.setEventBus(&m_eventBus);
    m_inventoryState.setEventBus(&m_eventBus);
    m_spellState.setEventBus(&m_eventBus);
}

void GameState::resetForZoneChange() {
    // Clear zone-specific state
    m_entityManager.clear();
    m_doorState.clear();
    m_worldState.resetZoneState();
    m_combatState.reset();
    m_petState.reset();
    m_tradeskillState.reset();

    // Player state persists across zones but we clear movement target
    m_playerState.clearMovementTarget();
    m_playerState.clearFollowTarget();
    m_playerState.setMoving(false);

    // Clear NPC interaction state (vendor, banker, trainer, looting)
    m_playerState.clearVendor();
    m_playerState.clearBanker();
    m_playerState.clearTrainer();
    m_playerState.clearLootingCorpse();

    // Clear bank inventory state (bank closes on zone)
    m_inventoryState.clearBank();

    // Spells and inventory persist across zones but casting state is reset
    m_spellState.clearCasting();

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
    m_petState.reset();
    m_tradeskillState.reset();
    m_inventoryState.reset();
    m_spellState.reset();

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
    m_petState.setEventBus(&m_eventBus);
    m_tradeskillState.setEventBus(&m_eventBus);
    m_inventoryState.setEventBus(&m_eventBus);
    m_spellState.setEventBus(&m_eventBus);
}

} // namespace state
} // namespace eqt
