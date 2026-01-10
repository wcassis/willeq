#include "client/state/combat_state.h"
#include "client/state/event_bus.h"

namespace eqt {
namespace state {

CombatState::CombatState() = default;

// ========== Targeting ==========

void CombatState::setTarget(uint16_t spawnId, const std::string& name, uint8_t hpPercent, uint8_t level) {
    bool changed = (m_targetId != spawnId);
    m_targetId = spawnId;
    m_targetName = name;
    m_targetHPPercent = hpPercent;
    m_targetLevel = level;
    if (changed) {
        fireTargetChangedEvent();
    }
}

void CombatState::clearTarget() {
    if (m_targetId != 0) {
        m_targetId = 0;
        m_targetName.clear();
        m_targetHPPercent = 100;
        m_targetLevel = 0;
        fireTargetChangedEvent();
    }
}

void CombatState::updateTargetHP(uint8_t hpPercent) {
    if (m_targetHPPercent != hpPercent) {
        m_targetHPPercent = hpPercent;
        // Could fire a TargetStatsChanged event here if needed
    }
}

void CombatState::updateTargetLevel(uint8_t level) {
    m_targetLevel = level;
}

// ========== Auto-Attack ==========

void CombatState::setAutoAttacking(bool attacking) {
    if (m_autoAttacking != attacking) {
        m_autoAttacking = attacking;
        fireCombatEvent();
    }
}

// ========== Reset ==========

void CombatState::reset() {
    clearTarget();
    m_autoAttacking = false;
    m_combatTarget.clear();
    m_combatStopDistance = 0.0f;
    m_inCombatMovement = false;
    m_lastSlainEntityName.clear();
}

// ========== Event Firing ==========

void CombatState::fireTargetChangedEvent() {
    if (m_eventBus) {
        // Target changed event would go here
        // For now, the TargetChanged event type is defined but we don't have
        // a corresponding data struct. This can be expanded as needed.
    }
}

void CombatState::fireCombatEvent() {
    if (m_eventBus) {
        // Combat state change event would go here
    }
}

} // namespace state
} // namespace eqt
