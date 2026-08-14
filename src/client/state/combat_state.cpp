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
        // T39/T104: this CombatState instance is fed by GameStateSyncBridge's
        // TargetChanged case (game_state_sync_bridge.cpp), itself published
        // from CombatManager::SetTarget()/ClearTarget() (combat.cpp) and
        // from eq_entities.cpp's target-HP re-publish -- CombatManager
        // remains the source of truth, this only publishes what it was just
        // told. (T104 replaced the prior feed, Application::syncCombatState(),
        // a per-frame poll, with these events -- see
        // refactor/T104_combat_poll_to_bridge.md.) This does NOT
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
        // T39/T104: setAutoAttacking() is now wired via GameStateSyncBridge's
        // AutoAttackChanged case (game_state_sync_bridge.cpp), fed from
        // CombatManager::EnableAutoAttack()/DisableAutoAttack() (combat.cpp)
        // -- T104 replaced the prior feed, Application::syncCombatState(), a
        // per-frame poll, with that typed event. This method itself stays a
        // non-publishing stub, deliberately: CombatEventData (event_bus.h)
        // models a single combat *round outcome* (Hit/Miss/Dodge/Parry/
        // Block/Riposte/CriticalHit/Death against a source/target pair with
        // a damage amount) -- there is no field of that struct, and no other
        // GameEventType meant for THIS EventBus (the one ConsoleRenderer
        // subscribes to), that means "auto-attack toggled". Reusing
        // CombatEventData with a fabricated Type or invented damage value to
        // represent a toggle would be inventing data the caller never
        // provided, the same trap T104's own new AutoAttackChanged (a
        // cross-thread bridge event, a different channel from this
        // EventBus) was added to avoid rather than repeat. isAutoAttacking()
        // now reflects real state and can be read directly; it has zero
        // production readers today.
    }
}

} // namespace state
} // namespace eqt
