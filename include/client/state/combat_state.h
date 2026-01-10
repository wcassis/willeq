#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

/**
 * CombatState - Contains combat-related state data.
 *
 * This class encapsulates state related to combat including targeting,
 * auto-attack, combat movement, and recent combat events.
 *
 * Note: The actual combat logic (damage calculation, attack handling) remains
 * in CombatManager. This class only holds the state.
 */
class CombatState {
public:
    CombatState();
    ~CombatState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Targeting ==========

    uint16_t targetId() const { return m_targetId; }
    const std::string& targetName() const { return m_targetName; }
    uint8_t targetHPPercent() const { return m_targetHPPercent; }
    uint8_t targetLevel() const { return m_targetLevel; }

    void setTarget(uint16_t spawnId, const std::string& name, uint8_t hpPercent = 100, uint8_t level = 0);
    void clearTarget();
    bool hasTarget() const { return m_targetId != 0; }

    void updateTargetHP(uint8_t hpPercent);
    void updateTargetLevel(uint8_t level);

    // ========== Auto-Attack ==========

    bool isAutoAttacking() const { return m_autoAttacking; }
    void setAutoAttacking(bool attacking);

    // ========== Combat Movement ==========

    const std::string& combatTarget() const { return m_combatTarget; }
    void setCombatTarget(const std::string& name) { m_combatTarget = name; }
    void clearCombatTarget() { m_combatTarget.clear(); }
    bool hasCombatTarget() const { return !m_combatTarget.empty(); }

    float combatStopDistance() const { return m_combatStopDistance; }
    void setCombatStopDistance(float distance) { m_combatStopDistance = distance; }

    bool inCombatMovement() const { return m_inCombatMovement; }
    void setInCombatMovement(bool moving) { m_inCombatMovement = moving; }

    std::chrono::steady_clock::time_point lastCombatMovementUpdate() const { return m_lastCombatMovementUpdate; }
    void setLastCombatMovementUpdate(std::chrono::steady_clock::time_point time) { m_lastCombatMovementUpdate = time; }

    // ========== Combat History ==========

    // Last slain entity name (for death messages)
    const std::string& lastSlainEntityName() const { return m_lastSlainEntityName; }
    void setLastSlainEntityName(const std::string& name) { m_lastSlainEntityName = name; }

    // ========== Reset ==========

    // Reset combat state (for zone changes, death, etc.)
    void reset();

private:
    void fireTargetChangedEvent();
    void fireCombatEvent();

    EventBus* m_eventBus = nullptr;

    // Targeting
    uint16_t m_targetId = 0;
    std::string m_targetName;
    uint8_t m_targetHPPercent = 100;
    uint8_t m_targetLevel = 0;

    // Auto-attack
    bool m_autoAttacking = false;

    // Combat movement
    std::string m_combatTarget;
    float m_combatStopDistance = 0.0f;
    bool m_inCombatMovement = false;
    std::chrono::steady_clock::time_point m_lastCombatMovementUpdate;

    // Combat history
    std::string m_lastSlainEntityName;
};

} // namespace state
} // namespace eqt
