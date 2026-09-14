#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <chrono>

#include "common/packets/common.h"  // T483: the gem counts

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

// Gem storage is EQT::SPELL_GEM_COUNT (T483: all nine Titanium gems).

// Spellbook slot count
constexpr uint16_t SPELLBOOK_SLOT_COUNT = 400;

// Invalid/unknown spell ID
constexpr uint32_t SPELL_ID_UNKNOWN = 0xFFFFFFFF;

/**
 * Gem state enumeration (matching spell_constants.h GemState).
 */
enum class SpellGemState : uint8_t {
    Empty = 0,              // No spell memorized
    Ready = 1,              // Spell ready to cast
    Casting = 2,            // Currently casting this spell
    Refresh = 3,            // On cooldown after cast
    MemorizeProgress = 4,   // Being memorized
};

/**
 * SpellState - Contains spell system state data for event-driven updates.
 *
 * This class tracks spell gem states, casting state, and spellbook summary
 * information. It can be synchronized from the SpellManager and provides
 * event notifications for UI updates.
 */
class SpellState {
public:
    SpellState();
    ~SpellState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Spell Gems ==========

    /**
     * Get spell ID in a gem slot.
     * @param gemSlot Gem slot (0-8)
     * @return Spell ID, or SPELL_ID_UNKNOWN if empty
     */
    uint32_t getGemSpellId(uint8_t gemSlot) const;

    /**
     * Get gem state.
     * @param gemSlot Gem slot (0-8)
     */
    SpellGemState getGemState(uint8_t gemSlot) const;

    /**
     * Get cooldown progress for a gem (0.0 = just cast, 1.0 = ready).
     * T44: computed on read from a stored start-time + total, not from a
     * per-tick poll -- see setGemCooldown().
     * @param gemSlot Gem slot (0-8)
     */
    float getGemCooldownProgress(uint8_t gemSlot) const;

    /**
     * Get remaining cooldown in milliseconds. Computed on read (T44); see
     * getGemCooldownProgress().
     * @param gemSlot Gem slot (0-8)
     */
    uint32_t getGemCooldownRemaining(uint8_t gemSlot) const;

    /**
     * Check if a gem slot has a spell memorized.
     * @param gemSlot Gem slot (0-8)
     */
    bool hasSpellMemorized(uint8_t gemSlot) const;

    /**
     * Check if a gem is ready to cast.
     * @param gemSlot Gem slot (0-8)
     */
    bool isGemReady(uint8_t gemSlot) const;

    /**
     * Set gem spell and state.
     * Fires SpellGemChanged event if changed.
     * @param gemSlot Gem slot (0-8)
     * @param spellId Spell ID (or SPELL_ID_UNKNOWN to clear)
     * @param state Gem state
     */
    void setGem(uint8_t gemSlot, uint32_t spellId, SpellGemState state);

    /**
     * Set gem cooldown -- called once, at the moment a gem enters cooldown
     * (from the SpellGemChanged event that fires on that transition), not
     * once per tick. Internally stores a start-time backdated by
     * (totalMs - remainingMs) so getGemCooldownRemaining()/
     * getGemCooldownProgress() can compute the current value on read with no
     * further calls needed -- T44's countdown model (see
     * refactor/T44_spell_state_via_bridge.md): publish once, consumer
     * computes elapsed.
     * @param gemSlot Gem slot (0-8)
     * @param remainingMs Remaining cooldown in milliseconds as of now
     * @param totalMs Total cooldown duration in milliseconds
     */
    void setGemCooldown(uint8_t gemSlot, uint32_t remainingMs, uint32_t totalMs);

    /**
     * Clear a gem slot.
     * Fires SpellGemChanged event.
     * @param gemSlot Gem slot (0-8)
     */
    void clearGem(uint8_t gemSlot);

    // ========== Casting State ==========

    /**
     * Check if currently casting a spell.
     */
    bool isCasting() const { return m_isCasting; }

    /**
     * Get the spell ID being cast.
     */
    uint32_t castingSpellId() const { return m_castingSpellId; }

    /**
     * Get the target ID of the spell being cast.
     */
    uint16_t castingTargetId() const { return m_castingTargetId; }

    /**
     * Get cast progress (0.0 = just started, 1.0 = complete). Computed on
     * read from a stored start-time + total (T44) -- there is no per-tick
     * update call.
     */
    float castProgress() const;

    /**
     * Get remaining cast time in milliseconds. Computed on read (T44); see
     * castProgress().
     */
    uint32_t castTimeRemaining() const;

    /**
     * Get total cast time in milliseconds.
     */
    uint32_t castTimeTotal() const { return m_castTimeTotal; }

    /**
     * Set casting state. Records the current time as the cast's start (when
     * casting is true); castTimeRemaining()/castProgress() compute elapsed
     * from it on every read, so nothing needs to call this again until the
     * cast actually starts, restarts (e.g. server corrects the cast time),
     * or ends. Fires CastingStateChanged event if isCasting or spellId
     * changed.
     */
    void setCasting(bool casting, uint32_t spellId = SPELL_ID_UNKNOWN,
                    uint16_t targetId = 0, uint32_t castTimeMs = 0);

    /**
     * Clear casting state.
     */
    void clearCasting();

    // ========== Memorization State ==========

    /**
     * Check if currently memorizing a spell.
     */
    bool isMemorizing() const { return m_isMemorizing; }

    /**
     * Get the gem slot being memorized to.
     */
    uint8_t memorizingGemSlot() const { return m_memorizingGemSlot; }

    /**
     * Get the spell ID being memorized.
     */
    uint32_t memorizingSpellId() const { return m_memorizingSpellId; }

    /**
     * Get memorization progress (0.0 = just started, 1.0 = complete).
     */
    float memorizeProgress() const;

    /**
     * Set memorization state.
     */
    void setMemorizing(bool memorizing, uint8_t gemSlot = 0,
                       uint32_t spellId = SPELL_ID_UNKNOWN, uint32_t totalMs = 0);

    /**
     * Update memorization progress.
     */
    void updateMemorizeProgress(uint32_t remainingMs);

    // ========== Spellbook Summary ==========

    /**
     * Get count of spells scribed in spellbook.
     */
    uint16_t scribedSpellCount() const { return m_scribedSpellCount; }

    /**
     * Check if a specific spell is scribed.
     */
    bool hasSpellScribed(uint32_t spellId) const;

    /**
     * Set spellbook summary.
     * @param count Number of spells scribed
     */
    void setScribedSpellCount(uint16_t count);

    /**
     * Mark a spell as scribed (for quick lookup).
     * Limited to tracking a subset of spells.
     */
    void addScribedSpell(uint32_t spellId);

    /**
     * Clear scribed spell tracking.
     */
    void clearScribedSpells();

    // ========== Reset ==========

    /**
     * Reset all spell state.
     */
    void reset();

private:
    void fireSpellGemChangedEvent(uint8_t gemSlot);
    void fireCastingStateChangedEvent();
    void fireSpellMemorizingEvent();

    EventBus* m_eventBus = nullptr;

    // Spell gem data. T44: cooldown is modelled as a start-time + total,
    // computed on read (getGemCooldownRemaining/Progress), rather than a
    // remaining-ms value that something has to keep polling to update.
    struct GemData {
        uint32_t spellId = SPELL_ID_UNKNOWN;
        SpellGemState state = SpellGemState::Empty;
        std::chrono::steady_clock::time_point cooldownStartTime{};
        uint32_t cooldownTotalMs = 0;
    };
    std::array<GemData, EQT::SPELL_GEM_COUNT> m_gems;

    // Casting state. T44: same start-time + total model as gem cooldowns --
    // castTimeRemaining()/castProgress() compute elapsed on read.
    bool m_isCasting = false;
    uint32_t m_castingSpellId = SPELL_ID_UNKNOWN;
    uint16_t m_castingTargetId = 0;
    std::chrono::steady_clock::time_point m_castStartTime{};
    uint32_t m_castTimeTotal = 0;

    // Memorization state
    bool m_isMemorizing = false;
    uint8_t m_memorizingGemSlot = 0;
    uint32_t m_memorizingSpellId = SPELL_ID_UNKNOWN;
    uint32_t m_memorizeTimeRemaining = 0;
    uint32_t m_memorizeTimeTotal = 0;

    // Spellbook summary
    uint16_t m_scribedSpellCount = 0;

    // Quick lookup for recently used spells (limited set)
    static constexpr size_t MAX_TRACKED_SPELLS = 64;
    std::array<uint32_t, MAX_TRACKED_SPELLS> m_trackedScribedSpells;
    size_t m_trackedSpellCount = 0;
};

} // namespace state
} // namespace eqt
