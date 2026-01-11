#include "client/state/spell_state.h"
#include "client/state/event_bus.h"
#include <algorithm>

namespace eqt {
namespace state {

SpellState::SpellState() {
    m_trackedScribedSpells.fill(SPELL_ID_UNKNOWN);
}

// ========== Spell Gems ==========

uint32_t SpellState::getGemSpellId(uint8_t gemSlot) const {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return SPELL_ID_UNKNOWN;
    }
    return m_gems[gemSlot].spellId;
}

SpellGemState SpellState::getGemState(uint8_t gemSlot) const {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return SpellGemState::Empty;
    }
    return m_gems[gemSlot].state;
}

float SpellState::getGemCooldownProgress(uint8_t gemSlot) const {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return 1.0f;
    }
    const auto& gem = m_gems[gemSlot];
    if (gem.cooldownTotalMs == 0) {
        return 1.0f;
    }
    float elapsed = static_cast<float>(gem.cooldownTotalMs - gem.cooldownRemainingMs);
    return elapsed / static_cast<float>(gem.cooldownTotalMs);
}

uint32_t SpellState::getGemCooldownRemaining(uint8_t gemSlot) const {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return 0;
    }
    return m_gems[gemSlot].cooldownRemainingMs;
}

bool SpellState::hasSpellMemorized(uint8_t gemSlot) const {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return false;
    }
    return m_gems[gemSlot].spellId != SPELL_ID_UNKNOWN;
}

bool SpellState::isGemReady(uint8_t gemSlot) const {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return false;
    }
    return m_gems[gemSlot].state == SpellGemState::Ready;
}

void SpellState::setGem(uint8_t gemSlot, uint32_t spellId, SpellGemState state) {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return;
    }

    auto& gem = m_gems[gemSlot];
    bool changed = (gem.spellId != spellId || gem.state != state);

    gem.spellId = spellId;
    gem.state = state;

    if (state == SpellGemState::Ready || state == SpellGemState::Empty) {
        gem.cooldownRemainingMs = 0;
        gem.cooldownTotalMs = 0;
    }

    if (changed) {
        fireSpellGemChangedEvent(gemSlot);
    }
}

void SpellState::setGemCooldown(uint8_t gemSlot, uint32_t remainingMs, uint32_t totalMs) {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return;
    }

    auto& gem = m_gems[gemSlot];
    gem.cooldownRemainingMs = remainingMs;
    gem.cooldownTotalMs = totalMs;

    if (remainingMs > 0) {
        gem.state = SpellGemState::Refresh;
    } else if (gem.spellId != SPELL_ID_UNKNOWN) {
        gem.state = SpellGemState::Ready;
    }
}

void SpellState::clearGem(uint8_t gemSlot) {
    setGem(gemSlot, SPELL_ID_UNKNOWN, SpellGemState::Empty);
}

// ========== Casting State ==========

float SpellState::castProgress() const {
    if (!m_isCasting || m_castTimeTotal == 0) {
        return 0.0f;
    }
    float elapsed = static_cast<float>(m_castTimeTotal - m_castTimeRemaining);
    return elapsed / static_cast<float>(m_castTimeTotal);
}

void SpellState::setCasting(bool casting, uint32_t spellId, uint16_t targetId, uint32_t castTimeMs) {
    bool changed = (m_isCasting != casting || m_castingSpellId != spellId);

    m_isCasting = casting;
    m_castingSpellId = spellId;
    m_castingTargetId = targetId;
    m_castTimeTotal = castTimeMs;
    m_castTimeRemaining = castTimeMs;

    if (changed) {
        fireCastingStateChangedEvent();
    }
}

void SpellState::updateCastProgress(uint32_t remainingMs) {
    m_castTimeRemaining = remainingMs;
}

void SpellState::clearCasting() {
    setCasting(false);
}

// ========== Memorization State ==========

float SpellState::memorizeProgress() const {
    if (!m_isMemorizing || m_memorizeTimeTotal == 0) {
        return 0.0f;
    }
    float elapsed = static_cast<float>(m_memorizeTimeTotal - m_memorizeTimeRemaining);
    return elapsed / static_cast<float>(m_memorizeTimeTotal);
}

void SpellState::setMemorizing(bool memorizing, uint8_t gemSlot, uint32_t spellId, uint32_t totalMs) {
    m_isMemorizing = memorizing;
    m_memorizingGemSlot = gemSlot;
    m_memorizingSpellId = spellId;
    m_memorizeTimeTotal = totalMs;
    m_memorizeTimeRemaining = totalMs;

    if (memorizing && gemSlot < SPELL_GEM_COUNT) {
        m_gems[gemSlot].state = SpellGemState::MemorizeProgress;
        fireSpellGemChangedEvent(gemSlot);
    }
}

void SpellState::updateMemorizeProgress(uint32_t remainingMs) {
    m_memorizeTimeRemaining = remainingMs;
}

// ========== Spellbook Summary ==========

bool SpellState::hasSpellScribed(uint32_t spellId) const {
    if (spellId == SPELL_ID_UNKNOWN) {
        return false;
    }

    for (size_t i = 0; i < m_trackedSpellCount; ++i) {
        if (m_trackedScribedSpells[i] == spellId) {
            return true;
        }
    }
    return false;
}

void SpellState::setScribedSpellCount(uint16_t count) {
    m_scribedSpellCount = count;
}

void SpellState::addScribedSpell(uint32_t spellId) {
    if (spellId == SPELL_ID_UNKNOWN) {
        return;
    }

    // Check if already tracked
    for (size_t i = 0; i < m_trackedSpellCount; ++i) {
        if (m_trackedScribedSpells[i] == spellId) {
            return;
        }
    }

    // Add if space available
    if (m_trackedSpellCount < MAX_TRACKED_SPELLS) {
        m_trackedScribedSpells[m_trackedSpellCount++] = spellId;
    }
}

void SpellState::clearScribedSpells() {
    m_trackedScribedSpells.fill(SPELL_ID_UNKNOWN);
    m_trackedSpellCount = 0;
    m_scribedSpellCount = 0;
}

// ========== Reset ==========

void SpellState::reset() {
    // Clear all gems
    for (auto& gem : m_gems) {
        gem.spellId = SPELL_ID_UNKNOWN;
        gem.state = SpellGemState::Empty;
        gem.cooldownRemainingMs = 0;
        gem.cooldownTotalMs = 0;
    }

    // Clear casting state
    m_isCasting = false;
    m_castingSpellId = SPELL_ID_UNKNOWN;
    m_castingTargetId = 0;
    m_castTimeRemaining = 0;
    m_castTimeTotal = 0;

    // Clear memorization state
    m_isMemorizing = false;
    m_memorizingGemSlot = 0;
    m_memorizingSpellId = SPELL_ID_UNKNOWN;
    m_memorizeTimeRemaining = 0;
    m_memorizeTimeTotal = 0;

    // Clear spellbook tracking
    clearScribedSpells();
}

// ========== Event Firing ==========

void SpellState::fireSpellGemChangedEvent(uint8_t gemSlot) {
    if (!m_eventBus || gemSlot >= SPELL_GEM_COUNT) {
        return;
    }

    SpellGemChangedData event;
    event.gemSlot = gemSlot;
    event.spellId = m_gems[gemSlot].spellId;
    event.gemState = static_cast<uint8_t>(m_gems[gemSlot].state);
    event.cooldownRemainingMs = m_gems[gemSlot].cooldownRemainingMs;
    m_eventBus->publish(GameEventType::SpellGemChanged, event);
}

void SpellState::fireCastingStateChangedEvent() {
    if (!m_eventBus) {
        return;
    }

    CastingStateChangedData event;
    event.isCasting = m_isCasting;
    event.spellId = m_castingSpellId;
    event.targetId = m_castingTargetId;
    event.castTimeRemainingMs = m_castTimeRemaining;
    event.castTimeTotalMs = m_castTimeTotal;
    m_eventBus->publish(GameEventType::CastingStateChanged, event);
}

} // namespace state
} // namespace eqt
