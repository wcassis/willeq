#include "client/state/spell_state.h"
#include "client/state/event_bus.h"
#include <algorithm>
#include <chrono>

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
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - gem.cooldownStartTime).count();
    if (elapsedMs < 0) {
        elapsedMs = 0;
    }
    float progress = static_cast<float>(elapsedMs) / static_cast<float>(gem.cooldownTotalMs);
    return std::clamp(progress, 0.0f, 1.0f);
}

uint32_t SpellState::getGemCooldownRemaining(uint8_t gemSlot) const {
    if (gemSlot >= SPELL_GEM_COUNT) {
        return 0;
    }
    const auto& gem = m_gems[gemSlot];
    if (gem.cooldownTotalMs == 0) {
        return 0;
    }
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - gem.cooldownStartTime).count();
    if (elapsedMs < 0) {
        elapsedMs = 0;
    }
    if (static_cast<uint32_t>(elapsedMs) >= gem.cooldownTotalMs) {
        return 0;
    }
    return gem.cooldownTotalMs - static_cast<uint32_t>(elapsedMs);
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
    gem.cooldownTotalMs = totalMs;
    // Backdate the start time by however much of the cooldown had already
    // elapsed as of the moment this was called, so a single call is enough
    // -- getGemCooldownRemaining()/getGemCooldownProgress() derive the
    // current value from wall-clock time on every read from here on (T44).
    uint32_t elapsedMs = (totalMs > remainingMs) ? (totalMs - remainingMs) : 0;
    gem.cooldownStartTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(elapsedMs);

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
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_castStartTime).count();
    if (elapsedMs < 0) {
        elapsedMs = 0;
    }
    float progress = static_cast<float>(elapsedMs) / static_cast<float>(m_castTimeTotal);
    return std::clamp(progress, 0.0f, 1.0f);
}

uint32_t SpellState::castTimeRemaining() const {
    if (!m_isCasting) {
        return 0;
    }
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_castStartTime).count();
    if (elapsedMs < 0) {
        elapsedMs = 0;
    }
    if (static_cast<uint32_t>(elapsedMs) >= m_castTimeTotal) {
        return 0;
    }
    return m_castTimeTotal - static_cast<uint32_t>(elapsedMs);
}

void SpellState::setCasting(bool casting, uint32_t spellId, uint16_t targetId, uint32_t castTimeMs) {
    bool changed = (m_isCasting != casting || m_castingSpellId != spellId);

    m_isCasting = casting;
    m_castingSpellId = spellId;
    m_castingTargetId = targetId;
    m_castTimeTotal = castTimeMs;
    m_castStartTime = std::chrono::steady_clock::now();

    if (changed) {
        fireCastingStateChangedEvent();
    }
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
    fireSpellMemorizingEvent();
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
        gem.cooldownStartTime = std::chrono::steady_clock::time_point{};
        gem.cooldownTotalMs = 0;
    }

    // Clear casting state
    m_isCasting = false;
    m_castingSpellId = SPELL_ID_UNKNOWN;
    m_castingTargetId = 0;
    m_castStartTime = std::chrono::steady_clock::time_point{};
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
    event.cooldownRemainingMs = getGemCooldownRemaining(gemSlot);
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
    event.castTimeRemainingMs = castTimeRemaining();
    event.castTimeTotalMs = m_castTimeTotal;
    m_eventBus->publish(GameEventType::CastingStateChanged, event);
}

void SpellState::fireSpellMemorizingEvent() {
    if (!m_eventBus) {
        return;
    }

    SpellMemorizingData event;
    event.isMemorizing = m_isMemorizing;
    event.gemSlot = m_memorizingGemSlot;
    event.spellId = m_memorizingSpellId;
    event.progressMs = m_memorizeTimeTotal - m_memorizeTimeRemaining;
    event.totalMs = m_memorizeTimeTotal;
    m_eventBus->publish(GameEventType::SpellMemorizing, event);
}

} // namespace state
} // namespace eqt
