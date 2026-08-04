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
        // T39: this CombatState instance is fed by Application::syncCombatState()
        // (application.cpp), which mirrors the authoritative CombatManager's
        // target id/HP/name/level -- CombatManager remains the source of
        // truth, this only publishes what it was just told. This does NOT
        // double-publish the T28-era concern: eq_bridge.cpp's three
        // TargetChanged pushes go onto the GameStateBridge (consumed by
        // IrrlichtBridge/ConsoleBridge), a channel entirely disjoint from
        // this EventBus (consumed by ConsoleRenderer, subscribed in
        // HeadlessMode via Application::m_gameState -- see
        // refactor/deferred.md "Dead parallel state copies in the state
        // layer, WITH LIVE READERS", 2026-08-04 correction). Nothing
        // forwards between the two channels.
        TargetChangedData data;
        data.spawnId = m_targetId;
        data.name = m_targetName;
        data.level = m_targetLevel;
        data.hpPercent = m_targetHPPercent;
        m_eventBus->publish(GameEventType::TargetChanged, data);
    }
}

void CombatState::fireCombatEvent() {
    if (m_eventBus) {
        // T39: setAutoAttacking() is now wired (Application::syncCombatState()
        // mirrors CombatManager::IsAutoAttackEnabled()), but this stays a
        // non-publishing stub: CombatEventData (event_bus.h:365) models a
        // single combat *round outcome* (Hit/Miss/Dodge/Parry/Block/
        // Riposte/CriticalHit/Death against a source/target pair with a
        // damage amount) -- there is no field of that struct, and no other
        // GameEventType in this header, that means "auto-attack toggled".
        // Reusing CombatEventData with a fabricated Type or invented damage
        // value to represent a toggle would be inventing data the caller
        // never provided, which is the trap this migration is explicitly
        // supposed to avoid (see refactor/deferred.md and
        // T39_state_layer_migration.md's inventory guidance, same
        // principle). isAutoAttacking() now reflects real state and can be
        // read directly; it has zero production readers today.
    }
}

} // namespace state
} // namespace eqt
